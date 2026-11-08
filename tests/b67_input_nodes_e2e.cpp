// B6.9-I behaviour E2E for the eight input nodes that previously had no
// node-level assertion (they were only proven to register):
//
//   SetMousePosition, SetCursorVisible, MouseData, KeyPressed, MousePressed,
//   MouseMoved, SetMousePositionLegacy, MouseLook
//
// Each assertion checks what the node *does*: the value it forwards to the
// input adapter, the columns it publishes, or the trigger it raises. Mocks
// replace the engine backend, exactly like the other headless harnesses.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

void RegisterFunctionGameNodes();
void RegisterExtendedFunctionGameNodes();
void RegisterB5FunctionGameNodes();

/* Families owned by parallel lines are not part of this harness. */
void RegisterB2FunctionGameNodes() {}
void RegisterB3FunctionGameNodes() {}
void RegisterB4FileFunctionGameNodes() {}
void RegisterB4UIFunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  ++g_checks;
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++g_failures;
  }
}

void CheckNear(float actual, float expected, float epsilon, const char *what)
{
  Check(std::fabs(actual - expected) <= epsilon, what, actual, expected);
}

/* ------------------------------------------------------------------ input */

class RecordingInput : public KX_MockInputAdapter {
 public:
  bool key_down = false;
  bool mouse_down = false;
  float px = 0.0f;
  float py = 0.0f;
  float dx = 0.0f;
  float dy = 0.0f;
  int wheel = 0;

  int last_key_code = -1;
  int last_button = -1;
  int set_pos_calls = 0;
  float set_nx = 0.0f;
  float set_ny = 0.0f;
  int set_cursor_calls = 0;
  bool cursor_visible = false;

  /** Derive this frame's edges from the previous frame's level state. */
  void FrameBegin()
  {
    key_press = key_down && !previous_key_down;
    key_release = !key_down && previous_key_down;
    mouse_press = mouse_down && !previous_mouse_down;
    mouse_release = !mouse_down && previous_mouse_down;
  }
  void FrameEnd()
  {
    previous_key_down = key_down;
    previous_mouse_down = mouse_down;
  }

  bool KeyActive(int code, KX_InputMode mode) override
  {
    last_key_code = code;
    switch (mode) {
      case KX_InputMode::Press:
        return key_press;
      case KX_InputMode::Release:
        return key_release;
      case KX_InputMode::Hold:
        return key_down;
    }
    return false;
  }

  bool MouseButtonActive(int button, KX_InputMode mode) override
  {
    last_button = button;
    switch (mode) {
      case KX_InputMode::Press:
        return mouse_press;
      case KX_InputMode::Release:
        return mouse_release;
      case KX_InputMode::Hold:
        return mouse_down;
    }
    return false;
  }

  void MousePosition(float &x, float &y) const override
  {
    x = px;
    y = py;
  }
  void MouseDelta(float &x, float &y) const override
  {
    x = dx;
    y = dy;
  }
  int MouseWheel() const override
  {
    return wheel;
  }
  void SetMousePosition(float nx, float ny) override
  {
    ++set_pos_calls;
    set_nx = nx;
    set_ny = ny;
  }
  void SetCursorVisible(bool visible) override
  {
    ++set_cursor_calls;
    cursor_visible = visible;
  }

 private:
  bool key_press = false;
  bool key_release = false;
  bool mouse_press = false;
  bool mouse_release = false;
  bool previous_key_down = false;
  bool previous_mouse_down = false;
};

static KX_GameObject *const kOwner = reinterpret_cast<KX_GameObject *>(0x1000);

class RecordingObjectAdapter : public KX_ObjectAdapter {
 public:
  float orientation[3] = {0.0f, 0.0f, 0.0f};
  float last_write[3] = {0.0f, 0.0f, 0.0f};
  int writes = 0;

  bool GetWorldOrientation(KX_GameObject *object, float out[3]) override
  {
    if (object != kOwner) {
      return false;
    }
    out[0] = orientation[0];
    out[1] = orientation[1];
    out[2] = orientation[2];
    return true;
  }

  bool SetWorldOrientation(KX_GameObject *object, const float value[3]) override
  {
    if (object != kOwner) {
      return false;
    }
    last_write[0] = value[0];
    last_write[1] = value[1];
    last_write[2] = value[2];
    ++writes;
    return true;
  }
};

/* ------------------------------------------------------- graph helpers */

int g_sink_hits[8] = {0, 0, 0, 0, 0, 0, 0, 0};

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

