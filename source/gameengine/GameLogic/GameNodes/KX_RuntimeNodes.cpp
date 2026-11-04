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

/** \file KX_RuntimeNodes.cpp
 *  \ingroup gamelogic
 */

#include "KX_RuntimeNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "KX_AssetRegistry.h"
#include "KX_FunctionNode.h"
#include "KX_PropertyAccessor.h"

namespace {

KX_NodePortSignature ValuePort(const char *name, KX_ValueType type)
{
  KX_NodePortSignature port;
  port.name = name;
  port.kind = IRPortKind::Value;
  port.value_type = type;
  return port;
}

KX_NodePortSignature TriggerPort(const char *name)
{
  KX_NodePortSignature port;
  port.name = name;
  port.kind = IRPortKind::Trigger;
  port.value_type = KX_VALUE_NONE;
  return port;
}

/** Value port whose type is resolved from the asset field during ResolveAssets. */
KX_NodePortSignature DynamicValuePort(const char *name)
{
  KX_NodePortSignature port;
  port.name = name;
  port.kind = IRPortKind::Value;
  port.value_type = KX_VALUE_NONE;
  port.dynamic_type = true;
  return port;
}

bool MatchOp(const std::string &name, const char *expected)
{
  return name == expected;
}

float Dot3(const float a[3], const float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/**
 * Cold path shared by AssetGet and AssetSet.
 *
 * Looks the asset up by string id, validates the optional type tag and locates
 * the field. Does not acquire a reference; the caller does that only after the
 * whole binding succeeded.
 */
bool LookupAssetField(KX_AssetRegistry &registry,
                      const char *node_label,
                      const std::string &asset_ref,
                      const std::string &expected_asset_type,
                      const std::string &field_name,
                      KX_AssetHandle *handle,
                      uint32_t *generation,
                      uint32_t *field_index,
                      KX_AssetFieldType *field_type,
                      KX_ValueType *field_value_type,
                      std::string *error)
{
  const KX_AssetRegistry::LookupResult lookup = registry.Lookup(asset_ref);
  if (lookup.asset == nullptr) {
    if (error != nullptr) {
      *error = std::string(node_label) + ": asset '" + asset_ref + "' is not registered";
    }
    return false;
  }

  if (!expected_asset_type.empty() && lookup.asset->asset_type != expected_asset_type) {
    if (error != nullptr) {
      *error = std::string(node_label) + ": asset '" + asset_ref + "' has type '" +
               lookup.asset->asset_type + "' but the node expects '" +
               expected_asset_type + "'";
    }
    return false;
  }

  const int index = lookup.asset->FindField(field_name);
  if (index < 0) {
    if (error != nullptr) {
      *error = std::string(node_label) + ": asset '" + asset_ref + "' has no field '" +
               field_name + "'";
    }
    return false;
  }

  const KX_AssetFieldDesc &field = lookup.asset->Field(static_cast<uint32_t>(index));
  *handle = lookup.handle;
  *generation = lookup.generation;
  *field_index = static_cast<uint32_t>(index);
  *field_type = field.type;
  *field_value_type = KX_AssetFieldValueType(field.type);
  return true;
}

void ParseAssetConfig(const std::string &config,
                      std::string *asset_ref,
                      std::string *field_name,
                      std::string *expected_asset_type)
{
  KX_IRConfigGetString(config, "asset_ref", asset_ref);
  KX_IRConfigGetString(config, "field", field_name);
  KX_IRConfigGetString(config, "asset_type", expected_asset_type);
}

/** Map a config value_type name to the runtime type. */
bool ParseValueTypeName(const std::string &name, KX_ValueType *type)
{
  if (MatchOp(name, "Bool")) {
    *type = KX_VALUE_BOOL;
    return true;
  }
  if (MatchOp(name, "Int")) {
    *type = KX_VALUE_INT;
    return true;
  }
  if (MatchOp(name, "Float")) {
    *type = KX_VALUE_FLOAT;
    return true;
  }
  if (MatchOp(name, "Vector")) {
    *type = KX_VALUE_VECTOR;
    return true;
  }
  return false;
}

/**
 * Read the common ObjectGet/ObjectSet configuration.
 *
 * Both nodes name a property and declare the type of the graph side value.
 */
bool ParseObjectConfig(const std::string &config,
                       const char *node_label,
                       std::string *property_name,
                       KX_ValueType *value_type,
                       std::string *error)
{
  property_name->clear();
  *value_type = KX_VALUE_FLOAT;

  if (!KX_IRConfigGetString(config, "property", property_name) || property_name->empty()) {
    if (error != nullptr) {
      *error = std::string(node_label) + ": missing 'property'";
    }
    return false;
  }

  std::string type_name;
  if (KX_IRConfigGetString(config, "value_type", &type_name) && !type_name.empty()) {
    if (!ParseValueTypeName(type_name, value_type)) {
      if (error != nullptr) {
        *error = std::string(node_label) + ": unknown value_type '" + type_name + "'";
      }
      return false;
    }
  }
  return true;
}

}  // namespace

void RegisterBuiltinGameNodes()
{
#ifdef KX_GAME_NODES_ENGINE
  KX_RegisterEnginePropertyBackend();
#endif

  RegisterGameNodeType<KX_Node_OnInit>("OnInit", "KX_Node_OnInit", KX_Node_OnInit::Signature());
  RegisterGameNodeType<KX_Node_OnUpdate>(
      "OnUpdate", "KX_Node_OnUpdate", KX_Node_OnUpdate::Signature());
  RegisterGameNodeType<KX_Node_ConstFloat>(
      "ConstFloat", "KX_Node_ConstFloat", KX_Node_ConstFloat::Signature());
  RegisterGameNodeType<KX_Node_ConstBool>(
      "ConstBool", "KX_Node_ConstBool", KX_Node_ConstBool::Signature());
  RegisterGameNodeType<KX_Node_Sink>("Sink", "KX_Node_Sink", KX_Node_Sink::Signature());
  RegisterGameNodeType<KX_Node_FloatMath>(
      "FloatMath", "KX_Node_FloatMath", KX_Node_FloatMath::Signature());
  RegisterGameNodeType<KX_Node_VectorMath>(
      "VectorMath", "KX_Node_VectorMath", KX_Node_VectorMath::Signature());
  RegisterGameNodeType<KX_Node_Branch>("Branch", "KX_Node_Branch", KX_Node_Branch::Signature());
  RegisterGameNodeType<KX_Node_Delay>("Delay", "KX_Node_Delay", KX_Node_Delay::Signature());
  RegisterGameNodeType<KX_Node_Raycast>(
      "Raycast", "KX_Node_Raycast", KX_Node_Raycast::Signature());
  RegisterGameNodeType<KX_Node_AssetGet>(
      "AssetGet", "KX_Node_AssetGet", KX_Node_AssetGet::Signature());
  RegisterGameNodeType<KX_Node_AssetSet>(
      "AssetSet", "KX_Node_AssetSet", KX_Node_AssetSet::Signature());
  RegisterGameNodeType<KX_Node_ObjectGet>(
      "ObjectGet", "KX_Node_ObjectGet", KX_Node_ObjectGet::Signature());
  RegisterGameNodeType<KX_Node_ObjectSet>(
      "ObjectSet", "KX_Node_ObjectSet", KX_Node_ObjectSet::Signature());

  /* Bulk migrated editor nodes: table driven, ports declared by their IR node. */
  RegisterFunctionGameNodes();
  RegisterExtendedFunctionGameNodes();
  RegisterB2FunctionGameNodes();
  RegisterB3FunctionGameNodes();
  RegisterB4FileFunctionGameNodes();
  RegisterB4UIFunctionGameNodes();
}

/* ------------------------------------------------------------------------- */
/* Float Math                                                                 */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_FloatMath::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(ValuePort("A", KX_VALUE_FLOAT));
  signature.inputs.push_back(ValuePort("B", KX_VALUE_FLOAT));
  signature.outputs.push_back(ValuePort("Result", KX_VALUE_FLOAT));
  return signature;
}

