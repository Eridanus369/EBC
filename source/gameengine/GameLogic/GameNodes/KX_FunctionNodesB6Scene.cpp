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

/** \file KX_FunctionNodesB6Scene.cpp
 *  \ingroup gamelogic
 *
 *  B6.3 advanced scene control nodes.
 *
 *  LoadSceneAsync reports frame stepped progress, but the engine still swaps
 *  the scene in one frame at the end of the operation: there is no incremental
 *  asset streaming in KX_KetsjiEngine::LoadScene. The progress is a timed
 *  feedback ramp, clearly documented in docs/B6_SCENE_ADVANCED.md. The fade in
 *  TransitionScene is driven through the existing UI adapter, never through a
 *  private render path.
 */

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

#define B6SC_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))
#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}
#define B6SC_COUNTS(array) static_cast<uint32_t>(B6SC_ARRAY_LEN(array))
#define B6SC_ACTION(id, ins, outs, exec, tick, state_type) \
  { id, ins, B6SC_COUNTS(ins), outs, B6SC_COUNTS(outs), exec, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    false, tick != nullptr, false }
#define B6SC_VALUE_NODE(id, ins, outs, exec) \
  { id, ins, B6SC_COUNTS(ins), outs, B6SC_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, true, false, false }
/* Zero-input value source; positional so the port contract scanner can read
 * the input and output arrays from args 1 and 2. */
#define B6SC_VALUE_SOURCE(id, ins, outs, exec) \
  { id, ins, 0, outs, B6SC_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, true, false, false }

namespace {

const float kLoadSceneFeedbackSeconds = 0.5f;
const char *kTransitionCanvasName = "__b6_transition_canvas";

std::string B6SceneInString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

/* ------------------------------------------------------------------------- */
/* LoadSceneAsync                                                             */
/* ------------------------------------------------------------------------- */

struct B6LoadSceneState {
  bool active;
  bool failed;
  float progress;
  char scene[128];
};

struct B6NoState {
  int reserved;
};

struct B6TransitionState {
  bool active;
  bool switched;
  float elapsed;
  float duration;
  char target[128];
  int canvas;
  int overlay;
};

void B6SceneCopyName(char *destination, const std::string &name)
{
  const size_t limit = 127;
  const size_t length = name.size() < limit ? name.size() : limit;
  for (size_t i = 0; i < length; ++i) {
    destination[i] = name[i];
  }
  destination[length] = '\0';
}

const KX_FuncPort kLoadSceneAsyncInputs[] = {
    TP("OnEnter"),
    FP("SceneName", KX_VALUE_STRING),
};
const KX_FuncPort kLoadSceneAsyncOutputs[] = {
    FP("OnProgress", KX_VALUE_FLOAT),
    TP("OnLoaded"),
    TP("OnFail"),
};

void FuncLoadSceneAsync(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6LoadSceneState *state = node.StateBlock<B6LoadSceneState>(ctx);
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  if (state->active) {
    /* An unconnected OnEnter makes this node a per frame root; do not restart
     * the feedback while a load is in flight. */
    return;
  }
  const std::string scene = B6SceneInString(node, ctx, 1);
  state->failed = false;
  state->active = false;
  state->progress = 0.0f;
  state->scene[0] = '\0';

  if (scene.empty() || !ctx.SceneControl().HasScene(scene)) {
    state->failed = true;
    node.OutFloat(ctx, 0, 0.0f);
    node.OutTrigger(ctx, 1, false);
    node.OutTrigger(ctx, 2, true);
    std::fprintf(stderr, "GameNode: LoadSceneAsync('%s') failed: no such scene\n", scene.c_str());
    return;
  }
  state->active = true;
  B6SceneCopyName(state->scene, scene);
  node.OutFloat(ctx, 0, 0.0f);
  node.OutTrigger(ctx, 1, false);
  node.OutTrigger(ctx, 2, false);
}

void FuncLoadSceneAsyncTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  B6LoadSceneState *state = node.StateBlock<B6LoadSceneState>(ctx);
  if (!state->active) {
    return;
  }
  state->progress += delta_time / kLoadSceneFeedbackSeconds;
  if (state->progress > 1.0f) {
    state->progress = 1.0f;
  }
  node.OutFloat(ctx, 0, state->progress);
  if (node.ConfigBool("verbose", false)) {
    std::fprintf(stderr,
                 "GameNode: LoadSceneAsync('%s') progress=%.3f\n",
                 state->scene,
                 double(state->progress));
  }
  if (state->progress >= 1.0f) {
    /* The engine applies the replacement at the end of this logic frame. The
     * progress ramp above is feedback, not streamed asset loading. */
    const bool ok = ctx.SceneQuery().LoadScene(state->scene);
    if (!ok) {
      state->failed = true;
      node.OutTrigger(ctx, 1, false);
      node.OutTrigger(ctx, 2, true);
      std::fprintf(stderr,
                   "GameNode: LoadSceneAsync('%s') failed at replacement\n",
                   state->scene);
    }
    else {
      node.OutTrigger(ctx, 1, true);
      node.OutTrigger(ctx, 2, false);
    }
    state->active = false;
  }
}

