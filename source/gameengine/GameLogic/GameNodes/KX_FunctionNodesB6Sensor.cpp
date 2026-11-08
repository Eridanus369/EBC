/* ***** BEGIN GPL LICENSE BLOCK *****
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

/** \file KX_FunctionNodesB6Sensor.cpp
 *  \ingroup gamelogic
 *
 *  B6.3 extended sensors. RadarExtended and AreaSensor query the scene through
 *  the shape-aware proximity adapter, which tests real collision shapes and
 *  falls back to origin distance when a node exceeds its per-frame budget or
 *  an object has no collider. LineOfSight and RaycastSensor use KX_RayCast
 *  through the physics adapter.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"

#define B6S_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}
#define B6S_COUNTS(array) static_cast<uint32_t>(B6S_ARRAY_LEN(array))
#define B6S_ACTION(id, ins, outs, exec, state_type) \
  { id, ins, B6S_COUNTS(ins), outs, B6S_COUNTS(outs), exec, nullptr, nullptr, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    false, false, false }

namespace {

const int kAreaMaxOccupants = 64;
const float kAreaEpsilon = 0.05f;

std::string B6SensorInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

float B6SensorDistance(const float a[3], const float b[3])
{
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void B6WarnShapeFallback(bool &warned, const char *node_name)
{
  if (warned) {
    return;
  }
  warned = true;
  std::fprintf(stderr,
               "[%s] shape overlap query exceeded %d objects, using origin distance\n",
               node_name,
               KX_PROXIMITY_SHAPE_BUDGET);
}

struct B6ShapeWarnState {
  bool warned;
};

/* ------------------------------------------------------------------------- */
/* RadarExtended                                                              */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kRadarExtendedInputs[] = {
    TP("OnEnter"),
};
const KX_FuncPort kRadarExtendedOutputs[] = {
    TP("OnFound"),
    FP("FoundObjects", KX_VALUE_LIST),
    FP("Closest", KX_VALUE_OBJECT),
    FP("Distance", KX_VALUE_FLOAT),
};

void FuncRadarExtended(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  /* The config blob stores scalars, so the cone axis arrives as three floats
   * rather than a vector array. */
  float axis[3] = {node.ConfigFloat("axis_x", 1.0f),
                   node.ConfigFloat("axis_y", 0.0f),
                   node.ConfigFloat("axis_z", 0.0f)};
  const float radius = node.ConfigFloat("radius", 10.0f);
  const float angle = node.ConfigFloat("angle", 0.0f);
  const std::string property = node.ConfigString("property");

  B6ShapeWarnState *state = node.StateBlock<B6ShapeWarnState>(ctx);
  KX_ProximityShapeQuery query;
  query.origin = ctx.Owner();
  query.shape = (angle > 0.0f && angle < 360.0f) ? KX_PROXIMITY_CONE : KX_PROXIMITY_SPHERE;
  query.radius = radius;
  query.angle_deg = angle;
  for (int i = 0; i < 3; ++i) {
    query.local_axis[i] = axis[i];
  }
  query.property = property;

  std::vector<KX_ProximityHit> hits;
  const bool degraded = ctx.SceneQuery().QueryProximityShapes(query, hits);
  if (degraded) {
    B6WarnShapeFallback(state->warned, "RadarExtended");
  }

  KX_ValueList &list = ctx.Pool().AllocList(hits.size());
  for (const KX_ProximityHit &hit : hits) {
    if (hit.object != nullptr) {
      list.push_back(KX_RuntimeValue::Object(hit.object));
    }
  }

  const bool found = !list.empty();
  node.OutTrigger(ctx, 0, found);
  node.Out(ctx, 1, KX_RuntimeValue::List(&list));
  if (found) {
    const KX_ProximityHit &closest = hits.front();
    node.OutObject(ctx, 2, closest.object);
    node.OutFloat(ctx, 3, closest.distance);
  }
  else {
    node.OutObject(ctx, 2, nullptr);
    node.OutFloat(ctx, 3, 0.0f);
  }
}

/* ------------------------------------------------------------------------- */
/* LineOfSight                                                                */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLineOfSightInputs[] = {
    TP("OnEnter"),
    FP("Target", KX_VALUE_OBJECT),
    FP("IgnoreMask", KX_VALUE_INT),
};
const KX_FuncPort kLineOfSightOutputs[] = {
    TP("OnVisible"),
    TP("OnHidden"),
    FP("Distance", KX_VALUE_FLOAT),
};