bool KX_Node_FloatMath::Configure(const std::string &config, std::string *error)
{
  std::string op_name;
  if (!KX_IRConfigGetString(config, "op", &op_name) || op_name.empty()) {
    return true;
  }

  if (MatchOp(op_name, "Add")) {
    op_ = Op::Add;
  }
  else if (MatchOp(op_name, "Sub")) {
    op_ = Op::Sub;
  }
  else if (MatchOp(op_name, "Mul")) {
    op_ = Op::Mul;
  }
  else if (MatchOp(op_name, "Div")) {
    op_ = Op::Div;
  }
  else if (MatchOp(op_name, "Min")) {
    op_ = Op::Min;
  }
  else if (MatchOp(op_name, "Max")) {
    op_ = Op::Max;
  }
  else if (MatchOp(op_name, "Pow")) {
    op_ = Op::Pow;
  }
  else {
    if (error != nullptr) {
      *error = "FloatMath: unknown op '" + op_name + "'";
    }
    return false;
  }
  return true;
}

KX_ExecResult KX_Node_FloatMath::Execute(KX_ExecutionContext &ctx)
{
  const float a = ReadInputFloat(ctx, IN_A);
  const float b = ReadInputFloat(ctx, IN_B);
  float result = 0.0f;

  switch (op_) {
    case Op::Add:
      result = a + b;
      break;
    case Op::Sub:
      result = a - b;
      break;
    case Op::Mul:
      result = a * b;
      break;
    case Op::Div:
      if (b == 0.0f) {
        warning_ = true;
        result = 0.0f;
      }
      else {
        result = a / b;
      }
      break;
    case Op::Min:
      result = std::min(a, b);
      break;
    case Op::Max:
      result = std::max(a, b);
      break;
    case Op::Pow:
      result = std::pow(a, b);
      break;
  }

  OutputValue(ctx, OUT_RESULT) = KX_RuntimeValue::Float(result);
  return KX_ExecResult::Continue;
}

