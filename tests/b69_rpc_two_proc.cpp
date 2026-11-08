// B6.9-Ms Ms-1 two-process RPC E2E.
//
// Two independent OS processes run real KX_NetworkAdapter sockets:
//   client: real SendRPC node + two SendNetworkMessage nodes, broadcast reliably;
//   host:   real OnRPC node followed by a real OnNetworkEvent node.
//
// Asserted here:
//   - the wire envelope is "EBCRPC1" + u32 subject_len + u32 payload_len +
//     subject + payload, little endian, 15 byte fixed header;
//   - a non-ASCII subject and a payload containing separators survive the
//     round trip byte-exactly and are restored across processes;
//   - Sender is "player:<client id>";
//   - plain (non-RPC) network messages that OnRPC drains while looking for
//     RPC envelopes are requeued at the FRONT in reverse order, so
//     OnNetworkEvent still sees them in their original order and never sees an
//     EBCRPC1 envelope.
//
// Usage:
//   b69_rpc_two_proc host   <port> <shared_dir>
//   b69_rpc_two_proc client <port> <shared_dir>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_FileAdapter.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_GameNodeRuntime.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

namespace {

const char *kSubject = "同步/通道";
const char *kPayload = "p|q,r;s/t-v";
const char *kPlainOne = "plain-1";
const char *kPlainTwo = "plain-2";

int g_pass = 0;
int g_fail = 0;

void Check(bool condition, const std::string &label)
{
  if (condition) {
    ++g_pass;
    std::printf("PASS %s\n", label.c_str());
  }
  else {
    ++g_fail;
    std::printf("FAIL %s\n", label.c_str());
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

bool WaitFile(const std::string &path, int timeout_ms)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (FileExists(path)) {
      return true;
    }
    SleepMs(5);
  }
  return FileExists(path);
}

void WriteFile(const std::string &path, const std::string &text)
{
  std::ofstream out(path, std::ios::trunc);
  out << text << "\n";
}

/* ------------------------------------------------------------------------- */
/* Harness nodes                                                             */
/* ------------------------------------------------------------------------- */

bool g_gates[4] = {false};
int g_ints[2] = {0};
int g_rpc_success = 0;
int g_rpc_fail = 0;
int g_plain1_success = 0;
int g_plain2_success = 0;

/* Host-side captures. */
std::vector<std::string> g_rpc_subjects;
std::vector<std::string> g_rpc_payloads;
std::vector<std::string> g_rpc_senders;
std::vector<std::string> g_message_data;
int g_join_player_id = -1;

class GateNode : public KX_GameNodeRuntime {
 public:
  explicit GateNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    SetOutputTrigger(ctx, 0, g_gates[slot_]);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

class IntNode : public KX_GameNodeRuntime {
 public:
  explicit IntNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Int(g_ints[slot_]);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

/** Counts trigger hits on input 0; used for SendRPC/SendNetworkMessage. */
class CountNode : public KX_GameNodeRuntime {
 public:
  explicit CountNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      if (slot_ == 0) {
        ++g_rpc_success;
      }
      else if (slot_ == 1) {
        ++g_plain1_success;
      }
      else if (slot_ == 2) {
        ++g_plain2_success;
      }
      else {
        ++g_rpc_fail;
      }
    }
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

/** Captures OnRPC Subject/Payload/Sender at the element trigger. */
class RpcCaptureNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      const char *subject = ReadInput(ctx, 1).AsString();
      const char *payload = ReadInput(ctx, 2).AsString();
      const char *sender = ReadInput(ctx, 3).AsString();
      g_rpc_subjects.push_back(subject != nullptr ? subject : "");
      g_rpc_payloads.push_back(payload != nullptr ? payload : "");
      g_rpc_senders.push_back(sender != nullptr ? sender : "");
    }
    return KX_ExecResult::Continue;
  }
};

/** Captures OnNetworkEvent Data at the OnMessage trigger. */
class MessageCaptureNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      const char *data = ReadInput(ctx, 1).AsString();
      g_message_data.push_back(data != nullptr ? data : "");
    }
    return KX_ExecResult::Continue;
  }
};

