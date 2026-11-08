/*
 * B6.2 advanced audio node family.
 *
 * All nodes go through KX_AudioAdapter; the engine backend maps attenuation,
 * doppler and 3D placement onto audaspace. Buses and zones are documented as
 * equivalent implementations because audaspace has no native concept for
 * either.
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"

#define B6A_ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))
#define FP(name, type) \
  { name, type, false }
#define TP(name) \
  { name, KX_VALUE_NONE, true }
#define B6A_COUNTS(array) static_cast<uint32_t>(B6A_ARRAY_LEN(array))

#define B6A_ACTION(id, ins, outs, exec) \
  { id, ins, B6A_COUNTS(ins), outs, B6A_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, false, false, false }

/* Stateful node: Execute prepares the pass, Tick advances the playlist. */
#define B6A_TICK_STATE(id, ins, outs, exec, tick_fn, state_type) \
  { id, ins, B6A_COUNTS(ins), outs, B6A_COUNTS(outs), exec, nullptr, tick_fn, \
    static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, false }

namespace {

/* ------------------------------------------------------- SetAudioAttenuation */

const KX_FuncPort kSetAudioAttenuationInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Model", KX_VALUE_INT),
    FP("RefDist", KX_VALUE_FLOAT),
    FP("MaxDist", KX_VALUE_FLOAT),
};
const KX_FuncPort kSetAudioAttenuationOutputs[] = {
    TP("OnDone"),
};

void FuncSetAudioAttenuation(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = static_cast<KX_GameObject *>(node.In(ctx, 1).AsPointer());
  const int model = node.InInt(ctx, 2);
  const float reference_distance = node.InFloat(ctx, 3);
  const float maximum_distance = node.InFloat(ctx, 4);
  if (model < 0 || model > 5) {
    std::fprintf(stderr, "[SetAudioAttenuation] model %d out of range 0..5\n", model);
  }
  if (maximum_distance > 0.0f && reference_distance > maximum_distance) {
    std::fprintf(stderr,
                 "[SetAudioAttenuation] RefDist %.3f exceeds MaxDist %.3f\n",
                 static_cast<double>(reference_distance),
                 static_cast<double>(maximum_distance));
  }
  ctx.Audio().SetAttenuation(object, model, reference_distance, maximum_distance);
  node.OutTrigger(ctx, 0, true);
}

/* ----------------------------------------------------------- SetAudioDoppler */

const KX_FuncPort kSetAudioDopplerInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Factor", KX_VALUE_FLOAT),
};
const KX_FuncPort kSetAudioDopplerOutputs[] = {
    TP("OnDone"),
};

void FuncSetAudioDoppler(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_GameObject *object = static_cast<KX_GameObject *>(node.In(ctx, 1).AsPointer());
  const float factor = node.InFloat(ctx, 2);
  if (factor < 0.0f) {
    std::fprintf(stderr, "[SetAudioDoppler] negative factor %.3f clamped to 0\n",
                 static_cast<double>(factor));
  }
  ctx.Audio().SetDopplerFactor(object, std::max(0.0f, factor));
  node.OutTrigger(ctx, 0, true);
}

/* ----------------------------------------------------------------- AudioZone */

const KX_FuncPort kAudioZoneInputs[] = {
    TP("OnEnter"),
    FP("ZoneName", KX_VALUE_STRING),
    FP("Sound", KX_VALUE_STRING),
    FP("Volume", KX_VALUE_FLOAT),
};
const KX_FuncPort kAudioZoneOutputs[] = {
    TP("OnDone"),
};

