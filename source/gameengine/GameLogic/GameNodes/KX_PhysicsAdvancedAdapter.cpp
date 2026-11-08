/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2026 EBC.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_PhysicsAdvancedAdapter.cpp
 *  \ingroup gamelogic
 *
 *  B6.1 advanced physics adapter: mock, null and engine implementations.
 *
 *  The engine side is a thin wrapper over the existing Bullet/Detour services:
 *  PHY_IVehicle for raycast vehicles, PHY_IPhysicsEnvironment::
 *  CreateConstraint for joints, PHY_IPhysicsController setters for body tuning,
 *  KX_NavMeshObject for Detour path queries and KX_RayCast for batched rays.
 *  No physics behaviour is reimplemented here.
 *
 *  Constraints created at runtime are tracked locally (the engine keeps no
 *  object → constraint mapping), which is what RemoveConstraint needs.
 */

#include "KX_PhysicsAdvancedAdapter.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>

#include "BKE_object.hh"
#include "BLI_bounds.hh"
#include "BLI_math_matrix.hh"
#include "KX_ClientObjectInfo.h"
#include "KX_GameObject.h"
#include "KX_NavMeshObject.h"
#include "KX_RayCast.h"
#include "KX_Scene.h"
#include "MT_Matrix3x3.h"
#include "MT_Vector3.h"
#include "PHY_IConstraint.h"
#include "PHY_DynamicTypes.h"
#include "PHY_IPhysicsController.h"
#include "PHY_IPhysicsEnvironment.h"
#include "PHY_IMotionState.h"
#include "PHY_IVehicle.h"

/* ------------------------------------------------------------------------- */
/* Null adapter                                                              */
/* ------------------------------------------------------------------------- */

namespace {

class NullPhysicsAdvancedAdapter final : public KX_PhysicsAdvancedAdapter {
 public:
};

NullPhysicsAdvancedAdapter &NullInstance()
{
  static NullPhysicsAdvancedAdapter instance;
  return instance;
}

}  // namespace

KX_PhysicsAdvancedAdapter &KX_NullPhysicsAdvancedAdapter()
{
  return NullInstance();
}

std::unique_ptr<KX_PhysicsAdvancedAdapter> KX_CreateNullPhysicsAdvancedAdapter()
{
  return std::unique_ptr<KX_PhysicsAdvancedAdapter>(new NullPhysicsAdvancedAdapter());
}

namespace {

/** Split "a, b ,c" into trimmed, non-empty names, preserving order. */
std::vector<std::string> SplitNames(const std::string &names)
{
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= names.size()) {
    const size_t comma = names.find(',', start);
    const size_t end = (comma == std::string::npos) ? names.size() : comma;
    std::string name = names.substr(start, end - start);
    const size_t first = name.find_first_not_of(" \t\r\n");
    if (first != std::string::npos) {
      const size_t last = name.find_last_not_of(" \t\r\n");
      out.push_back(name.substr(first, last - first + 1));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return out;
}

bool IsFinite3(const float value[3])
{
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

float Length3(const float value[3])
{
  return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

}  // namespace

bool KX_VehicleWheelIsUsable(const KX_VehicleWheelInput &wheel, std::string *error)
{
  auto fail = [error](const std::string &message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };

  char flags[128];
  snprintf(flags,
           sizeof(flags),
           "collision=%d rigid_body=%d compound_child=%d dynamic=%d",
           wheel.has_collision_flag ? 1 : 0,
           wheel.has_rigid_body_flag ? 1 : 0,
           wheel.is_compound_child ? 1 : 0,
           wheel.controller_is_dynamic ? 1 : 0);

  const std::string name = (wheel.name != nullptr) ? wheel.name : "";
  const std::string parent = (wheel.parent_name != nullptr) ? wheel.parent_name : "";
  const std::string source = (wheel.source != nullptr) ? wheel.source : "";

  if (!wheel.has_controller) {
    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "wheel object '%s' from %s has no physics controller "
             "(expected: physics controller with a motion state; actual: none; "
             "game flags: %s; parent='%s')",
             name.c_str(),
             source.c_str(),
             flags,
             parent.c_str());
    return fail(buffer);
  }

  if (!wheel.has_motion_state) {
    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "wheel object '%s' from %s has no motion state "
             "(expected: dynamic rigid body controller; actual: controller without "
             "a motion state; game flags: %s; parent='%s')",
             name.c_str(),
             source.c_str(),
             flags,
             parent.c_str());
    return fail(buffer);
  }

  if (!(std::isfinite(wheel.radius) && wheel.radius > 0.01f)) {
    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "wheel object '%s' from %s has no usable collision radius "
             "(expected: finite bounding radius > 0.01; actual: %.6g; game flags: %s)",
             name.c_str(),
             source.c_str(),
             static_cast<double>(wheel.radius),
             flags);
    return fail(buffer);
  }

  if (!IsFinite3(wheel.local)) {
    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "wheel object '%s' from %s has a non-finite suspension anchor "
             "(chassis or wheel transform is degenerate)",
             name.c_str(),
             source.c_str());
    return fail(buffer);
  }

  if (!IsFinite3(wheel.down) || !(Length3(wheel.down) > 1.0e-4f)) {
    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "wheel object '%s' from %s has a degenerate suspension direction "
             "(down=(%.6g, %.6g, %.6g); chassis orientation is invalid)",
             name.c_str(),
             source.c_str(),
             static_cast<double>(wheel.down[0]),
             static_cast<double>(wheel.down[1]),
             static_cast<double>(wheel.down[2]));
    return fail(buffer);
  }

  if (!IsFinite3(wheel.axle) || !(Length3(wheel.axle) > 1.0e-4f)) {
    char buffer[320];
    snprintf(buffer,
             sizeof(buffer),
             "wheel object '%s' from %s has a degenerate axle direction "
             "(axle=(%.6g, %.6g, %.6g); chassis orientation is invalid)",
             name.c_str(),
             source.c_str(),
             static_cast<double>(wheel.axle[0]),
             static_cast<double>(wheel.axle[1]),
             static_cast<double>(wheel.axle[2]));
    return fail(buffer);
  }

  return true;
}

/* ------------------------------------------------------------------------- */
/* Mock adapter                                                              */
/* ------------------------------------------------------------------------- */

