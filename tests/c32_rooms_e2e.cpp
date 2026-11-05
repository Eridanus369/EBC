// B6.7 C.3.2 E2E: room table + reconnect.
//
// Single process: one host adapter, one client adapter and a probe adapter for
// ListRooms. The host is ticked from a helper thread only during calls that
// wait for a reply (JoinRoom, ListRooms, Reconnect); otherwise both adapters
// are ticked from the main loop.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <atomic>
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

const int kPort = 46068;
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

/** Tick the client until a Disconnect event shows up. */
bool WaitForDisconnect(KX_NetworkAdapter &client, int ms)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    client.Tick();
    KX_NetworkEvent event;
    while (client.PollEvent(event)) {
      if (event.type == KX_NetworkEvent::Disconnect) {
        return true;
      }
    }
    SleepMs(2);
  }
  return false;
}

void TestMock()
{
  KX_MockNetworkAdapter mock;
  mock.SetRoomName("mockroom");
  Check(mock.RoomName() == "mockroom", "mock stores the room name");

  KX_RoomInfo room;
  room.name = "alpha";
  room.host = "127.0.0.1";
  room.port = 46000;
  room.players = 2;
  room.max_players = 4;
  mock.room_list.push_back(room);

  std::vector<KX_RoomInfo> rooms;
  std::string error;
  const bool listed = mock.ListRooms("10.0.0.2", 1234, rooms, &error);
  Check(listed && rooms.size() == 1 && rooms[0].name == "alpha",
        "mock ListRooms returns the programmed table");
  Check(mock.list_called && mock.list_host == "10.0.0.2" && mock.list_port == 1234,
        "mock ListRooms records host/port");
  Check(mock.RefreshRooms("10.0.0.2", 1234, rooms, &error) && mock.refresh_called,
        "mock RefreshRooms re-queries");

  mock.list_result = false;
  Check(!mock.ListRooms("10.0.0.2", 1234, rooms, &error) && !error.empty(),
        "mock ListRooms reports an explicit error");

  int attempts = -1;
  mock.connected = false;
  mock.reconnect_result = false;
  Check(!mock.Reconnect(3, 100, &attempts, &error) && attempts == 3 && !error.empty(),
        "mock Reconnect failure reports attempts and status");

  mock.reconnect_result = true;
  attempts = -1;
  Check(mock.Reconnect(3, 100, &attempts, &error) && attempts == 1 && mock.connected,
        "mock Reconnect succeeds on the first attempt");
  Check(mock.reconnect_calls == 2, "mock Reconnect counted both calls");
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);

  TestMock();

  std::unique_ptr<KX_NetworkAdapter> host = KX_CreateEngineNetworkAdapter();
  std::unique_ptr<KX_NetworkAdapter> client = KX_CreateEngineNetworkAdapter();

  host->SetRoomName("alpha");
  Check(host->RoomName() == "alpha", "host adapter stores the room name");
  Check(host->HostRoom(kPort, 4, nullptr), "host HostRoom");

  bool joined = false;
  WithTicking(*host, 600, [&]() { joined = client->JoinRoom("127.0.0.1", kPort, nullptr); });
  Check(joined && client->LocalPlayerId() >= 1, "client joined the room");
  PumpBoth(*host, *client, 60);

  /* --- room list -------------------------------------------------------- */
  std::unique_ptr<KX_NetworkAdapter> probe = KX_CreateEngineNetworkAdapter();
  std::vector<KX_RoomInfo> rooms;
  std::string error;
  bool listed = false;
  WithTicking(*host, 800, [&]() { listed = probe->ListRooms("127.0.0.1", kPort, rooms, &error); });
  std::printf("INFO list rooms=%zu name='%s' players=%d max=%d error='%s'\n",
              rooms.size(),
              rooms.empty() ? "" : rooms[0].name.c_str(),
              rooms.empty() ? 0 : rooms[0].players,
              rooms.empty() ? 0 : rooms[0].max_players,
              error.c_str());
  Check(listed && rooms.size() == 1, "ListRooms returned exactly one room");
  Check(rooms.size() == 1 && rooms[0].name == "alpha", "ListRooms reports the advertised name");
  Check(rooms.size() == 1 && rooms[0].players == 2, "ListRooms reports the live player count");
  Check(rooms.size() == 1 && rooms[0].max_players == 4, "ListRooms reports the player cap");
  Check(rooms.size() == 1 && rooms[0].host == "127.0.0.1" && rooms[0].port == kPort,
        "ListRooms echoes the queried endpoint");

  bool refreshed = false;
  WithTicking(*host, 800, [&]() { refreshed = probe->RefreshRooms("127.0.0.1", kPort, rooms, nullptr); });
  Check(refreshed && rooms.size() == 1 && rooms[0].name == "alpha",
        "RefreshRooms re-queries the same table");

  /* Closed host: the query must fail with a diagnostic, not hang silently. */
  host->LeaveRoom();
  Check(WaitForDisconnect(*client, 1000), "client saw the host disconnect");
  const auto started = std::chrono::steady_clock::now();
  const bool missing = probe->ListRooms("127.0.0.1", kPort, rooms, &error);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  Check(!missing && !error.empty(), "ListRooms fails explicitly when nobody answers");
  Check(elapsed < std::chrono::seconds(2), "ListRooms failure respects the bounded timeout");
  std::printf("INFO list-failure status='%s' elapsed_ms=%lld\n",
              error.c_str(),
              static_cast<long long>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));

  /* --- reconnect: host comes back --------------------------------------- */
  Check(host->HostRoom(kPort, 4, nullptr), "host re-opened on the same port");
  int attempts = -1;
  bool reconnected = false;
  std::string reconnect_error;
  WithTicking(*host, 2000, [&]() {
    reconnected = client->Reconnect(5, 100, &attempts, &reconnect_error);
  });
  std::printf("INFO reconnect ok=%d attempts=%d status='%s' id=%d\n",
              int(reconnected), attempts, reconnect_error.c_str(), client->LocalPlayerId());
  Check(reconnected, "Reconnect succeeded after the host returned");
  Check(attempts >= 1 && attempts <= 5, "Reconnect reported a bounded attempt count");
  Check(client->IsConnected() && client->LocalPlayerId() >= 1, "client is live with a new id");
  Check(reconnect_error.empty(), "successful Reconnect has an empty status");

  /* The v2 UDP handshake also runs again on the new connection. */
  PumpBoth(*host, *client, 100);
  bool unreliable_ok = false;
  for (int retry = 0; retry < 50 && !unreliable_ok; ++retry) {
    const std::string text = "after-reconnect";
    unreliable_ok = host->SendUnreliable(client->LocalPlayerId(), text.data(), text.size());
    if (!unreliable_ok) {
      PumpBoth(*host, *client, 10);
    }
  }
  Check(unreliable_ok, "host can send unreliable data after the reconnect");
  bool got_after_reconnect = false;
  const auto after_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!got_after_reconnect && std::chrono::steady_clock::now() < after_deadline) {
    host->Tick();
    client->Tick();
    KX_NetworkEvent event;
    while (client->PollEvent(event)) {
      if (event.type == KX_NetworkEvent::Message && !event.reliable &&
          event.data == "after-reconnect") {
        got_after_reconnect = true;
      }
    }
    SleepMs(2);
  }
  Check(got_after_reconnect, "post-reconnect UDP message arrived");

  /* --- reconnect failure: host stays down ------------------------------- */
  host->LeaveRoom();
  Check(WaitForDisconnect(*client, 1000), "client saw the second disconnect");
  attempts = -1;
  reconnect_error.clear();
  const auto fail_started = std::chrono::steady_clock::now();
  const bool failed = client->Reconnect(2, 50, &attempts, &reconnect_error);
  const auto fail_elapsed = std::chrono::steady_clock::now() - fail_started;
  std::printf("INFO reconnect-failure ok=%d attempts=%d status='%s' elapsed_ms=%lld\n",
              int(failed), attempts, reconnect_error.c_str(),
              static_cast<long long>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(fail_elapsed).count()));
  Check(!failed, "Reconnect fails when the host stays down");
  Check(attempts == 2, "Reconnect used exactly the configured attempt budget");
  Check(!reconnect_error.empty(), "failed Reconnect returns a non-empty status");

  std::unique_ptr<KX_NetworkAdapter> fresh = KX_CreateEngineNetworkAdapter();
  attempts = -1;
  reconnect_error.clear();
  Check(!fresh->Reconnect(3, 50, &attempts, &reconnect_error) && attempts == 0 &&
            !reconnect_error.empty(),
        "Reconnect on a never-joined adapter fails with a status");

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
