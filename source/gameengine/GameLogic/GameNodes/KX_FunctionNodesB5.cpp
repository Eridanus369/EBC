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

/** \file KX_FunctionNodesB5.cpp
 *  \ingroup gamelogic
 *
 *  B5.2 node family: smooth transforms, mouse look, axis alignment and
 *  property/material operators. All nodes are table driven; the state blocks
 *  keep the per-instance interpolation and property cache data.
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_GameObject.h"
#include "KX_PropertyAccessor.h"
#include "KX_RuntimeValue.h"
#include "MT_Matrix3x3.h"
#include "MT_Quaternion.h"
#include "MT_Vector3.h"

#define B5_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

#define FP(name, type) \
  { name, type, false }
#define TP(name) \
  { name, KX_VALUE_NONE, true }
#define B5_COUNTS(array) static_cast<uint32_t>(B5_ARRAY_LEN(array))

#define B5_ACTION(id, ins, outs, exec) \
  { id, ins, B5_COUNTS(ins), outs, B5_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, false, false, false }

#define B5_TICK_STATE(id, ins, outs, exec, tick, state_type) \
  { id, ins, B5_COUNTS(ins), outs, B5_COUNTS(outs), exec, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    false, true, false }

namespace {

const float kPi = 3.14159265358979323846f;
const float kReachEpsilon = 0.01f;
const float kDegToRad = kPi / 180.0f;

struct B5TransformState {
  bool active = false;
  bool done = false;
  bool warned = false;
  float last_target[3] = {0.0f, 0.0f, 0.0f};
};

struct B5MouseLookState {
  bool active = false;
  bool initialized = false;
  bool warned = false;
  float roll = 0.0f;
  float pitch = 0.0f;
  float yaw = 0.0f;
};

struct B5PropertyState {
  KX_PropertyCacheEntry cache;
};

bool IsFiniteVector(const float value[3])
{
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

std::string InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

void ReportOnce(bool *flag, const char *message)
{
  if (*flag) {
    return;
  }
  *flag = true;
  std::fprintf(stderr, "%s\n", message);
}

std::string ObjectLabel(KX_GameObject *object)
{
  return object != nullptr ? object->GetName() : std::string("<null>");
}

/**
 * Resolve the destination of MoveTo. `local` means parent space; unparented
 * objects treat local and world space as identical.
 */
bool ResolveMoveTarget(KX_ObjectAdapter &objects,
                       KX_GameObject *object,
                       const float target[3],
                       bool local,
                       MT_Vector3 &out)
{
  out = MT_Vector3(target);
  if (!local) {
    return true;
  }
  KX_GameObject *parent = objects.GetParent(object);
  if (parent == nullptr) {
    return true;
  }
  float parent_position[3];
  float parent_orientation[3];
  float parent_scale[3];
  if (!objects.GetWorldPosition(parent, parent_position)
      || !objects.GetWorldOrientation(parent, parent_orientation)
      || !objects.GetWorldScale(parent, parent_scale)) {
    return false;
  }
  const MT_Vector3 scaled(target[0] * parent_scale[0],
                          target[1] * parent_scale[1],
                          target[2] * parent_scale[2]);
  out = MT_Vector3(parent_position) + MT_Matrix3x3(MT_Vector3(parent_orientation)) * scaled;
  return true;
}

/** Rotation taking `from` onto `to`. Zero-length inputs are reported. */
bool RotationBetween(const MT_Vector3 &from,
                     const MT_Vector3 &to,
                     MT_Quaternion &out,
                     const char *node_name)
{
  if (from.length() < 1e-6f || to.length() < 1e-6f) {
    std::fprintf(stderr, "[%s] degenerate vector (axis or target is zero)\n", node_name);
    return false;
  }
  const MT_Vector3 a = from.normalized();
  const MT_Vector3 b = to.normalized();
  const MT_Scalar dot = a.dot(b);
  if (dot > 1.0f - 1e-6f) {
    out = MT_Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
  }
  if (dot < -1.0f + 1e-6f) {
    /* Opposite vectors: pick a stable perpendicular axis. */
    MT_Vector3 axis = MT_Vector3(0.0f, 0.0f, 1.0f).cross(a);
    if (axis.length() < 1e-6f) {
      axis = MT_Vector3(1.0f, 0.0f, 0.0f);
    }
    out = MT_Quaternion(axis.normalized(), kPi);
    return true;
  }
  MT_Vector3 axis = a.cross(b);
  out = MT_Quaternion(axis.normalized(), std::acos(dot));
  return true;
}

