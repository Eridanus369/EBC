// B6.6 node E2E: WorldPositionLegacy (screen pixel + arbitrary depth) and
// TouchLegacy (screen ray -> first hit -> overlap query). Uses a programmable
// scene adapter, no engine, no physics.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_ValuePool.h"

/* Parallel work streams: their registration tables are not linked here. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}

namespace {

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

// KX_CompiledGraph shares one vector for informational diagnostics and real
// warnings.  "inlined: ..." entries are informational only; keep them visible
// but do not count them as warnings.
std::vector<std::string> FilterInlineDiagnostics(const std::vector<std::string> &diagnostics,
                                                 std::vector<std::string> &info)
{
  std::vector<std::string> warnings;
  for (const std::string &entry : diagnostics) {
    if (entry.rfind("inlined:", 0) == 0) {
      info.push_back(entry);
    }
    else {
      warnings.push_back(entry);
    }
  }
  return warnings;
}

bool Near(float a, float b, float eps = 1e-4f)
{
  return std::fabs(a - b) <= eps;
}

/* ------------------------------------------------------------------------- */
/* Graph helpers                                                              */
/* ------------------------------------------------------------------------- */

IRNode MakeNode(const char *type_id, const char *config = "{}")
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = type_id;
  node.config_json = config;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  KX_NodeTypeId id = registry.FindByRuntimeType(type_id);
  if (id == KX_INVALID_NODE_TYPE_ID) {
    id = registry.FindByEditorType(type_id);
  }
  const KX_NodeSignature *signature = registry.GetSignature(id);
  for (const KX_NodePortSignature &port : signature->inputs) {
    IRPort ir_port;
    ir_port.name = port.name;
    ir_port.kind = port.kind;
    ir_port.value_tag = port.value_type;
    node.inputs.push_back(ir_port);
  }
  for (const KX_NodePortSignature &port : signature->outputs) {
    IRPort ir_port;
    ir_port.name = port.name;
    ir_port.kind = port.kind;
    ir_port.value_tag = port.value_type;
    node.outputs.push_back(ir_port);
  }
  return node;
}

void Connect(GraphIR &ir, int from, int from_port, int to, int to_port)
{
  IRConnection connection;
  connection.from_node = from;
  connection.from_port = from_port;
  connection.to_node = to;
  connection.to_port = to_port;
  ir.connections.push_back(connection);
}

int AddNode(GraphIR &ir, const char *type_id, const char *config = "{}")
{
  ir.nodes.push_back(MakeNode(type_id, config));
  return int(ir.nodes.size()) - 1;
}

int AddFloat(GraphIR &ir, float value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": \"%.4f\"}", double(value));
  return AddNode(ir, "ConstFloat", config);
}

/* ------------------------------------------------------------------------- */
/* Test sinks (never shipped)                                                 */
/* ------------------------------------------------------------------------- */

