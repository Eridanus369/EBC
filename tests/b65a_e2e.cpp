// B6.5 legacy compatibility E2E harness: exercises every node added in
// KX_FunctionNodesB6Legacy.cpp through a compiled IR graph and the mock
// adapters.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionContext.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_PropertyAccessor.h"
#include "KX_RuntimeNodes.h"

/* Parallel B6 translation units are not part of this harness. */
void RegisterB3FunctionGameNodes() {}
void RegisterB4FileFunctionGameNodes() {}
void RegisterB4UIFunctionGameNodes() {}
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}

KX_RaycastResult KX_ExecutionContext::Raycast(const float[3], const float[3], float)
{
  return KX_RaycastResult{};
}

namespace {

int failures = 0;

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

/* ------------------------------------------------------------------ mocks */

constexpr uintptr_t kParentAddress = 0x6000;
constexpr uintptr_t kChildAddress = 0x6100;

KX_GameObject *FakeChild()
{
  return reinterpret_cast<KX_GameObject *>(kChildAddress);
}
KX_GameObject *FakeParent()
{
  return reinterpret_cast<KX_GameObject *>(kParentAddress);
}

class TestSceneAdapter : public KX_SceneAdapter {
 public:
  std::map<std::string, KX_GameObject *> objects;
  uint32_t quit_calls = 0;
  KX_GameObject *FindObject(const std::string &name) override
  {
    const auto found = objects.find(name);
    return found != objects.end() ? found->second : nullptr;
  }
  void RequestQuit() override { ++quit_calls; }
};

class TestObjectAdapter : public KX_ObjectAdapter {
 public:
  std::map<KX_GameObject *, KX_GameObject *> parents;
  std::vector<std::string> materials;
  std::vector<int> slots;
  std::vector<KX_GameObject *> material_objects;
  KX_GameObject *GetParent(KX_GameObject *object) override
  {
    const auto found = parents.find(object);
    return found != parents.end() ? found->second : nullptr;
  }
  void ClearParent(KX_GameObject *object) override { parents.erase(object); }
  bool SetMaterial(KX_GameObject *object, const std::string &name, int slot) override
  {
    material_objects.push_back(object);
    materials.push_back(name);
    slots.push_back(slot);
    return true;
  }
  std::map<KX_GameObject *, std::string> names;
  std::string GetName(KX_GameObject *object) override
  {
    const auto found = names.find(object);
    return found != names.end() ? found->second : std::string();
  }
  std::map<KX_GameObject *, std::array<float, 3>> orientations;
  std::vector<float> last_orientation;
  bool GetWorldOrientation(KX_GameObject *object, float out[3]) override
  {
    const auto found = orientations.find(object);
    if (found == orientations.end()) {
      return false;
    }
    for (int i = 0; i < 3; ++i) {
      out[i] = found->second[i];
    }
    return true;
  }
  bool SetWorldOrientation(KX_GameObject *object, const float euler[3]) override
  {
    std::array<float, 3> value = {euler[0], euler[1], euler[2]};
    orientations[object] = value;
    last_orientation.assign(value.begin(), value.end());
    return true;
  }
};

class TestPropertyBackend : public KX_PropertyBackend {
 public:
  std::map<std::pair<void *, std::string>, float> values;
  uint32_t Generation(void *object) const override
  {
    (void)object;
    return 1;
  }
  KX_ValueType Type(void *object, const std::string &name) const override
  {
    const auto found = values.find({object, name});
    return found != values.end() ? KX_VALUE_FLOAT : KX_VALUE_NONE;
  }
  KX_RuntimeValue Read(void *object, const std::string &name, KX_ValuePool *) const override
  {
    return KX_RuntimeValue::Float(values.at({object, name}));
  }
  bool Write(void *object, const std::string &name, const KX_RuntimeValue &value) override
  {
    values[{object, name}] = value.AsFloat();
    return true;
  }
  bool Create(void *object, const std::string &name, KX_ValueType, const KX_RuntimeValue &value) override
  {
    values[{object, name}] = value.AsFloat();
    return true;
  }
};

/* --------------------------------------------------------- harness node types */

int g_sink_hits[10] = {0};
bool g_pulse[2] = {false, false};
KX_GameObject *g_object_const = nullptr;
float g_vector_const[3] = {0.0f, 0.0f, 0.0f};

class TestSinkNode : public KX_LogicNodeRuntime {
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

class TestPulseNode : public KX_LogicNodeRuntime {
 public:
  explicit TestPulseNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (g_pulse[slot_]) {
      SetOutputTrigger(ctx, 0, true);
    }
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

class TestObjectConstNode : public KX_LogicNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Object(g_object_const);
    return KX_ExecResult::Continue;
  }
  bool IsValueNode() const override { return true; }
};

class TestVectorConstNode : public KX_LogicNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Vector(g_vector_const[0], g_vector_const[1],
                                                  g_vector_const[2]);
    return KX_ExecResult::Continue;
  }
  bool IsValueNode() const override { return true; }
};