void KX_MockPhysicsAdvancedAdapter::Reset()
{
  vehicle_chassis = nullptr;
  vehicle_template.clear();
  vehicle_wheel_names.clear();
  vehicle_wheels.clear();
  vehicle_suspension.clear();
  vehicles.clear();
  next_vehicle = 1;
  constraints.clear();
  owners.clear();
  next_constraint = 1;
  last_steering = 0.0f;
  last_throttle = 0.0f;
  last_brake = 0.0f;
  last_vehicle = -1;
  last_mass = 0.0f;
  last_friction = 0.0f;
  last_restitution = 0.0f;
  last_linear_damping = 0.0f;
  last_angular_damping = 0.0f;
  last_gravity[0] = last_gravity[1] = last_gravity[2] = 0.0f;
  last_agent_radius = 0.0f;
  last_agent_height = 0.0f;
  path_waypoints.clear();
  batch_hits.clear();
  last_batch_origins.clear();
  last_batch_dirs.clear();
  last_batch_distance = 0.0f;
  last_group = 0;
  last_mask = 0;
  create_vehicle_calls = 0;
  vehicle_control_calls = 0;
  add_constraint_calls = 0;
  remove_constraint_calls = 0;
  add_joint_calls = 0;
  set_mass_calls = 0;
  set_friction_calls = 0;
  set_restitution_calls = 0;
  set_linear_damping_calls = 0;
  set_angular_damping_calls = 0;
  set_gravity_calls = 0;
  build_navmesh_calls = 0;
  query_path_calls = 0;
  raycast_batch_calls = 0;
  set_collision_group_calls = 0;
}

int KX_MockPhysicsAdvancedAdapter::CreateVehicle(KX_GameObject *chassis,
                                                 const std::string &wheel_names,
                                                 const std::string &wheel_template,
                                                 std::string *error)
{
  ++create_vehicle_calls;
  if (chassis == nullptr) {
    if (error != nullptr) {
      *error = "chassis is null";
    }
    return -1;
  }
  const std::vector<std::string> wheels = SplitNames(wheel_names);
  if (wheels.empty() && wheel_template.empty()) {
    if (error != nullptr) {
      *error = "wheel_names and wheel template are both empty";
    }
    return -1;
  }
  vehicle_chassis = chassis;
  vehicle_template = wheel_template;
  vehicle_wheel_names = wheel_names;
  vehicle_wheels = wheels;
  const int handle = next_vehicle++;
  vehicles.push_back(handle);
  return handle;
}

int KX_MockPhysicsAdvancedAdapter::CreateVehicleWithSuspension(
    KX_GameObject *chassis,
    const std::string &wheel_names,
    const std::string &wheel_template,
    const std::vector<float> &wheel_groups,
    std::string *error)
{
  const int handle = CreateVehicle(chassis, wheel_names, wheel_template, error);
  if (handle > 0) {
    vehicle_suspension = wheel_groups;
  }
  return handle;
}

bool KX_MockPhysicsAdvancedAdapter::VehicleControl(int vehicle,
                                                   float steering,
                                                   float throttle,
                                                   float brake,
                                                   std::string *error)
{
  ++vehicle_control_calls;
  for (int handle : vehicles) {
    if (handle == vehicle) {
      last_vehicle = vehicle;
      last_steering = steering;
      last_throttle = throttle;
      last_brake = brake;
      return true;
    }
  }
  if (error != nullptr) {
    *error = "unknown vehicle handle";
  }
  return false;
}

int KX_MockPhysicsAdvancedAdapter::AddConstraint(KX_GameObject *object_a,
                                                 KX_GameObject *object_b,
                                                 int type,
                                                 const float params[6],
                                                 std::string *error)
{
  ++add_constraint_calls;
  if (object_a == nullptr || object_b == nullptr) {
    if (error != nullptr) {
      *error = "constraint objects are null";
    }
    return 0;
  }
  ConstraintCall call;
  call.a = object_a;
  call.b = object_b;
  call.type = type;
  for (int i = 0; i < 6; ++i) {
    call.params[i] = params != nullptr ? params[i] : 0.0f;
  }
  call.id = next_constraint++;
  constraints.push_back(call);
  owners.push_back(std::make_pair(call.id, object_a));
  owners.push_back(std::make_pair(call.id, object_b));
  return call.id;
}

int KX_MockPhysicsAdvancedAdapter::AddConstraintWithFrame(KX_GameObject *object_a,
                                                          KX_GameObject *object_b,
                                                          int type,
                                                          const float params[6],
                                                          const float pivot[3],
                                                          const float axis[3],
                                                          std::string *error)
{
  const int id = AddConstraint(object_a, object_b, type, params, error);
  if (id == 0) {
    return 0;
  }
  for (ConstraintCall &call : constraints) {
    if (call.id == id) {
      call.has_frame = true;
      for (int i = 0; i < 3; ++i) {
        call.pivot[i] = pivot != nullptr ? pivot[i] : 0.0f;
        call.axis[i] = axis != nullptr ? axis[i] : 0.0f;
      }
      break;
    }
  }
  return id;
}

int KX_MockPhysicsAdvancedAdapter::RemoveConstraints(KX_GameObject *object, std::string *error)
{
  ++remove_constraint_calls;
  if (object == nullptr) {
    if (error != nullptr) {
      *error = "object is null";
    }
    return 0;
  }
  int removed = 0;
  std::vector<ConstraintCall> kept;
  for (const ConstraintCall &call : constraints) {
    if (call.a == object || call.b == object) {
      ++removed;
    }
    else {
      kept.push_back(call);
    }
  }
  constraints.swap(kept);
  std::vector<std::pair<int, KX_GameObject *>> kept_owners;
  for (const auto &entry : owners) {
    bool alive = false;
    for (const ConstraintCall &call : constraints) {
      if (call.id == entry.first) {
        alive = true;
        break;
      }
    }
    if (alive) {
      kept_owners.push_back(entry);
    }
  }
  owners.swap(kept_owners);
  return removed;
}

bool KX_MockPhysicsAdvancedAdapter::AddJoint(KX_GameObject *object_a,
                                             KX_GameObject *object_b,
                                             const float pivot[3],
                                             const float axis[3],
                                             std::string *error)
{
  ++add_joint_calls;
  if (object_a == nullptr || object_b == nullptr) {
    if (error != nullptr) {
      *error = "joint objects are null";
    }
    return false;
  }
  (void)pivot;
  (void)axis;
  ConstraintCall call;
  call.a = object_a;
  call.b = object_b;
  call.type = KX_PHYS_CONSTRAINT_HINGE;
  call.id = next_constraint++;
  constraints.push_back(call);
  owners.push_back(std::make_pair(call.id, object_a));
  owners.push_back(std::make_pair(call.id, object_b));
  return true;
}

bool KX_MockPhysicsAdvancedAdapter::SetMass(KX_GameObject *object, float mass)
{
  (void)object;
  ++set_mass_calls;
  last_mass = mass;
  return object != nullptr;
}

bool KX_MockPhysicsAdvancedAdapter::SetFriction(KX_GameObject *object, float friction)
{
  (void)object;
  ++set_friction_calls;
  last_friction = friction;
  return object != nullptr;
}

bool KX_MockPhysicsAdvancedAdapter::SetRestitution(KX_GameObject *object,
                                                   float restitution,
                                                   std::string *error)
{
  ++set_restitution_calls;
  last_restitution = restitution;
  if (object == nullptr) {
    if (error != nullptr) {
      *error = "object is null";
    }
    return false;
  }
  if (!(restitution >= 0.0f && restitution <= 1.0f)) {
    if (error != nullptr) {
      *error = "restitution must be within [0, 1]";
    }
    return false;
  }
  return true;
}

