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

/** \file KX_FunctionNodesB6Legacy.cpp
 *  \ingroup gamelogic
 *
 *  B6.5 legacy compatibility nodes.
 *
 *  These runtime types exist only because the legacy editor nodes expose a
 *  port set that differs from the hand written native nodes (extra outputs,
 *  trigger instead of value ports, different order). The port order here
 *  mirrors the legacy socket declaration exactly, so `native_catalog.py` can
 *  map the old node without changing its layout. Existing native nodes are
 *  never modified: old IR keeps compiling against its original signature.
 */

#include "KX_FunctionNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"
#include "KX_PropertyAccessor.h"
#include "MT_Matrix3x3.h"
#include "MT_Quaternion.h"
#include "MT_Vector3.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

std::string LegacyInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

/* ------------------------------------------------------------------------- */
/* Timer (legacy ULTimer: trigger in, trigger out, no Elapsed value)          */
/* ------------------------------------------------------------------------- */

struct KX_LegacyTimerState {
  float elapsed = 0.0f;
  float duration = 0.0f;
  bool running = false;
};

const KX_FuncPort kLegacyTimerInputs[] = {
    TP("OnEnter"),
    FP("Seconds", KX_VALUE_FLOAT),
};
const KX_FuncPort kLegacyTimerOutputs[] = {TP("WhenElapsed")};

void FuncLegacyTimerExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const bool start = node.InTrigger(ctx, 0);
  const float duration = node.InFloat(ctx, 1);
  KX_LegacyTimerState *state = node.StateBlock<KX_LegacyTimerState>(ctx);
  if (start && duration > 0.0f) {
    state->duration = duration;
    state->elapsed = 0.0f;
    state->running = true;
  }
}

void FuncLegacyTimerTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  KX_LegacyTimerState *state = node.StateBlock<KX_LegacyTimerState>(ctx);
  if (!state->running) {
    return;
  }
  state->elapsed += delta_time;
  if (state->elapsed >= state->duration) {
    state->elapsed = state->duration;
    state->running = false;
    node.OutTrigger(ctx, 0, true);
  }
}

/* ------------------------------------------------------------------------- */
/* Quit game (legacy ULEndGame has no output socket at all)                   */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacyQuitInputs[] = {TP("OnEnter")};

void FuncLegacyQuit(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (node.InTrigger(ctx, 0)) {
    /* RequestQuit is idempotent, same call as the native QuitGame node. */
    ctx.SceneQuery().RequestQuit();
  }
}

/* ------------------------------------------------------------------------- */
/* Clear parent (legacy emits the former parent as a second output)          */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacyClearParentInputs[] = {
    TP("OnEnter"),
    FP("Child", KX_VALUE_OBJECT),
};
const KX_FuncPort kLegacyClearParentOutputs[] = {
    TP("OnDone"),
    FP("Parent", KX_VALUE_OBJECT),
};

void FuncLegacyClearParent(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *child = node.InObject(ctx, 1);
  /* Read the parent before clearing: the legacy node reports the previous
   * parent so graphs can re-parent or restore it afterwards. */
  KX_GameObject *parent = ctx.Objects().GetParent(child);
  ctx.Objects().ClearParent(child);
  node.OutObject(ctx, 1, parent);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Message received (legacy also outputs the sender as a GameObject)          */
/* ------------------------------------------------------------------------- */

struct KX_LegacyMessageState {
  std::vector<KX_Message> *messages = nullptr;
};

const KX_FuncPort kLegacyMessageInputs[] = {
    FP("Subject", KX_VALUE_STRING),
    FP("Target", KX_VALUE_OBJECT),
};
const KX_FuncPort kLegacyMessageOutputs[] = {
    TP("OnMessage"),
    FP("Body", KX_VALUE_STRING),
    FP("Messenger", KX_VALUE_OBJECT),
};

void FuncLegacyMessageTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_LegacyMessageState *state = node.StateBlock<KX_LegacyMessageState>(ctx);
  if (state->messages != nullptr) {
    delete state->messages;
    state->messages = nullptr;
  }

  KX_GameObject *receiver = nullptr;
  if (node.ConfigBool("use_target", false)) {
    receiver = node.InObject(ctx, 1);
  }
  if (receiver == nullptr) {
    receiver = ctx.Owner();
  }
  if (receiver == nullptr) {
    return;
  }
  state->messages = new std::vector<KX_Message>();
  /* Read the name through the object adapter so the node does not touch the
   * KX_GameObject internals; the adapter is already the boundary every other
   * object access uses. */
  *state->messages = ctx.Message().Receive(ctx.Objects().GetName(receiver),
                                           LegacyInString(node, ctx, 0));
}