KX_NodeSignature TriggerOutSignature()
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

KX_NodeSignature ObjectConstSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Value";
  output.kind = IRPortKind::Value;
  output.value_type = KX_VALUE_OBJECT;
  signature.outputs.push_back(output);
  return signature;
}

KX_NodeSignature VectorConstSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Value";
  output.kind = IRPortKind::Value;
  output.value_type = KX_VALUE_VECTOR;
  signature.outputs.push_back(output);
  return signature;
}

/* --------------------------------------------------------------- IR helpers */

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

int AddFloat(GraphIR &ir, const std::string &name, float value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ConstFloat", name);
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": \"%.6f\"}", static_cast<double>(value));
  ir.nodes.back().config_json = config;
  return index;
}

int AddString(GraphIR &ir, const std::string &name, const std::string &value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringConst", name);
  ir.nodes.back().config_json = "{\"value\": \"" + value + "\"}";
  return index;
}

int AddVector(GraphIR &ir, const std::string &name, float x, float y, float z)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestVectorConst", name);
  /* One vector constant per graph keeps the harness small. */
  g_vector_const[0] = x;
  g_vector_const[1] = y;
  g_vector_const[2] = z;
  return index;
}

int AddSink(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestSink" + std::to_string(slot), name);
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

KX_GraphInstance *BuildInstance(GraphIR &ir,
                                TestSceneAdapter &scene,
                                TestObjectAdapter &objects,
                                KX_MockSceneControlAdapter &control,
                                KX_MessageAdapter *message = nullptr)
{
  KX_GraphInstance *instance = new KX_GraphInstance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  if (!instance->Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    delete instance;
    return nullptr;
  }
  instance->GetContext()->SetAdapters(
      nullptr, &scene, nullptr, &objects, nullptr, message, nullptr, &control, nullptr, nullptr,
      nullptr);
  return instance;
}

/* ------------------------------------------------------------------ graphs */

void RunTimer(TestSceneAdapter &scene, TestObjectAdapter &objects, KX_MockSceneControlAdapter &control)
{
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int timer = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TimerLegacy", "timer");
  Connect(ir, 0, 0, timer, 0);
  Connect(ir, AddFloat(ir, "duration", 0.05f), 0, timer, 1);
  Connect(ir, timer, 0, AddSink(ir, "timer_sink", 0), 0);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "TimerLegacy graph compiled");
    return;
  }
  for (int frame = 0; frame < 6; ++frame) {
    instance->Tick(0.02f);
  }
  Check(g_sink_hits[0] == 1, "TimerLegacy WhenElapsed fires exactly once", g_sink_hits[0], 1.0);
  delete instance;
}

void RunQuit(TestSceneAdapter &scene, TestObjectAdapter &objects, KX_MockSceneControlAdapter &control)
{
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int quit = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "QuitGameLegacy", "quit");
  Connect(ir, 0, 0, quit, 0);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "QuitGameLegacy graph compiled");
    return;
  }
  instance->Tick(0.016f);
  Check(scene.quit_calls == 1, "QuitGameLegacy RequestQuit", scene.quit_calls, 1.0);
  delete instance;
}

