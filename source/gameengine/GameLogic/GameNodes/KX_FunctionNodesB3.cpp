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

/** \file KX_FunctionNodesB3.cpp
 *  \ingroup gamelogic
 *
 *  B3 node family: animation playback.
 *
 *  Every node talks to #KX_AnimationAdapter, never to Ketsji directly. Actions
 *  are addressed by layer, matching BL_ActionManager and the legacy animation
 *  nodes. PlayAction/StopAction/SetActionFrame were previously declared in
 *  KX_FunctionNode.cpp against the layer-less object adapter; they live here
 *  now so the whole family shares one contract.
 */

#include "KX_FunctionNode.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

std::string B3InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

KX_AnimationPlayMode B3PlayMode(bool loop)
{
  return loop ? KX_AnimationPlayMode::Loop : KX_AnimationPlayMode::Play;
}

/* ------------------------------------------------------------------------- */
/* Playback                                                                  */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kPlayActionInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Action", KX_VALUE_STRING),
    FP("Start", KX_VALUE_FLOAT),
    FP("End", KX_VALUE_FLOAT),
    FP("Layer", KX_VALUE_INT),
    FP("Loop", KX_VALUE_BOOL),
};
const KX_FuncPort kPlayActionOutputs[] = {
    TP("Started"),
    FP("Frame", KX_VALUE_FLOAT),
};

void FuncPlayAction(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = node.InObject(ctx, 1);
  std::string name = B3InString(node, ctx, 2);
  if (name.empty()) {
    name = node.ConfigString("action");
  }
  const float start = node.InFloat(ctx, 3);
  const float end = node.InFloat(ctx, 4);
  const int layer = node.InInt(ctx, 5);
  const bool started = ctx.Animation().PlayAction(
      object, name, layer, B3PlayMode(node.InBool(ctx, 6)), 1.0f, start, end, 1.0f);
  node.OutFloat(ctx, 1, ctx.Animation().GetActionFrame(object, layer));
  node.OutTrigger(ctx, 0, started);
}

const KX_FuncPort kPlayActionByNameInputs[] = {
    TP("OnEnter"),
    FP("ObjectName", KX_VALUE_STRING),
    FP("Action", KX_VALUE_STRING),
    FP("Start", KX_VALUE_FLOAT),
    FP("End", KX_VALUE_FLOAT),
    FP("Layer", KX_VALUE_INT),
    FP("Loop", KX_VALUE_BOOL),
};

void FuncPlayActionByName(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_GameObject *object = ctx.SceneQuery().FindObject(B3InString(node, ctx, 1));
  std::string name = B3InString(node, ctx, 2);
  if (name.empty()) {
    name = node.ConfigString("action");
  }
  const float start = node.InFloat(ctx, 3);
  const float end = node.InFloat(ctx, 4);
  const int layer = node.InInt(ctx, 5);
  const bool started = ctx.Animation().PlayAction(
      object, name, layer, B3PlayMode(node.InBool(ctx, 6)), 1.0f, start, end, 1.0f);
  node.OutFloat(ctx, 1, ctx.Animation().GetActionFrame(object, layer));
  node.OutTrigger(ctx, 0, started);
}

const KX_FuncPort kStopActionInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Layer", KX_VALUE_INT),
};
const KX_FuncPort kDoneOutput[] = {TP("OnDone")};

void FuncStopAction(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Animation().StopAction(node.InObject(ctx, 1), node.InInt(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSetActionFrameInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Layer", KX_VALUE_INT),
    FP("Frame", KX_VALUE_FLOAT),
};

void FuncSetActionFrame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  node.OutTrigger(
      ctx, 0, ctx.Animation().SetActionFrame(node.InObject(ctx, 1), node.InInt(ctx, 2), node.InFloat(ctx, 3)));
}

const KX_FuncPort kSetActionSpeedInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Layer", KX_VALUE_INT),
    FP("Speed", KX_VALUE_FLOAT),
};

void FuncSetActionSpeed(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  node.OutTrigger(
      ctx, 0, ctx.Animation().SetActionSpeed(node.InObject(ctx, 1), node.InInt(ctx, 2), node.InFloat(ctx, 3)));
}

