/*
 * B6.2 input node family.
 *
 * GamepadButton / KeyCombo are condition nodes: Tick polls the adapter level,
 * compares it with the previous frame and enqueues a pending trigger through
 * the P3.5 mechanism. Press, Release and Hold stay distinct.
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "KX_ExecutionContext.h"
#include "KX_ExecutionPlan.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"

#define B6I_ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))
#define FP(name, type) \
  { name, type, false }
#define TP(name) \
  { name, KX_VALUE_NONE, true }
#define B6I_COUNTS(array) static_cast<uint32_t>(B6I_ARRAY_LEN(array))

#define B6I_VALUE(id, ins, outs, exec) \
  { id, ins, B6I_COUNTS(ins), outs, B6I_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, true, false, false }

#define B6I_VALUE_STATE(id, ins, outs, exec, state_type) \
  { id, ins, B6I_COUNTS(ins), outs, B6I_COUNTS(outs), exec, nullptr, nullptr, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    true, false, false }

#define B6I_ACTION(id, ins, outs, exec) \
  { id, ins, B6I_COUNTS(ins), outs, B6I_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, false, false, false }

#define B6I_TICK_COND(id, ins, outs, exec, tick, state_type) \
  { id, ins, B6I_COUNTS(ins), outs, B6I_COUNTS(outs), exec, nullptr, tick, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    false, true, false }

namespace {

/** Empty input table for nodes with no input sockets. */
const KX_FuncPort kB6NoInputs[] = {};

struct B6IButtonState {
  bool last_down = false;
};

struct B6IInputValueState {
  bool warned = false;
};

/* -------------------------------------------------------------- GamepadButton */

const KX_FuncPort kGamepadButtonOutputs[] = {
    TP("OnTrigger"),
    FP("IsDown", KX_VALUE_BOOL),
};

void FuncGamepadButtonTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  B6IButtonState *state = node.StateBlock<B6IButtonState>(ctx);
  const bool down = ctx.Input().IsGamepadButtonDown(node.ConfigInt("pad", 0),
                                                     node.ConfigInt("button", 0));
  node.Out(ctx, 1, KX_RuntimeValue::Bool(down));
  if (state == nullptr) {
    return;
  }

  const int edge = node.ConfigInt("edge", 0);
  bool emit = false;
  if (edge == 0) {  /* Press */
    emit = down && !state->last_down;
  }
  else if (edge == 1) {  /* Release */
    emit = !down && state->last_down;
  }
  else {  /* Hold */
    emit = down;
  }
  state->last_down = down;
  if (emit) {
    node.PendingTrigger(ctx, 0);
  }
}

/* -------------------------------------------------------------------- KeyCombo */

const KX_FuncPort kKeyComboOutputs[] = {
    TP("OnTrigger"),
    FP("IsDown", KX_VALUE_BOOL),
};

void FuncKeyComboTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  B6IButtonState *state = node.StateBlock<B6IButtonState>(ctx);
  const bool down = ctx.Input().IsKeyComboDown(node.ConfigString("keys"));
  node.Out(ctx, 1, KX_RuntimeValue::Bool(down));
  if (state == nullptr) {
    return;
  }

  const int edge = node.ConfigInt("edge", 0);
  bool emit = false;
  if (edge == 0) {
    emit = down && !state->last_down;
  }
  else if (edge == 1) {
    emit = !down && state->last_down;
  }
  else {
    emit = down;
  }
  state->last_down = down;
  if (emit) {
    node.PendingTrigger(ctx, 0);
  }
}

/* ---------------------------------------------------------------- GamepadAxis */

const KX_FuncPort kGamepadAxisOutputs[] = {
    FP("X", KX_VALUE_FLOAT),
    FP("Y", KX_VALUE_FLOAT),
};