void RunClearParent(TestSceneAdapter &scene,
                    TestObjectAdapter &objects,
                    KX_MockSceneControlAdapter &control)
{
  objects.parents[FakeChild()] = FakeParent();
  g_object_const = FakeChild();

  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ClearParentLegacy", "clear_parent");
  Connect(ir, 0, 0, node, 0);
  const int child = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestObjectConst", "child");
  Connect(ir, child, 0, node, 1);
  Connect(ir, node, 0, AddSink(ir, "clear_sink", 1), 0);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "ClearParentLegacy graph compiled");
    return;
  }
  instance->Tick(0.016f);
  const KX_RuntimeValue *parent = ReadOutput(*instance, node, 1);
  Check(objects.parents.find(FakeChild()) == objects.parents.end(),
        "ClearParentLegacy clears the parent link");
  Check(parent != nullptr && parent->AsPointer() == FakeParent(),
        "ClearParentLegacy reports the former parent");
  Check(g_sink_hits[1] == 1, "ClearParentLegacy OnDone", g_sink_hits[1], 1.0);
  delete instance;
}

void RunMessageReceived(TestSceneAdapter &scene,
                        TestObjectAdapter &objects,
                        KX_MockSceneControlAdapter &control,
                        KX_MockMessageAdapter &messages)
{
  objects.names[FakeChild()] = "player";
  scene.objects["sender"] = FakeParent();
  g_object_const = FakeChild();

  KX_Message first;
  first.to = "player";
  first.from = "sender";
  first.subject = "ping";
  first.body = "first";
  KX_Message second = first;
  second.body = "second";
  messages.mailbox.push_back(first);
  messages.mailbox.push_back(second);

  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "MessageReceivedLegacy", "message");
  ir.nodes.back().config_json = "{\"use_target\": \"true\"}";
  Connect(ir, AddString(ir, "subject", "ping"), 0, node, 0);
  const int target = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestObjectConst", "target");
  Connect(ir, target, 0, node, 1);
  Connect(ir, node, 0, AddSink(ir, "message_sink", 2), 0);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control, &messages);
  if (instance == nullptr) {
    Check(false, "MessageReceivedLegacy graph compiled");
    return;
  }
  instance->GetContext()->SetOwner(FakeChild());
  instance->Tick(0.016f);

  const KX_RuntimeValue *body = ReadOutput(*instance, node, 1);
  const KX_RuntimeValue *messenger = ReadOutput(*instance, node, 2);
  Check(g_sink_hits[2] == 2, "MessageReceivedLegacy fires once per message", g_sink_hits[2], 2.0);
  Check(body != nullptr && std::string(body->AsString()) == "second",
        "MessageReceivedLegacy Body carries the payload");
  Check(messenger != nullptr && messenger->AsPointer() == FakeParent(),
        "MessageReceivedLegacy Messenger resolves the sender");
  Check(messages.receive_calls == 1, "MessageReceivedLegacy polls the bus once", messages.receive_calls,
        1.0);
  delete instance;
  messages.Reset();
}