const KX_FuncPort kSetLayerWeightInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Layer", KX_VALUE_INT),
    FP("Weight", KX_VALUE_FLOAT),
};

void FuncSetLayerWeight(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  node.OutTrigger(
      ctx, 0, ctx.Animation().SetLayerWeight(node.InObject(ctx, 1), node.InInt(ctx, 2), node.InFloat(ctx, 3)));
}

/* ------------------------------------------------------------------------- */
/* Queries                                                                   */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kActionQueryInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Layer", KX_VALUE_INT),
};
const KX_FuncPort kActionFrameOutput[] = {FP("Frame", KX_VALUE_FLOAT)};

void FuncGetActionFrame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutFloat(ctx, 0, ctx.Animation().GetActionFrame(node.InObject(ctx, 0), node.InInt(ctx, 1)));
}

/**
 * Watches one layer and raises OnDone once the running action finishes.
 *
 * The tick keeps a name hash so a new action re-arms the node, and an explicit
 * fire flag so a finished action only produces one trigger.
 */
struct KX_ActionDoneState {
  uint64_t name_hash = 0;
  bool fired = false;
};

const KX_FuncPort kActionDoneInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Layer", KX_VALUE_INT),
};
const KX_FuncPort kActionDoneOutputs[] = {TP("OnDone")};

void FuncActionDoneTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_ActionDoneState *state = node.StateBlock<KX_ActionDoneState>(ctx);
  KX_GameObject *object = node.InObject(ctx, 0);
  const int layer = node.InInt(ctx, 1);
  KX_AnimationAdapter &animation = ctx.Animation();

  const std::string name = animation.GetActionName(object, layer);
  const uint64_t name_hash = name.empty() ? 0u :
      static_cast<uint64_t>(std::hash<std::string>{}(name));
  if (name_hash != state->name_hash) {
    state->name_hash = name_hash;
    state->fired = false;
  }
  if (name_hash == 0u) {
    return;
  }
  if (!animation.IsActionDone(object, layer)) {
    state->fired = false;
    return;
  }
  if (!state->fired) {
    state->fired = true;
    node.PendingTrigger(ctx, 0);
  }
}

/* ------------------------------------------------------------------------- */
/* Message bus                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Polls the message bus once per frame.
 *
 * The engine bus is broadcast and non consuming: every message sent on the
 * previous frame is visible to all receivers for the whole current frame, so
 * one node raises one trigger per matching message and the whole batch is
 * expanded inside the same frame (same contract as ForEach).
 */
struct KX_MessageReceivedState {
  std::vector<KX_Message> *messages = nullptr;
};

const KX_FuncPort kMessageReceivedInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Subject", KX_VALUE_STRING),
};
const KX_FuncPort kMessageReceivedOutputs[] = {
    TP("OnMessage"),
    FP("Body", KX_VALUE_STRING),
    FP("Sender", KX_VALUE_STRING),
};

void FuncMessageReceivedTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_MessageReceivedState *state = node.StateBlock<KX_MessageReceivedState>(ctx);
  if (state->messages != nullptr) {
    delete state->messages;
    state->messages = nullptr;
  }

  KX_GameObject *object = node.InObject(ctx, 0);
  if (object == nullptr) {
    object = ctx.Owner();
  }
  if (object == nullptr) {
    return;
  }
  state->messages = new std::vector<KX_Message>();
  std::string subject = B3InString(node, ctx, 1);
  if (subject.empty()) {
    subject = node.ConfigString("subject");
  }
  /* The editor exports the enum as its integer value (0 exact, 1 prefix). */
  const bool prefix_match = node.ConfigInt("match", 0) == 1;
  if (prefix_match) {
    /* The bus keys messages by exact subject, so a prefix filter has to pull
     * every message of the receiver and match the subject text itself. */
    std::vector<KX_Message> all = ctx.Message().Receive(object->GetName(), "");
    for (KX_Message &message : all) {
      if (subject.empty() || message.subject.compare(0, subject.size(), subject) == 0) {
        state->messages->push_back(message);
      }
    }
  }
  else {
    *state->messages = ctx.Message().Receive(object->GetName(), subject);
  }
}