bool KX_MockPhysicsAdvancedAdapter::SetLinearDamping(KX_GameObject *object, float damping)
{
  (void)object;
  ++set_linear_damping_calls;
  last_linear_damping = damping;
  return object != nullptr;
}

bool KX_MockPhysicsAdvancedAdapter::SetAngularDamping(KX_GameObject *object, float damping)
{
  (void)object;
  ++set_angular_damping_calls;
  last_angular_damping = damping;
  return object != nullptr;
}

bool KX_MockPhysicsAdvancedAdapter::SetGravity(const float gravity[3])
{
  ++set_gravity_calls;
  last_gravity[0] = gravity[0];
  last_gravity[1] = gravity[1];
  last_gravity[2] = gravity[2];
  return true;
}

bool KX_MockPhysicsAdvancedAdapter::BuildNavMesh(float agent_radius,
                                                 float agent_height,
                                                 std::string *error)
{
  ++build_navmesh_calls;
  last_agent_radius = agent_radius;
  last_agent_height = agent_height;
  if (agent_radius <= 0.0f || agent_height <= 0.0f) {
    if (error != nullptr) {
      *error = "agent dimensions must be positive";
    }
    return false;
  }
  return true;
}

bool KX_MockPhysicsAdvancedAdapter::QueryPath(const float from[3],
                                              const float to[3],
                                              std::vector<float> &waypoints,
                                              std::string *error)
{
  (void)from;
  (void)to;
  ++query_path_calls;
  waypoints = path_waypoints;
  if (path_waypoints.empty()) {
    if (error != nullptr) {
      *error = "no path";
    }
    return false;
  }
  return true;
}

bool KX_MockPhysicsAdvancedAdapter::RaycastBatch(KX_GameObject *ignore,
                                                 const std::vector<float> &origins,
                                                 const std::vector<float> &dirs,
                                                 float max_dist,
                                                 std::vector<KX_PhysicsBatchHit> &hits,
                                                 std::string *error)
{
  (void)ignore;
  ++raycast_batch_calls;
  last_batch_origins = origins;
  last_batch_dirs = dirs;
  last_batch_distance = max_dist;
  if (origins.size() != dirs.size()) {
    if (error != nullptr) {
      *error = "origins and directions must have the same size";
    }
    return false;
  }
  hits = batch_hits;
  return true;
}

bool KX_MockPhysicsAdvancedAdapter::SetCollisionGroup(KX_GameObject *object,
                                                      int group,
                                                      int mask,
                                                      std::string *error)
{
  (void)object;
  ++set_collision_group_calls;
  last_group = group;
  last_mask = mask;
  if (group < 0 || mask < 0 || group > 65535 || mask > 65535) {
    if (error != nullptr) {
      *error = "group/mask out of the 16 bit range";
    }
    return false;
  }
  return object != nullptr;
}

/* ------------------------------------------------------------------------- */
/* Engine adapter                                                            */
/* ------------------------------------------------------------------------- */

namespace {

class BatchRayCallback final : public KX_RayCast {
 public:
  KX_GameObject *hit_object = nullptr;

  explicit BatchRayCallback(PHY_IPhysicsController *ignore)
      : KX_RayCast(ignore, true, false)
  {
  }

  bool RayHit(KX_ClientObjectInfo *client) override
  {
    hit_object = (client != nullptr) ? client->m_gameobject : nullptr;
    return true;
  }
};

KX_NavMeshObject *FindSceneNavMesh(KX_Scene *scene)
{
  if (scene == nullptr) {
    return nullptr;
  }
  for (KX_GameObject *object : scene->GetObjectList()) {
    KX_NavMeshObject *navmesh = dynamic_cast<KX_NavMeshObject *>(object);
    if (navmesh != nullptr) {
      return navmesh;
    }
  }
  return nullptr;
}

KX_GameObject *FindSceneObject(KX_Scene *scene, const std::string &name)
{
  if (scene == nullptr || name.empty()) {
    return nullptr;
  }
  for (KX_GameObject *object : scene->GetObjectList()) {
    if (object != nullptr && name == object->GetName()) {
      return object;
    }
  }
  return nullptr;
}

/* Local (object space) half extents of the evaluated bounding box. This mirrors
 * what the scene converter derives for OB_BOUND_BOX / OB_BOUND_CYLINDER, so a
 * node default matches the shape a user would get from the collision bounds UI. */
bool ObjectLocalHalfExtents(KX_GameObject *object, float half_extents[3])
{
  blender::Object *blender_object = (object != nullptr) ? object->GetBlenderObject() : nullptr;
  if (blender_object == nullptr) {
    return false;
  }
  const std::optional<blender::Bounds<blender::float3>> bounds =
      blender::BKE_object_boundbox_eval_cached_get(blender_object);
  if (!bounds) {
    return false;
  }
  const std::array<blender::float3, 8> corners = blender::bounds::corners(*bounds);
  half_extents[0] = 0.5f * std::fabs(corners[0][0] - corners[4][0]);
  half_extents[1] = 0.5f * std::fabs(corners[0][1] - corners[2][1]);
  half_extents[2] = 0.5f * std::fabs(corners[0][2] - corners[1][2]);
  return half_extents[0] > 0.0f && half_extents[1] > 0.0f && half_extents[2] > 0.0f;
}

/* Wheel radius from the wheel mesh itself: the engine's default sphere reports
 * the object "radius" (1.0 m) as the bounding radius, which made the suspension
 * ray and the rest length several times taller than the real wheel. */
float WheelGeometryRadius(const float half_extents[3])
{
  /* OB_BOUND_CYLINDER convention: radius in local x/y, half height in z. */
  return (half_extents[1] > half_extents[0]) ? half_extents[1] : half_extents[0];
}

/* Default spring rate for a vehicle without an explicit suspension group. The
 * Bullet default (5.88) compresses an 800 kg chassis by ~0.42 m, which leaves
 * the body bottomed out and the front axle unloaded under drive torque. The
 * coefficient is calibrated on the vehicle scene (5 s, throttle 0.5): k = 100
 * for the 800 kg chassis holds the pitch at -2.7 degrees (3.7 peak) while k =
 * 60..80 already exceeded 4.4 degrees. Bullet computes suspension force as
 * stiffness * compression * chassisMass, hence the mass factor. */
float DefaultVehicleSuspensionStiffness(float mass)
{
  constexpr float kBulletDefaultStiffness = 5.88f;
  constexpr float kStiffnessPerKilogram = 0.125f;
  const float scaled = mass * kStiffnessPerKilogram;
  return (scaled > kBulletDefaultStiffness) ? scaled : kBulletDefaultStiffness;
}

/* Rest length that makes the authored pose (the wheel centre sits exactly one
 * wheel radius above the ground) the static equilibrium. With the ray length
 * = rest + radius, the suspension starts at compression = rest, so choosing
 * rest = mass * g / (4 * stiffness * mass) = g / (4 * stiffness) makes the
 * spring force exactly mass * g / 4 per wheel: the chassis neither launches
 * nor sags. A longer rest length (0.5 * wheel radius) preloads the spring and
 * a stiff default would fire the chassis into the air (measured 1.66 m/s at
 * stiffness 50 before this was derived). */
float DefaultVehicleSuspensionRestLength(float stiffness)
{
  return 9.81f / (4.0f * stiffness);
}

}  // namespace

