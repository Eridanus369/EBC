// B6.3 node E2E: network nodes, extended sensors, advanced scenes and the
// Collision port extension built on a legacy (3 output) IR graph.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "KX_FunctionNode.h"
#include "KX_GraphInstance.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

/* B5.2 / B6.2 are parallel work streams: stub their tables. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

namespace {

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

IRNode MakeNode(const char *type_id, const char *config = "{}")
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = type_id;
  node.config_json = config;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  KX_NodeTypeId id = registry.FindByEditorType(type_id);
  if (id == KX_INVALID_NODE_TYPE_ID) {
    id = registry.FindByRuntimeType(type_id);
  }
  if (id == KX_INVALID_NODE_TYPE_ID) {
    std::printf("FAIL unknown runtime type %s\n", type_id);
    ++failures;
    return node;
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
  /* ConstFloat reads its value as a JSON string (see KX_Node_ConstFloat::Configure). */
  std::snprintf(config, sizeof(config), "{\"value\": \"%.4f\"}", double(value));
  return AddNode(ir, "ConstFloat", config);
}

int AddInt(GraphIR &ir, int value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": %d}", value);
  return AddNode(ir, "IntLiteral", config);
}

int AddString(GraphIR &ir, const char *value)
{
  char config[128];
  std::snprintf(config, sizeof(config), "{\"value\": \"%s\"}", value);
  return AddNode(ir, "StringConst", config);
}

/* There is no int literal node in the shipped catalog, so the harness registers
 * a minimal one. It is test scaffolding only and never ships. */
const KX_FuncPort kIntLiteralOutput[] = {{"Value", KX_VALUE_INT, false}};

void FuncIntLiteral(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutInt(ctx, 0, node.ConfigInt("value", 0));
}

KX_FunctionSpec kIntLiteralSpec = {"IntLiteral",          nullptr,
                                   0,                    kIntLiteralOutput,
                                   1,                    FuncIntLiteral,
                                   nullptr,              nullptr,
                                   0,                    8,
                                   true,                 false,
                                   false};

void RegisterIntLiteral()
{
  KX_NodeSignature signature;
  KX_NodePortSignature port;
  port.name = "Value";
  port.kind = IRPortKind::Value;
  port.value_type = KX_VALUE_INT;
  signature.outputs.push_back(port);
  KX_NodeRegistry::Get().RegisterType(
      "IntLiteral", "KX_Func_IntLiteral", signature, []() {
        return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(&kIntLiteralSpec));
      });
}

