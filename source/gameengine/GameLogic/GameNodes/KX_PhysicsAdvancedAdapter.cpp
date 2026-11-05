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

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>

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

}  // namespace

/* ------------------------------------------------------------------------- */
/* Mock adapter                                                              */
/* ------------------------------------------------------------------------- */

void KX_MockPhysicsAdvancedAdapter::Reset()
{
  vehicle_chassis = nullptr;
  vehicle_template.clear();
  vehicle_wheel_names.clear();
  vehicle_wheels.clear();
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

    PHY_IPhysicsEnvironment *environment = Environment();
    if (environment == nullptr) {
      SetError(error, "scene has no physics environment");
      return -1;
    }

    /* Resolve the wheel rig before creating the vehicle: the engine has no
     * public call to delete a vehicle constraint, so a half built vehicle
     * would leak one. Explicit wheel_names take precedence over the legacy
     * template convention. */
    std::vector<KX_GameObject *> wheels;
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
        if (wheel->GetPhysicsController() == nullptr) {
          char buffer[256];
          snprintf(buffer,
                   sizeof(buffer),
                   "wheel object '%s' from wheel_names has no physics controller",
                   name.c_str());
          SetError(error, buffer);
          return -1;
        }
        wheels.push_back(wheel);
      }
      if (wheels.empty()) {
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
        if (child != nullptr && child->GetPhysicsController() != nullptr) {
          wheels.push_back(child);
        }
      }
      if (wheels.empty()) {
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

    const MT_Vector3 chassis_position = chassis->NodeGetWorldPosition();
    const MT_Matrix3x3 chassis_rotation = chassis->NodeGetWorldOrientation();
    const MT_Matrix3x3 inverse_rotation = chassis_rotation.inverse();
    const MT_Vector3 down_direction = inverse_rotation * MT_Vector3(0.0f, 0.0f, -1.0f);
    const MT_Vector3 axle_direction = inverse_rotation * MT_Vector3(1.0f, 0.0f, 0.0f);

    for (KX_GameObject *wheel : wheels) {
      PHY_IPhysicsController *wheel_ctrl = wheel->GetPhysicsController();
      const MT_Vector3 local = inverse_rotation * (wheel->NodeGetWorldPosition() - chassis_position);
      float radius = wheel_ctrl->GetRadius();
      if (!(radius > 0.01f)) {
        radius = 0.25f;
      }
      vehicle->AddWheel(wheel_ctrl->GetMotionState(),
                        local,
                        down_direction,
                        axle_direction,
                        0.2f,
                        radius,
                        true);
    }

    const int handle = vehicle->GetUserConstraintId();
    vehicles_[handle] = vehicle;
    chassis_[handle] = chassis;
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
        /* The engine has no native slider; a locked generic 6DOF with one
         * free linear axis is the closest representable constraint. */
        engine_type = PHY_GENERIC_6DOF_CONSTRAINT;
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
      case PHY_GENERIC_6DOF_CONSTRAINT:
        if (type == KX_PHYS_CONSTRAINT_SLIDER) {
          /* params[0] and params[1] are the X travel limits; every other
           * linear/angular axis is locked at zero. */
          constraint->SetParam(0, params[0], params[1]);
          constraint->SetParam(1, 0.0f, 0.0f);
          constraint->SetParam(2, 0.0f, 0.0f);
          constraint->SetParam(3, 0.0f, 0.0f);
          constraint->SetParam(4, 0.0f, 0.0f);
          constraint->SetParam(5, 0.0f, 0.0f);
        }
        else {
          /* params = [loX, hiX, loY, hiY, loZ, hiZ]. */
          constraint->SetParam(0, params[0], params[1]);
          constraint->SetParam(1, params[2], params[3]);
          constraint->SetParam(2, params[4], params[5]);
        }
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
    if (!navmesh->BuildNavMesh()) {
      SetError(error, "navmesh rebuild failed");
      return false;
    }
    fprintf(stderr,
            "[BuildNavMesh] rebuilt Detour static navmesh; agent radius %.2f / height %.2f "
            "are not applied because the static build has no Recast rasterization step\n",
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