/* The shipped set has no vector constant, and adapters are not reachable from
 * the IR, so these two test-only nodes supply the constant values. */
static KX_FuncPort g_vector_out = {"Value", KX_VALUE_VECTOR, false};
static KX_FunctionSpec g_vector_spec;

void FuncVectorConst(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutVector(ctx,
                 0,
                 node.ConfigFloat("x", 0.0f),
                 node.ConfigFloat("y", 0.0f),
                 node.ConfigFloat("z", 0.0f));
}

void RegisterTestVectorConst()
{
  KX_NodeSignature signature;
  KX_NodePortSignature port;
  port.name = "Value";
  port.kind = IRPortKind::Value;
  port.value_type = KX_VALUE_VECTOR;
  signature.outputs.push_back(port);

  g_vector_spec.type_id = "TestVectorConst";
  g_vector_spec.outputs = &g_vector_out;
  g_vector_spec.output_count = 1;
  g_vector_spec.execute = FuncVectorConst;
  g_vector_spec.is_value_node = true;

  KX_NodeRegistry::Get().RegisterType(
      "TestVectorConst",
      "TestVectorConst",
      signature,
      []() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(&g_vector_spec)); });
}

int AddNode(GraphIR &ir, const std::string &type_id, const std::string &name)
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
  return static_cast<int>(ir.nodes.size()) - 1;
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

void SetConfig(GraphIR &ir, int index, const std::string &json)
{
  ir.nodes[index].config_json = json;
}

int AddFloat(GraphIR &ir, const std::string &name, float value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": \"%.6f\"}", static_cast<double>(value));
  const int index = AddNode(ir, "ConstFloat", name);
  SetConfig(ir, index, config);
  return index;
}

int AddInt(GraphIR &ir, const std::string &name, int value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": %d}", value);
  const int index = AddNode(ir, "ConstInt", name);
  SetConfig(ir, index, config);
  return index;
}

int AddBool(GraphIR &ir, const std::string &name, bool value)
{
  const int index = AddNode(ir, "ConstBool", name);
  SetConfig(ir, index, value ? "{\"value\": \"true\"}" : "{\"value\": \"false\"}");
  return index;
}

int AddVector(GraphIR &ir, const std::string &name, float x, float y, float z)
{
  char config[128];
  std::snprintf(config, sizeof(config), "{\"x\": %.9g, \"y\": %.9g, \"z\": %.9g}", x, y, z);
  const int index = AddNode(ir, "TestVectorConst", name);
  SetConfig(ir, index, config);
  return index;
}

int AddSink(GraphIR &ir, const std::string &name, int slot)
{
  return AddNode(ir, "TestSink" + std::to_string(slot), name);
}

KX_GraphInstance *BuildInstance(const GraphIR &ir,
                                KX_GameObject *owner,
                                RecordingInput &input,
                                RecordingObjectAdapter *objects)
{
  KX_GraphInstance *instance = new KX_GraphInstance(nullptr, owner, nullptr);
  std::vector<std::string> warnings;
  if (!instance->Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    delete instance;
    return nullptr;
  }
  instance->GetContext()->SetAdapters(&input, nullptr, nullptr, objects);
  return instance;
}

void Step(KX_GraphInstance *instance, RecordingInput &input, float delta_time = 0.016f)
{
  input.FrameBegin();
  instance->Tick(delta_time);
  input.FrameEnd();
}

uint32_t OffsetFor(KX_GraphInstance &instance,
                   const std::string &debug_name,
                   bool input,
                   const std::string &port_name)
{
  const KX_ExecutionPlan *plan = instance.GetGraph()->Plan();
  for (size_t node_index = 0; node_index < plan->debug_names.size(); ++node_index) {
    if (plan->debug_names[node_index] != debug_name) {
      continue;
    }
    for (const KX_PortOffsetInfo &info : plan->port_offsets) {
      if (info.node_index == node_index && info.input == input && info.name == port_name) {
        return info.offset;
      }
    }
  }
  std::fprintf(stderr, "offset lookup failed: %s.%s\n", debug_name.c_str(), port_name.c_str());
  return 0;
}

void ResetSinks()
{
  for (int &hit : g_sink_hits) {
    hit = 0;
  }
}

/* ----------------------------------------------------------------- tests */