void DumpWarnings(const char *what, const std::vector<std::string> &warnings)
{
  for (const std::string &warning : warnings) {
    std::printf("  [%s] %s\n", what, warning.c_str());
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

class ScaleScene : public KX_SceneAdapter {
 public:
  std::vector<float> scales;
  void SetTimeScale(float scale) override { scales.push_back(scale); }
};

/* ------------------------------------------------------------------------- */
/* Network nodes                                                              */
/* ------------------------------------------------------------------------- */

void TestNetworkNodes()
{
  std::printf("--- network nodes ---\n");
  KX_MockNetworkAdapter network;
  network.local_player = 1;
  network.player_count = 2;
  network.host_result = true;
  network.join_result = true;
  network.connected = true;
  network.remote_props[2]["2"] = KX_RuntimeValue::Float(9.0f);
  KX_NetworkEvent message_event;
  message_event.type = KX_NetworkEvent::Message;
  message_event.player_id = 2;
  message_event.data = "hello";
  network.events.push_back(message_event);
  KX_NetworkEvent join_event;
  join_event.type = KX_NetworkEvent::PlayerJoin;
  join_event.player_id = 2;
  network.events.push_back(join_event);

  ScaleScene scene;
  KX_MockUIAdapter ui;
  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int host = AddNode(ir, "HostRoom");
  int message = AddNode(ir, "SendNetworkMessage", "{\"reliable\": \"true\"}");
  int broadcast = AddNode(ir, "BroadcastMessage");
  int sync = AddNode(ir, "SyncProperty");
  int remote = AddNode(ir, "GetRemoteProperty");
  int event = AddNode(ir, "OnNetworkEvent", "{\"event_type\": \"3\"}");
  int scale = AddNode(ir, "SetTimeScale");
  int two = AddInt(ir, 2);
  int prop_key = AddString(ir, "score");
  int remote_key = AddString(ir, "2");

  Connect(ir, on_init, 0, host, 0);
  Connect(ir, on_init, 0, message, 0);
  Connect(ir, on_init, 0, broadcast, 0);
  Connect(ir, on_init, 0, sync, 0);
  Connect(ir, on_init, 0, remote, 0);
  Connect(ir, two, 0, message, 1);
  Connect(ir, two, 0, remote, 1);
  Connect(ir, prop_key, 0, sync, 1);
  Connect(ir, two, 0, sync, 2);
  Connect(ir, remote_key, 0, remote, 2);
  Connect(ir, event, 2, scale, 0);
  Connect(ir, event, 4, scale, 1);

  KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x100)), nullptr);
  std::vector<std::string> warnings;
  Check(instance.Load(ir, &warnings), "network graph compiles");
  DumpWarnings("network", warnings);
  std::vector<std::string> inline_info;
  const std::vector<std::string> compiler_warnings = FilterInlineDiagnostics(warnings, inline_info);
  for (const std::string &info : inline_info) {
    std::printf("  [network][info] %s\n", info.c_str());
  }
  std::printf("  [network][info] count=%zu (informational, not a warning)\n", inline_info.size());
  Check(compiler_warnings.empty(), "network graph has no compiler warnings");
  KX_ExecutionContext *ctx = instance.GetContext();
  ctx->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ui, nullptr,
                   &network);
  instance.Tick(0.05f);

  Check(network.host_called, "HostRoom node called the adapter");
  Check(network.sent.size() == 2, "send plus broadcast queued two packets");
  bool saw_reliable = false;
  bool saw_broadcast = false;
  for (const KX_MockNetworkAdapter::SentPacket &packet : network.sent) {
    saw_reliable = saw_reliable || (!packet.broadcast && packet.reliable && packet.target == 2);
    saw_broadcast = saw_broadcast || packet.broadcast;
  }
  Check(saw_reliable, "SendNetworkMessage honoured target and reliable flag");
  Check(saw_broadcast, "BroadcastMessage queued a broadcast packet");
  Check(network.local_props.count("score") == 1 && network.local_props["score"].data.f == 2.0f,
        "SyncProperty stored the key/value diff");
  Check(scene.scales.size() >= 1 && scene.scales.back() == 2.0f,
        "OnNetworkEvent expanded the message and passed PlayerId downstream");
}

/* ------------------------------------------------------------------------- */
/* Sensors                                                                    */
/* ------------------------------------------------------------------------- */

class FakeScene : public KX_SceneAdapter {
 public:
  std::vector<float> scales;
  std::vector<std::string> loads;
  std::vector<KX_ProximityHit> proximity;
  KX_GameObject *area = nullptr;

  void SetTimeScale(float scale) override { scales.push_back(scale); }
  void QueryProximity(KX_GameObject *, const float *, float, float, const std::string &,
                      std::vector<KX_ProximityHit> &out) override
  {
    out.insert(out.end(), proximity.begin(), proximity.end());
  }
  KX_GameObject *FindObject(const std::string &name) override { return name == "2" ? area : nullptr; }
  bool LoadScene(const std::string &name) override
  {
    loads.push_back(name);
    return true;
  }
  float ScreenWidth() const override { return 800.0f; }
  float ScreenHeight() const override { return 600.0f; }
  void SceneNames(std::vector<std::string> &out) override { out = {"2", "3"}; }
};

class FakeObjects : public KX_ObjectAdapter {
 public:
  std::map<KX_GameObject *, std::vector<float> > position;
  std::map<KX_GameObject *, std::vector<float> > scale_map;
  std::map<KX_GameObject *, unsigned short> group;
  std::map<KX_GameObject *, std::vector<float> > velocity;

  bool GetWorldPosition(KX_GameObject *object, float out[3]) override
  {
    auto it = position.find(object);
    if (it == position.end()) {
      return false;
    }
    for (int i = 0; i < 3; ++i) {
      out[i] = it->second[i];
    }
    return true;
  }
  bool GetWorldScale(KX_GameObject *object, float out[3]) override
  {
    auto it = scale_map.find(object);
    if (it == scale_map.end()) {
      return false;
    }
    for (int i = 0; i < 3; ++i) {
      out[i] = it->second[i];
    }
    return true;
  }
  unsigned short GetCollisionGroup(KX_GameObject *object) const override
  {
    auto it = group.find(object);
    return it == group.end() ? 0 : it->second;
  }
  bool GetLinearVelocity(KX_GameObject *object, float out[3]) const override
  {
    auto it = velocity.find(object);
    if (it == velocity.end()) {
      return false;
    }
    for (int i = 0; i < 3; ++i) {
      out[i] = it->second[i];
    }
    return true;
  }
};