/* ------------------------------------------------------------------------- */
/* UnloadScene                                                                */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kUnloadSceneInputs[] = {
    TP("OnEnter"),
    FP("SceneName", KX_VALUE_STRING),
};
const KX_FuncPort kUnloadSceneOutputs[] = {
    TP("OnDone"),
    TP("OnFail"),
};

void FuncUnloadScene(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::string scene = B6SceneInString(node, ctx, 1);
  if (scene.empty()) {
    std::vector<std::string> active;
    ctx.SceneControl().ActiveSceneNames(active);
    if (!active.empty()) {
      scene = active.front();
    }
  }

  std::string reason;
  const KX_SceneUnloadResult result = ctx.SceneControl().UnloadSceneDetailed(scene, &reason);
  switch (result) {
    case KX_SceneUnloadResult::Success:
      node.OutTrigger(ctx, 0, true);
      node.OutTrigger(ctx, 1, false);
      return;
    case KX_SceneUnloadResult::SceneActive:
      /* An OnDone here would report success without unloading anything, which
       * is the worst kind of contract. Fail loudly instead. */
      std::fprintf(stderr,
                   "GameNode: UnloadScene('%s'): unload failed: %s\n",
                   scene.c_str(),
                   reason.empty() ? "scene is active" : reason.c_str());
      break;
    case KX_SceneUnloadResult::Unsupported:
      std::fprintf(stderr,
                   "GameNode: UnloadScene('%s'): unload unsupported: %s\n",
                   scene.c_str(),
                   reason.empty() ? "engine RemoveScene disabled" : reason.c_str());
      break;
    case KX_SceneUnloadResult::Missing:
      std::fprintf(stderr,
                   "GameNode: UnloadScene('%s'): unload failed: %s\n",
                   scene.c_str(),
                   reason.empty() ? "scene not found" : reason.c_str());
      break;
    default:
      std::fprintf(stderr, "GameNode: UnloadScene('%s'): unload failed: unknown result\n",
                   scene.c_str());
      break;
  }
  node.OutTrigger(ctx, 0, false);
  node.OutTrigger(ctx, 1, true);
}

/* ------------------------------------------------------------------------- */
/* TransitionScene                                                            */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kTransitionSceneInputs[] = {
    TP("OnEnter"),
    FP("FromScene", KX_VALUE_STRING),
    FP("ToScene", KX_VALUE_STRING),
    FP("Duration", KX_VALUE_FLOAT),
};
const KX_FuncPort kTransitionSceneOutputs[] = {
    TP("OnDone"),
};

void B6TransitionSetOpacity(KX_FunctionNode &node, KX_ExecutionContext &ctx, B6TransitionState *state, float alpha)
{
  (void)node;
  if (state->overlay >= 0) {
    ctx.UI().SetAttribute(state->overlay,
                          "opacity",
                          KX_RuntimeValue::Float(alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha)));
  }
}

void B6TransitionCloseOverlay(KX_FunctionNode &node, KX_ExecutionContext &ctx, B6TransitionState *state)
{
  (void)node;
  if (state->overlay >= 0) {
    ctx.UI().DestroyElement(state->overlay);
    state->overlay = -1;
  }
  if (state->canvas >= 0) {
    ctx.UI().DestroyElement(state->canvas);
    state->canvas = -1;
  }
}

void FuncTransitionScene(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6TransitionState *state = node.StateBlock<B6TransitionState>(ctx);
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string from = B6SceneInString(node, ctx, 1);
  const std::string to = B6SceneInString(node, ctx, 2);
  float duration = node.InFloat(ctx, 3);
  if (duration < 0.0f) {
    duration = 0.0f;
  }

  if (state->active) {
    return;
  }

  state->active = false;
  state->switched = false;
  state->elapsed = 0.0f;
  state->duration = duration;
  B6SceneCopyName(state->target, to);

  bool valid = !to.empty() && ctx.SceneControl().HasScene(to);
  if (valid && !from.empty() && !ctx.SceneControl().HasScene(from)) {
    std::fprintf(stderr,
                 "GameNode: TransitionScene from '%s' is not an active scene; using '%s' anyway\n",
                 from.c_str(),
                 to.c_str());
  }

  if (!valid) {
    std::fprintf(stderr, "GameNode: TransitionScene('%s') failed: no such scene\n", to.c_str());
    node.OutTrigger(ctx, 0, true);
    return;
  }

  if (duration <= 0.0001f) {
    ctx.SceneQuery().LoadScene(to);
    node.OutTrigger(ctx, 0, true);
    return;
  }

  const float width = ctx.SceneQuery().ScreenWidth();
  const float height = ctx.SceneQuery().ScreenHeight();
  state->canvas = -1;
  state->overlay = -1;
  if (width > 0.0f && height > 0.0f) {
    state->canvas = ctx.UI().CreateCanvas(kTransitionCanvasName);
    if (state->canvas >= 0) {
      state->overlay = ctx.UI().CreateLabel(state->canvas, std::string());
      if (state->overlay >= 0) {
        ctx.UI().SetAttribute(state->overlay, "pos", KX_RuntimeValue::Vector(0.0f, 0.0f, 0.0f));
        ctx.UI().SetAttribute(state->overlay,
                              "size",
                              KX_RuntimeValue::Vector(width, height, 0.0f));
        ctx.UI().SetAttribute(state->overlay,
                              "bg_color",
                              KX_RuntimeValue::Color(0.0f, 0.0f, 0.0f, 1.0f));
        B6TransitionSetOpacity(node, ctx, state, 0.0f);
      }
    }
  }
  state->active = true;
}

void FuncTransitionSceneTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  B6TransitionState *state = node.StateBlock<B6TransitionState>(ctx);
  if (!state->active) {
    return;
  }
  state->elapsed += delta_time;
  const float half = state->duration * 0.5f;
  const float alpha = state->elapsed < half
                          ? (half > 0.0001f ? state->elapsed / half : 1.0f)
                          : (state->duration - state->elapsed) / (state->duration - half);
  B6TransitionSetOpacity(node, ctx, state, alpha);

  if (!state->switched && state->elapsed >= half) {
    state->switched = true;
    /* Switch at the darkest point of the fade. */
    ctx.SceneQuery().LoadScene(state->target);
  }
  if (state->elapsed >= state->duration) {
    B6TransitionCloseOverlay(node, ctx, state);
    state->active = false;
    node.OutTrigger(ctx, 0, true);
  }
}

/* ------------------------------------------------------------------------- */
/* GetActiveScenes                                                            */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kGetActiveScenesOutputs[] = {
    FP("Scenes", KX_VALUE_LIST),
    FP("Count", KX_VALUE_INT),
};

void FuncGetActiveScenes(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  std::vector<std::string> names;
  ctx.SceneControl().ActiveSceneNames(names);
  KX_ValueList &list = ctx.Pool().AllocList(names.size());
  for (const std::string &name : names) {
    list.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(name)));
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&list));
  node.OutInt(ctx, 1, int(names.size()));
}

/* ------------------------------------------------------------------------- */
/* SetSceneTimeScale                                                          */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kSetSceneTimeScaleInputs[] = {
    TP("OnEnter"),
    FP("SceneName", KX_VALUE_STRING),
    FP("TimeScale", KX_VALUE_FLOAT),
};
const KX_FuncPort kSetSceneTimeScaleOutputs[] = {
    TP("OnDone"),
};

void FuncSetSceneTimeScale(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string scene = B6SceneInString(node, ctx, 1);
  const float scale = node.InFloat(ctx, 2);
  if (!ctx.SceneControl().SetSceneTimeScale(scene, scale)) {
    std::fprintf(stderr,
                 "GameNode: SetSceneTimeScale('%s', %.3f) declined (the engine only exposes a "
                 "global time scale)\n",
                 scene.c_str(),
                 double(scale));
  }
  node.OutTrigger(ctx, 0, true);
}

const KX_FunctionSpec kB6SceneSpecs[] = {
    B6SC_ACTION("LoadSceneAsync",
                kLoadSceneAsyncInputs,
                kLoadSceneAsyncOutputs,
                FuncLoadSceneAsync,
                FuncLoadSceneAsyncTick,
                B6LoadSceneState),
    B6SC_ACTION("UnloadScene", kUnloadSceneInputs, kUnloadSceneOutputs, FuncUnloadScene, nullptr, B6NoState),
    B6SC_ACTION("TransitionScene",
               kTransitionSceneInputs,
               kTransitionSceneOutputs,
               FuncTransitionScene,
               FuncTransitionSceneTick,
               B6TransitionState),
    B6SC_VALUE_SOURCE("GetActiveScenes", nullptr, kGetActiveScenesOutputs, FuncGetActiveScenes),
    B6SC_ACTION("SetSceneTimeScale",
                kSetSceneTimeScaleInputs,
                kSetSceneTimeScaleOutputs,
                FuncSetSceneTimeScale,
                nullptr,
                B6NoState),
};

KX_NodeSignature MakeB6SceneSignature(const KX_FunctionSpec &spec)
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

void RegisterB6SceneFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6SceneSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6Scene_") + spec.type_id,
        MakeB6SceneSignature(*entry),
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
