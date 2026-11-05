/* ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2026 EBC.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_NetworkAdapterEngine.cpp
 *  \ingroup gamelogic
 *
 *  B6.3 LAN network backend for the network nodes.
 *
 *  Scope: one LAN room, star topology, raw BSD sockets, no third party
 *  libraries. There is no NAT traversal, no rollback and no prediction.
 *  All sockets are non-blocking; the only bounded wait is the welcome timeout
 *  inside JoinRoom (100 ms) and the connect timeout (300 ms).
 */

#include "KX_GameAdapters.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET SocketHandle;
#  define KX_INVALID_SOCKET INVALID_SOCKET
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
typedef int SocketHandle;
#  define KX_INVALID_SOCKET (-1)
#endif

namespace {

const uint32_t kMagic = 0x42364E31u; /* "B6N1" */
/* Protocol v2: WELCOME carries the host UDP port and HELLO answers with the
 * client UDP port. Headers, the six v1 packet types and their payloads are
 * unchanged; v1 peers are rejected cleanly by the version check. */
const uint16_t kVersion = 2;
const size_t kHeaderSize = 16;
const size_t kMaxFrameBytes = 64 * 1024;
/* UDP datagrams carry the same 16 byte header; 65507 is the IPv4 maximum. */
const size_t kMaxDatagramBytes = 65507;
const size_t kMaxOutboxPackets = 256;
const int kConnectTimeoutMs = 300;
const int kWelcomeTimeoutMs = 100;
const int kRoomListTimeoutMs = 300;

enum PacketType : uint8_t {
  PKT_HELLO = 1,
  PKT_WELCOME = 2,
  PKT_JOIN = 3,
  PKT_LEAVE = 4,
  PKT_MESSAGE = 5,
  PKT_PROPERTY = 6,
  /* v2 additions, only ever sent over UDP. */
  PKT_ROOMLIST = 7, /* request: empty payload */
  PKT_ROOMINFO = 8, /* reply: name(U16+bytes), players(I32), max(I32) */
};

enum PacketFlags : uint8_t {
  FLAG_RELIABLE = 1 << 0,
};

void AppendU8(std::string &out, uint8_t value)
{
  out.push_back(char(value));
}

void AppendU16(std::string &out, uint16_t value)
{
  out.push_back(char(value & 0xFF));
  out.push_back(char((value >> 8) & 0xFF));
}

void AppendU32(std::string &out, uint32_t value)
{
  out.push_back(char(value & 0xFF));
  out.push_back(char((value >> 8) & 0xFF));
  out.push_back(char((value >> 16) & 0xFF));
  out.push_back(char((value >> 24) & 0xFF));
}

void AppendI32(std::string &out, int32_t value)
{
  AppendU32(out, uint32_t(value));
}

bool ReadU8(const std::string &buffer, size_t &cursor, uint8_t &out)
{
  if (cursor + 1 > buffer.size()) {
    return false;
  }
  out = uint8_t(buffer[cursor]);
  cursor += 1;
  return true;
}

bool ReadU16(const std::string &buffer, size_t &cursor, uint16_t &out)
{
  if (cursor + 2 > buffer.size()) {
    return false;
  }
  out = uint16_t(uint32_t(buffer[cursor]) | (uint32_t(buffer[cursor + 1]) << 8));
  cursor += 2;
  return true;
}

bool ReadU32(const std::string &buffer, size_t &cursor, uint32_t &out)
{
  if (cursor + 4 > buffer.size()) {
    return false;
  }
  out = uint32_t(buffer[cursor]) | (uint32_t(buffer[cursor + 1]) << 8) |
        (uint32_t(buffer[cursor + 2]) << 16) | (uint32_t(buffer[cursor + 3]) << 24);
  cursor += 4;
  return true;
}

bool ReadI32(const std::string &buffer, size_t &cursor, int32_t &out)
{
  uint32_t raw = 0;
  if (!ReadU32(buffer, cursor, raw)) {
    return false;
  }
  out = int32_t(raw);
  return true;
}

bool ReadString(const std::string &buffer, size_t &cursor, std::size_t &out_length)
{
  uint16_t length = 0;
  if (!ReadU16(buffer, cursor, length)) {
    return false;
  }
  if (cursor + length > buffer.size()) {
    return false;
  }
  out_length = length;
  return true;
}

std::string EncodeValue(const KX_RuntimeValue &value)
{
  std::string out;
  switch (value.type) {
    case KX_VALUE_FLOAT: {
      AppendU8(out, 'f');
      float number = value.AsFloat();
      /* IEEE-754 bit copy keeps the payload compact and avoids locale issues. */
      uint32_t bits = 0;
      std::memcpy(&bits, &number, sizeof(bits));
      AppendU32(out, bits);
      break;
    }
    case KX_VALUE_INT: {
      AppendU8(out, 'i');
      AppendI32(out, int32_t(value.AsInt()));
      break;
    }
    case KX_VALUE_BOOL: {
      AppendU8(out, 'b');
      AppendU8(out, value.AsBool() ? 1 : 0);
      break;
    }
    case KX_VALUE_STRING:
    default: {
      AppendU8(out, 's');
      const char *text = value.AsString();
      std::string stored = text != nullptr ? text : std::string();
      if (stored.size() > 0xFFFF) {
        stored.resize(0xFFFF);
      }
      AppendU16(out, uint16_t(stored.size()));
      out.append(stored);
      break;
    }
  }
  return out;
}

bool DecodeValue(const std::string &buffer, size_t cursor, KX_RuntimeValue &out, std::string &string_storage)
{
  uint8_t tag = 0;
  if (!ReadU8(buffer, cursor, tag)) {
    return false;
  }
  switch (tag) {
    case 'f': {
      uint32_t bits = 0;
      if (!ReadU32(buffer, cursor, bits)) {
        return false;
      }
      float number = 0.0f;
      std::memcpy(&number, &bits, sizeof(number));
      out = KX_RuntimeValue::Float(number);
      return true;
    }
    case 'i': {
      int32_t number = 0;
      if (!ReadI32(buffer, cursor, number)) {
        return false;
      }
      out = KX_RuntimeValue::Int(int(number));
      return true;
    }
    case 'b': {
      uint8_t flag = 0;
      if (!ReadU8(buffer, cursor, flag)) {
        return false;
      }
      out = KX_RuntimeValue::Bool(flag != 0);
      return true;
    }
    case 's': {
      std::size_t length = 0;
      if (!ReadString(buffer, cursor, length)) {
        return false;
      }
      string_storage.assign(buffer, cursor, length);
      cursor += length;
      /* KX_RuntimeValue string slots carry an interned char pointer. The
       * adapter owns the decoded copy for as long as the remote snapshot holds
       * it, so the pointer stays valid. */
      out.type = KX_VALUE_STRING;
      out.data.ptr = string_storage.empty() ? nullptr : (void *)string_storage.data();
      return true;
    }
    default:
      return false;
  }
}

std::string BuildPacket(uint8_t type, uint8_t flags, int sender, const std::string &payload)
{
  std::string packet;
  AppendU32(packet, kMagic);
  AppendU16(packet, kVersion);
  AppendU8(packet, type);
  AppendU8(packet, flags);
  AppendI32(packet, int32_t(sender));
  AppendU32(packet, uint32_t(payload.size()));
  packet.append(payload);
  return packet;
}

bool ParsePacketHeader(const std::string &packet,
                       size_t offset,
                       uint8_t &type,
                       uint8_t &flags,
                       int &sender,
                       uint32_t &payload_length)
{
  size_t cursor = offset;
  uint32_t magic = 0;
  uint16_t version = 0;
  int32_t sender_raw = 0;
  if (!ReadU32(packet, cursor, magic) || !ReadU16(packet, cursor, version) ||
      !ReadU8(packet, cursor, type) || !ReadU8(packet, cursor, flags) ||
      !ReadI32(packet, cursor, sender_raw) || !ReadU32(packet, cursor, payload_length)) {
    return false;
  }
  if (magic != kMagic || version != kVersion) {
    return false;
  }
  sender = int(sender_raw);
  return true;
}

void SetNonBlocking(SocketHandle socket)
{
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(socket, FIONBIO, &mode);
#else
  fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#endif
}

void CloseSocket(SocketHandle &socket)
{
  if (socket == KX_INVALID_SOCKET) {
    return;
  }
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
  socket = KX_INVALID_SOCKET;
}

std::string SocketErrorText()
{
#ifdef _WIN32
  return std::string("socket error");
#else
  return std::string(std::strerror(errno));
#endif
}

bool PollReady(SocketHandle socket, int events, int timeout_ms)
{
#ifdef _WIN32
  WSAPOLLFD entry;
  entry.fd = socket;
  entry.events = short(events);
  entry.revents = 0;
  return WSAPoll(&entry, 1, timeout_ms) > 0 && entry.revents != 0;
#else
  struct pollfd entry;
  entry.fd = socket;
  entry.events = short(events);
  entry.revents = 0;
  return poll(&entry, 1, timeout_ms) > 0 && entry.revents != 0;
#endif
}

const int kPollIn =
#ifdef _WIN32
    POLLRDNORM;
const int kPollOut = POLLWRNORM;
#else
    POLLIN;
const int kPollOut = POLLOUT;
#endif

bool SameEndpoint(const sockaddr_in &a, const sockaddr_in &b)
{
  return a.sin_family == b.sin_family && a.sin_port == b.sin_port &&
         a.sin_addr.s_addr == b.sin_addr.s_addr;
}

void SleepMs(int milliseconds)
{
  if (milliseconds <= 0) {
    return;
  }
#ifdef _WIN32
  Sleep(DWORD(milliseconds));
#else
  struct timespec pause;
  pause.tv_sec = milliseconds / 1000;
  pause.tv_nsec = long(milliseconds % 1000) * 1000 * 1000;
  nanosleep(&pause, nullptr);
#endif
}

}  // namespace