class FakePhysics : public KX_PhysicsAdapter {
 public:
  KX_GameObject *blocker = nullptr;
  KX_GameObject *target = nullptr;
  std::map<KX_GameObject *, std::pair<std::vector<float>, std::vector<float> > > bounds;
  float blocker_z = 2.0f;
  float target_z = 5.0f;

  bool WorldBounds(KX_GameObject *object, float min[3], float max[3]) override
  {
    auto it = bounds.find(object);
    if (it == bounds.end()) {
      return false;
    }
    for (int i = 0; i < 3; ++i) {
      min[i] = it->second.first[i];
      max[i] = it->second.second[i];
    }
    return true;
  }

  KX_RaycastResult Raycast(const float origin[3], const float direction[3], float max_dist,
                           const KX_GameObject *) override
  {
    KX_RaycastResult result;
    if (direction[2] <= 0.0f || origin[2] >= target_z) {
      return result;
    }
    const bool first_segment = origin[2] < blocker_z;
    const KX_GameObject *hit_object = first_segment ? blocker : target;
    const float z = first_segment ? blocker_z : target_z;
    if (hit_object == nullptr || (z - origin[2]) > max_dist) {
      return result;
    }
    result.hit = true;
    result.object = const_cast<KX_GameObject *>(hit_object);
    result.point[0] = origin[0];
    result.point[1] = origin[1];
    result.point[2] = z;
    result.normal[2] = -1.0f;
    return result;
  }
};

class FakeSceneControl : public KX_SceneControlAdapter {
 public:
  std::vector<std::string> active = {"2", "3"};
  std::vector<std::string> known = {"2", "3", "4"};
  std::vector<std::pair<std::string, float> > scene_scales;
  bool unload_result = false;
  std::string last_reason;

  bool HasScene(const std::string &name) override
  {
    for (const std::string &scene : known) {
      if (scene == name) {
        return true;
      }
    }
    return false;
  }
  bool UnloadScene(const std::string &name) override
  {
    return UnloadSceneDetailed(name, nullptr) == KX_SceneUnloadResult::Success;
  }
  KX_SceneUnloadResult UnloadSceneDetailed(const std::string &name,
                                           std::string *reason) override
  {
    if (!HasScene(name)) {
      last_reason = "scene not found";
      if (reason != nullptr) { *reason = last_reason; }
      return KX_SceneUnloadResult::Missing;
    }
    for (const std::string &scene : active) {
      if (scene == name) {
        last_reason = "scene is active";
        if (reason != nullptr) { *reason = last_reason; }
        return KX_SceneUnloadResult::SceneActive;
      }
    }
    last_reason = "engine RemoveScene is disabled";
    if (reason != nullptr) { *reason = last_reason; }
    return unload_result ? KX_SceneUnloadResult::Success : KX_SceneUnloadResult::Unsupported;
  }
  void ActiveSceneNames(std::vector<std::string> &out) override { out = active; }
  bool SetSceneTimeScale(const std::string &name, float scale) override
  {
    scene_scales.push_back(std::make_pair(name, scale));
    return true;
  }
};

