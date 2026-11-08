/*
 * B6.9-Ms E2E: SendRPC / OnRPC / SendDelayedMessage.
 *
 * Local loopback is exercised through the mock message bus (no sleep: every
 * Tick advances a fixed delta). Network receive is exercised by injecting a
 * Message event into the mock network queue.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

namespace {

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

const int kGateCount = 6;
bool g_gates[kGateCount] = {false};
int g_sink_hits[7] = {0};

class TestGateNode : public KX_GameNodeRuntime {
 public:
  explicit TestGateNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    SetOutputTrigger(ctx, 0, g_gates[slot_]);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

class TestSinkNode : public KX_GameNodeRuntime {
 public:
  explicit TestSinkNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      ++g_sink_hits[slot_];
    }
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

/**
 * One-frame message bus for the test: Receive consumes the pending batch, so a
 * single Send is delivered exactly once regardless of node execution order.
 */
class TestMessageAdapter : public KX_MessageAdapter {
 public:
  struct Delayed {
    KX_Message message;
    KX_GameObject *from = nullptr;
    float remaining = 0.0f;
  };

  std::vector<KX_Message> pending;
  std::vector<KX_Message> visible;
  std::vector<Delayed> delayed;
  size_t delayed_send_calls = 0;
  size_t dropped_delayed = 0;

  void Send(const std::string &to,
            KX_GameObject *from,
            const std::string &subject,
            const std::string &body) override
  {
    KX_Message message;
    message.to = to;
    message.from = from != nullptr ? "local" : std::string();
    message.subject = subject;
    message.body = body;
    pending.push_back(message);
  }

  std::vector<KX_Message> Receive(const std::string &to, const std::string &subject) override
  {
    std::vector<KX_Message> out;
    for (const KX_Message &message : visible) {
      if (!message.to.empty() && message.to != to) {
        continue;
      }
      if (!subject.empty() && message.subject != subject) {
        continue;
      }
      out.push_back(message);
    }
    return out;
  }

  void SendDelayed(const std::string &to,
                   KX_GameObject *from,
                   const std::string &subject,
                   const std::string &body,
                   float delay_seconds) override
  {
    if (!(delay_seconds > 0.0f)) {
      Send(to, from, subject, body);
      return;
    }
    Delayed entry;
    entry.message.to = to;
    entry.message.from = from != nullptr ? "local" : std::string();
    entry.message.subject = subject;
    entry.message.body = body;
    entry.from = from;
    entry.remaining = delay_seconds;
    delayed.push_back(entry);
  }

  void TickDelayed(float delta_time) override
  {
    /* Engine bus double buffering: messages sent in frame N are visible for
     * frame N+1 only. Advance the delayed clock first, then promote. */
    if (delta_time > 0.0f) {
      for (size_t index = 0; index < delayed.size();) {
        delayed[index].remaining -= delta_time;
        if (delayed[index].remaining > 0.0f) {
          ++index;
          continue;
        }
        const Delayed entry = delayed[index];
        delayed.erase(delayed.begin() + static_cast<std::ptrdiff_t>(index));
        ++delayed_send_calls;
        Send(entry.message.to, entry.from, entry.message.subject, entry.message.body);
      }
    }
    visible.swap(pending);
    pending.clear();
  }

  void Clear()
  {
    dropped_delayed += delayed.size();
    delayed.clear();
    pending.clear();
    visible.clear();
    delayed_send_calls = 0;
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

KX_NodeSignature SinkSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature input;
  input.name = "In";
  input.kind = IRPortKind::Trigger;
  input.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(input);
  return signature;
}

void RegisterHarnessNodes()
{
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  for (int slot = 0; slot < kGateCount; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69Gate" + std::to_string(slot),
                          "B69Gate" + std::to_string(slot),
                          GateSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestGateNode(captured));
                          });
  }
  for (int slot = 0; slot < 7; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69Sink" + std::to_string(slot), 
                          "B69Sink" + std::to_string(slot),
                          SinkSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestSinkNode(captured));
                          });
  }
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

void Connect(GraphIR &ir, int from_node, int from_port, int to_node, int to_port)
{
  IRConnection connection;
  connection.from_node = from_node;
  connection.from_port = from_port;
  connection.to_node = to_node;
  connection.to_port = to_port;
  ir.connections.push_back(connection);
}

int AddGate(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69Gate" + std::to_string(slot), "gate" + std::to_string(slot));
  return index;
}

int AddSink(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69Sink" + std::to_string(slot), "sink" + std::to_string(slot));
  return index;
}

int AddString(GraphIR &ir, const std::string &value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringConst", value);
  ir.nodes.back().config_json = "{\"value\": \"" + value + "\"}";
  return index;
}

int AddFloat(GraphIR &ir, const std::string &value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ConstFloat", value);
  ir.nodes.back().config_json = "{\"value\": \"" + value + "\"}";
  return index;
}

int AddSendRPC(GraphIR &ir, int gate, const std::string &subject, const std::string &payload)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SendRPC", "send_" + subject);
  Connect(ir, gate, 0, index, 0);
  Connect(ir, AddString(ir, subject), 0, index, 1);
  Connect(ir, AddString(ir, payload), 0, index, 2);
  return index;
}

