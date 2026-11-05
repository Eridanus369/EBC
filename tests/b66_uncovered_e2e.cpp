// E2E for the three nodes that had no output assertions:
//   ProximitySensor, JoystickAxis, JoystickButton.
//
// Every assertion reads a node output (or counts a downstream trigger firing
// caused by that output). ProximitySensor is a trigger-root node: the compiler
// puts nodes with trigger outputs but no trigger input into trigger_starts, so
// it is evaluated every frame and its OnNear/OnFar outputs are per-frame
// levels. The WriteFile nodes below count those firings, which is what makes
// "every frame" versus "edge" observable.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_FileAdapter.h"
#include "KX_GameAdapters.h"
#include "KX_GraphIR.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

/* Only B6Sensor is linked in this harness; the other B6 registries are stubs. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}

namespace {

int failures = 0;
int assertions = 0;

void Check(bool ok, const char *what)
{
  ++assertions;
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

IRNode MakeNode(const std::string &type_id, const std::string &debug_name)
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = debug_name;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  const KX_NodeTypeId id = registry.FindByEditorType(type_id);
  const KX_NodeSignature *signature = registry.GetSignature(id);
  if (signature == nullptr) {
    std::fprintf(stderr, "unknown node type %s\n", type_id.c_str());
    return node;
  }
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
  return node;
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

const KX_RuntimeValue *ReadOutput(KX_GraphInstance &instance, int node, int port)
{
  const KX_ExecutionPlan *plan =
      instance.GetGraph() != nullptr ? instance.GetGraph()->Plan() : nullptr;
  if (plan == nullptr) {
    return nullptr;
  }
  for (const KX_PortOffsetInfo &info : plan->port_offsets) {
    if (!info.input && static_cast<int>(info.node_index) == node &&
        static_cast<int>(info.port_index) == port && !info.trigger)
    {
      return &instance.GetGraph()->Data().Value(info.offset);
    }
  }
  return nullptr;
}

/** File adapter that counts writes per path, so each trigger branch has its
 * own observable counter. */
class CountingFileAdapter : public KX_MockFileAdapter {
 public:
  std::map<std::string, int> path_writes;

  bool WriteFile(const std::string &path, bool binary, const void *data, size_t len) override
  {
    ++path_writes[path];
    return KX_MockFileAdapter::WriteFile(path, binary, data, len);
  }
};

class FakeScene : public KX_SceneAdapter {
 public:
  KX_GameObject *target = nullptr;
  bool return_null = false;

  KX_GameObject *FindObject(const std::string &name) override
  {
    (void)name;
    return return_null ? nullptr : target;
  }
};

class FakeObjects : public KX_ObjectAdapter {
 public:
  std::map<KX_GameObject *, std::vector<float>> positions;

  void SetPosition(KX_GameObject *object, float x, float y, float z)
  {
    positions[object] = {x, y, z};
  }

  bool GetWorldPosition(KX_GameObject *object, float out[3]) override
  {
    auto it = positions.find(object);
    if (it == positions.end()) {
      out[0] = out[1] = out[2] = 0.0f;
      return false;
    }
    out[0] = it->second[0];
    out[1] = it->second[1];
    out[2] = it->second[2];
    return true;
  }
};

/** Subclass of the existing mock: the base class never overrides the legacy
 * joystick virtuals, so this is the only injection point available to tests. */
class KX_MockJoystickInputAdapter : public KX_MockInputAdapter {
 public:
  bool axis_injected = false;
  float axis_value = 0.0f;
  bool button_injected = false;
  bool button_value = false;

  float JoystickAxis(int axis) const override
  {
    (void)axis;
    return axis_injected ? axis_value : 0.0f;
  }

  bool JoystickButton(int index, KX_InputMode mode) override
  {
    (void)index;
    (void)mode;
    return button_injected ? button_value : false;
  }
};

/* Node indices in the test graph. */
enum NodeIndex {
  kTargetObject = 0,   /* ObjectByName -> Target */
  kDistance = 1,       /* ConstFloat 6.0 */
  kProximity = 2,      /* ProximitySensor */
  kNearPath = 3,       /* StringConst */
  kNearContent = 4,    /* StringConst */
  kWriteNear = 5,      /* WriteFile, driven by OnNear */
  kFarPath = 6,        /* StringConst */
  kFarContent = 7,     /* StringConst */
  kWriteFar = 8,       /* WriteFile, driven by OnFar */
  kJoyAxis = 9,        /* JoystickAxis axis 0 */
  kJoyButton = 10,     /* JoystickButton button 0 */
};