uint32_t FuncLegacyMessageCount(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_LegacyMessageState *state = node.StateBlock<KX_LegacyMessageState>(ctx);
  return state->messages != nullptr ? static_cast<uint32_t>(state->messages->size()) : 0;
}

void FuncLegacyMessageElement(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  KX_LegacyMessageState *state = node.StateBlock<KX_LegacyMessageState>(ctx);
  if (state->messages == nullptr || index >= state->messages->size()) {
    return;
  }
  const KX_Message &message = (*state->messages)[index];
  node.OutString(ctx, 1, message.body);
  /* The bus stores the sender as a name; resolve it back to an object so the
   * legacy Messenger socket keeps its Object type. */
  node.OutObject(ctx, 2, ctx.SceneQuery().FindObject(message.from));
  node.OutTrigger(ctx, 0, true);
}

void FuncLegacyMessageFinish(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_LegacyMessageState *state = node.StateBlock<KX_LegacyMessageState>(ctx);
  if (state->messages != nullptr) {
    delete state->messages;
    state->messages = nullptr;
  }
}

/* ------------------------------------------------------------------------- */
/* Object by name (legacy takes the name as an input port)                    */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacyObjectByNameInputs[] = {FP("Name", KX_VALUE_STRING)};
const KX_FuncPort kLegacyObjectByNameOutputs[] = {FP("Object", KX_VALUE_OBJECT)};

void FuncLegacyObjectByName(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutObject(ctx, 0, ctx.SceneQuery().FindObject(LegacyInString(node, ctx, 0)));
}

/* Legacy GetObject is a data block selector: pass the chosen object through. */
const KX_FuncPort kLegacyGetObjectInputs[] = {FP("Object", KX_VALUE_OBJECT)};
const KX_FuncPort kLegacyGetObjectOutputs[] = {FP("Object", KX_VALUE_OBJECT)};

void FuncLegacyGetObject(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutObject(ctx, 0, node.InObject(ctx, 0));
}

/* ------------------------------------------------------------------------- */
/* Time data (legacy order Time, Delta, FPS; native FrameTime is Delta, Time) */
/* ------------------------------------------------------------------------- */

struct KX_LegacyTimeState {
  float time = 0.0f;
  float fps = 0.0f;
};

const KX_FuncPort kLegacyTimeOutputs[] = {
    FP("Time", KX_VALUE_FLOAT),
    FP("Delta", KX_VALUE_FLOAT),
    FP("FPS", KX_VALUE_FLOAT),
};

void FuncLegacyTimeTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  KX_LegacyTimeState *state = node.StateBlock<KX_LegacyTimeState>(ctx);
  state->time += delta_time;
  state->fps = delta_time > 1e-6f ? 1.0f / delta_time : 0.0f;
  node.OutFloat(ctx, 0, state->time);
  node.OutFloat(ctx, 1, delta_time);
  node.OutFloat(ctx, 2, state->fps);
}

/* ------------------------------------------------------------------------- */
/* Simple value (legacy exposes four typed inputs and four typed outputs)     */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacySimpleValueInputs[] = {
    FP("Float", KX_VALUE_FLOAT),
    FP("Integer", KX_VALUE_INT),
    FP("Boolean", KX_VALUE_BOOL),
    FP("String", KX_VALUE_STRING),
};
const KX_FuncPort kLegacySimpleValueOutputs[] = {
    FP("Float", KX_VALUE_FLOAT),
    FP("Integer", KX_VALUE_INT),
    FP("Boolean", KX_VALUE_BOOL),
    FP("String", KX_VALUE_STRING),
};

void FuncLegacySimpleValue(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  /* The editor enables exactly one pair; mirroring all four keeps the IR
   * contract stable when the user switches the data type. */
  node.OutFloat(ctx, 0, node.InFloat(ctx, 0));
  node.OutInt(ctx, 1, node.InInt(ctx, 1));
  node.OutBool(ctx, 2, node.InBool(ctx, 2));
  node.OutString(ctx, 3, LegacyInString(node, ctx, 3));
}

/* ------------------------------------------------------------------------- */
/* Set material (legacy passes slot and material name as input ports)         */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacySetMaterialInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Slot", KX_VALUE_INT),
    FP("Material", KX_VALUE_STRING),
};
const KX_FuncPort kLegacySetMaterialOutputs[] = {TP("OnDone")};

