/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <string>

#ifdef WITH_OPENVDB
#  include <openvdb/openvdb.h>
#endif

#include "BLI_listbase_iterator.hh"
#include "BLI_listbase_wrapper.hh"
#include "BLI_map.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_string_ref.hh"
#include "BLI_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_collection.hh"
#include "BKE_geometry_set.hh"
#include "BKE_idprop.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_object.hh"
#include "BKE_particle.h"
#include "BKE_pointcloud.hh"
#include "BKE_volume.hh"
#include "BKE_volume_openvdb.hh"

#include "DEG_depsgraph_query.hh"

#include "NOD_shader.h"

#include "DNA_collection_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_pointcloud_types.h"
#include "DNA_scene_types.h"
#include "DNA_volume_types.h"

#include "bmesh.hh"
#include "bmesh_tools.hh"

#include "BKE_physnodes.hh"

namespace blender::bke::physnodes {

namespace {

/** Body kind, mirrors the `Type` menu of the C++ rigid body node. */
enum BodyKind : int {
  BODY_ACTIVE = 0,
  BODY_STATIC = 1,
  BODY_PASSIVE = 2,
};

struct BodyRecord {
  int kind = BODY_ACTIVE;
  std::string object_name;
  float inv_mass = 0.0f;
  float friction = 0.5f;
  float restitution = 0.0f;
  float density = 500.0f;
  int substeps = 4;
  bool from_collection = false;

  /* Motion state, preserved across world rebuilds by uid. A body only
   * integrates when it has a resolved object to write back to. */
  float3 initial_position = float3(0.0f);
  float3 position = float3(0.0f);
  float3 velocity = float3(0.0f);
  bool has_object = false;
};

struct RequestedOutputs {
  bool particle_mesh = false;
  bool particle_volume = false;
  bool volume_output = false;

  /* Mesh source and its extraction parameters. */
  std::string particle_object;
  float radius_scale = 1.0f;
  float voxel_size = 0.02f;
  float iso_value = 0.5f;
  /** Optional explicit receiver for the extracted mesh (Output node Object). */
  std::string particle_mesh_object;

  /* Volume source and its splatting parameters. Kept separate from the mesh
   * source: one tree may sample different objects for the two outputs. */
  std::string volume_particle_object;
  float volume_radius_scale = 1.0f;
  float volume_voxel_size = 0.02f;
  float density_scale = 1.0f;

