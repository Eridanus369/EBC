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

/** \file KX_FunctionNodesB6.cpp
 *  \ingroup gamelogic
 *
 *  B6.1 node family: advanced physics (vehicles, constraints, body tuning,
 *  navmesh and batched raycasts) and advanced animation. Table driven; one
 *  execute dispatcher keeps every node's failure diagnostic explicit.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"
#include "KX_ValuePool.h"

#define B6_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

#define TP(name) \
  { name, KX_VALUE_NONE, true }
#define FP(name, type) \
  { name, type, false }

namespace {

void Fail(KX_FunctionNode &node, const char *message)
{
  fprintf(stderr, "[%s] %s\n", node.Spec().type_id, message);
}

bool ReadParams(KX_FunctionNode &node, const std::string &text, float out[6])
{
  for (int i = 0; i < 6; ++i) {
    out[i] = 0.0f;
  }
  if (text.empty()) {
    return true;
  }
  if (std::sscanf(text.c_str(), "%f,%f,%f,%f,%f,%f",
                  &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) != 6) {
    Fail(node, "params must contain six comma separated numbers");
    return false;
  }
  return true;
}

/** Optional "x,y,z" config value. Empty text means "not set" without an error. */
bool ReadOptionalVector3(KX_FunctionNode &node,
                         const std::string &text,
                         float out[3],
                         bool *present)
{
  *present = false;
  out[0] = out[1] = out[2] = 0.0f;
  if (text.empty()) {
    return true;
  }
  if (std::sscanf(text.c_str(), "%f,%f,%f", &out[0], &out[1], &out[2]) != 3) {
    Fail(node, "expected three comma separated numbers");
    return false;
  }
  *present = true;
  return true;
}

/**
 * P-3.5 per-wheel suspension config: "s,d,c[,rest];..." in wheel order.
 * A single group is broadcast by the adapter; a missing rest length keeps the
 * historical 0.2 m default.
 */