void TestSetMousePosition()
{
  ResetSinks();
  RecordingInput input;
  GraphIR ir;
  const int init = AddNode(ir, "OnInit", "init");
  const int set = AddNode(ir, "SetMousePosition", "set_mouse");
  const int position = AddVector(ir, "position", 0.5f, 0.25f, 0.0f);
  const int done = AddSink(ir, "done", 0);
  Connect(ir, init, 0, set, 0);
  Connect(ir, position, 0, set, 1);
  Connect(ir, set, 0, done, 0);

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "SetMousePosition graph compiles");
  if (instance == nullptr) {
    return;
  }

  Step(instance, input);
  Step(instance, input);
  Check(input.set_pos_calls == 1, "SetMousePosition called the adapter once", input.set_pos_calls, 1);
  CheckNear(input.set_nx, 0.5f, 1e-5f, "SetMousePosition forwarded normalized x");
  CheckNear(input.set_ny, 0.25f, 1e-5f, "SetMousePosition forwarded normalized y");
  Check(g_sink_hits[0] == 1, "SetMousePosition raised OnDone once", g_sink_hits[0], 1);

  delete instance;
}

void TestSetCursorVisible()
{
  ResetSinks();
  RecordingInput input;
  GraphIR ir;
  const int init = AddNode(ir, "OnInit", "init");
  const int set = AddNode(ir, "SetCursorVisible", "set_cursor");
  const int visible = AddBool(ir, "visible", true);
  const int done = AddSink(ir, "done", 0);
  Connect(ir, init, 0, set, 0);
  Connect(ir, visible, 0, set, 1);
  Connect(ir, set, 0, done, 0);

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "SetCursorVisible graph compiles");
  if (instance == nullptr) {
    return;
  }

  Step(instance, input);
  Check(input.set_cursor_calls == 1,
        "SetCursorVisible called the adapter once",
        input.set_cursor_calls,
        1);
  Check(input.cursor_visible, "SetCursorVisible forwarded Visible=true");
  Check(g_sink_hits[0] == 1, "SetCursorVisible raised OnDone", g_sink_hits[0], 1);

  delete instance;
}

void TestMouseData()
{
  RecordingInput input;
  input.px = 120.0f;
  input.py = 240.0f;
  input.dx = 5.0f;
  input.dy = -3.0f;
  input.wheel = 2;

  GraphIR ir;
  AddNode(ir, "OnInit", "init");
  AddNode(ir, "MouseData", "mouse_data");

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "MouseData graph compiles");
  if (instance == nullptr) {
    return;
  }
  KX_ExecutionContext *ctx = instance->GetContext();
  Step(instance, input);

  const uint32_t position = OffsetFor(*instance, "mouse_data", false, "Position");
  const uint32_t movement = OffsetFor(*instance, "mouse_data", false, "Movement");
  const uint32_t x = OffsetFor(*instance, "mouse_data", false, "X");
  const uint32_t y = OffsetFor(*instance, "mouse_data", false, "Y");
  const uint32_t delta_x = OffsetFor(*instance, "mouse_data", false, "DeltaX");
  const uint32_t delta_y = OffsetFor(*instance, "mouse_data", false, "DeltaY");
  const uint32_t wheel = OffsetFor(*instance, "mouse_data", false, "Wheel");

  float vector[3];
  ctx->Value(position).VectorTo(vector);
  CheckNear(vector[0], 120.0f, 1e-5f, "MouseData Position.x");
  CheckNear(vector[1], 240.0f, 1e-5f, "MouseData Position.y");
  ctx->Value(movement).VectorTo(vector);
  CheckNear(vector[0], 5.0f, 1e-5f, "MouseData Movement.x");
  CheckNear(vector[1], -3.0f, 1e-5f, "MouseData Movement.y");
  CheckNear(ctx->Value(x).AsFloat(), 120.0f, 1e-5f, "MouseData X");
  CheckNear(ctx->Value(y).AsFloat(), 240.0f, 1e-5f, "MouseData Y");
  CheckNear(ctx->Value(delta_x).AsFloat(), 5.0f, 1e-5f, "MouseData DeltaX");
  CheckNear(ctx->Value(delta_y).AsFloat(), -3.0f, 1e-5f, "MouseData DeltaY");
  Check(ctx->Value(wheel).AsInt() == 2, "MouseData Wheel", ctx->Value(wheel).AsInt(), 2);

  delete instance;
}