struct VectorCapture {
  bool seen = false;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct ObjectCapture {
  bool seen = false;
  KX_GameObject *object = nullptr;
};

struct ListCapture {
  bool seen = false;
  std::size_t count = 0;
  std::vector<KX_GameObject *> objects;
};

struct CounterCapture {
  int count = 0;
};

VectorCapture g_vectors[8];
ObjectCapture g_objects[8];
ListCapture g_lists[8];
CounterCapture g_counters[8];

const KX_FuncPort kVectorSinkInputs[] = {
    {"OnEnter", KX_VALUE_NONE, true},
    {"Value", KX_VALUE_VECTOR, false},
};
const KX_FuncPort kObjectSinkInputs[] = {
    {"OnEnter", KX_VALUE_NONE, true},
    {"Value", KX_VALUE_OBJECT, false},
};
const KX_FuncPort kListSinkInputs[] = {
    {"OnEnter", KX_VALUE_NONE, true},
    {"Value", KX_VALUE_LIST, false},
};
const KX_FuncPort kCounterInputs[] = {
    {"OnEnter", KX_VALUE_NONE, true},
};

void FuncVectorSink(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int slot = node.ConfigInt("slot", 0);
  if (slot < 0 || slot >= 8) {
    return;
  }
  float value[3];
  node.InVector(ctx, 1, value);
  g_vectors[slot].seen = true;
  g_vectors[slot].x = value[0];
  g_vectors[slot].y = value[1];
  g_vectors[slot].z = value[2];
}

void FuncObjectSink(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int slot = node.ConfigInt("slot", 0);
  if (slot < 0 || slot >= 8) {
    return;
  }
  g_objects[slot].seen = true;
  g_objects[slot].object = node.InObject(ctx, 1);
}

void FuncListSink(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int slot = node.ConfigInt("slot", 0);
  if (slot < 0 || slot >= 8) {
    return;
  }
  ListCapture &capture = g_lists[slot];
  capture.seen = true;
  capture.count = 0;
  capture.objects.clear();
  void *raw = node.In(ctx, 1).AsList();
  KX_ValueList *list = static_cast<KX_ValueList *>(raw);
  if (list == nullptr) {
    return;
  }
  capture.count = list->size();
  for (const KX_RuntimeValue &value : *list) {
    capture.objects.push_back(static_cast<KX_GameObject *>(value.AsPointer()));
  }
}

void FuncCounter(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int slot = node.ConfigInt("slot", 0);
  if (slot >= 0 && slot < 8) {
    ++g_counters[slot].count;
  }
}

KX_FunctionSpec kVectorSinkSpec = {"VectorSink",
                                   kVectorSinkInputs,
                                   2,
                                   nullptr,
                                   0,
                                   FuncVectorSink,
                                   nullptr,
                                   nullptr,
                                   0,
                                   8,
                                   false,
                                   false,
                                   false};
KX_FunctionSpec kObjectSinkSpec = {"ObjectSink",
                                   kObjectSinkInputs,
                                   2,
                                   nullptr,
                                   0,
                                   FuncObjectSink,
                                   nullptr,
                                   nullptr,
                                   0,
                                   8,
                                   false,
                                   false,
                                   false};
KX_FunctionSpec kListSinkSpec = {"ListSink",
                                 kListSinkInputs,
                                 2,
                                 nullptr,
                                 0,
                                 FuncListSink,
                                 nullptr,
                                 nullptr,
                                 0,
                                 8,
                                 false,
                                 false,
                                 false};
KX_FunctionSpec kCounterSpec = {"StepCounter",
                                kCounterInputs,
                                1,
                                nullptr,
                                0,
                                FuncCounter,
                                nullptr,
                                nullptr,
                                0,
                                8,
                                false,
                                false,
                                false};

void RegisterTestSpec(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    port.kind = spec.inputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.inputs[i].type;
    signature.inputs.push_back(port);
  }
  const KX_FunctionSpec *entry = &spec;
  KX_NodeRegistry::Get().RegisterType(
      spec.type_id, std::string("KX_Test_") + spec.type_id, signature, [entry]() {
        return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry));
      });
}

void RegisterTestNodes()
{
  RegisterTestSpec(kVectorSinkSpec);
  RegisterTestSpec(kObjectSinkSpec);
  RegisterTestSpec(kListSinkSpec);
  RegisterTestSpec(kCounterSpec);
}

/* ------------------------------------------------------------------------- */
/* Programmable scene query adapter                                          */
/* ------------------------------------------------------------------------- */

class QueryScene : public KX_SceneAdapter {
 public:
  /* Task 4: programmable camera used by the inverse projection. */
  bool any_depth_ok = true;
  float camera_position[3] = {0.0f, 0.0f, 5.0f};
  float camera_forward[3] = {0.0f, 0.0f, -1.0f};
  int any_depth_calls = 0;
  float last_shown_sx = -1.0f;
  float last_shown_sy = -1.0f;
  float last_shown_depth = 0.0f;

  bool RayCastFromScreenAnyDepth(float sx, float sy, float depth, float out[3]) override
  {
    ++any_depth_calls;
    last_shown_sx = sx;
    last_shown_sy = sy;
    last_shown_depth = depth;
    out[0] = out[1] = out[2] = 0.0f;
    if (!any_depth_ok) {
      return false;
    }
    for (int i = 0; i < 3; ++i) {
      out[i] = camera_position[i] + camera_forward[i] * depth;
    }
    return true;
  }

  /* Task 5: programmable ray hit and overlap list. */
  bool ray_ok = false;
  KX_GameObject *ray_object = nullptr;
  float ray_point[3] = {0.0f, 0.0f, 0.0f};
  float ray_normal[3] = {0.0f, 0.0f, 0.0f};
  int ray_calls = 0;
  float last_nx = -1.0f;
  float last_ny = -1.0f;

  bool RayCastFromScreen(float nx, float ny, KX_GameObject *, KX_RayHit &out) override
  {
    ++ray_calls;
    last_nx = nx;
    last_ny = ny;
    out = KX_RayHit();
    if (!ray_ok) {
      return false;
    }
    out.object = ray_object;
    std::memcpy(out.point, ray_point, sizeof(ray_point));
    std::memcpy(out.normal, ray_normal, sizeof(ray_normal));
    out.distance = 1.0f;
    return true;
  }

