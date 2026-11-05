// B6.3 LAN network E2E: two real processes over a real TCP socket.
//
// Parent hosts, child joins on 127.0.0.1. The parent broadcasts, the child
// answers with a targeted message, then both sides exchange sync properties.
// The child leaves at the end so the parent sees the PlayerLeave event.
#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include "KX_GameAdapters.h"

/* B5.2 and B6.2 are written by parallel lines; stub their registration so this
 * harness only links the B6.3 sources. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

namespace {

const int kPort = 46063;
const char *kHostMessage = "ping-from-host";
const char *kClientMessage = "pong-from-client";

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

bool PumpUntil(KX_NetworkAdapter &adapter,
               KX_NetworkEvent::Type type,
               int timeout_ms,
               KX_NetworkEvent *sink = nullptr)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    adapter.Tick();
    KX_NetworkEvent event;
    while (adapter.PollEvent(event)) {
      if (sink != nullptr) {
        *sink = event;
      }
      if (event.type == type) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

int ChildMain()
{
  std::unique_ptr<KX_NetworkAdapter> adapter = KX_CreateEngineNetworkAdapter();
  KX_NetworkEvent event;
  const bool joined = adapter->JoinRoom("127.0.0.1", kPort, nullptr);
  Check(joined, "child JoinRoom");
  std::printf("INFO child after join id=%d role=%d connected=%d error='%s'\n",
              adapter->LocalPlayerId(),
              int(adapter->GetRole()),
              int(adapter->IsConnected()),
              adapter->LastError().c_str());
  Check(adapter->LocalPlayerId() >= 1, "child got a non host player id");

  Check(PumpUntil(*adapter, KX_NetworkEvent::Message, 3000, &event), "child received host broadcast");
  std::printf("INFO child after pump id=%d error='%s' event_type=%d data='%s'\n",
              adapter->LocalPlayerId(),
              adapter->LastError().c_str(),
              int(event.type),
              event.data.c_str());
  Check(event.data == kHostMessage, "child broadcast payload intact");

  const bool sent = adapter->SendReliable(0, kClientMessage, std::strlen(kClientMessage));
  Check(sent, "child SendReliable to host id 0");

  /* The host writes "score" right after the broadcast; read it back through
   * the replicated property snapshot. */
  KX_RuntimeValue remote;
  bool got_score = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
  while (!got_score && std::chrono::steady_clock::now() < deadline) {
    adapter->Tick();
    got_score = adapter->GetRemoteProperty(0, "score", &remote);
    if (!got_score) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  Check(got_score, "child read host property");
  Check(remote.type == KX_VALUE_FLOAT && remote.data.f == 42.5f,
        "child saw host float property");

  adapter->SetSyncProperty("name", KX_RuntimeValue::String("bob"));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  adapter->Tick();

  adapter->LeaveRoom();
  std::printf("CHILD_DONE\n");
  return 0;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  pid_t child = fork();
  if (child == 0) {
    alarm(6);
    return ChildMain();
  }

  std::unique_ptr<KX_NetworkAdapter> host = KX_CreateEngineNetworkAdapter();
  std::string error;
  Check(host->HostRoom(kPort, 4, &error), "host HostRoom bound the LAN port");
  Check(error.empty(), "host has no bind diagnostic");

  KX_NetworkEvent event;
  Check(PumpUntil(*host, KX_NetworkEvent::PlayerJoin, 4000, &event), "host saw PlayerJoin");
  std::printf("INFO host players=%d join_id=%d\n", host->PlayerCount(), event.player_id);
  Check(host->PlayerCount() == 2, "host PlayerCount is two");

  Check(host->BroadcastReliable(kHostMessage, std::strlen(kHostMessage)),
        "host BroadcastReliable queued");

  Check(PumpUntil(*host, KX_NetworkEvent::Message, 3000, &event), "host received client reply");
  Check(event.data == kClientMessage, "client reply payload intact");
  const int client_id = event.player_id;
  std::printf("INFO host received reply from player=%d\n", client_id);

  host->SetSyncProperty("score", KX_RuntimeValue::Float(42.5f));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  host->Tick();
  KX_RuntimeValue value;
  bool got_name = false;
  const auto name_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
  while (!got_name && std::chrono::steady_clock::now() < name_deadline) {
    host->Tick();
    got_name = host->GetRemoteProperty(client_id, "name", &value);
    if (!got_name) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  Check(got_name, "host read child property");
  Check(value.type == KX_VALUE_STRING && std::strcmp(value.AsString(), "bob") == 0,
        "host saw child string property");

  /* The child sets its string property right before leaving, so wait for it
   * before checking the leave event. */
  Check(PumpUntil(*host, KX_NetworkEvent::PlayerLeave, 3000, &event), "host saw PlayerLeave");

  host->LeaveRoom();
  int status = 0;
  waitpid(child, &status, 0);
  std::printf("INFO child status=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}
