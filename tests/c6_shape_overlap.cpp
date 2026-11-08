// C.6 harness: shape-aware proximity for RadarExtended and AreaSensor.
//
// The engine backend performs the actual narrow phase with Bullet
// (CcdPhysicsEnvironment::TestShapeContact / TestControllerContact). The
// harness cannot link that TU (harness_config.sh deliberately excludes the
// engine adapters), so it drives the same node contract through a reference
// backend that performs exact box/sphere overlap on registered shapes. This
// proves the decision is made by shape geometry rather than by origin distance.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"

void RegisterB5FunctionGameNodes() {}
void RegisterBuiltinGameNodes();

namespace {

int failures = 0;
int assertions = 0;

void Check(bool ok, const char *what, const char *expected, int actual)
{
  ++assertions;
  std::printf("%s %s (expected=%s actual=%d)\n", ok ? "PASS" : "FAIL", what, expected, actual);
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
  std::snprintf(config, sizeof(config), "{\"value\": \"%.4f\"}", double(value));
  return AddNode(ir, "ConstFloat", config);
}

int AddString(GraphIR &ir, const char *value)
{
  char config[160];
  std::snprintf(config, sizeof(config), "{\"value\": \"%s\"}", value);
  return AddNode(ir, "StringConst", config);
}

KX_GameObject *Handle(uintptr_t value)
{
  return reinterpret_cast<KX_GameObject *>(value);
}

/* ------------------------------------------------------------------------- */
/* Capture sink                                                               */
/* ------------------------------------------------------------------------- */

struct Capture {
  std::vector<KX_GameObject *> objects;
  std::vector<float> distances;
  int count = 0;
};

Capture *g_capture = nullptr;

const KX_FuncPort kSinkInputs[] = {
    {"OnEnter", KX_VALUE_NONE, true},
    {"Objects", KX_VALUE_LIST, false},
    {"Distances", KX_VALUE_LIST, false},
    {"Count", KX_VALUE_INT, false},
};

void FuncListSink(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0) || g_capture == nullptr) {
    return;
  }
  g_capture->objects.clear();
  g_capture->distances.clear();
  KX_ValueList *objects = static_cast<KX_ValueList *>(node.In(ctx, 1).AsList());
  KX_ValueList *distances = static_cast<KX_ValueList *>(node.In(ctx, 2).AsList());
  if (objects != nullptr) {
    for (const KX_RuntimeValue &value : *objects) {
      g_capture->objects.push_back(reinterpret_cast<KX_GameObject *>(value.data.ptr));
    }
  }
  if (distances != nullptr) {
    for (const KX_RuntimeValue &value : *distances) {
      g_capture->distances.push_back(value.AsFloat());
    }
  }
  g_capture->count = node.InInt(ctx, 3);
}

KX_FunctionSpec kListSinkSpec = {"TestListSink", kSinkInputs, 4, nullptr, 0, FuncListSink,
                                 nullptr,        nullptr,     0, 8,       false, false,
                                 false};

void RegisterTestNodes()
{
  KX_NodeSignature sink_signature;
  for (const KX_FuncPort &port : kSinkInputs) {
    KX_NodePortSignature signature_port;
    signature_port.name = port.name;
    signature_port.kind = port.trigger ? IRPortKind::Trigger : IRPortKind::Value;
    signature_port.value_type = port.type;
    sink_signature.inputs.push_back(signature_port);
  }
  KX_NodeRegistry::Get().RegisterType("TestListSink",
                                      "KX_Func_TestListSink",
                                      sink_signature,
                                      []() {
                                        return std::unique_ptr<KX_GameNodeRuntime>(
                                            new KX_FunctionNode(&kListSinkSpec));
                                      });
}

/* ------------------------------------------------------------------------- */
/* Reference shape world                                                      */
/* ------------------------------------------------------------------------- */

struct ShapeEntry {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float scale[3] = {1.0f, 1.0f, 1.0f};
  float half[3] = {0.0f, 0.0f, 0.0f};
  float radius = 0.0f; /* > 0 means sphere */
  bool has_collider = true;
  std::string property;
};

class ShapeWorld {
 public:
  std::map<const KX_GameObject *, ShapeEntry> entries;
  std::vector<KX_GameObject *> order;
  std::string find_name;
  KX_GameObject *find_handle = nullptr;
  int degraded_queries = 0;