const char kNearPathValue[] = "near.txt";
const char kFarPathValue[] = "far.txt";

GraphIR BuildGraph()
{
  GraphIR ir;

  IRNode target = MakeNode("ObjectByName", "target");
  target.config_json = "{\"name\": \"Target\"}";
  ir.nodes.push_back(std::move(target));

  IRNode threshold = MakeNode("ConstFloat", "threshold");
  threshold.config_json = "{\"value\": \"6.0\"}";
  ir.nodes.push_back(std::move(threshold));

  ir.nodes.push_back(MakeNode("ProximitySensor", "proximity"));

  IRNode near_path = MakeNode("StringConst", "nearpath");
  near_path.config_json = "{\"value\": \"near.txt\"}";
  ir.nodes.push_back(std::move(near_path));

  IRNode near_content = MakeNode("StringConst", "nearcontent");
  near_content.config_json = "{\"value\": \"near\"}";
  ir.nodes.push_back(std::move(near_content));

  ir.nodes.push_back(MakeNode("WriteFile", "writenear"));

  IRNode far_path = MakeNode("StringConst", "farpath");
  far_path.config_json = "{\"value\": \"far.txt\"}";
  ir.nodes.push_back(std::move(far_path));

  IRNode far_content = MakeNode("StringConst", "farcontent");
  far_content.config_json = "{\"value\": \"far\"}";
  ir.nodes.push_back(std::move(far_content));

  ir.nodes.push_back(MakeNode("WriteFile", "writefar"));

  IRNode axis = MakeNode("JoystickAxis", "joyaxis");
  axis.config_json = "{\"axis\": \"0\"}";
  ir.nodes.push_back(std::move(axis));

  IRNode button = MakeNode("JoystickButton", "joybutton");
  button.config_json = "{\"button\": \"0\", \"mode\": \"2\"}";
  ir.nodes.push_back(std::move(button));

  /* Object + threshold into the sensor. */
  Connect(ir, kTargetObject, 0, kProximity, 0);
  Connect(ir, kDistance, 0, kProximity, 1);
  /* OnNear (port 0) and OnFar (port 1) each drive their own WriteFile. */
  Connect(ir, kProximity, 0, kWriteNear, 0);
  Connect(ir, kNearPath, 0, kWriteNear, 1);
  Connect(ir, kNearContent, 0, kWriteNear, 2);
  Connect(ir, kProximity, 1, kWriteFar, 0);
  Connect(ir, kFarPath, 0, kWriteFar, 1);
  Connect(ir, kFarContent, 0, kWriteFar, 2);

  return ir;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  Check(registry.FindByEditorType("ProximitySensor") != KX_INVALID_NODE_TYPE_ID,
        "ProximitySensor registered");
  Check(registry.FindByEditorType("JoystickAxis") != KX_INVALID_NODE_TYPE_ID,
        "JoystickAxis registered");
  Check(registry.FindByEditorType("JoystickButton") != KX_INVALID_NODE_TYPE_ID,
        "JoystickButton registered");

  /* Fake objects are opaque handles; only the adapter map decides positions. */
  KX_GameObject *owner = reinterpret_cast<KX_GameObject *>(uintptr_t(0x1000));
  KX_GameObject *target = reinterpret_cast<KX_GameObject *>(uintptr_t(0x2000));

  FakeScene scene;
  scene.target = target;
  FakeObjects objects;
  objects.SetPosition(owner, 0.0f, 0.0f, 0.0f);
  objects.SetPosition(target, 3.0f, 4.0f, 0.0f); /* distance 5.0 */

  CountingFileAdapter file;
  KX_MockJoystickInputAdapter input;

  KX_GraphInstance instance(nullptr, owner, nullptr);
  instance.GetContext()->SetAdapters(
      &input, &scene, nullptr, &objects, nullptr, nullptr, nullptr, nullptr, nullptr, &file);

  const GraphIR graph = BuildGraph();
  std::vector<std::string> diagnostics;
  Check(instance.Load(graph, &diagnostics), "ProximitySensor + joystick graph compiles");
  for (const std::string &diagnostic : diagnostics) {
    std::printf("INFO compile: %s\n", diagnostic.c_str());
  }

  const float dt = 0.016f;

  /* Phase 1: four ticks, distance 5.0 < threshold 6.0, so OnNear must fire on
   * every tick. Joystick state changes between ticks. */
  instance.Tick(dt);
  const KX_RuntimeValue *axis_out = ReadOutput(instance, kJoyAxis, 0);
  Check(axis_out != nullptr && axis_out->AsFloat() == 0.0f,
        "JoystickAxis baseline is 0 without injection");
  const KX_RuntimeValue *button_out = ReadOutput(instance, kJoyButton, 0);
  Check(button_out != nullptr && !button_out->AsBool(),
        "JoystickButton baseline is false without injection");

  input.axis_injected = true;
  input.axis_value = 0.6f;
  input.button_injected = true;
  input.button_value = true;
  instance.Tick(dt);
  axis_out = ReadOutput(instance, kJoyAxis, 0);
  Check(axis_out != nullptr && std::fabs(axis_out->AsFloat() - 0.6f) < 1e-6f,
        "JoystickAxis reports the injected 0.6");
  button_out = ReadOutput(instance, kJoyButton, 0);
  Check(button_out != nullptr && button_out->AsBool(),
        "JoystickButton reports pressed");

  input.axis_value = -0.8f;
  instance.Tick(dt);
  axis_out = ReadOutput(instance, kJoyAxis, 0);
  Check(axis_out != nullptr && std::fabs(axis_out->AsFloat() + 0.8f) < 1e-6f,
        "JoystickAxis reports the injected -0.8");
  Check(axis_out != nullptr && std::fabs(axis_out->AsFloat()) <= 1.0f,
        "JoystickAxis stays inside [-1, 1]");

  input.button_value = false;
  instance.Tick(dt);
  button_out = ReadOutput(instance, kJoyButton, 0);
  Check(button_out != nullptr && !button_out->AsBool(),
        "JoystickButton reports released");

  const KX_RuntimeValue *distance_out = ReadOutput(instance, kProximity, 2);
  Check(distance_out != nullptr && std::fabs(distance_out->AsFloat() - 5.0f) < 1e-4f,
        "ProximitySensor computes distance 5.0 from the object positions");
  Check(file.path_writes[kNearPathValue] == 4,
        "ProximitySensor OnNear fires on every tick (4 ticks -> 4 writes)");
  Check(file.path_writes[kFarPathValue] == 0,
        "ProximitySensor OnFar stays silent while near");

  /* Phase 2: move the target to (9, 12, 0) -> distance 15.0 > 6.0. */
  objects.SetPosition(target, 9.0f, 12.0f, 0.0f);
  instance.Tick(dt);
  instance.Tick(dt);
  distance_out = ReadOutput(instance, kProximity, 2);
  Check(distance_out != nullptr && std::fabs(distance_out->AsFloat() - 15.0f) < 1e-4f,
        "ProximitySensor recomputes distance 15.0 after the target moved");
  Check(file.path_writes[kFarPathValue] == 2,
        "ProximitySensor OnFar fires on every tick while far (2 ticks -> 2 writes)");
  Check(file.path_writes[kNearPathValue] == 4,
        "OnNear does not fire while the target is far");

  /* Phase 3: the target disappears -> the null guard reports distance 0 and
   * OnFar, with OnNear staying silent. */
  scene.return_null = true;
  instance.Tick(dt);
  instance.Tick(dt);
  distance_out = ReadOutput(instance, kProximity, 2);
  Check(distance_out != nullptr && distance_out->AsFloat() == 0.0f,
        "ProximitySensor reports distance 0 when the target is missing");
  Check(file.path_writes[kFarPathValue] == 4,
        "OnFar fires every tick on the missing-target path (2 more writes)");
  Check(file.path_writes[kNearPathValue] == 4,
        "OnNear stays silent on the missing-target path");

  std::printf("INFO assertions=%d\n", assertions);
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
