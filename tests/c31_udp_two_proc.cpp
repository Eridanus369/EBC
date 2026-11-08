// B6.7 D.1 E2E: unreliable messages over real UDP, measured across two
// processes.
//
// The existing c31_udp_e2e runs both adapters inside one process and asserts
// ">= 90% arrived", which on loopback always reports 0% loss and therefore
// proves a socket was opened but not that the channel is unreliable.
//
// This harness runs one host process and one client process:
//   c31_udp_two_proc host   <port> <shared_dir>
//   c31_udp_two_proc client <port> <shared_dir>
//
// Phase 1 (paced): the client sends kPacedCount messages every few ms while
// both sides tick. Loss is reported, never asserted to be zero.
//
// Phase 2 (overflow): the host deliberately stops ticking (it only polls a
// coordination file), while the client fires kBurstCount small datagrams back
// to back. The kernel receive buffer holds only a few hundred of them, so the
// rest are dropped by the kernel. The host then resumes ticking and drains
// what is left. A real UDP channel therefore yields
//       0 < received < accepted
// and a further 500 ms of ticking must not increase the count (no retransmit).
// A TCP-backed implementation would deliver every accepted byte after the
// resume, which is exactly what this phase rules out.
//
// The process that finishes the checks is the host; the client only sends and
// relays the host's numbers.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

#include "KX_GameAdapters.h"

/* Sources from parallel lines are not part of this harness. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

namespace {

const int kPacedCount = 200;
const int kPacedIntervalMs = 2;
const int kBurstCount = 20000;
const size_t kBurstPayloadBytes = 512;

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

/** Reads "<prefix><digits>:..." or "<prefix><digits>"; returns the number. */
bool ParseSeq(const std::string &text, const char *prefix, int *seq)
{
  const size_t prefix_len = std::strlen(prefix);
  if (text.size() <= prefix_len || text.compare(0, prefix_len, prefix) != 0) {
    return false;
  }
  int value = -1;
  for (size_t i = prefix_len; i < text.size(); ++i) {
    const char c = text[i];
    if (c >= '0' && c <= '9') {
      value = (value < 0 ? 0 : value) * 10 + (c - '0');
      continue;
    }
    break;
  }
  if (value < 0) {
    return false;
  }
  *seq = value;
  return true;
}

struct HostStats {
  int paced_received = 0;
  int paced_corrupt = 0;
  int burst_received = 0;
  int burst_corrupt = 0;
  int burst_duplicates = 0;
};

/** Tick and collect unreliable messages with the given prefix. */
void DrainUnreliable(KX_NetworkAdapter &adapter,
                     const char *prefix,
                     int duration_ms,
                     std::set<int> *seen,
                     HostStats *stats,
                     bool burst)
{
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(duration_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    adapter.Tick();
    KX_NetworkEvent event;
    while (adapter.PollEvent(event)) {
      if (event.type != KX_NetworkEvent::Message || event.reliable) {
        continue;
      }
      int seq = -1;
      if (!ParseSeq(event.data, prefix, &seq)) {
        if (burst) {
          ++stats->burst_corrupt;
        }
        else {
          ++stats->paced_corrupt;
        }
        continue;
      }
      if (!seen->insert(seq).second) {
        if (burst) {
          ++stats->burst_duplicates;
        }
        continue;
      }
      if (burst) {
        ++stats->burst_received;
      }
      else {
        ++stats->paced_received;
      }
    }
    SleepMs(1);
  }
}

