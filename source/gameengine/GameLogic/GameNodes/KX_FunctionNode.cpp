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

/** \file KX_FunctionNode.cpp
 *  \ingroup gamelogic
 *
 *  Table driven node library. Each entry below is one editor node family
 *  (input, time, transform, physics, object, scene, math, string, ...). The
 *  callbacks only talk to the execution context and its adapters, so they stay
 *  testable without the engine.
 */

#include "KX_FunctionNode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_GraphIR.h"
#include "KX_NodeRegistry.h"

/* ------------------------------------------------------------------------- */
/* Spec helpers                                                               */
/* ------------------------------------------------------------------------- */

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

/** Value type each widget_attr entry carries, used to type SetUIWidgetAttr. */
KX_ValueType WidgetAttrValueType(int attr)
{
  switch (attr) {
    case 0:  /* show */
    case 18: /* wrap */
    case 19: /* shadow */
      return KX_VALUE_BOOL;
    case 1:  /* bg_color */
    case 9:  /* border_color */
    case 10: /* hover_color */
    case 13: /* font_color */
    case 21: /* shadow_color */
      return KX_VALUE_COLOR;
    case 3:  /* pos */
    case 4:  /* size */
    case 20: /* shadow_offset */
      return KX_VALUE_VECTOR;
    case 11: /* text */
    case 12: /* font */
    case 22: /* texture */
      return KX_VALUE_STRING;
    case 2:  /* opacity */
    case 5:  /* angle */
    case 15: /* line_height */
      return KX_VALUE_FLOAT;
    default: /* width, height, border_width, font_size, alignment, spacing, ... */
      return KX_VALUE_INT;
  }
}

KX_ValueType ResolveWidgetAttrPort(const KX_FunctionSpec *spec, int attr)
{
  if (spec == nullptr) {
    return KX_VALUE_NONE;
  }
  if (std::strcmp(spec->type_id, "SetUIWidgetAttr") != 0) {
    return KX_VALUE_NONE;
  }
  return WidgetAttrValueType(attr);
}

std::string InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

/** Generic formatting used by Print: keeps the value's own type. */
std::string FormatRuntimeValue(const KX_RuntimeValue &value, KX_ObjectAdapter &objects)
{
  char buffer[128];
  switch (value.type) {
    case KX_VALUE_BOOL:
      return value.data.b ? "True" : "False";
    case KX_VALUE_INT:
      std::snprintf(buffer, sizeof(buffer), "%d", value.data.i);
      return buffer;
    case KX_VALUE_FLOAT:
      std::snprintf(buffer, sizeof(buffer), "%g", value.data.f);
      return buffer;
    case KX_VALUE_STRING: {
      const char *text = value.AsString();
      return text != nullptr ? std::string(text) : std::string("None");
    }
    case KX_VALUE_VECTOR:
      std::snprintf(buffer,
                    sizeof(buffer),
                    "(%g, %g, %g)",
                    value.data.vec[0],
                    value.data.vec[1],
                    value.data.vec[2]);
      return buffer;
    case KX_VALUE_COLOR:
      std::snprintf(buffer,
                    sizeof(buffer),
                    "(%g, %g, %g, %g)",
                    value.data.vec[0],
                    value.data.vec[1],
                    value.data.vec[2],
                    value.data.vec[3]);
      return buffer;
    case KX_VALUE_OBJECT: {
      KX_GameObject *object = static_cast<KX_GameObject *>(value.data.ptr);
      return object != nullptr ? "Object(" + objects.GetName(object) + ")" : std::string("None");
    }
    case KX_VALUE_ASSET:
      std::snprintf(buffer, sizeof(buffer), "<Asset %p>", value.data.ptr);
      return buffer;
    case KX_VALUE_DATABLOCK:
      std::snprintf(buffer, sizeof(buffer), "<DataBlock %p>", value.data.ptr);
      return buffer;
    case KX_VALUE_LIST:
      std::snprintf(buffer, sizeof(buffer), "<List %p>", value.data.ptr);
      return buffer;
    default:
      return "None";
  }
}

float Dot3(const float a[3], const float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

uint32_t HashToUnit(uint32_t seed)
{
  seed ^= seed >> 16;
  seed *= 0x7feb352dU;
  seed ^= seed >> 15;
  seed *= 0x846ca68bU;
  seed ^= seed >> 16;
  return seed;
}

/* ------------------------------------------------------------------------- */
/* Input                                                                      */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kKeyboardOutputs[] = {FP("Active", KX_VALUE_BOOL)};

void FuncKeyboard(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int key = node.ConfigInt("key", -1);
  const int mode = node.ConfigInt("mode", 2);
  node.OutBool(ctx, 0, ctx.Input().KeyActive(key, static_cast<KX_InputMode>(mode)));
}

const KX_FuncPort kMouseButtonOutputs[] = {FP("Active", KX_VALUE_BOOL)};

void FuncMouseButton(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int button = node.ConfigInt("button", 0);
  const int mode = node.ConfigInt("mode", 2);
  node.OutBool(ctx, 0, ctx.Input().MouseButtonActive(button, static_cast<KX_InputMode>(mode)));
}

const KX_FuncPort kMousePositionOutputs[] = {
    FP("Position", KX_VALUE_VECTOR),
    FP("Normalized", KX_VALUE_VECTOR),
};

void FuncMousePosition(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float x = 0.0f;
  float y = 0.0f;
  ctx.Input().MousePosition(x, y);
  node.OutVector(ctx, 0, x, y, 0.0f);
  ctx.Input().MouseNormalized(x, y);
  node.OutVector(ctx, 1, x, y, 0.0f);
}

const KX_FuncPort kMouseDeltaOutputs[] = {FP("Delta", KX_VALUE_VECTOR)};

void FuncMouseDelta(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float dx = 0.0f;
  float dy = 0.0f;
  ctx.Input().MouseDelta(dx, dy);
  node.OutVector(ctx, 0, dx, dy, 0.0f);
}

const KX_FuncPort kMouseWheelOutputs[] = {FP("Wheel", KX_VALUE_FLOAT)};

void FuncMouseWheel(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, static_cast<float>(ctx.Input().MouseWheel()));
}

const KX_FuncPort kJoystickAxisOutputs[] = {FP("Value", KX_VALUE_FLOAT)};

void FuncJoystickAxis(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, ctx.Input().JoystickAxis(node.ConfigInt("axis", 0)));
}

const KX_FuncPort kJoystickButtonOutputs[] = {FP("Active", KX_VALUE_BOOL)};

void FuncJoystickButton(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int index = node.ConfigInt("button", 0);
  const int mode = node.ConfigInt("mode", 2);
  node.OutBool(ctx, 0, ctx.Input().JoystickButton(index, static_cast<KX_InputMode>(mode)));
}

const KX_FuncPort kSetMousePositionInputs[] = {
    TP("OnEnter"),
    FP("Position", KX_VALUE_VECTOR),
};
const KX_FuncPort kDoneOutput[] = {TP("OnDone")};