KX_Node_FloatMath::Op KX_Node_FloatMath::Operation() const
{
  return op_;
}

bool KX_Node_FloatMath::Warning() const
{
  return warning_;
}

/* ------------------------------------------------------------------------- */
/* Vector Math                                                                */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_VectorMath::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(ValuePort("A", KX_VALUE_VECTOR));
  signature.inputs.push_back(ValuePort("B", KX_VALUE_VECTOR));
  signature.outputs.push_back(ValuePort("Result", KX_VALUE_VECTOR));
  return signature;
}

bool KX_Node_VectorMath::Configure(const std::string &config, std::string *error)
{
  std::string op_name;
  if (!KX_IRConfigGetString(config, "op", &op_name) || op_name.empty()) {
    return true;
  }

  if (MatchOp(op_name, "Add")) {
    op_ = Op::Add;
  }
  else if (MatchOp(op_name, "Sub")) {
    op_ = Op::Sub;
  }
  else if (MatchOp(op_name, "Mul")) {
    op_ = Op::Mul;
  }
  else if (MatchOp(op_name, "Dot")) {
    op_ = Op::Dot;
  }
  else if (MatchOp(op_name, "Cross")) {
    op_ = Op::Cross;
  }
  else if (MatchOp(op_name, "Normalize")) {
    op_ = Op::Normalize;
  }
  else if (MatchOp(op_name, "Length")) {
    op_ = Op::Length;
  }
  else {
    if (error != nullptr) {
      *error = "VectorMath: unknown op '" + op_name + "'";
    }
    return false;
  }
  return true;
}

KX_ExecResult KX_Node_VectorMath::Execute(KX_ExecutionContext &ctx)
{
  float a[3];
  float b[3];
  ReadInputVector(ctx, IN_A, a);
  ReadInputVector(ctx, IN_B, b);

  float result[3] = {0.0f, 0.0f, 0.0f};

  switch (op_) {
    case Op::Add:
      result[0] = a[0] + b[0];
      result[1] = a[1] + b[1];
      result[2] = a[2] + b[2];
      break;
    case Op::Sub:
      result[0] = a[0] - b[0];
      result[1] = a[1] - b[1];
      result[2] = a[2] - b[2];
      break;
    case Op::Mul:
      result[0] = a[0] * b[0];
      result[1] = a[1] * b[1];
      result[2] = a[2] * b[2];
      break;
    case Op::Dot: {
      const float dot = Dot3(a, b);
      result[0] = dot;
      break;
    }
    case Op::Cross:
      result[0] = a[1] * b[2] - a[2] * b[1];
      result[1] = a[2] * b[0] - a[0] * b[2];
      result[2] = a[0] * b[1] - a[1] * b[0];
      break;
    case Op::Normalize: {
      const float length = std::sqrt(Dot3(a, a));
      if (length > 1e-6f) {
        result[0] = a[0] / length;
        result[1] = a[1] / length;
        result[2] = a[2] / length;
      }
      break;
    }
    case Op::Length: {
      result[0] = std::sqrt(Dot3(a, a));
      break;
    }
  }

  OutputValue(ctx, OUT_RESULT) = KX_RuntimeValue::Vector(result[0], result[1], result[2]);
  return KX_ExecResult::Continue;
}

