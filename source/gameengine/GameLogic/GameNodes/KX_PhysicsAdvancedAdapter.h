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

/** \file KX_PhysicsAdvancedAdapter.h
 *  \ingroup gamelogic
 *
 *  B6.1 advanced physics services (vehicles, constraints, body tuning, navmesh
 *  queries and batched raycasts).
 *
 *  This adapter is deliberately separate from #KX_PhysicsAdapter so B6.1 never
 *  has to touch the shared physics adapter block. Nodes talk only to this
 *  interface; the engine implementation lives in KX_PhysicsAdvancedAdapter.cpp.
 *
 *  Every method has a neutral default, so the null and mock adapters are thin
 *  subclasses and headless builds never dereference a missing scene service.
 *  A failure is always reported through the optional @a error string; no method
 *  silently succeeds when the engine refused the request.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

class KX_GameObject;
class KX_Scene;

/** Constraint type ids used by AddConstraint. Mirrors the editor enum order. */
enum KX_PhysicsConstraintType : int {
  KX_PHYS_CONSTRAINT_POINT = 0,
  KX_PHYS_CONSTRAINT_HINGE = 1,
  KX_PHYS_CONSTRAINT_SLIDER = 2,
  KX_PHYS_CONSTRAINT_CONE = 3,
  KX_PHYS_CONSTRAINT_GENERIC = 4,
};

/** One ray of a batch query. */
struct KX_PhysicsBatchHit {
  bool hit = false;
  /** Hit object, never owned by the result. */
  KX_GameObject *object = nullptr;
  float point[3] = {0.0f, 0.0f, 0.0f};
};

class KX_PhysicsAdvancedAdapter {
 public:
  virtual ~KX_PhysicsAdvancedAdapter() = default;

  /**
   * Create a raycast vehicle for @a chassis.
   *
   * @a wheel_names is a comma separated list of wheel object names. When it is
   * empty the legacy convention is used: @a wheel_template names an object in
   * the scene and every child of that object that owns a physics controller
   * becomes a wheel. Explicit names are resolved in order and every name must
   * exist; a missing name fails the whole call instead of silently building a
   * vehicle with fewer wheels.
   *
   * @return a positive vehicle handle, or -1 on failure. On failure @a error
   *         contains a diagnostic suitable for stderr.
   */
  virtual int CreateVehicle(KX_GameObject *chassis,
                            const std::string &wheel_names,
                            const std::string &wheel_template,
                            std::string *error)
  {
    (void)chassis;
    (void)wheel_names;
    (void)wheel_template;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return -1;
  }

  /** Apply steering/throttle/brake to every wheel of @a vehicle. */
  virtual bool VehicleControl(int vehicle, float steering, float throttle, float brake, std::string *error)
  {
    (void)vehicle;
    (void)steering;
    (void)throttle;
    (void)brake;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }

  /**
   * Create a two body constraint.
   *
   * @a type is a #KX_PhysicsConstraintType value, @a params has six entries
   * whose meaning depends on the type (see the B6.1 documentation).
   *
   * @return the engine constraint id, or 0 on failure.
   */
  virtual int AddConstraint(KX_GameObject *object_a,
                            KX_GameObject *object_b,
                            int type,
                            const float params[6],
                            std::string *error)
  {
    (void)object_a;
    (void)object_b;
    (void)type;
    (void)params;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return 0;
  }

  /** Remove every constraint created through this adapter that involves @a object. */
  virtual int RemoveConstraints(KX_GameObject *object, std::string *error)
  {
    (void)object;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return 0;
  }

  /** Create a hinge around @a pivot with @a axis. */
  virtual bool AddJoint(KX_GameObject *object_a,
                        KX_GameObject *object_b,
                        const float pivot[3],
                        const float axis[3],
                        std::string *error)
  {
    (void)object_a;
    (void)object_b;
    (void)pivot;
    (void)axis;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }

  virtual bool SetMass(KX_GameObject *object, float mass)
  {
    (void)object;
    (void)mass;
    return false;
  }
  virtual bool SetFriction(KX_GameObject *object, float friction)
  {
    (void)object;
    (void)friction;
    return false;
  }
  virtual bool SetRestitution(KX_GameObject *object, float restitution, std::string *error)
  {
    (void)object;
    (void)restitution;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }
  virtual bool SetLinearDamping(KX_GameObject *object, float damping)
  {
    (void)object;
    (void)damping;
    return false;
  }
  virtual bool SetAngularDamping(KX_GameObject *object, float damping)
  {
    (void)object;
    (void)damping;
    return false;
  }
  virtual bool SetGravity(const float gravity[3])
  {
    (void)gravity;
    return false;
  }

  /**
   * Rebuild the scene navmesh.
   *
   * Blender's Detour static navmesh is used; agent radius/height are validated
   * and logged but the current static build has no Recast rasterization step,
   * so they cannot change the result. A caller that needs a different agent
   * size has to rebuild the navmesh asset, not this node.
   */
  virtual bool BuildNavMesh(float agent_radius, float agent_height, std::string *error)
  {
    (void)agent_radius;
    (void)agent_height;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }

  /**
   * Query a path on the scene navmesh.
   *
   * @a waypoints is filled with flat xyz triples. An empty result is a
   * successful query that found no path; only a missing navmesh or a failure
   * returns false.
   */
  virtual bool QueryPath(const float from[3],
                         const float to[3],
                         std::vector<float> &waypoints,
                         std::string *error)
  {
    (void)from;
    (void)to;
    (void)waypoints;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }

  /**
   * Run a batch of raycasts.
   *
   * @a origins and @a dirs are flat xyz arrays; pairs = min(size/3 of both).
   * @a ignore is the graph owner, matching the single Raycast nodes.
   */
  virtual bool RaycastBatch(KX_GameObject *ignore,
                            const std::vector<float> &origins,
                            const std::vector<float> &dirs,
                            float max_dist,
                            std::vector<KX_PhysicsBatchHit> &hits,
                            std::string *error)
  {
    (void)ignore;
    (void)origins;
    (void)dirs;
    (void)max_dist;
    (void)hits;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }

  virtual bool SetCollisionGroup(KX_GameObject *object, int group, int mask, std::string *error)
  {
    (void)object;
    (void)group;
    (void)mask;
    if (error != nullptr) {
      *error = "physics advanced adapter unavailable";
    }
    return false;
  }
};

/** Deterministic adapter used by headless contract tests. */
class KX_MockPhysicsAdvancedAdapter : public KX_PhysicsAdvancedAdapter {
 public:
  struct ConstraintCall {
    KX_GameObject *a = nullptr;
    KX_GameObject *b = nullptr;
    int type = 0;
    float params[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int id = 0;
  };

  KX_GameObject *vehicle_chassis = nullptr;
  std::string vehicle_template;
  std::string vehicle_wheel_names;
  std::vector<std::string> vehicle_wheels;
  std::vector<int> vehicles;
  int next_vehicle = 1;

  std::vector<ConstraintCall> constraints;
  std::vector<std::pair<int, KX_GameObject *>> owners;
  int next_constraint = 1;

  float last_steering = 0.0f;
  float last_throttle = 0.0f;
  float last_brake = 0.0f;
  int last_vehicle = -1;

  float last_mass = 0.0f;
  float last_friction = 0.0f;
  float last_restitution = 0.0f;
  float last_linear_damping = 0.0f;
  float last_angular_damping = 0.0f;
  float last_gravity[3] = {0.0f, 0.0f, 0.0f};

  float last_agent_radius = 0.0f;
  float last_agent_height = 0.0f;
  std::vector<float> path_waypoints;

  std::vector<KX_PhysicsBatchHit> batch_hits;
  std::vector<float> last_batch_origins;
  std::vector<float> last_batch_dirs;
  float last_batch_distance = 0.0f;

  int last_group = 0;
  int last_mask = 0;

  uint32_t create_vehicle_calls = 0;
  uint32_t vehicle_control_calls = 0;
  uint32_t add_constraint_calls = 0;
  uint32_t remove_constraint_calls = 0;
  uint32_t add_joint_calls = 0;
  uint32_t set_mass_calls = 0;
  uint32_t set_friction_calls = 0;
  uint32_t set_restitution_calls = 0;
  uint32_t set_linear_damping_calls = 0;
  uint32_t set_angular_damping_calls = 0;
  uint32_t set_gravity_calls = 0;
  uint32_t build_navmesh_calls = 0;
  uint32_t query_path_calls = 0;
  uint32_t raycast_batch_calls = 0;
  uint32_t set_collision_group_calls = 0;

  int CreateVehicle(KX_GameObject *chassis,
                    const std::string &wheel_names,
                    const std::string &wheel_template,
                    std::string *error) override;
  bool VehicleControl(int vehicle, float steering, float throttle, float brake, std::string *error) override;
  int AddConstraint(KX_GameObject *object_a,
                    KX_GameObject *object_b,
                    int type,
                    const float params[6],
                    std::string *error) override;
  int RemoveConstraints(KX_GameObject *object, std::string *error) override;
  bool AddJoint(KX_GameObject *object_a,
                KX_GameObject *object_b,
                const float pivot[3],
                const float axis[3],
                std::string *error) override;
  bool SetMass(KX_GameObject *object, float mass) override;
  bool SetFriction(KX_GameObject *object, float friction) override;
  bool SetRestitution(KX_GameObject *object, float restitution, std::string *error) override;
  bool SetLinearDamping(KX_GameObject *object, float damping) override;
  bool SetAngularDamping(KX_GameObject *object, float damping) override;
  bool SetGravity(const float gravity[3]) override;
  bool BuildNavMesh(float agent_radius, float agent_height, std::string *error) override;
  bool QueryPath(const float from[3],
                 const float to[3],
                 std::vector<float> &waypoints,
                 std::string *error) override;
  bool RaycastBatch(KX_GameObject *ignore,
                    const std::vector<float> &origins,
                    const std::vector<float> &dirs,
                    float max_dist,
                    std::vector<KX_PhysicsBatchHit> &hits,
                    std::string *error) override;
  bool SetCollisionGroup(KX_GameObject *object, int group, int mask, std::string *error) override;

  void Reset();
};

/** Neutral adapter: every call reports failure with a diagnostic. */
KX_PhysicsAdvancedAdapter &KX_NullPhysicsAdvancedAdapter();

/** Owning neutral adapter for KX_AdapterSet. */
std::unique_ptr<KX_PhysicsAdvancedAdapter> KX_CreateNullPhysicsAdvancedAdapter();

/** Engine backed adapter, or null when the build has no physics environment. */
KX_PhysicsAdvancedAdapter *KX_CreateEnginePhysicsAdvancedAdapter(KX_Scene *scene);