const KX_RuntimeValue *ReadOutput(KX_GraphInstance &instance, int node, int port)
{
  const KX_ExecutionPlan *plan = instance.GetGraph()->Plan();
  for (const KX_PortOffsetInfo &info : plan->port_offsets) {
    if (!info.input && static_cast<int>(info.node_index) == node &&
        static_cast<int>(info.port_index) == port && !info.trigger) {
      return &instance.GetGraph()->Data().Value(info.offset);
    }
  }
  return nullptr;
}

std::string OutputString(KX_GraphInstance &instance, int node, int port)
{
  const KX_RuntimeValue *value = ReadOutput(instance, node, port);
  const char *text = value != nullptr ? value->AsString() : nullptr;
  return text != nullptr ? std::string(text) : std::string();
}

std::string EncodeRpcPacket(const std::string &subject, const std::string &payload)
{
  std::string packet = "EBCRPC1";
  const auto append = [&packet](uint32_t value) {
    packet.push_back(char(value & 0xff));
    packet.push_back(char((value >> 8) & 0xff));
    packet.push_back(char((value >> 16) & 0xff));
    packet.push_back(char((value >> 24) & 0xff));
  };
  append(static_cast<uint32_t>(subject.size()));
  append(static_cast<uint32_t>(payload.size()));
  packet += subject;
  packet += payload;
  return packet;
}

struct GraphIds {
  int exact = -1;
  int prefix = -1;
  int delayed = -1;
  int sinks[7] = {};
};

GraphIR BuildGraph(GraphIds *ids)
{
  GraphIR ir;
  for (int slot = 0; slot < 7; ++slot) {
    ids->sinks[slot] = AddSink(ir, slot);
  }

  const int gate_a = AddGate(ir, 0);
  const int gate_b = AddGate(ir, 1);
  const int gate_two = AddGate(ir, 2);
  const int gate_ack = AddGate(ir, 3);
  const int gate_delay = AddGate(ir, 4);
  const int gate_bad_delay = AddGate(ir, 5);

  const int send_a = AddSendRPC(ir, gate_a, "a.ping", "one");
  const int send_b = AddSendRPC(ir, gate_b, "b.other", "two");
  const int send_p1 = AddSendRPC(ir, gate_two, "a.ping", "p1");
  const int send_p2 = AddSendRPC(ir, gate_two, "a.ping", "p2");
  const int send_ack = AddSendRPC(ir, gate_ack, "a.ack", "ack");
  Connect(ir, send_a, 0, ids->sinks[0], 0);
  Connect(ir, send_b, 0, ids->sinks[0], 0);
  Connect(ir, send_p1, 0, ids->sinks[0], 0);
  Connect(ir, send_p2, 0, ids->sinks[0], 0);
  Connect(ir, send_ack, 0, ids->sinks[0], 0);

  ids->exact = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "OnRPC", "onrpc_exact");
  ir.nodes.back().config_json = "{\"subject\": \"a.ping\", \"match\": 0}";
  Connect(ir, ids->exact, 0, ids->sinks[2], 0);

  ids->prefix = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "OnRPC", "onrpc_prefix");
  ir.nodes.back().config_json = "{\"subject\": \"a.\", \"match\": 1}";
  Connect(ir, ids->prefix, 0, ids->sinks[3], 0);

  ids->delayed = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "OnRPC", "onrpc_delayed");
  ir.nodes.back().config_json = "{\"subject\": \"delayed.ping\", \"match\": 0}";
  Connect(ir, ids->delayed, 0, ids->sinks[4], 0);

  const int schedule = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SendDelayedMessage", "schedule");
  Connect(ir, gate_delay, 0, schedule, 0);
  Connect(ir, AddString(ir, "rpc:delayed.ping"), 0, schedule, 1);
  Connect(ir, AddString(ir, "delayed-body"), 0, schedule, 2);
  Connect(ir, AddFloat(ir, "0.5"), 0, schedule, 3);
  Connect(ir, schedule, 0, ids->sinks[5], 0);

  const int bad_delay = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SendDelayedMessage", "bad_delay");
  Connect(ir, gate_bad_delay, 0, bad_delay, 0);
  Connect(ir, AddString(ir, "rpc:delayed.ping"), 0, bad_delay, 1);
  Connect(ir, AddString(ir, "bad"), 0, bad_delay, 2);
  Connect(ir, AddFloat(ir, "-0.5"), 0, bad_delay, 3);
  Connect(ir, bad_delay, 1, ids->sinks[1], 0);

  return ir;
}