class EnginePhysicsAdvancedAdapter final : public KX_PhysicsAdvancedAdapter {
 public:
  explicit EnginePhysicsAdvancedAdapter(KX_Scene *scene) : scene_(scene) {}

  int CreateVehicle(KX_GameObject *chassis,
                    const std::string &wheel_names,
                    const std::string &wheel_template,
                    std::string *error) override
  {
    if (chassis == nullptr) {
      SetError(error, "chassis object is null");
      return -1;
    }
    PHY_IPhysicsController *chassis_ctrl = chassis->GetPhysicsController();
    if (chassis_ctrl == nullptr) {
      SetError(error, "chassis has no physics controller (enable rigid body physics)");
      return -1;
    }
    if (!chassis_ctrl->IsDynamic()) {
      SetError(error, "chassis must be a dynamic rigid body");
      return -1;
    }
    if (!(std::isfinite(static_cast<double>(chassis_ctrl->GetMass())) &&
          chassis_ctrl->GetMass() > 0.0f)) {
      char buffer[256];
      snprintf(buffer,
               sizeof(buffer),
               "chassis object '%s' has an invalid mass (expected finite > 0; actual: %.6g)",
               chassis->GetName().c_str(),
               static_cast<double>(chassis_ctrl->GetMass()));
      SetError(error, buffer);
      return -1;
    }

    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return -1;
    }

    /* The scene converter falls back to a unit sphere for dynamic bodies that
     * do not opt into OB_BOUNDS. That sphere lifts the chassis off the ground
     * and the drive force then applies a large pitch torque below the center
     * of mass, which rolls the vehicle over. Give the chassis a box that
     * matches its mesh unless the blend file set the bounds explicitly (a
     * compound chassis is also left alone: its shape is deliberate). */
    blender::Object *chassis_object = chassis->GetBlenderObject();
    if (chassis_object != nullptr && (chassis_object->gameflag & blender::OB_BOUNDS) == 0 &&
        !chassis_ctrl->IsCompound())
    {
      float chassis_half_extents[3];
      if (!ObjectLocalHalfExtents(chassis, chassis_half_extents)) {
        SetError(error,
                 "chassis object has no usable bounding box for the default BOX collision shape");
        return -1;
      }
      const bool box_ok = chassis_ctrl->SetAnalyticCollisionShape(PHY_SHAPE_BOX,
                                                                 chassis_half_extents);
      if (!box_ok) {
        SetError(error,
                 "physics controller refused the default BOX collision shape for the chassis");
        return -1;
      }
      chassis_object->gameflag |= blender::OB_BOUNDS;
      chassis_object->collision_boundtype = blender::OB_BOUND_BOX;

      /* The engine steps physics once while the scene is created, before this
       * node runs. In that step the chassis is still the default 1 m sphere and
       * the wheels are still their own default spheres, so the bodies are
       * pushed apart and the chassis is ejected upwards (measured +0.36 m)
       * while the object transform keeps the authored height. Re-apply the
       * authored pose: otherwise the vehicle starts in the air and drops onto
       * its suspension before the drive torque has been applied. */
      const blender::float4x4 &authored = chassis_object->object_to_world();
      const MT_Vector3 authored_position(authored[3][0], authored[3][1], authored[3][2]);
      /* SetPosition/SetOrientation write the Bullet body transform directly.
       * Do not round-trip through the motion state here: for a dynamic body the
       * controller's motion state reads the object transform, which is only
       * refreshed after the simulation, so SetTransform() would write the
       * ejected position straight back into the body. */
      chassis_ctrl->SetPosition(authored_position);
      chassis_ctrl->SetOrientation(MT_Matrix3x3(&authored[0][0]));
      fprintf(stderr,
              "[VDBG] after reset body_z=%.3f node_z=%.3f\n",
              ([&] {
                MT_Vector3 now;
                chassis_ctrl->GetPosition(now);
                return (double)now.z();
              })(),
              (double)chassis->NodeGetWorldPosition().z());
    }
    const MT_Vector3 chassis_position = chassis->NodeGetWorldPosition();
    const MT_Matrix3x3 chassis_rotation = chassis->NodeGetWorldOrientation();
    const MT_Matrix3x3 inverse_rotation = chassis_rotation.inverse();
    const MT_Vector3 down_direction = inverse_rotation * MT_Vector3(0.0f, 0.0f, -1.0f);
    const MT_Vector3 axle_direction = inverse_rotation * MT_Vector3(1.0f, 0.0f, 0.0f);

    /* Every wheel is resolved and validated before the vehicle exists: the
     * engine has no public call to delete a vehicle constraint, so a half
     * built vehicle would leak one. Only admitted wheels reach Bullet. */
    struct WheelSpec {
      PHY_IPhysicsController *controller;
      MT_Vector3 local;
      float radius;
      blender::Object *blender_object;
      float half_extents[3];
      bool default_shape;
    };
    std::vector<WheelSpec> wheel_specs;