void FuncLineOfSight(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *owner = ctx.Owner();
  KX_GameObject *target = node.InObject(ctx, 1);
  const int ignore_mask = node.InInt(ctx, 2);

  node.OutTrigger(ctx, 0, false);
  node.OutTrigger(ctx, 1, false);

  if (owner == nullptr || target == nullptr) {
    node.OutFloat(ctx, 2, 0.0f);
    node.OutTrigger(ctx, 1, true);
    return;
  }

  float origin[3];
  float target_position[3];
  if (!ctx.Objects().GetWorldPosition(owner, origin) ||
      !ctx.Objects().GetWorldPosition(target, target_position)) {
    node.OutFloat(ctx, 2, 0.0f);
    node.OutTrigger(ctx, 1, true);
    return;
  }

  const float distance = B6SensorDistance(origin, target_position);
  node.OutFloat(ctx, 2, distance);
  if (distance <= 0.0001f) {
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float direction[3] = {target_position[0] - origin[0],
                        target_position[1] - origin[1],
                        target_position[2] - origin[2]};
  for (int axis = 0; axis < 3; ++axis) {
    direction[axis] /= distance;
  }

  bool visible = false;
  float cursor[3] = {origin[0], origin[1], origin[2]};
  float remaining = distance;
  /* IgnoreMask semantics: bits set in the mask are collision groups the line
   * of sight passes through. Each masked hit advances the ray past the hit
   * point, up to a bounded number of steps. */
  for (int step = 0; step < 8; ++step) {
    KX_RaycastResult result = ctx.Physics().Raycast(cursor, direction, remaining, owner);
    if (!result.hit || result.object == nullptr) {
      visible = true;
      break;
    }
    if (result.object == target) {
      visible = true;
      break;
    }
    if (ignore_mask != 0 && (ctx.Objects().GetCollisionGroup(result.object) & ignore_mask) != 0) {
      const float advance = B6SensorDistance(cursor, result.point) + 0.05f;
      remaining -= advance;
      for (int axis = 0; axis < 3; ++axis) {
        cursor[axis] = result.point[axis] + direction[axis] * 0.05f;
      }
      if (remaining <= 0.0f) {
        visible = true;
        break;
      }
      continue;
    }
    visible = false;
    break;
  }

  node.OutTrigger(ctx, 0, visible);
  node.OutTrigger(ctx, 1, !visible);
}

/* ------------------------------------------------------------------------- */
/* AreaSensor                                                                 */
/* ------------------------------------------------------------------------- */

struct B6AreaState {
  KX_GameObject *occupants[kAreaMaxOccupants];
  int count;
  bool warned;
};

inline void B6AreaClear(B6AreaState &state)
{
  state.count = 0;
  for (int i = 0; i < kAreaMaxOccupants; ++i) {
    state.occupants[i] = nullptr;
  }
}

struct B6AreaSignature {
};

const KX_FuncPort kAreaSensorInputs[] = {
    TP("OnEnter"),
    FP("AreaName", KX_VALUE_STRING),
};
const KX_FuncPort kAreaSensorOutputs[] = {
    TP("OnEnterArea"),
    TP("OnLeaveArea"),
    FP("Occupants", KX_VALUE_LIST),
};

void FuncAreaSensor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  B6AreaState *state = node.StateBlock<B6AreaState>(ctx);
  const std::string area_name = B6SensorInString(node, ctx, 1);
  KX_GameObject *area = area_name.empty() ? nullptr : ctx.SceneQuery().FindObject(area_name);

  node.OutTrigger(ctx, 0, false);
  node.OutTrigger(ctx, 1, false);

  if (area == nullptr) {
    if (state->count > 0) {
      node.OutTrigger(ctx, 1, true);
    }
    B6AreaClear(*state);
    KX_ValueList &empty_list = ctx.Pool().AllocList(0);
    node.Out(ctx, 2, KX_RuntimeValue::List(&empty_list));
    return;
  }

  float centre[3];
  float scale[3];
  if (!ctx.Objects().GetWorldPosition(area, centre) ||
      !ctx.Objects().GetWorldScale(area, scale)) {
    B6AreaClear(*state);
    KX_ValueList &empty_list = ctx.Pool().AllocList(0);
    node.Out(ctx, 2, KX_RuntimeValue::List(&empty_list));
    return;
  }

  float half[3];
  for (int axis = 0; axis < 3; ++axis) {
    half[axis] = std::max(std::fabs(scale[axis]) * 0.5f, 0.5f);
  }
  const float radius = std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]) * 1.05f;

  KX_ProximityShapeQuery query;
  query.origin = area;
  query.shape = KX_PROXIMITY_BOX;
  query.radius = radius;
  for (int axis = 0; axis < 3; ++axis) {
    query.half_extents[axis] = half[axis];
  }

  std::vector<KX_ProximityHit> hits;
  const bool degraded = ctx.SceneQuery().QueryProximityShapes(query, hits);
  if (degraded) {
    B6WarnShapeFallback(state->warned, "AreaSensor");
  }

  KX_GameObject *current[kAreaMaxOccupants];
  int current_count = 0;
  KX_ValueList &list = ctx.Pool().AllocList(hits.size());
  for (const KX_ProximityHit &hit : hits) {
    if (hit.object == nullptr || hit.object == area) {
      continue;
    }
    float position[3];
    if (!ctx.Objects().GetWorldPosition(hit.object, position)) {
      continue;
    }
    /* Hits without a shape test (no collider, or degraded budget) keep the
     * legacy origin-in-box rule. Real shape overlaps are already exact. */
    if (!hit.shape_overlap &&
        (std::fabs(position[0] - centre[0]) > half[0] + kAreaEpsilon ||
         std::fabs(position[1] - centre[1]) > half[1] + kAreaEpsilon ||
         std::fabs(position[2] - centre[2]) > half[2] + kAreaEpsilon)) {
      continue;
    }
    if (current_count >= kAreaMaxOccupants) {
      break;
    }
    current[current_count++] = hit.object;
    list.push_back(KX_RuntimeValue::Object(hit.object));
  }

  bool entered = false;
  for (int i = 0; i < current_count; ++i) {
    bool was_inside = false;
    for (int j = 0; j < state->count; ++j) {
      if (state->occupants[j] == current[i]) {
        was_inside = true;
        break;
      }
    }
    if (!was_inside) {
      entered = true;
    }
  }

  bool left = false;
  for (int j = 0; j < state->count; ++j) {
    bool still_inside = false;
    for (int i = 0; i < current_count; ++i) {
      if (current[i] == state->occupants[j]) {
        still_inside = true;
        break;
      }
    }
    if (!still_inside) {
      left = true;
    }
  }

  node.OutTrigger(ctx, 0, entered);
  node.OutTrigger(ctx, 1, left);
  node.Out(ctx, 2, KX_RuntimeValue::List(&list));

  state->count = current_count;
  for (int i = 0; i < current_count; ++i) {
    state->occupants[i] = current[i];
  }
  for (int i = current_count; i < kAreaMaxOccupants; ++i) {
    state->occupants[i] = nullptr;
  }
}