void FuncLegacySetMaterial(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Objects().SetMaterial(
      node.InObject(ctx, 1), LegacyInString(node, ctx, 3), node.InInt(ctx, 2));
  /* The legacy node always continues to Done; the adapter already reports a
   * missing material on stderr. */
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Pulsify (legacy turns a continuous True condition into a timed pulse)      */
/* ------------------------------------------------------------------------- */

struct KX_LegacyPulsifyState {
  float now = 0.0f;
  float last_emit = 0.0f;
  bool has_emit = false;
};

const KX_FuncPort kLegacyPulsifyInputs[] = {
    TP("OnEnter"),
    FP("Gap", KX_VALUE_FLOAT),
};
const KX_FuncPort kLegacyPulsifyOutputs[] = {TP("Out")};

void FuncLegacyPulsifyTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  KX_LegacyPulsifyState *state = node.StateBlock<KX_LegacyPulsifyState>(ctx);
  state->now += delta_time;
}

void FuncLegacyPulsifyExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_LegacyPulsifyState *state = node.StateBlock<KX_LegacyPulsifyState>(ctx);
  const float gap = node.InFloat(ctx, 1);
  const bool emit = !state->has_emit || state->now - state->last_emit >= gap - 1e-5f;
  if (emit) {
    state->last_emit = state->now;
    state->has_emit = true;
  }
  node.OutTrigger(ctx, 0, emit);
}

/* ------------------------------------------------------------------------- */
/* Barrier (legacy relays True only after the condition stayed True long enough)*/
/* ------------------------------------------------------------------------- */

struct KX_LegacyBarrierState {
  float now = 0.0f;
  float start = 0.0f;
  uint64_t last_frame = ~0ull;
  bool started = false;
};

const KX_FuncPort kLegacyBarrierInputs[] = {
    TP("OnEnter"),
    FP("Time", KX_VALUE_FLOAT),
};
const KX_FuncPort kLegacyBarrierOutputs[] = {TP("Out")};

void FuncLegacyBarrierTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  KX_LegacyBarrierState *state = node.StateBlock<KX_LegacyBarrierState>(ctx);
  state->now += delta_time;
}

void FuncLegacyBarrierExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_LegacyBarrierState *state = node.StateBlock<KX_LegacyBarrierState>(ctx);
  const uint64_t frame = ctx.Frame();
  /* A frame gap means the condition was False in between, so the barrier has
   * to start counting again from the resumed True condition. */
  if (!state->started || frame != state->last_frame + 1ull) {
    state->start = state->now;
    state->started = true;
  }
  state->last_frame = frame;
  const float required = node.InFloat(ctx, 1);
  node.OutTrigger(ctx, 0, state->now - state->start >= required - 1e-5f);
}

/* ------------------------------------------------------------------------- */
/* Evaluate property (legacy compares a game property against a value)       */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacyEvaluatePropertyInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Property", KX_VALUE_STRING),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kLegacyEvaluatePropertyOutputs[] = {
    TP("IfTrue"),
    FP("Value", KX_VALUE_FLOAT),
};

bool LegacyPropertyNumber(KX_PropertyBackend &backend,
                          KX_GameObject *object,
                          const std::string &name,
                          float &out)
{
  const KX_ValueType type = backend.Type(object, name);
  if (type != KX_VALUE_FLOAT && type != KX_VALUE_INT && type != KX_VALUE_BOOL) {
    return false;
  }
  const KX_RuntimeValue value = backend.Read(object, name, nullptr);
  out = type == KX_VALUE_FLOAT ? value.AsFloat() :
        type == KX_VALUE_INT ? static_cast<float>(value.AsInt()) : (value.AsBool() ? 1.0f : 0.0f);
  return true;
}

bool LegacyCompare(int op, float a, float b)
{
  switch (op) {
    case 1: return a != b;
    case 2: return a > b;
    case 3: return a < b;
    case 4: return a >= b;
    case 5: return a <= b;
    default: return a == b;
  }
}

void FuncLegacyEvaluateProperty(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = node.InObject(ctx, 0);
  const std::string name = LegacyInString(node, ctx, 1);
  if (object == nullptr || name.empty()) {
    return;
  }
  /* Mode 1 (internal attribute) has no backend path; game properties only. */
  if (node.ConfigInt("mode", 0) != 0) {
    return;
  }
  float actual = 0.0f;
  KX_PropertyBackend &backend = *ctx.PropertyBackend();
  if (!LegacyPropertyNumber(backend, object, name, actual)) {
    return;
  }
  node.OutFloat(ctx, 1, actual);
  if (LegacyCompare(node.ConfigInt("op", 0), actual, node.InFloat(ctx, 2))) {
    node.OutTrigger(ctx, 0, true);
  }
}