uint32_t FuncMessageReceivedPending(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_MessageReceivedState *state = node.StateBlock<KX_MessageReceivedState>(ctx);
  return state->messages != nullptr ? static_cast<uint32_t>(state->messages->size()) : 0;
}

void FuncMessageReceivedElement(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  KX_MessageReceivedState *state = node.StateBlock<KX_MessageReceivedState>(ctx);
  if (state->messages == nullptr || index >= state->messages->size()) {
    return;
  }
  const KX_Message &message = (*state->messages)[index];
  node.OutString(ctx, 1, message.body);
  node.OutString(ctx, 2, message.from);
  node.OutTrigger(ctx, 0, true);
}

void FuncMessageReceivedFinish(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_MessageReceivedState *state = node.StateBlock<KX_MessageReceivedState>(ctx);
  if (state->messages != nullptr) {
    delete state->messages;
    state->messages = nullptr;
  }
}

/* ------------------------------------------------------------------------- */
/* Audio                                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Narrow a device handle to the 32-bit integer carried by node ports.
 *
 * #KX_RuntimeValue stores integers as `int`, while #KX_AudioAdapter hands out
 * `uint64_t` ids. A raw `static_cast<int>` turns an id above INT32_MAX into a
 * different (often negative) id, and a later StopSound would then address
 * whatever sound owns the truncated value. Reject such ids here so the node
 * reports "not started" instead of a silently wrapped handle.
 */
constexpr uint64_t kMaxNodeSoundHandle = 0x7fffffffu;

bool B3SoundHandleInt(uint64_t handle, int &out)
{
  out = 0;
  if (handle == 0u) {
    return false;
  }
  if (handle > kMaxNodeSoundHandle) {
    std::fprintf(stderr,
                 "GameNode: sound handle %llu exceeds the 32-bit node port range\n",
                 static_cast<unsigned long long>(handle));
    return false;
  }
  out = static_cast<int>(handle);
  return true;
}

const KX_FuncPort kStartSoundInputs[] = {
    TP("OnEnter"),
    FP("File", KX_VALUE_STRING),
    FP("Loop", KX_VALUE_BOOL),
    FP("Volume", KX_VALUE_FLOAT),
    FP("Pitch", KX_VALUE_FLOAT),
};
const KX_FuncPort kStartSoundOutputs[] = {
    TP("OnStarted"),
    FP("Handle", KX_VALUE_INT),
};