  void AddBox(KX_GameObject *handle,
              const float pos[3],
              const float half[3],
              bool has_collider,
              const std::string &property = std::string())
  {
    ShapeEntry entry;
    entry.pos[0] = pos[0];
    entry.pos[1] = pos[1];
    entry.pos[2] = pos[2];
    entry.half[0] = half[0];
    entry.half[1] = half[1];
    entry.half[2] = half[2];
    entry.has_collider = has_collider;
    entry.property = property;
    entries[handle] = entry;
    order.push_back(handle);
  }

  void AddSphere(KX_GameObject *handle,
                 const float pos[3],
                 float radius,
                 bool has_collider,
                 const std::string &property = std::string())
  {
    ShapeEntry entry;
    entry.pos[0] = pos[0];
    entry.pos[1] = pos[1];
    entry.pos[2] = pos[2];
    entry.radius = radius;
    entry.has_collider = has_collider;
    entry.property = property;
    entries[handle] = entry;
    order.push_back(handle);
  }

  static float Distance(const ShapeEntry &a, const ShapeEntry &b)
  {
    const float dx = a.pos[0] - b.pos[0];
    const float dy = a.pos[1] - b.pos[1];
    const float dz = a.pos[2] - b.pos[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  static float Extent(const ShapeEntry &entry)
  {
    if (!entry.has_collider) {
      return 1.0f;
    }
    if (entry.radius > 0.0f) {
      return entry.radius;
    }
    return std::sqrt(entry.half[0] * entry.half[0] + entry.half[1] * entry.half[1] +
                     entry.half[2] * entry.half[2]);
  }

  static bool SphereSphere(const ShapeEntry &a, float radius, const ShapeEntry &b)
  {
    return Distance(a, b) <= radius + b.radius;
  }

  static bool SphereBox(const ShapeEntry &query, float radius, const ShapeEntry &box)
  {
    float closest[3];
    for (int axis = 0; axis < 3; ++axis) {
      const float low = box.pos[axis] - box.half[axis];
      const float high = box.pos[axis] + box.half[axis];
      closest[axis] = std::min(std::max(query.pos[axis], low), high);
    }
    const float dx = query.pos[0] - closest[0];
    const float dy = query.pos[1] - closest[1];
    const float dz = query.pos[2] - closest[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz) <= radius;
  }

  static bool BoxBox(const ShapeEntry &a, const ShapeEntry &b)
  {
    for (int axis = 0; axis < 3; ++axis) {
      if (std::fabs(a.pos[axis] - b.pos[axis]) > a.half[axis] + b.half[axis]) {
        return false;
      }
    }
    return true;
  }

  bool Query(const KX_ProximityShapeQuery &query, std::vector<KX_ProximityHit> &out)
  {
    out.clear();
    auto origin_entry = entries.find(query.origin);
    if (origin_entry == entries.end()) {
      return false;
    }
    const ShapeEntry &origin = origin_entry->second;

    ShapeEntry volume = origin;
    if (query.shape == KX_PROXIMITY_BOX) {
      if (!origin.has_collider) {
        for (int axis = 0; axis < 3; ++axis) {
          volume.half[axis] = query.half_extents[axis];
        }
      }
      volume.radius = 0.0f;
    }
    else {
      volume.radius = query.radius;
      volume.has_collider = true;
    }

    float query_extent = query.radius;
    if (query.shape == KX_PROXIMITY_BOX) {
      query_extent = std::max(query.radius, std::max(Extent(volume), Extent(origin)));
    }

    struct Candidate {
      KX_GameObject *handle;
      const ShapeEntry *entry;
      float distance;
    };
    std::vector<Candidate> candidates;
    for (KX_GameObject *handle : order) {
      if (handle == query.origin) {
        continue;
      }
      auto found = entries.find(handle);
      if (found == entries.end()) {
        continue;
      }
      const ShapeEntry &entry = found->second;
      if (!query.property.empty() && entry.property != query.property) {
        continue;
      }
      const float distance = Distance(origin, entry);
      if (distance > query_extent + Extent(entry)) {
        continue;
      }
      candidates.push_back(Candidate{handle, &entry, distance});
    }

    if (static_cast<int>(candidates.size()) > KX_PROXIMITY_SHAPE_BUDGET) {
      ++degraded_queries;
      /* Degraded path: exactly the legacy origin-distance result. */
      for (const Candidate &candidate : candidates) {
        if (candidate.distance > query.radius) {
          continue;
        }
        KX_ProximityHit hit;
        hit.object = candidate.handle;
        hit.distance = candidate.distance;
        hit.shape_overlap = false;
        out.push_back(hit);
      }
      std::sort(out.begin(), out.end(), [](const KX_ProximityHit &a, const KX_ProximityHit &b) {
        return a.distance < b.distance;
      });
      return true;
    }

    for (const Candidate &candidate : candidates) {
      bool overlap = false;
      if (candidate.entry->has_collider) {
        if (query.shape == KX_PROXIMITY_BOX) {
          overlap = candidate.entry->radius > 0.0f ?
                        SphereBox(volume, Extent(volume), *candidate.entry) :
                        BoxBox(volume, *candidate.entry);
        }
        else {
          overlap = candidate.entry->radius > 0.0f ?
                        SphereSphere(origin, query.radius, *candidate.entry) :
                        SphereBox(origin, query.radius, *candidate.entry);
        }
      }

      KX_ProximityHit hit;
      hit.object = candidate.handle;
      hit.distance = candidate.distance;
      if (overlap) {
        hit.shape_overlap = true;
      }
      else if (!candidate.entry->has_collider) {
        if (candidate.distance > query.radius) {
          continue;
        }
      }
      else {
        continue;
      }
      out.push_back(hit);
    }

    std::sort(out.begin(), out.end(), [](const KX_ProximityHit &a, const KX_ProximityHit &b) {
      return a.distance < b.distance;
    });
    return false;
  }
};

class C6Objects : public KX_ObjectAdapter {
 public:
  ShapeWorld *world = nullptr;

  bool GetWorldPosition(KX_GameObject *object, float out[3]) override
  {
    auto found = world->entries.find(object);
    if (found == world->entries.end()) {
      out[0] = out[1] = out[2] = 0.0f;
      return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
      out[axis] = found->second.pos[axis];
    }
    return true;
  }

  bool GetWorldScale(KX_GameObject *object, float out[3]) override
  {
    auto found = world->entries.find(object);
    if (found == world->entries.end()) {
      out[0] = out[1] = out[2] = 1.0f;
      return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
      out[axis] = found->second.scale[axis];
    }
    return true;
  }
};

class C6Scene : public KX_SceneAdapter {
 public:
  ShapeWorld *world = nullptr;

  KX_GameObject *FindObject(const std::string &name) override
  {
    return name == world->find_name ? world->find_handle : nullptr;
  }

  bool QueryProximityShapes(const KX_ProximityShapeQuery &query,
                            std::vector<KX_ProximityHit> &out) override
  {
    return world->Query(query, out);
  }
};

/* ------------------------------------------------------------------------- */
/* Scenario runners                                                           */
/* ------------------------------------------------------------------------- */

std::vector<KX_GameObject *> RunRadar(ShapeWorld &world,
                                      KX_GameObject *owner,
                                      float radius,
                                      bool use_update = false)
{
  GraphIR ir;
  int trigger = AddNode(ir, use_update ? "OnUpdate" : "OnInit");
  char config[160];
  std::snprintf(config,
                sizeof(config),
                "{\"radius\": %.4f, \"angle\": 0.0}",
                double(radius));
  int radar = AddNode(ir, "RadarExtended", config);
  int sink = AddNode(ir, "TestListSink");
  Connect(ir, trigger, 0, radar, 0);
  Connect(ir, radar, 0, sink, 0);
  Connect(ir, radar, 1, sink, 1);

  KX_GraphInstance instance(nullptr, owner, nullptr);
  std::vector<std::string> warnings;
  const bool compiled = instance.Load(ir, &warnings);
  Check(compiled, "radar shape graph compiles", "1", compiled ? 1 : 0);
  if (!compiled) {
    return {};
  }

  C6Objects objects;
  C6Scene scene;
  objects.world = &world;
  scene.world = &world;
  instance.GetContext()->SetAdapters(
      nullptr, &scene, nullptr, &objects, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr);

  Capture capture;
  g_capture = &capture;
  instance.Tick(0.05f);
  if (use_update) {
    instance.Tick(0.05f);
  }
  g_capture = nullptr;
  return capture.objects;
}

std::vector<KX_GameObject *> RunArea(ShapeWorld &world, const char *area_name)
{
  GraphIR ir;
  int trigger = AddNode(ir, "OnInit");
  int area = AddNode(ir, "AreaSensor");
  int name = AddString(ir, area_name);
  int sink = AddNode(ir, "TestListSink");
  Connect(ir, trigger, 0, area, 0);
  Connect(ir, name, 0, area, 1);
  Connect(ir, area, 0, sink, 0);
  Connect(ir, area, 2, sink, 1);

  KX_GraphInstance instance(nullptr, Handle(0x900), nullptr);
  std::vector<std::string> warnings;
  const bool compiled = instance.Load(ir, &warnings);
  Check(compiled, "area shape graph compiles", "1", compiled ? 1 : 0);
  if (!compiled) {
    return {};
  }

  C6Objects objects;
  C6Scene scene;
  objects.world = &world;
  scene.world = &world;
  instance.GetContext()->SetAdapters(
      nullptr, &scene, nullptr, &objects, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr);

  Capture capture;
  g_capture = &capture;
  instance.Tick(0.05f);
  g_capture = nullptr;
  return capture.objects;
}

int CountLegacyAreaOccupants(ShapeWorld &world,
                             KX_GameObject *area,
                             KX_GameObject *candidate)
{
  const ShapeEntry &area_entry = world.entries[area];
  const ShapeEntry &candidate_entry = world.entries[candidate];
  const float half[3] = {std::max(std::fabs(area_entry.scale[0]) * 0.5f, 0.5f),
                         std::max(std::fabs(area_entry.scale[1]) * 0.5f, 0.5f),
                         std::max(std::fabs(area_entry.scale[2]) * 0.5f, 0.5f)};
  const float radius =
      std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]) * 1.05f;
  if (ShapeWorld::Distance(area_entry, candidate_entry) > radius) {
    return 0;
  }
  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(candidate_entry.pos[axis] - area_entry.pos[axis]) > half[axis] + 0.05f) {
      return 0;
    }
  }
  return 1;
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                      */
/* ------------------------------------------------------------------------- */

