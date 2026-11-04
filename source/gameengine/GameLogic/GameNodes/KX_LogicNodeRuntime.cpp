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

/** \file KX_LogicNodeRuntime.cpp
 *  \ingroup gamelogic
 */

#include "KX_LogicNodeRuntime.h"

#include <cassert>
#include <cstring>

void KX_LogicNodeRuntime::ConfigureInputs(uint32_t count, const uint32_t *offsets)
{
  assert(count <= KX_MAX_NODE_INPUTS);
  input_count_ = count;
  if (count != 0) {
    std::memcpy(input_offsets_, offsets, sizeof(uint32_t) * count);
  }
}

void KX_LogicNodeRuntime::ConfigureOutputs(uint32_t count, const uint32_t *offsets)
{
  assert(count <= KX_MAX_NODE_OUTPUTS);
  output_count_ = count;
  if (count != 0) {
    std::memcpy(output_offsets_, offsets, sizeof(uint32_t) * count);
  }
}

void KX_LogicNodeRuntime::ConfigureInputConversions(uint32_t count, const KX_ValueType *targets)
{
  assert(count <= KX_MAX_NODE_INPUTS);
  for (uint32_t i = 0; i < KX_MAX_NODE_INPUTS; ++i) {
    input_conversions_[i] = KX_VALUE_NONE;
  }
  if (targets != nullptr) {
    std::memcpy(input_conversions_, targets, sizeof(KX_ValueType) * count);
  }
}

void KX_LogicNodeRuntime::SetStateOffset(uint32_t offset)
{
  state_offset_ = offset;
}

uint32_t KX_LogicNodeRuntime::InputCount() const
{
  return input_count_;
}

uint32_t KX_LogicNodeRuntime::OutputCount() const
{
  return output_count_;
}

uint32_t KX_LogicNodeRuntime::InputOffset(uint32_t index) const
{
  assert(index < input_count_);
  return input_offsets_[index];
}

uint32_t KX_LogicNodeRuntime::OutputOffset(uint32_t index) const
{
  assert(index < output_count_);
  return output_offsets_[index];
}

uint32_t KX_LogicNodeRuntime::StateOffset() const
{
  return state_offset_;
}

KX_NodeTypeId KX_LogicNodeRuntime::TypeId() const
{
  return type_id_;
}

void KX_LogicNodeRuntime::SetTypeId(KX_NodeTypeId type_id)
{
  type_id_ = type_id;
}

KX_RuntimeValue &KX_LogicNodeRuntime::InputValue(KX_ExecutionContext &ctx,
                                                 uint32_t index) const
{
  assert(index < input_count_);
  return ctx.Value(input_offsets_[index]);
}

KX_RuntimeValue &KX_LogicNodeRuntime::OutputValue(KX_ExecutionContext &ctx,
                                                  uint32_t index) const
{
  assert(index < output_count_);
  return ctx.Value(output_offsets_[index]);
}

bool KX_LogicNodeRuntime::InputTrigger(KX_ExecutionContext &ctx, uint32_t index) const
{
  assert(index < input_count_);
  return ctx.IsTriggered(input_offsets_[index]);
}

void KX_LogicNodeRuntime::SetOutputTrigger(KX_ExecutionContext &ctx,
                                           uint32_t index,
                                           bool value) const
{
  assert(index < output_count_);
  ctx.SetTriggered(output_offsets_[index], value);
}

KX_RuntimeValue KX_LogicNodeRuntime::ReadInput(KX_ExecutionContext &ctx,
                                               uint32_t index) const
{
  assert(index < input_count_);
  const KX_RuntimeValue &raw = ctx.Value(input_offsets_[index]);
  const KX_ValueType target = input_conversions_[index];
  if (target == KX_VALUE_NONE || raw.type == target) {
    return raw;
  }
  return raw.ConvertTo(target);
}

float KX_LogicNodeRuntime::ReadInputFloat(KX_ExecutionContext &ctx, uint32_t index) const
{
  return ReadInput(ctx, index).AsFloat();
}

int KX_LogicNodeRuntime::ReadInputInt(KX_ExecutionContext &ctx, uint32_t index) const
{
  return ReadInput(ctx, index).AsInt();
}

bool KX_LogicNodeRuntime::ReadInputBool(KX_ExecutionContext &ctx, uint32_t index) const
{
  return ReadInput(ctx, index).AsBool();
}

void KX_LogicNodeRuntime::ReadInputVector(KX_ExecutionContext &ctx,
                                          uint32_t index,
                                          float out[3]) const
{
  ReadInput(ctx, index).VectorTo(out);
}

void *KX_LogicNodeRuntime::ReadInputPointer(KX_ExecutionContext &ctx, uint32_t index) const
{
  return ReadInput(ctx, index).AsPointer();
}

void KX_LogicNodeRuntime::EmitPendingTrigger(KX_ExecutionContext &ctx,
                                             uint32_t output_index)
{
  if (output_index >= output_count_) {
    return;
  }
  ctx.EnqueuePendingOutput(output_offsets_[output_index]);
}

bool KX_LogicNodeRuntime::ConsumeInputTrigger(KX_ExecutionContext &ctx,
                                              uint32_t index) const
{
  assert(index < input_count_);
  return ctx.ConsumeTrigger(input_offsets_[index]);
}