/* ------------------------------------------------------------------------- */
/* Modify property (legacy applies an arithmetic operator and optional clamp) */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacyModifyPropertyInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Property", KX_VALUE_STRING),
    FP("Value", KX_VALUE_FLOAT),
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};
const KX_FuncPort kLegacyModifyPropertyOutputs[] = {TP("OnDone")};

float LegacyApplyOperator(const std::string &op, float a, float b)
{
  if (op == "SUB") {
    return a - b;
  }
  if (op == "MUL") {
    return a * b;
  }
  if (op == "DIV") {
    return b != 0.0f ? a / b : a;
  }
  if (op == "POW") {
    return std::pow(a, b);
  }
  if (op == "MOD") {
    return b != 0.0f ? std::fmod(a, b) : a;
  }
  if (op == "FDIV") {
    return b != 0.0f ? std::floor(a / b) : a;
  }
  return a + b;
}

void FuncLegacyModifyProperty(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  const std::string name = LegacyInString(node, ctx, 2);
  if (object == nullptr || name.empty() || node.ConfigInt("mode", 0) != 0) {
    return;
  }
  float current = 0.0f;
  KX_PropertyBackend &backend = *ctx.PropertyBackend();
  if (!LegacyPropertyNumber(backend, object, name, current)) {
    return;
  }
  float next = LegacyApplyOperator(node.ConfigString("op"), current, node.InFloat(ctx, 3));
  if (node.ConfigBool("clamp", false)) {
    const float low = node.InFloat(ctx, 4);
    const float high = node.InFloat(ctx, 5);
    if (high >= low) {
      next = std::min(std::max(next, low), high);
    }
  }
  backend.Write(object, name, KX_RuntimeValue::Float(next));
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Align axis to vector (legacy exposes the axis and a blend factor)          */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kLegacyAlignAxisInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Vector", KX_VALUE_VECTOR),
    FP("Axis", KX_VALUE_INT),
    FP("Factor", KX_VALUE_FLOAT),
};
const KX_FuncPort kLegacyAlignAxisOutputs[] = {TP("OnDone")};

/** Rotation taking `from` onto `to`; degenerate inputs are reported. */
bool RotationFromTo(const MT_Vector3 &from, const MT_Vector3 &to, MT_Quaternion &out)
{
  if (from.length() < 1e-6f || to.length() < 1e-6f) {
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
    MT_Vector3 axis = MT_Vector3(0.0f, 0.0f, 1.0f).cross(a);
    if (axis.length() < 1e-6f) {
      axis = MT_Vector3(1.0f, 0.0f, 0.0f);
    }
    out = MT_Quaternion(axis.normalized(), MT_Scalar(M_PI));
    return true;
  }
  MT_Vector3 axis = a.cross(b);
  out = MT_Quaternion(axis.normalized(), std::acos(dot));
  return true;
}