void TestRodShapes()
{
  std::printf("--- rod shape overlap (area sensor) ---\n");
  ShapeWorld world;
  KX_GameObject *area = Handle(0x1001);
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float half[3] = {2.0f, 0.1f, 0.1f};
  world.AddBox(area, origin, half, true);
  world.find_name = "zone";
  world.find_handle = area;

  /* Scenario 1: origins 3.0 apart, both rods are 4 long, so the shapes overlap
   * while the origin distance is far outside the scale-derived query radius. */
  const float overlapping_pos[3] = {3.0f, 0.0f, 0.0f};
  world.AddBox(Handle(0x1002), overlapping_pos, half, true);
  const std::vector<KX_GameObject *> overlapping = RunArea(world, "zone");
  Check(overlapping.size() == 1, "rods with overlapping shapes are detected", "1",
        int(overlapping.size()));
  Check(overlapping.size() == 1 && overlapping[0] == Handle(0x1002),
        "shape overlap reports the rod that intersects the area", "1",
        overlapping.size() == 1 && overlapping[0] == Handle(0x1002) ? 1 : 0);

  /* Scenario 2: origins only 0.5 apart, but the thin rods are offset in z and
   * do not intersect. The legacy origin test would report a false positive. */
  ShapeWorld offset_world;
  KX_GameObject *offset_area = Handle(0x1011);
  offset_world.AddBox(offset_area, origin, half, true);
  offset_world.find_name = "zone";
  offset_world.find_handle = offset_area;
  const float offset_pos[3] = {0.0f, 0.0f, 0.5f};
  KX_GameObject *offset_candidate = Handle(0x1012);
  offset_world.AddBox(offset_candidate, offset_pos, half, true);
  const std::vector<KX_GameObject *> separated = RunArea(offset_world, "zone");
  Check(separated.empty(), "thin rods with separated shapes are not reported", "0",
        int(separated.size()));
  Check(CountLegacyAreaOccupants(offset_world, offset_area, offset_candidate) == 1,
        "legacy origin rule would have reported the separated rods", "1",
        CountLegacyAreaOccupants(offset_world, offset_area, offset_candidate));
}

