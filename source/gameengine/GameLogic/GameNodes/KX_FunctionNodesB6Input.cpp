/*
 * B6.2 input node family.
 *
 * GamepadButton / KeyCombo are condition nodes: Tick polls the adapter level,
 * compares it with the previous frame and enqueues a pending trigger through
 * the P3.5 mechanism. Press, Release and Hold stay distinct.
 */
#include <algorithm>
#include <cmath>
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

/** Baseline captured when two fingers first touch, used for pinch / rotate. */
struct B6ITouchGestureState {
  bool tracking = false;
  float base_distance = 0.0f;
  float base_angle = 0.0f;
};

float ClampUnit(float value)
{
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/** Radial stick deadzone: inside the zone both axes read 0, outside remapped. */
void ApplyRadialDeadzone(float &x, float &y, float deadzone)
{
  deadzone = ClampUnit(deadzone);
  if (deadzone <= 0.0f) {
    return;
  }
  float magnitude = std::sqrt(x * x + y * y);
  if (magnitude > 1.0f) {
    x /= magnitude;
    y /= magnitude;
    magnitude = 1.0f;
  }
  if (magnitude <= deadzone) {
    x = 0.0f;
    y = 0.0f;
    return;
  }
  const float scale = ((magnitude - deadzone) / (1.0f - deadzone)) / magnitude;
  x *= scale;
  y *= scale;
}

/** Scalar 0..1 deadzone; below the zone the result is 0, above it is remapped. */
float ApplyScalarDeadzone(float value, float deadzone)
{
  value = ClampUnit(value);
  deadzone = ClampUnit(deadzone);
  if (value <= deadzone) {
    return 0.0f;
  }
  if (deadzone <= 0.0f) {
    return value;
  }
  return (value - deadzone) / (1.0f - deadzone);
}

/** Wrap an angle delta into (-180, 180] degrees. */
float NormalizeDegrees(float degrees)
{
  while (degrees > 180.0f) {
    degrees -= 360.0f;
  }
  while (degrees <= -180.0f) {
    degrees += 360.0f;
  }
  return degrees;
}

/* -------------------------------------------------------------- GamepadButton */

const KX_FuncPort kGamepadButtonOutputs[] = {
    TP("OnTrigger"),
    FP("IsDown", KX_VALUE_BOOL),
};

void FuncGamepadButtonTick(KX_FunctionNode &node, KX_ExecutionContext &ctx, float delta_time)
{
  (void)delta_time;
  B6IButtonState *state = node.StateBlock<B6IButtonState>(ctx);
  const int pad = node.ConfigInt("pad", 0);
  const int button = node.ConfigInt("button", 0);
  /* Buttons are digital on DEV_Joystick (0/1), but the adapter can report an
   * analog value; the deadzone is compared against that value. */
  const float deadzone = ClampUnit(node.ConfigFloat("deadzone", 0.0f));
  const float value = ClampUnit(ctx.Input().GetGamepadButtonValue(pad, button));
  const bool down = value > deadzone;
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
  ApplyRadialDeadzone(x, y, node.ConfigFloat("deadzone", 0.0f));
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
  node.Out(ctx, 0, KX_RuntimeValue::Float(
                      ApplyScalarDeadzone(value, node.ConfigFloat("deadzone", 0.0f))));
}

/* ---------------------------------------------------------------- TouchInput */

const KX_FuncPort kTouchInputOutputs[] = {
    FP("Count", KX_VALUE_INT),
    FP("Position0", KX_VALUE_VECTOR),
    FP("Position1", KX_VALUE_VECTOR),
    FP("Down0", KX_VALUE_BOOL),
    FP("Pressure0", KX_VALUE_FLOAT),
    FP("Pressure1", KX_VALUE_FLOAT),
    FP("Down1", KX_VALUE_BOOL),
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
  node.Out(ctx, 4, KX_RuntimeValue::Float(ClampUnit(ctx.Input().GetTouchPressure(0))));
  node.Out(ctx, 5, KX_RuntimeValue::Float(ClampUnit(ctx.Input().GetTouchPressure(1))));
  node.Out(ctx, 6, KX_RuntimeValue::Bool(down1));
}

/* ---------------------------------------------------------------- TouchGet */

const KX_FuncPort kTouchGetInputs[] = {
    FP("Index", KX_VALUE_INT),
};
const KX_FuncPort kTouchGetOutputs[] = {
    FP("Position", KX_VALUE_VECTOR),
    FP("Pressure", KX_VALUE_FLOAT),
    FP("Down", KX_VALUE_BOOL),
};

/** Random access to any touch point, so 3rd+ fingers are readable. */
void FuncTouchGet(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const int index = std::max(0, node.InInt(ctx, 0));
  float x = 0.0f;
  float y = 0.0f;
  bool down = false;
  ctx.Input().GetTouch(index, x, y, down);
  node.Out(ctx, 0, KX_RuntimeValue::Vector(x, y, 0.0f));
  node.Out(ctx, 1, KX_RuntimeValue::Float(ClampUnit(ctx.Input().GetTouchPressure(index))));
  node.Out(ctx, 2, KX_RuntimeValue::Bool(down));
}

/* --------------------------------------------------------------- GamepadCount */

const KX_FuncPort kGamepadCountOutputs[] = {
    FP("Count", KX_VALUE_INT),
};

void FuncGamepadCount(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.Out(ctx, 0, KX_RuntimeValue::Int(ctx.Input().GetGamepadCount()));
}

/* -------------------------------------------------------------- TouchGesture */

const KX_FuncPort kTouchGestureOutputs[] = {
    FP("PinchScale", KX_VALUE_FLOAT),
    FP("Rotation", KX_VALUE_FLOAT),
    FP("Active", KX_VALUE_BOOL),
};

/**
 * Two-finger pinch / rotate.
 *
 * The baseline is captured on the first frame where both touch points are
 * down; PinchScale is distance / baseline and Rotation is the signed angle
 * delta in degrees. Lifting either finger drops the baseline, so the next
 * two-finger contact starts from scale 1 / rotation 0 again.
 */
void FuncTouchGesture(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6ITouchGestureState *state = node.StateBlock<B6ITouchGestureState>(ctx);

  float x0 = 0.0f;
  float y0 = 0.0f;
  bool down0 = false;
  float x1 = 0.0f;
  float y1 = 0.0f;
  bool down1 = false;
  ctx.Input().GetTouch(0, x0, y0, down0);
  ctx.Input().GetTouch(1, x1, y1, down1);

  const bool active = down0 && down1;
  float scale = 1.0f;
  float rotation = 0.0f;
  if (active) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float angle = std::atan2(dy, dx) * 57.29577951308232f;
    if (state != nullptr && !state->tracking) {
      state->tracking = true;
      state->base_distance = distance > 1e-4f ? distance : 1e-4f;
      state->base_angle = angle;
    }
    if (state != nullptr) {
      scale = distance / state->base_distance;
      rotation = NormalizeDegrees(angle - state->base_angle);
    }
  }
  else if (state != nullptr) {
    state->tracking = false;
  }

  node.Out(ctx, 0, KX_RuntimeValue::Float(scale));
  node.Out(ctx, 1, KX_RuntimeValue::Float(rotation));
  node.Out(ctx, 2, KX_RuntimeValue::Bool(active));
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
    ctx.Input().SetInputMap(name, binding, node.ConfigInt("priority", 0));
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
  for (const std::unique_ptr<KX_GameNodeRuntime> &runtime : plan->nodes) {
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
      ctx.Input().SetInputMap(name, value, candidate->ConfigInt("priority", 0));
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
  /* Intensities at or below the deadzone are ignored; OnDone still fires so
   * graphs never stall on a suppressed request. */
  const float deadzone = ClampUnit(node.ConfigFloat("deadzone", 0.0f));
  if (intensity <= deadzone) {
    node.OutTrigger(ctx, 0, true);
    return;
  }
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
    B6I_VALUE("TouchGet", kTouchGetInputs, kTouchGetOutputs, FuncTouchGet),
    B6I_VALUE("GamepadCount", kB6NoInputs, kGamepadCountOutputs, FuncGamepadCount),
    B6I_VALUE_STATE("TouchGesture",
                    kB6NoInputs,
                    kTouchGestureOutputs,
                    FuncTouchGesture,
                    B6ITouchGestureState),
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
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