/** Captures OnNetworkEvent PlayerId at the OnPlayerJoin trigger. */
class JoinCaptureNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      g_join_player_id = ReadInput(ctx, 1).AsInt();
    }
    return KX_ExecResult::Continue;
  }
};

KX_NodeSignature GateSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Out";
  output.kind = IRPortKind::Trigger;
  output.value_type = KX_VALUE_NONE;
  signature.outputs.push_back(output);
  return signature;
}

KX_NodeSignature IntSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Value";
  output.kind = IRPortKind::Value;
  output.value_type = KX_VALUE_INT;
  signature.outputs.push_back(output);
  return signature;
}

KX_NodeSignature CountSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature input;
  input.name = "In";
  input.kind = IRPortKind::Trigger;
  input.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(input);
  return signature;
}

KX_NodeSignature RpcCaptureSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature input;
  input.name = "In";
  input.kind = IRPortKind::Trigger;
  input.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(input);
  for (const char *name : {"Subject", "Payload", "Sender"}) {
    KX_NodePortSignature value;
    value.name = name;
    value.kind = IRPortKind::Value;
    value.value_type = KX_VALUE_STRING;
    signature.inputs.push_back(value);
  }
  return signature;
}

KX_NodeSignature MessageCaptureSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature trigger;
  trigger.name = "In";
  trigger.kind = IRPortKind::Trigger;
  trigger.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(trigger);
  KX_NodePortSignature data;
  data.name = "Data";
  data.kind = IRPortKind::Value;
  data.value_type = KX_VALUE_STRING;
  signature.inputs.push_back(data);
  return signature;
}

KX_NodeSignature JoinCaptureSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature trigger;
  trigger.name = "In";
  trigger.kind = IRPortKind::Trigger;
  trigger.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(trigger);
  KX_NodePortSignature id;
  id.name = "PlayerId";
  id.kind = IRPortKind::Value;
  id.value_type = KX_VALUE_INT;
  signature.inputs.push_back(id);
  return signature;
}

void RegisterHarnessNodes()
{
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  for (int slot = 0; slot < 4; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69RGate" + std::to_string(slot),
                          "B69RGate" + std::to_string(slot),
                          GateSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new GateNode(captured));
                          });
  }
  for (int slot = 0; slot < 2; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69RInt" + std::to_string(slot),
                          "B69RInt" + std::to_string(slot),
                          IntSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new IntNode(captured));
                          });
  }
  for (int slot = 0; slot < 4; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69RCount" + std::to_string(slot),
                          "B69RCount" + std::to_string(slot),
                          CountSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new CountNode(captured));
                          });
  }
  registry.RegisterType("B69RRpcCapture",
                        "B69RRpcCapture",
                        RpcCaptureSignature(),
                        []() {
                          return std::unique_ptr<KX_GameNodeRuntime>(new RpcCaptureNode());
                        });
  registry.RegisterType("B69RMsgCapture",
                        "B69RMsgCapture",
                        MessageCaptureSignature(),
                        []() {
                          return std::unique_ptr<KX_GameNodeRuntime>(new MessageCaptureNode());
                        });
  registry.RegisterType("B69RJoinCapture",
                        "B69RJoinCapture",
                        JoinCaptureSignature(),
                        []() {
                          return std::unique_ptr<KX_GameNodeRuntime>(new JoinCaptureNode());
                        });
}

IRNode MakeNode(GraphIR &ir, const std::string &type_id, const std::string &name)
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = name;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  const KX_NodeTypeId id = registry.FindByEditorType(type_id);
  const KX_NodeSignature *signature = registry.GetSignature(id);
  if (signature == nullptr) {
    std::fprintf(stderr, "unknown node type %s\n", type_id.c_str());
  }
  else {
    for (const KX_NodePortSignature &input : signature->inputs) {
      IRPort port;
      port.name = input.name;
      port.kind = input.kind;
      port.value_tag = input.value_type;
      node.inputs.push_back(port);
    }
    for (const KX_NodePortSignature &output : signature->outputs) {
      IRPort port;
      port.name = output.name;
      port.kind = output.kind;
      port.value_tag = output.value_type;
      node.outputs.push_back(port);
    }
  }
  ir.nodes.push_back(node);
  return ir.nodes.back();
}

