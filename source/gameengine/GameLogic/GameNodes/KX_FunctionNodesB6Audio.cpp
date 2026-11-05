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
#include <cstdio>
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
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