void FuncStartSound(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string file = B3InString(node, ctx, 1);
  if (file.empty()) {
    file = node.ConfigString("file");
  }
  const float volume = node.InFloat(ctx, 3) > 0.0f ? node.InFloat(ctx, 3) : 1.0f;
  const float pitch = node.InFloat(ctx, 4) > 0.0f ? node.InFloat(ctx, 4) : 1.0f;
  const uint64_t handle = ctx.Audio().PlaySound(file, node.InBool(ctx, 2), volume, pitch);
  int handle_int = 0;
  if (!B3SoundHandleInt(handle, handle_int)) {
    /* Out of the node port range: stop it rather than hand out a wrapped id. */
    ctx.Audio().StopSound(handle);
    node.OutInt(ctx, 1, 0);
    node.OutTrigger(ctx, 0, false);
    return;
  }
  node.OutInt(ctx, 1, handle_int);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSoundHandleInputs[] = {
    TP("OnEnter"),
    FP("Handle", KX_VALUE_INT),
};
const KX_FuncPort kSoundVolumeInputs[] = {
    TP("OnEnter"),
    FP("Handle", KX_VALUE_INT),
    FP("Volume", KX_VALUE_FLOAT),
};
const KX_FuncPort kSoundPitchInputs[] = {
    TP("OnEnter"),
    FP("Handle", KX_VALUE_INT),
    FP("Pitch", KX_VALUE_FLOAT),
};

void FuncStopSound(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Audio().StopSound(static_cast<uint64_t>(node.InInt(ctx, 1)));
  node.OutTrigger(ctx, 0, true);
}

void FuncSetSoundVolume(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Audio().SetVolume(static_cast<uint64_t>(node.InInt(ctx, 1)), node.InFloat(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

void FuncSetSoundPitch(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  ctx.Audio().SetPitch(static_cast<uint64_t>(node.InInt(ctx, 1)), node.InFloat(ctx, 2));
  node.OutTrigger(ctx, 0, true);
}

/** Raises OnFinish once per handle after the device reports it stopped. */
struct KX_SoundFinishedState {
  uint64_t handle = 0;
  bool fired = false;
};

const KX_FuncPort kSoundFinishedInputs[] = {FP("Handle", KX_VALUE_INT)};
const KX_FuncPort kSoundFinishedOutputs[] = {TP("OnFinish")};

void FuncSoundFinishedTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_SoundFinishedState *state = node.StateBlock<KX_SoundFinishedState>(ctx);
  const uint64_t handle = static_cast<uint64_t>(node.InInt(ctx, 0));
  if (handle != state->handle) {
    state->handle = handle;
    state->fired = false;
  }
  if (handle == 0u || state->fired) {
    return;
  }
  if (ctx.Audio().IsFinished(handle)) {
    state->fired = true;
    node.PendingTrigger(ctx, 0);
  }
}

const KX_FuncPort kPlaySoundByNameInputs[] = {
    TP("OnEnter"),
    FP("Name", KX_VALUE_STRING),
    FP("Loop", KX_VALUE_BOOL),
    FP("Volume", KX_VALUE_FLOAT),
    FP("Pitch", KX_VALUE_FLOAT),
};
const KX_FuncPort kPlaySoundByNameOutputs[] = {
    TP("OnDone"),
    FP("Handle", KX_VALUE_INT),
    FP("Found", KX_VALUE_BOOL),
};

void FuncPlaySoundByName(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string name = B3InString(node, ctx, 1);
  if (name.empty()) {
    name = node.ConfigString("name");
  }
  const float volume = node.InFloat(ctx, 3) > 0.0f ? node.InFloat(ctx, 3) : 1.0f;
  const float pitch = node.InFloat(ctx, 4) > 0.0f ? node.InFloat(ctx, 4) : 1.0f;
  const uint64_t handle = ctx.Audio().PlaySoundByName(name, node.InBool(ctx, 2), volume, pitch);
  int handle_int = 0;
  const bool started = B3SoundHandleInt(handle, handle_int);
  if (!started && handle != 0u) {
    /* Out of the node port range: stop it rather than hand out a wrapped id. */
    ctx.Audio().StopSound(handle);
  }
  node.OutInt(ctx, 1, handle_int);
  node.OutBool(ctx, 2, started);
  node.OutTrigger(ctx, 0, started);
}

const KX_FuncPort kSet3DListenerInputs[] = {
    TP("OnEnter"),
    FP("Position", KX_VALUE_VECTOR),
    FP("Orientation", KX_VALUE_VECTOR),
};
const KX_FuncPort kSet3DListenerOutputs[] = {TP("OnDone")};

void FuncSet3DListener(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  float position[3] = {0.0f, 0.0f, 0.0f};
  float orientation[3] = {0.0f, 0.0f, -1.0f};
  node.InVector(ctx, 1, position);
  node.InVector(ctx, 2, orientation);
  ctx.Audio().Set3DListener(position, orientation);
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kSceneControlInputs[] = {TP("OnEnter")};
const KX_FuncPort kSuspendSceneOutputs[] = {TP("OnDone"), FP("Suspended", KX_VALUE_BOOL)};
const KX_FuncPort kResumeSceneOutputs[] = {TP("OnDone"), FP("Suspended", KX_VALUE_BOOL)};

void FuncSuspendScene(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const bool done = ctx.SceneControl().SuspendScene();
  node.OutBool(ctx, 1, ctx.SceneControl().IsSuspended());
  node.OutTrigger(ctx, 0, done);
}

void FuncResumeScene(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const bool done = ctx.SceneControl().ResumeScene();
  node.OutBool(ctx, 1, ctx.SceneControl().IsSuspended());
  node.OutTrigger(ctx, 0, done);
}

const KX_FuncPort kAddObjectInputs[] = {TP("OnEnter"), FP("Name", KX_VALUE_STRING)};
const KX_FuncPort kAddObjectOutputs[] = {
    TP("OnDone"),
    FP("Spawned", KX_VALUE_BOOL),
    FP("Object", KX_VALUE_OBJECT),
};

void FuncAddObject(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string name = B3InString(node, ctx, 1);
  if (name.empty()) {
    name = node.ConfigString("name");
  }
  KX_GameObject *object = ctx.SceneControl().AddObject(name);
  node.OutBool(ctx, 1, object != nullptr);
  node.OutObject(ctx, 2, object);
  node.OutTrigger(ctx, 0, object != nullptr);
}

const KX_FuncPort kTouchInputs[] = {
    TP("OnEnter"),
    FP("Camera", KX_VALUE_OBJECT),
    FP("Button", KX_VALUE_INT),
};
const KX_FuncPort kTouchOutputs[] = {
    TP("OnHit"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Point", KX_VALUE_VECTOR),
    FP("Normal", KX_VALUE_VECTOR),
    FP("Distance", KX_VALUE_FLOAT),
    FP("Hit", KX_VALUE_BOOL),
    FP("IsOwner", KX_VALUE_BOOL),
};

/**
 * Mouse click pick: a physics ray from the cursor through the active or given
 * camera. The result is a real collision hit (object, point, normal, distance)
 * and is unrelated to the collision sensors, which test shape overlap instead.
 */
void FuncTouch(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const bool clicked = ctx.Input().MouseButtonActive(node.InInt(ctx, 2), KX_InputMode::Press);
  if (!clicked) {
    node.OutBool(ctx, 5, false);
    node.OutBool(ctx, 6, false);
    node.OutObject(ctx, 1, nullptr);
    node.OutVector(ctx, 2, 0.0f, 0.0f, 0.0f);
    node.OutVector(ctx, 3, 0.0f, 0.0f, 0.0f);
    node.OutFloat(ctx, 4, 0.0f);
    return;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  ctx.Input().MouseNormalized(nx, ny);
  KX_RayHit hit;
  const bool found = ctx.SceneQuery().RayCastFromScreen(nx, ny, node.InObject(ctx, 1), hit);

  node.OutTrigger(ctx, 0, found);
  node.OutBool(ctx, 5, found);
  node.OutBool(ctx, 6, found && hit.object == ctx.Owner());
  node.OutObject(ctx, 1, found ? hit.object : nullptr);
  node.OutVector(ctx, 2, hit.point[0], hit.point[1], hit.point[2]);
  node.OutVector(ctx, 3, hit.normal[0], hit.normal[1], hit.normal[2]);
  node.OutFloat(ctx, 4, hit.distance);
}

/* ------------------------------------------------------------------------- */
/* Radar / proximity                                                         */
/* ------------------------------------------------------------------------- */

/** Emits OnDetect every frame at least one object sits inside the range. */
struct KX_RadarState {
  bool active = false;
};

const KX_FuncPort kRadarInputs[] = {
    FP("Object", KX_VALUE_OBJECT),
    FP("Axis", KX_VALUE_VECTOR),
    FP("Radius", KX_VALUE_FLOAT),
    FP("Angle", KX_VALUE_FLOAT),
    FP("Property", KX_VALUE_STRING),
};
const KX_FuncPort kRadarOutputs[] = {
    TP("OnDetect"),
    FP("Target", KX_VALUE_OBJECT),
    FP("Distance", KX_VALUE_FLOAT),
    FP("Count", KX_VALUE_INT),
};

void FuncRadarTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  KX_RadarState *state = node.StateBlock<KX_RadarState>(ctx);
  KX_GameObject *origin = node.InObject(ctx, 0);
  if (origin == nullptr) {
    origin = ctx.Owner();
  }

  float axis[3] = {0.0f, 0.0f, 1.0f};
  node.InVector(ctx, 1, axis);

  std::vector<KX_ProximityHit> hits;
  ctx.SceneQuery().QueryProximity(origin,
                                  axis,
                                  node.InFloat(ctx, 2),
                                  node.InFloat(ctx, 3),
                                  B3InString(node, ctx, 4),
                                  hits);

  state->active = !hits.empty();
  node.OutInt(ctx, 3, static_cast<int>(hits.size()));
  if (hits.empty()) {
    node.OutObject(ctx, 1, nullptr);
    node.OutFloat(ctx, 2, 0.0f);
    return;
  }
  node.OutObject(ctx, 1, hits.front().object);
  node.OutFloat(ctx, 2, hits.front().distance);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define B3_ACTION(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define B3_VALUE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define B3_TICK_STATE(id, ins, outs, exec, tick_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, false}

/* Polled node which expands one trigger per element after Execute. */
#define B3_TICK_SEQUENCE(id, ins, outs, tick_fn, count_fn, element_fn, finish_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), nullptr, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, \
                   false, count_fn, element_fn, finish_fn}

const KX_FunctionSpec kB3Specs[] = {
    B3_ACTION("PlayAction", kPlayActionInputs, kPlayActionOutputs, FuncPlayAction),
    B3_ACTION("PlayActionByName",
              kPlayActionByNameInputs,
              kPlayActionOutputs,
              FuncPlayActionByName),
    B3_ACTION("StopAction", kStopActionInputs, kDoneOutput, FuncStopAction),
    B3_ACTION("SetActionFrame", kSetActionFrameInputs, kDoneOutput, FuncSetActionFrame),
    B3_ACTION("SetActionSpeed", kSetActionSpeedInputs, kDoneOutput, FuncSetActionSpeed),
    B3_ACTION("SetLayerWeight", kSetLayerWeightInputs, kDoneOutput, FuncSetLayerWeight),
    B3_VALUE("GetActionFrame", kActionQueryInputs, kActionFrameOutput, FuncGetActionFrame),
    B3_TICK_STATE("ActionDone",
                  kActionDoneInputs,
                  kActionDoneOutputs,
                  nullptr,
                  FuncActionDoneTick,
                  KX_ActionDoneState),

    /* B3.2 message bus (broadcast, readable one frame later, cleared per frame) */
    B3_TICK_SEQUENCE("MessageReceived",
                     kMessageReceivedInputs,
                     kMessageReceivedOutputs,
                     FuncMessageReceivedTick,
                     FuncMessageReceivedPending,
                     FuncMessageReceivedElement,
                     FuncMessageReceivedFinish,
                     KX_MessageReceivedState),

    /* B3.2 audio */
    B3_ACTION("StartSound", kStartSoundInputs, kStartSoundOutputs, FuncStartSound),
    B3_ACTION("StopSound", kSoundHandleInputs, kDoneOutput, FuncStopSound),
    B3_ACTION("SetSoundVolume", kSoundVolumeInputs, kDoneOutput, FuncSetSoundVolume),
    B3_ACTION("SetSoundPitch", kSoundPitchInputs, kDoneOutput, FuncSetSoundPitch),
    B3_TICK_STATE("SoundFinished",
                  kSoundFinishedInputs,
                  kSoundFinishedOutputs,
                  nullptr,
                  FuncSoundFinishedTick,
                  KX_SoundFinishedState),
    B3_ACTION("PlaySoundByName",
              kPlaySoundByNameInputs,
              kPlaySoundByNameOutputs,
              FuncPlaySoundByName),
    B3_ACTION("Set3DListener", kSet3DListenerInputs, kSet3DListenerOutputs, FuncSet3DListener),
    B3_ACTION("SuspendScene", kSceneControlInputs, kSuspendSceneOutputs, FuncSuspendScene),
    B3_ACTION("ResumeScene", kSceneControlInputs, kResumeSceneOutputs, FuncResumeScene),
    B3_ACTION("AddObject", kAddObjectInputs, kAddObjectOutputs, FuncAddObject),
    B3_ACTION("Touch", kTouchInputs, kTouchOutputs, FuncTouch),

    /* B3.3 proximity sensor (sphere when Angle == 0, cone otherwise) */
    B3_TICK_STATE("Radar", kRadarInputs, kRadarOutputs, nullptr, FuncRadarTick, KX_RadarState),
};

#undef PORT_COUNT

}  // namespace

namespace {

KX_NodeSignature MakeB3Signature(const KX_FunctionSpec &spec)
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
  return signature;
}

}  // namespace

void RegisterB3FunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB3Specs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB3_") + spec.type_id,
        MakeB3Signature(*entry),
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