  std::string volume_object;
};

/** Dense scalar field produced by Particle to Volume / smoke bridge. */
struct OutputDensity {
  bool valid = false;
  float3 origin = float3(0.0f);
  float3 voxel_size = float3(0.02f);
  int3 dims = int3(0);
  Vector<float> values;
};

/** Triangle mesh extracted from the dense field. */
struct OutputMesh {
  bool valid = false;
  Vector<float3> vertices;
  Vector<int3> faces;
};

struct Runtime {
  FrameState state;
  /** Bodies assembled on the last evaluation, keyed by uid. */
  Map<std::string, BodyRecord> bodies;
  /** Objects whose Physics Nodes modifier references this world, by name. */
  Map<std::string, float3> sink_objects;
  /** Output requests and produced payloads for the latest evaluation. */
  RequestedOutputs requested;
  OutputDensity density;
  OutputMesh mesh;
};

/* Body count of the most recent evaluation that actually found its node tree.
 * Diagnostics only: it lets a sink report the last real assembly even when the
 * tree name no longer resolves (for example after a typo or a deleted group),
 * without changing simulation or frame gating in any way. Guarded by the
 * runtime mutex. */
int g_last_assembled_body_count = 0;

struct KernelProbe {
  bool done = false;
  bool ok = false;
  std::string path;
  std::string error;
};

std::mutex &runtime_mutex()
{
  static std::mutex mutex;
  return mutex;
}

Map<std::string, Runtime> &runtimes()
{
  static Map<std::string, Runtime> map;
  return map;
}

KernelProbe &kernel_probe()
{
  static KernelProbe probe;
  return probe;
}

/* Frame ctimes are floats; compare with a small tolerance. */
bool ctime_equal(const float a, const float b)
{
  return std::fabs(a - b) < 1e-4f;
}

void advance_one_frame(Runtime &runtime, const float ctime, const float dt)
{
  FrameState &state = runtime.state;
  if (!state.initialized) {
    /* First evaluation of a scene only establishes the baseline frame. This
     * mirrors Blender's rigid body world, which does not simulate on the
     * start frame. */
    state.initialized = true;
    state.last_ctime = ctime;
    state.step_count = 0;
    return;
  }

  if (ctime < state.last_ctime && !ctime_equal(ctime, state.last_ctime)) {
    /* Went backwards in time: restart the world at the new frame. */
    state.last_ctime = ctime;
    state.step_count = 0;
    state.offset = float3(0.0f, 0.0f, 0.0f);
    for (BodyRecord &record : runtime.bodies.values()) {
      record.position = record.initial_position;
      record.velocity = float3(0.0f, 0.0f, 0.0f);
    }
    return;
  }

  if (ctime_equal(ctime, state.last_ctime)) {
    /* Same frame, evaluated again (redraw / parameter change / render). */
    return;
  }

  /* Only advance when the frame moved forward by exactly one frame. A jump
   * does not silently integrate several frames: the world is re-anchored at
   * the new frame, matching rigid body behavior. */
  if (ctime <= state.last_ctime + 1.0f + 1e-4f) {
    state.step_count++;
    state.last_ctime = ctime;
    state.offset = float3(0.0f, 0.0f, 0.0f);
    for (BodyRecord &record : runtime.bodies.values()) {
      if (record.kind != BODY_ACTIVE || !record.has_object) {
        continue;
      }
      /* Step 3 minimum: integrate the body origin with gravity so the sink
       * writes a real displacement. Contacts and constraint solving are still
       * ahead; no contact force is invented here. */
      record.velocity.z += -9.8f * dt;
      record.position.x += record.velocity.x * dt;
      record.position.y += record.velocity.y * dt;
      record.position.z += record.velocity.z * dt;
    }
  }
  else {
    state.last_ctime = ctime;
  }
}

/* --------------------------------------------------------------------------
 * Node socket readers.
 * ------------------------------------------------------------------------- */

const bNodeSocket *input_socket(const bNode &node, const char *identifier)
{
  for (const bNodeSocket *socket = node.inputs.first(); socket != nullptr;
       socket = socket->next)
  {
    /* Declared sockets use their name as identifier; accept the visible name
     * as well so renamed or auto-uniquified identifiers still resolve. */
    if (STREQ(socket->identifier, identifier) || STREQ(socket->name, identifier)) {
      return socket;
    }
  }
  return nullptr;
}

int menu_value(const bNode &node, const char *identifier, int fallback)
{
  const bNodeSocket *socket = input_socket(node, identifier);
  if (socket == nullptr || socket->type != SOCK_MENU) {
    return fallback;
  }
  const bNodeSocketValueMenu *value = static_cast<const bNodeSocketValueMenu *>(
      socket->default_value);
  return value != nullptr ? value->value : fallback;
}

float float_value(const bNode &node, const char *identifier, float fallback)
{
  const bNodeSocket *socket = input_socket(node, identifier);
  if (socket == nullptr || socket->type != SOCK_FLOAT) {
    return fallback;
  }
  const bNodeSocketValueFloat *value = static_cast<const bNodeSocketValueFloat *>(
      socket->default_value);
  return value != nullptr ? value->value : fallback;
}

int int_value(const bNode &node, const char *identifier, int fallback)
{
  const bNodeSocket *socket = input_socket(node, identifier);
  if (socket == nullptr || socket->type != SOCK_INT) {
    return fallback;
  }
  const bNodeSocketValueInt *value = static_cast<const bNodeSocketValueInt *>(
      socket->default_value);
  return value != nullptr ? value->value : fallback;
}

bool bool_value(const bNode &node, const char *identifier, bool fallback)
{
  const bNodeSocket *socket = input_socket(node, identifier);
  if (socket == nullptr || socket->type != SOCK_BOOLEAN) {
    return fallback;
  }
  const bNodeSocketValueBoolean *value = static_cast<const bNodeSocketValueBoolean *>(
      socket->default_value);
  return value != nullptr ? (value->value != 0) : fallback;
}

Object *object_value(const bNode &node, const char *identifier)
{
  const bNodeSocket *socket = input_socket(node, identifier);
  if (socket == nullptr || socket->type != SOCK_OBJECT) {
    return nullptr;
  }
  const bNodeSocketValueObject *value = static_cast<const bNodeSocketValueObject *>(
      socket->default_value);
  return value != nullptr ? value->value : nullptr;
}

Collection *collection_value(const bNode &node, const char *identifier)
{
  const bNodeSocket *socket = input_socket(node, identifier);
  if (socket == nullptr || socket->type != SOCK_COLLECTION) {
    return nullptr;
  }
  const bNodeSocketValueCollection *value = static_cast<const bNodeSocketValueCollection *>(
      socket->default_value);
  return value != nullptr ? value->value : nullptr;
}

/* --------------------------------------------------------------------------
 * World assembly.
 * ------------------------------------------------------------------------- */

bNodeTree *find_tree(Main *bmain, const std::string &name)
{
  for (bNodeTree &tree : bmain->nodetrees) {
    if (STREQ(tree.id.name + 2, name.c_str())) {
      return &tree;
    }
  }
  return nullptr;
}

void add_body(Runtime &runtime,
              Map<std::string, BodyRecord> &next,
              const std::string &uid,
              BodyRecord record)
{
  const BodyRecord *previous = runtime.bodies.lookup_ptr(uid);
  if (previous != nullptr && previous->kind == record.kind) {
    record.initial_position = previous->initial_position;
    record.position = previous->position;
    record.velocity = previous->velocity;
    record.has_object = previous->has_object;
  }
  next.add(uid, std::move(record));
}

/* Resolve bodies that have no explicit Object socket to the object the
 * Physics Nodes modifier is attached to, but only when that is unambiguous. */
std::string default_body_object(const Runtime &runtime)
{
  if (runtime.sink_objects.size() != 1) {
    return "";
  }
  for (const auto &item : runtime.sink_objects.items()) {
    return item.key;
  }
  return "";
}

void assemble_rigid_node(Depsgraph *depsgraph,
                         const bNode &node,
                         Runtime &runtime,
                         Map<std::string, BodyRecord> &next)
{
  FrameState &state = runtime.state;
  BodyRecord record;
  record.kind = menu_value(node, "Type", BODY_ACTIVE);
  record.from_collection = false;

  Object *object = object_value(node, "Object");
  record.object_name = object != nullptr ? std::string(object->id.name + 2) :
                                           default_body_object(runtime);

  switch (record.kind) {
    case BODY_STATIC: {
      record.inv_mass = 0.0f;
      record.friction = float_value(node, "Friction", 0.5f);
      record.restitution = float_value(node, "Restitution", 0.0f);
      break;
    }
    case BODY_PASSIVE: {
      record.inv_mass = 0.0f;
      record.friction = float_value(node, "Friction", 0.5f);
      record.restitution = float_value(node, "Restitution", 0.0f);

      /* Kinematic pose injection: resolve the driver by pointer from the
       * socket, then read its *evaluated* world matrix (the depsgraph
       * relation builder guarantees the transform is evaluated first). Falling
       * back to the body object itself matches the Python path, where a
       * passive body follows its own animation. */
      Object *driver = object_value(node, "Driver");
      if (driver == nullptr) {
        driver = object;
      }
      if (driver != nullptr) {
        const Object *driver_eval = DEG_get_evaluated<Object>(depsgraph, driver);
        if (driver_eval != nullptr) {
          state.passive_pose = driver_eval->object_to_world().location();
        }
      }
      state.passive_inv_mass = 0.0f;
      break;
    }
    case BODY_ACTIVE:
    default: {
      const float mass = float_value(node, "Mass", 1.0f);
      record.inv_mass = mass > 0.0f ? 1.0f / mass : 0.0f;
      record.substeps = int_value(node, "Substeps", 4);
      const bNodeSocket *velocity_socket = input_socket(node, "Velocity");
      if (velocity_socket != nullptr &&
          velocity_socket->type == SOCK_VECTOR &&
          velocity_socket->default_value != nullptr)
      {
        const bNodeSocketValueVector *value = static_cast<const bNodeSocketValueVector *>(
            velocity_socket->default_value);
        record.velocity = float3(value->value[0], value->value[1], value->value[2]);
      }
      break;
    }
  }

  add_body(runtime, next, node.name, std::move(record));
}

void collect_collection_objects(Collection *collection,
                                bool recursive,
                                Vector<Object *> &r_objects)
{
  if (collection == nullptr) {
    return;
  }
  for (CollectionObject *item = collection->gobject.first(); item != nullptr;
       item = item->next)
  {
    if (item->ob != nullptr) {
      r_objects.append(item->ob);
    }
  }
  if (recursive) {
    for (CollectionChild *child = collection->children.first(); child != nullptr;
         child = child->next)
    {
      collect_collection_objects(child->collection, true, r_objects);
    }
  }
}

void assemble_collection_node(Runtime &runtime,
                              const bNode &node,
                              Map<std::string, BodyRecord> &next)
{
  Collection *collection = collection_value(node, "Collection");
  const int kind = menu_value(node, "Type", BODY_ACTIVE);
  const bool recursive = bool_value(node, "Recursive", false);
  const float friction = float_value(node, "Friction", 0.5f);
  const float density = float_value(node, "Density", 500.0f);
  const int substeps = int_value(node, "Substeps", 4);

  Vector<Object *> objects;
  collect_collection_objects(collection, recursive, objects);

  for (Object *object : objects) {
    if (object->type != OB_MESH) {
      /* Non-mesh members are skipped with a readable diagnostic; they never
       * silently become bodies. */
      continue;
    }
    BodyRecord record;
    record.kind = kind;
    record.from_collection = true;
    record.object_name = object->id.name + 2;
    record.friction = friction;
    record.density = density;
    record.substeps = substeps;
    if (kind == BODY_ACTIVE) {
      /* Mass follows from the hull volume once the solver assembly exists
       * (step 3). Until then the record carries density only. */
      record.inv_mass = 0.0f;
    }
    else {
      record.inv_mass = 0.0f;
    }
    /* uid rule: `<collection node name>/<object name>`, never a pointer. */
    const std::string uid = std::string(node.name) + "/" + record.object_name;
    add_body(runtime, next, uid, std::move(record));
  }
}

/* Native `PhysColliderNodeCXX`: a static body for one object. The collision
 * shape is declared on the node; contact generation is still handled by the
 * step-3 solver work, so this only records the body. */
void assemble_collider_node(Runtime &runtime,
                            const bNode &node,
                            Map<std::string, BodyRecord> &next)
{
  Object *object = object_value(node, "Object");
  BodyRecord record;
  record.kind = BODY_STATIC;
  record.object_name = object != nullptr ? std::string(object->id.name + 2) :
                                           default_body_object(runtime);
  record.inv_mass = 0.0f;
  record.friction = float_value(node, "Friction", 0.5f);
  record.restitution = float_value(node, "Restitution", 0.0f);
  if (!record.object_name.empty()) {
    add_body(runtime, next, std::string(node.name), std::move(record));
  }
}

void assemble_world(Depsgraph *depsgraph, bNodeTree &tree, Runtime &runtime)
{
  FrameState &state = runtime.state;
  Map<std::string, BodyRecord> next_bodies;

  state.passive_inv_mass = 0.0f;
  /* Output requests are recomputed from the tree every evaluation so removing
   * a node clears its status instead of leaving a stale reason. */
  state.output = {};
  runtime.requested = RequestedOutputs{};

  for (bNode *node = tree.nodes.first(); node != nullptr; node = node->next) {
    if (STREQ(node->idname, "PhysRigidBodyNodeCXX")) {
      assemble_rigid_node(depsgraph, *node, runtime, next_bodies);
    }
    else if (STREQ(node->idname, "PhysCollectionInputNodeCXX")) {
      assemble_collection_node(runtime, *node, next_bodies);
    }
    else if (STREQ(node->idname, "PhysColliderNodeCXX")) {
      assemble_collider_node(runtime, *node, next_bodies);
    }
    else if (STREQ(node->idname, "PhysParticleToMeshNodeCXX")) {
      runtime.requested.particle_mesh = true;
      if (Object *source = object_value(*node, "Particle Source")) {
        runtime.requested.particle_object = source->id.name + 2;
      }
      runtime.requested.radius_scale = float_value(*node, "Radius Scale", 1.0f);
      runtime.requested.voxel_size = float_value(*node, "Voxel Size", 0.02f);
      runtime.requested.iso_value = float_value(*node, "Iso Value", 0.5f);
    }
    else if (STREQ(node->idname, "PhysParticleToVolumeNodeCXX")) {
      runtime.requested.particle_volume = true;
      if (Object *source = object_value(*node, "Particle Source")) {
        runtime.requested.volume_particle_object = source->id.name + 2;
      }
      runtime.requested.volume_radius_scale = float_value(*node, "Radius Scale", 1.0f);
      runtime.requested.volume_voxel_size = float_value(*node, "Voxel Size", 0.02f);
      runtime.requested.density_scale = float_value(*node, "Density Scale", 1.0f);
    }
    else if (STREQ(node->idname, "PhysOutputNodeCXX")) {
      /* Mode values come from node_phys_output.cc: POINTCLOUD=0, MESH=1. In
       * mesh mode the Object socket names an explicit receiver for the
       * particle-to-mesh result; empty keeps the automatic PHYSMesh_<tree>. */
      constexpr int mesh_mode = 1;
      if (menu_value(*node, "Mode", mesh_mode) == mesh_mode) {
        if (Object *target = object_value(*node, "Object")) {
          runtime.requested.particle_mesh_object = target->id.name + 2;
        }
      }
    }
    else if (STREQ(node->idname, "PhysVolumeOutputNodeCXX")) {
      runtime.requested.volume_output = true;
      if (Object *target = object_value(*node, "Volume Object")) {
        runtime.requested.volume_object = target->id.name + 2;
      }
    }
  }

  /* Diff by uid: survivors keep their identity across rebuilds. */
  int added = 0;
  int removed = 0;
  for (const auto &item : next_bodies.items()) {
    if (!runtime.bodies.contains(item.key)) {
      added++;
    }
  }
  for (const auto &item : runtime.bodies.items()) {
    if (!next_bodies.contains(item.key)) {
      removed++;
    }
  }
  runtime.bodies = std::move(next_bodies);

  state.body_count = int(runtime.bodies.size());
  g_last_assembled_body_count = state.body_count;
  state.active_count = 0;
  state.static_count = 0;
  state.passive_count = 0;
  for (const BodyRecord &record : runtime.bodies.values()) {
    switch (record.kind) {
      case BODY_STATIC:
        state.static_count++;
        break;
      case BODY_PASSIVE:
        state.passive_count++;
        break;
      case BODY_ACTIVE:
      default:
        state.active_count++;
        break;
    }
  }
  state.collection_added = added;
  state.collection_removed = removed;

  Vector<std::string> uids;
  uids.reserve(runtime.bodies.size());
  for (const auto &item : runtime.bodies.items()) {
    uids.append(item.key);
  }
  std::sort(uids.begin(), uids.end());
  std::string dump;
  for (const std::string &uid : uids) {
    if (!dump.empty()) {
      dump += "\n";
    }
    dump += uid;
  }
  state.uid_dump = std::move(dump);
}

void update_sink_objects(Main *bmain, const std::string &tree_name, Runtime &runtime)
{
  Map<std::string, float3> next;
  if (bmain != nullptr) {
    for (Object &object : bmain->objects) {
      bool references_tree = false;
      for (ModifierData &md : object.modifiers.items_mutable()) {
        if (md.type != eModifierType_PhysNodes) {
          continue;
        }
        const PhysNodesModifierData &pmd = reinterpret_cast<const PhysNodesModifierData &>(md);
        if (std::strcmp(pmd.tree_name, tree_name.c_str()) == 0) {
          references_tree = true;
          break;
        }
      }
      if (!references_tree) {
        continue;
      }
      const std::string object_name = object.id.name + 2;
      const float3 *previous = runtime.sink_objects.lookup_ptr(object_name);
      next.add(object_name,
               previous != nullptr ? *previous : object.object_to_world().location());
    }
  }
  runtime.sink_objects = std::move(next);
}

/* Give each active body an initial position once, taken from the sink object
 * it writes back to. Later rebuilds keep the carried motion state. */
void bind_body_objects(Runtime &runtime)
{
  for (BodyRecord &record : runtime.bodies.values()) {
    if (record.kind != BODY_ACTIVE) {
      continue;
    }
    if (record.has_object) {
      continue;
    }
    if (record.object_name.empty()) {
      continue;
    }
    const float3 *location = runtime.sink_objects.lookup_ptr(record.object_name);
    if (location == nullptr) {
      record.object_name.clear();
      continue;
    }
    record.initial_position = *location;
    record.position = *location;
    record.has_object = true;
  }
}

void update_state_offset(Runtime &runtime)
{
  runtime.state.offset = float3(0.0f, 0.0f, 0.0f);
  for (const BodyRecord &record : runtime.bodies.values()) {
    if (record.kind != BODY_ACTIVE || !record.has_object) {
      continue;
    }
    runtime.state.offset = float3(record.position.x - record.initial_position.x,
                                  record.position.y - record.initial_position.y,
                                  record.position.z - record.initial_position.z);
    return;
  }
}

/* --------------------------------------------------------------------------
 * Output data chain: particles -> dense field -> mesh / OpenVDB volume.
 * ------------------------------------------------------------------------- */

/* B17.3's cube corner order and the six-tetrahedron decomposition. */
static constexpr int CUBE_CORNERS[8][3] = {{0, 0, 0},
                                           {1, 0, 0},
                                           {1, 1, 0},
                                           {0, 1, 0},
                                           {0, 0, 1},
                                           {1, 0, 1},
                                           {1, 1, 1},
                                           {0, 1, 1}};
static constexpr int TETRAHEDRA[6][4] = {
    {0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
static constexpr int TET_EDGES[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};

static bool collect_particles(Depsgraph *depsgraph,
                              const Object &source,
                              float radius_scale,
                              ParticleSet &r_particles)
{
  const Object *ob_eval = DEG_get_evaluated<Object>(depsgraph, &source);
  if (ob_eval == nullptr) {
    return false;
  }
  const float4x4 obmat = ob_eval->object_to_world();

  auto append_particle = [&](const float3 &local, const float radius) {
    const float3 world = math::transform_point(obmat, local);
    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z)) {
      return;
    }
    r_particles.positions.push_back(world);
    r_particles.radii.push_back(std::max(radius * radius_scale, 1.0e-5f));
  };

  if (ob_eval->type == OB_POINTCLOUD) {
    const PointCloud *pointcloud = reinterpret_cast<const PointCloud *>(ob_eval->data);
    if (pointcloud == nullptr) {
      return false;
    }
    const Span<float3> positions = pointcloud->positions();
    VArray<float> radii;
    const AttributeAccessor attributes = pointcloud->attributes();
    const AttributeReader<float> radius_reader = attributes.lookup<float>("radius");
    if (radius_reader) {
      radii = radius_reader.varray;
    }
    for (const int i : positions.index_range()) {
      const float radius = radii ? radii[i] : 0.01f;
      append_particle(positions[i], radius);
    }
  }
  else if (ob_eval->type == OB_MESH) {
    const Mesh *mesh = reinterpret_cast<const Mesh *>(ob_eval->data);
    if (mesh == nullptr) {
      return false;
    }
    const ParticleSystem *psys = ob_eval->particlesystem.first();
    if (psys != nullptr && psys->particles != nullptr && psys->totpart > 0) {
      for (int i = 0; i < psys->totpart; i++) {
        const ParticleData *particle = &psys->particles[i];
        append_particle(float3(particle->state.co[0], particle->state.co[1],
                               particle->state.co[2]),
                        particle->size > 0.0f ? particle->size : 0.01f);
      }
    }
    else {
      const Span<float3> positions = mesh->vert_positions();
      for (const float3 &position : positions) {
        append_particle(position, 0.01f);
      }
    }
  }
  return r_particles.valid();
}

static void splat_density(const ParticleSet &particles,
                          float voxel_size,
                          float radius_scale,
                          float density_scale,
                          OutputDensity &r_density)
{
  if (!particles.valid()) {
    return;
  }
  const float voxel = std::max(voxel_size, 1.0e-4f);
  float3 min_corner = float3(std::numeric_limits<float>::max());
  float3 max_corner = float3(std::numeric_limits<float>::lowest());
  for (int i = 0; i < particles.positions.size(); i++) {
    const float radius = std::max(particles.radii[i] * radius_scale, voxel);
    const float3 &position = particles.positions[i];
    min_corner = math::min(min_corner, position - float3(radius));
    max_corner = math::max(max_corner, position + float3(radius));
  }
  const float3 extent = math::max(max_corner - min_corner, float3(voxel * 4.0f));
  const int3 dims = int3(
      std::clamp(int(std::ceil(extent.x / voxel)) + 1, 4, 128),
      std::clamp(int(std::ceil(extent.y / voxel)) + 1, 4, 128),
      std::clamp(int(std::ceil(extent.z / voxel)) + 1, 4, 128));

  r_density.valid = true;
  r_density.origin = min_corner - float3(voxel);
  r_density.voxel_size = float3(voxel);
  r_density.dims = dims;
  r_density.values.clear();
  r_density.values.resize(size_t(dims.x) * size_t(dims.y) * size_t(dims.z), 0.0f);

  auto add_trilinear = [&](const float3 &position, float weight) {
    const float3 local = (position - r_density.origin) / voxel - float3(0.5f);
    const int3 base = int3(int(std::floor(local.x)),
                           int(std::floor(local.y)),
                           int(std::floor(local.z)));
    const float3 frac = local - float3(base);
    for (int dz = 0; dz <= 1; dz++) {
      for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
          const float w = (dx ? frac.x : 1.0f - frac.x) *
                          (dy ? frac.y : 1.0f - frac.y) *
                          (dz ? frac.z : 1.0f - frac.z);
          const int3 coord = base + int3(dx, dy, dz);
          if (coord.x < 0 || coord.y < 0 || coord.z < 0 || coord.x >= dims.x ||
              coord.y >= dims.y || coord.z >= dims.z)
          {
            continue;
          }
          r_density.values[size_t(coord.z) * dims.x * dims.y + size_t(coord.y) * dims.x +
                           coord.x] += w * weight;
        }
      }
    }
  };