void Frame(KX_GraphInstance &instance, int gate)
{
  g_gates[gate] = true;
  instance.Tick(0.25f);
  g_gates[gate] = false;
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterHarnessNodes();

  TestMessageAdapter message;
  KX_MockNetworkAdapter network;
  KX_MockFileAdapter file;
  GraphIds ids;
  GraphIR ir = BuildGraph(&ids);
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  if (!instance.Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    std::printf("FAIL message graph did not compile\n");
    return 1;
  }
  instance.GetContext()->SetAdapters(nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &message,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &file,
                                     &network);

  /* Ms-1: local loopback with no connected network adapter. */
  Frame(instance, 0);
  Check(g_sink_hits[0] == 1, "SendRPC: loopback succeeds without a network adapter");
  instance.Tick(1.0f / 60.0f);
  Check(g_sink_hits[2] == 1, "OnRPC: exact subject receives the loopback RPC");
  Check(OutputString(instance, ids.exact, 1) == "a.ping" &&
            OutputString(instance, ids.exact, 2) == "one",
        "OnRPC: exposes subject and payload");

  /* Subject mismatch must not trigger. */
  message.Clear();
  const int exact_before = g_sink_hits[2];
  const int prefix_before = g_sink_hits[3];
  Frame(instance, 1);
  instance.Tick(1.0f / 60.0f);
  Check(g_sink_hits[2] == exact_before && g_sink_hits[3] == prefix_before,
        "OnRPC: non-matching subject triggers neither exact nor prefix node");

  /* Same-frame multiple sends are queued, not merged. */
  message.Clear();
  const int exact_same = g_sink_hits[2];
  Frame(instance, 2);
  instance.Tick(1.0f / 60.0f);
  Check(g_sink_hits[2] == exact_same + 2,
        "OnRPC: two same-frame RPCs fire the trigger twice");
  Check(OutputString(instance, ids.exact, 2) == "p2",
        "OnRPC: elements are delivered in send order (last payload is p2)");

  /* match enum: 0 exact, 1 prefix. */
  message.Clear();
  const int exact_prefix = g_sink_hits[2];
  const int prefix_prefix = g_sink_hits[3];
  Frame(instance, 3);
  instance.Tick(1.0f / 60.0f);
  Check(g_sink_hits[2] == exact_prefix && g_sink_hits[3] == prefix_prefix + 1,
        "OnRPC: prefix match accepts a.ack while exact match does not");

  /* Network receive: RPC envelopes are consumed, unrelated events requeued. */
  message.Clear();
  network.connected = true;
  KX_NetworkEvent unrelated;
  unrelated.type = KX_NetworkEvent::PlayerJoin;
  unrelated.player_id = 3;
  network.events.push_back(unrelated);
  KX_NetworkEvent rpc_event;
  rpc_event.type = KX_NetworkEvent::Message;
  rpc_event.player_id = 2;
  rpc_event.data = EncodeRpcPacket("a.ping", "remote");
  network.events.push_back(rpc_event);
  const int exact_net = g_sink_hits[2];
  instance.Tick(1.0f / 60.0f);
  Check(g_sink_hits[2] == exact_net + 1, "OnRPC: network RPC envelope is decoded and delivered");
  Check(OutputString(instance, ids.exact, 2) == "remote" &&
            OutputString(instance, ids.exact, 3) == "player:2",
        "OnRPC: remote sender and payload are reported");
  Check(network.events.size() == 1 && network.events[0].type == KX_NetworkEvent::PlayerJoin,
        "OnRPC: unrelated network events are requeued for OnNetworkEvent");
  network.connected = false;

  /* Ms-2: delayed message fires at 0.5s of accumulated frame time. */
  message.Clear();
  Frame(instance, 4);
  Check(g_sink_hits[5] == 1, "SendDelayedMessage: scheduling succeeds");
  instance.Tick(0.25f);
  Check(g_sink_hits[4] == 0, "SendDelayedMessage: 0.25s is too early");
  instance.Tick(0.25f);
  Check(g_sink_hits[4] == 1 && message.delayed_send_calls == 1,
        "SendDelayedMessage: 0.5s of frame time delivers exactly once");
  Check(OutputString(instance, ids.delayed, 2) == "delayed-body",
        "SendDelayedMessage: payload survives the delay");

  /* Invalid delay is rejected, not clamped. */
  message.Clear();
  Frame(instance, 5);
  Check(g_sink_hits[1] == 1, "SendDelayedMessage: negative delay takes OnFail");

  /* Scene teardown drops pending delayed messages and counts the drop. */
  message.Clear();
  Frame(instance, 4);
  Check(message.delayed.size() == 1,
        "SendDelayedMessage: pending message is adapter-owned");
  message.Clear();
  Check(message.dropped_delayed == 1,
        "SendDelayedMessage: teardown counts the dropped message");

  /* The shipped mock adapter provides the same delayed contract. */
  KX_MockMessageAdapter mock_message;
  mock_message.SendDelayed("", nullptr, "mock.subject", "mock.body", 0.5f);
  mock_message.TickDelayed(0.25f);
  Check(mock_message.delayed.size() == 1, "MockMessageAdapter: 0.25s keeps the message pending");
  mock_message.TickDelayed(0.25f);
  Check(mock_message.delayed.empty() && mock_message.delayed_send_calls == 1 &&
            mock_message.mailbox.size() == 1,
        "MockMessageAdapter: 0.5s delivers exactly one message");
  mock_message.SendDelayed("", nullptr, "mock.subject", "mock.body", 1.0f);
  mock_message.Reset();
  Check(mock_message.dropped_delayed == 1,
        "MockMessageAdapter: Reset reports the dropped pending message");

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