int RunHost(KX_NetworkAdapter &adapter, int port, const std::string &dir)
{
  std::string error;
  if (!adapter.HostRoom(port, 4, &error)) {
    std::printf("FAIL HostRoom: %s\n", error.c_str());
    return 1;
  }
  WriteFile(Path(dir, "host_ready"), "1");

  /* Wait for the client's TCP hello, which also carries its UDP port. */
  const auto join_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (adapter.PlayerCount() < 2 && std::chrono::steady_clock::now() < join_deadline) {
    adapter.Tick();
    SleepMs(2);
  }
  Check(adapter.PlayerCount() >= 2, "client joined over TCP");
  Check(adapter.LocalPlayerId() == 0, "host id is 0");
  WriteFile(Path(dir, "host_joined"), std::to_string(adapter.PlayerCount()));

  /* Phase 0: host -> client paced stream, so both directions cross process
   * boundaries, not just client -> host. */
  if (!WaitFile(Path(dir, "client_joined"), 2000)) {
    std::printf("FAIL phase0: client never reported its id\n");
    return 1;
  }
  const int client_id = std::atoi(ReadFile(Path(dir, "client_joined")).c_str());
  int reverse_accepted = 0;
  for (int i = 0; i < kPacedCount; ++i) {
    const std::string text = "h:" + std::to_string(i);
    if (adapter.SendUnreliable(client_id, text.data(), text.size())) {
      ++reverse_accepted;
    }
    adapter.Tick();
    SleepMs(kPacedIntervalMs);
  }
  WriteFile(Path(dir, "reverse_sent"), std::to_string(reverse_accepted));
  if (!WaitFile(Path(dir, "reverse_result"), 3000)) {
    std::printf("FAIL phase0: client never reported reverse_result\n");
    return 1;
  }
  const int reverse_received = std::atoi(ReadFile(Path(dir, "reverse_result")).c_str());
  std::printf("INFO phase0 host_to_client sent=%d received=%d dropped=%d\n",
              reverse_accepted,
              reverse_received,
              reverse_accepted - reverse_received);
  Check(reverse_received > 0, "host->client: datagrams crossed the process boundary");

  /* Phase 1: paced stream, both sides ticking. */
  std::set<int> paced_seen;
  HostStats stats;
  DrainUnreliable(adapter, "p:", 800, &paced_seen, &stats, false);
  if (!WaitFile(Path(dir, "paced_sent"), 2000)) {
    std::printf("FAIL phase1: client never reported paced_sent\n");
    return 1;
  }
  const int paced_sent = std::atoi(ReadFile(Path(dir, "paced_sent")).c_str());
  std::printf("INFO phase1 paced sent=%d received=%d dropped=%d corrupt=%d\n",
              paced_sent,
              stats.paced_received,
              paced_sent - stats.paced_received,
              stats.paced_corrupt);
  Check(stats.paced_received > 0, "paced: at least one unreliable datagram arrived");
  Check(stats.paced_corrupt == 0, "paced: every arrived payload is intact");
  WriteFile(Path(dir, "paced_result"),
            std::to_string(stats.paced_received) + " " + std::to_string(stats.paced_corrupt));

  /* Phase 2: stop ticking so the kernel receive buffer overflows. */
  WriteFile(Path(dir, "burst_go"), "1");
  if (!WaitFile(Path(dir, "burst_done"), 15000)) {
    std::printf("FAIL phase2: client never finished the burst\n");
    return 1;
  }
  const int accepted = std::atoi(ReadFile(Path(dir, "burst_done")).c_str());

  WriteFile(Path(dir, "burst_drain"), "1");
  std::set<int> burst_seen;
  DrainUnreliable(adapter, "b:", 800, &burst_seen, &stats, true);
  const int right_after = stats.burst_received;
  DrainUnreliable(adapter, "b:", 500, &burst_seen, &stats, true);
  const int final_count = stats.burst_received;

  std::printf("INFO phase2 burst accepted=%d received=%d dropped=%d "
              "after_drain_received=%d final_received=%d duplicates=%d corrupt=%d\n",
              accepted,
              right_after,
              accepted - right_after,
              right_after,
              final_count,
              stats.burst_duplicates,
              stats.burst_corrupt);
  std::printf("INFO loss_rate=%.2f%%\n",
              accepted > 0 ? 100.0 * double(accepted - right_after) / double(accepted) : 0.0);

  Check(accepted > 0, "burst: client handed datagrams to the kernel");
  Check(right_after > 0, "burst: some datagrams survived the overflow");
  Check(right_after < accepted, "burst: kernel dropped datagrams (0 < received < accepted)");
  Check(final_count == right_after, "burst: no retransmission after 500 ms");
  Check(stats.burst_duplicates == 0, "burst: no duplicated datagram");
  Check(stats.burst_corrupt == 0, "burst: arrived payloads are intact");

  WriteFile(Path(dir, "burst_result"),
            std::to_string(accepted) + " " + std::to_string(right_after) + " " +
                std::to_string(final_count));
  WriteFile(Path(dir, "host_done"), "1");

  adapter.LeaveRoom();
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

int RunClient(KX_NetworkAdapter &adapter, int port, const std::string &dir)
{
  if (!WaitFile(Path(dir, "host_ready"), 3000)) {
    std::printf("FAIL client: host never became ready\n");
    return 1;
  }

  std::string error;
  if (!adapter.JoinRoom("127.0.0.1", port, &error)) {
    std::printf("FAIL JoinRoom: %s\n", error.c_str());
    return 1;
  }
  Check(adapter.LocalPlayerId() >= 1, "client received a non-host id");
  WriteFile(Path(dir, "client_joined"), std::to_string(adapter.LocalPlayerId()));

  /* Phase 0: receive the host -> client paced stream. */
  if (!WaitFile(Path(dir, "host_joined"), 3000)) {
    std::printf("FAIL client: host never reported host_joined\n");
    return 1;
  }
  std::set<int> reverse_seen;
  HostStats reverse_stats;
  DrainUnreliable(adapter, "h:", 900, &reverse_seen, &reverse_stats, false);
  WriteFile(Path(dir, "reverse_result"), std::to_string(reverse_stats.paced_received));
  std::printf("INFO phase0 client received=%d corrupt=%d\n",
              reverse_stats.paced_received,
              reverse_stats.paced_corrupt);
  Check(reverse_stats.paced_received > 0, "client received host->client datagrams");
  Check(reverse_stats.paced_corrupt == 0, "client: host->client payloads intact");

  /* Phase 1: paced sends. The client keeps ticking so its own outbox and the
   * handshake stay healthy; pacing is what makes the stream "periodic". */
  int paced_accepted = 0;
  for (int i = 0; i < kPacedCount; ++i) {
    const std::string text = "p:" + std::to_string(i);
    if (adapter.SendUnreliable(0, text.data(), text.size())) {
      ++paced_accepted;
    }
    adapter.Tick();
    SleepMs(kPacedIntervalMs);
  }
  WriteFile(Path(dir, "paced_sent"), std::to_string(paced_accepted));
  std::printf("INFO phase1 client paced accepted=%d\n", paced_accepted);

  /* Phase 2: fire-and-forget burst while the host is not ticking. */
  if (!WaitFile(Path(dir, "burst_go"), 3000)) {
    std::printf("FAIL client: host never started the burst phase\n");
    return 1;
  }
  std::string pad(kBurstPayloadBytes, 'x');
  int burst_accepted = 0;
  for (int i = 0; i < kBurstCount; ++i) {
    const std::string head = "b:" + std::to_string(i) + ":";
    const std::string text = head + pad.substr(0, kBurstPayloadBytes - head.size());
    if (adapter.SendUnreliable(0, text.data(), text.size())) {
      ++burst_accepted;
    }
  }
  WriteFile(Path(dir, "burst_done"), std::to_string(burst_accepted));
  std::printf("INFO phase2 client burst accepted=%d of %d\n", burst_accepted, kBurstCount);

  if (!WaitFile(Path(dir, "burst_result"), 5000)) {
    std::printf("FAIL client: host produced no burst_result\n");
    return 1;
  }
  std::printf("INFO host burst_result='%s'\n", ReadFile(Path(dir, "burst_result")).c_str());

  WaitFile(Path(dir, "host_done"), 3000);
  adapter.LeaveRoom();
  std::printf("RESULT client_failures=%d\n", failures);
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
#ifndef _WIN32
  std::printf("INFO pid=%d mode=%s\n", int(getpid()), mode.c_str());
#else
  std::printf("INFO pid=%d mode=%s\n", int(_getpid()), mode.c_str());
#endif
  return mode == "host" ? RunHost(*adapter, port, dir)
                        : mode == "client" ? RunClient(*adapter, port, dir) : 2;
}