  /* Objects near the hit point, as (object, distance) pairs. */
  std::vector<std::pair<KX_GameObject *, float> > overlaps;
  int overlap_calls = 0;
  float last_threshold = -1.0f;

  void OverlapQuery(const KX_RayHit &hit,
                    float threshold,
                    KX_OverlapCallback fn,
                    void *user) override
  {
    ++overlap_calls;
    last_threshold = threshold;
    if (fn == nullptr) {
      return;
    }
    if (hit.object != nullptr) {
      KX_OverlapHit own;
      own.object = hit.object;
      std::memcpy(own.point, hit.point, sizeof(own.point));
      std::memcpy(own.normal, hit.normal, sizeof(own.normal));
      fn(own, user);
    }
    for (const std::pair<KX_GameObject *, float> &entry : overlaps) {
      if (entry.first == nullptr || entry.second > threshold) {
        continue;
      }
      KX_OverlapHit nearby;
      nearby.object = entry.first;
      nearby.point[0] = hit.point[0];
      nearby.point[1] = hit.point[1];
      nearby.point[2] = hit.point[2];
      nearby.normal[0] = hit.normal[0];
      nearby.normal[1] = hit.normal[1];
      nearby.normal[2] = hit.normal[2];
      fn(nearby, user);
    }
  }

  float ScreenWidth() const override { return 1920.0f; }
  float ScreenHeight() const override { return 1080.0f; }
};

/* ------------------------------------------------------------------------- */
/* Task 4: WorldPositionLegacy                                                */
/* ------------------------------------------------------------------------- */

void TestWorldPosition()
{
  std::printf("--- WorldPositionLegacy ---\n");
  QueryScene scene;
  KX_GameObject *owner = reinterpret_cast<KX_GameObject *>(uintptr_t(0x100));

  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int sx = AddFloat(ir, 960.0f);
  int sy = AddFloat(ir, 540.0f);
  int d10 = AddFloat(ir, 10.0f);
  int d0 = AddFloat(ir, 0.0f);
  int dneg = AddFloat(ir, -2.0f);

  int front = AddNode(ir, "WorldPositionLegacy");
  int at_camera = AddNode(ir, "WorldPositionLegacy");
  int behind = AddNode(ir, "WorldPositionLegacy");

  int sink_front = AddNode(ir, "VectorSink", "{\"slot\": 0}");
  int sink_camera = AddNode(ir, "VectorSink", "{\"slot\": 1}");
  int sink_behind = AddNode(ir, "VectorSink", "{\"slot\": 2}");
  int counter_front = AddNode(ir, "StepCounter", "{\"slot\": 0}");
  int counter_camera = AddNode(ir, "StepCounter", "{\"slot\": 1}");
  int counter_behind = AddNode(ir, "StepCounter", "{\"slot\": 2}");

  Connect(ir, on_init, 0, front, 0);
  Connect(ir, on_init, 0, at_camera, 0);
  Connect(ir, on_init, 0, behind, 0);
  Connect(ir, sx, 0, front, 1);
  Connect(ir, sy, 0, front, 2);
  Connect(ir, d10, 0, front, 3);
  Connect(ir, sx, 0, at_camera, 1);
  Connect(ir, sy, 0, at_camera, 2);
  Connect(ir, d0, 0, at_camera, 3);
  Connect(ir, sx, 0, behind, 1);
  Connect(ir, sy, 0, behind, 2);
  Connect(ir, dneg, 0, behind, 3);
  Connect(ir, front, 1, sink_front, 1);
  Connect(ir, at_camera, 1, sink_camera, 1);
  Connect(ir, behind, 1, sink_behind, 1);
  Connect(ir, front, 0, counter_front, 0);
  Connect(ir, at_camera, 0, counter_camera, 0);
  Connect(ir, behind, 0, counter_behind, 0);

  KX_GraphInstance instance(nullptr, owner, nullptr);
  std::vector<std::string> warnings;
  Check(instance.Load(ir, &warnings), "WorldPosition graph compiles");
  std::vector<std::string> inline_info;
  const std::vector<std::string> compiler_warnings = FilterInlineDiagnostics(warnings, inline_info);
  for (const std::string &warning : compiler_warnings) {
    std::printf("  [worldpos] %s\n", warning.c_str());
  }
  for (const std::string &entry : inline_info) {
    std::printf("  [worldpos][info] %s\n", entry.c_str());
  }
  std::printf("  [worldpos][info] count=%zu (informational, not a warning)\n", inline_info.size());
  Check(compiler_warnings.empty(), "WorldPosition graph has no compiler warnings");
  KX_ExecutionContext *ctx = instance.GetContext();
  ctx->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                   nullptr, nullptr);
  instance.Tick(0.05f);
  instance.Tick(0.05f);

  std::printf("  depth 10 -> (%.3f, %.3f, %.3f)\n",
              double(g_vectors[0].x),
              double(g_vectors[0].y),
              double(g_vectors[0].z));
  std::printf("  depth  0 -> (%.3f, %.3f, %.3f)\n",
              double(g_vectors[1].x),
              double(g_vectors[1].y),
              double(g_vectors[1].z));
  std::printf("  depth -2 -> (%.3f, %.3f, %.3f)\n",
              double(g_vectors[2].x),
              double(g_vectors[2].y),
              double(g_vectors[2].z));

  Check(scene.any_depth_calls >= 3, "WorldPosition queried the adapter for every trigger");
  Check(scene.last_shown_sx == 960.0f && scene.last_shown_sy == 540.0f,
        "screen pixel coordinates are passed through unchanged (960, 540)");
  Check(g_vectors[0].seen && Near(g_vectors[0].x, 0.0f) && Near(g_vectors[0].y, 0.0f) &&
            Near(g_vectors[0].z, -5.0f),
        "depth 10 in front of camera (0,0,5) facing -Z is (0,0,-5)");
  Check(g_vectors[1].seen && Near(g_vectors[1].x, 0.0f) && Near(g_vectors[1].y, 0.0f) &&
            Near(g_vectors[1].z, 5.0f),
        "depth 0 returns the camera position (0,0,5)");
  Check(g_vectors[2].seen && Near(g_vectors[2].z, 7.0f),
        "negative depth lands behind the camera (0,0,7)");
  Check(g_counters[0].count >= 1 && g_counters[1].count >= 1 && g_counters[2].count >= 1,
        "OnDone fired for every WorldPosition branch");
}