void TestSphereShapes()
{
  std::printf("--- sphere shape overlap (radar) ---\n");
  ShapeWorld world;
  KX_GameObject *radar_owner = Handle(0x2001);
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float half[3] = {0.0f, 0.0f, 0.0f};
  world.AddBox(radar_owner, origin, half, false);

  /* Scenario 3: query sphere 2.0, candidate sphere 1.0.
   * Origin distance 3.1 = 2.0 + 1.0 + 0.1, so the shapes do not touch. */
  const float far_pos[3] = {3.1f, 0.0f, 0.0f};
  KX_GameObject *far_sphere = Handle(0x2002);
  world.AddSphere(far_sphere, far_pos, 1.0f, true);
  const std::vector<KX_GameObject *> misses = RunRadar(world, radar_owner, 2.0f);
  Check(misses.empty(), "sphere pair beyond the radius sum is not reported", "0",
        int(misses.size()));

  /* And at 2.9 the shapes overlap, so the same query reports it. */
  world.entries[far_sphere].pos[0] = 2.9f;
  const std::vector<KX_GameObject *> hit = RunRadar(world, radar_owner, 2.0f);
  Check(hit.size() == 1 && hit[0] == far_sphere, "sphere pair inside the radius sum is reported",
        "1", hit.size() == 1 && hit[0] == far_sphere ? 1 : 0);
}