void RunValueNodes(TestSceneAdapter &scene,
                   TestObjectAdapter &objects,
                   KX_MockSceneControlAdapter &control)
{
  scene.objects["target"] = FakeParent();
  g_object_const = FakeParent();

  GraphIR ir;
  MakeNode(ir, "OnInit", "init");

  const int by_name = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ObjectByNameLegacy", "by_name");
  Connect(ir, AddString(ir, "name", "target"), 0, by_name, 0);

  const int get_object = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GetObjectLegacy", "get_object");
  const int obj_const = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestObjectConst", "obj");
  Connect(ir, obj_const, 0, get_object, 0);

  const int simple = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SimpleValueLegacy", "simple");
  Connect(ir, AddFloat(ir, "simple_float", 2.5f), 0, simple, 0);
  Connect(ir, AddString(ir, "simple_string", "hi"), 0, simple, 3);

  const int time_data = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TimeDataLegacy", "time");

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "value node graph compiled");
    return;
  }
  instance->Tick(0.02f);
  instance->Tick(0.02f);
  instance->Tick(0.02f);

  const KX_RuntimeValue *found = ReadOutput(*instance, by_name, 0);
  Check(found != nullptr && found->AsPointer() == FakeParent(),
        "ObjectByNameLegacy resolves the name");

  const KX_RuntimeValue *passed = ReadOutput(*instance, get_object, 0);
  Check(passed != nullptr && passed->AsPointer() == FakeParent(),
        "GetObjectLegacy passes the selected object");

  const KX_RuntimeValue *simple_float = ReadOutput(*instance, simple, 0);
  const KX_RuntimeValue *simple_string = ReadOutput(*instance, simple, 3);
  Check(simple_float != nullptr && std::fabs(simple_float->AsFloat() - 2.5f) < 1e-5f,
        "SimpleValueLegacy float output", simple_float != nullptr ? simple_float->AsFloat() : 0.0,
        2.5);
  Check(simple_string != nullptr && std::string(simple_string->AsString()) == "hi",
        "SimpleValueLegacy string output");

  const KX_RuntimeValue *time = ReadOutput(*instance, time_data, 0);
  const KX_RuntimeValue *delta = ReadOutput(*instance, time_data, 1);
  const KX_RuntimeValue *fps = ReadOutput(*instance, time_data, 2);
  Check(time != nullptr && std::fabs(time->AsFloat() - 0.06f) < 1e-4f,
        "TimeDataLegacy Time accumulates", time != nullptr ? time->AsFloat() : 0.0, 0.06);
  Check(delta != nullptr && std::fabs(delta->AsFloat() - 0.02f) < 1e-5f,
        "TimeDataLegacy Delta", delta != nullptr ? delta->AsFloat() : 0.0, 0.02);
  Check(fps != nullptr && std::fabs(fps->AsFloat() - 50.0f) < 0.1f, "TimeDataLegacy FPS",
        fps != nullptr ? fps->AsFloat() : 0.0, 50.0);
  delete instance;
}

void RunSetMaterial(TestSceneAdapter &scene,
                    TestObjectAdapter &objects,
                    KX_MockSceneControlAdapter &control)
{
  g_object_const = FakeParent();
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SetMaterialLegacy", "set_material");
  Connect(ir, 0, 0, node, 0);
  const int obj = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestObjectConst", "obj");
  Connect(ir, obj, 0, node, 1);
  Connect(ir, AddString(ir, "material", "mat_red"), 0, node, 3);
  Connect(ir, node, 0, AddSink(ir, "material_sink", 6), 0);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "SetMaterialLegacy graph compiled");
    return;
  }
  instance->Tick(0.016f);
  Check(objects.materials.size() == 1 && objects.materials[0] == "mat_red",
        "SetMaterialLegacy name reached the adapter");
  Check(!objects.slots.empty() && objects.slots[0] == 0, "SetMaterialLegacy slot default");
  Check(objects.material_objects.size() == 1 && objects.material_objects[0] == FakeParent(),
        "SetMaterialLegacy object");
  Check(g_sink_hits[6] == 1, "SetMaterialLegacy OnDone", g_sink_hits[6], 1.0);
  delete instance;
}

void RunPulsifyBarrier(TestSceneAdapter &scene,
                       TestObjectAdapter &objects,
                       KX_MockSceneControlAdapter &control)
{
  GraphIR ir;
  const int pulse = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestPulse0", "pulse");

  const int pulsify = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "PulsifyLegacy", "pulsify");
  Connect(ir, pulse, 0, pulsify, 0);
  Connect(ir, AddFloat(ir, "gap", 0.05f), 0, pulsify, 1);
  Connect(ir, pulsify, 0, AddSink(ir, "pulsify_sink", 7), 0);

  const int barrier = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "BarrierLegacy", "barrier");
  Connect(ir, pulse, 0, barrier, 0);
  Connect(ir, AddFloat(ir, "barrier_time", 0.03f), 0, barrier, 1);
  Connect(ir, barrier, 0, AddSink(ir, "barrier_sink", 8), 0);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "Pulsify/Barrier graph compiled");
    return;
  }
  for (int frame = 0; frame < 6; ++frame) {
    g_pulse[0] = true;
    instance->Tick(0.01f);
  }
  Check(g_sink_hits[7] == 2, "PulsifyLegacy emits once per gap", g_sink_hits[7], 2.0);
  Check(g_sink_hits[8] == 3, "BarrierLegacy relays after the time has elapsed",
        g_sink_hits[8], 3.0);

  /* Two false frames break the run: the barrier must start counting again. */
  g_pulse[0] = false;
  instance->Tick(0.01f);
  instance->Tick(0.01f);
  for (int i = 0; i < 4; ++i) {
    g_pulse[0] = true;
    instance->Tick(0.01f);
  }
  Check(g_sink_hits[8] == 4, "BarrierLegacy restarts after a gap", g_sink_hits[8], 4.0);
  delete instance;
  g_pulse[0] = false;
}