void FuncSetMousePosition(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float position[3];
  node.InVector(ctx, 1, position);
  ctx.Input().SetMousePosition(position[0], position[1]);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Time                                                                       */
/* ------------------------------------------------------------------------- */

struct KX_TimerState {
  float elapsed = 0.0f;
  float duration = 0.0f;
  bool running = false;
};

const KX_FuncPort kTimerInputs[] = {
    TP("OnEnter"),
    FP("Duration", KX_VALUE_FLOAT),
};
const KX_FuncPort kTimerOutputs[] = {
    FP("Elapsed", KX_VALUE_FLOAT),
    TP("OnFinished"),
};

void FuncTimerExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const bool start = node.InTrigger(ctx, 0);
  const float duration = node.InFloat(ctx, 1);
  KX_TimerState *state = node.StateBlock<KX_TimerState>(ctx);
  if (start && duration > 0.0f) {
    state->duration = duration;
    state->elapsed = 0.0f;
    state->running = true;
  }
  node.OutFloat(ctx, 0, state->elapsed);
}

void FuncTimerTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  KX_TimerState *state = node.StateBlock<KX_TimerState>(ctx);
  if (!state->running) {
    return;
  }
  state->elapsed += delta_time;
  if (state->elapsed >= state->duration) {
    state->elapsed = state->duration;
    state->running = false;
    node.OutTrigger(ctx, 1, true);
  }
  node.OutFloat(ctx, 0, state->elapsed);
}

const KX_FuncPort kOnceInputs[] = {TP("OnEnter")};
const KX_FuncPort kOnceOutputs[] = {TP("OnOut")};

void FuncOnce(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  bool *fired = node.StateBlock<bool>(ctx);
  node.OutTrigger(ctx, 0, !*fired);
  *fired = true;
}

const KX_FuncPort kSequenceInputs[] = {TP("OnEnter")};
const KX_FuncPort kSequenceOutputs[] = {
    TP("Out0"),
    TP("Out1"),
    TP("Out2"),
    TP("Out3"),
};

/* Full Sequence: one OnEnter drives every branch.
 *
 *   mode=serial   (default) Out0 -> subtree -> Out1 -> subtree -> ...
 *                 The scheduler runs the whole downstream subtree of one
 *                 element before raising the next, so a consumer observes the
 *                 branch order exactly as written.
 *   mode=parallel          out0..outN are raised together and the scheduler
 *                 drains their subtrees in output order.
 *
 * The pre-B6.9 node advanced one output per OnEnter; that erased the branch
 * order whenever a single trigger was meant to drive the sequence. */
struct KX_SequenceState {
  uint32_t count = 0;
};

void FuncSequenceExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_SequenceState *state = node.StateBlock<KX_SequenceState>(ctx);
  state->count = 0;
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  if (node.ConfigString("mode") == "parallel") {
    for (uint32_t index = 0; index < node.Spec().output_count; ++index) {
      node.OutTrigger(ctx, index, true);
    }
    return;
  }
  state->count = node.Spec().output_count;
}

uint32_t FuncSequencePending(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  return node.StateBlock<KX_SequenceState>(ctx)->count;
}

void FuncSequenceElement(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  node.OutTrigger(ctx, index, true);
}

void FuncSequenceFinish(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.StateBlock<KX_SequenceState>(ctx)->count = 0;
}

const KX_FuncPort kFrameTimeOutputs[] = {
    FP("Delta", KX_VALUE_FLOAT),
    FP("Time", KX_VALUE_FLOAT),
    FP("Frame", KX_VALUE_INT),
};

void FuncFrameTimeTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  float *time = node.StateBlock<float>(ctx);
  *time += delta_time;
  node.OutFloat(ctx, 0, delta_time);
  node.OutFloat(ctx, 1, *time);
  node.OutInt(ctx, 2, static_cast<int>(ctx.Frame()));
}

/* ------------------------------------------------------------------------- */
/* Scene / engine actions                                                     */
/* ------------------------------------------------------------------------- */

void FuncQuitGame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (node.InTrigger(ctx, 0)) {
    ctx.SceneQuery().RequestQuit();
    node.OutTrigger(ctx, 0, true);
  }
}

void FuncRestartGame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (node.InTrigger(ctx, 0)) {
    ctx.SceneQuery().RequestRestart();
    node.OutTrigger(ctx, 0, true);
  }
}

const KX_FuncPort kTriggerValueInputs[] = {
    TP("OnEnter"),
    FP("Value", KX_VALUE_STRING),
};

void FuncPrint(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  /* Print accepts any value type: format by the runtime tag instead of
   * forcing a String conversion.
   *
   * An unbound slot stays KX_VALUE_NONE (IR emitted by an older exporter has
   * no ConstString for the input). Never report that as the literal string
   * "None": say explicitly that the input is unset. */
  const KX_RuntimeValue value = node.In(ctx, 1);
  const std::string text = (value.type == KX_VALUE_NONE) ?
                               std::string("<Print: Value input unset>") :
                               FormatRuntimeValue(value, ctx.Objects());
  std::printf("%s\n", text.c_str());
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetTimeScaleInputs[] = {
    TP("OnEnter"),
    FP("Scale", KX_VALUE_FLOAT),
};

void FuncSetTimeScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetTimeScale(node.InFloat(ctx, 1));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetGravityInputs[] = {
    TP("OnEnter"),
    FP("Gravity", KX_VALUE_VECTOR),
};

/** Largest per-component gravity accepted by SetGravity (1000 m/s^2). */
const float kMaxGravityComponent = 1000.0f;

void FuncSetGravity(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float gravity[3];
  node.InVector(ctx, 1, gravity);
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(gravity[i]) ||
        std::fabs(gravity[i]) > kMaxGravityComponent) {
      fprintf(stderr,
              "[SetGravity] rejected gravity (%g,%g,%g): components must be finite "
              "and within +/-%g\n",
              double(gravity[0]),
              double(gravity[1]),
              double(gravity[2]),
              double(kMaxGravityComponent));
      node.OutTrigger(ctx, 0, true);
      return;
    }
  }
  ctx.Physics().SetGravity(gravity);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetResolutionInputs[] = {
    TP("OnEnter"),
    FP("Width", KX_VALUE_INT),
    FP("Height", KX_VALUE_INT),
};