void TestSensors()
{
  std::printf("--- sensors ---\n");
  FakeScene scene;
  FakeObjects objects;
  FakePhysics physics;
  KX_MockUIAdapter ui;
  KX_GameObject *owner = reinterpret_cast<KX_GameObject *>(uintptr_t(0x100));
  KX_GameObject *near_object = reinterpret_cast<KX_GameObject *>(uintptr_t(0x120));
  KX_GameObject *far_object = reinterpret_cast<KX_GameObject *>(uintptr_t(0x130));
  KX_GameObject *blocker = reinterpret_cast<KX_GameObject *>(uintptr_t(0x140));
  KX_GameObject *area = reinterpret_cast<KX_GameObject *>(uintptr_t(0x150));

  objects.position[owner] = {0.0f, 0.0f, 0.0f};
  objects.position[near_object] = {0.0f, 0.0f, 5.0f};
  objects.position[far_object] = {0.0f, 0.0f, 7.0f};
  objects.position[blocker] = {0.0f, 0.0f, 2.0f};
  objects.position[area] = {0.0f, 0.0f, 0.0f};
  objects.scale_map[area] = {10.0f, 10.0f, 10.0f};
  objects.group[blocker] = 2;
  objects.group[near_object] = 1;
  scene.area = area;
  scene.proximity.push_back(KX_ProximityHit{near_object, 5.0f, 0.0f});
  scene.proximity.push_back(KX_ProximityHit{far_object, 7.0f, 0.0f});
  physics.blocker = blocker;
  physics.target = near_object;

  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int radar = AddNode(ir, "RadarExtended");
  int radar_scale = AddNode(ir, "SetTimeScale");
  int foreach = AddNode(ir, "ForEach");
  int foreach_scale = AddNode(ir, "SetTimeScale");
  int los_hidden = AddNode(ir, "LineOfSight");
  int los_masked = AddNode(ir, "LineOfSight");
  int hidden_scale = AddNode(ir, "SetTimeScale");
  int visible_scale = AddNode(ir, "SetTimeScale");
  int area_node = AddNode(ir, "AreaSensor");
  int area_scale = AddNode(ir, "SetTimeScale");
  int area_name = AddString(ir, "2");
  int ray_node = AddNode(ir, "RaycastSensor");
  int ray_scale = AddNode(ir, "SetTimeScale");
  int ray_dir = AddNode(ir, "Vector3");
  int ray_x = AddFloat(ir, 0.0f);
  int ray_y = AddFloat(ir, 0.0f);
  int ray_z = AddFloat(ir, 1.0f);
  int ray_len = AddFloat(ir, 10.0f);
  int mask_plain = AddInt(ir, 0);
  int mask_two = AddInt(ir, 2);
  /* 0.5 / 0.75 / 2.5 / 4.0 are markers written back through SetTimeScale so
   * each trigger branch is observable independently of execution order. */
  int hidden_marker = AddFloat(ir, 0.5f);
  int visible_marker = AddFloat(ir, 0.75f);
  int area_marker = AddFloat(ir, 2.5f);
  int ray_marker = AddFloat(ir, 4.0f);
  int each_marker = AddFloat(ir, 3.0f);

  Connect(ir, on_init, 0, radar, 0);
  Connect(ir, on_init, 0, los_hidden, 0);
  Connect(ir, on_init, 0, los_masked, 0);
  Connect(ir, on_init, 0, area_node, 0);
  Connect(ir, on_init, 0, ray_node, 0);
  Connect(ir, radar, 2, los_hidden, 1);
  Connect(ir, radar, 2, los_masked, 1);
  Connect(ir, mask_plain, 0, los_hidden, 2);
  Connect(ir, mask_two, 0, los_masked, 2);
  Connect(ir, radar, 0, radar_scale, 0);
  Connect(ir, radar, 3, radar_scale, 1);
  Connect(ir, radar, 1, foreach, 0);
  Connect(ir, radar, 0, foreach, 1);
  Connect(ir, foreach, 0, foreach_scale, 0);
  Connect(ir, each_marker, 0, foreach_scale, 1);
  Connect(ir, los_hidden, 1, hidden_scale, 0);
  Connect(ir, hidden_marker, 0, hidden_scale, 1);
  Connect(ir, los_masked, 0, visible_scale, 0);
  Connect(ir, visible_marker, 0, visible_scale, 1);
  Connect(ir, area_name, 0, area_node, 1);
  Connect(ir, area_node, 0, area_scale, 0);
  Connect(ir, area_marker, 0, area_scale, 1);
  Connect(ir, ray_x, 0, ray_dir, 0);
  Connect(ir, ray_y, 0, ray_dir, 1);
  Connect(ir, ray_z, 0, ray_dir, 2);
  Connect(ir, ray_dir, 0, ray_node, 1);
  Connect(ir, ray_len, 0, ray_node, 2);
  Connect(ir, ray_node, 0, ray_scale, 0);
  Connect(ir, ray_marker, 0, ray_scale, 1);

  KX_GraphInstance instance(nullptr, owner, nullptr);
  std::vector<std::string> warnings;
  Check(instance.Load(ir, &warnings), "sensor graph compiles");
  DumpWarnings("sensor", warnings);
  KX_ExecutionContext *ctx = instance.GetContext();
  ctx->SetAdapters(nullptr, &scene, &physics, &objects, nullptr, nullptr, nullptr, nullptr, &ui, nullptr,
                   nullptr);
  instance.Tick(0.05f);

  bool saw_value[5] = {false, false, false, false, false};
  const float expected[5] = {5.0f, 0.5f, 0.75f, 2.5f, 4.0f};
  int foreach_hits = 0;
  for (float value : scene.scales) {
    for (int i = 0; i < 5; ++i) {
      saw_value[i] = saw_value[i] || value == expected[i];
    }
    foreach_hits += value == 3.0f ? 1 : 0;
  }
  std::printf("  sensor scale values:");
  for (float value : scene.scales) { std::printf(" %.3f", double(value)); }
  std::printf("\n");
  Check(scene.scales.size() == 7, "seven sensor branches fired");
  Check(saw_value[0], "RadarExtended reported the closest distance (5.0)");
  Check(foreach_hits == 2, "RadarExtended FoundObjects held both hits (ForEach ran twice)");
  Check(saw_value[1] && saw_value[2],
        "LineOfSight blocked by the unmasked blocker and saw through it with IgnoreMask=2");
  Check(saw_value[3], "AreaSensor entered the named area");
  Check(saw_value[4], "RaycastSensor hit the blocker");
}