int AddNode(GraphIR &ir, const std::string &type_id, const std::string &name)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, type_id, name);
  return index;
}

void Connect(GraphIR &ir, int from_node, int from_port, int to_node, int to_port)
{
  IRConnection connection;
  connection.from_node = from_node;
  connection.from_port = from_port;
  connection.to_node = to_node;
  connection.to_port = to_port;
  ir.connections.push_back(connection);
}

int AddString(GraphIR &ir, const std::string &value, const std::string &name)
{
  const int index = AddNode(ir, "StringConst", name);
  ir.nodes.back().config_json = "{\"value\": \"" + value + "\"}";
  return index;
}

int AddGate(GraphIR &ir, int slot)
{
  return AddNode(ir, "B69RGate" + std::to_string(slot), "gate" + std::to_string(slot));
}

int AddInt(GraphIR &ir, int slot, int value)
{
  g_ints[slot] = value;
  return AddNode(ir, "B69RInt" + std::to_string(slot), "int" + std::to_string(slot));
}

int AddCount(GraphIR &ir, int slot)
{
  return AddNode(ir, "B69RCount" + std::to_string(slot), "count" + std::to_string(slot));
}

/** Host graph: OnRPC first, then OnNetworkEvent, then capture sinks. */
GraphIR BuildHostGraph(int *rpc_node, int *event_node)
{
  GraphIR ir;
  *rpc_node = AddNode(ir, "OnRPC", "onrpc");
  *event_node = AddNode(ir, "OnNetworkEvent", "onevent");

  const int rpc_capture = AddNode(ir, "B69RRpcCapture", "rpc_capture");
  Connect(ir, *rpc_node, 0, rpc_capture, 0);
  Connect(ir, *rpc_node, 1, rpc_capture, 1);
  Connect(ir, *rpc_node, 2, rpc_capture, 2);
  Connect(ir, *rpc_node, 3, rpc_capture, 3);

  const int msg_capture = AddNode(ir, "B69RMsgCapture", "msg_capture");
  Connect(ir, *event_node, 2, msg_capture, 0);
  Connect(ir, *event_node, 5, msg_capture, 1);

  const int join_capture = AddNode(ir, "B69RJoinCapture", "join_capture");
  Connect(ir, *event_node, 0, join_capture, 0);
  Connect(ir, *event_node, 4, join_capture, 1);
  return ir;
}

/** Client graph: two plain messages first, then the RPC, all in one frame. */
GraphIR BuildClientGraph()
{
  GraphIR ir;
  const int gate_rpc = AddGate(ir, 0);
  const int gate_one = AddGate(ir, 1);
  const int gate_two = AddGate(ir, 2);

  const int plain_one = AddNode(ir, "SendNetworkMessage", "plain_one");
  ir.nodes.back().config_json = "{\"reliable\": true}";
  Connect(ir, gate_one, 0, plain_one, 0);
  Connect(ir, AddInt(ir, 0, 0), 0, plain_one, 1);
  Connect(ir, AddString(ir, kPlainOne, "plain_one_data"), 0, plain_one, 2);
  Connect(ir, plain_one, 0, AddCount(ir, 1), 0);

  const int plain_two = AddNode(ir, "SendNetworkMessage", "plain_two");
  ir.nodes.back().config_json = "{\"reliable\": true}";
  Connect(ir, gate_two, 0, plain_two, 0);
  Connect(ir, AddInt(ir, 0, 0), 0, plain_two, 1);
  Connect(ir, AddString(ir, kPlainTwo, "plain_two_data"), 0, plain_two, 2);
  Connect(ir, plain_two, 0, AddCount(ir, 2), 0);

  const int rpc = AddNode(ir, "SendRPC", "send_rpc");
  Connect(ir, gate_rpc, 0, rpc, 0);
  Connect(ir, AddString(ir, kSubject, "subject"), 0, rpc, 1);
  Connect(ir, AddString(ir, kPayload, "payload"), 0, rpc, 2);
  Connect(ir, rpc, 0, AddCount(ir, 0), 0);
  Connect(ir, rpc, 1, AddCount(ir, 3), 0);
  return ir;
}