void FuncGamepadAxis(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float x = 0.0f;
  float y = 0.0f;
  ctx.Input().GetGamepadAxis(node.ConfigInt("pad", 0), node.ConfigInt("axis", 0), x, y);
  node.Out(ctx, 0, KX_RuntimeValue::Float(x));
  node.Out(ctx, 1, KX_RuntimeValue::Float(y));
}

/* ------------------------------------------------------------- GamepadTrigger */

const KX_FuncPort kGamepadTriggerOutputs[] = {
    FP("Value", KX_VALUE_FLOAT),
};

void FuncGamepadTrigger(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float value = 0.0f;
  ctx.Input().GetGamepadTrigger(node.ConfigInt("pad", 0), node.ConfigInt("trigger", 0), value);
  node.Out(ctx, 0, KX_RuntimeValue::Float(value));
}

/* ---------------------------------------------------------------- TouchInput */

const KX_FuncPort kTouchInputOutputs[] = {
    FP("Count", KX_VALUE_INT),
    FP("Position0", KX_VALUE_VECTOR),
    FP("Position1", KX_VALUE_VECTOR),
    FP("Down0", KX_VALUE_BOOL),
};

void FuncTouchInput(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int count = ctx.Input().GetTouchCount();
  node.Out(ctx, 0, KX_RuntimeValue::Int(count));

  float x0 = 0.0f;
  float y0 = 0.0f;
  bool down0 = false;
  ctx.Input().GetTouch(0, x0, y0, down0);
  node.Out(ctx, 1, KX_RuntimeValue::Vector(x0, y0, 0.0f));

  float x1 = 0.0f;
  float y1 = 0.0f;
  bool down1 = false;
  ctx.Input().GetTouch(1, x1, y1, down1);
  node.Out(ctx, 2, KX_RuntimeValue::Vector(x1, y1, 0.0f));
  node.Out(ctx, 3, KX_RuntimeValue::Bool(down0));
  (void)down1;
}

/* ------------------------------------------------------------------- InputMap */

const KX_FuncPort kInputMapInputs[] = {
    TP("OnEnter"),
    FP("ActionName", KX_VALUE_STRING),
    FP("Binding", KX_VALUE_STRING),
};
const KX_FuncPort kInputMapOutputs[] = {
    TP("OnDone"),
};

void FuncInputMap(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string name = node.In(ctx, 1).AsString() != nullptr
                               ? std::string(node.In(ctx, 1).AsString())
                               : std::string();
  const std::string binding = node.In(ctx, 2).AsString() != nullptr
                                  ? std::string(node.In(ctx, 2).AsString())
                                  : std::string();
  if (name.empty() || binding.empty()) {
    std::fprintf(stderr, "[InputMap] ActionName and Binding must not be empty\n");
  }
  else {
    ctx.Input().SetInputMap(name, binding);
  }
  node.OutTrigger(ctx, 0, true);
}

/* --------------------------------------------------------------- GetInputValue */

const KX_FuncPort kGetInputValueInputs[] = {
    FP("ActionName", KX_VALUE_STRING),
};
const KX_FuncPort kGetInputValueOutputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("IsDown", KX_VALUE_BOOL),
};

/**
 * Register every InputMap node of the current graph right now.
 *
 * Value nodes run before trigger propagation, so on the first frame the
 * InputMap action node has not run yet. GetInputValue calls this once on a
 * lookup miss so the binding is visible in the same frame instead of the next
 * one. ActionName / Binding are regular value inputs, so they are already
 * evaluated when their sources are constants.
 */
void RegisterInputMapsFromGraph(KX_ExecutionContext &ctx)
{
  const KX_ExecutionPlan *plan = ctx.Plan();
  if (plan == nullptr) {
    return;
  }
  for (const std::unique_ptr<KX_LogicNodeRuntime> &runtime : plan->nodes) {
    KX_FunctionNode *candidate = dynamic_cast<KX_FunctionNode *>(runtime.get());
    if (candidate == nullptr) {
      continue;
    }
    if (std::strcmp(candidate->Spec().type_id, "InputMap") != 0) {
      continue;
    }
    const KX_RuntimeValue action = candidate->In(ctx, 1);
    const KX_RuntimeValue binding = candidate->In(ctx, 2);
    const char *name = action.AsString();
    const char *value = binding.AsString();
    if (name != nullptr && value != nullptr && name[0] != '\0' && value[0] != '\0') {
      ctx.Input().SetInputMap(name, value);
    }
  }
}