/* ------------------------------------------------------------------------- */
/* Scene control                                                              */
/* ------------------------------------------------------------------------- */

void TestScenes()
{
  std::printf("--- scene nodes ---\n");
  {
    FakeScene scene;
    FakeSceneControl control;
    control.active = {"2"};
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int on_update = AddNode(ir, "OnUpdate");
    int load = AddNode(ir, "LoadSceneAsync", "{\"verbose\": \"true\"}");
    int name = AddString(ir, "2");
    int progress = AddNode(ir, "SetTimeScale");
    Connect(ir, on_init, 0, load, 0);
    Connect(ir, name, 0, load, 1);
    Connect(ir, on_update, 0, progress, 0);
    Connect(ir, load, 0, progress, 1);

    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "async scene graph compiles");
    DumpWarnings("async", warnings);
    KX_ExecutionContext *ctx = instance.GetContext();
    ctx->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr, &control, nullptr,
                     nullptr, nullptr);
    for (int frame = 0; frame < 14; ++frame) {
      instance.Tick(0.05f);
    }
    std::printf("  async progress samples:");
    for (float value : scene.scales) { std::printf(" %.3f", double(value)); }
    std::printf("\n");
    bool monotonic = true;
    for (size_t i = 1; i < scene.scales.size(); ++i) {
      monotonic = monotonic && scene.scales[i] >= scene.scales[i - 1];
    }
    Check(monotonic, "LoadSceneAsync progress is monotonic across frames");
    Check(!scene.scales.empty() && scene.scales.front() <= 0.1f && scene.scales.back() == 1.0f,
          "LoadSceneAsync progress ramps 0..1 over frames");
    Check(scene.loads.size() == 1 && scene.loads[0] == "2",
          "LoadSceneAsync replaced the scene exactly once at the end");
  }

  {
    FakeScene scene;
    FakeSceneControl control;
    KX_MockUIAdapter ui;
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int on_update = AddNode(ir, "OnUpdate");
    int unload = AddNode(ir, "UnloadScene");
    int unload_name = AddString(ir, "2");
    int unload_scale = AddNode(ir, "SetTimeScale");
    int unload_value = AddFloat(ir, 9.9f);
    int unload_done_scale = AddNode(ir, "SetTimeScale");
    int unload_done_value = AddFloat(ir, 1.1f);
    int unload_inactive = AddNode(ir, "UnloadScene");
    int inactive_name = AddString(ir, "4");
    int inactive_scale = AddNode(ir, "SetTimeScale");
    int inactive_value = AddFloat(ir, 8.8f);
    int transition = AddNode(ir, "TransitionScene");
    int from_name = AddString(ir, "2");
    int to_name = AddString(ir, "3");
    int duration = AddFloat(ir, 0.2f);
    int transition_scale = AddNode(ir, "SetTimeScale");
    int transition_value = AddFloat(ir, 7.7f);
    int active = AddNode(ir, "GetActiveScenes");
    int active_scale = AddNode(ir, "SetTimeScale");
    int scene_scale = AddNode(ir, "SetSceneTimeScale");
    int scene_scale_name = AddString(ir, "2");
    int scene_scale_value = AddFloat(ir, 0.5f);

    Connect(ir, on_init, 0, unload, 0);
    Connect(ir, unload_name, 0, unload, 1);
    Connect(ir, unload, 1, unload_scale, 0);      /* OnFail */
    Connect(ir, unload_value, 0, unload_scale, 1);
    Connect(ir, unload, 0, unload_done_scale, 0); /* OnDone must stay silent */
    Connect(ir, unload_done_value, 0, unload_done_scale, 1);
    Connect(ir, on_init, 0, unload_inactive, 0);
    Connect(ir, inactive_name, 0, unload_inactive, 1);
    Connect(ir, unload_inactive, 1, inactive_scale, 0);
    Connect(ir, inactive_value, 0, inactive_scale, 1);
    Connect(ir, on_init, 0, transition, 0);
    Connect(ir, from_name, 0, transition, 1);
    Connect(ir, to_name, 0, transition, 2);
    Connect(ir, duration, 0, transition, 3);
    Connect(ir, transition, 0, transition_scale, 0);
    Connect(ir, transition_value, 0, transition_scale, 1);
    Connect(ir, on_update, 0, active_scale, 0);
    Connect(ir, active, 1, active_scale, 1);
    Connect(ir, on_init, 0, scene_scale, 0);
    Connect(ir, scene_scale_name, 0, scene_scale, 1);
    Connect(ir, scene_scale_value, 0, scene_scale, 2);

    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "scene graph compiles");
    DumpWarnings("scene", warnings);
    KX_ExecutionContext *ctx = instance.GetContext();
    ctx->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr, &control, &ui,
                     nullptr, nullptr);
    for (int frame = 0; frame < 6; ++frame) {
      instance.Tick(0.05f);
    }
    Check(ui.create_canvas_calls >= 1 && ui.create_label_calls >= 1,
          "TransitionScene built the fade overlay through the UI adapter");
    Check(ui.destroy_calls >= 2, "TransitionScene destroyed the overlay after fading");
    bool transitioned = false;
    for (const std::string &name : scene.loads) {
      transitioned = transitioned || name == "3";
    }
    Check(transitioned, "TransitionScene loaded the destination at the fade midpoint");
    bool unload_fail = false;
    bool unload_false_done = false;
    bool unload_unsupported = false;
    bool transition_done = false;
    bool active_count = false;
    for (float value : scene.scales) {
      unload_fail = unload_fail || value == 9.9f;
      unload_false_done = unload_false_done || value == 1.1f;
      unload_unsupported = unload_unsupported || value == 8.8f;
      transition_done = transition_done || value == 7.7f;
      active_count = active_count || value == 2.0f;
    }
    Check(!unload_false_done, "UnloadScene no longer reports OnDone without unloading");
    Check(unload_fail, "UnloadScene fired OnFail for the active scene");
    Check(unload_unsupported, "UnloadScene fired OnFail when the engine cannot remove scenes");
    Check(transition_done, "TransitionScene fired OnDone");
    Check(active_count, "GetActiveScenes exposed the active scene count");
    Check(control.scene_scales.size() == 1 && control.scene_scales[0].first == "2" &&
              control.scene_scales[0].second == 0.5f,
          "SetSceneTimeScale forwarded name and scale");
  }
}