  for (int i = 0; i < particles.positions.size(); i++) {
    add_trilinear(particles.positions[i], 1.0f);
  }

  /* Normalize the peak so the 0..1 iso value has a stable meaning. This runs
   * after the smoothing pass below: normalizing first would let the blur
   * attenuate an isolated splat well below the iso level. */
  float peak = 0.0f;
  for (const float value : r_density.values) {
    if (std::isfinite(value)) {
      peak = std::max(peak, value);
    }
  }
  if (peak <= 0.0f) {
    r_density.valid = false;
    return;
  }

  /* One separable [1,2,1] smoothing pass widens the particle splats. */
  Vector<float> tmp(r_density.values.size());
  const int sx = dims.x, sy = dims.y, sz = dims.z;
  auto at = [&](const int x, const int y, const int z) -> size_t {
    return size_t(z) * sx * sy + size_t(y) * sx + x;
  };
  for (int z = 0; z < sz; z++) {
    for (int y = 0; y < sy; y++) {
      for (int x = 0; x < sx; x++) {
        const float a = r_density.values[at(std::max(x - 1, 0), y, z)];
        const float b = r_density.values[at(x, y, z)];
        const float c = r_density.values[at(std::min(x + 1, sx - 1), y, z)];
        tmp[at(x, y, z)] = (a + 2.0f * b + c) * 0.25f;
      }
    }
  }
  for (int z = 0; z < sz; z++) {
    for (int y = 0; y < sy; y++) {
      for (int x = 0; x < sx; x++) {
        const float a = tmp[at(x, std::max(y - 1, 0), z)];
        const float b = tmp[at(x, y, z)];
        const float c = tmp[at(x, std::min(y + 1, sy - 1), z)];
        r_density.values[at(x, y, z)] = (a + 2.0f * b + c) * 0.25f;
      }
    }
  }
  for (int z = 0; z < sz; z++) {
    for (int y = 0; y < sy; y++) {
      for (int x = 0; x < sx; x++) {
        const float a = tmp[at(x, y, std::max(z - 1, 0))];
        const float b = tmp[at(x, y, z)];
        const float c = tmp[at(x, y, std::min(z + 1, sz - 1))];
        r_density.values[at(x, y, z)] = (a + 2.0f * b + c) * 0.25f;
      }
    }
  }

