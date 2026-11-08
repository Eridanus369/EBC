// B6.9-N N-2 E2E: player pose replication across two processes.
//
// Semantics under test (fixed in KX_NetworkAdapterEngine.cpp):
//   - fixed 20 Hz (one PKT_STATE every 50 ms), not per frame;
//   - position/rotation are int16 fixed point (1 cm / 0.01 degree), animation
//     phase is uint16; a state packet is 34 bytes with an animation clip;
//   - loss keeps the last pose: no interpolation and no extrapolation. A newer
//     sequence overwrites directly; stale/reordered packets are dropped.
//
// The "lost tick" is produced by making the host hold its Tick() for 200 ms.
// That window emits no PKT_STATE at all, which is exactly what a receiver sees
// when one tick's datagram is lost. Because the sequence is the 20 Hz tick
// index (not a send counter), the next packet jumps forward and the gap is
// visible on the wire.
//
// Usage:
//   n2_state_sync_two_proc host   <port> <shared_dir>
//   n2_state_sync_two_proc client <port> <shared_dir>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
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

/* Host pose A (before the simulated loss) and B (after it). */
const float kHostA[3] = {1.0f, 2.0f, 3.0f};
const float kHostARot[3] = {10.0f, 20.0f, 30.0f};
const int kHostAAnim = 2;
const float kHostAPhase = 0.25f;

const float kHostB[3] = {5.0f, 0.0f, 0.0f};
const float kHostBRot[3] = {0.0f, 90.0f, 0.0f};
const int kHostBAnim = 3;
const float kHostBPhase = 0.5f;

/* Client pose C, sent client -> host. */
const float kClientPos[3] = {-1.5f, 0.25f, 7.0f};
const float kClientRot[3] = {-5.0f, 15.0f, 45.0f};
const int kClientAnim = 5;
const float kClientPhase = 0.75f;

/* 1 cm position quantisation + 0.01 degree rotation quantisation. */
const float kPositionTolerance = 0.011f;
const float kRotationTolerance = 0.011f;
const float kPhaseTolerance = 2.0f / 65535.0f;

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

bool Near(float actual, float expected, float tolerance)
{
    return std::fabs(actual - expected) <= tolerance;
}

bool PoseMatches(const KX_PlayerState &state,
                 const float *position,
                 const float *rotation,
                 int animation,
                 float phase)
{
  for (int axis = 0; axis < 3; ++axis) {
    if (!Near(state.position[axis], position[axis], kPositionTolerance)) {
      return false;
    }
    if (!Near(state.rotation[axis], rotation[axis], kRotationTolerance)) {
      return false;
    }
  }
  return state.animation == animation && Near(state.animation_phase, phase, kPhaseTolerance);
}

/** Sequence distance as a signed 16-bit difference (wrap aware). */
int SequenceDelta(uint16_t before, uint16_t after)
{
  return int(int16_t(uint16_t(after - before)));
}

void DescribePose(const char *prefix, const KX_PlayerState &state)
{
  std::printf("INFO %s pos=%.3f,%.3f,%.3f rot=%.2f,%.2f,%.2f anim=%d phase=%.4f seq=%u\n",
              prefix,
              state.position[0],
              state.position[1],
              state.position[2],
              state.rotation[0],
              state.rotation[1],
              state.rotation[2],
              state.animation,
              state.animation_phase,
              unsigned(state.sequence));
}

/* ------------------------------------------------------------------------- */
/* Host                                                                       */
/* ------------------------------------------------------------------------- */