/* ------------------------------------------------------------------------- */
/* Client-side tap: records the exact bytes handed to BroadcastReliable.      */
/* ------------------------------------------------------------------------- */

class TapAdapter : public KX_NetworkAdapter {
 public:
  explicit TapAdapter(std::unique_ptr<KX_NetworkAdapter> inner) : inner_(std::move(inner)) {}

  std::vector<std::string> broadcast_packets;

  bool HostRoom(int port, int max_players, std::string *error = nullptr) override
  {
    return inner_->HostRoom(port, max_players, error);
  }
  bool JoinRoom(const std::string &host, int port, std::string *error = nullptr) override
  {
    return inner_->JoinRoom(host, port, error);
  }
  void LeaveRoom() override { inner_->LeaveRoom(); }
  int PlayerCount() const override { return inner_->PlayerCount(); }
  int LocalPlayerId() const override { return inner_->LocalPlayerId(); }
  bool SendReliable(int target_id, const void *data, size_t len) override
  {
    return inner_->SendReliable(target_id, data, len);
  }
  bool SendUnreliable(int target_id, const void *data, size_t len) override
  {
    return inner_->SendUnreliable(target_id, data, len);
  }
  bool BroadcastReliable(const void *data, size_t len) override
  {
    broadcast_packets.emplace_back(static_cast<const char *>(data), len);
    return inner_->BroadcastReliable(data, len);
  }
  bool PollEvent(KX_NetworkEvent &out) override { return inner_->PollEvent(out); }
  void RequeueEvent(const KX_NetworkEvent &event) override { inner_->RequeueEvent(event); }
  void Tick() override { inner_->Tick(); }
  Role GetRole() const override { return inner_->GetRole(); }
  bool IsConnected() const override { return inner_->IsConnected(); }
  std::string LastError() const override { return inner_->LastError(); }
  void SetRoomName(const std::string &name) override { inner_->SetRoomName(name); }
  void SetLocalPlayerState(const KX_PlayerState &state) override
  {
    inner_->SetLocalPlayerState(state);
  }
  bool GetRemotePlayerState(int player_id, KX_PlayerState *out) const override
  {
    return inner_->GetRemotePlayerState(player_id, out);
  }

 private:
  std::unique_ptr<KX_NetworkAdapter> inner_;
};

/* ------------------------------------------------------------------------- */
/* Shared setup                                                              */
/* ------------------------------------------------------------------------- */

std::unique_ptr<KX_GraphInstance> MakeInstance(GraphIR &ir)
{
  std::unique_ptr<KX_GraphInstance> instance(new KX_GraphInstance(nullptr, nullptr, nullptr));
  std::vector<std::string> warnings;
  if (!instance->Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    return nullptr;
  }
  return instance;
}

void BindAdapters(KX_GraphInstance &instance, KX_NetworkAdapter &network)
{
  instance.GetContext()->SetAdapters(nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &KX_NullMessageAdapter(),
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &KX_NullFileAdapter(),
                                     &network);
}

/* ------------------------------------------------------------------------- */
/* Host                                                                      */
/* ------------------------------------------------------------------------- */

