// B6.7 C.3.1 E2E: unreliable messages must travel over a real UDP socket.
//
// One process hosts and joins through two adapter instances. The host pump runs
// in a short helper thread only while a blocking call (JoinRoom) is waiting,
// otherwise both adapters are ticked in the main loop.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

#include "KX_GameAdapters.h"

/* Sources from parallel lines are not part of this harness. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

namespace {

const int kPort = 46067;

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

void SleepMs(int ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/** Tick #adapter in a background thread while #fn runs in the caller. */
template<typename Fn>
void WithTicking(KX_NetworkAdapter &adapter, int max_ms, Fn fn)
{
  std::atomic<bool> stop(false);
  std::thread pump([&]() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
    while (!stop.load() && std::chrono::steady_clock::now() < deadline) {
      adapter.Tick();
      SleepMs(2);
    }
  });
  fn();
  stop.store(true);
  pump.join();
  adapter.Tick();
}

void PumpBoth(KX_NetworkAdapter &host, KX_NetworkAdapter &client, int ms)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    host.Tick();
    client.Tick();
    SleepMs(2);
  }
}

bool ParseSeq(const std::string &text, const char *prefix, int &seq)
{
  const size_t prefix_len = std::strlen(prefix);
  if (text.size() <= prefix_len || text.compare(0, prefix_len, prefix) != 0) {
    return false;
  }
  std::string tail = text.substr(prefix_len);
  if (tail.empty()) {
    return false;
  }
  int value = 0;
  for (char c : tail) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  seq = value;
  return true;
}