/* ------------------------------------------------------------------------- */
/* Task 5: TouchLegacy                                                       */
/* ------------------------------------------------------------------------- */

void ClearCaptures()
{
  for (int i = 0; i < 8; ++i) {
    g_vectors[i] = VectorCapture();
    g_objects[i] = ObjectCapture();
    g_lists[i] = ListCapture();
    g_counters[i] = CounterCapture();
  }
}

void RunTouchCase(const char *name,
                  bool ray_ok,
                  KX_GameObject *target,
                  std::vector<std::pair<KX_GameObject *, float> > overlaps)
{
  ClearCaptures();
  QueryScene scene;
  scene.ray_ok = ray_ok;
  scene.ray_object = target;
  scene.ray_point[0] = 0.0f;
  scene.ray_point[1] = 0.0f;
  scene.ray_point[2] = 4.5f;
  scene.ray_normal[0] = 0.0f;
  scene.ray_normal[1] = 0.0f;
  scene.ray_normal[2] = 1.0f;
  scene.overlaps = overlaps;

  KX_GameObject *owner = reinterpret_cast<KX_GameObject *>(uintptr_t(0x100));
  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int sx = AddFloat(ir, 960.0f);
  int sy = AddFloat(ir, 540.0f);
  int touch = AddNode(ir, "TouchLegacy", "{\"overlap_threshold\": 0.1}");
  int target_sink = AddNode(ir, "ObjectSink", "{\"slot\": 0}");
  int list_sink = AddNode(ir, "ListSink", "{\"slot\": 0}");
  int point_sink = AddNode(ir, "VectorSink", "{\"slot\": 0}");
  int normal_sink = AddNode(ir, "VectorSink", "{\"slot\": 1}");
  int counter = AddNode(ir, "StepCounter", "{\"slot\": 0}");

  Connect(ir, on_init, 0, touch, 0);
  Connect(ir, sx, 0, touch, 1);
  Connect(ir, sy, 0, touch, 2);
  Connect(ir, touch, 1, target_sink, 1);
  Connect(ir, touch, 2, list_sink, 1);
  Connect(ir, touch, 3, point_sink, 1);
  Connect(ir, touch, 4, normal_sink, 1);
  Connect(ir, touch, 0, target_sink, 0);
  Connect(ir, touch, 0, list_sink, 0);
  Connect(ir, touch, 0, point_sink, 0);
  Connect(ir, touch, 0, normal_sink, 0);
  Connect(ir, touch, 0, counter, 0);

  KX_GraphInstance instance(nullptr, owner, nullptr);
  std::vector<std::string> warnings;
  std::printf("--- TouchLegacy: %s ---\n", name);
  Check(instance.Load(ir, &warnings), "Touch graph compiles");
  std::vector<std::string> inline_info;
  const std::vector<std::string> compiler_warnings = FilterInlineDiagnostics(warnings, inline_info);
  for (const std::string &warning : compiler_warnings) {
    std::printf("  [touch] %s\n", warning.c_str());
  }
  for (const std::string &entry : inline_info) {
    std::printf("  [touch][info] %s\n", entry.c_str());
  }
  std::printf("  [touch][info] count=%zu (informational, not a warning)\n", inline_info.size());
  Check(compiler_warnings.empty(), "Touch graph has no compiler warnings");
  KX_ExecutionContext *ctx = instance.GetContext();
  ctx->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                   nullptr, nullptr);
  instance.Tick(0.05f);
  instance.Tick(0.05f);

  std::printf("  target=%p list=%zu point=(%.3f,%.3f,%.3f) normal=(%.3f,%.3f,%.3f)\n",
              static_cast<void *>(g_objects[0].object),
              g_lists[0].count,
              double(g_vectors[0].x),
              double(g_vectors[0].y),
              double(g_vectors[0].z),
              double(g_vectors[1].x),
              double(g_vectors[1].y),
              double(g_vectors[1].z));

  Check(scene.ray_calls >= 1, "Touch cast the screen ray");
  Check(Near(scene.last_nx, 0.5f) && Near(scene.last_ny, 0.5f),
        "960/540 on a 1920x1080 screen normalized to (0.5, 0.5)");
  Check(!ray_ok || Near(scene.last_threshold, 0.1f),
        "overlap_threshold config reached the adapter as 0.1");
  Check(g_counters[0].count >= 1, "OnDone fired (including the empty space case)");
  Check(g_lists[0].seen, "Objects list output was produced");
}

