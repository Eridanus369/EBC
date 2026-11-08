// B6.9-N N-1 E2E: LAN room discovery across two processes.
//
// Semantics under test (fixed in KX_NetworkAdapterEngine.cpp):
//   - DiscoverRooms broadcasts a PKT_DISCOVER every 200 ms for the caller's
//     window (default 1000 ms); a room that does not answer inside one window
//     is offline and is absent from the next result;
//   - replies are de-duplicated by source endpoint ip:port, never by room name;
//   - a host answers a query unicast and never answers another room's reply,
//     so hosts cannot ping-pong each other into a broadcast storm.
//
// Usage:
//   n1_discover_two_proc host   <port> <shared_dir>
//   n1_discover_two_proc client <port> <shared_dir>
//
// This is a real broadcast test: the room is a separate process with its own
// sockets. A single-process mock would not exercise the datagram path at all.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "KX_GameAdapters.h"

/* Sources from parallel lines are not part of this harness. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

namespace {

const char *kRoomName = "alpha-room";
const int kMaxPlayers = 4;

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  std::fflush(stdout);
  if (!ok) {
    ++failures;
  }
}

void SleepMs(int ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string Path(const std::string &dir, const std::string &name)
{
  return dir + "/" + name;
}

bool FileExists(const std::string &path)
{
  std::ifstream in(path);
  return bool(in);
}

std::string ReadFile(const std::string &path)
{
  std::ifstream in(path);
  if (!in) {
    return std::string();
  }
  std::string line;
  std::getline(in, line);
  return line;
}

void WriteFile(const std::string &path, const std::string &text)
{
  std::ofstream out(path, std::ios::trunc);
  out << text << "\n";
}

bool WaitFile(const std::string &path, int timeout_ms)
{
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (FileExists(path)) {
      return true;
    }
    SleepMs(5);
  }
  return FileExists(path);
}

/* ------------------------------------------------------------------------- */
/* Raw protocol helpers for the "do not answer a reply" negative test.       */
/* ------------------------------------------------------------------------- */

void RawAppendU16(std::string &out, uint16_t value)
{
  out.push_back(char(value & 0xFF));
  out.push_back(char((value >> 8) & 0xFF));
}

void RawAppendU32(std::string &out, uint32_t value)
{
  for (int i = 0; i < 4; ++i) {
    out.push_back(char((value >> (8 * i)) & 0xFF));
  }
}

void RawAppendI32(std::string &out, int32_t value)
{
  RawAppendU32(out, uint32_t(value));
}

/** Mirrors BuildPacket(): magic "B6N1", version 2, 16-byte header. */
std::string RawPacket(uint8_t type, int sender, const std::string &payload)
{
  std::string packet;
  RawAppendU32(packet, 0x42364E31u);
  RawAppendU16(packet, 2);
  packet.push_back(char(type));
  packet.push_back(char(0));
  RawAppendI32(packet, int32_t(sender));
  RawAppendU32(packet, uint32_t(payload.size()));
  packet.append(payload);
  return packet;
}

std::string RawRoomInfoPayload(const std::string &name, int players, int max_players)
{
  std::string payload;
  RawAppendU16(payload, uint16_t(name.size()));
  payload.append(name);
  RawAppendI32(payload, int32_t(players));
  RawAppendI32(payload, int32_t(max_players));
  return payload;
}