void TestKeyPressed()
{
  ResetSinks();
  RecordingInput input;
  GraphIR ir;
  AddNode(ir, "OnInit", "init");
  const int key_pressed = AddNode(ir, "KeyPressed", "key_pressed");
  SetConfig(ir, key_pressed, "{\"mode\": 0}");
  const int key_code = AddInt(ir, "key_code", 65);
  const int sink = AddSink(ir, "key_sink", 0);
  Connect(ir, key_code, 0, key_pressed, 0);
  Connect(ir, key_pressed, 0, sink, 0);

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "KeyPressed graph compiles");
  if (instance == nullptr) {
    return;
  }

  input.key_down = false;
  Step(instance, input);
  Check(g_sink_hits[0] == 0, "KeyPressed is silent while the key is up", g_sink_hits[0], 0);
  Check(input.last_key_code == 65, "KeyPressed forwarded KeyCode 65 to the adapter", input.last_key_code, 65);

  input.key_down = true;
  Step(instance, input);
  Check(g_sink_hits[0] == 1, "KeyPressed fired on the press edge", g_sink_hits[0], 1);

  Step(instance, input);
  Check(g_sink_hits[0] == 1, "KeyPressed Press is an edge, not a level", g_sink_hits[0], 1);

  input.key_down = false;
  Step(instance, input);
  Check(g_sink_hits[0] == 1, "KeyPressed mode=Press ignores the release edge", g_sink_hits[0], 1);

  delete instance;
}

void TestMousePressed()
{
  ResetSinks();
  RecordingInput input;
  GraphIR ir;
  AddNode(ir, "OnInit", "init");
  const int pressed = AddNode(ir, "MousePressed", "mouse_pressed");
  SetConfig(ir, pressed, "{\"mode\": 2}");
  const int button = AddInt(ir, "button", 0);
  const int sink = AddSink(ir, "mouse_sink", 0);
  Connect(ir, button, 0, pressed, 0);
  Connect(ir, pressed, 0, sink, 0);

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "MousePressed graph compiles");
  if (instance == nullptr) {
    return;
  }

  input.mouse_down = false;
  Step(instance, input);
  Check(g_sink_hits[0] == 0, "MousePressed Hold stays silent while the button is up", g_sink_hits[0], 0);
  Check(input.last_button == 0, "MousePressed forwarded Button 0 to the adapter", input.last_button, 0);

  input.mouse_down = true;
  Step(instance, input);
  Check(g_sink_hits[0] == 1, "MousePressed Hold fired while the button is down", g_sink_hits[0], 1);

  Step(instance, input);
  Check(g_sink_hits[0] == 2, "MousePressed Hold fires every frame while held", g_sink_hits[0], 2);

  input.mouse_down = false;
  Step(instance, input);
  Check(g_sink_hits[0] == 2, "MousePressed Hold stops after release", g_sink_hits[0], 2);

  delete instance;
}

void TestMouseMoved()
{
  ResetSinks();
  RecordingInput input;
  GraphIR ir;
  AddNode(ir, "OnInit", "init");
  const int moved = AddNode(ir, "MouseMoved", "mouse_moved");
  const int sink = AddSink(ir, "moved_sink", 0);
  Connect(ir, moved, 0, sink, 0);

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "MouseMoved graph compiles");
  if (instance == nullptr) {
    return;
  }

  Step(instance, input);
  Check(g_sink_hits[0] == 0, "MouseMoved is silent with a zero delta", g_sink_hits[0], 0);

  input.dx = 4.0f;
  input.dy = 0.0f;
  Step(instance, input);
  Check(g_sink_hits[0] == 1, "MouseMoved fired on a non-zero delta", g_sink_hits[0], 1);

  input.dx = 0.0f;
  input.dy = -2.0f;
  Step(instance, input);
  Check(g_sink_hits[0] == 2, "MouseMoved fired on a vertical delta", g_sink_hits[0], 2);

  input.dy = 0.0f;
  Step(instance, input);
  Check(g_sink_hits[0] == 2, "MouseMoved stops when the delta returns to zero", g_sink_hits[0], 2);

  delete instance;
}

void TestSetMousePositionLegacy()
{
  ResetSinks();
  RecordingInput input;
  GraphIR ir;
  const int init = AddNode(ir, "OnInit", "init");
  const int set = AddNode(ir, "SetMousePositionLegacy", "set_legacy");
  const int x = AddFloat(ir, "x", 0.3f);
  const int y = AddFloat(ir, "y", 0.7f);
  const int done = AddSink(ir, "done", 0);
  Connect(ir, init, 0, set, 0);
  Connect(ir, x, 0, set, 1);
  Connect(ir, y, 0, set, 2);
  Connect(ir, set, 0, done, 0);

  KX_GraphInstance *instance = BuildInstance(ir, nullptr, input, nullptr);
  Check(instance != nullptr, "SetMousePositionLegacy graph compiles");
  if (instance == nullptr) {
    return;
  }

  Step(instance, input);
  Step(instance, input);
  Check(input.set_pos_calls == 1, "SetMousePositionLegacy called the adapter once", input.set_pos_calls, 1);
  CheckNear(input.set_nx, 0.3f, 1e-5f, "SetMousePositionLegacy forwarded X unchanged");
  CheckNear(input.set_ny, 0.7f, 1e-5f, "SetMousePositionLegacy forwarded Y unchanged");
  Check(g_sink_hits[0] == 1, "SetMousePositionLegacy raised OnDone", g_sink_hits[0], 1);

  delete instance;
}