void TestTouch()
{
  KX_GameObject *a = reinterpret_cast<KX_GameObject *>(uintptr_t(0x200));
  KX_GameObject *b = reinterpret_cast<KX_GameObject *>(uintptr_t(0x210));

  RunTouchCase("single target", true, a, {});
  Check(g_objects[0].seen && g_objects[0].object == a, "Target is the first ray hit A");
  Check(g_lists[0].count == 1 && g_lists[0].objects[0] == a, "Objects contains A");
  Check(Near(g_vectors[0].x, 0.0f) && Near(g_vectors[0].y, 0.0f) && Near(g_vectors[0].z, 4.5f),
        "Point is the ray hit on A's surface (0,0,4.5)");
  Check(Near(g_vectors[1].x, 0.0f) && Near(g_vectors[1].y, 0.0f) && Near(g_vectors[1].z, 1.0f),
        "Normal points back toward the camera (0,0,1)");

  RunTouchCase("empty space", false, nullptr, {});
  Check(g_objects[0].seen && g_objects[0].object == nullptr, "Empty space yields no Target");
  Check(g_lists[0].seen && g_lists[0].count == 0, "Empty space yields an empty Objects list");
  Check(Near(g_vectors[0].x, 0.0f) && Near(g_vectors[0].z, 0.0f),
        "Empty space yields a zero Point");
  Check(Near(g_vectors[1].x, 0.0f) && Near(g_vectors[1].z, 0.0f),
        "Empty space yields a zero Normal");
  Check(g_counters[0].count >= 1, "Empty space still completes OnDone");

  RunTouchCase("overlapping objects", true, a, {{b, 0.02f}});
  Check(g_lists[0].count == 2, "Overlapping B is reported next to A (two objects)");
  Check(g_lists[0].objects[0] == a && g_lists[0].objects[1] == b,
        "Objects order is the ray hit first, then the overlapping object");
  Check(g_objects[0].object == a, "Target remains the ray hit A even when B overlaps");
}

}  // namespace

int main(int argc, char **argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();
  RegisterTestNodes();

  const std::string mode = argc > 1 ? argv[1] : "all";
  if (mode == "all" || mode == "worldpos") {
    TestWorldPosition();
  }
  if (mode == "all" || mode == "touch") {
    TestTouch();
  }
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
