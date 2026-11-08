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

/** \file KX_FunctionNodesExtra.cpp
 *  \ingroup gamelogic
 *
 *  Second batch of table driven nodes. Port order mirrors the legacy editor
 *  sockets exactly so catalogued legacy nodes can reuse these runtime types.
 */

#include "KX_FunctionNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

std::string ExtraInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

/** Type dispatched equality: values of different types are never equal. */
bool RuntimeValuesEqual(const KX_RuntimeValue &a, const KX_RuntimeValue &b)
{
  if (a.type != b.type) {
    return false;
  }
  switch (a.type) {
    case KX_VALUE_NONE:
      return true;
    case KX_VALUE_BOOL:
      return a.data.b == b.data.b;
    case KX_VALUE_INT:
      return a.data.i == b.data.i;
    case KX_VALUE_FLOAT:
      return a.data.f == b.data.f;
    case KX_VALUE_VECTOR:
    case KX_VALUE_COLOR:
      return a.data.vec[0] == b.data.vec[0] && a.data.vec[1] == b.data.vec[1] &&
             a.data.vec[2] == b.data.vec[2] && a.data.vec[3] == b.data.vec[3];
    case KX_VALUE_STRING: {
      const char *left = a.AsString();
      const char *right = b.AsString();
      if (left == nullptr || right == nullptr) {
        return left == right;
      }
      return std::strcmp(left, right) == 0;
    }
    default:
      return a.data.ptr == b.data.ptr;
  }
}

/* ------------------------------------------------------------------------- */
/* Value nodes                                                                */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kVector3Inputs[] = {
    FP("X", KX_VALUE_FLOAT),
    FP("Y", KX_VALUE_FLOAT),
    FP("Z", KX_VALUE_FLOAT),
};
const KX_FuncPort kVectorOutput[] = {FP("Vector", KX_VALUE_VECTOR)};

void FuncVector3(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutVector(ctx, 0, node.InFloat(ctx, 0), node.InFloat(ctx, 1), node.InFloat(ctx, 2));
}

const KX_FuncPort kVectorSplit3Inputs[] = {FP("Vector", KX_VALUE_VECTOR)};
const KX_FuncPort kVectorSplit3Outputs[] = {
    FP("X", KX_VALUE_FLOAT),
    FP("Y", KX_VALUE_FLOAT),
    FP("Z", KX_VALUE_FLOAT),
};

void FuncVectorSplit3(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float value[3];
  node.InVector(ctx, 0, value);
  node.OutFloat(ctx, 0, value[0]);
  node.OutFloat(ctx, 1, value[1]);
  node.OutFloat(ctx, 2, value[2]);
}

const KX_FuncPort kValueInvertInputs[] = {FP("Value", KX_VALUE_FLOAT)};
const KX_FuncPort kFloatValueOutput[] = {FP("Value", KX_VALUE_FLOAT)};

void FuncValueInvert(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, -node.InFloat(ctx, 0));
}

const KX_FuncPort kLimitRangeInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("Threshold", KX_VALUE_VECTOR),
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};

void FuncLimitRange(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const float lo = node.InFloat(ctx, 2);
  const float hi = node.InFloat(ctx, 3);
  node.OutFloat(ctx, 0, std::max(std::min(lo, hi), std::min(std::max(lo, hi), value)));
}

const KX_FuncPort kMapRangeNodeInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("FromMin", KX_VALUE_FLOAT),
    FP("FromMax", KX_VALUE_FLOAT),
    FP("ToMin", KX_VALUE_FLOAT),
    FP("ToMax", KX_VALUE_FLOAT),
    FP("Steps", KX_VALUE_FLOAT),
    FP("ValueVector", KX_VALUE_VECTOR),
    FP("FromMinVector", KX_VALUE_VECTOR),
    FP("FromMaxVector", KX_VALUE_VECTOR),
    FP("ToMinVector", KX_VALUE_VECTOR),
    FP("ToMaxVector", KX_VALUE_VECTOR),
    FP("StepsVector", KX_VALUE_VECTOR),
};