void RunPropertyNodes(TestSceneAdapter &scene,
                      TestObjectAdapter &objects,
                      KX_MockSceneControlAdapter &control)
{
  TestPropertyBackend backend;
  backend.values[{(void *)FakeChild(), "score"}] = 5.0f;
  g_object_const = FakeChild();

  /* Evaluate: 5 > 3 must fire, and the Value output mirrors the property. */
  {
    GraphIR ir;
    MakeNode(ir, "OnInit", "init");
    const int node = static_cast<int>(ir.nodes.size());
    MakeNode(ir, "EvaluatePropertyLegacy", "evaluate");
    ir.nodes.back().config_json = "{\"mode\": 0, \"op\": 2}";
    const int object = static_cast<int>(ir.nodes.size());
    MakeNode(ir, "TestObjectConst", "object");
    Connect(ir, object, 0, node, 0);
    Connect(ir, AddString(ir, "property", "score"), 0, node, 1);
    Connect(ir, AddFloat(ir, "threshold", 3.0f), 0, node, 2);
    Connect(ir, node, 0, AddSink(ir, "evaluate_sink", 9), 0);

    KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
    if (instance == nullptr) {
      Check(false, "EvaluatePropertyLegacy graph compiled");
    }
    else {
      instance->GetContext()->SetPropertyBackend(&backend);
      instance->Tick(0.016f);
      const KX_RuntimeValue *value = ReadOutput(*instance, node, 1);
      Check(g_sink_hits[9] == 1, "EvaluatePropertyLegacy fires on a true comparison",
            g_sink_hits[9], 1.0);
      Check(value != nullptr && std::fabs(value->AsFloat() - 5.0f) < 1e-5,
            "EvaluatePropertyLegacy exposes the property value");
      delete instance;
    }
  }

  /* Modify: 5 + 4 clamped to [0, 8] leaves 8. */
  {
    GraphIR ir;
    MakeNode(ir, "OnInit", "init");
    const int node = static_cast<int>(ir.nodes.size());
    MakeNode(ir, "ModifyPropertyLegacy", "modify");
    ir.nodes.back().config_json =
        "{\"mode\": 0, \"op\": \"ADD\", \"clamp\": true}";
    Connect(ir, 0, 0, node, 0);
    const int object = static_cast<int>(ir.nodes.size());
    MakeNode(ir, "TestObjectConst", "object");
    Connect(ir, object, 0, node, 1);
    Connect(ir, AddString(ir, "property", "score"), 0, node, 2);
    Connect(ir, AddFloat(ir, "delta", 4.0f), 0, node, 3);
    Connect(ir, AddFloat(ir, "min", 0.0f), 0, node, 4);
    Connect(ir, AddFloat(ir, "max", 8.0f), 0, node, 5);

    KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
    if (instance == nullptr) {
      Check(false, "ModifyPropertyLegacy graph compiled");
    }
    else {
      instance->GetContext()->SetPropertyBackend(&backend);
      instance->Tick(0.016f);
      Check(std::fabs(backend.values[{(void *)FakeChild(), "score"}] - 8.0f) < 1e-5,
            "ModifyPropertyLegacy clamps the result", backend.values[{(void *)FakeChild(), "score"}],
            8.0);
      delete instance;
    }
  }
}