int RunHost(int port, const std::string &dir)
{
  std::unique_ptr<KX_NetworkAdapter> adapter = KX_CreateEngineNetworkAdapter();
  std::string error;
  Check(adapter->HostRoom(port, 4, &error), "host: HostRoom succeeded");
  WriteFile(Path(dir, "host_ready"), "1");

  int rpc_node = -1;
  int event_node = -1;
  GraphIR ir = BuildHostGraph(&rpc_node, &event_node);
  std::unique_ptr<KX_GraphInstance> instance = MakeInstance(ir);
  if (!instance) {
    std::printf("FAIL host: graph did not compile\n");
    return 1;
  }
  BindAdapters(*instance, *adapter);

  const auto join_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (adapter->PlayerCount() < 2 && std::chrono::steady_clock::now() < join_deadline) {
    adapter->Tick();
    SleepMs(2);
  }
  Check(adapter->PlayerCount() >= 2, "host: client joined over the network adapter");

  bool saw_same_tick_requeue = false;
  size_t rpc_before = 0;
  size_t msg_before = 0;
  const auto event_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < event_deadline) {
    rpc_before = g_rpc_subjects.size();
    msg_before = g_message_data.size();
    adapter->Tick();
    instance->Tick(1.0f / 60.0f);
    /* OnRPC runs before OnNetworkEvent in this graph. A plain message that
     * OnRPC requeued must therefore surface in the same tick. */
    if (g_rpc_subjects.size() > rpc_before && g_message_data.size() > msg_before) {
      saw_same_tick_requeue = true;
    }
    if (!g_rpc_subjects.empty() && !g_message_data.empty() &&
        std::find(g_message_data.begin(), g_message_data.end(), kPlainOne) != g_message_data.end() &&
        std::find(g_message_data.begin(), g_message_data.end(), kPlainTwo) != g_message_data.end()) {
      break;
    }
    SleepMs(2);
  }

  Check(!g_rpc_subjects.empty(), "host: OnRPC received the RPC envelope");
  Check(!g_rpc_subjects.empty() && g_rpc_subjects.front() == kSubject,
        "host: subject round-trips byte-exactly across processes (non-ASCII + '/')");
  Check(!g_rpc_payloads.empty() && g_rpc_payloads.front() == kPayload,
        "host: payload round-trips byte-exactly across processes (separators)");
  const bool sender_ok = !g_rpc_senders.empty() && g_join_player_id >= 0 &&
                         g_rpc_senders.front() == "player:" + std::to_string(g_join_player_id);
  Check(sender_ok, "host: Sender is 'player:<client id>'");

  Check(std::find(g_message_data.begin(), g_message_data.end(), kPlainOne) != g_message_data.end(),
        "host: plain message 1 survived OnRPC draining the queue");
  Check(std::find(g_message_data.begin(), g_message_data.end(), kPlainTwo) != g_message_data.end(),
        "host: plain message 2 survived OnRPC draining the queue");

  bool order_ok = false;
  {
    const auto first = std::find(g_message_data.begin(), g_message_data.end(), kPlainOne);
    const auto second = std::find(g_message_data.begin(), g_message_data.end(), kPlainTwo);
    order_ok = first != g_message_data.end() && second != g_message_data.end() && first < second;
  }
  Check(order_ok, "host: plain messages keep their original order after reverse requeue");
  Check(saw_same_tick_requeue, "host: RPC and requeued plain events surfaced in the same tick");

  bool no_envelope_leak = true;
  for (const std::string &data : g_message_data) {
    if (data.compare(0, 7, "EBCRPC1") == 0) {
      no_envelope_leak = false;
    }
  }
  Check(no_envelope_leak, "host: OnNetworkEvent never sees an EBCRPC1 envelope");

  WriteFile(Path(dir, "host_ack"), "1");
  if (!WaitFile(Path(dir, "client_done"), 3000)) {
    std::printf("FAIL host: client never finished\n");
    return 1;
  }
  adapter->LeaveRoom();
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Client                                                                    */
/* ------------------------------------------------------------------------- */