void FuncMapRangeNode(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  bool clamp = node.ConfigBool("clamp", false);
  /* The disabled branch is still exported (fixed port indices are part of the
   * table contract). The mode config selects which branch the runtime reads:
   * "0" float, "1" vector. */
  if (node.ConfigString("mode") == "1") {
    float value[3];
    float from_min[3];
    float from_max[3];
    float to_min[3];
    float to_max[3];
    node.InVector(ctx, 6, value);
    node.InVector(ctx, 7, from_min);
    node.InVector(ctx, 8, from_max);
    node.InVector(ctx, 9, to_min);
    node.InVector(ctx, 10, to_max);
    float result[3];
    for (int i = 0; i < 3; ++i) {
      const float span = from_max[i] - from_min[i];
      result[i] = span != 0.0f ? to_min[i] + (value[i] - from_min[i]) * (to_max[i] - to_min[i]) / span
                               : to_min[i];
      if (clamp) {
        result[i] = std::max(std::min(to_min[i], to_max[i]),
                             std::min(std::max(to_min[i], to_max[i]), result[i]));
      }
    }
    node.OutVector(ctx, 0, result[0], result[1], result[2]);
    return;
  }

  const float value = node.InFloat(ctx, 0);
  const float from_min = node.InFloat(ctx, 1);
  const float from_max = node.InFloat(ctx, 2);
  const float to_min = node.InFloat(ctx, 3);
  const float to_max = node.InFloat(ctx, 4);
  const float span = from_max - from_min;
  float result = span != 0.0f ? to_min + (value - from_min) * (to_max - to_min) / span : to_min;
  if (clamp) {
    const float lo = std::min(to_min, to_max);
    const float hi = std::max(to_min, to_max);
    result = std::max(lo, std::min(hi, result));
  }
  node.OutFloat(ctx, 0, result);
}

const KX_FuncPort kInterpolateValueInputs[] = {
    FP("From", KX_VALUE_FLOAT),
    FP("To", KX_VALUE_FLOAT),
    FP("Factor", KX_VALUE_FLOAT),
};

void FuncInterpolateValue(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float a = node.InFloat(ctx, 0);
  const float b = node.InFloat(ctx, 1);
  float factor = node.InFloat(ctx, 2);
  if (node.ConfigBool("clamp", true)) {
    factor = std::max(0.0f, std::min(1.0f, factor));
  }
  node.OutFloat(ctx, 0, a + (b - a) * factor);
}

const KX_FuncPort kThresholdNodeInputs[] = {
    FP("ElseZero", KX_VALUE_BOOL),
    FP("Value", KX_VALUE_FLOAT),
    FP("Threshold", KX_VALUE_FLOAT),
};

void FuncThresholdNode(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 1);
  const float threshold = node.InFloat(ctx, 2);
  const std::string op = node.ConfigString("op");
  bool pass = false;
  if (op == "gt") {
    pass = value > threshold;
  }
  else if (op == "ge") {
    pass = value >= threshold;
  }
  else if (op == "lt") {
    pass = value < threshold;
  }
  else if (op == "le") {
    pass = value <= threshold;
  }
  else if (op == "ne") {
    pass = value != threshold;
  }
  else {
    pass = value == threshold;
  }
  node.OutFloat(ctx, 0, pass ? value : (node.InBool(ctx, 0) ? 0.0f : value));
}

const KX_FuncPort kValueSwitchInputs[] = {
    FP("Condition", KX_VALUE_BOOL),
    FP("IfTrue", KX_VALUE_FLOAT),
    FP("IfFalse", KX_VALUE_FLOAT),
};

void FuncValueSwitch(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.Out(ctx, 0, node.InBool(ctx, 0) ? node.In(ctx, 1) : node.In(ctx, 2));
}

const KX_FuncPort kTimeFactorOutput[] = {FP("Factor", KX_VALUE_FLOAT)};

void FuncTimeFactor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, ctx.DeltaTime());
}

const KX_FuncPort kGetTimeScaleOutput[] = {FP("Timescale", KX_VALUE_FLOAT)};

void FuncGetTimeScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, ctx.SceneQuery().TimeScale());
}

const KX_FuncPort kGetResolutionOutputs[] = {
    FP("Width", KX_VALUE_FLOAT),
    FP("Height", KX_VALUE_FLOAT),
    FP("Resolution", KX_VALUE_VECTOR),
};

void FuncGetResolution(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  int width = 0;
  int height = 0;
  ctx.SceneQuery().Resolution(width, height);
  node.OutFloat(ctx, 0, float(width));
  node.OutFloat(ctx, 1, float(height));
  node.OutVector(ctx, 2, float(width), float(height), 0.0f);
}

const KX_FuncPort kGetFullscreenOutput[] = {FP("Fullscreen", KX_VALUE_BOOL)};

void FuncGetFullscreen(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutBool(ctx, 0, ctx.SceneQuery().FullScreen());
}

const KX_FuncPort kGetVsyncOutput[] = {FP("Mode", KX_VALUE_BOOL)};

void FuncGetVsync(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutBool(ctx, 0, ctx.SceneQuery().Vsync());
}

const KX_FuncPort kMouseDataOutputs[] = {
    FP("Position", KX_VALUE_VECTOR),
    FP("Movement", KX_VALUE_VECTOR),
    FP("X", KX_VALUE_FLOAT),
    FP("Y", KX_VALUE_FLOAT),
    FP("DeltaX", KX_VALUE_FLOAT),
    FP("DeltaY", KX_VALUE_FLOAT),
    FP("Wheel", KX_VALUE_INT),
};

void FuncMouseData(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float x = 0.0f;
  float y = 0.0f;
  float dx = 0.0f;
  float dy = 0.0f;
  ctx.Input().MousePosition(x, y);
  ctx.Input().MouseDelta(dx, dy);
  node.OutVector(ctx, 0, x, y, 0.0f);
  node.OutVector(ctx, 1, dx, dy, 0.0f);
  node.OutFloat(ctx, 2, x);
  node.OutFloat(ctx, 3, y);
  node.OutFloat(ctx, 4, dx);
  node.OutFloat(ctx, 5, dy);
  node.OutInt(ctx, 6, ctx.Input().MouseWheel());
}

const KX_FuncPort kGetPropertyNodeInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Property", KX_VALUE_STRING),
};

struct KX_GetPropertyState {
  bool warned = false;
};

void FuncGetPropertyNode(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GetPropertyState *state = node.StateBlock<KX_GetPropertyState>(ctx);
  KX_GameObject *object = node.InObject(ctx, 0);
  const std::string name = ExtraInString(node, ctx, 1);
  KX_RuntimeValue value;
  if (object != nullptr && !name.empty() &&
      ctx.Objects().GetProperty(object, name, &value, &ctx.Pool())) {
    node.Out(ctx, 0, value);
    state->warned = false;
    return;
  }
  node.Out(ctx, 0, KX_RuntimeValue::None());
  if (!state->warned) {
    std::fprintf(stderr,
                 "GameNode GetPropertyNode: cannot read property '%s'%s; output is None\n",
                 name.empty() ? "<empty>" : name.c_str(),
                 object == nullptr ? " from a null object" : "");
    state->warned = true;
  }
}

const KX_FuncPort kWithinRangeLegacyInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("Range", KX_VALUE_VECTOR),
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};
const KX_FuncPort kConditionOutput[] = {TP("Out")};

void FuncWithinRangeLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const float lo = std::min(node.InFloat(ctx, 2), node.InFloat(ctx, 3));
  const float hi = std::max(node.InFloat(ctx, 2), node.InFloat(ctx, 3));
  const bool inside = value >= lo && value <= hi;
  const std::string op = node.ConfigString("op");
  node.OutTrigger(ctx, 0, op == "outside" ? !inside : inside);
}