struct Stats {
  int sent = 0;
  int queued = 0;
  int received = 0;
  int corrupt = 0;
  int reordered = 0;
  int duplicates = 0;
};

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);

  std::unique_ptr<KX_NetworkAdapter> host = KX_CreateEngineNetworkAdapter();
  std::unique_ptr<KX_NetworkAdapter> client = KX_CreateEngineNetworkAdapter();

  Check(host->HostRoom(kPort, 4, nullptr), "host HostRoom opened TCP + UDP");
  Check(host->GetRole() == KX_NetworkAdapter::Role::Host, "host role is Host");

  bool joined = false;
  std::string join_error;
  WithTicking(*host, 600, [&]() { joined = client->JoinRoom("127.0.0.1", kPort, &join_error); });
  Check(joined, "client JoinRoom");
  std::printf("INFO join id=%d error='%s'\n", client->LocalPlayerId(), join_error.c_str());
  Check(client->LocalPlayerId() >= 1, "client received a non-host player id");

  /* Give the host time to read the v2 HELLO (client UDP port). */
  PumpBoth(*host, *client, 100);

  /* --- host -> client: unreliable burst over UDP ------------------------ */
  const int kHostBurst = 50;
  Stats h2c;
  std::vector<std::string> msgs;
  for (int i = 0; i < kHostBurst; ++i) {
    const std::string text = "us:" + std::to_string(i);
    msgs.push_back(text);
    ++h2c.sent;
    if (host->SendUnreliable(client->LocalPlayerId(), text.data(), text.size())) {
      ++h2c.queued;
    }
  }
  /* The old TCP-backed path dropped everything beyond 8 queued packets. A
   * burst of 50 accepted packets therefore proves a real datagram send. */
  Check(h2c.queued == kHostBurst, "all 50 unreliable packets were accepted by the UDP send");

  bool seen[64] = {false};
  int last_seq = -1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  int last_received = 0;
  auto last_activity = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() < deadline) {
    host->Tick();
    client->Tick();
    KX_NetworkEvent event;
    while (client->PollEvent(event)) {
      if (event.type != KX_NetworkEvent::Message || event.reliable) {
        continue;
      }
      int seq = -1;
      if (!ParseSeq(event.data, "us:", seq) || seq < 0 || seq >= kHostBurst) {
        ++h2c.corrupt;
        continue;
      }
      if (seen[seq]) {
        ++h2c.duplicates;
        continue;
      }
      seen[seq] = true;
      ++h2c.received;
      if (last_seq >= 0 && seq < last_seq) {
        ++h2c.reordered;
      }
      last_seq = seq;
      last_activity = std::chrono::steady_clock::now();
    }
    if (h2c.received != last_received) {
      last_received = h2c.received;
      last_activity = std::chrono::steady_clock::now();
    }
    if (h2c.received > 0 &&
        std::chrono::steady_clock::now() - last_activity > std::chrono::milliseconds(200)) {
      break;
    }
    SleepMs(2);
  }
  std::printf("INFO udp host->client sent=%d queued=%d received=%d dropped=%d reordered=%d "
              "duplicate=%d corrupt=%d\n",
              h2c.sent,
              h2c.queued,
              h2c.received,
              h2c.sent - h2c.received,
              h2c.reordered,
              h2c.duplicates,
              h2c.corrupt);
  Check(h2c.received >= 45, "at least 90% of the UDP burst arrived");
  Check(h2c.corrupt == 0, "every arrived unreliable payload is intact");
  Check(h2c.duplicates == 0, "no duplicated unreliable payload");

  /* --- client -> host: unreliable over UDP ------------------------------ */
  const int kClientBurst = 30;
  Stats c2h;
  for (int i = 0; i < kClientBurst; ++i) {
    const std::string text = "cs:" + std::to_string(i);
    ++c2h.sent;
    if (client->SendUnreliable(0, text.data(), text.size())) {
      ++c2h.queued;
    }
  }
  Check(c2h.queued == kClientBurst, "client accepted all 30 unreliable packets");

  bool seen_client[64] = {false};
  int last_client_seq = -1;
  const auto c2h_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  auto c2h_activity = std::chrono::steady_clock::now();
  int last_c2h = 0;
  while (std::chrono::steady_clock::now() < c2h_deadline) {
    host->Tick();
    client->Tick();
    KX_NetworkEvent event;
    while (host->PollEvent(event)) {
      if (event.type != KX_NetworkEvent::Message || event.reliable) {
        continue;
      }
      int seq = -1;
      if (!ParseSeq(event.data, "cs:", seq) || seq < 0 || seq >= kClientBurst) {
        ++c2h.corrupt;
        continue;
      }
      if (seen_client[seq]) {
        ++c2h.duplicates;
        continue;
      }
      seen_client[seq] = true;
      ++c2h.received;
      if (last_client_seq >= 0 && seq < last_client_seq) {
        ++c2h.reordered;
      }
      last_client_seq = seq;
      c2h_activity = std::chrono::steady_clock::now();
    }
    if (c2h.received != last_c2h) {
      last_c2h = c2h.received;
      c2h_activity = std::chrono::steady_clock::now();
    }
    if (c2h.received > 0 &&
        std::chrono::steady_clock::now() - c2h_activity > std::chrono::milliseconds(200)) {
      break;
    }
    SleepMs(2);
  }
  std::printf("INFO udp client->host sent=%d queued=%d received=%d dropped=%d reordered=%d "
              "duplicate=%d corrupt=%d\n",
              c2h.sent,
              c2h.queued,
              c2h.received,
              c2h.sent - c2h.received,
              c2h.reordered,
              c2h.duplicates,
              c2h.corrupt);
  Check(c2h.received >= 27, "at least 90% of the reverse UDP burst arrived");
  Check(c2h.corrupt == 0, "reverse UDP payloads are intact");

  /* --- reliable path still runs over TCP and is complete/ordered -------- */
  const int kReliable = 30;
  int reliable_sent = 0;
  for (int i = 0; i < kReliable; ++i) {
    const std::string text = "rl:" + std::to_string(i);
    if (host->BroadcastReliable(text.data(), text.size())) {
      ++reliable_sent;
    }
  }
  Check(reliable_sent == kReliable, "host queued all reliable packets");

  int reliable_received = 0;
  int expected = 0;
  bool order_ok = true;
  const auto rel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (reliable_received < kReliable && std::chrono::steady_clock::now() < rel_deadline) {
    host->Tick();
    client->Tick();
    KX_NetworkEvent event;
    while (client->PollEvent(event)) {
      if (event.type != KX_NetworkEvent::Message || !event.reliable) {
        continue;
      }
      int seq = -1;
      if (!ParseSeq(event.data, "rl:", seq) || seq != expected) {
        order_ok = false;
      }
      ++expected;
      ++reliable_received;
    }
    SleepMs(2);
  }
  std::printf("INFO tcp reliable sent=%d received=%d in-order=%d\n",
              reliable_sent,
              reliable_received,
              int(order_ok));
  Check(reliable_received == kReliable, "every reliable packet arrived over TCP");
  Check(order_ok, "reliable packets stayed in order");

  client->LeaveRoom();
  host->LeaveRoom();

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
