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

/** \file KX_RuntimeData.cpp
 *  \ingroup gamelogic
 */

#include "KX_RuntimeData.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace {

inline size_t align_up(size_t value, size_t alignment)
{
  assert(alignment != 0);
  return (value + alignment - 1) / alignment * alignment;
}

}  // namespace

KX_RuntimeData::KX_RuntimeData(size_t capacity_bytes)
{
  Reserve(capacity_bytes);
}

void KX_RuntimeData::Reserve(size_t capacity_bytes)
{
  if (capacity_bytes > buffer_.size()) {
    buffer_.resize(capacity_bytes, 0);
  }
}

void KX_RuntimeData::SetLayoutSize(size_t size_bytes)
{
  EnsureCapacity(size_bytes);
  if (size_bytes < used_) {
    std::memset(buffer_.data() + size_bytes, 0, used_ - size_bytes);
  }
  used_ = size_bytes;
}

void KX_RuntimeData::Clear()
{
  buffer_.clear();
  buffer_.shrink_to_fit();
  used_ = 0;
  trigger_offsets_.clear();
}

void KX_RuntimeData::ResetContent()
{
  if (used_ != 0) {
    std::memset(buffer_.data(), 0, used_);
  }
}

void KX_RuntimeData::EnsureCapacity(size_t required_bytes)
{
  if (required_bytes <= buffer_.size()) {
    return;
  }
  size_t new_capacity = std::max(required_bytes, buffer_.size() * 2);
  buffer_.resize(new_capacity, 0);
}

uint32_t KX_RuntimeData::Allocate(size_t size, size_t alignment)
{
  assert(alignment != 0);
  const size_t offset = align_up(used_, alignment);
  const size_t required = offset + size;
  EnsureCapacity(required);
  used_ = required;

  if (size != 0) {
    std::memset(buffer_.data() + offset, 0, size);
  }

  assert(offset < KX_INVALID_OFFSET);
  return static_cast<uint32_t>(offset);
}

uint32_t KX_RuntimeData::AllocateValuePort()
{
  return Allocate(sizeof(KX_RuntimeValue), alignof(KX_RuntimeValue));
}

uint32_t KX_RuntimeData::AllocateTriggerPort()
{
  const uint32_t offset = Allocate(sizeof(bool), alignof(bool));
  trigger_offsets_.push_back(offset);
  return offset;
}

uint32_t KX_RuntimeData::AllocateState(size_t size, size_t alignment)
{
  return Allocate(size, alignment);
}

uint8_t *KX_RuntimeData::Data()
{
  return buffer_.data();
}

const uint8_t *KX_RuntimeData::Data() const
{
  return buffer_.data();
}

size_t KX_RuntimeData::UsedSize() const
{
  return used_;
}

size_t KX_RuntimeData::Capacity() const
{
  return buffer_.size();
}

KX_RuntimeValue &KX_RuntimeData::Value(uint32_t offset)
{
  assert(offset != KX_INVALID_OFFSET);
  assert(size_t(offset) + sizeof(KX_RuntimeValue) <= used_);
  return *reinterpret_cast<KX_RuntimeValue *>(buffer_.data() + offset);
}

const KX_RuntimeValue &KX_RuntimeData::Value(uint32_t offset) const
{
  assert(offset != KX_INVALID_OFFSET);
  assert(size_t(offset) + sizeof(KX_RuntimeValue) <= used_);
  return *reinterpret_cast<const KX_RuntimeValue *>(buffer_.data() + offset);
}

bool KX_RuntimeData::Trigger(uint32_t offset) const
{
  assert(offset != KX_INVALID_OFFSET);
  assert(size_t(offset) + sizeof(bool) <= used_);
  return *reinterpret_cast<const bool *>(buffer_.data() + offset);
}

void KX_RuntimeData::SetTrigger(uint32_t offset, bool value)
{
  assert(offset != KX_INVALID_OFFSET);
  assert(size_t(offset) + sizeof(bool) <= used_);
  *reinterpret_cast<bool *>(buffer_.data() + offset) = value;
}

void KX_RuntimeData::ClearTriggers()
{
  for (const uint32_t offset : trigger_offsets_) {
    *reinterpret_cast<bool *>(buffer_.data() + offset) = false;
  }
}