/* ------------------------------------------------------------------------- */
/* Actions                                                                   */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kCameraValueInputs[] = {
    TP("OnEnter"),
    FP("Camera", KX_VALUE_OBJECT),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kDone[] = {TP("OnDone")};

void FuncSetCameraFov(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetCameraFov(node.InObject(ctx, 1), node.InFloat(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

void FuncSetCameraOrthoScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetCameraOrthoScale(node.InObject(ctx, 1), node.InFloat(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSimpleTriggerInputs[] = {TP("OnEnter")};

void FuncSetVSync(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetVsync(!ctx.SceneQuery().Vsync());
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetMousePositionLegacyInputs[] = {
    TP("OnEnter"),
    FP("X", KX_VALUE_FLOAT),
    FP("Y", KX_VALUE_FLOAT),
};

void FuncSetMousePositionLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Input().SetMousePosition(node.InFloat(ctx, 1), node.InFloat(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetVisibilityLegacyInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Visible", KX_VALUE_BOOL),
    FP("IncludeChildren", KX_VALUE_BOOL),
};

void FuncSetVisibilityLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Objects().SetVisible(node.InObject(ctx, 1), node.InBool(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetParentLegacyInputs[] = {
    TP("OnEnter"),
    FP("Child", KX_VALUE_OBJECT),
    FP("Parent", KX_VALUE_OBJECT),
    FP("Compound", KX_VALUE_BOOL),
    FP("Ghost", KX_VALUE_BOOL),
};

void FuncSetParentLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Objects().SetParent(node.InObject(ctx, 1), node.InObject(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kCharacterJumpInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
};

void FuncCharacterJump(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float velocity[3] = {0.0f, 0.0f, 0.0f};
  ctx.Physics().CharacterJump(node.InObject(ctx, 1), velocity);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kCharacterVectorInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Vector", KX_VALUE_VECTOR),
};

void FuncCharacterGravity(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float value[3];
  node.InVector(ctx, 2, value);
  ctx.Physics().SetCharacterGravity(node.InObject(ctx, 1), value);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kCharacterVelocityInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Vector", KX_VALUE_VECTOR),
    FP("Time", KX_VALUE_FLOAT),
};

void FuncCharacterVelocity(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float value[3];
  node.InVector(ctx, 2, value);
  ctx.Physics().SetCharacterVelocity(node.InObject(ctx, 1), value);
  node.OutTrigger(ctx, 0, true);
}

void FuncCharacterWalkDir(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float value[3];
  node.InVector(ctx, 2, value);
  ctx.Physics().SetCharacterWalkDirection(node.InObject(ctx, 1), value);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kCharacterJumpSpeedInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Force", KX_VALUE_FLOAT),
};

void FuncCharacterJumpSpeed(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Physics().SetCharacterJumpSpeed(node.InObject(ctx, 1), node.InFloat(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetPropertyLegacyInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Property", KX_VALUE_STRING),
    FP("Value", KX_VALUE_FLOAT),
};

void FuncSetPropertyLegacy(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  const std::string name = ExtraInString(node, ctx, 2);
  const KX_RuntimeValue value = node.In(ctx, 3);
  if (object == nullptr || name.empty() ||
      !ctx.Objects().SetProperty(object, name, value)) {
    std::fprintf(stderr,
                 "GameNode SetPropertyLegacy: cannot write %s property '%s'%s\n",
                 KX_ValueTypeName(value.type),
                 name.empty() ? "<empty>" : name.c_str(),
                 object == nullptr ? " on a null object" : "");
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Conditions                                                                */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kKeyPressedInputs[] = {FP("KeyCode", KX_VALUE_INT)};

void FuncKeyPressed(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int mode = node.ConfigInt("mode", 0);
  node.OutTrigger(ctx, 0, ctx.Input().KeyActive(node.InInt(ctx, 0), static_cast<KX_InputMode>(mode)));
}

const KX_FuncPort kMousePressedInputs[] = {FP("Button", KX_VALUE_INT)};

void FuncMousePressed(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int mode = node.ConfigInt("mode", 0);
  node.OutTrigger(
      ctx, 0, ctx.Input().MouseButtonActive(node.InInt(ctx, 0), static_cast<KX_InputMode>(mode)));
}

const KX_FuncPort kMouseMovedOutput[] = {TP("IfMoved")};

void FuncMouseMoved(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float dx = 0.0f;
  float dy = 0.0f;
  ctx.Input().MouseDelta(dx, dy);
  node.OutTrigger(ctx, 0, dx != 0.0f || dy != 0.0f);
}

struct KX_ValueChangedState {
  KX_RuntimeValue previous;
  bool has_previous = false;
  bool type_warned = false;
};

const KX_FuncPort kValueChangedInputs[] = {FP("Value", KX_VALUE_FLOAT)};
const KX_FuncPort kValueChangedOutputs[] = {
    TP("IfChanged"),
    FP("Old", KX_VALUE_FLOAT),
    FP("New", KX_VALUE_FLOAT),
};

void FuncValueChanged(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ValueChangedState *state = node.StateBlock<KX_ValueChangedState>(ctx);
  const KX_RuntimeValue current = node.In(ctx, 0);
  bool changed = false;
  if (!state->has_previous) {
    state->previous = current;
    state->has_previous = true;
  }
  else if (state->previous.type != current.type) {
    /* A type switch is a change; warn once per transition instead of
     * silently comparing the converted floats. */
    changed = true;
    if (!state->type_warned) {
      std::fprintf(stderr,
                   "GameNode ValueChanged: value type changed from %s to %s; treating it as "
                   "changed\n",
                   KX_ValueTypeName(state->previous.type),
                   KX_ValueTypeName(current.type));
      state->type_warned = true;
    }
  }
  else {
    state->type_warned = false;
    changed = !RuntimeValuesEqual(state->previous, current);
  }
  node.Out(ctx, 1, state->previous);
  node.Out(ctx, 2, current);
  node.OutTrigger(ctx, 0, changed);
  state->previous = current;
}

struct KX_NextFrameState {
  bool pending = false;
};

void FuncNextFrameExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (node.InTrigger(ctx, 0)) {
    node.StateBlock<KX_NextFrameState>(ctx)->pending = true;
  }
}

void FuncNextFrameTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_NextFrameState *state = node.StateBlock<KX_NextFrameState>(ctx);
  if (state->pending) {
    state->pending = false;
    node.PendingTrigger(ctx, 0);
  }
}

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define VALUE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define NO_INPUT_VALUE(id, outs, exec) \
  KX_FunctionSpec {id, nullptr, 0, outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define ACTION(id, ins, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), kDone, PORT_COUNT(kDone), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define STATIC_STATE(id, ins, outs, exec, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, false, false}

#define TICK_STATE(id, ins, outs, exec, tick_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, true}

/* Polymorphic variants keep the port order and names from the table but take
 * the value types from the IR, because the editor resolves them from the
 * connected socket or property definition. */
#define POLY_VALUE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false, \
                   nullptr, nullptr, nullptr, true}

#define POLY_STATE_VALUE(id, ins, outs, exec, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), true, false, \
                   false, nullptr, nullptr, nullptr, true}

#define POLY_ACTION(id, ins, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), kDone, PORT_COUNT(kDone), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false, \
                   nullptr, nullptr, nullptr, true}

#define POLY_STATIC_STATE(id, ins, outs, exec, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, false, \
                   false, nullptr, nullptr, nullptr, true}

#define CONDITION(id, ins, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), kConditionOutput, PORT_COUNT(kConditionOutput), exec, \
                   nullptr, nullptr, 0, KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define NO_INPUT_CONDITION(id, exec) \
  KX_FunctionSpec {id, nullptr, 0, kConditionOutput, PORT_COUNT(kConditionOutput), exec, nullptr, \
                   nullptr, 0, KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

const KX_FunctionSpec kExtraSpecs[] = {
    VALUE("Vector3", kVector3Inputs, kVectorOutput, FuncVector3),
    VALUE("VectorSplit3", kVectorSplit3Inputs, kVectorSplit3Outputs, FuncVectorSplit3),
    VALUE("ValueInvert", kValueInvertInputs, kFloatValueOutput, FuncValueInvert),
    VALUE("LimitRange", kLimitRangeInputs, kFloatValueOutput, FuncLimitRange),
    POLY_VALUE("MapRangeNode", kMapRangeNodeInputs, kFloatValueOutput, FuncMapRangeNode),
    VALUE("InterpolateValue", kInterpolateValueInputs, kFloatValueOutput, FuncInterpolateValue),
    VALUE("ThresholdNode", kThresholdNodeInputs, kFloatValueOutput, FuncThresholdNode),
    POLY_VALUE("ValueSwitch", kValueSwitchInputs, kFloatValueOutput, FuncValueSwitch),
    NO_INPUT_VALUE("TimeFactor", kTimeFactorOutput, FuncTimeFactor),
    NO_INPUT_VALUE("GetTimeScale", kGetTimeScaleOutput, FuncGetTimeScale),
    NO_INPUT_VALUE("GetResolution", kGetResolutionOutputs, FuncGetResolution),
    NO_INPUT_VALUE("GetFullscreen", kGetFullscreenOutput, FuncGetFullscreen),
    NO_INPUT_VALUE("GetVsync", kGetVsyncOutput, FuncGetVsync),
    NO_INPUT_VALUE("MouseData", kMouseDataOutputs, FuncMouseData),
    POLY_STATE_VALUE("GetPropertyNode",
                     kGetPropertyNodeInputs,
                     kFloatValueOutput,
                     FuncGetPropertyNode,
                     KX_GetPropertyState),
    CONDITION("WithinRangeNode", kWithinRangeLegacyInputs, FuncWithinRangeLegacy),

    ACTION("SetCameraFov", kCameraValueInputs, FuncSetCameraFov),
    ACTION("SetCameraOrthoScale", kCameraValueInputs, FuncSetCameraOrthoScale),
    ACTION("SetVSync", kSimpleTriggerInputs, FuncSetVSync),
    ACTION("SetMousePositionLegacy", kSetMousePositionLegacyInputs, FuncSetMousePositionLegacy),
    ACTION("SetVisibilityLegacy", kSetVisibilityLegacyInputs, FuncSetVisibilityLegacy),
    ACTION("SetParentLegacy", kSetParentLegacyInputs, FuncSetParentLegacy),
    ACTION("CharacterJump", kCharacterJumpInputs, FuncCharacterJump),
    ACTION("CharacterGravity", kCharacterVectorInputs, FuncCharacterGravity),
    ACTION("CharacterVelocity", kCharacterVelocityInputs, FuncCharacterVelocity),
    ACTION("CharacterWalkDir", kCharacterVectorInputs, FuncCharacterWalkDir),
    ACTION("CharacterJumpSpeed", kCharacterJumpSpeedInputs, FuncCharacterJumpSpeed),
    POLY_ACTION("SetPropertyLegacy", kSetPropertyLegacyInputs, FuncSetPropertyLegacy),

    CONDITION("KeyPressed", kKeyPressedInputs, FuncKeyPressed),
    CONDITION("MousePressed", kMousePressedInputs, FuncMousePressed),
    NO_INPUT_CONDITION("MouseMoved", FuncMouseMoved),
    POLY_STATIC_STATE("ValueChanged",
                      kValueChangedInputs,
                      kValueChangedOutputs,
                      FuncValueChanged,
                      KX_ValueChangedState),
    TICK_STATE("NextFrame",
               kSimpleTriggerInputs,
               kConditionOutput,
               FuncNextFrameExec,
               FuncNextFrameTick,
               KX_NextFrameState),
};

#undef PORT_COUNT

}  // namespace

namespace {

KX_NodeSignature MakeExtraSignature(const KX_FunctionSpec &spec)
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

void RegisterExtendedFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kExtraSpecs) {
    const KX_FunctionSpec *entry = &spec;
    const KX_NodeSignature signature = MakeExtraSignature(spec);
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncX_") + spec.type_id,
        signature,
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