KX_Node_VectorMath::Op KX_Node_VectorMath::Operation() const
{
  return op_;
}

/* ------------------------------------------------------------------------- */
/* Branch                                                                     */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_Branch::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(ValuePort("Condition", KX_VALUE_BOOL));
  signature.outputs.push_back(TriggerPort("OnTrue"));
  signature.outputs.push_back(TriggerPort("OnFalse"));
  return signature;
}

KX_ExecResult KX_Node_Branch::Execute(KX_ExecutionContext &ctx)
{
  if (!ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  /* The condition is read at trigger time on purpose: the graph can feed it
   * from a value node that changes every frame, so it must never be baked in
   * during compilation. */
  const bool condition = ReadInputBool(ctx, IN_CONDITION);
  SetOutputTrigger(ctx, OUT_TRUE, condition);
  SetOutputTrigger(ctx, OUT_FALSE, !condition);
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Delay                                                                      */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_Delay::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(ValuePort("Duration", KX_VALUE_FLOAT));
  signature.outputs.push_back(TriggerPort("OnDone"));
  signature.state_size = sizeof(KX_DelayState);
  signature.state_alignment = alignof(KX_DelayState);
  signature.needs_tick = true;
  /* A Delay output is a cross frame edge. Ignoring its outgoing edges is what
   * makes Delay a legal cycle breaker during topological sorting. */
  signature.breaks_trigger_cycles = true;
  return signature;
}

KX_ExecResult KX_Node_Delay::Execute(KX_ExecutionContext &ctx)
{
  if (ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    KX_DelayState *state = State<KX_DelayState>(ctx);
    state->remaining_sec = ReadInputFloat(ctx, IN_DURATION);
    state->pending = true;
    SetOutputTrigger(ctx, OUT_DONE, false);
  }
  return KX_ExecResult::Continue;
}

void KX_Node_Delay::Tick(KX_ExecutionContext &ctx, float delta_time)
{
  KX_DelayState *state = State<KX_DelayState>(ctx);
  if (!state->pending) {
    return;
  }

  state->remaining_sec -= delta_time;
  if (state->remaining_sec <= 0.0f) {
    state->remaining_sec = 0.0f;
    state->pending = false;
    SetOutputTrigger(ctx, OUT_DONE, true);
  }
}

/* ------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_Raycast::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(ValuePort("Origin", KX_VALUE_VECTOR));
  signature.inputs.push_back(ValuePort("Direction", KX_VALUE_VECTOR));
  signature.inputs.push_back(ValuePort("MaxDist", KX_VALUE_FLOAT));

  signature.outputs.push_back(TriggerPort("OnHit"));
  signature.outputs.push_back(TriggerPort("OnMiss"));
  signature.outputs.push_back(ValuePort("HitObject", KX_VALUE_OBJECT));
  signature.outputs.push_back(ValuePort("HitPoint", KX_VALUE_VECTOR));
  signature.outputs.push_back(ValuePort("HitNormal", KX_VALUE_VECTOR));
  return signature;
}

KX_ExecResult KX_Node_Raycast::Execute(KX_ExecutionContext &ctx)
{
  if (!ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  float origin[3];
  float direction[3];
  ReadInputVector(ctx, IN_ORIGIN, origin);
  ReadInputVector(ctx, IN_DIRECTION, direction);
  const float max_dist = ReadInputFloat(ctx, IN_MAX_DIST);

  KX_RaycastResult result;
  if (max_dist > 0.0f) {
    result = ctx.Raycast(origin, direction, max_dist);
  }

  const bool hit = result.hit && result.object != nullptr;
  if (hit) {
    OutputValue(ctx, OUT_HIT_OBJECT) = KX_RuntimeValue::Object(result.object);
    OutputValue(ctx, OUT_HIT_POINT) = KX_RuntimeValue::Vector(
        result.point[0], result.point[1], result.point[2]);
    OutputValue(ctx, OUT_HIT_NORMAL) = KX_RuntimeValue::Vector(
        result.normal[0], result.normal[1], result.normal[2]);
  }
  else {
    /* A typed null object is the miss sentinel. Downstream nodes must treat a
     * null pointer as "no object" instead of relying on the trigger path. */
    OutputValue(ctx, OUT_HIT_OBJECT) = KX_RuntimeValue::Object(nullptr);
    OutputValue(ctx, OUT_HIT_POINT) = KX_RuntimeValue::Vector(0.0f, 0.0f, 0.0f);
    OutputValue(ctx, OUT_HIT_NORMAL) = KX_RuntimeValue::Vector(0.0f, 0.0f, 0.0f);
  }

  SetOutputTrigger(ctx, OUT_HIT_TRIGGER, hit);
  SetOutputTrigger(ctx, OUT_MISS_TRIGGER, !hit);
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Asset Get                                                                  */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_AssetGet::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.outputs.push_back(DynamicValuePort("Value"));
  signature.outputs.push_back(TriggerPort("OnRead"));
  return signature;
}