    auto collect_wheel = [&](KX_GameObject *wheel, const char *source) -> bool {
      PHY_IPhysicsController *wheel_ctrl = wheel->GetPhysicsController();
      const std::string wheel_name = wheel->GetName();
      KX_GameObject *parent = wheel->GetParent();
      const std::string parent_name = (parent != nullptr) ? parent->GetName() : std::string();
      blender::Object *blender_object = wheel->GetBlenderObject();
      blender::Object *parent_object = (parent != nullptr) ? parent->GetBlenderObject() : nullptr;

      /* The suspension anchor is the wheel's authored rest position in chassis
       * space, not its world position at node time: the engine has already
       * stepped physics once before the node runs, and the default sphere
       * fallback ejects the chassis upwards in that step. Using the runtime
       * position then bakes that bogus lift into every wheel offset and the
       * suspension can never settle back down. */
      MT_Vector3 local;
      const MT_Vector3 runtime_local =
          inverse_rotation * (wheel->NodeGetWorldPosition() - chassis_position);
      const blender::Object *chassis_blender_object = chassis->GetBlenderObject();
      if (chassis_blender_object != nullptr && blender_object != nullptr) {
        const blender::float4x4 relative =
            blender::math::invert(chassis_blender_object->object_to_world()) *
            blender_object->object_to_world();
        local = MT_Vector3(relative[3][0], relative[3][1], relative[3][2]);
      }
      else {
        local = runtime_local;
      }

      float half_extents[3] = {0.0f, 0.0f, 0.0f};
      const bool has_half_extents = ObjectLocalHalfExtents(wheel, half_extents);

      KX_VehicleWheelInput input;
      input.name = wheel_name.c_str();
      input.parent_name = parent_name.c_str();
      input.source = source;
      input.has_controller = wheel_ctrl != nullptr;
      input.has_motion_state = wheel_ctrl != nullptr && wheel_ctrl->GetMotionState() != nullptr;
      input.controller_is_dynamic = wheel_ctrl != nullptr && wheel_ctrl->IsDynamic();
      /* The suspension ray and rest length must use the real wheel size: the
       * default sphere fallback reports the object radius (1.0 m) instead. */
      input.radius = has_half_extents
                        ? WheelGeometryRadius(half_extents)
                        : ((wheel_ctrl != nullptr) ? wheel_ctrl->GetShapeBoundingRadius() : 0.0f);
      local.getValue(input.local);
      down_direction.getValue(input.down);
      axle_direction.getValue(input.axle);
      if (blender_object != nullptr) {
        input.has_collision_flag = (blender_object->gameflag & blender::OB_COLLISION) != 0;
        input.has_rigid_body_flag = (blender_object->gameflag & blender::OB_RIGID_BODY) != 0;
        if (parent_object != nullptr) {
          input.is_compound_child = ((blender_object->gameflag & blender::OB_CHILD) != 0) &&
                                    ((parent_object->gameflag & blender::OB_CHILD) != 0);
        }
      }

      if (!has_half_extents &&
          (blender_object == nullptr || (blender_object->gameflag & blender::OB_BOUNDS) == 0)) {
        char buffer[256];
        snprintf(buffer,
                 sizeof(buffer),
                 "wheel object '%s' has no usable bounding box for the suspension radius",
                 wheel_name.c_str());
        SetError(error, buffer);
        return false;
      }

      if (!KX_VehicleWheelIsUsable(input, error)) {
        return false;
      }

      WheelSpec spec;
      spec.controller = wheel_ctrl;
      spec.local = local;
      spec.radius = input.radius;
      spec.blender_object = blender_object;
      spec.half_extents[0] = half_extents[0];
      spec.half_extents[1] = half_extents[1];
      spec.half_extents[2] = half_extents[2];
      spec.default_shape = has_half_extents && blender_object != nullptr &&
                           (blender_object->gameflag & blender::OB_BOUNDS) == 0 &&
                           !wheel_ctrl->IsCompound();
      wheel_specs.push_back(spec);
      return true;
    };

    if (!wheel_names.empty()) {
      for (const std::string &name : SplitNames(wheel_names)) {
        KX_GameObject *wheel = FindSceneObject(scene_, name);
        if (wheel == nullptr) {
          char buffer[256];
          snprintf(buffer,
                   sizeof(buffer),
                   "wheel object '%s' from wheel_names was not found in the scene",
                   name.c_str());
          SetError(error, buffer);
          return -1;
        }
        if (!collect_wheel(wheel, "wheel_names")) {
          return -1;
        }
      }
      if (wheel_specs.empty()) {
        SetError(error, "wheel_names contains no usable names");
        return -1;
      }
    }
    else {
      if (wheel_template.empty()) {
        SetError(error, "wheel template name is empty (and wheel_names is not set)");
        return -1;
      }
      KX_GameObject *template_object = FindSceneObject(scene_, wheel_template);
      if (template_object == nullptr) {
        char buffer[256];
        snprintf(buffer,
                 sizeof(buffer),
                 "wheel template object '%s' not found in the scene",
                 wheel_template.c_str());
        SetError(error, buffer);
        return -1;
      }
      for (KX_GameObject *child : template_object->GetChildrenRecursive()) {
        /* Legacy template convention: helper objects without physics are ignored. */
        if (child != nullptr && child->GetPhysicsController() != nullptr) {
          if (!collect_wheel(child, "wheel template")) {
            return -1;
          }
        }
      }
      if (wheel_specs.empty()) {
        char buffer[256];
        snprintf(buffer,
                 sizeof(buffer),
                 "wheel template '%s' has no child with a physics controller",
                 wheel_template.c_str());
        SetError(error, buffer);
        return -1;
      }
    }

    PHY_IVehicle *vehicle = environment->CreateVehicle(chassis_ctrl);
    if (vehicle == nullptr) {
      SetError(error, "physics environment refused to create the vehicle");
      return -1;
    }

    const float default_stiffness = DefaultVehicleSuspensionStiffness(chassis_ctrl->GetMass());
    for (int i = 0; i < static_cast<int>(wheel_specs.size()); ++i) {
      const WheelSpec &spec = wheel_specs[static_cast<size_t>(i)];
      /* See DefaultVehicleSuspensionRestLength: using 0.5 * wheel radius here
       * preloads the spring because the authored wheel centre is already on the
       * ground, and a calibrated stiff default would catapult the chassis. */
      const float rest_length = DefaultVehicleSuspensionRestLength(default_stiffness);
      /* The wheel controller owns its motion state; the vehicle must not
       * delete it (ownsMotionState = false). */
      vehicle->AddWheel(spec.controller->GetMotionState(),
                        spec.local,
                        down_direction,
                        axle_direction,
                        rest_length,
                        spec.radius,
                        true,
                        false);

      /* Wheels are ghosts: a raycast wheel only needs a motion state, its
       * collider must not push the chassis or sink into the ground (the old
       * per-wheel group bit made the wheels fall through the floor). They
       * share the chassis group/mask so tooling that inspects the wheels sees
       * the same setup, and the suspension ray skips them because Bullet
       * ray results ignore bodies without a contact response. */
      if (spec.default_shape &&
          spec.controller->SetAnalyticCollisionShape(PHY_SHAPE_CYLINDER, spec.half_extents)) {
        /* Mirror the engine's own analytic shape for a wheel: a cylinder whose
         * local z is the axle axis, radius in x/y. The fallback sphere would
         * otherwise keep a 1 m radius on a 0.35 m wheel. */
        spec.blender_object->gameflag |= blender::OB_BOUNDS;
        spec.blender_object->collision_boundtype = blender::OB_BOUND_CYLINDER;
      }
      spec.controller->SetCollisionGroup(chassis_ctrl->GetCollisionGroup());
      spec.controller->SetCollisionMask(chassis_ctrl->GetCollisionMask());
      fprintf(stderr,
              "[VDBG] wheel %d pre-suspend z=%.3f group=%d mask=%d\n",
              i,
              spec.controller->GetMotionState() != nullptr
                  ? (double)spec.controller->GetMotionState()->GetWorldPosition().z()
                  : -999.0,
              (int)spec.controller->GetCollisionGroup(),
              (int)spec.controller->GetCollisionMask());
      spec.controller->SuspendDynamics(true);

      /* Only applied when no explicit suspension group was supplied; the
       * suspending variant overwrites this afterwards with its own values. */
      vehicle->SetSuspensionStiffness(default_stiffness, i);
    }