void FuncLegacyAlignAxis(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  float target[3];
  node.InVector(ctx, 2, target);
  /* Signed axis enum: 0..2 = +X/+Y/+Z, 3..5 = -X/-Y/-Z. */
  const int axis_index = node.InInt(ctx, 3);
  float axis[3] = {0.0f, 0.0f, 0.0f};
  if (axis_index >= 0 && axis_index < 6) {
    const int component = axis_index % 3;
    axis[component] = axis_index < 3 ? 1.0f : -1.0f;
  }
  if (object == nullptr || MT_Vector3(axis).length() < 1e-6f ||
      MT_Vector3(target).length() < 1e-6f) {
    node.OutTrigger(ctx, 0, true);
    return;
  }

  float orientation[3];
  if (!ctx.Objects().GetWorldOrientation(object, orientation)) {
    node.OutTrigger(ctx, 0, true);
    return;
  }
  const MT_Quaternion current =
      MT_Matrix3x3(MT_Vector3(orientation[0], orientation[1], orientation[2])).getRotation();
  /* The legacy `local` flag selects whether the axis is expressed in object
   * space (rotated into world) or is already a world direction. */
  const MT_Vector3 axis_world = node.ConfigBool("local", false) ?
                                    MT_Matrix3x3(current) * MT_Vector3(axis).normalized() :
                                    MT_Vector3(axis).normalized();
  MT_Quaternion delta;
  if (!RotationFromTo(axis_world, MT_Vector3(target).normalized(), delta)) {
    node.OutTrigger(ctx, 0, true);
    return;
  }
  float factor = node.InFloat(ctx, 4);
  factor = std::min(std::max(factor, 0.0f), 1.0f);
  const MT_Quaternion next = current.slerp(delta * current, factor);

  MT_Matrix3x3 matrix(next);
  float yaw;
  float pitch;
  float roll;
  matrix.getEuler(yaw, pitch, roll);
  const float euler[3] = {yaw, pitch, roll};
  ctx.Objects().SetWorldOrientation(object, euler);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define B6L_VALUE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define B6L_ACTION(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define B6L_NO_OUTPUT(id, ins, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), nullptr, 0, exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define B6L_STATE(id, ins, outs, exec, tick_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, \
                   tick_fn != nullptr, false}

#define NO_INPUT_STATE_NODE(id, outs, tick_fn, state_type) \
  KX_FunctionSpec {id, nullptr, 0, outs, PORT_COUNT(outs), nullptr, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, \
                   false}

#define B6L_SEQUENCE(id, ins, outs, tick_fn, count_fn, element_fn, finish_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), nullptr, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, \
                   false, count_fn, element_fn, finish_fn}

const KX_FunctionSpec kB6LegacySpecs[] = {
    B6L_STATE("TimerLegacy",
              kLegacyTimerInputs,
              kLegacyTimerOutputs,
              FuncLegacyTimerExec,
              FuncLegacyTimerTick,
              KX_LegacyTimerState),
    B6L_NO_OUTPUT("QuitGameLegacy", kLegacyQuitInputs, FuncLegacyQuit),
    B6L_ACTION("ClearParentLegacy",
               kLegacyClearParentInputs,
               kLegacyClearParentOutputs,
               FuncLegacyClearParent),
    B6L_SEQUENCE("MessageReceivedLegacy",
                 kLegacyMessageInputs,
                 kLegacyMessageOutputs,
                 FuncLegacyMessageTick,
                 FuncLegacyMessageCount,
                 FuncLegacyMessageElement,
                 FuncLegacyMessageFinish,
                 KX_LegacyMessageState),
    B6L_VALUE("ObjectByNameLegacy",
              kLegacyObjectByNameInputs,
              kLegacyObjectByNameOutputs,
              FuncLegacyObjectByName),
    B6L_VALUE("GetObjectLegacy",
              kLegacyGetObjectInputs,
              kLegacyGetObjectOutputs,
              FuncLegacyGetObject),
    NO_INPUT_STATE_NODE("TimeDataLegacy", kLegacyTimeOutputs, FuncLegacyTimeTick, KX_LegacyTimeState),
    B6L_VALUE("SimpleValueLegacy",
              kLegacySimpleValueInputs,
              kLegacySimpleValueOutputs,
              FuncLegacySimpleValue),
    B6L_ACTION("SetMaterialLegacy",
               kLegacySetMaterialInputs,
               kLegacySetMaterialOutputs,
               FuncLegacySetMaterial),
    B6L_STATE("PulsifyLegacy",
              kLegacyPulsifyInputs,
              kLegacyPulsifyOutputs,
              FuncLegacyPulsifyExec,
              FuncLegacyPulsifyTick,
              KX_LegacyPulsifyState),
    B6L_STATE("BarrierLegacy",
              kLegacyBarrierInputs,
              kLegacyBarrierOutputs,
              FuncLegacyBarrierExec,
              FuncLegacyBarrierTick,
              KX_LegacyBarrierState),
    B6L_ACTION("EvaluatePropertyLegacy",
               kLegacyEvaluatePropertyInputs,
               kLegacyEvaluatePropertyOutputs,
               FuncLegacyEvaluateProperty),
    B6L_ACTION("ModifyPropertyLegacy",
               kLegacyModifyPropertyInputs,
               kLegacyModifyPropertyOutputs,
               FuncLegacyModifyProperty),
    B6L_ACTION("AlignAxisToVectorLegacy",
               kLegacyAlignAxisInputs,
               kLegacyAlignAxisOutputs,
               FuncLegacyAlignAxis),
};

#undef PORT_COUNT

KX_NodeSignature MakeB6LegacySignature(const KX_FunctionSpec &spec)
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

void RegisterB6LegacyFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6LegacySpecs) {
    const KX_FunctionSpec *entry = &spec;
    const KX_NodeSignature signature = MakeB6LegacySignature(spec);
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6L_") + spec.type_id,
        signature,
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