/* ---------------------------------------------------------------- RotateTo */

const KX_FuncPort kRotateToInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Target", KX_VALUE_VECTOR),
    FP("Speed", KX_VALUE_FLOAT),
};
const KX_FuncPort kRotateToOutputs[] = {
    TP("OnDone"),
};

void FuncRotateToEnter(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B5TransformState *state = node.StateBlock<B5TransformState>(ctx);
  if (state == nullptr) {
    return;
  }
  float target[3];
  node.InVector(ctx, 2, target);
  const bool same = state->active && IsFiniteVector(target)
      && std::fabs(target[0] - state->last_target[0]) <= 1e-4f
      && std::fabs(target[1] - state->last_target[1]) <= 1e-4f
      && std::fabs(target[2] - state->last_target[2]) <= 1e-4f;
  state->active = true;
  if (!same) {
    state->done = false;
    state->warned = false;
    state->last_target[0] = target[0];
    state->last_target[1] = target[1];
    state->last_target[2] = target[2];
  }
}

void FuncRotateToTick(KX_FunctionNode &node,
                      KX_ExecutionContext &ctx,
                      float delta_time)
{
  B5TransformState *state = node.StateBlock<B5TransformState>(ctx);
  if (state == nullptr || !state->active || state->done) {
    return;
  }
  KX_ObjectAdapter &objects = ctx.Objects();
  KX_GameObject *object = node.InObject(ctx, 1);
  if (object == nullptr) {
    ReportOnce(&state->warned, "[RotateTo] Object input is empty");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float target[3];
  node.InVector(ctx, 2, target);
  const float speed = node.InFloat(ctx, 3);
  if (!IsFiniteVector(target)) {
    std::fprintf(stderr, "[RotateTo] target is not finite on object '%s'\n",
                 ObjectLabel(object).c_str());
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (!(speed > 0.0f) || !std::isfinite(speed)) {
    std::fprintf(stderr, "[RotateTo] Speed=%g is not a positive rate on object '%s'\n",
                 static_cast<double>(speed), ObjectLabel(object).c_str());
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float position[3];
  float orientation[3];
  if (!objects.GetWorldPosition(object, position)
      || !objects.GetWorldOrientation(object, orientation)) {
    ReportOnce(&state->warned, "[RotateTo] object transform is unavailable");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  MT_Vector3 direction;
  if (node.ConfigBool("local", false)) {
    KX_GameObject *parent = objects.GetParent(object);
    if (parent != nullptr) {
      float parent_orientation[3];
      if (!objects.GetWorldOrientation(parent, parent_orientation)) {
        ReportOnce(&state->warned, "[RotateTo] parent transform is unavailable");
        state->done = true;
        node.OutTrigger(ctx, 0, true);
        return;
      }
      direction = MT_Matrix3x3(MT_Vector3(parent_orientation)) * MT_Vector3(target);
    }
    else {
      direction = MT_Vector3(target);
    }
  }
  else {
    direction = MT_Vector3(target) - MT_Vector3(position);
  }

  MT_Quaternion rotation;
  if (!RotationBetween(MT_Vector3(0.0f, 1.0f, 0.0f), direction, rotation, "RotateTo")) {
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  const MT_Quaternion current =
      MT_Matrix3x3(MT_Vector3(orientation[0], orientation[1], orientation[2])).getRotation();
  const MT_Scalar remaining = current.angle(rotation);
  if (!std::isfinite(remaining)) {
    ReportOnce(&state->warned, "[RotateTo] rotation angle is not finite");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  MT_Quaternion next = rotation;
  bool reached = remaining <= kReachEpsilon;
  if (!reached) {
    const MT_Scalar step = static_cast<MT_Scalar>(speed) * static_cast<MT_Scalar>(delta_time);
    const MT_Scalar fraction = step >= remaining ? 1.0f : step / remaining;
    next = current.slerp(rotation, fraction);
    reached = (remaining - step) <= kReachEpsilon;
  }

  MT_Matrix3x3 matrix(next);
  float yaw;
  float pitch;
  float roll;
  matrix.getEuler(yaw, pitch, roll);
  const float euler[3] = {yaw, pitch, roll};
  objects.SetWorldOrientation(object, euler);

  if (reached) {
    state->done = true;
    node.OutTrigger(ctx, 0, true);
  }
}

/* ------------------------------------------------------------------ MoveTo */

const KX_FuncPort kMoveToInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Target", KX_VALUE_VECTOR),
    FP("Speed", KX_VALUE_FLOAT),
};
const KX_FuncPort kMoveToOutputs[] = {
    TP("OnDone"),
};

void FuncMoveToEnter(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B5TransformState *state = node.StateBlock<B5TransformState>(ctx);
  if (state == nullptr) {
    return;
  }
  float target[3];
  node.InVector(ctx, 2, target);
  const bool same = state->active && IsFiniteVector(target)
      && std::fabs(target[0] - state->last_target[0]) <= 1e-4f
      && std::fabs(target[1] - state->last_target[1]) <= 1e-4f
      && std::fabs(target[2] - state->last_target[2]) <= 1e-4f;
  state->active = true;
  if (!same) {
    state->done = false;
    state->warned = false;
    state->last_target[0] = target[0];
    state->last_target[1] = target[1];
    state->last_target[2] = target[2];
  }
}

void FuncMoveToTick(KX_FunctionNode &node,
                    KX_ExecutionContext &ctx,
                    float delta_time)
{
  B5TransformState *state = node.StateBlock<B5TransformState>(ctx);
  if (state == nullptr || !state->active || state->done) {
    return;
  }
  KX_ObjectAdapter &objects = ctx.Objects();
  KX_GameObject *object = node.InObject(ctx, 1);
  if (object == nullptr) {
    ReportOnce(&state->warned, "[MoveTo] Object input is empty");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float target[3];
  node.InVector(ctx, 2, target);
  const float speed = node.InFloat(ctx, 3);
  if (!IsFiniteVector(target)) {
    std::fprintf(stderr, "[MoveTo] target is not finite on object '%s'\n",
                 ObjectLabel(object).c_str());
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (!(speed > 0.0f) || !std::isfinite(speed)) {
    std::fprintf(stderr, "[MoveTo] Speed=%g is not a positive rate on object '%s'\n",
                 static_cast<double>(speed), ObjectLabel(object).c_str());
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float position[3];
  if (!objects.GetWorldPosition(object, position)) {
    ReportOnce(&state->warned, "[MoveTo] world position is unavailable");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }
  MT_Vector3 destination;
  if (!ResolveMoveTarget(objects, object, target, node.ConfigBool("local", false), destination)) {
    ReportOnce(&state->warned, "[MoveTo] parent transform is unavailable");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  const MT_Vector3 current(position);
  const MT_Vector3 delta = destination - current;
  const MT_Scalar distance = delta.length();
  if (!std::isfinite(distance)) {
    ReportOnce(&state->warned, "[MoveTo] distance is not finite");
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (distance <= kReachEpsilon) {
    const float final_position[3] = {destination[0], destination[1], destination[2]};
    objects.SetWorldPosition(object, final_position);
    state->done = true;
    node.OutTrigger(ctx, 0, true);
    return;
  }

  const MT_Scalar step = static_cast<MT_Scalar>(speed) * static_cast<MT_Scalar>(delta_time);
  const MT_Scalar fraction = step >= distance ? 1.0f : step / distance;
  const MT_Vector3 next = current + delta * fraction;
  const float next_position[3] = {next[0], next[1], next[2]};
  objects.SetWorldPosition(object, next_position);

  if (distance - step <= kReachEpsilon) {
    state->done = true;
    node.OutTrigger(ctx, 0, true);
  }
}

/* --------------------------------------------------------------- MouseLook */

const KX_FuncPort kMouseLookInputs[] = {
    TP("OnEnter"),
    FP("Sensitivity", KX_VALUE_FLOAT),
    FP("MinPitch", KX_VALUE_FLOAT),
    FP("MaxPitch", KX_VALUE_FLOAT),
};
const KX_FuncPort kMouseLookOutputs[] = {
    TP("OnUpdate"),
};

/** MouseLook rotates the object owning the tree, falling back to the active camera. */
KX_GameObject *ResolveMouseLookCamera(KX_ExecutionContext &ctx)
{
  KX_GameObject *owner = ctx.Owner();
  if (owner != nullptr) {
    return owner;
  }
  return ctx.SceneQuery().ActiveCamera();
}

void FuncMouseLookEnter(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B5MouseLookState *state = node.StateBlock<B5MouseLookState>(ctx);
  if (state == nullptr) {
    return;
  }
  state->active = true;
  if (state->initialized) {
    return;
  }
  KX_GameObject *camera = ResolveMouseLookCamera(ctx);
  if (camera == nullptr) {
    return;
  }
  float orientation[3];
  if (ctx.Objects().GetWorldOrientation(camera, orientation)) {
    state->pitch = orientation[0];
    state->roll = orientation[1];
    state->yaw = orientation[2];
    state->initialized = true;
  }
}

void FuncMouseLookTick(KX_FunctionNode &node,
                       KX_ExecutionContext &ctx,
                       float delta_time)
{
  (void)delta_time;
  B5MouseLookState *state = node.StateBlock<B5MouseLookState>(ctx);
  if (state == nullptr || !state->active) {
    return;
  }
  KX_GameObject *camera = ResolveMouseLookCamera(ctx);
  if (camera == nullptr) {
    ReportOnce(&state->warned, "[MouseLook] no owner object and no active camera");
    return;
  }
  if (!state->initialized) {
    float orientation[3];
    if (ctx.Objects().GetWorldOrientation(camera, orientation)) {
      state->pitch = orientation[0];
      state->roll = orientation[1];
      state->yaw = orientation[2];
      state->initialized = true;
    }
  }

  float dx = 0.0f;
  float dy = 0.0f;
  ctx.Input().MouseDelta(dx, dy);

  float sensitivity = node.InFloat(ctx, 1);
  if (!std::isfinite(sensitivity) || sensitivity == 0.0f) {
    sensitivity = 1.0f;
  }
  float min_pitch = node.InFloat(ctx, 2);
  float max_pitch = node.InFloat(ctx, 3);
  if (!std::isfinite(min_pitch)) {
    min_pitch = -89.0f;
  }
  if (!std::isfinite(max_pitch)) {
    max_pitch = 89.0f;
  }
  if (min_pitch > max_pitch) {
    std::swap(min_pitch, max_pitch);
  }
  const float min_rad = min_pitch * kDegToRad;
  const float max_rad = max_pitch * kDegToRad;

  const float sign_x = node.ConfigBool("invert_x", false) ? -1.0f : 1.0f;
  const float sign_y = node.ConfigBool("invert_y", false) ? -1.0f : 1.0f;
  state->yaw += sign_x * dx * sensitivity * kDegToRad;
  state->pitch += sign_y * dy * sensitivity * kDegToRad;
  state->pitch = std::max(min_rad, std::min(max_rad, state->pitch));

  const float euler[3] = {state->pitch, state->roll, state->yaw};
  ctx.Objects().SetWorldOrientation(camera, euler);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------- AlignAxisToVector */

const KX_FuncPort kAlignAxisInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Axis", KX_VALUE_VECTOR),
    FP("Target", KX_VALUE_VECTOR),
};
const KX_FuncPort kAlignAxisOutputs[] = {
    TP("OnDone"),
};

void FuncAlignAxisToVector(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = node.InObject(ctx, 1);
  float axis[3];
  float target[3];
  node.InVector(ctx, 2, axis);
  node.InVector(ctx, 3, target);

  if (object == nullptr) {
    std::fprintf(stderr, "[AlignAxisToVector] Object input is empty\n");
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (!IsFiniteVector(axis) || MT_Vector3(axis).length() < 1e-6f) {
    std::fprintf(stderr,
                 "[AlignAxisToVector] Axis is zero or not finite on object '%s'\n",
                 ObjectLabel(object).c_str());
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (!IsFiniteVector(target) || MT_Vector3(target).length() < 1e-6f) {
    std::fprintf(stderr,
                 "[AlignAxisToVector] Target is zero or not finite on object '%s'\n",
                 ObjectLabel(object).c_str());
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float orientation[3];
  if (!ctx.Objects().GetWorldOrientation(object, orientation)) {
    std::fprintf(stderr, "[AlignAxisToVector] orientation unavailable on object '%s'\n",
                 ObjectLabel(object).c_str());
    node.OutTrigger(ctx, 0, true);
    return;
  }

  const MT_Quaternion current =
      MT_Matrix3x3(MT_Vector3(orientation[0], orientation[1], orientation[2])).getRotation();
  const MT_Vector3 world_axis =
      MT_Matrix3x3(current) * MT_Vector3(axis).normalized();
  MT_Quaternion delta;
  if (!RotationBetween(world_axis, MT_Vector3(target), delta, "AlignAxisToVector")) {
    node.OutTrigger(ctx, 0, true);
    return;
  }
  const MT_Quaternion next = delta * current;

  MT_Matrix3x3 matrix(next);
  float yaw;
  float pitch;
  float roll;
  matrix.getEuler(yaw, pitch, roll);
  const float euler[3] = {yaw, pitch, roll};
  ctx.Objects().SetWorldOrientation(object, euler);
  node.OutTrigger(ctx, 0, true);
}

/* --------------------------------------------------- ObjectPropertyOperator */

const KX_FuncPort kPropertyOperatorInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Property", KX_VALUE_STRING),
    FP("Op", KX_VALUE_STRING),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kPropertyOperatorOutputs[] = {
    FP("Result", KX_VALUE_FLOAT),
    TP("OnDone"),
};

std::string Lowercase(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

KX_RuntimeValue ApplyScalarOp(const std::string &op,
                              float a,
                              float b,
                              bool *ok)
{
  *ok = true;
  if (op == "add" || op == "+") {
    return KX_RuntimeValue::Float(a + b);
  }
  if (op == "sub" || op == "-") {
    return KX_RuntimeValue::Float(a - b);
  }
  if (op == "mul" || op == "*") {
    return KX_RuntimeValue::Float(a * b);
  }
  if (op == "div" || op == "/") {
    if (std::fabs(b) < 1e-9f) {
      *ok = false;
      return KX_RuntimeValue::Float(a);
    }
    return KX_RuntimeValue::Float(a / b);
  }
  if (op == "min") {
    return KX_RuntimeValue::Float(std::min(a, b));
  }
  if (op == "max") {
    return KX_RuntimeValue::Float(std::max(a, b));
  }
  *ok = false;
  return KX_RuntimeValue::Float(a);
}

KX_RuntimeValue ApplyIntOp(const std::string &op,
                           int a,
                           int b,
                           bool *ok)
{
  *ok = true;
  if (op == "add" || op == "+") {
    return KX_RuntimeValue::Int(a + b);
  }
  if (op == "sub" || op == "-") {
    return KX_RuntimeValue::Int(a - b);
  }
  if (op == "mul" || op == "*") {
    return KX_RuntimeValue::Int(a * b);
  }
  if (op == "div" || op == "/") {
    if (b == 0) {
      *ok = false;
      return KX_RuntimeValue::Int(a);
    }
    return KX_RuntimeValue::Int(a / b);
  }
  if (op == "min") {
    return KX_RuntimeValue::Int(std::min(a, b));
  }
  if (op == "max") {
    return KX_RuntimeValue::Int(std::max(a, b));
  }
  *ok = false;
  return KX_RuntimeValue::Int(a);
}

KX_RuntimeValue ApplyVectorOp(const std::string &op,
                              const float a[3],
                              const float b[3],
                              bool *ok)
{
  *ok = true;
  float out[3] = {a[0], a[1], a[2]};
  if (op == "add" || op == "+") {
    for (int i = 0; i < 3; ++i) {
      out[i] = a[i] + b[i];
    }
  }
  else if (op == "sub" || op == "-") {
    for (int i = 0; i < 3; ++i) {
      out[i] = a[i] - b[i];
    }
  }
  else if (op == "mul" || op == "*") {
    for (int i = 0; i < 3; ++i) {
      out[i] = a[i] * b[i];
    }
  }
  else if (op == "div" || op == "/") {
    for (int i = 0; i < 3; ++i) {
      if (std::fabs(b[i]) < 1e-9f) {
        *ok = false;
        return KX_RuntimeValue::Vector(a[0], a[1], a[2]);
      }
    }
    for (int i = 0; i < 3; ++i) {
      out[i] = a[i] / b[i];
    }
  }
  else if (op == "min" || op == "max") {
    const bool use_min = op == "min";
    for (int i = 0; i < 3; ++i) {
      out[i] = use_min ? std::min(a[i], b[i]) : std::max(a[i], b[i]);
    }
  }
  else {
    *ok = false;
    return KX_RuntimeValue::Vector(a[0], a[1], a[2]);
  }
  return KX_RuntimeValue::Vector(out[0], out[1], out[2]);
}

void FuncObjectPropertyOperator(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B5PropertyState *state = node.StateBlock<B5PropertyState>(ctx);
  KX_GameObject *object = node.InObject(ctx, 1);
  const std::string property = InString(node, ctx, 2);
  const std::string op = Lowercase(InString(node, ctx, 3));
  const KX_RuntimeValue incoming = node.In(ctx, 4);

  auto finish = [&](const KX_RuntimeValue &result) {
    node.Out(ctx, 0, result);
    node.OutTrigger(ctx, 1, true);
  };

  if (object == nullptr || property.empty()) {
    std::fprintf(stderr, "[ObjectPropertyOperator] missing object or property name\n");
    finish(incoming);
    return;
  }

  KX_PropertyAccessor accessor(ctx.PropertyBackend());
  const KX_ValueType type = state != nullptr
      ? accessor.Resolve(object, property, &state->cache)
      : KX_VALUE_NONE;
  if (type == KX_VALUE_NONE) {
    std::fprintf(stderr,
                 "[ObjectPropertyOperator] property '%s' not found on object '%s'\n",
                 property.c_str(),
                 ObjectLabel(object).c_str());
    finish(incoming);
    return;
  }

  KX_PropertyCacheEntry scratch;
  KX_PropertyCacheEntry *cache = state != nullptr ? &state->cache : &scratch;
  KX_RuntimeValue current;
  if (!accessor.Read(object, property, cache, type, &current)) {
    std::fprintf(stderr,
                 "[ObjectPropertyOperator] cannot read property '%s' on object '%s'\n",
                 property.c_str(),
                 ObjectLabel(object).c_str());
    finish(incoming);
    return;
  }

  bool ok = true;
  KX_RuntimeValue result = current;
  switch (type) {
    case KX_VALUE_FLOAT:
      result = ApplyScalarOp(op, current.AsFloat(), incoming.AsFloat(), &ok);
      break;
    case KX_VALUE_INT:
      result = ApplyIntOp(op, current.AsInt(), incoming.AsInt(), &ok);
      break;
    case KX_VALUE_BOOL: {
      const bool value = ApplyIntOp(op, current.AsBool() ? 1 : 0, incoming.AsBool() ? 1 : 0, &ok)
                             .AsBool();
      result = KX_RuntimeValue::Bool(value);
      break;
    }
    case KX_VALUE_VECTOR: {
      float a[3];
      float b[3];
      current.VectorTo(a);
      incoming.VectorTo(b);
      result = ApplyVectorOp(op, a, b, &ok);
      break;
    }
    default:
      std::fprintf(stderr,
                   "[ObjectPropertyOperator] unsupported property type %d for '%s' (op '%s')\n",
                   static_cast<int>(type),
                   property.c_str(),
                   op.c_str());
      finish(incoming);
      return;
  }

  if (!ok) {
    std::fprintf(stderr,
                 "[ObjectPropertyOperator] op '%s' failed (unknown op or division by zero) "
                 "on property '%s' of object '%s'\n",
                 op.c_str(),
                 property.c_str(),
                 ObjectLabel(object).c_str());
    finish(current);
    return;
  }

  const KX_PropertyAccessor::WriteResult written =
      accessor.Write(object, property, result, false, cache);
  if (written != KX_PropertyAccessor::WriteResult::Written
      && written != KX_PropertyAccessor::WriteResult::Created) {
    std::fprintf(stderr,
                 "[ObjectPropertyOperator] write to property '%s' on object '%s' failed\n",
                 property.c_str(),
                 ObjectLabel(object).c_str());
  }
  finish(result);
}

/* --------------------------------------------------------------- SetMaterial */

const KX_FuncPort kSetMaterialInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Material", KX_VALUE_STRING),
};
const KX_FuncPort kSetMaterialOutputs[] = {
    TP("OnDone"),
};

void FuncSetMaterial(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = node.InObject(ctx, 1);
  const std::string material = InString(node, ctx, 2);
  if (object == nullptr || material.empty()) {
    std::fprintf(stderr, "[SetMaterial] missing object or material name\n");
    node.OutTrigger(ctx, 0, true);
    return;
  }
  ctx.Objects().SetMaterial(object, material, node.ConfigInt("slot", 0));
  node.OutTrigger(ctx, 0, true);
}

const KX_FunctionSpec kB5Specs[] = {
    B5_TICK_STATE("RotateTo",
                  kRotateToInputs,
                  kRotateToOutputs,
                  FuncRotateToEnter,
                  FuncRotateToTick,
                  B5TransformState),
    B5_TICK_STATE("MoveTo",
                  kMoveToInputs,
                  kMoveToOutputs,
                  FuncMoveToEnter,
                  FuncMoveToTick,
                  B5TransformState),
    B5_TICK_STATE("MouseLook",
                  kMouseLookInputs,
                  kMouseLookOutputs,
                  FuncMouseLookEnter,
                  FuncMouseLookTick,
                  B5MouseLookState),
    B5_ACTION("AlignAxisToVector",
              kAlignAxisInputs,
              kAlignAxisOutputs,
              FuncAlignAxisToVector),
    { "ObjectPropertyOperator", kPropertyOperatorInputs, B5_COUNTS(kPropertyOperatorInputs),
      kPropertyOperatorOutputs, B5_COUNTS(kPropertyOperatorOutputs),
      FuncObjectPropertyOperator, nullptr, nullptr, sizeof(B5PropertyState),
      alignof(B5PropertyState), false, false, false, nullptr, nullptr, nullptr, true },
    B5_ACTION("SetMaterial", kSetMaterialInputs, kSetMaterialOutputs, FuncSetMaterial),
};

}  // namespace

namespace {

KX_NodeSignature MakeB5Signature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    if (spec.inputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.inputs[i].type;
    }
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    if (spec.outputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.outputs[i].type;
    }
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

void RegisterB5FunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB5Specs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB5_") + spec.type_id,
        MakeB5Signature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