void RunAlignAxis(TestSceneAdapter &scene,
                  TestObjectAdapter &objects,
                  KX_MockSceneControlAdapter &control)
{
  objects.orientations[FakeParent()] = {0.0f, 0.0f, 0.0f};

  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "AlignAxisToVectorLegacy", "align");
  ir.nodes.back().config_json = "{\"local\": true}";
  Connect(ir, 0, 0, node, 0);
  const int object = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestObjectConst", "object");
  g_object_const = FakeParent();
  Connect(ir, object, 0, node, 1);
  Connect(ir, AddVector(ir, "target", 0.0f, 1.0f, 0.0f), 0, node, 2);
  /* Axis left unconnected: integer default 0 means +X. */
  Connect(ir, AddFloat(ir, "factor", 1.0f), 0, node, 4);

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "AlignAxisToVectorLegacy graph compiled");
    return;
  }
  instance->Tick(0.016f);
  bool quarter_turn = false;
  for (const float angle : objects.last_orientation) {
    quarter_turn = quarter_turn || std::fabs(std::fabs(angle) - 1.5707963f) < 1e-3f;
  }
  Check(quarter_turn, "AlignAxisToVectorLegacy rotates +X onto +Y");
  delete instance;
}

void RunRebuild(TestSceneAdapter &scene,
                TestObjectAdapter &objects,
                KX_MockSceneControlAdapter &control)
{
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const int time = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TimeDataLegacy", "time");

  KX_GraphInstance *instance = BuildInstance(ir, scene, objects, control);
  if (instance == nullptr) {
    Check(false, "rebuild graph compiled");
    return;
  }
  instance->Tick(0.1f);
  std::vector<std::string> warnings;
  const bool rebuilt = instance->Rebuild(ir, &warnings);
  instance->Tick(0.25f);
  const KX_RuntimeValue *value = ReadOutput(*instance, time, 0);
  Check(rebuilt, "old graph survives a Rebuild");
  Check(value != nullptr && value->AsFloat() >= 0.2f,
        "TimeDataLegacy keeps producing values after Rebuild",
        value != nullptr ? value->AsFloat() : -1.0f, 0.2);
  delete instance;
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  const std::string sink = "TestSink";
  for (int slot = 0; slot < 10; ++slot) {
    const std::string type_id = sink + std::to_string(slot);
    const int captured = slot;
    KX_NodeRegistry::Get().RegisterType(
        type_id,
        type_id,
        SinkSignature(),
        [captured]() { return std::unique_ptr<KX_LogicNodeRuntime>(new TestSinkNode(captured)); });
  }
  for (int slot = 0; slot < 2; ++slot) {
    const std::string type_id = "TestPulse" + std::to_string(slot);
    const int captured = slot;
    KX_NodeRegistry::Get().RegisterType(
        type_id,
        type_id,
        TriggerOutSignature(),
        [captured]() { return std::unique_ptr<KX_LogicNodeRuntime>(new TestPulseNode(captured)); });
  }
  KX_NodeRegistry::Get().RegisterType(
      "TestObjectConst",
      "TestObjectConst",
      ObjectConstSignature(),
      []() { return std::unique_ptr<KX_LogicNodeRuntime>(new TestObjectConstNode()); });
  KX_NodeRegistry::Get().RegisterType(
      "TestVectorConst",
      "TestVectorConst",
      VectorConstSignature(),
      []() { return std::unique_ptr<KX_LogicNodeRuntime>(new TestVectorConstNode()); });

  std::printf("INFO B6.5 legacy E2E start\n");

  TestSceneAdapter scene;
  TestObjectAdapter objects;
  KX_MockSceneControlAdapter control;
  KX_MockMessageAdapter messages;

  RunTimer(scene, objects, control);
  RunQuit(scene, objects, control);
  RunClearParent(scene, objects, control);
  RunMessageReceived(scene, objects, control, messages);
  RunValueNodes(scene, objects, control);
  RunSetMaterial(scene, objects, control);
  RunPulsifyBarrier(scene, objects, control);
  RunPropertyNodes(scene, objects, control);
  RunAlignAxis(scene, objects, control);
  RunRebuild(scene, objects, control);
  std::printf("%s total failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