  /* Normalize after smoothing so the peak keeps the requested density scale. */
  float smoothed_peak = 0.0f;
  for (const float value : r_density.values) {
    if (std::isfinite(value)) {
      smoothed_peak = std::max(smoothed_peak, value);
    }
  }
  const float scale = (smoothed_peak > 0.0f) ? std::max(density_scale, 0.0f) / smoothed_peak :
                                               0.0f;
  for (float &value : r_density.values) {
    value = std::isfinite(value) ? value * scale : 0.0f;
  }
}

/** Callback used by the extractor to read the padded phi field. */
struct PhiField {
  Vector<float> values;
  int sx = 0, sy = 0, sz = 0;
  float at(const int x, const int y, const int z) const
  {
    return values[size_t(z) * sx * sy + size_t(y) * sx + x];
  }
};

static void extract_mesh(const OutputDensity &density, float iso_value, OutputMesh &r_mesh)
{
  if (!density.valid || density.dims.x < 2 || density.dims.y < 2 || density.dims.z < 2) {
    return;
  }
  const int nx = density.dims.x, ny = density.dims.y, nz = density.dims.z;
  /** One voxel air border keeps the surface closed at the field boundary. */
  PhiField phi;
  phi.sx = nx + 2;
  phi.sy = ny + 2;
  phi.sz = nz + 2;
  phi.values.resize(size_t(phi.sx) * phi.sy * phi.sz, iso_value);
  for (int z = 0; z < nz; z++) {
    for (int y = 0; y < ny; y++) {
      for (int x = 0; x < nx; x++) {
        const float value = density.values[size_t(z) * nx * ny + size_t(y) * nx + x];
        float signed_distance = iso_value - value;
        if (signed_distance == 0.0f) {
          signed_distance = -1.0e-12f;
        }
        phi.values[size_t(z + 1) * phi.sx * phi.sy + size_t(y + 1) * phi.sx + (x + 1)] =
            signed_distance;
      }
    }
  }

  const int64_t cell_nodes =
      int64_t(phi.sx) * phi.sy * phi.sz; /* weld key base, fits in uint64 for 130^3 */
  Map<uint64_t, int> edge_vertices;
  r_mesh.vertices.clear();
  r_mesh.faces.clear();

  auto node_index = [&](const int x, const int y, const int z) -> int64_t {
    return int64_t(z) * phi.sx * phi.sy + int64_t(y) * phi.sx + x;
  };
  auto node_world = [&](const int x, const int y, const int z) -> float3 {
    return density.origin +
           float3(float(x - 1), float(y - 1), float(z - 1)) * density.voxel_size;
  };
  auto edge_vertex = [&](const int64_t a, const int64_t b) -> int {
    const int64_t lo = std::min(a, b);
    const int64_t hi = std::max(a, b);
    const uint64_t key = uint64_t(lo) * uint64_t(cell_nodes) + uint64_t(hi);
    if (const int *found = edge_vertices.lookup_ptr(key)) {
      return *found;
    }
    const int64_t lo_x = lo % phi.sx;
    const int64_t lo_rest = lo / phi.sx;
    const int64_t lo_y = lo_rest % phi.sy;
    const int64_t lo_z = lo_rest / phi.sy;
    const int64_t hi_x = hi % phi.sx;
    const int64_t hi_rest = hi / phi.sx;
    const int64_t hi_y = hi_rest % phi.sy;
    const int64_t hi_z = hi_rest / phi.sy;
    const float va = phi.at(int(lo_x), int(lo_y), int(lo_z));
    const float vb = phi.at(int(hi_x), int(hi_y), int(hi_z));
    const float denom = va - vb;
    const float t = std::fabs(denom) > 1.0e-20f ? va / denom : 0.5f;
    const float3 pa = node_world(int(lo_x), int(lo_y), int(lo_z));
    const float3 pb = node_world(int(hi_x), int(hi_y), int(hi_z));
    const float3 p = pa + (pb - pa) * t;
    const int index = r_mesh.vertices.size();
    r_mesh.vertices.append(p);
    edge_vertices.add(key, index);
    return index;
  };

  for (int z = 0; z + 1 < phi.sz; z++) {
    for (int y = 0; y + 1 < phi.sy; y++) {
      for (int x = 0; x + 1 < phi.sx; x++) {
        /* Gather the eight cube corners in B17.3 order. */
        int64_t corners[8];
        float values[8];
        float min_value = std::numeric_limits<float>::max();
        float max_value = std::numeric_limits<float>::lowest();
        for (int c = 0; c < 8; c++) {
          corners[c] = node_index(x + CUBE_CORNERS[c][0],
                                  y + CUBE_CORNERS[c][1],
                                  z + CUBE_CORNERS[c][2]);
          values[c] = phi.values[corners[c]];
          min_value = std::min(min_value, values[c]);
          max_value = std::max(max_value, values[c]);
        }
        if (!(min_value < 0.0f && max_value > 0.0f)) {
          continue;
        }
        for (const auto &tet : TETRAHEDRA) {
          const int64_t tet_nodes[4] = {corners[tet[0]], corners[tet[1]], corners[tet[2]],
                                        corners[tet[3]]};
          const float tet_phi[4] = {values[tet[0]], values[tet[1]], values[tet[2]],
                                     values[tet[3]]};
          int mask = 0;
          for (int i = 0; i < 4; i++) {
            if (tet_phi[i] < 0.0f) {
              mask |= (1 << i);
            }
          }
          if (mask == 0 || mask == 15) {
            continue;
          }
          int crossing_edges[6];
          int crossing_count = 0;
          for (const auto &edge : TET_EDGES) {
            const bool a_neg = (mask >> edge[0]) & 1;
            const bool b_neg = (mask >> edge[1]) & 1;
            if (a_neg != b_neg) {
              crossing_edges[crossing_count++] = edge_vertex(tet_nodes[edge[0]],
                                                             tet_nodes[edge[1]]);
            }
          }
          if (crossing_count < 3) {
            continue;
          }
          /* For two negative vertices order the quad by alternating edge
           * polarity: neg0-pos0, neg1-pos0, neg1-pos1, neg0-pos1. */
          int ordered[4] = {crossing_edges[0], crossing_edges[1], crossing_edges[2],
                            crossing_count > 3 ? crossing_edges[3] : crossing_edges[2]};
          if (crossing_count == 4) {
            const int neg_indices[2] = {(mask & 1) ? 0 : 1, (mask & 2) ? 1 : 2};
            int neg0 = -1, neg1 = -1;
            for (int i = 0; i < 4; i++) {
              if (mask & (1 << i)) {
                if (neg0 < 0) {
                  neg0 = i;
                }
                else {
                  neg1 = i;
                }
              }
            }
            int pos0 = -1, pos1 = -1;
            for (int i = 0; i < 4; i++) {
              if (!(mask & (1 << i))) {
                if (pos0 < 0) {
                  pos0 = i;
                }
                else {
                  pos1 = i;
                }
              }
            }
            (void)neg_indices;
            ordered[0] = edge_vertex(tet_nodes[neg0], tet_nodes[pos0]);
            ordered[1] = edge_vertex(tet_nodes[neg1], tet_nodes[pos0]);
            ordered[2] = edge_vertex(tet_nodes[neg1], tet_nodes[pos1]);
            ordered[3] = edge_vertex(tet_nodes[neg0], tet_nodes[pos1]);
          }
          auto emit = [&](int a, int b, int c) {
            if (a == b || b == c || a == c) {
              return;
            }
            r_mesh.faces.append(int3(a, b, c));
          };
          if (crossing_count == 3) {
            emit(ordered[0], ordered[1], ordered[2]);
          }
          else {
            emit(ordered[0], ordered[1], ordered[2]);
            emit(ordered[0], ordered[2], ordered[3]);
          }
        }
      }
    }
  }
  r_mesh.valid = !r_mesh.faces.is_empty();
}