/* ------------------------------------------------------------------------- */
/* Collision port extension                                                   */
/* ------------------------------------------------------------------------- */

void TestCollisionExtension()
{
  std::printf("--- collision extension ---\n");
  FakeScene scene;
  FakeObjects objects;
  FakePhysics physics;
  KX_MockUIAdapter ui;
  KX_GameObject *owner = reinterpret_cast<KX_GameObject *>(uintptr_t(0x100));
  KX_GameObject *other = reinterpret_cast<KX_GameObject *>(uintptr_t(0x120));
  objects.position[owner] = {0.0f, 0.0f, 0.0f};
  objects.position[other] = {0.0f, 0.0f, 0.0f};
  objects.velocity[owner] = {0.0f, 0.0f, 0.0f};
  objects.velocity[other] = {0.0f, 0.0f, 2.0f};
  physics.bounds[owner] = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
  physics.bounds[other] = {{-0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 1.0f}};
  scene.proximity.push_back(KX_ProximityHit{other, 0.0f, 0.0f});

  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int radar = AddNode(ir, "RadarExtended");
  int collision = AddNode(ir, "Collision");
  int scale = AddNode(ir, "SetTimeScale");
  Connect(ir, on_init, 0, radar, 0);
  Connect(ir, radar, 2, collision, 0);
  Connect(ir, collision, 1, scale, 0);
  /* Simulate IR exported before the port extension: three trigger outputs. */
  ir.nodes[collision].outputs.resize(3);

  KX_GraphInstance instance(nullptr, owner, nullptr);
  std::vector<std::string> warnings;
  Check(instance.Load(ir, &warnings), "legacy three output Collision IR still compiles");
  DumpWarnings("legacy", warnings);
  KX_ExecutionContext *ctx = instance.GetContext();
  ctx->SetAdapters(nullptr, &scene, &physics, &objects, nullptr, nullptr, nullptr, nullptr, &ui, nullptr,
                   nullptr);
  /* Frame 1: Collision ticks before the radar trigger has produced its
   * Closest value (the tick pass runs before trigger propagation).
   * Frame 2: first overlap (OnEnter). Frame 3: OnStay. */
  instance.Tick(0.05f);
  instance.Tick(0.05f);
  instance.Tick(0.05f);
  Check(scene.scales.size() >= 1, "legacy Collision OnStay fired");

  GraphIR extended;
  int on_init2 = AddNode(extended, "OnInit");
  int radar2 = AddNode(extended, "RadarExtended");
  int collision2 = AddNode(extended, "Collision");
  int scale2 = AddNode(extended, "SetTimeScale");
  Connect(extended, on_init2, 0, radar2, 0);
  Connect(extended, radar2, 2, collision2, 0);
  Connect(extended, collision2, 1, scale2, 0);
  Connect(extended, collision2, 5, scale2, 1);
  std::vector<std::string> rebuild_warnings;
  Check(instance.Rebuild(extended, &rebuild_warnings), "extended Collision IR rebuilds in place");
  DumpWarnings("rebuild", rebuild_warnings);
  instance.Tick(0.05f);
  instance.Tick(0.05f);
  instance.Tick(0.05f);
  Check(!scene.scales.empty() && scene.scales.back() == 2.0f,
        "Collision ImpactForce reports the relative normal speed");
}