void FuncGetInputValue(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6IInputValueState *state = node.StateBlock<B6IInputValueState>(ctx);
  const std::string name = node.In(ctx, 0).AsString() != nullptr
                               ? std::string(node.In(ctx, 0).AsString())
                               : std::string();
  float value = 0.0f;
  bool down = false;
  bool bound = !name.empty() && ctx.Input().ReadInputMap(name, value, down);
  if (!bound && !name.empty()) {
    /* InputMap registration is normally trigger driven; pull it forward so the
     * very first frame already sees the binding. */
    RegisterInputMapsFromGraph(ctx);
    bound = ctx.Input().ReadInputMap(name, value, down);
  }
  if (!bound && state != nullptr && !state->warned) {
    state->warned = true;
    std::fprintf(stderr,
                 "[GetInputValue] action '%s' is not registered via InputMap\n",
                 name.c_str());
  }
  node.Out(ctx, 0, KX_RuntimeValue::Float(value));
  node.Out(ctx, 1, KX_RuntimeValue::Bool(down));
}

/* -------------------------------------------------------------- VibrateGamepad */

const KX_FuncPort kVibrateGamepadInputs[] = {
    TP("OnEnter"),
    FP("Pad", KX_VALUE_INT),
    FP("Intensity", KX_VALUE_FLOAT),
    FP("Duration", KX_VALUE_FLOAT),
};
const KX_FuncPort kVibrateGamepadOutputs[] = {
    TP("OnDone"),
};

void FuncVibrateGamepad(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int pad = node.InInt(ctx, 1);
  const float intensity = node.InFloat(ctx, 2);
  const float duration = node.InFloat(ctx, 3);
  if (!ctx.Input().VibrateGamepad(pad, intensity, duration)) {
    std::fprintf(stderr, "[VibrateGamepad] no gamepad with rumble support at index %d\n", pad);
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------- Registry */

const KX_FunctionSpec kB6InputSpecs[] = {
    B6I_TICK_COND("GamepadButton",
                  kB6NoInputs,
                  kGamepadButtonOutputs,
                  nullptr,
                  FuncGamepadButtonTick,
                  B6IButtonState),
    B6I_VALUE("GamepadAxis", kB6NoInputs, kGamepadAxisOutputs, FuncGamepadAxis),
    B6I_VALUE("GamepadTrigger", kB6NoInputs, kGamepadTriggerOutputs, FuncGamepadTrigger),
    B6I_VALUE("TouchInput", kB6NoInputs, kTouchInputOutputs, FuncTouchInput),
    B6I_TICK_COND("KeyCombo",
                  kB6NoInputs,
                  kKeyComboOutputs,
                  nullptr,
                  FuncKeyComboTick,
                  B6IButtonState),
    B6I_ACTION("InputMap", kInputMapInputs, kInputMapOutputs, FuncInputMap),
    B6I_VALUE_STATE("GetInputValue",
                    kGetInputValueInputs,
                    kGetInputValueOutputs,
                    FuncGetInputValue,
                    B6IInputValueState),
    B6I_ACTION("VibrateGamepad",
               kVibrateGamepadInputs,
               kVibrateGamepadOutputs,
               FuncVibrateGamepad),
};

KX_NodeSignature MakeB6InputSignature(const KX_FunctionSpec &spec)
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

void RegisterB6InputFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6InputSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6Input_") + spec.type_id,
        MakeB6InputSignature(*entry),
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