/* ------------------------------------------------------------------------- */
/* RaycastSensor                                                              */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kRaycastSensorInputs[] = {
    TP("OnEnter"),
    FP("Direction", KX_VALUE_VECTOR),
    FP("Length", KX_VALUE_FLOAT),
};
const KX_FuncPort kRaycastSensorOutputs[] = {
    TP("OnHit"),
    TP("OnMiss"),
    FP("HitObject", KX_VALUE_OBJECT),
    FP("HitPoint", KX_VALUE_VECTOR),
};

void FuncRaycastSensor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float origin[3];
  if (ctx.Owner() == nullptr || !ctx.Objects().GetWorldPosition(ctx.Owner(), origin)) {
    node.OutTrigger(ctx, 0, false);
    node.OutTrigger(ctx, 1, true);
    node.OutObject(ctx, 2, nullptr);
    node.OutVector(ctx, 3, origin[0], origin[1], origin[2]);
    return;
  }

  float direction[3];
  node.In(ctx, 1).VectorTo(direction);
  const float length = node.InFloat(ctx, 2);
  KX_RaycastResult result = ctx.Physics().Raycast(origin, direction, length, ctx.Owner());

  node.OutTrigger(ctx, 0, result.hit);
  node.OutTrigger(ctx, 1, !result.hit);
  node.OutObject(ctx, 2, result.hit ? result.object : nullptr);
  node.OutVector(ctx, 3, result.point[0], result.point[1], result.point[2]);
}

/* ------------------------------------------------------------------------- */
/* RaycastAll                                                                 */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kRaycastAllInputs[] = {
    TP("OnEnter"),
    FP("Direction", KX_VALUE_VECTOR),
    FP("Length", KX_VALUE_FLOAT),
};
const KX_FuncPort kRaycastAllOutputs[] = {
    TP("OnHit"),
    TP("OnMiss"),
    FP("Objects", KX_VALUE_LIST),
    FP("Distances", KX_VALUE_LIST),
    FP("Count", KX_VALUE_INT),
};

