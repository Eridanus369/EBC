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

/** \file KX_ExecutionContextPhysics.cpp
 *  \ingroup gamelogic
 *
 *  Physics backing of #KX_ExecutionContext::Raycast.
 *
 *  This is a thin adapter over the existing KX_RayCast query. The P1 goal is to
 *  expose a stable raycast result to the Raycast game node without reworking
 *  the physics engine or retaining any controller handle.
 */

#include "KX_ExecutionContext.h"

#include "KX_ClientObjectInfo.h"
#include "KX_GameObject.h"
#include "KX_RayCast.h"
#include "KX_Scene.h"
#include "MT_Vector3.h"
#include "PHY_IPhysicsController.h"
#include "PHY_IPhysicsEnvironment.h"

namespace {

class GameNodeRayCastCallback final : public KX_RayCast {
 public:
  KX_GameObject *hit_object = nullptr;

  explicit GameNodeRayCastCallback(PHY_IPhysicsController *ignore_controller)
      : KX_RayCast(ignore_controller, true, false)
  {
  }

  bool RayHit(KX_ClientObjectInfo *client) override
  {
    hit_object = (client != nullptr) ? client->m_gameobject : nullptr;
    /* Stop at the first accepted hit. Multi hit queries belong to a later
     * phase; the node only exposes a single result. */
    return true;
  }
};

}  // namespace

KX_RaycastResult KX_ExecutionContext::Raycast(const float origin_data[3],
                                             const float direction_data[3],
                                             float max_dist)
{
  KX_RaycastResult result;

  KX_Scene *scene = Scene();
  if (scene == nullptr) {
    return result;
  }

  PHY_IPhysicsEnvironment *environment = scene->GetPhysicsEnvironment();
  if (environment == nullptr) {
    return result;
  }

  const MT_Vector3 origin(origin_data[0], origin_data[1], origin_data[2]);
  const MT_Vector3 direction(direction_data[0], direction_data[1], direction_data[2]);
  if (direction.length2() < 1e-12f) {
    return result;
  }

  const MT_Vector3 target = origin + direction.safe_normalized() * max_dist;

  /* Ignore the graph owner's own controller so a node on an object never
   * reports hitting itself. The callback does not store the controller. */
  PHY_IPhysicsController *ignore_controller = nullptr;
  KX_GameObject *owner = Owner();
  if (owner != nullptr) {
    ignore_controller = owner->GetPhysicsController();
  }

  GameNodeRayCastCallback callback(ignore_controller);
  if (!KX_RayCast::RayTest(environment, origin, target, callback)) {
    return result;
  }
  if (!callback.m_hitFound || callback.hit_object == nullptr) {
    return result;
  }

  result.hit = true;
  result.object = callback.hit_object;
  result.point[0] = callback.m_hitPoint[0];
  result.point[1] = callback.m_hitPoint[1];
  result.point[2] = callback.m_hitPoint[2];
  result.normal[0] = callback.m_hitNormal[0];
  result.normal[1] = callback.m_hitNormal[1];
  result.normal[2] = callback.m_hitNormal[2];
  return result;
}