int OpenRawUdp(int port)
{
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in to;
  std::memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(uint16_t(port));
  to.sin_addr.s_addr = htonl(0x7F000001u);
  /* A first sendto() binds the socket to an ephemeral port, which is what the
   * host's unicast reply would be addressed to. */
  if (sendto(fd, "", 0, 0, reinterpret_cast<sockaddr *>(&to), sizeof(to)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool SendRaw(int fd, int port, const std::string &packet)
{
  sockaddr_in to;
  std::memset(&to, 0, sizeof(to));
  to.sin_family = AF_INET;
  to.sin_port = htons(uint16_t(port));
  to.sin_addr.s_addr = htonl(0x7F000001u);
  return sendto(fd,
                packet.data(),
                packet.size(),
                0,
                reinterpret_cast<sockaddr *>(&to),
                sizeof(to)) >= 0;
}

bool ReceiveRaw(int fd, int timeout_ms)
{
  pollfd entry;
  entry.fd = fd;
  entry.events = POLLIN;
  entry.revents = 0;
  if (poll(&entry, 1, timeout_ms) <= 0) {
    return false;
  }
  char buffer[512];
  const int received = int(recv(fd, buffer, sizeof(buffer), 0));
  return received > 0;
}

/* ------------------------------------------------------------------------- */
/* Host: stays alive while the client discovers, then shuts the room down.   */
/* ------------------------------------------------------------------------- */

int RunHost(KX_NetworkAdapter &adapter, int port, const std::string &dir)
{
  adapter.SetRoomName(kRoomName);
  std::string error;
  if (!adapter.HostRoom(port, kMaxPlayers, &error)) {
    std::printf("FAIL HostRoom: %s\n", error.c_str());
    return 1;
  }
  Check(adapter.LocalPlayerId() == 0, "host is player 0");
  WriteFile(Path(dir, "host_ready"), std::to_string(port));

  /* Answer queries until the client says it has finished both discovery calls
   * (including the forged-reply negative test). Tick() is what pumps UDP. */
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  while (!FileExists(Path(dir, "stop_host")) &&
         std::chrono::steady_clock::now() < deadline) {
    adapter.Tick();
    SleepMs(5);
  }
  if (!FileExists(Path(dir, "stop_host"))) {
    std::printf("FAIL host: client never asked to stop the room\n");
    return 1;
  }
  adapter.LeaveRoom();
  WriteFile(Path(dir, "host_stopped"), "1");
  std::printf("INFO host stopped port=%d\n", port);
  return failures == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------------- */
/* Client: discover, dedup, forged-reply negative test, offline after stop.   */
/* ------------------------------------------------------------------------- */

int RunClient(KX_NetworkAdapter &adapter, int port, const std::string &dir)
{
  if (!WaitFile(Path(dir, "host_ready"), 5000)) {
    std::printf("FAIL client: host never became ready\n");
    return 1;
  }

  std::string error;
  /* 1200 ms window = six broadcast rounds. Without endpoint de-duplication the
   * list would hold a reply per round (two targets per round), not one room. */
  std::vector<KX_RoomInfo> rooms;
  const bool found = adapter.DiscoverRooms(port, 1200, rooms, &error);
  Check(found, "DiscoverRooms completed the window");
  if (!found) {
    std::printf("INFO DiscoverRooms error='%s'\n", error.c_str());
  }
  std::printf("INFO discovered_rooms=%zu\n", rooms.size());
  Check(rooms.size() == 1, "six broadcast rounds collapse to exactly one room");
  if (rooms.size() == 1) {
    Check(rooms[0].name == kRoomName, "discovered room name matches");
    /* The broadcast reply arrives from the room's LAN interface, not from
     * loopback: the host field must be the non-empty reply source, and the
     * port must match the queried port. */
    Check(!rooms[0].host.empty() && rooms[0].host != "0.0.0.0",
          "discovered room host is the reply source address");
    Check(rooms[0].port == port, "discovered room port is the queried UDP port");
    Check(rooms[0].players == 1, "room reports one player (the host)");
    Check(rooms[0].max_players == kMaxPlayers, "room reports the configured capacity");
    std::printf("INFO room name='%s' host=%s port=%d players=%d max=%d\n",
                rooms[0].name.c_str(),
                rooms[0].host.c_str(),
                rooms[0].port,
                rooms[0].players,
                rooms[0].max_players);
  }

  /* Negative test for rule (3): the host must not answer another room's
   * PKT_ROOMINFO. If it did, the forged sender would receive a datagram. */
  const int raw = OpenRawUdp(port);
  if (raw < 0) {
    std::printf("FAIL client: could not open the raw probe socket\n");
    ++failures;
  }
  else {
    const std::string forged = RawPacket(8, 77, RawRoomInfoPayload("other-room", 1, 4));
    Check(SendRaw(raw, port, forged), "forged ROOMINFO was sent to the room");
    Check(!ReceiveRaw(raw, 400), "host does not answer another room's reply");
    /* Positive control: the same socket queries, and the host must answer. */
    Check(SendRaw(raw, port, RawPacket(9, -1, std::string())),
          "forged DISCOVER was sent to the room");
    Check(ReceiveRaw(raw, 600), "host answers a DISCOVER from a raw socket");
    close(raw);
  }

  WriteFile(Path(dir, "stop_host"), "1");
  if (!WaitFile(Path(dir, "host_stopped"), 5000)) {
    std::printf("FAIL client: host never stopped\n");
    return 1;
  }

  /* Rule (1): a room that does not answer inside one 1000 ms window is offline.
   * DiscoverRooms itself still succeeds: an empty LAN is a valid answer. */
  std::vector<KX_RoomInfo> after;
  const bool offline_ok = adapter.DiscoverRooms(port, 1000, after, &error);
  Check(offline_ok, "offline discovery completed the window");
  Check(after.empty(), "stopped room is gone after the discovery timeout");
  std::printf("INFO after_stop rooms=%zu\n", after.size());

  WriteFile(Path(dir, "client_done"), "1");
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <host|client> <port> <shared_dir>\n", argv[0]);
    return 2;
  }
  const std::string mode = argv[1];
  const int port = std::atoi(argv[2]);
  const std::string dir = argv[3];
  if (port <= 0 || port > 65535) {
    std::fprintf(stderr, "invalid port: %s\n", argv[2]);
    return 2;
  }

  std::unique_ptr<KX_NetworkAdapter> adapter = KX_CreateEngineNetworkAdapter();
  if (!adapter) {
    std::fprintf(stderr, "KX_CreateEngineNetworkAdapter returned null\n");
    return 1;
  }
  std::printf("INFO pid=%d mode=%s\n", int(getpid()), mode.c_str());
  return mode == "host" ? RunHost(*adapter, port, dir)
                        : mode == "client" ? RunClient(*adapter, port, dir) : 2;
}