int RunClient(int port, const std::string &dir)
{
  if (!WaitFile(Path(dir, "host_ready"), 5000)) {
    std::printf("FAIL client: host never became ready\n");
    return 1;
  }
  std::unique_ptr<KX_NetworkAdapter> inner = KX_CreateEngineNetworkAdapter();
  TapAdapter adapter(std::move(inner));
  std::string error;
  Check(adapter.JoinRoom("127.0.0.1", port, &error), "client: JoinRoom succeeded");

  GraphIR ir = BuildClientGraph();
  std::unique_ptr<KX_GraphInstance> instance = MakeInstance(ir);
  if (!instance) {
    std::printf("FAIL client: graph did not compile\n");
    return 1;
  }
  BindAdapters(*instance, adapter);

  const auto join_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (adapter.PlayerCount() < 2 && std::chrono::steady_clock::now() < join_deadline) {
    adapter.Tick();
    instance->Tick(1.0f / 60.0f);
    SleepMs(2);
  }
  Check(adapter.PlayerCount() >= 2, "client: joined the room");
  Check(adapter.IsConnected(), "client: adapter reports connected (RPC takes the network path)");

  g_gates[1] = true;
  g_gates[2] = true;
  g_gates[0] = true;
  const auto send_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < send_deadline &&
         !FileExists(Path(dir, "host_ack"))) {
    adapter.Tick();
    instance->Tick(1.0f / 60.0f);
    SleepMs(10);
  }
  g_gates[0] = false;
  g_gates[1] = false;
  g_gates[2] = false;

  Check(g_rpc_success > 0, "client: SendRPC took OnSuccess");
  Check(g_plain1_success > 0 && g_plain2_success > 0,
        "client: both SendNetworkMessage nodes took OnSuccess");
  Check(!adapter.broadcast_packets.empty(), "client: BroadcastReliable was called for the RPC");

  /* Envelope layout: magic + u32 subject_len + u32 payload_len + subject + payload. */
  bool layout_ok = false;
  bool magic_ok = false;
  bool subject_len_ok = false;
  bool payload_len_ok = false;
  bool subject_ok = false;
  bool payload_ok = false;
  if (!adapter.broadcast_packets.empty()) {
    const std::string &packet = adapter.broadcast_packets.front();
    const std::string subject(kSubject);
    const std::string payload(kPayload);
    const size_t header = 7 + 8;
    layout_ok = packet.size() == header + subject.size() + payload.size();
    magic_ok = packet.size() >= 7 && packet.compare(0, 7, "EBCRPC1") == 0;
    if (packet.size() >= header) {
      const uint32_t subject_len = uint32_t(uint8_t(packet[7])) |
                                  (uint32_t(uint8_t(packet[8])) << 8) |
                                  (uint32_t(uint8_t(packet[9])) << 16) |
                                  (uint32_t(uint8_t(packet[10])) << 24);
      const uint32_t payload_len = uint32_t(uint8_t(packet[11])) |
                                    (uint32_t(uint8_t(packet[12])) << 8) |
                                    (uint32_t(uint8_t(packet[13])) << 16) |
                                    (uint32_t(uint8_t(packet[14])) << 24);
      subject_len_ok = subject_len == subject.size();
      payload_len_ok = payload_len == payload.size();
      subject_ok = packet.size() >= header + subject_len &&
                   packet.compare(header, subject_len, subject) == 0;
      payload_ok = packet.size() >= header + subject_len + payload_len &&
                   packet.compare(header + subject_len, payload_len, payload) == 0;
    }
  }
  Check(layout_ok, "envelope: total length is 15 + subject_len + payload_len");
  Check(magic_ok, "envelope: magic is EBCRPC1");
  Check(subject_len_ok, "envelope: u32 subject_len matches the subject bytes");
  Check(payload_len_ok, "envelope: u32 payload_len matches the payload bytes");
  Check(subject_ok, "envelope: subject bytes are stored verbatim");
  Check(payload_ok, "envelope: payload bytes are stored verbatim");

  WriteFile(Path(dir, "client_done"), "1");
  adapter.LeaveRoom();
  return 0;
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc < 4) {
    std::fprintf(stderr,
                 "usage: %s host|client <port> <shared_dir>\n",
                 argc > 0 ? argv[0] : "b69_rpc_two_proc");
    return 2;
  }
  RegisterBuiltinGameNodes();
  RegisterHarnessNodes();

  const std::string mode = argv[1];
  const int port = std::atoi(argv[2]);
  const std::string dir = argv[3];
  int rc = 1;
  if (mode == "host") {
    rc = RunHost(port, dir);
  }
  else if (mode == "client") {
    rc = RunClient(port, dir);
  }
  else {
    std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
    return 2;
  }
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::printf("RESULT assertions=%d failures=%d\n", g_pass + g_fail, g_fail);
  return (rc == 0 && g_fail == 0) ? 0 : 1;
}