KX_Node_AssetGet::~KX_Node_AssetGet()
{
  if (handle_ != KX_INVALID_ASSET_HANDLE) {
    KX_AssetRegistry::Get().Release(handle_);
  }
}

bool KX_Node_AssetGet::Configure(const std::string &config, std::string *error)
{
  asset_ref_.clear();
  field_name_.clear();
  expected_asset_type_.clear();
  mode_ = Mode::Poll;

  ParseAssetConfig(config, &asset_ref_, &field_name_, &expected_asset_type_);
  if (asset_ref_.empty()) {
    if (error != nullptr) {
      *error = "AssetGet: missing 'asset_ref'";
    }
    return false;
  }
  if (field_name_.empty()) {
    if (error != nullptr) {
      *error = "AssetGet: missing 'field'";
    }
    return false;
  }

  std::string mode_name;
  if (KX_IRConfigGetString(config, "mode", &mode_name) && !mode_name.empty()) {
    if (MatchOp(mode_name, "poll")) {
      mode_ = Mode::Poll;
    }
    else if (MatchOp(mode_name, "trigger")) {
      mode_ = Mode::Trigger;
    }
    else {
      if (error != nullptr) {
        *error = "AssetGet: unknown mode '" + mode_name + "'";
      }
      return false;
    }
  }
  return true;
}

bool KX_Node_AssetGet::HasAssetBinding() const
{
  return true;
}

bool KX_Node_AssetGet::ResolveAssetBinding(KX_AssetRegistry &registry, std::string *error)
{
  KX_AssetHandle handle = KX_INVALID_ASSET_HANDLE;
  uint32_t generation = 0;
  uint32_t field_index = 0;
  KX_AssetFieldType field_type = KX_AssetFieldType::Float;
  KX_ValueType field_value_type = KX_VALUE_FLOAT;

  if (!LookupAssetField(registry,
                        "AssetGet",
                        asset_ref_,
                        expected_asset_type_,
                        field_name_,
                        &handle,
                        &generation,
                        &field_index,
                        &field_type,
                        &field_value_type,
                        error))
  {
    return false;
  }

  /* Acquire the new reference before dropping the old one so a repeated
   * resolution of the same asset never dips the count below the registry's. */
  registry.Acquire(handle);
  if (handle_ != KX_INVALID_ASSET_HANDLE) {
    registry.Release(handle_);
  }

  handle_ = handle;
  registry_generation_ = generation;
  field_index_ = field_index;
  field_type_ = field_type;
  field_value_type_ = field_value_type;
  return true;
}

KX_ValueType KX_Node_AssetGet::ResolvedPortType(bool input, uint32_t port) const
{
  if (!input && port == OUT_VALUE) {
    return field_value_type_;
  }
  return KX_VALUE_NONE;
}

bool KX_Node_AssetGet::GetResolvedAssetBinding(KX_ResolvedAssetBinding *binding) const
{
  if (binding == nullptr || handle_ == KX_INVALID_ASSET_HANDLE) {
    return false;
  }

  const KX_GameDataAsset *asset = KX_AssetRegistry::Get().Get(handle_);
  binding->asset_id = asset_ref_;
  binding->asset_type = asset != nullptr ? asset->asset_type : expected_asset_type_;
  binding->handle = handle_;
  binding->generation = registry_generation_;
  binding->field_name = field_name_;
  return true;
}

bool KX_Node_AssetGet::IsValueNode() const
{
  return mode_ == Mode::Poll;
}

KX_Node_AssetGet::Mode KX_Node_AssetGet::GetMode() const
{
  return mode_;
}

KX_AssetHandle KX_Node_AssetGet::Handle() const
{
  return handle_;
}