/** Read a numeric IDProperty as float, accepting the types Python can write. */
static bool read_number(const IDProperty *group, const StringRef name, float &r_value)
{
  const IDProperty *prop = IDP_GetPropertyFromGroup_null(group, name);
  if (prop == nullptr) {
    return false;
  }
  if (prop->type == IDP_FLOAT) {
    r_value = IDP_float_get(prop);
    return true;
  }
  if (prop->type == IDP_DOUBLE) {
    r_value = float(IDP_double_get(prop));
    return true;
  }
  if (prop->type == IDP_INT) {
    r_value = float(IDP_int_get(prop));
    return true;
  }
  return false;
}

/**
 * Read the smoke density grid published by the Python B18 solver into the
 * node tree's IDProperties (`physnodes_density`). This is the C++ side of the
 * smoke -> volume bridge: the sink consumes the real solver output and never
 * substitutes an empty placeholder grid.
 */
static void read_smoke_density(const bNodeTree &tree, OutputDensity &r_density)
{
  if (r_density.valid) {
    return;
  }
  const IDProperty *group = IDP_GetPropertyFromGroup_null(tree.id.properties,
                                                          "physnodes_density");
  if (group == nullptr || group->type != IDP_GROUP) {
    return;
  }
  const IDProperty *nx = IDP_GetPropertyFromGroup_null(group, "nx");
  const IDProperty *ny = IDP_GetPropertyFromGroup_null(group, "ny");
  const IDProperty *nz = IDP_GetPropertyFromGroup_null(group, "nz");
  const IDProperty *values = IDP_GetPropertyFromGroup_null(group, "values");
  if (nx == nullptr || ny == nullptr || nz == nullptr || values == nullptr ||
      nx->type != IDP_INT || ny->type != IDP_INT || nz->type != IDP_INT ||
      values->type != IDP_ARRAY)
  {
    return;
  }
  const int3 dims(IDP_int_get(nx), IDP_int_get(ny), IDP_int_get(nz));
  if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0 ||
      values->len != dims.x * dims.y * dims.z)
  {
    return;
  }
  if (values->subtype != IDP_FLOAT && values->subtype != IDP_DOUBLE) {
    return;
  }
  float3 origin(0.0f);
  float3 voxel(1.0f);
  read_number(group, "ox", origin.x);
  read_number(group, "oy", origin.y);
  read_number(group, "oz", origin.z);
  read_number(group, "hx", voxel.x);
  read_number(group, "hy", voxel.y);
  read_number(group, "hz", voxel.z);

  OutputDensity density;
  density.valid = true;
  density.dims = dims;
  density.origin = origin;
  density.voxel_size = math::max(voxel, float3(1.0e-5f));
  density.values.clear();
  density.values.reserve(values->len);
  const bool is_float = values->subtype == IDP_FLOAT;
  const float *float_data = is_float ? IDP_array_float_get(values) : nullptr;
  const double *double_data = is_float ? nullptr : IDP_array_double_get(values);
  for (int i = 0; i < values->len; i++) {
    const float value = is_float ? float_data[i] : float(double_data[i]);
    density.values.append(std::isfinite(value) ? value : 0.0f);
  }
  r_density = std::move(density);
}