/* ------------------------------------------------------------------------- */
/* Compiler output-port whitelist                                             */
/* ------------------------------------------------------------------------- */

void TestCompilerOutputWhitelist()
{
  std::printf("--- compiler output whitelist ---\n");

  /* FloatMath is not whitelisted: an IR with no outputs must be rejected. */
  GraphIR rejected;
  int math = AddNode(rejected, "FloatMath");
  rejected.nodes[math].outputs.clear();
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  Check(!instance.Load(rejected, &warnings), "FloatMath with a missing output is a compile error");
  bool mismatch_reported = false;
  for (const std::string &warning : warnings) {
    mismatch_reported = mismatch_reported || warning.find("Output port count mismatch") != std::string::npos;
  }
  Check(mismatch_reported, "FloatMath rejection reports the output port count mismatch");
  DumpWarnings("whitelist-reject", warnings);

  /* Collision opts in: the legacy three-output IR still compiles. */
  GraphIR legacy;
  int radar = AddNode(legacy, "RadarExtended");
  int collision = AddNode(legacy, "Collision");
  Connect(legacy, radar, 2, collision, 0);
  legacy.nodes[collision].outputs.resize(3);
  KX_GraphInstance legacy_instance(nullptr, nullptr, nullptr);
  std::vector<std::string> legacy_warnings;
  Check(legacy_instance.Load(legacy, &legacy_warnings),
        "Collision with three outputs still compiles (whitelisted)");
  DumpWarnings("whitelist-accept", legacy_warnings);
}

}  // namespace

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();
  RegisterIntLiteral();
  TestNetworkNodes();
  TestSensors();
  TestScenes();
  TestCollisionExtension();
  TestCompilerOutputWhitelist();
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