    const int handle = vehicle->GetUserConstraintId();
    vehicles_[handle] = vehicle;
    chassis_[handle] = chassis;
    return handle;
  }

  int CreateVehicleWithSuspension(KX_GameObject *chassis,
                                  const std::string &wheel_names,
                                  const std::string &wheel_template,
                                  const std::vector<float> &wheel_groups,
                                  std::string *error) override
  {
    if ((wheel_groups.size() % 4u) != 0u) {
      SetError(error, "suspension groups must contain four values each");
      return -1;
    }
    const int groups = static_cast<int>(wheel_groups.size() / 4u);
    /* Validate everything we can before creating the vehicle: the engine has no
     * public "delete vehicle" call, so a late failure would leave it in the scene. */
    if (!wheel_names.empty() && groups != 1 &&
        groups != static_cast<int>(SplitNames(wheel_names).size())) {
      SetError(error, "suspension needs one group or one group per named wheel");
      return -1;
    }
    for (int i = 0; i < groups; ++i) {
      const int base = i * 4;
      for (int k = 0; k < 3; ++k) {
        if (!(std::isfinite(wheel_groups[base + k]) && wheel_groups[base + k] >= 0.0f)) {
          SetError(error, "suspension stiffness and damping must be finite and non-negative");
          return -1;
        }
      }
      if (!(std::isfinite(wheel_groups[base + 3]) && wheel_groups[base + 3] > 0.0f)) {
        SetError(error, "suspension rest length must be finite and positive");
        return -1;
      }
    }
    const int handle = CreateVehicle(chassis, wheel_names, wheel_template, error);
    if (handle <= 0) {
      return handle;
    }
    PHY_IVehicle *vehicle = FindVehicle(handle);
    if (vehicle == nullptr) {
      SetError(error, "vehicle handle was not registered");
      return -1;
    }
    const int wheels = vehicle->GetNumWheels();
    if (wheels <= 0) {
      SetError(error, "vehicle has no wheels");
      return -1;
    }
    /* Template-derived wheel counts are only known after creation; a mismatch
     * here cannot be rolled back, so report it instead of applying wrong values. */
    if (groups != 1 && groups != wheels) {
      SetError(error, "suspension needs one group or one group per wheel");
      return -1;
    }
    for (int i = 0; i < wheels; ++i) {
      const int base = (groups == 1 ? 0 : i) * 4;
      vehicle->SetSuspensionStiffness(wheel_groups[base], i);
      vehicle->SetSuspensionDamping(wheel_groups[base + 1], i);
      vehicle->SetSuspensionCompression(wheel_groups[base + 2], i);
      vehicle->SetSuspensionRestLength(wheel_groups[base + 3], i);
    }
    return handle;
  }

  bool VehicleControl(int vehicle, float steering, float throttle, float brake, std::string *error) override
  {
    PHY_IVehicle *handle = FindVehicle(vehicle);
    if (handle == nullptr) {
      SetError(error, "unknown vehicle handle");
      return false;
    }
    const int wheel_count = handle->GetNumWheels();
    if (wheel_count <= 0) {
      SetError(error, "vehicle has no wheels");
      return false;
    }
    /* Normalized inputs: steering is radians, throttle and brake are 0..1. */
    const float engine_force = throttle * 2000.0f;
    const float braking = brake * 100.0f;
    for (int i = 0; i < wheel_count; ++i) {
      handle->SetSteeringValue(steering, i);
      handle->ApplyEngineForce(engine_force, i);
      handle->ApplyBraking(braking, i);
    }
    return true;
  }

  int AddConstraint(KX_GameObject *object_a,
                    KX_GameObject *object_b,
                    int type,
                    const float params[6],
                    std::string *error) override
  {
    if (object_a == nullptr || object_b == nullptr) {
      SetError(error, "constraint objects are null");
      return 0;
    }
    PHY_IPhysicsController *ctrl_a = object_a->GetPhysicsController();
    PHY_IPhysicsController *ctrl_b = object_b->GetPhysicsController();
    if (ctrl_a == nullptr || ctrl_b == nullptr) {
      SetError(error, "both constraint objects need a physics controller");
      return 0;
    }
    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return 0;
    }

    PHY_ConstraintType engine_type = PHY_POINT2POINT_CONSTRAINT;
    switch (type) {
      case KX_PHYS_CONSTRAINT_POINT:
        engine_type = PHY_POINT2POINT_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_HINGE:
        engine_type = PHY_LINEHINGE_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_SLIDER:
        /* Native Bullet slider: one free linear axis, rotations locked. */
        engine_type = PHY_SLIDER_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_CONE:
        engine_type = PHY_CONE_TWIST_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_GENERIC:
        engine_type = PHY_GENERIC_6DOF_CONSTRAINT;
        break;
      default:
        SetError(error, "unknown constraint type id");
        return 0;
    }

    const MT_Vector3 pivot_world =
        (object_a->NodeGetWorldPosition() + object_b->NodeGetWorldPosition()) * 0.5f;
    const MT_Vector3 local_pivot =
        object_a->NodeGetWorldOrientation().inverse() * (pivot_world - object_a->NodeGetWorldPosition());
    const MT_Vector3 local_axis =
        object_a->NodeGetWorldOrientation().inverse() * MT_Vector3(0.0f, 1.0f, 0.0f);

    PHY_IConstraint *constraint = environment->CreateConstraint(ctrl_a,
                                                                ctrl_b,
                                                                engine_type,
                                                                local_pivot.x(),
                                                                local_pivot.y(),
                                                                local_pivot.z(),
                                                                local_axis.x(),
                                                                local_axis.y(),
                                                                local_axis.z());
    if (constraint == nullptr) {
      SetError(error, "physics environment refused to create the constraint (two static bodies?)");
      return 0;
    }

    switch (engine_type) {
      case PHY_SLIDER_CONSTRAINT:
        /* params[0] and params[1] are the linear travel limits along the
         * slider axis; rotations are locked by btSliderConstraint itself. */
        constraint->SetParam(0, params[0], params[1]);
        break;
      case PHY_GENERIC_6DOF_CONSTRAINT:
        /* params = [loX, hiX, loY, hiY, loZ, hiZ]. */
        constraint->SetParam(0, params[0], params[1]);
        constraint->SetParam(1, params[2], params[3]);
        constraint->SetParam(2, params[4], params[5]);
        break;
      case PHY_CONE_TWIST_CONSTRAINT:
        /* params[0..2] are the three angular high limits. */
        constraint->SetParam(3, 0.0f, params[0]);
        constraint->SetParam(4, 0.0f, params[1]);
        constraint->SetParam(5, 0.0f, params[2]);
        break;
      case PHY_LINEHINGE_CONSTRAINT:
        /* params[0] and params[1] are the hinge low/high angle limits. */
        constraint->SetParam(3, params[0], params[1]);
        break;
      default:
        break;
    }

    const int id = constraint->GetIdentifier();
    constraints_.push_back(ConstraintEntry{id, object_a, object_b});
    return id;
  }

  int AddConstraintWithFrame(KX_GameObject *object_a,
                             KX_GameObject *object_b,
                             int type,
                             const float params[6],
                             const float pivot[3],
                             const float axis[3],
                             std::string *error) override
  {
    if (object_a == nullptr || object_b == nullptr) {
      SetError(error, "constraint objects are null");
      return 0;
    }
    PHY_IPhysicsController *ctrl_a = object_a->GetPhysicsController();
    PHY_IPhysicsController *ctrl_b = object_b->GetPhysicsController();
    if (ctrl_a == nullptr || ctrl_b == nullptr) {
      SetError(error, "both constraint objects need a physics controller");
      return 0;
    }
    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return 0;
    }

    PHY_ConstraintType engine_type = PHY_POINT2POINT_CONSTRAINT;
    switch (type) {
      case KX_PHYS_CONSTRAINT_POINT:
        engine_type = PHY_POINT2POINT_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_HINGE:
        engine_type = PHY_LINEHINGE_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_SLIDER:
        engine_type = PHY_SLIDER_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_CONE:
        engine_type = PHY_CONE_TWIST_CONSTRAINT;
        break;
      case KX_PHYS_CONSTRAINT_GENERIC:
        engine_type = PHY_GENERIC_6DOF_CONSTRAINT;
        break;
      default:
        SetError(error, "unknown constraint type id");
        return 0;
    }

    const MT_Vector3 world_axis(axis[0], axis[1], axis[2]);
    if (world_axis.length2() < 1e-10f) {
      SetError(error, "constraint axis is degenerate");
      return 0;
    }
    const MT_Vector3 world_pivot(pivot[0], pivot[1], pivot[2]);
    const MT_Matrix3x3 inverse = object_a->NodeGetWorldOrientation().inverse();
    const MT_Vector3 local_pivot = inverse * (world_pivot - object_a->NodeGetWorldPosition());
    const MT_Vector3 local_axis = inverse * world_axis;

    PHY_IConstraint *constraint = environment->CreateConstraint(ctrl_a,
                                                                ctrl_b,
                                                                engine_type,
                                                                local_pivot.x(),
                                                                local_pivot.y(),
                                                                local_pivot.z(),
                                                                local_axis.x(),
                                                                local_axis.y(),
                                                                local_axis.z());
    if (constraint == nullptr) {
      SetError(error, "physics environment refused to create the constraint (two static bodies?)");
      return 0;
    }

    switch (engine_type) {
      case PHY_SLIDER_CONSTRAINT:
        constraint->SetParam(0, params[0], params[1]);
        break;
      case PHY_GENERIC_6DOF_CONSTRAINT:
        constraint->SetParam(0, params[0], params[1]);
        constraint->SetParam(1, params[2], params[3]);
        constraint->SetParam(2, params[4], params[5]);
        break;
      case PHY_CONE_TWIST_CONSTRAINT:
        constraint->SetParam(3, 0.0f, params[0]);
        constraint->SetParam(4, 0.0f, params[1]);
        constraint->SetParam(5, 0.0f, params[2]);
        break;
      case PHY_LINEHINGE_CONSTRAINT:
        constraint->SetParam(3, params[0], params[1]);
        break;
      default:
        break;
    }

    const int id = constraint->GetIdentifier();
    constraints_.push_back(ConstraintEntry{id, object_a, object_b});
    return id;
  }

  int RemoveConstraints(KX_GameObject *object, std::string *error) override
  {
    if (object == nullptr) {
      SetError(error, "object is null");
      return 0;
    }
    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return 0;
    }
    int removed = 0;
    std::vector<ConstraintEntry> kept;
    for (const ConstraintEntry &entry : constraints_) {
      if (entry.a == object || entry.b == object) {
        environment->RemoveConstraintById(entry.id, true);
        ++removed;
      }
      else {
        kept.push_back(entry);
      }
    }
    constraints_.swap(kept);
    return removed;
  }

  bool AddJoint(KX_GameObject *object_a,
                KX_GameObject *object_b,
                const float pivot[3],
                const float axis[3],
                std::string *error) override
  {
    if (object_a == nullptr || object_b == nullptr) {
      SetError(error, "joint objects are null");
      return false;
    }
    PHY_IPhysicsController *ctrl_a = object_a->GetPhysicsController();
    PHY_IPhysicsController *ctrl_b = object_b->GetPhysicsController();
    if (ctrl_a == nullptr || ctrl_b == nullptr) {
      SetError(error, "both joint objects need a physics controller");
      return false;
    }
    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return false;
    }

    const MT_Vector3 world_axis(axis[0], axis[1], axis[2]);
    if (world_axis.length2() < 1e-10f) {
      SetError(error, "joint axis is degenerate");
      return false;
    }
    const MT_Vector3 world_pivot(pivot[0], pivot[1], pivot[2]);
    const MT_Matrix3x3 inverse = object_a->NodeGetWorldOrientation().inverse();
    const MT_Vector3 local_pivot = inverse * (world_pivot - object_a->NodeGetWorldPosition());
    const MT_Vector3 local_axis = inverse * world_axis;

    PHY_IConstraint *constraint = environment->CreateConstraint(ctrl_a,
                                                                ctrl_b,
                                                                PHY_LINEHINGE_CONSTRAINT,
                                                                local_pivot.x(),
                                                                local_pivot.y(),
                                                                local_pivot.z(),
                                                                local_axis.x(),
                                                                local_axis.y(),
                                                                local_axis.z());
    if (constraint == nullptr) {
      SetError(error, "physics environment refused to create the joint");
      return false;
    }
    constraints_.push_back(ConstraintEntry{constraint->GetIdentifier(), object_a, object_b});
    return true;
  }

  bool SetMass(KX_GameObject *object, float mass) override
  {
    PHY_IPhysicsController *controller = ControllerOf(object);
    if (controller == nullptr || !(mass > 0.0f)) {
      return false;
    }
    controller->SetMass(mass);
    return true;
  }

  bool SetFriction(KX_GameObject *object, float friction) override
  {
    PHY_IPhysicsController *controller = ControllerOf(object);
    if (controller == nullptr || friction < 0.0f) {
      return false;
    }
    controller->SetFriction(friction);
    return true;
  }

  bool SetRestitution(KX_GameObject *object, float restitution, std::string *error) override
  {
    PHY_IPhysicsController *controller = ControllerOf(object);
    if (controller == nullptr) {
      SetError(error, "object has no physics controller");
      return false;
    }
    if (!(restitution >= 0.0f && restitution <= 1.0f)) {
      SetError(error, "restitution must be within [0, 1]");
      return false;
    }
    /* Narrow controller interface: a scalar write on the live rigid body,
     * no remove/re-add, so contacts, constraints and velocities survive.
     * Bullet recomputes combined restitution when manifold points are
     * created, so a change applies to new contact points; persistent
     * manifold points keep their old combined value until refreshed. */
    if (!controller->SetRestitution(restitution)) {
      SetError(error,
               "physics controller refused the restitution change "
               "(no rigid body or physics suspended)");
      return false;
    }
    return true;
  }

  bool SetLinearDamping(KX_GameObject *object, float damping) override
  {
    PHY_IPhysicsController *controller = ControllerOf(object);
    if (controller == nullptr || damping < 0.0f) {
      return false;
    }
    controller->SetLinearDamping(damping);
    return true;
  }

  bool SetAngularDamping(KX_GameObject *object, float damping) override
  {
    PHY_IPhysicsController *controller = ControllerOf(object);
    if (controller == nullptr || damping < 0.0f) {
      return false;
    }
    controller->SetAngularDamping(damping);
    return true;
  }

  bool SetGravity(const float gravity[3]) override
  {
    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      return false;
    }
    environment->SetGravity(gravity[0], gravity[1], gravity[2]);
    return true;
  }

  bool BuildNavMesh(float agent_radius, float agent_height, std::string *error) override
  {
    KX_NavMeshObject *navmesh = FindSceneNavMesh(scene_);
    if (navmesh == nullptr) {
      SetError(error, "scene has no navmesh object");
      return false;
    }
    if (!(agent_radius > 0.0f) || !(agent_height > 0.0f)) {
      SetError(error, "agent radius and height must be positive");
      return false;
    }
    /* Runtime Recast bake (P-1): rasterizes the object's geometry at the given
     * agent size, so unlike the editor-baked `.recast_data` path the parameters
     * really change the walkable surface. */
    if (!navmesh->BuildNavMeshRuntime(agent_radius, agent_height)) {
      SetError(error, "runtime navmesh bake failed");
      return false;
    }
    fprintf(stderr,
            "[BuildNavMesh] baked Detour navmesh from scene geometry; agent radius %.2f / "
            "height %.2f applied\n",
            agent_radius,
            agent_height);
    return true;
  }

  bool QueryPath(const float from[3],
                 const float to[3],
                 std::vector<float> &waypoints,
                 std::string *error) override
  {
    KX_NavMeshObject *navmesh = FindSceneNavMesh(scene_);
    if (navmesh == nullptr) {
      SetError(error, "scene has no navmesh object");
      return false;
    }
    if (navmesh->GetNavMesh() == nullptr && !navmesh->BuildNavMesh()) {
      SetError(error, "navmesh is not built and could not be built");
      return false;
    }
    const int max_points = 64;
    std::vector<float> buffer(max_points * 3);
    const int count = navmesh->FindPath(
        MT_Vector3(from[0], from[1], from[2]),
        MT_Vector3(to[0], to[1], to[2]),
        buffer.data(),
        max_points);
    if (count < 0) {
      SetError(error, "navmesh path query failed");
      return false;
    }
    waypoints.assign(buffer.begin(), buffer.begin() + count * 3);
    return true;
  }

  bool RaycastBatch(KX_GameObject *ignore,
                    const std::vector<float> &origins,
                    const std::vector<float> &dirs,
                    float max_dist,
                    std::vector<KX_PhysicsBatchHit> &hits,
                    std::string *error) override
  {
    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return false;
    }
    if (origins.size() != dirs.size() || origins.size() % 3 != 0) {
      SetError(error, "origins and directions must be flat xyz arrays of the same size");
      return false;
    }
    if (!(max_dist > 0.0f)) {
      SetError(error, "raycast distance must be positive");
      return false;
    }

    const size_t rays = origins.size() / 3;
    hits.assign(rays, KX_PhysicsBatchHit());

    PHY_IPhysicsController *ignore_controller =
        ignore != nullptr ? ignore->GetPhysicsController() : nullptr;

    for (size_t i = 0; i < rays; ++i) {
      const MT_Vector3 origin(origins[i * 3], origins[i * 3 + 1], origins[i * 3 + 2]);
      const MT_Vector3 direction(dirs[i * 3], dirs[i * 3 + 1], dirs[i * 3 + 2]);
      if (direction.length2() < 1e-12f) {
        continue;
      }
      const MT_Vector3 target = origin + direction.safe_normalized() * max_dist;
      BatchRayCallback callback(ignore_controller);
      if (!KX_RayCast::RayTest(environment, origin, target, callback)) {
        continue;
      }
      if (!callback.m_hitFound || callback.hit_object == nullptr) {
        continue;
      }
      hits[i].hit = true;
      hits[i].object = callback.hit_object;
      hits[i].point[0] = callback.m_hitPoint[0];
      hits[i].point[1] = callback.m_hitPoint[1];
      hits[i].point[2] = callback.m_hitPoint[2];
    }
    return true;
  }

  bool SetCollisionGroup(KX_GameObject *object, int group, int mask, std::string *error) override
  {
    if (object == nullptr) {
      SetError(error, "object is null");
      return false;
    }
    if (group < 0 || group > 65535 || mask < 0 || mask > 65535) {
      SetError(error, "collision group and mask must fit in 16 bits");
      return false;
    }
    PHY_IPhysicsController *controller = object->GetPhysicsController();
    if (controller == nullptr) {
      SetError(error, "object has no physics controller");
      return false;
    }
    controller->SetCollisionGroup(static_cast<unsigned short>(group));
    controller->SetCollisionMask(static_cast<unsigned short>(mask));
    return true;
  }

 private:
  struct ConstraintEntry {
    int id = 0;
    KX_GameObject *a = nullptr;
    KX_GameObject *b = nullptr;
  };

  PHY_IPhysicsEnvironment *Environment() const
  {
    return scene_ != nullptr ? scene_->GetPhysicsEnvironment() : nullptr;
  }

  PHY_IPhysicsController *ControllerOf(KX_GameObject *object) const
  {
    return object != nullptr ? object->GetPhysicsController() : nullptr;
  }

  PHY_IVehicle *FindVehicle(int handle) const
  {
    const auto found = vehicles_.find(handle);
    return found != vehicles_.end() ? found->second : nullptr;
  }

  static void SetError(std::string *error, const std::string &message)
  {
    if (error != nullptr) {
      *error = message;
    }
  }

  KX_Scene *scene_ = nullptr;
  std::map<int, PHY_IVehicle *> vehicles_;
  std::map<int, KX_GameObject *> chassis_;
  std::vector<ConstraintEntry> constraints_;
};

KX_PhysicsAdvancedAdapter *KX_CreateEnginePhysicsAdvancedAdapter(KX_Scene *scene)
{
  if (scene == nullptr) {
    return nullptr;
  }
  return new EnginePhysicsAdvancedAdapter(scene);
}