void TestNoColliderFallback()
{
  std::printf("--- no collider fallback ---\n");
  ShapeWorld world;
  KX_GameObject *radar_owner = Handle(0x3001);
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  world.AddBox(radar_owner, origin, zero, false);

  const float inside_pos[3] = {1.5f, 0.0f, 0.0f};
  KX_GameObject *empty_inside = Handle(0x3002);
  world.AddBox(empty_inside, inside_pos, zero, false);
  const std::vector<KX_GameObject *> inside = RunRadar(world, radar_owner, 2.0f);
  Check(inside.size() == 1 && inside[0] == empty_inside,
        "object without a collider falls back to origin distance", "1",
        inside.size() == 1 && inside[0] == empty_inside ? 1 : 0);

  const std::vector<KX_GameObject *> outside = [&]() {
    world.entries[empty_inside].pos[0] = 2.5f;
    return RunRadar(world, radar_owner, 2.0f);
  }();
  Check(outside.empty(), "no-collider fallback still respects the radius", "0",
        int(outside.size()));
}

int CaptureWarningCount(const std::function<void()> &work, const char *needle)
{
  const int saved = dup(fileno(stderr));
  FILE *file = std::fopen("tests/.c6_shape_warn.tmp", "w");
  if (file == nullptr) {
    close(saved);
    return -1;
  }
  dup2(fileno(file), fileno(stderr));
  work();
  std::fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);
  std::fclose(file);

  FILE *read_file = std::fopen("tests/.c6_shape_warn.tmp", "r");
  if (read_file == nullptr) {
    return -1;
  }
  char line[256];
  int count = 0;
  while (std::fgets(line, sizeof(line), read_file) != nullptr) {
    if (std::strstr(line, needle) != nullptr) {
      ++count;
    }
  }
  std::fclose(read_file);
  std::remove("tests/.c6_shape_warn.tmp");
  return count;
}

void TestBudgetFallback()
{
  std::printf("--- 64 object budget ---\n");
  ShapeWorld world;
  KX_GameObject *radar_owner = Handle(0x4001);
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  world.AddBox(radar_owner, origin, zero, false);
  for (int index = 0; index < 65; ++index) {
    const float pos[3] = {float(index + 1), 0.0f, 0.0f};
    world.AddBox(Handle(uintptr_t(0x4100 + index)), pos, zero, false);
  }

  std::vector<KX_GameObject *> result;
  const int warnings = CaptureWarningCount(
      [&]() { result = RunRadar(world, radar_owner, 100.0f, true); },
      "shape overlap query exceeded");
  Check(world.degraded_queries > 0, "budget overflow reports degradation", "1",
        world.degraded_queries > 0 ? 1 : 0);
  Check(result.size() == 65, "budget overflow uses the origin distance result", "65",
        int(result.size()));
  Check(warnings == 1, "budget overflow warns on stderr exactly once", "1", warnings);
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterFunctionGameNodes();
  RegisterB6StringFunctionGameNodes();
  RegisterB6SensorFunctionGameNodes();
  RegisterTestNodes();

  TestRodShapes();
  TestSphereShapes();
  TestNoColliderFallback();
  TestBudgetFallback();

  std::printf("\n%s assertions=%d failures=%d\n",
              failures == 0 ? "ALL PASS" : "FAILURES",
              assertions,
              failures);
  return failures == 0 ? 0 : 1;
}