void TestMouseLook()
{
  ResetSinks();
  RecordingInput input;
  RecordingObjectAdapter objects;
  GraphIR ir;
  const int init = AddNode(ir, "OnInit", "init");
  const int look = AddNode(ir, "MouseLook", "mouse_look");
  const int sensitivity = AddFloat(ir, "sensitivity", 1.0f);
  const int min_pitch = AddFloat(ir, "min_pitch", -89.0f);
  const int max_pitch = AddFloat(ir, "max_pitch", 89.0f);
  const int update = AddSink(ir, "update", 0);
  Connect(ir, init, 0, look, 0);
  Connect(ir, sensitivity, 0, look, 1);
  Connect(ir, min_pitch, 0, look, 2);
  Connect(ir, max_pitch, 0, look, 3);
  Connect(ir, look, 0, update, 0);

  KX_GraphInstance *instance = BuildInstance(ir, kOwner, input, &objects);
  Check(instance != nullptr, "MouseLook graph compiles");
  if (instance == nullptr) {
    return;
  }

  const float deg_to_rad = 3.14159265358979323846f / 180.0f;

  /* Frame 1 activates the node from OnEnter; frame 2 is the first tick. */
  input.dx = 10.0f;
  input.dy = 5.0f;
  Step(instance, input);
  Step(instance, input);
  Check(objects.writes == 1, "MouseLook wrote the orientation once", objects.writes, 1);
  CheckNear(objects.last_write[0], 5.0f * deg_to_rad, 1e-4f, "MouseLook pitch from mouse dy");
  CheckNear(objects.last_write[1], 0.0f, 1e-5f, "MouseLook keeps roll untouched");
  CheckNear(objects.last_write[2], 10.0f * deg_to_rad, 1e-4f, "MouseLook yaw from mouse dx");
  Check(g_sink_hits[0] == 1, "MouseLook raised OnUpdate", g_sink_hits[0], 1);

  /* A large downward delta must be clamped to MaxPitch (89 degrees). */
  input.dx = 0.0f;
  input.dy = 200.0f;
  Step(instance, input);
  Check(objects.writes == 2, "MouseLook wrote again on the next tick", objects.writes, 2);
  CheckNear(objects.last_write[0], 89.0f * deg_to_rad, 1e-4f, "MouseLook clamps pitch to MaxPitch");

  delete instance;
}

void RegisterCoreGameNodesForHarness()
{
  RegisterGameNodeType<KX_Node_OnInit>("OnInit", "KX_Node_OnInit", KX_Node_OnInit::Signature());
  RegisterGameNodeType<KX_Node_OnUpdate>(
      "OnUpdate", "KX_Node_OnUpdate", KX_Node_OnUpdate::Signature());
  RegisterGameNodeType<KX_Node_ConstFloat>(
      "ConstFloat", "KX_Node_ConstFloat", KX_Node_ConstFloat::Signature());
  RegisterGameNodeType<KX_Node_ConstBool>("ConstBool", "KX_Node_ConstBool", KX_Node_ConstBool::Signature());
}

}  // namespace

int main()
{
  RegisterCoreGameNodesForHarness();
  RegisterFunctionGameNodes();
  RegisterExtendedFunctionGameNodes();
  RegisterB5FunctionGameNodes();
  RegisterTestVectorConst();
  for (int slot = 0; slot < 8; ++slot) {
    const std::string type_id = "TestSink" + std::to_string(slot);
    const int captured = slot;
    KX_NodeRegistry::Get().RegisterType(
        type_id,
        type_id,
        SinkSignature(),
        [captured]() { return std::unique_ptr<KX_GameNodeRuntime>(new TestSinkNode(captured)); });
  }

  std::printf("INFO harness start\n");

  TestSetMousePosition();
  TestSetCursorVisible();
  TestMouseData();
  TestKeyPressed();
  TestMousePressed();
  TestMouseMoved();
  TestSetMousePositionLegacy();
  TestMouseLook();

  std::printf("RESULT assertions=%d failures=%d\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