static void process_outputs(Depsgraph *depsgraph,
                            Main *bmain,
                            const bNodeTree &tree,
                            Runtime &runtime)
{
  FrameState &state = runtime.state;
  state.particle_count = 0;
  state.density_voxel_count = 0;
  state.particles = ParticleSet{};
  state.density = DensityField{};
  runtime.density = OutputDensity{};
  runtime.mesh = OutputMesh{};
  state.output = {};

  const RequestedOutputs &request = runtime.requested;

  auto collect_source = [&](const std::string &name, float radius_scale, ParticleSet &r_out) {
    if (name.empty() || bmain == nullptr) {
      return false;
    }
    for (Object &object : bmain->objects) {
      if (STREQ(object.id.name + 2, name.c_str())) {
        return collect_particles(depsgraph, object, radius_scale, r_out);
      }
    }
    return false;
  };

  auto publish_density = [&]() {
    state.density.origin = runtime.density.origin;
    state.density.voxel_size = runtime.density.voxel_size;
    state.density.resolution = runtime.density.dims;
    state.density.values.resize(runtime.density.values.size());
    for (int i = 0; i < runtime.density.values.size(); i++) {
      state.density.values[i] = runtime.density.values[i];
    }
    state.density_voxel_count = runtime.density.values.size();
  };

  /* The volume field is owned by the Particle to Volume source only, which may
   * differ from the Particle to Mesh source in the same tree. */
  ParticleSet volume_particles;
  const bool volume_source_ready =
      request.particle_volume &&
      collect_source(request.volume_particle_object, request.volume_radius_scale, volume_particles);

  if (request.particle_volume) {
    if (volume_source_ready && volume_particles.valid()) {
      splat_density(volume_particles,
                    request.volume_voxel_size,
                    request.volume_radius_scale,
                    request.density_scale,
                    runtime.density);
    }
    if (runtime.density.valid) {
      publish_density();
      state.output.particle_volume = "";
    }
    else {
      state.output.particle_volume = output_reasons::particle_volume;
    }
  }

  read_smoke_density(tree, runtime.density);
  if (runtime.density.valid && state.density_voxel_count == 0) {
    publish_density();
  }

  if (!request.particle_mesh) {
    /* Expose the volume source particles for diagnostics when no mesh source
     * took ownership of the frame state. */
    state.particles = volume_particles;
    state.particle_count = volume_particles.positions.size();
  }

  if (request.particle_mesh) {
    ParticleSet particles;
    const bool ready = collect_source(request.particle_object, request.radius_scale, particles);
    if (ready && particles.valid()) {
      state.particles = particles;
      state.particle_count = particles.positions.size();
      /* The mesh only needs relative density for the iso threshold, so the
       * volume's amplitude does not leak into the surface extraction. */
      OutputDensity mesh_density;
      splat_density(particles, request.voxel_size, request.radius_scale, 1.0f, mesh_density);
      if (mesh_density.valid) {
        extract_mesh(mesh_density, request.iso_value, runtime.mesh);
      }
      state.output.particle_mesh = runtime.mesh.valid ? "" : output_reasons::particle_mesh;
    }
    else {
      state.output.particle_mesh = output_reasons::particle_mesh;
    }
  }

  if (request.volume_output) {
    state.output.volume_output = runtime.density.valid ? "" : output_reasons::volume_output;
  }
}

static Object *find_object_by_name(Main *bmain, const std::string &name)
{
  for (Object &object : bmain->objects) {
    if (STREQ(object.id.name + 2, name.c_str())) {
      return &object;
    }
  }
  return nullptr;
}

static Object *ensure_scene_object(Main *bmain, const char *name, ObjectType type)
{
  Object *object = find_object_by_name(bmain, name);
  if (object != nullptr && object->type == type) {
    return object;
  }
  object = BKE_object_add_only_object(bmain, type, name);
  Scene *scene = bmain->scenes.first();
  if (scene != nullptr && !BKE_collection_has_object(scene->master_collection, object)) {
    BKE_collection_object_add(bmain, scene->master_collection, object);
  }
  return object;
}

static Mesh *find_mesh_by_name(Main *bmain, const std::string &name)
{
  for (Mesh &mesh : bmain->meshes) {
    if (STREQ(mesh.id.name + 2, name.c_str())) {
      return &mesh;
    }
  }
  return nullptr;
}

static Volume *find_volume_by_name(Main *bmain, const std::string &name)
{
  for (Volume &volume : bmain->volumes) {
    if (STREQ(volume.id.name + 2, name.c_str())) {
      return &volume;
    }
  }
  return nullptr;
}

/** Write the extracted surface into the dedicated sink mesh. */
static void write_mesh_output(Main *bmain,
                              const std::string &name,
                              Object *object,
                              const OutputMesh &output)
{
  Mesh *mesh = (object->data != nullptr) ?
                   reinterpret_cast<Mesh *>(object->data) :
                   find_mesh_by_name(bmain, name);
  if (mesh == nullptr) {
    mesh = BKE_mesh_add(bmain, name.c_str());
  }

  BMeshCreateParams create_params{};
  BMesh *bm = BM_mesh_create(&bm_mesh_allocsize_default, &create_params);
  Vector<BMVert *> bm_verts;
  bm_verts.reserve(output.vertices.size());
  for (const float3 &co : output.vertices) {
    bm_verts.append(BM_vert_create(bm, co, nullptr, BM_CREATE_NOP));
  }
  for (const int3 &face : output.faces) {
    BMVert *tri[3] = {bm_verts[face.x], bm_verts[face.y], bm_verts[face.z]};
    BM_face_create_verts(bm, tri, 3, nullptr, BM_CREATE_NOP, true);
  }
  BMeshToMeshParams mesh_params{};
  mesh_params.calc_object_remap = false;
  BM_mesh_bm_to_me(bmain, bm, mesh, &mesh_params);
  BM_mesh_free(bm);

  object->data = &mesh->id;
  DEG_id_type_tag(bmain, ID_ME);
}

/** Volume material whose density is driven by the grid, so Cycles/EEVEE actually
 * request the "density" attribute. A constant Principled Volume density would
 * make the render engines skip the grid entirely. */
