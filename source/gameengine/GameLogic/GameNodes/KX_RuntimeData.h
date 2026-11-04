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

/** \file KX_RuntimeData.h
 *  \ingroup gamelogic
 *
 *  Flat memory block shared by every node of one compiled game node graph.
 *
 *  Ports, trigger flags and node state are allocated once at compile time.
 *  Nodes only keep the resulting byte offsets, so reading a port at runtime is
 *  one pointer addition instead of a dictionary lookup.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "KX_RuntimeValue.h"

/** Alignment used for every runtime data allocation. */
static constexpr size_t KX_RUNTIME_DATA_ALIGNMENT = 16;
/** Offset value that marks "no slot". */
static constexpr uint32_t KX_INVALID_OFFSET = 0xFFFFFFFFu;

class KX_RuntimeData {
 public:
  KX_RuntimeData() = default;
  explicit KX_RuntimeData(size_t capacity_bytes);

  /** Reserve at least #capacity_bytes of storage. Never shrinks the buffer. */
  void Reserve(size_t capacity_bytes);
  /**
   * Adopt a layout that was computed by a previous compile pass.
   *
   * Offsets in #KX_ExecutionPlan are absolute, so at runtime the buffer only
   * needs to be sized and zeroed. No allocator call is required.
   */
  void SetLayoutSize(size_t size_bytes);
  /** Release all storage, including the compiled layout. */
  void Clear();
  /** Zero every allocated byte while keeping the compiled layout intact. */
  void ResetContent();

  /**
   * Allocate a zeroed raw block and return its byte offset.
   *
   * The whole layout is allocated during compilation. References returned by
   * #Value() or #State() are invalidated by any later allocation.
   */
  uint32_t Allocate(size_t size, size_t alignment = KX_RUNTIME_DATA_ALIGNMENT);
  /** Allocate one #KX_RuntimeValue slot. */
  uint32_t AllocateValuePort();
  /** Allocate a trigger flag slot. */
  uint32_t AllocateTriggerPort();
  /** Allocate private, zeroed node state (delay counters and similar). */
  uint32_t AllocateState(size_t size, size_t alignment = KX_RUNTIME_DATA_ALIGNMENT);

  uint8_t *Data();
  const uint8_t *Data() const;

  /** Number of bytes currently handed out by the allocator. */
  size_t UsedSize() const;
  /** Number of bytes owned by the buffer. */
  size_t Capacity() const;

  KX_RuntimeValue &Value(uint32_t offset);
  const KX_RuntimeValue &Value(uint32_t offset) const;

  bool Trigger(uint32_t offset) const;
  void SetTrigger(uint32_t offset, bool value);
  /** Clear every trigger flag, called at the beginning of a frame. */
  void ClearTriggers();

  template<typename T> T *State(uint32_t offset)
  {
    return reinterpret_cast<T *>(Data() + offset);
  }

  template<typename T> const T *State(uint32_t offset) const
  {
    return reinterpret_cast<const T *>(Data() + offset);
  }

 private:
  void EnsureCapacity(size_t required_bytes);

  std::vector<uint8_t> buffer_;
  size_t used_ = 0;
  std::vector<uint32_t> trigger_offsets_;
};