bool ReadSuspension(KX_FunctionNode &node, const std::string &text, std::vector<float> *out)
{
  out->clear();
  if (text.empty()) {
    return true;
  }
  size_t start = 0;
  while (true) {
    const size_t end = text.find(';', start);
    const std::string group =
        text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    float values[4] = {0.0f, 0.0f, 0.0f, 0.2f};
    const int count =
        std::sscanf(group.c_str(), "%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3]);
    if (count < 3) {
      Fail(node, "suspension groups need stiffness,damping,compression[,rest_length]");
      out->clear();
      return false;
    }
    out->insert(out->end(), values, values + 4);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return true;
}

KX_ValueList *ReadList(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index, const char *label)
{
  KX_ValueList *list = static_cast<KX_ValueList *>(node.In(ctx, index).AsList());
  if (list == nullptr) {
    fprintf(stderr, "[%s] %s is not a List\n", node.Spec().type_id, label);
  }
  return list;
}

/* ---------------------------------------------------------------- physics */

void FuncCreateVehicle(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *template_name = node.In(ctx, 2).AsString();
  const std::string wheel_names = node.ConfigString("wheel_names");
  const std::string suspension_text = node.ConfigString("suspension");
  std::vector<float> suspension;
  if (!ReadSuspension(node, suspension_text, &suspension)) {
    node.OutInt(ctx, 2, 0);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string error;
  const int handle = suspension.empty()
                         ? ctx.PhysicsAdvanced().CreateVehicle(
                               node.InObject(ctx, 1),
                               wheel_names,
                               template_name != nullptr ? std::string(template_name) : std::string(),
                               &error)
                         : ctx.PhysicsAdvanced().CreateVehicleWithSuspension(
                               node.InObject(ctx, 1),
                               wheel_names,
                               template_name != nullptr ? std::string(template_name) : std::string(),
                               suspension,
                               &error);
  node.OutInt(ctx, 2, handle);
  if (handle > 0) {
    node.OutTrigger(ctx, 0, true);
  }
  else {
    fprintf(stderr, "[CreateVehicle] %s\n", error.c_str());
    node.OutTrigger(ctx, 1, true);
  }
}

void FuncVehicleControl(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string error;
  if (!ctx.PhysicsAdvanced().VehicleControl(node.InInt(ctx, 1),
                                            node.InFloat(ctx, 2),
                                            node.InFloat(ctx, 3),
                                            node.InFloat(ctx, 4),
                                            &error))
  {
    fprintf(stderr, "[VehicleControl] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncAddConstraint(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string params_text = node.ConfigString("params");
  float params[6];
  if (!ReadParams(node, params_text, params)) {
    node.OutTrigger(ctx, 0, true);
    return;
  }
  float pivot[3];
  float axis[3];
  bool has_pivot = false;
  bool has_axis = false;
  if (!ReadOptionalVector3(node, node.ConfigString("pivot"), pivot, &has_pivot) ||
      !ReadOptionalVector3(node, node.ConfigString("axis"), axis, &has_axis)) {
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (!has_pivot) {
    const MT_Vector3 midpoint =
        (node.InObject(ctx, 1)->NodeGetWorldPosition() +
         node.InObject(ctx, 2)->NodeGetWorldPosition()) * 0.5f;
    pivot[0] = midpoint.x();
    pivot[1] = midpoint.y();
    pivot[2] = midpoint.z();
  }
  if (!has_axis) {
    axis[0] = 0.0f;
    axis[1] = 1.0f;
    axis[2] = 0.0f;
  }
  std::string error;
  const int constraint_id =
      (has_pivot || has_axis)
          ? ctx.PhysicsAdvanced().AddConstraintWithFrame(node.InObject(ctx, 1),
                                                         node.InObject(ctx, 2),
                                                         node.ConfigInt("type", 0),
                                                         params,
                                                         pivot,
                                                         axis,
                                                         &error)
          : ctx.PhysicsAdvanced().AddConstraint(node.InObject(ctx, 1),
                                                node.InObject(ctx, 2),
                                                node.ConfigInt("type", 0),
                                                params,
                                                &error);
  if (constraint_id == 0)
  {
    fprintf(stderr, "[AddConstraint] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncRemoveConstraint(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string error;
  const int removed = ctx.PhysicsAdvanced().RemoveConstraints(node.InObject(ctx, 1), &error);
  if (removed == 0) {
    fprintf(stderr, "[RemoveConstraint] %s\n",
            error.empty() ? "no runtime constraint owned by this object" : error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncAddJoint(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float pivot[3];
  float axis[3];
  node.InVector(ctx, 3, pivot);
  node.InVector(ctx, 4, axis);
  std::string error;
  if (!ctx.PhysicsAdvanced().AddJoint(node.InObject(ctx, 1), node.InObject(ctx, 2),
                                      pivot, axis, &error))
  {
    fprintf(stderr, "[AddJoint] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetMass(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  if (!ctx.PhysicsAdvanced().SetMass(node.InObject(ctx, 1), node.InFloat(ctx, 2))) {
    Fail(node, "object is null, has no physics controller, or mass <= 0");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetFriction(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  if (!ctx.PhysicsAdvanced().SetFriction(node.InObject(ctx, 1), node.InFloat(ctx, 2))) {
    Fail(node, "object is null, has no physics controller, or friction < 0");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetRestitution(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string error;
  if (!ctx.PhysicsAdvanced().SetRestitution(node.InObject(ctx, 1),
                                            node.InFloat(ctx, 2),
                                            &error))
  {
    fprintf(stderr, "[SetPhysicsRestitution] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetDamping(KX_FunctionNode &node, KX_ExecutionContext &ctx, bool angular)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  const float damping = node.InFloat(ctx, 2);
  const bool ok = angular ? ctx.PhysicsAdvanced().SetAngularDamping(object, damping)
                          : ctx.PhysicsAdvanced().SetLinearDamping(object, damping);
  if (!ok) {
    Fail(node, "object is null, has no physics controller, or damping < 0");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncBuildNavMesh(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string error;
  if (ctx.PhysicsAdvanced().BuildNavMesh(node.ConfigFloat("agent_radius", 0.3f),
                                         node.ConfigFloat("agent_height", 1.8f),
                                         &error))
  {
    node.OutTrigger(ctx, 0, true);
  }
  else {
    fprintf(stderr, "[BuildNavMesh] %s\n", error.c_str());
    node.OutTrigger(ctx, 1, true);
  }
}

void FuncQueryPath(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float from[3];
  float to[3];
  node.InVector(ctx, 1, from);
  node.InVector(ctx, 2, to);
  std::vector<float> waypoints;
  std::string error;
  if (!ctx.PhysicsAdvanced().QueryPath(from, to, waypoints, &error)) {
    fprintf(stderr, "[QueryPath] %s\n", error.c_str());
    node.OutTrigger(ctx, 1, true);
    return;
  }
  KX_ValueList &list = ctx.Pool().AllocList(waypoints.size() / 3);
  for (size_t i = 0; i + 2 < waypoints.size(); i += 3) {
    list.push_back(KX_RuntimeValue::Vector(waypoints[i], waypoints[i + 1], waypoints[i + 2]));
  }
  node.Out(ctx, 2, KX_RuntimeValue::List(&list));
  if (waypoints.empty()) {
    fprintf(stderr, "[QueryPath] query succeeded but returned no waypoints\n");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncRaycastBatch(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_ValueList *origins = ReadList(node, ctx, 1, "Origins");
  KX_ValueList *dirs = ReadList(node, ctx, 2, "Dirs");
  if (origins == nullptr || dirs == nullptr) {
    return;
  }
  std::vector<float> flat_origins;
  std::vector<float> flat_dirs;
  for (const KX_RuntimeValue &value : *origins) {
    flat_origins.push_back(value.data.vec[0]);
    flat_origins.push_back(value.data.vec[1]);
    flat_origins.push_back(value.data.vec[2]);
  }
  for (const KX_RuntimeValue &value : *dirs) {
    flat_dirs.push_back(value.data.vec[0]);
    flat_dirs.push_back(value.data.vec[1]);
    flat_dirs.push_back(value.data.vec[2]);
  }

  std::vector<KX_PhysicsBatchHit> hits;
  std::string error;
  if (!ctx.PhysicsAdvanced().RaycastBatch(
          ctx.Owner(), flat_origins, flat_dirs, node.InFloat(ctx, 3), hits, &error))
  {
    fprintf(stderr, "[RaycastBatch] %s\n", error.c_str());
    return;
  }

  KX_ValueList &hit_list = ctx.Pool().AllocList(hits.size());
  KX_ValueList &point_list = ctx.Pool().AllocList(hits.size());
  for (const KX_PhysicsBatchHit &hit : hits) {
    hit_list.push_back(KX_RuntimeValue::Object(hit.object));
    point_list.push_back(KX_RuntimeValue::Vector(hit.point[0], hit.point[1], hit.point[2]));
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&hit_list));
  node.Out(ctx, 1, KX_RuntimeValue::List(&point_list));
}

void FuncSetCollisionGroup(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string error;
  if (!ctx.PhysicsAdvanced().SetCollisionGroup(node.InObject(ctx, 1),
                                                node.InInt(ctx, 2),
                                                node.InInt(ctx, 3),
                                                &error))
  {
    fprintf(stderr, "[SetCollisionGroup] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

/* -------------------------------------------------------------- animation */

void FuncPlayActionByLayer(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *name = node.In(ctx, 2).AsString();
  if (name == nullptr || !ctx.Animation().PlayActionByLayer(node.InObject(ctx, 1),
                                                            std::string(name),
                                                            node.InInt(ctx, 3),
                                                            node.InFloat(ctx, 4)))
  {
    Fail(node, "action name is empty or the object cannot play it");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncBlendActions(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *action_a = node.In(ctx, 2).AsString();
  const char *action_b = node.In(ctx, 3).AsString();
  if (action_a == nullptr || action_b == nullptr ||
      !ctx.Animation().BlendActions(node.InObject(ctx, 1),
                                    std::string(action_a),
                                    std::string(action_b),
                                    node.InFloat(ctx, 4)))
  {
    Fail(node, "both actions must exist and factor must be within [0, 1]");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetBoneTransform(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *bone = node.In(ctx, 2).AsString();
  float position[3];
  float rotation[3];
  node.InVector(ctx, 3, position);
  node.InVector(ctx, 4, rotation);
  /* 0 = Override (default, legacy behaviour), 1 = Additive (quaternion product against the
   * action-evaluated pose, never an euler addition, never accumulated). 2 = Multiplicative is
   * reserved for P2 and rejected by the adapter instead of silently falling back to Override. */
  const int mode = node.ConfigInt("mode", 0);
  if (bone == nullptr || !ctx.Animation().SetBoneTransform(node.InObject(ctx, 1),
                                                           std::string(bone),
                                                           position,
                                                           rotation,
                                                           mode))
  {
    Fail(node, "object must be an armature, the bone must exist and mode must be 0 or 1");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncClearBoneOverride(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *bone = node.In(ctx, 2).AsString();
  if (bone == nullptr || !ctx.Animation().ClearPoseOverride(node.InObject(ctx, 1), std::string(bone)))
  {
    /* Unknown bone / no override is a documented no-op; report it but keep the graph alive. */
    Fail(node, "object must be an armature and the bone must have a pose override");
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetBoneWeight(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *bone = node.In(ctx, 2).AsString();
  std::string error;
  if (bone == nullptr || !ctx.Animation().SetBoneWeight(node.InObject(ctx, 1),
                                                        std::string(bone),
                                                        node.InFloat(ctx, 3),
                                                        &error))
  {
    fprintf(stderr, "[SetBoneWeight] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncPlayBoneAction(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const char *bone = node.In(ctx, 2).AsString();
  const char *action = node.In(ctx, 3).AsString();
  std::string error;
  if (bone == nullptr || action == nullptr ||
      !ctx.Animation().PlayBoneAction(node.InObject(ctx, 1),
                                      std::string(bone),
                                      std::string(action),
                                      &error))
  {
    fprintf(stderr, "[PlayBoneAction] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

void FuncSetAnimationSpeed(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  if (!ctx.Animation().SetActionSpeed(node.InObject(ctx, 1),
                                      node.InInt(ctx, 2),
                                      node.InFloat(ctx, 3)))
  {
    Fail(node, "object has no active action on that layer");
  }
  node.OutTrigger(ctx, 0, true);
}

struct B6AnimEventState {
  KX_GameObject *object = nullptr;
  /* Raw state blocks are not constructed; keep strings fixed-size so no std::string ctor runs. */
  char event_name[64] = {};
  char action[128] = {};
  int frame = 0;
  int layer = -1;
  float previous_frame = 0.0f;
  bool armed = false;
  bool action_bound = false;
};

/* Bind the event to the layer currently playing `action`, and seed `previous_frame` so a
 * node armed after the event frame does not fire retroactively while a node armed on the
 * event frame still fires once. Returns false while the action is not playing. */
static bool BindAnimationEventAction(B6AnimEventState *state, KX_ExecutionContext &ctx)
{
  if (state->object == nullptr || state->action[0] == '\0') {
    return false;
  }
  const int layer = ctx.Animation().GetActionLayer(state->object, std::string(state->action));
  if (layer < 0) {
    state->action_bound = false;
    state->layer = -1;
    return false;
  }
  state->layer = layer;
  state->action_bound = true;
  const float current = ctx.Animation().GetActionFrameAtCurrentEngineTime(state->object, layer);
  const float event_frame = static_cast<float>(state->frame);
  state->previous_frame =
      (current >= event_frame && current < event_frame + 0.5f) ? current - 0.5f : current;
  return true;
}

void FuncAddAnimationEvent(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  B6AnimEventState *state = node.StateBlock<B6AnimEventState>(ctx);
  state->object = node.InObject(ctx, 1);
  const char *action = node.In(ctx, 2).AsString();
  state->frame = node.InInt(ctx, 3);
  const std::string event_name = node.ConfigString("event_name");
  state->event_name[0] = '\0';
  if (!event_name.empty()) {
    snprintf(state->event_name, sizeof(state->event_name), "%s", event_name.c_str());
  }
  state->action[0] = '\0';
  if (action != nullptr) {
    snprintf(state->action, sizeof(state->action), "%s", action);
  }
  state->layer = -1;
  state->action_bound = false;
  state->armed = state->object != nullptr && state->action[0] != '\0';
  if (state->armed) {
    BindAnimationEventAction(state, ctx);
  }
  else {
    Fail(node, "object or action input is empty");
  }
}

void TickAddAnimationEvent(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  B6AnimEventState *state = node.StateBlock<B6AnimEventState>(ctx);
  if (state->object == nullptr || state->action[0] == '\0') {
    return;
  }

  /* The graph ticks before the render-time action update; ask the adapter for the frame that
   * update will apply so the event lands on the frame the engine is about to show. Resolve the
   * action name on every tick so the event follows the layer it actually plays on. */
  const int layer = ctx.Animation().GetActionLayer(state->object, std::string(state->action));
  if (layer < 0) {
    state->action_bound = false;
    state->layer = -1;
    return;
  }
  const float current = ctx.Animation().GetActionFrameAtCurrentEngineTime(state->object, layer);
  const float event_frame = static_cast<float>(state->frame);
  if (!state->action_bound || layer != state->layer) {
    state->layer = layer;
    state->action_bound = true;
    state->previous_frame =
        (current >= event_frame && current < event_frame + 0.5f) ? current - 0.5f : current;
  }

  if (state->armed && state->previous_frame < event_frame && current >= event_frame) {
    state->armed = false;
    fprintf(stderr,
            "[AddAnimationEvent] fired '%s' at frame %.1f\n",
            state->event_name,
            current);
    node.PendingTrigger(ctx, 0);
  }
  else if (current < state->previous_frame) {
    /* The action wrapped (loop) or restarted. Re-arm so the event fires once per pass and
     * fire immediately when the wrap lands on the event frame (common for frame 0 events). */
    state->armed = true;
    if (current >= event_frame && current < event_frame + 0.5f) {
      state->armed = false;
      fprintf(stderr,
              "[AddAnimationEvent] fired '%s' at frame %.1f\n",
              state->event_name,
              current);
      node.PendingTrigger(ctx, 0);
    }
  }
  state->previous_frame = current;
}

void FuncGetAnimationFrame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = node.InObject(ctx, 0);
  const int layer = node.InInt(ctx, 1);
  if (object == nullptr) {
    node.OutFloat(ctx, 0, 0.0f);
    node.OutFloat(ctx, 1, 0.0f);
    return;
  }
  node.OutFloat(ctx, 0, ctx.Animation().GetActionFrame(object, layer));
  node.OutFloat(ctx, 1, ctx.Animation().GetActionNormalized(object, layer));
}

/* B6.9-A: separate time scale. The engine only exposes BL_Action::m_speed as a single
 * playback multiplier, so an independent time scale cannot be represented. The node is
 * registered so a graph fails with an explicit reason instead of silently aliasing speed. */
void FuncSetActionTimeScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string error;
  if (!ctx.Animation().SetActionTimeScale(node.InObject(ctx, 1),
                                          node.InInt(ctx, 2),
                                          node.InFloat(ctx, 3),
                                          &error))
  {
    fprintf(stderr, "[SetActionTimeScale] %s\n", error.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

/* B6.9-A: normalized playback position. Uses the engine's existing frame query through the
 * animation adapter; the value is clamped to [0, 1] and returns to 0 when the action loops. */
void FuncGetActionNormalizedTime(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = node.InObject(ctx, 0);
  const int layer = node.InInt(ctx, 1);
  if (object == nullptr) {
    node.OutFloat(ctx, 0, 0.0f);
    return;
  }
  node.OutFloat(ctx, 0, ctx.Animation().GetActionNormalized(object, layer));
}

/* B6.9-A: read back the local pose stored on a bone channel. Value node used by graphs (and by
 * runtime probes) to observe whether a manual SetBoneTransform survived the action pipeline. */
void FuncGetBoneTransform(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = node.InObject(ctx, 0);
  const char *bone = node.In(ctx, 1).AsString();
  float position[3] = {0.0f, 0.0f, 0.0f};
  float rotation[3] = {0.0f, 0.0f, 0.0f};
  if (object != nullptr && bone != nullptr) {
    ctx.Animation().GetBoneTransform(object, std::string(bone), position, rotation);
  }
  node.OutVector(ctx, 0, position[0], position[1], position[2]);
  node.OutVector(ctx, 1, rotation[0], rotation[1], rotation[2]);
}

void FuncSetLinearDamping(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  FuncSetDamping(node, ctx, false);
}

void FuncSetAngularDamping(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  FuncSetDamping(node, ctx, true);
}

/* ------------------------------------------------------------------- specs */

const KX_FuncPort kDoneOutputs[] = {TP("OnDone")};

const KX_FuncPort kCreateVehicleInputs[] = {
    TP("OnEnter"), FP("Chassis", KX_VALUE_OBJECT), FP("Template", KX_VALUE_STRING)};
const KX_FuncPort kCreateVehicleOutputs[] = {
    TP("OnSuccess"), TP("OnFail"), FP("Vehicle", KX_VALUE_INT)};

const KX_FuncPort kVehicleControlInputs[] = {
    TP("OnEnter"), FP("Vehicle", KX_VALUE_INT), FP("Steering", KX_VALUE_FLOAT),
    FP("Throttle", KX_VALUE_FLOAT), FP("Brake", KX_VALUE_FLOAT)};

const KX_FuncPort kAddConstraintInputs[] = {
    TP("OnEnter"), FP("ObjectA", KX_VALUE_OBJECT), FP("ObjectB", KX_VALUE_OBJECT)};

const KX_FuncPort kRemoveConstraintInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT)};

const KX_FuncPort kAddJointInputs[] = {
    TP("OnEnter"), FP("ObjectA", KX_VALUE_OBJECT), FP("ObjectB", KX_VALUE_OBJECT),
    FP("Pivot", KX_VALUE_VECTOR), FP("Axis", KX_VALUE_VECTOR)};

const KX_FuncPort kSetMassInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Mass", KX_VALUE_FLOAT)};
const KX_FuncPort kSetFrictionInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Friction", KX_VALUE_FLOAT)};
const KX_FuncPort kSetRestitutionInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Restitution", KX_VALUE_FLOAT)};
const KX_FuncPort kSetDampingInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Damping", KX_VALUE_FLOAT)};

const KX_FuncPort kBuildNavMeshInputs[] = {TP("OnEnter")};
const KX_FuncPort kBuildNavMeshOutputs[] = {TP("OnSuccess"), TP("OnFail")};

const KX_FuncPort kQueryPathInputs[] = {
    TP("OnEnter"), FP("From", KX_VALUE_VECTOR), FP("To", KX_VALUE_VECTOR)};
const KX_FuncPort kQueryPathOutputs[] = {
    TP("OnSuccess"), TP("OnFail"), FP("Waypoints", KX_VALUE_LIST)};

const KX_FuncPort kRaycastBatchInputs[] = {
    TP("OnEnter"), FP("Origins", KX_VALUE_LIST), FP("Dirs", KX_VALUE_LIST),
    FP("Distance", KX_VALUE_FLOAT)};
const KX_FuncPort kRaycastBatchOutputs[] = {
    FP("Hits", KX_VALUE_LIST), FP("Points", KX_VALUE_LIST)};

const KX_FuncPort kSetCollisionGroupInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Group", KX_VALUE_INT),
    FP("Mask", KX_VALUE_INT)};

const KX_FuncPort kPlayActionByLayerInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Action", KX_VALUE_STRING),
    FP("Layer", KX_VALUE_INT), FP("BlendIn", KX_VALUE_FLOAT)};

const KX_FuncPort kBlendActionsInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("ActionA", KX_VALUE_STRING),
    FP("ActionB", KX_VALUE_STRING), FP("Factor", KX_VALUE_FLOAT)};

const KX_FuncPort kSetBoneTransformInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Bone", KX_VALUE_STRING),
    FP("Position", KX_VALUE_VECTOR), FP("Rotation", KX_VALUE_VECTOR)};

const KX_FuncPort kSetBoneWeightInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Bone", KX_VALUE_STRING),
    FP("Weight", KX_VALUE_FLOAT)};

const KX_FuncPort kPlayBoneActionInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Bone", KX_VALUE_STRING),
    FP("Action", KX_VALUE_STRING)};

const KX_FuncPort kSetAnimationSpeedInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Layer", KX_VALUE_INT),
    FP("Speed", KX_VALUE_FLOAT)};

const KX_FuncPort kAddAnimationEventInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Action", KX_VALUE_STRING),
    FP("Frame", KX_VALUE_INT)};
const KX_FuncPort kAddAnimationEventOutputs[] = {TP("OnEvent")};

const KX_FuncPort kGetAnimationFrameInputs[] = {
    FP("Object", KX_VALUE_OBJECT), FP("Layer", KX_VALUE_INT)};
const KX_FuncPort kGetAnimationFrameOutputs[] = {
    FP("Frame", KX_VALUE_FLOAT), FP("Normalized", KX_VALUE_FLOAT)};

const KX_FuncPort kSetActionTimeScaleInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Layer", KX_VALUE_INT),
    FP("TimeScale", KX_VALUE_FLOAT)};

const KX_FuncPort kGetActionNormalizedTimeInputs[] = {
    FP("Object", KX_VALUE_OBJECT), FP("Layer", KX_VALUE_INT)};
const KX_FuncPort kGetActionNormalizedTimeOutputs[] = {
    FP("Normalized", KX_VALUE_FLOAT)};

const KX_FuncPort kGetBoneTransformInputs[] = {
    FP("Object", KX_VALUE_OBJECT), FP("Bone", KX_VALUE_STRING)};
const KX_FuncPort kGetBoneTransformOutputs[] = {
    FP("Position", KX_VALUE_VECTOR), FP("Rotation", KX_VALUE_VECTOR)};

/* B6.10 (appended): remove a pose override so the bone follows the action again. Without this
 * clear path an override triggered once could never be undone. */
const KX_FuncPort kClearBoneOverrideInputs[] = {
    TP("OnEnter"), FP("Object", KX_VALUE_OBJECT), FP("Bone", KX_VALUE_STRING)};

#define B6_SPEC(id, in, out, fn, tick, state, value_node) \
  { id, in, B6_ARRAY_LEN(in), out, B6_ARRAY_LEN(out), fn, nullptr, tick, state, \
    KX_RUNTIME_DATA_ALIGNMENT, value_node, tick != nullptr, false }

const KX_FunctionSpec kB6Specs[] = {
    B6_SPEC("CreateVehicle", kCreateVehicleInputs, kCreateVehicleOutputs,
            FuncCreateVehicle, nullptr, 0, false),
    B6_SPEC("VehicleControl", kVehicleControlInputs, kDoneOutputs,
            FuncVehicleControl, nullptr, 0, false),
    B6_SPEC("AddConstraint", kAddConstraintInputs, kDoneOutputs,
            FuncAddConstraint, nullptr, 0, false),
    B6_SPEC("RemoveConstraint", kRemoveConstraintInputs, kDoneOutputs,
            FuncRemoveConstraint, nullptr, 0, false),
    B6_SPEC("AddJoint", kAddJointInputs, kDoneOutputs,
            FuncAddJoint, nullptr, 0, false),
    B6_SPEC("SetPhysicsMass", kSetMassInputs, kDoneOutputs,
            FuncSetMass, nullptr, 0, false),
    B6_SPEC("SetPhysicsFriction", kSetFrictionInputs, kDoneOutputs,
            FuncSetFriction, nullptr, 0, false),
    B6_SPEC("SetPhysicsRestitution", kSetRestitutionInputs, kDoneOutputs,
            FuncSetRestitution, nullptr, 0, false),
    B6_SPEC("SetLinearDamping", kSetDampingInputs, kDoneOutputs,
            FuncSetLinearDamping, nullptr, 0, false),
    B6_SPEC("SetAngularDamping", kSetDampingInputs, kDoneOutputs,
            FuncSetAngularDamping, nullptr, 0, false),
    B6_SPEC("BuildNavMesh", kBuildNavMeshInputs, kBuildNavMeshOutputs,
            FuncBuildNavMesh, nullptr, 0, false),
    B6_SPEC("QueryPath", kQueryPathInputs, kQueryPathOutputs,
            FuncQueryPath, nullptr, 0, false),
    B6_SPEC("RaycastBatch", kRaycastBatchInputs, kRaycastBatchOutputs,
            FuncRaycastBatch, nullptr, 0, false),
    B6_SPEC("SetCollisionGroup", kSetCollisionGroupInputs, kDoneOutputs,
            FuncSetCollisionGroup, nullptr, 0, false),
    B6_SPEC("PlayActionByLayer", kPlayActionByLayerInputs, kDoneOutputs,
            FuncPlayActionByLayer, nullptr, 0, false),
    B6_SPEC("BlendActions", kBlendActionsInputs, kDoneOutputs,
            FuncBlendActions, nullptr, 0, false),
    B6_SPEC("SetBoneTransform", kSetBoneTransformInputs, kDoneOutputs,
            FuncSetBoneTransform, nullptr, 0, false),
    B6_SPEC("SetBoneWeight", kSetBoneWeightInputs, kDoneOutputs,
            FuncSetBoneWeight, nullptr, 0, false),
    B6_SPEC("PlayBoneAction", kPlayBoneActionInputs, kDoneOutputs,
            FuncPlayBoneAction, nullptr, 0, false),
    B6_SPEC("SetAnimationSpeed", kSetAnimationSpeedInputs, kDoneOutputs,
            FuncSetAnimationSpeed, nullptr, 0, false),
    B6_SPEC("AddAnimationEvent", kAddAnimationEventInputs, kAddAnimationEventOutputs,
            FuncAddAnimationEvent, TickAddAnimationEvent,
            sizeof(B6AnimEventState), false),
    B6_SPEC("GetAnimationFrame", kGetAnimationFrameInputs, kGetAnimationFrameOutputs,
            FuncGetAnimationFrame, nullptr, 0, true),
    /* B6.9-A (appended): explicit time-scale failure path and normalized playback query. */
    B6_SPEC("SetActionTimeScale", kSetActionTimeScaleInputs, kDoneOutputs,
            FuncSetActionTimeScale, nullptr, 0, false),
    B6_SPEC("GetActionNormalizedTime", kGetActionNormalizedTimeInputs,
            kGetActionNormalizedTimeOutputs, FuncGetActionNormalizedTime, nullptr, 0, true),
    /* B6.9-A (appended): bone pose read-back used to verify channel locking. */
    B6_SPEC("GetBoneTransform", kGetBoneTransformInputs, kGetBoneTransformOutputs,
            FuncGetBoneTransform, nullptr, 0, true),
    /* B6.10 (appended): clear a SetBoneTransform pose override (bone returns to the action). */
    B6_SPEC("ClearBoneOverride", kClearBoneOverrideInputs, kDoneOutputs,
            FuncClearBoneOverride, nullptr, 0, false),
};

KX_NodeSignature MakeB6Signature(const KX_FunctionSpec &spec)
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

void RegisterB6AdvancedFunctionGameNodes()
{
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;
  for (const KX_FunctionSpec &spec : kB6Specs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6_") + spec.type_id,
        MakeB6Signature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