void FuncSetResolution(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetResolution(node.InInt(ctx, 1), node.InInt(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetFullScreenInputs[] = {
    TP("OnEnter"),
    FP("Enabled", KX_VALUE_BOOL),
};

void FuncSetFullScreen(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetFullScreen(node.InBool(ctx, 1));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetActiveCameraInputs[] = {
    TP("OnEnter"),
    FP("Camera", KX_VALUE_OBJECT),
};

void FuncSetActiveCamera(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().SetActiveCamera(node.InObject(ctx, 1));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kLoadSceneInputs[] = {
    TP("OnEnter"),
    FP("Scene", KX_VALUE_STRING),
};

void FuncLoadScene(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string name = InString(node, ctx, 1);
  if (name.empty()) {
    name = node.ConfigString("scene");
  }
  node.OutTrigger(ctx, 0, ctx.SceneQuery().LoadScene(name));
}

const KX_FuncPort kObjectActionInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
};

void FuncRemoveObject(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.SceneQuery().RemoveObject(node.InObject(ctx, 1));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetVisibilityInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Visible", KX_VALUE_BOOL),
};

void FuncSetVisibility(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Objects().SetVisible(node.InObject(ctx, 1), node.InBool(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetParentInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Parent", KX_VALUE_OBJECT),
};

void FuncSetParent(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  KX_GameObject *parent = node.InObject(ctx, 2);
  node.OutTrigger(ctx, 0, ctx.Objects().SetParent(object, parent));
}

void FuncClearParent(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Objects().ClearParent(node.InObject(ctx, 1));
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Physics actions                                                           */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kSetLinearVelocityInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Velocity", KX_VALUE_VECTOR),
};

void FuncSetLinearVelocity(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float velocity[3];
  node.InVector(ctx, 2, velocity);
  ctx.Physics().SetLinearVelocity(node.InObject(ctx, 1), velocity);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kApplyImpulseInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Impulse", KX_VALUE_VECTOR),
    FP("Point", KX_VALUE_VECTOR),
};

void FuncApplyImpulse(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float impulse[3];
  float point[3];
  node.InVector(ctx, 2, impulse);
  node.InVector(ctx, 3, point);
  ctx.Physics().ApplyImpulse(node.InObject(ctx, 1), point, impulse, node.ConfigBool("local", false));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kApplyForceInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Force", KX_VALUE_VECTOR),
    FP("Point", KX_VALUE_VECTOR),
};

void FuncApplyForce(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float force[3];
  float point[3];
  node.InVector(ctx, 2, force);
  node.InVector(ctx, 3, point);
  ctx.Physics().ApplyForce(node.InObject(ctx, 1), point, force, node.ConfigBool("local", false));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kApplyTorqueInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Torque", KX_VALUE_VECTOR),
};

void FuncApplyTorque(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float torque[3];
  node.InVector(ctx, 2, torque);
  ctx.Physics().ApplyTorque(node.InObject(ctx, 1), torque, node.ConfigBool("local", false));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kTogglePhysicsInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Enabled", KX_VALUE_BOOL),
};

void FuncSetDynamics(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Physics().SetDynamics(node.InObject(ctx, 1), node.InBool(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

void FuncSetRigidBody(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Physics().SetRigidBody(node.InObject(ctx, 1), node.InBool(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetCollisionMaskInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Mask", KX_VALUE_INT),
};

void FuncSetCollisionMask(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Physics().SetCollisionMask(node.InObject(ctx, 1), node.InInt(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Network / animation / text                                                */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kSendMessageInputs[] = {
    TP("OnEnter"),
    FP("To", KX_VALUE_OBJECT),
    FP("Subject", KX_VALUE_STRING),
    FP("Body", KX_VALUE_STRING),
};

void FuncSendMessage(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string subject = InString(node, ctx, 2);
  const std::string body = InString(node, ctx, 3);
  ctx.SceneQuery().SendMessage(node.InObject(ctx, 1), subject, body);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetFontTextInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Text", KX_VALUE_STRING),
};

void FuncSetFontText(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  node.OutTrigger(
      ctx, 0, ctx.Objects().SetFontText(node.InObject(ctx, 1), InString(node, ctx, 2)));
}

/* ------------------------------------------------------------------------- */
/* Transform                                                                 */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kSetTransformInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Value", KX_VALUE_VECTOR),
};

void FuncSetPosition(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float value[3];
  node.InVector(ctx, 2, value);
  node.OutTrigger(ctx, 0, ctx.Objects().SetLocalPosition(node.InObject(ctx, 1), value));
}

void FuncSetRotation(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float value[3];
  node.InVector(ctx, 2, value);
  node.OutTrigger(ctx, 0, ctx.Objects().SetLocalOrientation(node.InObject(ctx, 1), value));
}

void FuncSetScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float value[3];
  node.InVector(ctx, 2, value);
  node.OutTrigger(ctx, 0, ctx.Objects().SetLocalScale(node.InObject(ctx, 1), value));
}

void FuncTranslate(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  if (object == nullptr) {
    return;
  }
  float position[3];
  float offset[3];
  if (!ctx.Objects().GetWorldPosition(object, position)) {
    return;
  }
  node.InVector(ctx, 2, offset);
  position[0] += offset[0];
  position[1] += offset[1];
  position[2] += offset[2];
  node.OutTrigger(ctx, 0, ctx.Objects().SetLocalPosition(object, position));
}

/* ------------------------------------------------------------------------- */
/* Value nodes                                                               */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kWorldTransformOutputs[] = {FP("Value", KX_VALUE_VECTOR)};

void FuncWorldPosition(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float value[3] = {0.0f, 0.0f, 0.0f};
  ctx.Objects().GetWorldPosition(node.InObject(ctx, 0), value);
  node.OutVector(ctx, 0, value[0], value[1], value[2]);
}

void FuncWorldRotation(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float value[3] = {0.0f, 0.0f, 0.0f};
  ctx.Objects().GetWorldOrientation(node.InObject(ctx, 0), value);
  node.OutVector(ctx, 0, value[0], value[1], value[2]);
}

void FuncWorldScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float value[3] = {0.0f, 0.0f, 0.0f};
  ctx.Objects().GetWorldScale(node.InObject(ctx, 0), value);
  node.OutVector(ctx, 0, value[0], value[1], value[2]);
}

const KX_FuncPort kObjectValueInputs[] = {FP("Object", KX_VALUE_OBJECT)};
const KX_FuncPort kObjectValueOutputs[] = {FP("Object", KX_VALUE_OBJECT)};

void FuncGetParent(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutObject(ctx, 0, ctx.Objects().GetParent(node.InObject(ctx, 0)));
}

void FuncObjectByName(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutObject(ctx, 0, ctx.SceneQuery().FindObject(node.ConfigString("name")));
}

void FuncGetOwner(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutObject(ctx, 0, ctx.Owner());
}

const KX_FuncPort kIsNoneOutputs[] = {FP("Value", KX_VALUE_BOOL)};

void FuncIsNone(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutBool(ctx, 0, node.InObject(ctx, 0) == nullptr);
}

const KX_FuncPort kGetGravityOutputs[] = {FP("Gravity", KX_VALUE_VECTOR)};

void FuncGetGravity(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float gravity[3] = {0.0f, 0.0f, 0.0f};
  ctx.Physics().GetGravity(gravity);
  node.OutVector(ctx, 0, gravity[0], gravity[1], gravity[2]);
}

const KX_FuncPort kGetVelocityOutputs[] = {FP("Velocity", KX_VALUE_VECTOR)};

void FuncGetVelocity(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float velocity[3] = {0.0f, 0.0f, 0.0f};
  ctx.Physics().GetVelocity(node.InObject(ctx, 0), velocity);
  node.OutVector(ctx, 0, velocity[0], velocity[1], velocity[2]);
}

const KX_FuncPort kObjectPairInputs[] = {
    FP("A", KX_VALUE_OBJECT),
    FP("B", KX_VALUE_OBJECT),
};
const KX_FuncPort kObjectPairRangeInputs[] = {
    FP("A", KX_VALUE_OBJECT),
    FP("B", KX_VALUE_OBJECT),
    FP("Distance", KX_VALUE_FLOAT),
};

const KX_FuncPort kDistanceOutputs[] = {FP("Distance", KX_VALUE_FLOAT)};

float ObjectDistance(KX_ExecutionContext &ctx, KX_GameObject *a, KX_GameObject *b)
{
  float pa[3];
  float pb[3];
  if (!ctx.Objects().GetWorldPosition(a, pa) || !ctx.Objects().GetWorldPosition(b, pb)) {
    return 0.0f;
  }
  const float d[3] = {pa[0] - pb[0], pa[1] - pb[1], pa[2] - pb[2]};
  return std::sqrt(Dot3(d, d));
}

void FuncDistance(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, ObjectDistance(ctx, node.InObject(ctx, 0), node.InObject(ctx, 1)));
}

const KX_FuncPort kWithinRangeOutputs[] = {FP("Value", KX_VALUE_BOOL)};

void FuncWithinRange(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float distance = ObjectDistance(ctx, node.InObject(ctx, 0), node.InObject(ctx, 1));
  node.OutBool(ctx, 0, distance <= node.InFloat(ctx, 2));
}

/* ------------------------------------------------------------------------- */
/* Collision                                                                 */
/* ------------------------------------------------------------------------- */

struct KX_CollisionState {
  bool overlapping = false;
};

const KX_FuncPort kCollisionInputs[] = {FP("Object", KX_VALUE_OBJECT)};
const KX_FuncPort kCollisionOutputs[] = {
    TP("OnEnter"),
    TP("OnStay"),
    TP("OnLeave"),
    FP("ContactPoint", KX_VALUE_VECTOR),
    FP("ContactNormal", KX_VALUE_VECTOR),
    FP("ImpactForce", KX_VALUE_FLOAT),
};

bool BoundsOverlap(const float min_a[3],
                   const float max_a[3],
                   const float min_b[3],
                   const float max_b[3])
{
  return min_a[0] <= max_b[0] && max_a[0] >= min_b[0] && min_a[1] <= max_b[1] &&
         max_a[1] >= min_b[1] && min_a[2] <= max_b[2] && max_a[2] >= min_b[2];
}

void FuncCollisionTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_CollisionState *state = node.StateBlock<KX_CollisionState>(ctx);
  KX_GameObject *a = ctx.Owner();
  KX_GameObject *b = node.InObject(ctx, 0);

  bool overlapping = false;
  float contact_point[3] = {0.0f, 0.0f, 0.0f};
  float contact_normal[3] = {0.0f, 0.0f, 1.0f};
  float impact_force = 0.0f;
  if (a != nullptr && b != nullptr) {
    float min_a[3];
    float max_a[3];
    float min_b[3];
    float max_b[3];
    if (ctx.Physics().WorldBounds(a, min_a, max_a) &&
        ctx.Physics().WorldBounds(b, min_b, max_b))
    {
      overlapping = BoundsOverlap(min_a, max_a, min_b, max_b);
      if (overlapping) {
        /* The node is bounds based, so the contact is derived from the AABB
         * overlap: the centre of the overlap volume plus the axis of least
         * penetration. This is a contact proxy, not a Bullet manifold. */
        float overlap_min[3];
        float overlap_max[3];
        float penetration[3];
        for (int axis = 0; axis < 3; ++axis) {
          overlap_min[axis] = std::max(min_a[axis], min_b[axis]);
          overlap_max[axis] = std::min(max_a[axis], max_b[axis]);
          penetration[axis] = overlap_max[axis] - overlap_min[axis];
          contact_point[axis] = (overlap_min[axis] + overlap_max[axis]) * 0.5f;
        }
        int least = 0;
        for (int axis = 1; axis < 3; ++axis) {
          if (penetration[axis] < penetration[least]) {
            least = axis;
          }
        }
        contact_normal[0] = contact_normal[1] = contact_normal[2] = 0.0f;
        const float centre_a = (min_a[least] + max_a[least]) * 0.5f;
        const float centre_b = (min_b[least] + max_b[least]) * 0.5f;
        contact_normal[least] = centre_a >= centre_b ? 1.0f : -1.0f;

        float velocity_a[3];
        float velocity_b[3];
        ctx.Objects().GetLinearVelocity(a, velocity_a);
        ctx.Objects().GetLinearVelocity(b, velocity_b);
        const float relative[3] = {velocity_a[0] - velocity_b[0],
                                  velocity_a[1] - velocity_b[1],
                                  velocity_a[2] - velocity_b[2]};
        impact_force = std::fabs(relative[0] * contact_normal[0] +
                                 relative[1] * contact_normal[1] +
                                 relative[2] * contact_normal[2]);
      }
    }
  }

  if (overlapping && node.OutputCount() >= 6) {
    /* Extension ports added in B6.3. A graph exported before the extension
     * declares only the three trigger outputs, so the guard keeps that IR
     * re-buildable instead of writing past the shorter offset table. */
    node.OutVector(ctx, 3, contact_point[0], contact_point[1], contact_point[2]);
    node.OutVector(ctx, 4, contact_normal[0], contact_normal[1], contact_normal[2]);
    node.OutFloat(ctx, 5, impact_force);
  }

  if (overlapping && !state->overlapping) {
    node.OutTrigger(ctx, 0, true);
  }
  else if (overlapping && state->overlapping) {
    node.OutTrigger(ctx, 1, true);
  }
  else if (!overlapping && state->overlapping) {
    node.OutTrigger(ctx, 2, true);
  }
  state->overlapping = overlapping;
}

/* ------------------------------------------------------------------------- */
/* Math / logic                                                              */
/* ------------------------------------------------------------------------- */

int CompareInts(const std::string &op, float a, float b)
{
  if (op == "eq") {
    return a == b;
  }
  if (op == "ne") {
    return a != b;
  }
  if (op == "lt") {
    return a < b;
  }
  if (op == "le") {
    return a <= b;
  }
  if (op == "gt") {
    return a > b;
  }
  if (op == "ge") {
    return a >= b;
  }
  return 0;
}

const KX_FuncPort kCompareInputs[] = {
    FP("A", KX_VALUE_FLOAT),
    FP("B", KX_VALUE_FLOAT),
};
const KX_FuncPort kCompareOutputs[] = {FP("Value", KX_VALUE_BOOL)};

void FuncCompare(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string op = node.ConfigString("op");
  node.OutBool(ctx, 0, CompareInts(op, node.InFloat(ctx, 0), node.InFloat(ctx, 1)) != 0);
}

/* Logic gates combine trigger edges. They opt into the scheduler trigger
 * snapshot so A and B can be observed in the same frame. */
const KX_FuncPort kLogicGateInputs[] = {
    TP("A"),
    TP("B"),
};
const KX_FuncPort kLogicGateOutputs[] = {
    FP("Result", KX_VALUE_BOOL),
    TP("OnTrue"),
    TP("OnFalse"),
};

void FuncLogicGate(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const bool a = ctx.TriggerSnapshot(0);
  const bool b = ctx.TriggerSnapshot(1);
  std::string op = node.ConfigString("op");
  for (char &character : op) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  bool result = false;
  if (op == "or") {
    result = a || b;
  }
  else if (op == "xor") {
    result = a != b;
  }
  else if (op == "not") {
    /* NOT is single input: A is inverted, B only triggers the evaluation. */
    result = !a;
  }
  else if (op == "nand") {
    result = !(a && b);
  }
  else if (op == "nor") {
    result = !(a || b);
  }
  else {
    result = a && b;
  }
  node.OutBool(ctx, 0, result);
  node.OutTrigger(ctx, 1, result);
  node.OutTrigger(ctx, 2, !result);
}

const KX_FuncPort kLogicNotInputs[] = {FP("Value", KX_VALUE_BOOL)};
const KX_FuncPort kLogicNotOutputs[] = {FP("Value", KX_VALUE_BOOL)};

void FuncLogicNot(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutBool(ctx, 0, !node.InBool(ctx, 0));
}

const KX_FuncPort kClampInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};
const KX_FuncPort kFloatOutput[] = {FP("Value", KX_VALUE_FLOAT)};

void FuncClamp(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const float minimum = node.InFloat(ctx, 1);
  const float maximum = node.InFloat(ctx, 2);
  node.OutFloat(ctx, 0, std::max(minimum, std::min(value, maximum)));
}

const KX_FuncPort kMapRangeInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("FromMin", KX_VALUE_FLOAT),
    FP("FromMax", KX_VALUE_FLOAT),
    FP("ToMin", KX_VALUE_FLOAT),
    FP("ToMax", KX_VALUE_FLOAT),
};

void FuncMapRange(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const float from_min = node.InFloat(ctx, 1);
  const float from_max = node.InFloat(ctx, 2);
  const float to_min = node.InFloat(ctx, 3);
  const float to_max = node.InFloat(ctx, 4);
  float factor = 0.0f;
  if (from_max != from_min) {
    factor = (value - from_min) / (from_max - from_min);
  }
  if (node.ConfigBool("clamp", true)) {
    factor = std::max(0.0f, std::min(factor, 1.0f));
  }
  node.OutFloat(ctx, 0, to_min + (to_max - to_min) * factor);
}

const KX_FuncPort kInterpolateInputs[] = {
    FP("A", KX_VALUE_FLOAT),
    FP("B", KX_VALUE_FLOAT),
    FP("Factor", KX_VALUE_FLOAT),
};

void FuncInterpolate(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float factor = node.InFloat(ctx, 2);
  if (node.ConfigBool("clamp", true)) {
    factor = std::max(0.0f, std::min(factor, 1.0f));
  }
  const float a = node.InFloat(ctx, 0);
  node.OutFloat(ctx, 0, a + (node.InFloat(ctx, 1) - a) * factor);
}

const KX_FuncPort kRandomInputs[] = {
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};

void FuncRandomFloat(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float minimum = node.InFloat(ctx, 0);
  const float maximum = node.InFloat(ctx, 1);
  const uint32_t hash = HashToUnit(static_cast<uint32_t>(ctx.Frame()) ^
                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&node)));
  const float unit = static_cast<float>(hash & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
  node.OutFloat(ctx, 0, minimum + (maximum - minimum) * unit);
}

const KX_FuncPort kThresholdInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("Threshold", KX_VALUE_FLOAT),
};
const KX_FuncPort kThresholdOutputs[] = {FP("Value", KX_VALUE_BOOL)};

void FuncThreshold(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string op = node.ConfigString("op");
  node.OutBool(ctx, 0, CompareInts(op, node.InFloat(ctx, 0), node.InFloat(ctx, 1)) != 0);
}

/* ------------------------------------------------------------------------- */
/* String / colour / list                                                    */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kStringOutput[] = {FP("Value", KX_VALUE_STRING)};

void FuncStringConst(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutString(ctx, 0, node.ConfigString("value"));
}

const KX_FuncPort kStringConcatInputs[] = {
    FP("A", KX_VALUE_STRING),
    FP("B", KX_VALUE_STRING),
};

void FuncStringConcat(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutString(ctx, 0, InString(node, ctx, 0) + InString(node, ctx, 1));
}

const KX_FuncPort kStringLengthInputs[] = {FP("Text", KX_VALUE_STRING)};
const KX_FuncPort kIntOutput[] = {FP("Value", KX_VALUE_INT)};

void FuncStringLength(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutInt(ctx, 0, static_cast<int>(InString(node, ctx, 0).size()));
}

const KX_FuncPort kStringReplaceInputs[] = {
    FP("Text", KX_VALUE_STRING),
    FP("Find", KX_VALUE_STRING),
    FP("Replace", KX_VALUE_STRING),
};

void FuncStringReplace(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  std::string text = InString(node, ctx, 0);
  const std::string find = InString(node, ctx, 1);
  const std::string replace = InString(node, ctx, 2);
  if (find.empty()) {
    node.OutString(ctx, 0, text);
    return;
  }
  std::size_t position = 0;
  while ((position = text.find(find, position)) != std::string::npos) {
    text.replace(position, find.size(), replace);
    position += replace.size();
  }
  node.OutString(ctx, 0, text);
}

const KX_FuncPort kColorOutput[] = {FP("Color", KX_VALUE_COLOR)};

void FuncColorConst(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutColor(ctx,
                0,
                node.ConfigFloat("r", 0.0f),
                node.ConfigFloat("g", 0.0f),
                node.ConfigFloat("b", 0.0f),
                node.ConfigFloat("a", 1.0f));
}

const KX_FuncPort kColorFromVectorInputs[] = {FP("Vector", KX_VALUE_VECTOR)};

void FuncColorFromVector(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float v[3];
  node.InVector(ctx, 0, v);
  node.OutColor(ctx, 0, v[0], v[1], v[2], 1.0f);
}

const KX_FuncPort kVectorFromColorInputs[] = {FP("Color", KX_VALUE_COLOR)};

void FuncVectorFromColor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_RuntimeValue value = node.In(ctx, 0);
  float color[4];
  value.ColorTo(color);
  node.OutVector(ctx, 0, color[0], color[1], color[2]);
}

const KX_FuncPort kListLengthInputs[] = {FP("List", KX_VALUE_LIST)};

void FuncListLength(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  void *list = node.InPointer(ctx, 0);
  const auto *values = static_cast<const std::vector<KX_RuntimeValue> *>(list);
  node.OutInt(ctx, 0, values != nullptr ? static_cast<int>(values->size()) : 0);
}

/* --- While ---------------------------------------------------------------- */

/* Bounded synchronous loop.
 *
 * The condition is a floating point expression evaluated against the loop
 * counter `i` and any numeric input port of this node ("i<2.5",
 * "i*Step<10", "i < Threshold"). Because the scheduler expands the whole
 * sequence inside one frame, input port values are captured when OnEnter
 * fires and cannot change between iterations; that restriction is documented
 * in the node docs.
 *
 * `max_iterations` is the hard guard. A condition that never turns false stops
 * at that bound and prints one diagnostic to stderr instead of spinning.
 */
struct KX_WhileState {
  uint32_t count = 0;
};

using KX_WhileVars = std::map<std::string, double>;

class KX_WhileCondition {
 public:
  KX_WhileCondition(const std::string &text, const KX_WhileVars *vars)
      : text_(text), vars_(vars)
  {
  }
  bool Evaluate(int index, bool *valid)
  {
    pos_ = 0;
    valid_ = true;
    const bool result = Additive(index) != 0.0;
    Skip();
    if (pos_ < text_.size()) {
      valid_ = false;
    }
    *valid = valid_;
    return result;
  }

  bool EvaluateBool(int index, bool *valid)
  {
    pos_ = 0;
    valid_ = true;
    const double left = Additive(index);
    Skip();
    if (pos_ >= text_.size()) {
      *valid = valid_;
      return left != 0.0;
    }
    const char *op = MatchOperator();
    if (op == nullptr) {
      valid_ = false;
      *valid = false;
      return false;
    }
    const double right = Additive(index);
    Skip();
    if (pos_ < text_.size()) {
      valid_ = false;
      *valid = false;
      return false;
    }
    bool result = false;
    if (std::strcmp(op, "==") == 0) {
      result = left == right;
    }
    else if (std::strcmp(op, "!=") == 0) {
      result = left != right;
    }
    else if (std::strcmp(op, "<") == 0) {
      result = left < right;
    }
    else if (std::strcmp(op, "<=") == 0) {
      result = left <= right;
    }
    else if (std::strcmp(op, ">") == 0) {
      result = left > right;
    }
    else {
      result = left >= right;
    }
    *valid = valid_;
    return result;
  }

 private:
  void Skip()
  {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }

  bool Peek(char c)
  {
    Skip();
    return pos_ < text_.size() && text_[pos_] == c;
  }

  const char *MatchOperator()
  {
    Skip();
    if (pos_ + 1 < text_.size()) {
      const std::string two = text_.substr(pos_, 2);
      if (two == "==" || two == "!=" || two == "<=" || two == ">=") {
        pos_ += 2;
        return two == "==" ? "==" : two == "!=" ? "!=" : two == "<=" ? "<=" : ">=";
      }
    }
    if (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == '<' || c == '>') {
        ++pos_;
        return c == '<' ? "<" : ">";
      }
    }
    return nullptr;
  }

  double Additive(int index)
  {
    double value = Term(index);
    while (true) {
      Skip();
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        const char op = text_[pos_++];
        const double right = Term(index);
        value = op == '+' ? value + right : value - right;
      }
      else {
        break;
      }
    }
    return value;
  }

  double Term(int index)
  {
    double value = Factor(index);
    while (true) {
      Skip();
      if (pos_ < text_.size() && (text_[pos_] == '*' || text_[pos_] == '/')) {
        const char op = text_[pos_++];
        const double right = Factor(index);
        if (op == '/') {
          if (right == 0.0) {
            valid_ = false;
            return 0.0;
          }
          value /= right;
        }
        else {
          value *= right;
        }
      }
      else {
        break;
      }
    }
    return value;
  }

  double Factor(int index)
  {
    Skip();
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
      return -Factor(index);
    }
    if (pos_ < text_.size() && text_[pos_] == '+') {
      ++pos_;
      return Factor(index);
    }
    if (Peek('(')) {
      ++pos_;
      const double value = Additive(index);
      Skip();
      if (pos_ < text_.size() && text_[pos_] == ')') {
        ++pos_;
      }
      else {
        valid_ = false;
      }
      return value;
    }
    if (pos_ < text_.size() &&
        (std::isalpha(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
      size_t begin = pos_;
      while (pos_ < text_.size() &&
             (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
        ++pos_;
      }
      std::string name = text_.substr(begin, pos_ - begin);
      for (char &c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (name == "i") {
        return static_cast<double>(index);
      }
      if (vars_ != nullptr) {
        const auto it = vars_->find(name);
        if (it != vars_->end()) {
          return it->second;
        }
      }
      valid_ = false;
      return 0.0;
    }
    if (pos_ < text_.size() &&
        (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
      size_t begin = pos_;
      while (pos_ < text_.size() &&
             (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
        ++pos_;
      }
      const std::string literal = text_.substr(begin, pos_ - begin);
      char *end = nullptr;
      const double value = std::strtod(literal.c_str(), &end);
      if (end == literal.c_str()) {
        valid_ = false;
        return 0.0;
      }
      return value;
    }
    valid_ = false;
    return 0.0;
  }

  std::string text_;
  const KX_WhileVars *vars_ = nullptr;
  size_t pos_ = 0;
  bool valid_ = true;
};

const KX_FuncPort kWhileInputs[] = {
    TP("OnEnter"),
    FP("Condition", KX_VALUE_BOOL),
    FP("Threshold", KX_VALUE_FLOAT),
    FP("Step", KX_VALUE_FLOAT),
};
const KX_FuncPort kWhileOutputs[] = {
    TP("OnIteration"),
    FP("Index", KX_VALUE_INT),
    TP("OnDone"),
};

void FuncWhileExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_WhileState *state = node.StateBlock<KX_WhileState>(ctx);
  state->count = 0;
  if (!node.InTrigger(ctx, 0)) {
    return;
  }

  const int limit = std::max(1, node.ConfigInt("max_iterations", 1000));
  const std::string text = node.ConfigString("condition");

  if (!text.empty()) {
    /* Every non-trigger input port is addressable by its lower-cased name, so
     * the condition can mix `i` with floats/ints/bools wired into this node. */
    KX_WhileVars vars;
    for (uint32_t input = 1; input < node.Spec().input_count; ++input) {
      const KX_FuncPort &port = node.Spec().inputs[input];
      if (port.trigger) {
        continue;
      }
      double value = 0.0;
      switch (port.type) {
        case KX_VALUE_FLOAT:
          value = node.InFloat(ctx, input);
          break;
        case KX_VALUE_INT:
          value = static_cast<double>(node.InInt(ctx, input));
          break;
        case KX_VALUE_BOOL:
          value = node.InBool(ctx, input) ? 1.0 : 0.0;
          break;
        default:
          continue;
      }
      std::string name = port.name;
      for (char &c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      vars[name] = value;
    }
    KX_WhileCondition condition(text, &vars);
    int iterations = 0;
    bool valid = true;
    while (iterations < limit) {
      if (!condition.EvaluateBool(iterations, &valid)) {
        break;
      }
      ++iterations;
    }
    if (!valid) {
      std::fprintf(stderr,
                   "[While] invalid condition '%s' (use `i`, input port names, decimals "
                   "and + - * / comparisons)\n",
                   text.c_str());
    }
    else if (iterations >= limit) {
      std::fprintf(stderr,
                   "[While] stopped at max_iterations=%d: condition '%s' still true\n",
                   limit,
                   text.c_str());
    }
    state->count = static_cast<uint32_t>(iterations);
    if (state->count == 0) {
      node.OutTrigger(ctx, 2, true);
    }
    return;
  }

  /* No condition expression: the bool input is evaluated once per OnEnter. An
   * always-true input is still bounded by max_iterations plus a diagnostic. */
  const bool condition = node.InBool(ctx, 1);
  if (!condition) {
    node.OutTrigger(ctx, 2, true);
    return;
  }
  std::fprintf(stderr,
               "[While] stopped at max_iterations=%d: Condition input still true\n",
               limit);
  state->count = static_cast<uint32_t>(limit);
}

uint32_t FuncWhilePending(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  return node.StateBlock<KX_WhileState>(ctx)->count;
}

void FuncWhileElement(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  node.OutInt(ctx, 1, static_cast<int>(index));
  node.OutTrigger(ctx, 0, true);
}

void FuncWhileFinish(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.StateBlock<KX_WhileState>(ctx)->count = 0;
  node.OutTrigger(ctx, 2, true);
}

/* --- ConstInt ------------------------------------------------------------- */

const KX_FuncPort kConstIntOutputs[] = {FP("Value", KX_VALUE_INT)};

void FuncConstInt(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string text = node.ConfigString("value");
  const int value = !text.empty() ? std::atoi(text.c_str()) : node.ConfigInt("value", 0);
  node.OutInt(ctx, 0, value);
}

/* --- ConstString ---------------------------------------------------------- */

const KX_FuncPort kConstStringOutputs[] = {FP("Value", KX_VALUE_STRING)};

void FuncConstString(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutString(ctx, 0, node.ConfigString("value"));
}

/* --- ConstVector ---------------------------------------------------------- */

const KX_FuncPort kConstVectorOutputs[] = {FP("Value", KX_VALUE_VECTOR)};

void FuncConstVector(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float value[3];
  node.ConfigVector("value", value);
  node.OutVector(ctx, 0, value[0], value[1], value[2]);
}

/* --- Switch --------------------------------------------------------------- */

/* Multi branch trigger. Value selects exactly one CaseN output; values outside
 * [0, output_count-1] go to Default. */
const KX_FuncPort kSwitchInputs[] = {
    TP("OnEnter"),
    FP("Value", KX_VALUE_INT),
};
const KX_FuncPort kSwitchOutputs[] = {
    TP("Case0"),
    TP("Case1"),
    TP("Case2"),
    TP("Case3"),
    TP("Default"),
};

void FuncSwitch(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const int value = node.InInt(ctx, 1);
  const uint32_t case_count = node.Spec().output_count - 1;
  if (value >= 0 && static_cast<uint32_t>(value) < case_count) {
    node.OutTrigger(ctx, static_cast<uint32_t>(value), true);
  }
  else {
    node.OutTrigger(ctx, case_count, true);
  }
}

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define NODE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define VALUE_NODE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

/* Trigger node that observes several same frame trigger inputs (logic gates).
 * The trailing true enables KX_FunctionSpec::needs_trigger_snapshot. */
#define TRIGGER_SNAPSHOT_NODE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false, \
                   nullptr, nullptr, nullptr, false, false, true}

/* Polymorphic node: port value types come from the IR. */
#define POLY_NODE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false, \
                   nullptr, nullptr, nullptr, true}

#define NO_INPUT_VALUE_NODE(id, outs, exec) \
  KX_FunctionSpec {id, nullptr, 0, outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define STATE_NODE(id, ins, outs, exec, tick_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, true}

/* Stateful node whose IR may declare a prefix of its trailing outputs. Only
 * used by Collision, whose contact details were appended in B6.3. */
#define STATE_NODE_LEGACY_OUTPUTS(id, ins, outs, exec, tick_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, true, \
                   nullptr, nullptr, nullptr, false, true}

#define STATIC_STATE_NODE(id, ins, outs, exec, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, false, false}

/* Sequence / While: one OnEnter fans out into a bounded pending sequence. */
#define PENDING_NODE(id, ins, outs, exec, pending_count, pending_element, pending_finish, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, false, false, \
                   pending_count, pending_element, pending_finish}

#define NO_INPUT_STATE_NODE(id, outs, tick_fn, state_type) \
  KX_FunctionSpec {id, nullptr, 0, outs, PORT_COUNT(outs), nullptr, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, false}

const KX_FuncPort kSimpleTriggerInputs[] = {TP("OnEnter")};
const KX_FuncPort kSetCursorVisibleInputs[] = {
    TP("OnEnter"),
    FP("Visible", KX_VALUE_BOOL),
};

void FuncSetCursorVisible(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Input().SetCursorVisible(node.InBool(ctx, 1));
  node.OutTrigger(ctx, 0, true);
}

const KX_FunctionSpec kSpecs[] = {
    NO_INPUT_VALUE_NODE("Keyboard", kKeyboardOutputs, FuncKeyboard),
    NO_INPUT_VALUE_NODE("MouseButton", kMouseButtonOutputs, FuncMouseButton),
    NO_INPUT_VALUE_NODE("MousePosition", kMousePositionOutputs, FuncMousePosition),
    NO_INPUT_VALUE_NODE("MouseDelta", kMouseDeltaOutputs, FuncMouseDelta),
    NO_INPUT_VALUE_NODE("MouseWheel", kMouseWheelOutputs, FuncMouseWheel),
    NO_INPUT_VALUE_NODE("JoystickAxis", kJoystickAxisOutputs, FuncJoystickAxis),
    NO_INPUT_VALUE_NODE("JoystickButton", kJoystickButtonOutputs, FuncJoystickButton),
    NODE("SetMousePosition", kSetMousePositionInputs, kDoneOutput, FuncSetMousePosition),
    NODE("SetCursorVisible", kSetCursorVisibleInputs, kDoneOutput, FuncSetCursorVisible),
    POLY_NODE("Print", kTriggerValueInputs, kDoneOutput, FuncPrint),
    NODE("QuitGame", kSimpleTriggerInputs, kDoneOutput, FuncQuitGame),
    NODE("RestartGame", kSimpleTriggerInputs, kDoneOutput, FuncRestartGame),
    NODE("SetTimeScale", kSetTimeScaleInputs, kDoneOutput, FuncSetTimeScale),
    NODE("SetGravity", kSetGravityInputs, kDoneOutput, FuncSetGravity),
    NODE("SetResolution", kSetResolutionInputs, kDoneOutput, FuncSetResolution),
    NODE("SetFullScreen", kSetFullScreenInputs, kDoneOutput, FuncSetFullScreen),
    NODE("SetActiveCamera", kSetActiveCameraInputs, kDoneOutput, FuncSetActiveCamera),
    NODE("LoadScene", kLoadSceneInputs, kDoneOutput, FuncLoadScene),
    NODE("RemoveObject", kObjectActionInputs, kDoneOutput, FuncRemoveObject),
    NODE("SetVisibility", kSetVisibilityInputs, kDoneOutput, FuncSetVisibility),
    NODE("SetParent", kSetParentInputs, kDoneOutput, FuncSetParent),
    NODE("ClearParent", kObjectActionInputs, kDoneOutput, FuncClearParent),
    NODE("SetLinearVelocity", kSetLinearVelocityInputs, kDoneOutput, FuncSetLinearVelocity),
    NODE("ApplyImpulse", kApplyImpulseInputs, kDoneOutput, FuncApplyImpulse),
    NODE("ApplyForce", kApplyForceInputs, kDoneOutput, FuncApplyForce),
    NODE("ApplyTorque", kApplyTorqueInputs, kDoneOutput, FuncApplyTorque),
    NODE("SetDynamics", kTogglePhysicsInputs, kDoneOutput, FuncSetDynamics),
    NODE("SetRigidBody", kTogglePhysicsInputs, kDoneOutput, FuncSetRigidBody),
    NODE("SetCollisionMask", kSetCollisionMaskInputs, kDoneOutput, FuncSetCollisionMask),
    NODE("SendMessage", kSendMessageInputs, kDoneOutput, FuncSendMessage),
    NODE("SetFontText", kSetFontTextInputs, kDoneOutput, FuncSetFontText),
    NODE("SetPosition", kSetTransformInputs, kDoneOutput, FuncSetPosition),
    NODE("SetRotation", kSetTransformInputs, kDoneOutput, FuncSetRotation),
    NODE("SetScale", kSetTransformInputs, kDoneOutput, FuncSetScale),
    NODE("Translate", kSetTransformInputs, kDoneOutput, FuncTranslate),

    STATE_NODE("Timer", kTimerInputs, kTimerOutputs, FuncTimerExec, FuncTimerTick, KX_TimerState),
    STATIC_STATE_NODE("Once", kOnceInputs, kOnceOutputs, FuncOnce, bool),
    PENDING_NODE("Sequence",
                 kSequenceInputs,
                 kSequenceOutputs,
                 FuncSequenceExec,
                 FuncSequencePending,
                 FuncSequenceElement,
                 FuncSequenceFinish,
                 KX_SequenceState),
    PENDING_NODE("While",
                 kWhileInputs,
                 kWhileOutputs,
                 FuncWhileExec,
                 FuncWhilePending,
                 FuncWhileElement,
                 FuncWhileFinish,
                 KX_WhileState),
    NODE("Switch", kSwitchInputs, kSwitchOutputs, FuncSwitch),
    NO_INPUT_VALUE_NODE("ConstInt", kConstIntOutputs, FuncConstInt),
    NO_INPUT_VALUE_NODE("ConstString", kConstStringOutputs, FuncConstString),
    NO_INPUT_VALUE_NODE("ConstVector", kConstVectorOutputs, FuncConstVector),
    NO_INPUT_STATE_NODE("FrameTime", kFrameTimeOutputs, FuncFrameTimeTick, float),

    VALUE_NODE("WorldPosition", kObjectValueInputs, kWorldTransformOutputs, FuncWorldPosition),
    VALUE_NODE("WorldRotation", kObjectValueInputs, kWorldTransformOutputs, FuncWorldRotation),
    VALUE_NODE("WorldScale", kObjectValueInputs, kWorldTransformOutputs, FuncWorldScale),
    VALUE_NODE("GetParent", kObjectValueInputs, kObjectValueOutputs, FuncGetParent),
    NO_INPUT_VALUE_NODE("ObjectByName", kObjectValueOutputs, FuncObjectByName),
    NO_INPUT_VALUE_NODE("GetOwner", kObjectValueOutputs, FuncGetOwner),
    VALUE_NODE("IsNone", kObjectValueInputs, kIsNoneOutputs, FuncIsNone),
    NO_INPUT_VALUE_NODE("GetGravity", kGetGravityOutputs, FuncGetGravity),
    VALUE_NODE("GetVelocity", kObjectValueInputs, kGetVelocityOutputs, FuncGetVelocity),
    VALUE_NODE("Distance", kObjectPairInputs, kDistanceOutputs, FuncDistance),
    VALUE_NODE("WithinRange", kObjectPairRangeInputs, kWithinRangeOutputs, FuncWithinRange),
    STATE_NODE_LEGACY_OUTPUTS("Collision",
                              kCollisionInputs,
                              kCollisionOutputs,
                              nullptr,
                              FuncCollisionTick,
                              KX_CollisionState),

    VALUE_NODE("Compare", kCompareInputs, kCompareOutputs, FuncCompare),
    TRIGGER_SNAPSHOT_NODE("LogicGate", kLogicGateInputs, kLogicGateOutputs, FuncLogicGate),
    VALUE_NODE("LogicNot", kLogicNotInputs, kLogicNotOutputs, FuncLogicNot),
    VALUE_NODE("Clamp", kClampInputs, kFloatOutput, FuncClamp),
    VALUE_NODE("MapRange", kMapRangeInputs, kFloatOutput, FuncMapRange),
    VALUE_NODE("Interpolate", kInterpolateInputs, kFloatOutput, FuncInterpolate),
    VALUE_NODE("RandomFloat", kRandomInputs, kFloatOutput, FuncRandomFloat),
    VALUE_NODE("Threshold", kThresholdInputs, kThresholdOutputs, FuncThreshold),

    NO_INPUT_VALUE_NODE("StringConst", kStringOutput, FuncStringConst),
    VALUE_NODE("StringConcat", kStringConcatInputs, kStringOutput, FuncStringConcat),
    VALUE_NODE("StringLength", kStringLengthInputs, kIntOutput, FuncStringLength),
    VALUE_NODE("StringReplace", kStringReplaceInputs, kStringOutput, FuncStringReplace),
    NO_INPUT_VALUE_NODE("ColorConst", kColorOutput, FuncColorConst),
    VALUE_NODE("ColorFromVector", kColorFromVectorInputs, kColorOutput, FuncColorFromVector),
    VALUE_NODE("VectorFromColor", kVectorFromColorInputs, kWorldTransformOutputs, FuncVectorFromColor),
    VALUE_NODE("ListLength", kListLengthInputs, kIntOutput, FuncListLength),
};

}  // namespace

/* ------------------------------------------------------------------------- */
/* KX_FunctionNode                                                            */
/* ------------------------------------------------------------------------- */

KX_GameObject *KX_FunctionNode::InObject(KX_ExecutionContext &ctx, uint32_t index) const
{
  return static_cast<KX_GameObject *>(ReadInputPointer(ctx, index));
}

KX_ExecResult KX_FunctionNode::Execute(KX_ExecutionContext &ctx)
{
  if (spec_->execute != nullptr) {
    spec_->execute(*this, ctx);
  }
  return KX_ExecResult::Continue;
}

void KX_FunctionNode::Tick(KX_ExecutionContext &ctx, float delta_time)
{
  if (spec_->tick != nullptr) {
    spec_->tick(*this, ctx, delta_time);
  }
}

uint32_t KX_FunctionNode::CollectPendingTriggers(KX_ExecutionContext &ctx)
{
  return spec_->pending_count != nullptr ? spec_->pending_count(*this, ctx) : 0;
}

void KX_FunctionNode::PreparePendingTrigger(KX_ExecutionContext &ctx, uint32_t index)
{
  if (spec_->pending_element != nullptr) {
    spec_->pending_element(*this, ctx, index);
  }
}

void KX_FunctionNode::FinishPendingTriggers(KX_ExecutionContext &ctx)
{
  if (spec_->pending_finish != nullptr) {
    spec_->pending_finish(*this, ctx);
  }
}

bool KX_FunctionNode::Configure(const std::string &config, std::string *error)
{
  config_ = config;
  if (spec_->configure != nullptr) {
    return spec_->configure(*this, config, error);
  }
  return true;
}

bool KX_FunctionNode::IsValueNode() const
{
  return spec_->is_value_node;
}

bool KX_FunctionNode::NeedsTriggerSnapshot() const
{
  return spec_->needs_trigger_snapshot;
}

bool KX_FunctionNode::ExecuteValueOnly(KX_ExecutionContext &ctx)
{
  if (!spec_->is_value_node) {
    return false;
  }
  Execute(ctx);
  return true;
}

KX_ValueType KX_FunctionNode::ResolvedPortType(bool input, uint32_t port) const
{
  /* SetUIWidgetAttr carries the attribute type on its Value port (port 2). */
  if (input && port == 2) {
    const KX_ValueType type = ResolveWidgetAttrPort(spec_, ConfigInt("widget_attr", -1));
    if (type != KX_VALUE_NONE) {
      return type;
    }
  }
  return KX_GameNodeRuntime::ResolvedPortType(input, port);
}

float KX_FunctionNode::ConfigFloat(const char *key, float fallback) const
{
  float value = fallback;
  KX_IRConfigGetFloat(config_, key, &value);
  return value;
}

int KX_FunctionNode::ConfigInt(const char *key, int fallback) const
{
  int value = fallback;
  KX_IRConfigGetInt(config_, key, &value);
  return value;
}

bool KX_FunctionNode::ConfigBool(const char *key, bool fallback) const
{
  bool value = fallback;
  KX_IRConfigGetBool(config_, key, &value);
  return value;
}

std::string KX_FunctionNode::ConfigString(const char *key) const
{
  std::string value;
  KX_IRConfigGetString(config_, key, &value);
  return value;
}

void KX_FunctionNode::ConfigVector(const char *key, float out[3]) const
{
  if (!KX_IRConfigGetVector(config_, key, out)) {
    out[0] = out[1] = out[2] = 0.0f;
  }
}

/* ------------------------------------------------------------------------- */
/* Registration                                                              */
/* ------------------------------------------------------------------------- */

namespace {

KX_NodeSignature MakeSignature(const KX_FunctionSpec &spec)
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
  signature.allow_fewer_outputs = spec.allow_fewer_outputs;
  return signature;
}

const KX_FunctionSpec *SpecFor(const char *type_id)
{
  for (const KX_FunctionSpec &spec : kSpecs) {
    if (std::strcmp(spec.type_id, type_id) == 0) {
      return &spec;
    }
  }
  return nullptr;
}

}  // namespace

void RegisterFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kSpecs) {
    const KX_FunctionSpec *entry = SpecFor(spec.type_id);
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_Func_") + spec.type_id,
        MakeSignature(*entry),
        [entry]() {
          return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry));
        });
  }
}