static Material *ensure_volume_material(Main *bmain)
{
  for (Material &candidate : bmain->materials) {
    if (STREQ(candidate.id.name + 2, "PHYS Volume")) {
      return &candidate;
    }
  }
  Material *material = BKE_material_add(bmain, "PHYS Volume");
  bNodeTree *ntree = material->nodetree;
  if (ntree == nullptr || ntree->nodes.first() != nullptr) {
    return material;
  }

  bNode *info = bke::node_add_static_node(nullptr, *ntree, SH_NODE_VOLUME_INFO);
  bNode *principled = bke::node_add_static_node(nullptr, *ntree, SH_NODE_VOLUME_PRINCIPLED);
  bNode *output = bke::node_add_static_node(nullptr, *ntree, SH_NODE_OUTPUT_MATERIAL);
  bke::node_add_link(*ntree,
                     *info,
                     *bke::node_find_socket(*info, SOCK_OUT, "Density"_ustr),
                     *principled,
                     *bke::node_find_socket(*principled, SOCK_IN, "Density"_ustr));
  /* Leave the scattering color constant. The density grid carries no color
   * grid, so wiring Volume Info's Color output would feed black and turn the
   * fog into a pure absorber that is invisible against a dark background. */
  bNodeSocket *color = bke::node_find_socket(*principled, SOCK_IN, "Color"_ustr);
  if (color != nullptr && color->default_value != nullptr) {
    static_cast<bNodeSocketValueRGBA *>(color->default_value)->value[0] = 0.8f;
    static_cast<bNodeSocketValueRGBA *>(color->default_value)->value[1] = 0.8f;
    static_cast<bNodeSocketValueRGBA *>(color->default_value)->value[2] = 0.8f;
    static_cast<bNodeSocketValueRGBA *>(color->default_value)->value[3] = 1.0f;
  }
  bke::node_add_link(*ntree,
                     *principled,
                     *bke::node_find_socket(*principled, SOCK_OUT, "Volume"_ustr),
                     *output,
                     *bke::node_find_socket(*output, SOCK_IN, "Volume"_ustr));
  /* Render engines pick the material output by this flag. Statically added
   * nodes do not get it automatically, and without it the shader has no
   * output, so the volume renders as if it were empty. */
  for (bNode *other = ntree->nodes.first(); other != nullptr; other = other->next)
  {
    if (other->type_legacy == output->type_legacy) {
      other->flag &= ~NODE_DO_OUTPUT;
    }
  }
  output->flag |= NODE_DO_OUTPUT;
  bke::node_set_active(*ntree, *output);
  BKE_ntree_update_tag_active_output_changed(ntree);
  BKE_ntree_update_without_main(*ntree);
  return material;
}

/** Write the dense field into the object's Volume datablock as a VDB grid.
 *
 * \param allow_create: create a Volume datablock when the object has none. This
 *   is only valid for datablocks of the non-evaluated main. Evaluated objects
 *   must write into the copy-on-eval Volume that already exists.
 */
static bool write_volume_output(Main *bmain,
                                Object *object,
                                const OutputDensity &density,
                                const bool allow_create)
{
  Volume *volume = reinterpret_cast<Volume *>(object->data);
  if (volume == nullptr) {
    if (!allow_create) {
      return false;
    }
    volume = find_volume_by_name(bmain, object->id.name + 2);
    if (volume == nullptr) {
      volume = BKE_volume_add(bmain, object->id.name + 2);
    }
  }
  while (BKE_volume_num_grids(volume) > 0) {
    BKE_volume_grid_remove(volume, BKE_volume_grid_get(volume, 0));
  }

  const int nx = density.dims.x, ny = density.dims.y, nz = density.dims.z;
  /* Hard NaN check: a non-finite voxel would poison the grid and the render,
   * so reject the whole field instead of publishing partial data. */
  size_t nonfinite = 0;
  for (const float value : density.values) {
    if (!std::isfinite(value)) {
      nonfinite++;
    }
  }
  if (nonfinite > 0) {
    fprintf(stderr, "[phys] volume rejected: %zu non-finite voxels\n", nonfinite);
    return false;
  }
  openvdb::FloatGrid::Ptr grid = openvdb::FloatGrid::create(0.0f);
  grid->setGridClass(openvdb::GRID_FOG_VOLUME);

  /* OpenVDB matrices are indexed (column, row); translation lives in the
   * fourth column. */
  openvdb::math::Mat4d transform = openvdb::math::Mat4d::identity();
  transform(0, 0) = double(density.voxel_size.x);
  transform(1, 1) = double(density.voxel_size.y);
  transform(2, 2) = double(density.voxel_size.z);
  transform(3, 0) = double(density.origin.x);
  transform(3, 1) = double(density.origin.y);
  transform(3, 2) = double(density.origin.z);
  grid->setTransform(std::make_shared<openvdb::math::Transform>(
      std::make_shared<openvdb::math::AffineMap>(transform)));
  grid->setName("density");

  auto accessor = grid->getAccessor();
  for (int z = 0; z < nz; z++) {
    for (int y = 0; y < ny; y++) {
      for (int x = 0; x < nx; x++) {
        const float value =
            density.values[size_t(z) * size_t(nx) * size_t(ny) + size_t(y) * size_t(nx) + x];
        if (!std::isfinite(value) || value <= 0.0f) {
          continue;
        }
        accessor.setValue(openvdb::Coord(x, y, z), value);
      }
    }
  }
  BKE_volume_grid_add_vdb(*volume, "density", grid);

  object->data = &volume->id;
  if (volume->totcol == 0) {
    /* Keep a user-assigned material; only fill in the default fog shader. */
    BKE_object_material_assign_single_obdata(bmain, object, ensure_volume_material(bmain), 0);
  }
  DEG_id_type_tag(bmain, ID_VO);
  return true;
}

/** True when the object carries a Physics Nodes sink for this same world. */
static bool object_hosts_sink(const Object &object, const std::string &tree_name)
{
  for (const ModifierData *md = object.modifiers.first_as<ModifierData>();
       md != nullptr;
       md = md->next)
  {
    if (md->type != eModifierType_PhysNodes) {
      continue;
    }
    const PhysNodesModifierData *pmd = reinterpret_cast<const PhysNodesModifierData *>(md);
    if (STREQ(pmd->tree_name, tree_name.c_str())) {
      return true;
    }
  }
  return false;
}