KX_ExecResult KX_Node_AssetGet::Execute(KX_ExecutionContext &ctx)
{
  if (mode_ == Mode::Trigger && !ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  /* A missing asset yields the typed zero value so the graph keeps running:
   * the handle was validated at compile time, so this only happens when a
   * scene unloads the registry behind a live plan. */
  const KX_GameDataAsset *asset = ctx.ResolveAsset(handle_);
  OutputValue(ctx, OUT_VALUE) = asset != nullptr ? asset->ReadField(field_index_)
                                                 : KX_MakeZeroValue(field_value_type_);

  if (mode_ == Mode::Trigger) {
    SetOutputTrigger(ctx, OUT_READ, true);
  }
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Asset Set                                                                  */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_AssetSet::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(DynamicValuePort("Value"));
  signature.outputs.push_back(TriggerPort("OnWritten"));
  return signature;
}

KX_Node_AssetSet::~KX_Node_AssetSet()
{
  if (handle_ != KX_INVALID_ASSET_HANDLE) {
    KX_AssetRegistry::Get().Release(handle_);
  }
}

bool KX_Node_AssetSet::Configure(const std::string &config, std::string *error)
{
  asset_ref_.clear();
  field_name_.clear();
  expected_asset_type_.clear();

  ParseAssetConfig(config, &asset_ref_, &field_name_, &expected_asset_type_);
  if (asset_ref_.empty()) {
    if (error != nullptr) {
      *error = "AssetSet: missing 'asset_ref'";
    }
    return false;
  }
  if (field_name_.empty()) {
    if (error != nullptr) {
      *error = "AssetSet: missing 'field'";
    }
    return false;
  }
  return true;
}

bool KX_Node_AssetSet::HasAssetBinding() const
{
  return true;
}

bool KX_Node_AssetSet::ResolveAssetBinding(KX_AssetRegistry &registry, std::string *error)
{
  KX_AssetHandle handle = KX_INVALID_ASSET_HANDLE;
  uint32_t generation = 0;
  uint32_t field_index = 0;
  KX_AssetFieldType field_type = KX_AssetFieldType::Float;
  KX_ValueType field_value_type = KX_VALUE_FLOAT;

  if (!LookupAssetField(registry,
                        "AssetSet",
                        asset_ref_,
                        expected_asset_type_,
                        field_name_,
                        &handle,
                        &generation,
                        &field_index,
                        &field_type,
                        &field_value_type,
                        error))
  {
    return false;
  }

  registry.Acquire(handle);
  if (handle_ != KX_INVALID_ASSET_HANDLE) {
    registry.Release(handle_);
  }

  handle_ = handle;
  registry_generation_ = generation;
  field_index_ = field_index;
  field_type_ = field_type;
  field_value_type_ = field_value_type;
  return true;
}

KX_ValueType KX_Node_AssetSet::ResolvedPortType(bool input, uint32_t port) const
{
  if (input && port == IN_VALUE) {
    return field_value_type_;
  }
  return KX_VALUE_NONE;
}

bool KX_Node_AssetSet::GetResolvedAssetBinding(KX_ResolvedAssetBinding *binding) const
{
  if (binding == nullptr || handle_ == KX_INVALID_ASSET_HANDLE) {
    return false;
  }

  const KX_GameDataAsset *asset = KX_AssetRegistry::Get().Get(handle_);
  binding->asset_id = asset_ref_;
  binding->asset_type = asset != nullptr ? asset->asset_type : expected_asset_type_;
  binding->handle = handle_;
  binding->generation = registry_generation_;
  binding->field_name = field_name_;
  return true;
}

KX_AssetHandle KX_Node_AssetSet::Handle() const
{
  return handle_;
}

KX_ExecResult KX_Node_AssetSet::Execute(KX_ExecutionContext &ctx)
{
  if (!ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  KX_GameDataAsset *asset = ctx.ResolveAsset(handle_);
  if (asset == nullptr) {
    /* Nothing to write to: do not raise OnWritten, so a runtime that lost the
     * asset behind a valid plan is visible as a stalled branch. */
    return KX_ExecResult::Continue;
  }

  asset->WriteField(field_index_, ReadInput(ctx, IN_VALUE));
  SetOutputTrigger(ctx, OUT_WRITTEN, true);
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Object Get                                                                 */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_ObjectGet::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(ValuePort("Object", KX_VALUE_OBJECT));
  signature.outputs.push_back(DynamicValuePort("Value"));
  signature.outputs.push_back(TriggerPort("OnRead"));
  signature.outputs.push_back(TriggerPort("OnMissing"));
  signature.state_size = sizeof(KX_PropertyCacheEntry);
  signature.state_alignment = alignof(KX_PropertyCacheEntry);
  return signature;
}

bool KX_Node_ObjectGet::Configure(const std::string &config, std::string *error)
{
  mode_ = Mode::Poll;

  if (!ParseObjectConfig(config, "ObjectGet", &property_name_, &value_type_, error)) {
    return false;
  }

  std::string mode_name;
  if (KX_IRConfigGetString(config, "mode", &mode_name) && !mode_name.empty()) {
    if (MatchOp(mode_name, "poll")) {
      mode_ = Mode::Poll;
    }
    else if (MatchOp(mode_name, "trigger")) {
      mode_ = Mode::Trigger;
    }
    else {
      if (error != nullptr) {
        *error = "ObjectGet: unknown mode '" + mode_name + "'";
      }
      return false;
    }
  }
  return true;
}

KX_ValueType KX_Node_ObjectGet::ResolvedPortType(bool input, uint32_t port) const
{
  if (!input && port == OUT_VALUE) {
    return value_type_;
  }
  return KX_VALUE_NONE;
}

bool KX_Node_ObjectGet::IsValueNode() const
{
  return mode_ == Mode::Poll;
}

KX_Node_ObjectGet::Mode KX_Node_ObjectGet::GetMode() const
{
  return mode_;
}

const std::string &KX_Node_ObjectGet::PropertyName() const
{
  return property_name_;
}

KX_ExecResult KX_Node_ObjectGet::Execute(KX_ExecutionContext &ctx)
{
  if (mode_ == Mode::Trigger && !ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  void *object = ReadInputPointer(ctx, IN_OBJECT);
  KX_PropertyAccessor accessor(ctx.PropertyBackend());

  /* A missing object or property yields the declared zero value, so a graph
   * keeps producing well typed data instead of stalling. */
  KX_RuntimeValue value = KX_MakeZeroValue(value_type_);
  const bool found = accessor.Read(object, property_name_, State<KX_PropertyCacheEntry>(ctx),
                                   value_type_, &value);
  OutputValue(ctx, OUT_VALUE) = value;

  /* Poll mode runs in the value node list which never propagates triggers, so
   * only the trigger mode raises the optional output triggers. */
  if (mode_ == Mode::Trigger) {
    SetOutputTrigger(ctx, found ? OUT_READ : OUT_MISSING, true);
  }
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Object Set                                                                 */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_ObjectSet::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(ValuePort("Object", KX_VALUE_OBJECT));
  signature.inputs.push_back(DynamicValuePort("Value"));
  signature.outputs.push_back(TriggerPort("OnWritten"));
  signature.state_size = sizeof(KX_PropertyCacheEntry);
  signature.state_alignment = alignof(KX_PropertyCacheEntry);
  return signature;
}

bool KX_Node_ObjectSet::Configure(const std::string &config, std::string *error)
{
  create_missing_ = true;
  creation_warned_ = false;

  if (!ParseObjectConfig(config, "ObjectSet", &property_name_, &value_type_, error)) {
    return false;
  }

  std::string missing_name;
  if (KX_IRConfigGetString(config, "on_missing", &missing_name) && !missing_name.empty()) {
    if (MatchOp(missing_name, "create")) {
      create_missing_ = true;
    }
    else if (MatchOp(missing_name, "error")) {
      create_missing_ = false;
    }
    else {
      if (error != nullptr) {
        *error = "ObjectSet: unknown on_missing '" + missing_name + "'";
      }
      return false;
    }
  }
  return true;
}

KX_ValueType KX_Node_ObjectSet::ResolvedPortType(bool input, uint32_t port) const
{
  if (input && port == IN_VALUE) {
    return value_type_;
  }
  return KX_VALUE_NONE;
}

const std::string &KX_Node_ObjectSet::PropertyName() const
{
  return property_name_;
}

bool KX_Node_ObjectSet::CreatesMissingProperty() const
{
  return create_missing_;
}

KX_ExecResult KX_Node_ObjectSet::Execute(KX_ExecutionContext &ctx)
{
  if (!ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  void *object = ReadInputPointer(ctx, IN_OBJECT);
  if (object == nullptr) {
    return KX_ExecResult::Continue;
  }

  KX_PropertyAccessor accessor(ctx.PropertyBackend());
  const KX_PropertyAccessor::WriteResult result =
      accessor.Write(object,
                     property_name_,
                     ReadInput(ctx, IN_VALUE),
                     create_missing_,
                     State<KX_PropertyCacheEntry>(ctx));

  if (result == KX_PropertyAccessor::WriteResult::Missing ||
      result == KX_PropertyAccessor::WriteResult::NoBackend)
  {
    /* "on_missing": "error" skips the write and does not raise OnWritten, which
     * makes the broken connection visible as a stalled branch. */
    return KX_ExecResult::Continue;
  }

  if (result == KX_PropertyAccessor::WriteResult::Created && !creation_warned_) {
    creation_warned_ = true;
    std::fprintf(stderr,
                 "GameNode ObjectSet: created missing property '%s' (check the spelling)\n",
                 property_name_.c_str());
  }

  SetOutputTrigger(ctx, OUT_WRITTEN, true);
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Graph entry nodes                                                          */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_OnInit::Signature()
{
  KX_NodeSignature signature;
  signature.outputs.push_back(TriggerPort("OnOut"));
  return signature;
}

KX_ExecResult KX_Node_OnInit::Execute(KX_ExecutionContext &ctx)
{
  /* A graph instance is compiled from scratch on load and on hot reload, so a
   * plain member flag makes OnInit single shot without depending on the frame
   * counter (which survives a Rebuild). */
  SetOutputTrigger(ctx, OUT_TRIGGER, !fired_);
  fired_ = true;
  return KX_ExecResult::Continue;
}

KX_NodeSignature KX_Node_OnUpdate::Signature()
{
  KX_NodeSignature signature;
  signature.outputs.push_back(TriggerPort("OnOut"));
  return signature;
}

KX_ExecResult KX_Node_OnUpdate::Execute(KX_ExecutionContext &ctx)
{
  SetOutputTrigger(ctx, OUT_TRIGGER, true);
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Constant value nodes                                                       */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_ConstFloat::Signature()
{
  KX_NodeSignature signature;
  signature.outputs.push_back(ValuePort("Value", KX_VALUE_FLOAT));
  return signature;
}

bool KX_Node_ConstFloat::Configure(const std::string &config, std::string *error)
{
  (void)error;
  std::string value_text;
  if (KX_IRConfigGetString(config, "value", &value_text) && !value_text.empty()) {
    value_ = static_cast<float>(std::atof(value_text.c_str()));
  }
  return true;
}

KX_ExecResult KX_Node_ConstFloat::Execute(KX_ExecutionContext &ctx)
{
  OutputValue(ctx, OUT_VALUE) = KX_RuntimeValue::Float(value_);
  return KX_ExecResult::Continue;
}

KX_NodeSignature KX_Node_ConstBool::Signature()
{
  KX_NodeSignature signature;
  signature.outputs.push_back(ValuePort("Value", KX_VALUE_BOOL));
  return signature;
}

bool KX_Node_ConstBool::Configure(const std::string &config, std::string *error)
{
  (void)error;
  std::string value_text;
  if (KX_IRConfigGetString(config, "value", &value_text) && !value_text.empty()) {
    value_ = value_text == "true" || value_text == "1";
  }
  return true;
}

KX_ExecResult KX_Node_ConstBool::Execute(KX_ExecutionContext &ctx)
{
  OutputValue(ctx, OUT_VALUE) = KX_RuntimeValue::Bool(value_);
  return KX_ExecResult::Continue;
}

/* ------------------------------------------------------------------------- */
/* Sink                                                                       */
/* ------------------------------------------------------------------------- */

KX_NodeSignature KX_Node_Sink::Signature()
{
  KX_NodeSignature signature;
  signature.inputs.push_back(TriggerPort("OnEnter"));
  signature.inputs.push_back(ValuePort("Value", KX_VALUE_FLOAT));
  return signature;
}

KX_ExecResult KX_Node_Sink::Execute(KX_ExecutionContext &ctx)
{
  if (!ConsumeInputTrigger(ctx, IN_TRIGGER)) {
    return KX_ExecResult::Continue;
  }

  last_value_ = ReadInput(ctx, IN_VALUE);
  ++count_;
  return KX_ExecResult::Continue;
}

uint32_t KX_Node_Sink::Count() const
{
  return count_;
}

KX_RuntimeValue KX_Node_Sink::LastValue() const
{
  return last_value_;
}