int RunHost(KX_NetworkAdapter &adapter, int port, const std::string &dir)
{
  std::string error;
  if (!adapter.HostRoom(port, 4, &error)) {
    std::printf("FAIL HostRoom: %s\n", error.c_str());
    return 1;
  }
  Check(adapter.LocalPlayerId() == 0, "host is player 0");
  WriteFile(Path(dir, "host_ready"), "1");

  const auto join_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (adapter.PlayerCount() < 2 && std::chrono::steady_clock::now() < join_deadline) {
    adapter.Tick();
    SleepMs(2);
  }
  Check(adapter.PlayerCount() >= 2, "client joined over TCP");
  WriteFile(Path(dir, "host_joined"), std::to_string(adapter.PlayerCount()));

  if (!WaitFile(Path(dir, "client_joined"), 3000)) {
    std::printf("FAIL host: client never reported its id\n");
    return 1;
  }
  const int client_id = std::atoi(ReadFile(Path(dir, "client_joined")).c_str());

  /* Send pose A until the client confirms it decoded it. */
  KX_PlayerState state;
  state.position[0] = kHostA[0];
  state.position[1] = kHostA[1];
  state.position[2] = kHostA[2];
  state.rotation[0] = kHostARot[0];
  state.rotation[1] = kHostARot[1];
  state.rotation[2] = kHostARot[2];
  state.animation = kHostAAnim;
  state.animation_phase = kHostAPhase;
  adapter.SetLocalPlayerState(state);

  const auto a_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!FileExists(Path(dir, "client_sees_host")) &&
         std::chrono::steady_clock::now() < a_deadline) {
    adapter.Tick();
    SleepMs(5);
  }
  Check(FileExists(Path(dir, "client_sees_host")), "client confirmed host pose A");
  std::printf("INFO host pose A sent\n");

  /* Receive the client's own pose C while continuing to emit pose A, so the
   * client's baseline sequence reflects the live stream, not a stale queue. */
  const auto sent_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!FileExists(Path(dir, "client_state_sent")) &&
         std::chrono::steady_clock::now() < sent_deadline) {
    adapter.Tick();
    SleepMs(5);
  }
  if (!FileExists(Path(dir, "client_state_sent"))) {
    std::printf("FAIL host: client never reported its pose\n");
    return 1;
  }
  bool have_client = false;
  KX_PlayerState client_state;
  const auto c_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
  while (std::chrono::steady_clock::now() < c_deadline) {
    adapter.Tick();
    if (adapter.GetRemotePlayerState(client_id, &client_state)) {
      have_client = true;
      break;
    }
    SleepMs(5);
  }
  Check(have_client, "host received the client pose");
  if (have_client) {
    DescribePose("host_sees_client", client_state);
    Check(PoseMatches(client_state, kClientPos, kClientRot, kClientAnim, kClientPhase),
          "client pose is within 1 cm / 0.01 degree tolerance");
  }

  /* Keep ticking until the client has drained the queued pose-A packets and is
   * ready to sample, so the hold window starts from the newest sequence. */
  const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!FileExists(Path(dir, "client_ready")) &&
         std::chrono::steady_clock::now() < ready_deadline) {
    adapter.Tick();
    SleepMs(5);
  }
  Check(FileExists(Path(dir, "client_ready")), "client drained queued poses before the hold");

  /* Simulated packet loss: 200 ms without Tick(), so no PKT_STATE is emitted.
   * A receiver cannot tell this apart from one lost datagram. */
  WriteFile(Path(dir, "hold_start"), "1");
  SleepMs(200);
  WriteFile(Path(dir, "hold_end"), "1");
  SleepMs(30);

  state.position[0] = kHostB[0];
  state.position[1] = kHostB[1];
  state.position[2] = kHostB[2];
  state.rotation[0] = kHostBRot[0];
  state.rotation[1] = kHostBRot[1];
  state.rotation[2] = kHostBRot[2];
  state.animation = kHostBAnim;
  state.animation_phase = kHostBPhase;
  adapter.SetLocalPlayerState(state);

  const auto b_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!FileExists(Path(dir, "client_sees_b")) &&
         std::chrono::steady_clock::now() < b_deadline) {
    adapter.Tick();
    SleepMs(5);
  }
  Check(FileExists(Path(dir, "client_sees_b")), "client confirmed host pose B after the gap");

  if (!WaitFile(Path(dir, "client_done"), 3000)) {
    std::printf("FAIL host: client never finished\n");
    return 1;
  }
  WriteFile(Path(dir, "host_done"), "1");
  adapter.LeaveRoom();
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------------- */
/* Client                                                                     */
/* ------------------------------------------------------------------------- */

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

  KX_PlayerState state;
  state.position[0] = kClientPos[0];
  state.position[1] = kClientPos[1];
  state.position[2] = kClientPos[2];
  state.rotation[0] = kClientRot[0];
  state.rotation[1] = kClientRot[1];
  state.rotation[2] = kClientRot[2];
  state.animation = kClientAnim;
  state.animation_phase = kClientPhase;
  adapter.SetLocalPlayerState(state);

  if (!WaitFile(Path(dir, "host_joined"), 3000)) {
    std::printf("FAIL client: host never reported host_joined\n");
    return 1;
  }

  /* Receive host pose A. */
  bool have_host = false;
  KX_PlayerState host_state;
  const auto a_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!have_host && std::chrono::steady_clock::now() < a_deadline) {
    adapter.Tick();
    if (adapter.GetRemotePlayerState(0, &host_state)) {
      have_host = true;
      break;
    }
    SleepMs(5);
  }
  Check(have_host, "client received host pose A");
  if (have_host) {
    DescribePose("client_sees_host", host_state);
    Check(PoseMatches(host_state, kHostA, kHostARot, kHostAAnim, kHostAPhase),
          "host pose A is within 1 cm / 0.01 degree tolerance");
    WriteFile(Path(dir, "client_sees_host"), std::to_string(host_state.sequence));
  }

  /* Keep sending pose C long enough for the host to see at least one packet,
   * and drain queued pose-A packets so the hold starts at the newest sequence. */
  const auto c_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
  while (std::chrono::steady_clock::now() < c_deadline) {
    adapter.Tick();
    SleepMs(5);
  }
  KX_PlayerState drain_probe;
  if (adapter.GetRemotePlayerState(0, &drain_probe)) {
    std::printf("INFO client_after_c_loop seq=%u\n", unsigned(drain_probe.sequence));
  }
  WriteFile(Path(dir, "client_state_sent"), "1");
  WriteFile(Path(dir, "client_ready"), "1");

  /* Missing-tick window: host emits nothing for 200 ms. The last pose must be
   * held; there must be no interpolation or extrapolation. Keep ticking while
   * waiting, so the baseline is the newest pose rather than a stale queue. */
  const auto start_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!FileExists(Path(dir, "hold_start")) &&
         std::chrono::steady_clock::now() < start_deadline) {
    adapter.Tick();
    SleepMs(3);
  }
  if (!FileExists(Path(dir, "hold_start"))) {
    std::printf("FAIL client: host never started the loss window\n");
    return 1;
  }
  uint16_t seq_before = 0;
  KX_PlayerState at_hold;
  if (!adapter.GetRemotePlayerState(0, &at_hold)) {
    std::printf("FAIL client: no host pose to hold\n");
    return 1;
  }
  seq_before = at_hold.sequence;
  float min_position[3] = {at_hold.position[0], at_hold.position[1], at_hold.position[2]};
  float max_position[3] = {at_hold.position[0], at_hold.position[1], at_hold.position[2]};
  KX_PlayerState sample;
  const auto hold_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(260);
  while (std::chrono::steady_clock::now() < hold_deadline &&
         !FileExists(Path(dir, "hold_end"))) {
    adapter.Tick();
    if (adapter.GetRemotePlayerState(0, &sample)) {
      for (int axis = 0; axis < 3; ++axis) {
        min_position[axis] = std::min(min_position[axis], sample.position[axis]);
        max_position[axis] = std::max(max_position[axis], sample.position[axis]);
      }
    }
    SleepMs(2);
  }
  Check(FileExists(Path(dir, "hold_end")), "host finished the loss window");
  const bool held = std::fabs(max_position[0] - min_position[0]) < 1e-5f &&
                    std::fabs(max_position[1] - min_position[1]) < 1e-5f &&
                    std::fabs(max_position[2] - min_position[2]) < 1e-5f;
  Check(held, "pose is held during the missing tick (no interpolation/extrapolation)");
  std::printf("INFO hold seq_before=%u range=%.5f,%.5f,%.5f\n",
              unsigned(seq_before),
              max_position[0] - min_position[0],
              max_position[1] - min_position[1],
              max_position[2] - min_position[2]);

  /* Pose B must arrive after the gap and overwrite A directly. */
  KX_PlayerState after;
  bool saw_b = false;
  int seq_after = 0;
  const auto b_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < b_deadline) {
    adapter.Tick();
    if (adapter.GetRemotePlayerState(0, &after)) {
      if (PoseMatches(after, kHostB, kHostBRot, kHostBAnim, kHostBPhase)) {
        saw_b = true;
        seq_after = int(after.sequence);
        break;
      }
    }
    SleepMs(3);
  }
  Check(saw_b, "client applied host pose B after the gap");
  if (saw_b) {
    DescribePose("client_sees_b", after);
    const int delta = SequenceDelta(seq_before, uint16_t(seq_after));
    std::printf("INFO sequence_delta=%d\n", delta);
    Check(delta >= 2, "20 Hz tick sequence jumped by the missed tick(s)");
  }
  WriteFile(Path(dir, "client_sees_b"), "1");

  WriteFile(Path(dir, "client_done"), "1");
  WaitFile(Path(dir, "host_done"), 3000);
  adapter.LeaveRoom();
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
#ifndef _WIN32
  std::printf("INFO pid=%d mode=%s\n", int(getpid()), mode.c_str());
#else
  std::printf("INFO pid=%d mode=%s\n", int(_getpid()), mode.c_str());
#endif
  return mode == "host" ? RunHost(*adapter, port, dir)
                        : mode == "client" ? RunClient(*adapter, port, dir) : 2;
}