static void realize_outputs(Main *bmain,
                            Depsgraph *depsgraph,
                            const std::string &tree_name,
                            const Runtime &runtime)
{
#ifdef WITH_OPENVDB
  if (bmain == nullptr) {
    return;
  }
  const RequestedOutputs &request = runtime.requested;

  if (request.particle_mesh && runtime.mesh.valid) {
    std::string name = "PHYSMesh_" + tree_name;
    Object *object = nullptr;
    if (!request.particle_mesh_object.empty()) {
      Object *target = find_object_by_name(bmain, request.particle_mesh_object);
      /* Writing into an object that already hosts a sink for this world would
       * stack the extracted mesh on top of the modifier's own write-back. */
      if (target != nullptr && target->type == OB_MESH && !object_hosts_sink(*target, tree_name)) {
        object = target;
        name = request.particle_mesh_object;
      }
    }
    if (object == nullptr) {
      object = find_object_by_name(bmain, name);
      if (object == nullptr || object->type != OB_MESH) {
        object = ensure_scene_object(bmain, name.c_str(), OB_MESH);
      }
    }
    write_mesh_output(bmain, name, object, runtime.mesh);
    /* Write the evaluated copy too so the viewport and render see the frame
     * that is currently being evaluated instead of lagging one update behind. */
    if (depsgraph != nullptr) {
      Object *object_eval = DEG_get_evaluated<Object>(depsgraph, object);
      if (object_eval != nullptr && object_eval != object && object_eval->type == OB_MESH &&
          object_eval->data != nullptr)
      {
        write_mesh_output(bmain, name, object_eval, runtime.mesh);
      }
    }
    DEG_id_type_tag(bmain, ID_OB);
  }

  if (request.volume_output && runtime.density.valid) {
    Object *object = nullptr;
    if (!request.volume_object.empty()) {
      Object *target = find_object_by_name(bmain, request.volume_object);
      if (target != nullptr && target->type == OB_VOLUME) {
        object = target;
      }
    }
    if (object == nullptr) {
      const std::string name = "PHYSVolume_" + tree_name;
      object = find_object_by_name(bmain, name);
      if (object == nullptr || object->type != OB_VOLUME) {
        object = ensure_scene_object(bmain, name.c_str(), OB_VOLUME);
      }
    }
    write_volume_output(bmain, object, runtime.density, true);
    /* Also fill the evaluated copy so rendering does not lag one depsgraph
     * update behind the simulation. */
    if (depsgraph != nullptr) {
      Object *object_eval = DEG_get_evaluated<Object>(depsgraph, object);
      if (object_eval != nullptr && object_eval != object && object_eval->type == OB_VOLUME &&
          object_eval->data != nullptr)
      {
        write_volume_output(bmain, object_eval, runtime.density, false);
      }
    }
    DEG_id_type_tag(bmain, ID_OB);
  }
#else
  (void)bmain;
  (void)depsgraph;
  (void)tree_name;
  (void)runtime;
#endif
}

}  // namespace

void sim_eval(Depsgraph *depsgraph, Span<std::string> tree_names)
{
  if (depsgraph == nullptr) {
    return;
  }
  if (!DEG_is_active(depsgraph)) {
    /* Inactive graphs must not mutate the shared world. */
    return;
  }
  const float ctime = DEG_get_ctime(depsgraph);
  Main *bmain = DEG_get_bmain(depsgraph);
  float dt = 1.0f / 24.0f;
  if (const Scene *scene = DEG_get_evaluated_scene(depsgraph)) {
    if (scene->r.frs_sec != 0 && scene->r.frs_sec_base > 0.0f) {
      dt = scene->r.frs_sec_base / float(scene->r.frs_sec);
    }
  }

  std::lock_guard<std::mutex> lock(runtime_mutex());
  for (const std::string &name : tree_names) {
    Runtime &runtime = runtimes().lookup_or_add_as(name, Runtime{});
    runtime.state.eval_count++;
    bNodeTree *tree = nullptr;
    if (bmain != nullptr) {
      update_sink_objects(bmain, name, runtime);
      tree = find_tree(bmain, name);
      runtime.state.tree_found = tree != nullptr;
      if (tree != nullptr) {
        assemble_world(depsgraph, *tree, runtime);
        bind_body_objects(runtime);
      }
    }
    advance_one_frame(runtime, ctime, dt);
    update_state_offset(runtime);
    if (tree != nullptr && bmain != nullptr) {
      /* Consume the real particle/smoke payloads and write the output
       * datablocks. This runs after the world step so the field matches the
       * frame that is about to be rendered. */
      process_outputs(depsgraph, bmain, *tree, runtime);
      realize_outputs(bmain, depsgraph, name, runtime);
    }
  }
}

bool frame_state(StringRefNull tree_name, FrameState *r_state)
{
  std::lock_guard<std::mutex> lock(runtime_mutex());
  const Runtime *runtime = runtimes().lookup_ptr(std::string(tree_name));
  if (runtime == nullptr) {
    return false;
  }
  *r_state = runtime->state;
  return true;
}

int last_assembled_body_count()
{
  std::lock_guard<std::mutex> lock(runtime_mutex());
  return g_last_assembled_body_count;
}

bool object_offset(StringRefNull tree_name, StringRefNull object_name, float3 *r_offset)
{
  if (tree_name.is_empty() || object_name.is_empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(runtime_mutex());
  const Runtime *runtime = runtimes().lookup_ptr(std::string(tree_name));
  if (runtime == nullptr) {
    return false;
  }
  for (const BodyRecord &record : runtime->bodies.values()) {
    if (record.kind != BODY_ACTIVE || !record.has_object) {
      continue;
    }
    if (std::strcmp(record.object_name.c_str(), object_name.data()) != 0) {
      continue;
    }
    *r_offset = float3(record.position.x - record.initial_position.x,
                       record.position.y - record.initial_position.y,
                       record.position.z - record.initial_position.z);
    return true;
  }
  return false;
}

void note_collider(StringRefNull tree_name, float volume, int source, int stack_index)
{
  if (tree_name.is_empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(runtime_mutex());
  Runtime &runtime = runtimes().lookup_or_add_as(std::string(tree_name), Runtime{});
  runtime.state.collider_volume = volume;
  runtime.state.collider_source = source;
  runtime.state.collider_stack_index = stack_index;
}

void reset_runtime()
{
  std::lock_guard<std::mutex> lock(runtime_mutex());
  runtimes().clear();
}

bool ParticleSet::valid() const
{
  if (positions.empty() || radii.size() != positions.size()) {
    return false;
  }
  for (const float3 &position : positions) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z))
    {
      return false;
    }
  }
  return true;
}

bool DensityField::valid() const
{
  if (voxel_count() <= 0) {
    return false;
  }
  if (values.size() != size_t(voxel_count())) {
    return false;
  }
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
    return false;
  }
  if (!std::isfinite(voxel_size.x) || !std::isfinite(voxel_size.y) ||
      !std::isfinite(voxel_size.z) || voxel_size.x <= 0.0f || voxel_size.y <= 0.0f ||
      voxel_size.z <= 0.0f)
  {
    return false;
  }
  return true;
}

int DensityField::voxel_count() const
{
  if (resolution.x <= 0 || resolution.y <= 0 || resolution.z <= 0) {
    return 0;
  }
  return resolution.x * resolution.y * resolution.z;
}

bool probe_kernel(StringRefNull path)
{
  KernelProbe &probe = kernel_probe();
  {
    std::lock_guard<std::mutex> lock(runtime_mutex());
    if (probe.done && probe.path == path) {
      return probe.ok;
    }
    probe.done = true;
    probe.ok = false;
    probe.path = std::string(path);
    probe.error.clear();
  }

  if (path.is_empty()) {
    std::lock_guard<std::mutex> lock(runtime_mutex());
    probe.error = "kernel path is empty";
    return false;
  }

  void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    std::lock_guard<std::mutex> lock(runtime_mutex());
    const char *error = dlerror();
    probe.error = error != nullptr ? error : "dlopen failed";
    return false;
  }

  /* Only entry points required by the step-1 ABI check. */
  static const char *symbols[] = {
      "rigid_create",
      "rigid_destroy",
      "rigid_integrate",
      "rigid_solve_normal",
      "rigid_friction",
      "rigid_velocities",
  };
  bool ok = true;
  for (const char *symbol : symbols) {
    if (dlsym(handle, symbol) == nullptr) {
      std::lock_guard<std::mutex> lock(runtime_mutex());
      probe.error = std::string("missing symbol: ") + symbol;
      ok = false;
      break;
    }
  }

  /* The probe intentionally keeps the library loaded; the solver will reuse
   * it in a later step. Closing it here would invalidate resolved pointers. */
  std::lock_guard<std::mutex> lock(runtime_mutex());
  probe.ok = ok;
  if (ok) {
    probe.error.clear();
  }
  return ok;
}

StringRefNull kernel_error()
{
  std::lock_guard<std::mutex> lock(runtime_mutex());
  return StringRefNull(kernel_probe().error);
}

}  // namespace blender::bke::physnodes