/**
 * TCP star-topology backend used by the LAN network nodes.
 *
 * Host: one listening socket, one socket per client. Clients connect to the
 * host and receive their player id from a WELCOME frame. Messages addressed to
 * another client are relayed by the host.
 */
class EngineNetworkAdapter final : public KX_NetworkAdapter {
 public:
  ~EngineNetworkAdapter() override
  {
    Shutdown();
  }

  bool HostRoom(int port, int max_players, std::string *error) override
  {
    if (role_ != Role::None) {
      return role_ == Role::Host && host_port_ == port;
    }
    if (port <= 0 || port > 65535) {
      SetError("HostRoom: port must be 1..65535");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    if (max_players < 1) {
      SetError("HostRoom: max_players must be at least 1 (the host counts)");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == KX_INVALID_SOCKET) {
      SetError(std::string("HostRoom: socket() failed: ") + SocketErrorText());
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    int reuse = 1;
    setsockopt(listener,
               SOL_SOCKET,
               SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse),
               sizeof(reuse));

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(uint16_t(port));

    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
      SetError(std::string("HostRoom: bind() failed on port ") + std::to_string(port) + ": " +
               SocketErrorText());
      CloseSocket(listener);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    if (listen(listener, 16) != 0) {
      SetError(std::string("HostRoom: listen() failed: ") + SocketErrorText());
      CloseSocket(listener);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    SetNonBlocking(listener);
    if (!OpenHostUdp(port)) {
      CloseSocket(listener);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    listen_socket_ = listener;
    role_ = Role::Host;
    local_id_ = 0;
    max_players_ = max_players;
    host_port_ = port;
    connected_ = true;
    last_error_.clear();
    return true;
  }

  bool JoinRoom(const std::string &host, int port, std::string *error) override
  {
    if (role_ == Role::Client && connected_) {
      return true;
    }
    if (role_ == Role::Host) {
      SetError("JoinRoom: already hosting a room");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    if (host.empty() || port <= 0 || port > 65535) {
      SetError("JoinRoom: host and port are required");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    SocketHandle socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle == KX_INVALID_SOCKET) {
      SetError(std::string("JoinRoom: socket() failed: ") + SocketErrorText());
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    SetNonBlocking(socket_handle);

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(uint16_t(port));
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
      SetError("JoinRoom: host must be a numeric IPv4 address");
      CloseSocket(socket_handle);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    int result = connect(socket_handle, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (result != 0) {
#ifndef _WIN32
      if (errno != EINPROGRESS) {
        SetError(std::string("JoinRoom: connect() failed: ") + SocketErrorText());
        CloseSocket(socket_handle);
        if (error != nullptr) {
          *error = last_error_;
        }
        return false;
      }
#endif
      if (!PollReady(socket_handle, kPollOut, kConnectTimeoutMs)) {
        SetError("JoinRoom: connect timed out after 300 ms");
        CloseSocket(socket_handle);
        if (error != nullptr) {
          *error = last_error_;
        }
        return false;
      }
#ifndef _WIN32
      int pending_error = 0;
      int option_length = sizeof(pending_error);
      getsockopt(socket_handle,
                 SOL_SOCKET,
                 SO_ERROR,
                 &pending_error,
                 reinterpret_cast<unsigned int *>(&option_length));
      if (pending_error != 0) {
        SetError(std::string("JoinRoom: connect() failed: ") + std::strerror(pending_error));
        CloseSocket(socket_handle);
        if (error != nullptr) {
          *error = last_error_;
        }
        return false;
      }
#endif
    }

    last_join_host_ = host;
    last_join_port_ = port;

    if (!OpenClientUdp(address)) {
      CloseSocket(socket_handle);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    HostPeer host_peer;
    host_peer.socket = socket_handle;
    host_peer.id = 0;
    host_peer.address = address;
    peers_.push_back(host_peer);
    role_ = Role::Client;
    connected_ = true;
    local_id_ = -1;
    player_count_ = 1;

    /* Bounded welcome wait: the host assigns the client id, so wait at most
     * 100 ms for the WELCOME frame. This is short and never blocks the logic
     * thread for longer. */
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kWelcomeTimeoutMs);
    while (local_id_ < 0 && std::chrono::steady_clock::now() < deadline) {
      Pump();
      if (local_id_ >= 0) {
        break;
      }
#ifdef _WIN32
      Sleep(5);
#else
      struct timespec pause;
      pause.tv_sec = 0;
      pause.tv_nsec = 5 * 1000 * 1000;
      nanosleep(&pause, nullptr);
#endif
    }
    if (local_id_ < 0) {
      /* Silent half-open connections are not useful: report the timeout as a
       * join failure so callers (and Reconnect) can act on it. */
      SetError("JoinRoom: no WELCOME within 100 ms");
      ResetClientState();
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    last_error_.clear();
    return true;
  }

  void LeaveRoom() override
  {
    if (connected_ && local_id_ >= 0) {
      std::string payload;
      AppendI32(payload, int32_t(local_id_));
      std::string packet = BuildPacket(PKT_LEAVE, FLAG_RELIABLE, local_id_, payload);
      for (HostPeer &peer : peers_) {
        QueuePacket(peer, packet, true);
      }
      FlushPeer(peers_[0]);
    }
    Shutdown();
    role_ = Role::None;
    connected_ = false;
    local_id_ = -1;
    player_count_ = 0;
    /* An explicit leave also forgets where to reconnect to. */
    last_join_host_.clear();
    last_join_port_ = 0;
  }

  int PlayerCount() const override { return player_count_; }

  int LocalPlayerId() const override { return local_id_; }

  bool SendReliable(int target_id, const void *data, size_t len) override
  {
    return Send(target_id, data, len, true);
  }

  bool SendUnreliable(int target_id, const void *data, size_t len) override
  {
    if (!connected_ || data == nullptr || local_id_ < 0) {
      return false;
    }
    if (udp_socket_ == KX_INVALID_SOCKET) {
      SetError("SendUnreliable: UDP socket is not open");
      return false;
    }
    if (len + kHeaderSize > kMaxDatagramBytes) {
      SetError("SendUnreliable: payload exceeds the UDP datagram limit");
      return false;
    }
    std::string payload(static_cast<const char *>(data), len);
    std::string packet = BuildPacket(PKT_MESSAGE, 0, local_id_, payload);

    if (role_ == Role::Host) {
      if (target_id == 0 || target_id == local_id_) {
        return true; /* Sending to the host itself is a no-op success. */
      }
      HostPeer *target = FindPeer(target_id);
      if (target == nullptr) {
        SetError("SendUnreliable: unknown target player " + std::to_string(target_id));
        return false;
      }
      if (!target->udp_ready) {
        SetError("SendUnreliable: peer UDP endpoint not registered yet");
        return false;
      }
      return SendDatagram(target->udp_address, packet);
    }

    if (target_id != 0 && target_id != local_id_) {
      SetError("SendUnreliable: clients can only address the host (0) in this LAN build");
      return false;
    }
    if (!host_udp_ready_) {
      SetError("SendUnreliable: host UDP endpoint not registered yet");
      return false;
    }
    return SendDatagram(host_udp_address_, packet);
  }

  /* B6.7 C.3.2 room table and reconnect. */

  void SetRoomName(const std::string &name) override
  {
    if (!name.empty()) {
      room_name_ = name.size() > 0xFFFF ? name.substr(0, 0xFFFF) : name;
    }
  }

  std::string RoomName() const override { return room_name_; }

  bool ListRooms(const std::string &host, int port, std::vector<KX_RoomInfo> &out, std::string *error)
      override
  {
    out.clear();
    if (host.empty() || port <= 0 || port > 65535) {
      SetError("ListRooms: host and port are required");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    SocketHandle probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe == KX_INVALID_SOCKET) {
      SetError(std::string("ListRooms: socket() failed: ") + SocketErrorText());
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    sockaddr_in target;
    std::memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(uint16_t(port));
    if (inet_pton(AF_INET, host.c_str(), &target.sin_addr) != 1) {
      SetError("ListRooms: host must be a numeric IPv4 address");
      CloseSocket(probe);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    /* Bind an ephemeral port so the host can answer the query. */
    sockaddr_in local;
    std::memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(probe, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
      SetError(std::string("ListRooms: bind() failed: ") + SocketErrorText());
      CloseSocket(probe);
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    SetNonBlocking(probe);

    std::string request = BuildPacket(PKT_ROOMLIST, FLAG_RELIABLE, -1, std::string());
    bool sent = SendDatagramTo(probe, target, request);

    std::vector<char> buffer(kMaxDatagramBytes);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kRoomListTimeoutMs);
    bool replied = false;
    while (sent && std::chrono::steady_clock::now() < deadline) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - std::chrono::steady_clock::now())
                                 .count();
      if (!PollReady(probe, kPollIn, int(std::max<long long>(1, remaining)))) {
        continue;
      }
      sockaddr_in source;
      int received = RecvDatagram(probe, buffer, source);
      if (received <= 0) {
        break;
      }
      if (DecodeRoomInfo(std::string(buffer.data(), size_t(received)), host, port, out)) {
        replied = true;
        break;
      }
    }
    CloseSocket(probe);

    if (!sent) {
      SetError("ListRooms: sendto() failed");
    }
    else if (!replied) {
      SetError("ListRooms: no room-list reply within 300 ms");
    }
    if (!replied) {
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    last_error_.clear();
    return true;
  }

  bool RefreshRooms(const std::string &host,
                    int port,
                    std::vector<KX_RoomInfo> &out,
                    std::string *error) override
  {
    return ListRooms(host, port, out, error);
  }

  bool Reconnect(int max_attempts, int backoff_ms, int *attempts, std::string *error) override
  {
    if (attempts != nullptr) {
      *attempts = 0;
    }
    if (role_ == Role::Host) {
      SetError("Reconnect: a host cannot reconnect to itself");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }
    if (role_ == Role::Client && connected_ && local_id_ >= 0) {
      if (error != nullptr) {
        error->clear();
      }
      return true;
    }
    if (last_join_host_.empty() || last_join_port_ <= 0) {
      SetError("Reconnect: no previous JoinRoom to retry");
      if (error != nullptr) {
        *error = last_error_;
      }
      return false;
    }

    const int tries = std::max(1, std::min(max_attempts, 10));
    const int first_backoff = std::max(0, std::min(backoff_ms, 5000));

    for (int attempt = 1; attempt <= tries; ++attempt) {
      if (attempts != nullptr) {
        *attempts = attempt;
      }
      /* Drop the stale client half so the new connection starts clean. */
      ResetClientState();

      std::string join_error;
      if (JoinRoom(last_join_host_, last_join_port_, &join_error) && connected_ &&
          local_id_ >= 0) {
        if (error != nullptr) {
          error->clear();
        }
        return true;
      }
      if (attempt < tries) {
        int wait_ms = first_backoff;
        for (int step = 1; step < attempt; ++step) {
          wait_ms = std::min(4000, wait_ms * 2);
        }
        SleepMs(wait_ms);
      }
    }

    SetError("Reconnect: " + std::to_string(tries) + " attempts failed; last error: " +
             last_error_);
    if (error != nullptr) {
      *error = last_error_;
    }
    return false;
  }

  bool BroadcastReliable(const void *data, size_t len) override
  {
    if (!connected_ || data == nullptr) {
      return false;
    }
    std::string payload(static_cast<const char *>(data), len);
    std::string packet = BuildPacket(PKT_MESSAGE, FLAG_RELIABLE, local_id_, payload);
    bool queued = false;
    for (HostPeer &peer : peers_) {
      if (QueuePacket(peer, packet, true)) {
        queued = true;
      }
    }
    return queued;
  }

  void SetSyncProperty(const std::string &key, const KX_RuntimeValue &value) override
  {
    local_properties_[key] = value;
    if (!connected_ || local_id_ < 0) {
      return;
    }
    remote_properties_[local_id_][key] = PropertyEntry{value, std::string()};
    std::string payload;
    std::string key_text = key.size() > 0xFFFF ? key.substr(0, 0xFFFF) : key;
    AppendU16(payload, uint16_t(key_text.size()));
    payload.append(key_text);
    std::string encoded = EncodeValue(value);
    payload.append(encoded);
    std::string packet = BuildPacket(PKT_PROPERTY, FLAG_RELIABLE, local_id_, payload);
    for (HostPeer &peer : peers_) {
      QueuePacket(peer, packet, true);
    }
  }

  bool GetRemoteProperty(int player_id, const std::string &key, KX_RuntimeValue *out) const override
  {
    const auto player = remote_properties_.find(player_id);
    if (player == remote_properties_.end()) {
      return false;
    }
    const auto property = player->second.find(key);
    if (property == player->second.end()) {
      return false;
    }
    if (out != nullptr) {
      *out = property->second.value;
      if (out->type == KX_VALUE_STRING) {
        const std::string &stored = property->second.storage;
        out->data.ptr = stored.empty() ? nullptr : (void *)stored.data();
      }
    }
    return true;
  }

  bool PollEvent(KX_NetworkEvent &out) override
  {
    if (events_.empty()) {
      return false;
    }
    out = events_.front();
    events_.pop_front();
    return true;
  }

  void Tick() override
  {
    Pump();
    for (HostPeer &peer : peers_) {
      FlushPeer(peer);
    }
  }

  Role GetRole() const override { return role_; }
  bool IsConnected() const override { return connected_; }
  std::string LastError() const override { return last_error_; }

 private:
  struct PropertyEntry {
    KX_RuntimeValue value;
    std::string storage;
  };

  struct HostPeer {
    SocketHandle socket = KX_INVALID_SOCKET;
    int id = -1;
    std::deque<std::string> outbox;
    std::string read_buffer;
    bool alive = true;
    /* TCP source address, reused as the IP half of the UDP endpoint. */
    sockaddr_in address;
    sockaddr_in udp_address;
    uint16_t udp_port = 0;
    bool udp_ready = false;
  };

  typedef std::map<int, std::map<std::string, PropertyEntry>> RemotePropertyMap;

  bool SendDatagramTo(SocketHandle handle, const sockaddr_in &to, const std::string &packet)
  {
    if (handle == KX_INVALID_SOCKET) {
      return false;
    }
#ifdef _WIN32
    int sent = int(sendto(handle,
                          packet.data(),
                          int(packet.size()),
                          0,
                          reinterpret_cast<const sockaddr *>(&to),
                          sizeof(to)));
#else
    ssize_t sent = sendto(handle,
                          packet.data(),
                          packet.size(),
                          0,
                          reinterpret_cast<const sockaddr *>(&to),
                          sizeof(to));
#endif
    return sent >= 0 && size_t(sent) == packet.size();
  }

  bool SendDatagram(const sockaddr_in &to, const std::string &packet)
  {
    return SendDatagramTo(udp_socket_, to, packet);
  }

  int RecvDatagram(SocketHandle handle, std::vector<char> &buffer, sockaddr_in &from)
  {
#ifdef _WIN32
    int length = int(sizeof(sockaddr_in));
    return int(recvfrom(handle,
                        buffer.data(),
                        int(buffer.size()),
                        0,
                        reinterpret_cast<sockaddr *>(&from),
                        &length));
#else
    socklen_t length = sizeof(sockaddr_in);
    return int(recvfrom(handle,
                        buffer.data(),
                        buffer.size(),
                        0,
                        reinterpret_cast<sockaddr *>(&from),
                        &length));
#endif
  }

  bool OpenHostUdp(int port)
  {
    CloseSocket(udp_socket_);
    SocketHandle handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (handle == KX_INVALID_SOCKET) {
      SetError(std::string("HostRoom: UDP socket() failed: ") + SocketErrorText());
      return false;
    }
    int reuse = 1;
    setsockopt(handle,
               SOL_SOCKET,
               SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse),
               sizeof(reuse));
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(uint16_t(port));
    if (bind(handle, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
      SetError(std::string("HostRoom: UDP bind() failed on port ") + std::to_string(port) + ": " +
               SocketErrorText());
      CloseSocket(handle);
      return false;
    }
    SetNonBlocking(handle);
    udp_socket_ = handle;
    host_udp_port_ = port;
    return true;
  }

  bool OpenClientUdp(const sockaddr_in &host_address)
  {
    CloseSocket(udp_socket_);
    SocketHandle handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (handle == KX_INVALID_SOCKET) {
      SetError(std::string("JoinRoom: UDP socket() failed: ") + SocketErrorText());
      return false;
    }
    sockaddr_in local;
    std::memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(handle, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
      SetError(std::string("JoinRoom: UDP bind() failed: ") + SocketErrorText());
      CloseSocket(handle);
      return false;
    }
    sockaddr_in bound;
    std::memset(&bound, 0, sizeof(bound));
#ifdef _WIN32
    int length = int(sizeof(bound));
    getsockname(handle, reinterpret_cast<sockaddr *>(&bound), &length);
#else
    socklen_t length = sizeof(bound);
    getsockname(handle, reinterpret_cast<sockaddr *>(&bound), &length);
#endif
    SetNonBlocking(handle);
    udp_socket_ = handle;
    /* sin_port is network order; the wire format wants the host-order value. */
    client_udp_port_ = uint16_t(ntohs(bound.sin_port));
    host_udp_address_ = host_address;
    host_udp_address_.sin_port = 0; /* Filled in by the WELCOME frame. */
    host_udp_ready_ = false;
    return true;
  }

  void ResetClientState()
  {
    Shutdown();
    role_ = Role::None;
    local_id_ = -1;
    connected_ = false;
    player_count_ = 0;
  }

  void SendHello(HostPeer &peer)
  {
    std::string payload;
    AppendU16(payload, client_udp_port_);
    std::string packet = BuildPacket(PKT_HELLO, FLAG_RELIABLE, local_id_, payload);
    QueuePacket(peer, packet, true);
    FlushPeer(peer);
  }

  bool DecodeRoomInfo(const std::string &datagram,
                      const std::string &host,
                      int port,
                      std::vector<KX_RoomInfo> &out)
  {
    uint8_t type = 0;
    uint8_t flags = 0;
    int sender = 0;
    uint32_t payload_length = 0;
    if (!ParsePacketHeader(datagram, 0, type, flags, sender, payload_length)) {
      return false;
    }
    if (type != PKT_ROOMINFO || datagram.size() != kHeaderSize + payload_length) {
      return false;
    }
    size_t cursor = kHeaderSize;
    std::size_t name_length = 0;
    if (!ReadString(datagram, cursor, name_length)) {
      return false;
    }
    KX_RoomInfo info;
    info.host = host;
    info.port = port;
    info.name.assign(datagram, cursor, name_length);
    cursor += name_length;
    int32_t players = 0;
    int32_t max_players = 0;
    if (!ReadI32(datagram, cursor, players) || !ReadI32(datagram, cursor, max_players)) {
      return false;
    }
    info.players = int(players);
    info.max_players = int(max_players);
    out.clear();
    out.push_back(info);
    return true;
  }

  void SetError(const std::string &text)
  {
    last_error_ = text;
  }

  int FindFreePlayerId() const
  {
    for (int id = 1; id < max_players_; ++id) {
      bool used = false;
      for (const HostPeer &peer : peers_) {
        if (peer.id == id && peer.alive) {
          used = true;
          break;
        }
      }
      if (!used) {
        return id;
      }
    }
    return -1;
  }

  HostPeer *FindPeer(int player_id)
  {
    for (HostPeer &peer : peers_) {
      if (peer.alive && peer.id == player_id) {
        return &peer;
      }
    }
    return nullptr;
  }

  bool QueuePacket(HostPeer &peer, const std::string &packet, bool reliable)
  {
    if (!peer.alive) {
      return false;
    }
    if (reliable) {
      if (peer.outbox.size() >= kMaxOutboxPackets) {
        SetError("network outbox full; reliable message dropped");
        return false;
      }
    }
    peer.outbox.push_back(packet);
    return true;
  }

  bool Send(int target_id, const void *data, size_t len, bool reliable)
  {
    if (!connected_ || data == nullptr || local_id_ < 0) {
      return false;
    }
    if (len > kMaxFrameBytes) {
      SetError("Send: payload exceeds 64 KiB");
      return false;
    }
    std::string payload(static_cast<const char *>(data), len);
    uint8_t flags = reliable ? FLAG_RELIABLE : 0;
    std::string packet = BuildPacket(PKT_MESSAGE, flags, local_id_, payload);

    if (role_ == Role::Host) {
      if (target_id == 0 || target_id == local_id_) {
        return true; /* Sending to the host itself is a no-op success. */
      }
      HostPeer *target = FindPeer(target_id);
      if (target == nullptr) {
        SetError("Send: unknown target player " + std::to_string(target_id));
        return false;
      }
      return QueuePacket(*target, packet, reliable);
    }

    if (target_id != 0 && target_id != local_id_) {
      SetError("Send: clients can only address the host (0) in this LAN build");
      return false;
    }
    if (peers_.empty()) {
      return false;
    }
    return QueuePacket(peers_[0], packet, reliable);
  }

  void FlushPeer(HostPeer &peer)
  {
    if (!peer.alive || peer.socket == KX_INVALID_SOCKET) {
      return;
    }
    while (!peer.outbox.empty()) {
      const std::string &packet = peer.outbox.front();
#ifdef _WIN32
      int written = int(send(peer.socket, packet.data(), int(packet.size()), 0));
#else
      ssize_t written = send(peer.socket, packet.data(), packet.size(), 0);
#endif
      if (written <= 0) {
#ifndef _WIN32
        if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          return;
        }
#endif
        MarkDead(peer);
        return;
      }
      if (size_t(written) < packet.size()) {
        peer.outbox.front().erase(0, size_t(written));
        return;
      }
      peer.outbox.pop_front();
    }
  }

  void MarkDead(HostPeer &peer)
  {
    if (!peer.alive) {
      return;
    }
    peer.alive = false;
    CloseSocket(peer.socket);
    peer.outbox.clear();
    peer.read_buffer.clear();
    std::fprintf(stderr,
                 "GameNode: network %s dropped peer %d (%s)\n",
                 role_ == Role::Host ? "host" : "client",
                 peer.id,
                 last_error_.empty() ? "connection closed" : last_error_.c_str());
    if (role_ == Role::Host && peer.id >= 0) {
      PushEvent(KX_NetworkEvent::PlayerLeave, peer.id, std::string(), true);
      std::string payload;
      AppendI32(payload, int32_t(peer.id));
      std::string packet = BuildPacket(PKT_LEAVE, FLAG_RELIABLE, peer.id, payload);
      for (HostPeer &other : peers_) {
        if (other.alive && other.id != peer.id) {
          QueuePacket(other, packet, true);
        }
      }
      UpdatePlayerCount();
    }
    else if (role_ == Role::Client) {
      PushEvent(KX_NetworkEvent::Disconnect, 0, std::string(), true);
      connected_ = false;
      UpdatePlayerCount();
    }
  }

  void UpdatePlayerCount()
  {
    int alive = 0;
    for (const HostPeer &peer : peers_) {
      if (peer.alive) {
        ++alive;
      }
    }
    player_count_ = connected_ ? alive + 1 : 0;
  }

  void PushEvent(KX_NetworkEvent::Type type, int player_id, const std::string &data, bool reliable)
  {
    KX_NetworkEvent event;
    event.type = type;
    event.player_id = player_id;
    event.data = data;
    event.reliable = reliable;
    events_.push_back(event);
  }

  void Pump()
  {
    if (!connected_) {
      return;
    }
    if (role_ == Role::Host) {
      AcceptPeers();
    }
    for (HostPeer &peer : peers_) {
      if (!peer.alive) {
        continue;
      }
      ReadPeer(peer);
      if (peer.alive) {
        FlushPeer(peer);
      }
    }
    peers_.erase(std::remove_if(peers_.begin(),
                                peers_.end(),
                                [](const HostPeer &peer) { return !peer.alive; }),
                 peers_.end());
    PumpUdp();
  }

  void SendRoomInfo(const sockaddr_in &to)
  {
    std::string payload;
    std::string name = room_name_.size() > 0xFFFF ? room_name_.substr(0, 0xFFFF) : room_name_;
    AppendU16(payload, uint16_t(name.size()));
    payload.append(name);
    AppendI32(payload, int32_t(player_count_));
    AppendI32(payload, int32_t(max_players_));
    std::string packet = BuildPacket(PKT_ROOMINFO, FLAG_RELIABLE, 0, payload);
    SendDatagramTo(udp_socket_, to, packet);
  }

  void HandleDatagram(const sockaddr_in &from, const std::string &datagram)
  {
    if (datagram.size() < kHeaderSize) {
      return;
    }
    uint8_t type = 0;
    uint8_t flags = 0;
    int sender = 0;
    uint32_t payload_length = 0;
    if (!ParsePacketHeader(datagram, 0, type, flags, sender, payload_length)) {
      return;
    }
    /* Datagrams are self contained: a trailing or missing byte is a corrupt
     * packet, not something to resynchronise like a TCP stream. */
    if (datagram.size() != kHeaderSize + payload_length) {
      return;
    }
    (void)flags;
    std::string payload = datagram.substr(kHeaderSize, payload_length);

    if (type == PKT_ROOMLIST) {
      /* Read-only query, accepted from any source: it changes no room state. */
      if (role_ == Role::Host) {
        SendRoomInfo(from);
      }
      return;
    }
    if (type != PKT_MESSAGE) {
      return;
    }

    if (role_ == Role::Host) {
      HostPeer *peer = nullptr;
      for (HostPeer &candidate : peers_) {
        if (candidate.alive && candidate.udp_ready &&
            SameEndpoint(candidate.udp_address, from)) {
          peer = &candidate;
          break;
        }
      }
      if (peer == nullptr || sender != peer->id) {
        return; /* Unknown source or a forged sender id. */
      }
      PushEvent(KX_NetworkEvent::Message, sender, payload, false);
      return;
    }

    if (role_ == Role::Client && host_udp_ready_ && SameEndpoint(host_udp_address_, from) &&
        sender == 0) {
      PushEvent(KX_NetworkEvent::Message, 0, payload, false);
    }
  }

  void PumpUdp()
  {
    if (udp_socket_ == KX_INVALID_SOCKET) {
      return;
    }
    if (udp_buffer_.size() < kMaxDatagramBytes) {
      udp_buffer_.resize(kMaxDatagramBytes);
    }
    for (int guard = 0; guard < 256; ++guard) {
      sockaddr_in from;
      std::memset(&from, 0, sizeof(from));
      const int received = RecvDatagram(udp_socket_, udp_buffer_, from);
      if (received <= 0) {
#ifndef _WIN32
        if (received == -1 && errno == EAGAIN) {
          return;
        }
#endif
        return;
      }
      HandleDatagram(from, std::string(udp_buffer_.data(), size_t(received)));
    }
  }

  void AcceptPeers()
  {
    if (listen_socket_ == KX_INVALID_SOCKET) {
      return;
    }
    for (int guard = 0; guard < 8; ++guard) {
      if (!PollReady(listen_socket_, kPollIn, 0)) {
        return;
      }
      SocketHandle client = accept(listen_socket_, nullptr, nullptr);
      if (client == KX_INVALID_SOCKET) {
        return;
      }
      sockaddr_in client_address;
      std::memset(&client_address, 0, sizeof(client_address));
#ifdef _WIN32
      int address_length = int(sizeof(client_address));
      getpeername(client, reinterpret_cast<sockaddr *>(&client_address), &address_length);
#else
      socklen_t address_length = sizeof(client_address);
      getpeername(client, reinterpret_cast<sockaddr *>(&client_address), &address_length);
#endif
      int id = FindFreePlayerId();
      if (id < 0) {
        /* Room full: close immediately instead of silently overflowing. */
        CloseSocket(client);
        continue;
      }
      SetNonBlocking(client);
      int no_delay = 1;
      setsockopt(client,
                 IPPROTO_TCP,
                 TCP_NODELAY,
                 reinterpret_cast<const char *>(&no_delay),
                 sizeof(no_delay));
      HostPeer peer;
      peer.socket = client;
      peer.id = id;
      peer.address = client_address;
      peers_.push_back(peer);

      std::string payload;
      AppendI32(payload, int32_t(id));
      /* v2: tell the client which UDP port to send unreliable packets to. */
      AppendU16(payload, uint16_t(host_udp_port_));
      std::string welcome = BuildPacket(PKT_WELCOME, FLAG_RELIABLE, 0, payload);
      QueuePacket(peers_.back(), welcome, true);
      FlushPeer(peers_.back());

      PushEvent(KX_NetworkEvent::PlayerJoin, id, std::string(), true);
      std::string join_payload;
      AppendI32(join_payload, int32_t(id));
      std::string join_packet = BuildPacket(PKT_JOIN, FLAG_RELIABLE, id, join_payload);
      for (HostPeer &other : peers_) {
        if (other.alive && other.id != id) {
          QueuePacket(other, join_packet, true);
        }
      }
      UpdatePlayerCount();
    }
  }

  void ReadPeer(HostPeer &peer)
  {
    for (int guard = 0; guard < 32; ++guard) {
      char chunk[4096];
#ifdef _WIN32
      int received = int(recv(peer.socket, chunk, int(sizeof(chunk)), 0));
#else
      ssize_t received = recv(peer.socket, chunk, sizeof(chunk), 0);
#endif
      if (received == 0) {
        MarkDead(peer);
        return;
      }
      if (received < 0) {
#ifndef _WIN32
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return;
        }
#endif
        MarkDead(peer);
        return;
      }
      peer.read_buffer.append(chunk, size_t(received));
      if (!ProcessBuffer(peer)) {
        MarkDead(peer);
        return;
      }
    }
  }

  bool ProcessBuffer(HostPeer &peer)
  {
    while (peer.read_buffer.size() >= kHeaderSize) {
      uint8_t type = 0;
      uint8_t flags = 0;
      int sender = 0;
      uint32_t payload_length = 0;
      if (!ParsePacketHeader(peer.read_buffer, 0, type, flags, sender, payload_length)) {
        SetError("network protocol error: bad frame header");
        return false;
      }
      if (payload_length > kMaxFrameBytes) {
        SetError("network protocol error: frame too large");
        return false;
      }
      if (peer.read_buffer.size() < kHeaderSize + payload_length) {
        return true; /* Wait for the rest of the frame. */
      }
      std::string payload = peer.read_buffer.substr(kHeaderSize, payload_length);
      peer.read_buffer.erase(0, kHeaderSize + payload_length);
      if (!HandlePacket(peer, type, flags, sender, payload)) {
        return false;
      }
    }
    return true;
  }

  bool HandlePacket(HostPeer &peer, uint8_t type, uint8_t flags, int sender, const std::string &payload)
  {
    switch (type) {
      case PKT_WELCOME: {
        size_t cursor = 0;
        int32_t assigned = 0;
        uint16_t host_udp_port = 0;
        if (!ReadI32(payload, cursor, assigned) || !ReadU16(payload, cursor, host_udp_port)) {
          SetError("network protocol error: truncated WELCOME frame");
          return false;
        }
        if (role_ == Role::Client && local_id_ < 0) {
          local_id_ = int(assigned);
          peer.id = 0;
          if (host_udp_port == 0) {
            SetError("network protocol error: WELCOME has no UDP port");
            return false;
          }
          host_udp_address_ = peer.address;
          host_udp_address_.sin_port = htons(host_udp_port);
          host_udp_ready_ = true;
          UpdatePlayerCount();
          PushEvent(KX_NetworkEvent::PlayerJoin, 0, std::string(), true);
          /* Answer the UDP port exchange so the host can reach this client. */
          SendHello(peer);
        }
        return true;
      }
      case PKT_JOIN: {
        size_t cursor = 0;
        int32_t joined = 0;
        if (!ReadI32(payload, cursor, joined)) {
          return false;
        }
        if (role_ == Role::Client) {
          PushEvent(KX_NetworkEvent::PlayerJoin, int(joined), std::string(), true);
          UpdatePlayerCount();
        }
        return true;
      }
      case PKT_LEAVE: {
        size_t cursor = 0;
        int32_t left = 0;
        if (!ReadI32(payload, cursor, left)) {
          return false;
        }
        if (role_ == Role::Client) {
          PushEvent(KX_NetworkEvent::PlayerLeave, int(left), std::string(), true);
          UpdatePlayerCount();
        }
        return true;
      }
      case PKT_MESSAGE: {
        PushEvent(KX_NetworkEvent::Message,
                  sender,
                  payload,
                  (flags & FLAG_RELIABLE) != 0);
        return true;
      }
      case PKT_PROPERTY: {
        size_t cursor = 0;
        std::size_t key_length = 0;
        if (!ReadString(payload, cursor, key_length)) {
          return false;
        }
        std::string key = payload.substr(cursor, key_length);
        cursor += key_length;
        PropertyEntry entry;
        if (!DecodeValue(payload, cursor, entry.value, entry.storage)) {
          return false;
        }
        remote_properties_[sender][key] = entry;
        return true;
      }
      case PKT_HELLO: {
        /* v2: the client announces its UDP port after WELCOME. The IP half of
         * the endpoint is the TCP source address, never trusted from payload. */
        size_t cursor = 0;
        uint16_t client_udp_port = 0;
        if (!ReadU16(payload, cursor, client_udp_port)) {
          return false;
        }
        if (role_ == Role::Host && peer.id >= 0 && client_udp_port != 0) {
          peer.udp_address = peer.address;
          peer.udp_address.sin_port = htons(client_udp_port);
          peer.udp_port = client_udp_port;
          peer.udp_ready = true;
        }
        return true;
      }
      default:
        return true;
    }
  }

  void Shutdown()
  {
    for (HostPeer &peer : peers_) {
      CloseSocket(peer.socket);
      peer.outbox.clear();
      peer.read_buffer.clear();
      peer.alive = false;
    }
    peers_.clear();
    CloseSocket(listen_socket_);
    CloseSocket(udp_socket_);
    udp_buffer_.clear();
    host_udp_port_ = 0;
    client_udp_port_ = 0;
    host_udp_ready_ = false;
    std::memset(&host_udp_address_, 0, sizeof(host_udp_address_));
    events_.clear();
    player_count_ = 0;
    connected_ = false;
  }

  Role role_ = Role::None;
  int local_id_ = -1;
  int player_count_ = 0;
  int max_players_ = 0;
  int host_port_ = 0;
  bool connected_ = false;
  std::string last_error_;
  SocketHandle listen_socket_ = KX_INVALID_SOCKET;
  SocketHandle udp_socket_ = KX_INVALID_SOCKET;
  std::vector<char> udp_buffer_;
  int host_udp_port_ = 0;
  uint16_t client_udp_port_ = 0;
  sockaddr_in host_udp_address_;
  bool host_udp_ready_ = false;
  std::string last_join_host_;
  int last_join_port_ = 0;
  std::string room_name_ = "room";
  std::vector<HostPeer> peers_;
  std::deque<KX_NetworkEvent> events_;
  std::map<std::string, KX_RuntimeValue> local_properties_;
  RemotePropertyMap remote_properties_;
};

std::unique_ptr<KX_NetworkAdapter> KX_CreateEngineNetworkAdapter()
{
  return std::unique_ptr<KX_NetworkAdapter>(new EngineNetworkAdapter());
}