void FuncAudioZone(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string zone = node.In(ctx, 1).AsString() != nullptr
                               ? std::string(node.In(ctx, 1).AsString())
                               : std::string();
  const std::string sound = node.In(ctx, 2).AsString() != nullptr
                                ? std::string(node.In(ctx, 2).AsString())
                                : std::string();
  const float volume = node.InFloat(ctx, 3);
  ctx.Audio().SetAudioZone(zone, sound, volume);
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------- SetAudioBusVolume */

const KX_FuncPort kSetAudioBusVolumeInputs[] = {
    TP("OnEnter"),
    FP("Bus", KX_VALUE_STRING),
    FP("Volume", KX_VALUE_FLOAT),
};
const KX_FuncPort kSetAudioBusVolumeOutputs[] = {
    TP("OnDone"),
};

void FuncSetAudioBusVolume(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string bus = node.In(ctx, 1).AsString() != nullptr
                              ? std::string(node.In(ctx, 1).AsString())
                              : std::string();
  const float volume = node.InFloat(ctx, 2);
  if (bus.empty()) {
    std::fprintf(stderr, "[SetAudioBusVolume] empty bus name\n");
  }
  ctx.Audio().SetAudioBus(bus, volume);
  node.OutTrigger(ctx, 0, true);
}

/* --------------------------------------------------------------- PlaySound3D */

const KX_FuncPort kPlaySound3DInputs[] = {
    TP("OnEnter"),
    FP("Sound", KX_VALUE_STRING),
    FP("Position", KX_VALUE_VECTOR),
    FP("Volume", KX_VALUE_FLOAT),
    FP("Pitch", KX_VALUE_FLOAT),
};
const KX_FuncPort kPlaySound3DOutputs[] = {
    TP("OnDone"),
};

void FuncPlaySound3D(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string sound = node.In(ctx, 1).AsString() != nullptr
                                ? std::string(node.In(ctx, 1).AsString())
                                : std::string();
  float position[3] = {0.0f, 0.0f, 0.0f};
  node.InVector(ctx, 2, position);
  const float volume = node.InFloat(ctx, 3);
  const float pitch = node.InFloat(ctx, 4);
  /* Owner attribution: a graph bound to an object owns the handle it spawns,
   * so a later object scoped SetAttenuation/SetDoppler can target it. A null
   * owner keeps the sound unowned and therefore globally affected. */
  if (ctx.Audio().PlaySound3DFor(ctx.Owner(), sound, position, volume, pitch) == 0) {
    std::fprintf(stderr, "[PlaySound3D] unknown sound '%s'\n", sound.c_str());
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------ PlaySoundPlaylist */

/* The state block is raw memory (no constructor runs), so the playlist keeps
 * fixed size character buffers instead of std::string/vector. */
constexpr int kPlaylistMaxTracks = 16;
constexpr int kPlaylistNameSize = 48;

struct KX_PlaylistState {
  char names[kPlaylistMaxTracks][kPlaylistNameSize];
  int order[kPlaylistMaxTracks];
  int count;
  int pos;
  int mode;
  uint32_t rng;
  uint64_t handle;
  float volume;
  float pitch;
  bool active;
  bool started;
};

const KX_FuncPort kPlaySoundPlaylistInputs[] = {
    TP("OnEnter"),
    FP("Sounds", KX_VALUE_LIST),
    FP("Mode", KX_VALUE_INT),
    FP("Seed", KX_VALUE_INT),
    FP("Volume", KX_VALUE_FLOAT),
    FP("Pitch", KX_VALUE_FLOAT),
};
const KX_FuncPort kPlaySoundPlaylistOutputs[] = {
    TP("OnDone"),
    FP("Current", KX_VALUE_STRING),
    FP("Index", KX_VALUE_INT),
    FP("Handle", KX_VALUE_INT),
};

/* Deterministic xorshift32: the same seed produces the same shuffle on every
 * platform and build, unlike std::shuffle/std::uniform_int_distribution whose
 * implementations differ between standard libraries. */
uint32_t PlaylistNextRand(uint32_t *state)
{
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

void PlaylistShuffle(KX_PlaylistState *state)
{
  for (int i = 0; i < state->count; ++i) {
    state->order[i] = i;
  }
  for (int i = state->count - 1; i > 0; --i) {
    const uint32_t r = PlaylistNextRand(&state->rng);
    const int j = int(r % uint32_t(i + 1));
    std::swap(state->order[i], state->order[j]);
  }
}

int PlaylistHandleToInt(uint64_t handle)
{
  if (handle == 0u || handle > uint64_t(INT32_MAX)) {
    return 0;
  }
  return int(handle);
}

void PlaylistStartTrack(KX_FunctionNode &node,
                        KX_ExecutionContext &ctx,
                        KX_PlaylistState *state)
{
  const int slot = state->order[state->pos];
  const char *name = state->names[slot];
  if (state->handle != 0u) {
    ctx.Audio().StopSound(state->handle);
    state->handle = 0u;
  }
  state->handle = ctx.Audio().PlaySoundByNameFor(
      ctx.Owner(), name, false, state->volume, state->pitch);
  if (state->handle == 0u) {
    /* Unknown sound: the old B3 PlaySoundByName reports this through Found.
     * Here the pass continues, so report once and let the next tick advance. */
    std::fprintf(stderr, "[PlaySoundPlaylist] unknown sound '%s'\n", name);
  }
  node.OutString(ctx, 1, std::string(name));
  node.OutInt(ctx, 2, state->pos);
  node.OutInt(ctx, 3, PlaylistHandleToInt(state->handle));
}

void FuncPlaySoundPlaylist(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_PlaylistState *state = node.StateBlock<KX_PlaylistState>(ctx);
  if (state->handle != 0u) {
    ctx.Audio().StopSound(state->handle);
  }
  std::memset(state, 0, sizeof(*state));

  KX_ValueList *list = static_cast<KX_ValueList *>(node.In(ctx, 1).AsList());
  const size_t requested = list == nullptr ? 0u : list->size();
  if (list != nullptr && list->size() > size_t(kPlaylistMaxTracks)) {
    std::fprintf(stderr,
                 "[PlaySoundPlaylist] %d tracks requested, only %d kept, %d dropped\n",
                 int(requested),
                 kPlaylistMaxTracks,
                 int(requested - size_t(kPlaylistMaxTracks)));
  }
  const int count =
      list == nullptr ? 0 : int(std::min<size_t>(list->size(), size_t(kPlaylistMaxTracks)));
  state->count = count;
  int first_truncated = -1;
  for (int i = 0; i < count; ++i) {
    const char *name = (*list)[size_t(i)].AsString();
    const int written =
        std::snprintf(state->names[i], kPlaylistNameSize, "%s", name != nullptr ? name : "");
    if (written >= kPlaylistNameSize && first_truncated < 0) {
      first_truncated = i;
    }
  }
  if (first_truncated >= 0) {
    std::fprintf(stderr,
                 "[PlaySoundPlaylist] track %d name longer than %d bytes, truncated\n",
                 first_truncated,
                 kPlaylistNameSize - 1);
  }

  state->mode = std::max(0, std::min(2, node.InInt(ctx, 2)));
  const int seed = node.InInt(ctx, 3);
  state->rng = seed == 0 ? 0x9E3779B9u : uint32_t(seed);
  state->volume = node.InFloat(ctx, 4) > 0.0f ? node.InFloat(ctx, 4) : 1.0f;
  state->pitch = node.InFloat(ctx, 5) > 0.0f ? node.InFloat(ctx, 5) : 1.0f;
  state->pos = 0;
  state->handle = 0u;
  state->active = count > 0;
  state->started = false;

  for (int i = 0; i < count; ++i) {
    state->order[i] = i;
  }
  if (state->mode == 2) {
    PlaylistShuffle(state);
  }

  node.OutString(ctx, 1, count > 0 ? std::string(state->names[state->order[0]]) : std::string());
  node.OutInt(ctx, 2, 0);
  node.OutInt(ctx, 3, 0);
}

void FuncPlaySoundPlaylistTick(KX_FunctionNode &node,
                               KX_ExecutionContext &ctx,
                               float delta_time)
{
  (void)delta_time;
  KX_PlaylistState *state = node.StateBlock<KX_PlaylistState>(ctx);
  if (!state->active || state->count <= 0) {
    return;
  }
  if (!state->started) {
    state->started = true;
    PlaylistStartTrack(node, ctx, state);
    return;
  }
  if (state->handle != 0u && !ctx.Audio().IsFinished(state->handle)) {
    return;
  }
  if (state->handle != 0u) {
    /* Release ownership of the finished handle; the device already stopped it. */
    ctx.Audio().ReleaseSoundOwner(state->handle);
    state->handle = 0u;
  }

  ++state->pos;
  if (state->pos >= state->count) {
    /* Pass boundary: sequential stops here, loop wraps to the first track and
     * shuffle starts a new deterministic pass. OnDone marks the boundary. */
    node.PendingTrigger(ctx, 0);
    if (state->mode == 0) {
      state->active = false;
      node.OutInt(ctx, 2, state->count);
      node.OutInt(ctx, 3, 0);
      return;
    }
    state->pos = 0;
    if (state->mode == 2) {
      const int previous_last = state->count > 0 ? state->order[state->count - 1] : -1;
      PlaylistShuffle(state);
      /* A new pass must not start with the track that just finished. */
      if (state->count > 1 && state->order[0] == previous_last) {
        std::swap(state->order[0], state->order[1]);
      }
    }
  }
  PlaylistStartTrack(node, ctx, state);
}

/* ------------------------------------------------------------------- Registry */

const KX_FunctionSpec kB6AudioSpecs[] = {
    B6A_ACTION("SetAudioAttenuation",
               kSetAudioAttenuationInputs,
               kSetAudioAttenuationOutputs,
               FuncSetAudioAttenuation),
    B6A_ACTION("SetAudioDoppler",
               kSetAudioDopplerInputs,
               kSetAudioDopplerOutputs,
               FuncSetAudioDoppler),
    B6A_ACTION("AudioZone", kAudioZoneInputs, kAudioZoneOutputs, FuncAudioZone),
    B6A_ACTION("SetAudioBusVolume",
               kSetAudioBusVolumeInputs,
               kSetAudioBusVolumeOutputs,
               FuncSetAudioBusVolume),
    B6A_ACTION("PlaySound3D", kPlaySound3DInputs, kPlaySound3DOutputs, FuncPlaySound3D),
    B6A_TICK_STATE("PlaySoundPlaylist",
                   kPlaySoundPlaylistInputs,
                   kPlaySoundPlaylistOutputs,
                   FuncPlaySoundPlaylist,
                   FuncPlaySoundPlaylistTick,
                   KX_PlaylistState),
};

KX_NodeSignature MakeB6AudioSignature(const KX_FunctionSpec &spec)
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

void RegisterB6AudioFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6AudioSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6Audio_") + spec.type_id,
        MakeB6AudioSignature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