float B6RayHitDistance(const float origin[3], const float point[3])
{
  const float dx = point[0] - origin[0];
  const float dy = point[1] - origin[1];
  const float dz = point[2] - origin[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void FuncRaycastAll(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_ValueList &objects = ctx.Pool().AllocList(0);
  KX_ValueList &distances = ctx.Pool().AllocList(0);

  float origin[3] = {0.0f, 0.0f, 0.0f};
  if (ctx.Owner() == nullptr || !ctx.Objects().GetWorldPosition(ctx.Owner(), origin)) {
    node.OutTrigger(ctx, 0, false);
    node.OutTrigger(ctx, 1, true);
    node.Out(ctx, 2, KX_RuntimeValue::List(&objects));
    node.Out(ctx, 3, KX_RuntimeValue::List(&distances));
    node.OutInt(ctx, 4, 0);
    return;
  }

  float direction[3];
  node.InVector(ctx, 1, direction);
  const float length = node.InFloat(ctx, 2);

  std::vector<KX_RaycastResult> hits;
  ctx.Physics().RaycastAll(origin, direction, length, ctx.Owner(), hits);

  /* Drop empty entries, then order nearest first. The contract is part of the
   * adapter API (KX_PhysicsAdapter::RaycastAll); sorting here keeps it true even
   * for a backend that forgot to sort. Ties keep backend order (stable_sort). */
  std::vector<KX_RaycastResult> ordered;
  ordered.reserve(hits.size());
  for (const KX_RaycastResult &hit : hits) {
    if (hit.hit && hit.object != nullptr) {
      ordered.push_back(hit);
    }
  }
  std::stable_sort(ordered.begin(),
                   ordered.end(),
                   [&origin](const KX_RaycastResult &a, const KX_RaycastResult &b) {
                     return B6RayHitDistance(origin, a.point) <
                            B6RayHitDistance(origin, b.point);
                   });

  for (const KX_RaycastResult &hit : ordered) {
    objects.push_back(KX_RuntimeValue::Object(hit.object));
    distances.push_back(KX_RuntimeValue::Float(B6RayHitDistance(origin, hit.point)));
  }

  const bool found = !objects.empty();
  node.OutTrigger(ctx, 0, found);
  node.OutTrigger(ctx, 1, !found);
  node.Out(ctx, 2, KX_RuntimeValue::List(&objects));
  node.Out(ctx, 3, KX_RuntimeValue::List(&distances));
  node.OutInt(ctx, 4, int(objects.size()));
}

/* ------------------------------------------------------------------------- */
/* ProximitySensor                                                            */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kProximitySensorInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Distance", KX_VALUE_FLOAT),
};
const KX_FuncPort kProximitySensorOutputs[] = {
    TP("OnNear"),
    TP("OnFar"),
    FP("DistanceValue", KX_VALUE_FLOAT),
};

void FuncProximitySensor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutTrigger(ctx, 0, false);
  node.OutTrigger(ctx, 1, false);

  KX_GameObject *owner = ctx.Owner();
  KX_GameObject *target = node.InObject(ctx, 0);
  if (owner == nullptr || target == nullptr) {
    node.OutFloat(ctx, 2, 0.0f);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  float a[3];
  float b[3];
  if (!ctx.Objects().GetWorldPosition(owner, a) || !ctx.Objects().GetWorldPosition(target, b)) {
    node.OutFloat(ctx, 2, 0.0f);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  const float distance = B6SensorDistance(a, b);
  node.OutFloat(ctx, 2, distance);
  const bool near = distance <= node.InFloat(ctx, 1);
  node.OutTrigger(ctx, 0, near);
  node.OutTrigger(ctx, 1, !near);
}

const KX_FunctionSpec kB6SensorSpecs[] = {
    B6S_ACTION("RadarExtended",
               kRadarExtendedInputs,
               kRadarExtendedOutputs,
               FuncRadarExtended,
               B6ShapeWarnState),
    B6S_ACTION("LineOfSight",
               kLineOfSightInputs,
               kLineOfSightOutputs,
               FuncLineOfSight,
               B6AreaSignature),
    B6S_ACTION("AreaSensor", kAreaSensorInputs, kAreaSensorOutputs, FuncAreaSensor, B6AreaState),
    B6S_ACTION("RaycastSensor",
               kRaycastSensorInputs,
               kRaycastSensorOutputs,
               FuncRaycastSensor,
               B6AreaSignature),
    B6S_ACTION("RaycastAll",
               kRaycastAllInputs,
               kRaycastAllOutputs,
               FuncRaycastAll,
               B6AreaSignature),
    B6S_ACTION("ProximitySensor",
               kProximitySensorInputs,
               kProximitySensorOutputs,
               FuncProximitySensor,
               B6AreaSignature),
};

KX_NodeSignature MakeB6SensorSignature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    port.kind = spec.inputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.inputs[i].trigger ? KX_VALUE_NONE : spec.inputs[i].type;
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    port.kind = spec.outputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.outputs[i].trigger ? KX_VALUE_NONE : spec.outputs[i].type;
    signature.outputs.push_back(port);
  }
  signature.state_size = spec.state_size;
  signature.state_alignment = spec.state_alignment;
  signature.needs_tick = spec.needs_tick || spec.tick != nullptr;
  signature.breaks_trigger_cycles = spec.breaks_trigger_cycles;
  signature.ports_from_ir = spec.ports_from_ir;
  return signature;
}

}  // namespace

void RegisterB6SensorFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6SensorSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6Sensor_") + spec.type_id,
        MakeB6SensorSignature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
