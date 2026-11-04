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

/** \file KX_LogicNodeRuntime.h
 *  \ingroup gamelogic
 *
 *  Base class of every compiled game node.
 *
 *  A runtime node only knows byte offsets into the graph #KX_RuntimeData:
 *  value inputs, value outputs, trigger inputs, trigger outputs and an optional
 *  private state block. The editor node definitions live in a separate layer
 *  and are matched to these classes by the #KX_NodeRegistry.
 *
 *  Value inputs are wired directly to the output slot that feeds them, so
 *  reading an input is one pointer addition. When the compiler detected an
 *  implicit conversion it stores a target type per input port; #ReadInput
 *  applies that conversion on the fly without allocating extra storage.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_GraphIR.h"
#include "KX_RuntimeData.h"
#include "KX_RuntimeValue.h"

class KX_AssetRegistry;
struct KX_ResolvedAssetBinding;

/** Numeric id of a registered game node type. Zero is never valid. */
using KX_NodeTypeId = uint32_t;
static constexpr KX_NodeTypeId KX_INVALID_NODE_TYPE_ID = 0;

/** Upper bound of value/trigger ports a single runtime node can address. */
/* B4.1 CreateUIButton declares 17 inputs (OnEnter + 16 properties), so the
 * per node table is sized at 24 to leave headroom for the widget families. */
static constexpr uint32_t KX_MAX_NODE_INPUTS = 24;
static constexpr uint32_t KX_MAX_NODE_OUTPUTS = 16;

/** Result of one execution step. P1 only uses Continue. */
enum class KX_ExecResult : uint8_t {
  Continue = 0,
  Stop = 1,
};

class KX_LogicNodeRuntime {
 public:
  KX_LogicNodeRuntime() = default;
  virtual ~KX_LogicNodeRuntime() = default;

  /** Assign the offset table resolved by the graph compiler. */
  void ConfigureInputs(uint32_t count, const uint32_t *offsets);
  void ConfigureOutputs(uint32_t count, const uint32_t *offsets);
  /**
   * Target type of every value input, KX_VALUE_NONE when no conversion is
   * needed. The compiler only emits markers for conversions it validated.
   */
  void ConfigureInputConversions(uint32_t count, const KX_ValueType *targets);
  void SetStateOffset(uint32_t offset);

  uint32_t InputCount() const;
  uint32_t OutputCount() const;
  uint32_t InputOffset(uint32_t index) const;
  uint32_t OutputOffset(uint32_t index) const;
  uint32_t StateOffset() const;

  KX_NodeTypeId TypeId() const;
  /** Called by #KX_NodeRegistry when the instance is created. */
  void SetTypeId(KX_NodeTypeId type_id);

  /**
   * Run one execution step. Values are read and written through the context. */
  virtual KX_ExecResult Execute(KX_ExecutionContext &ctx) = 0;

  /**
   * Bind the IR port names to their indices.
   *
   * Called by the compiler after #Configure and before any execution. Table
   * driven nodes use it to resolve ports by identifier instead of index.
   */
  virtual void ConfigurePorts(const std::vector<IRPort> &inputs, const std::vector<IRPort> &outputs)
  {
    (void)inputs;
    (void)outputs;
  }

  /**
   * Ask the scheduler to forward a trigger output later in this frame.
   *
   * Used by stateful nodes that must not raise their output during a Tick call
   * (Delay, Timer). The default implementation raises the trigger output slot
   * of #output_index; the scheduler picks it up after the Tick returns.
   */
  virtual void EmitPendingTrigger(KX_ExecutionContext &ctx, uint32_t output_index);

  /**
   * Drive private state (Delay timers and future Timer nodes).
   *
   * Called before trigger propagation so a node can emit its output trigger
   * within the same frame. The scheduler clears output trigger flags before
   * calling this and enqueues whatever the node raised afterwards.
   */
  virtual void Tick(KX_ExecutionContext &ctx, float delta_time)
  {
    (void)ctx;
    (void)delta_time;
  }

  /**
   * Number of per-element triggers the node wants to emit after #Execute.
   *
   * ForEach uses this to run its downstream subtree once per list element
   * inside one frame, matching the legacy uplogic LoopNode which evaluates a
   * LoopList synchronously. A node returning a non zero count must not raise
   * any output trigger from #Execute, the scheduler drives them instead:
   * #PreparePendingTrigger writes the element ports and raises the outputs,
   * #FinishPendingTriggers raises the completion output.
   */
  virtual uint32_t CollectPendingTriggers(KX_ExecutionContext &ctx)
  {
    (void)ctx;
    return 0;
  }

  /** Write the output ports for pending element @a index and raise its trigger. */
  virtual void PreparePendingTrigger(KX_ExecutionContext &ctx, uint32_t index)
  {
    (void)ctx;
    (void)index;
  }

  /** Called once after the last pending element of the frame. */
  virtual void FinishPendingTriggers(KX_ExecutionContext &ctx)
  {
    (void)ctx;
  }

  /**
   * Parse the node private config string emitted by the editor.
   *
   * Returns false and fills #error when the configuration is invalid. The
   * compiler turns that into a node level diagnostic.
   */
  virtual bool Configure(const std::string &config, std::string *error)
  {
    (void)config;
    (void)error;
    return true;
  }

  /**
   * True when the node references an asset by string id.
   *
   * The compiler runs a ResolveAssets pass between Resolve and Analyze and
   * calls #ResolveAssetBinding for every such node.
   */
  virtual bool HasAssetBinding() const
  {
    return false;
  }

  /**
   * Resolve the configured asset reference to a handle and fix the field offset.
   *
   * Implementations acquire one registry reference and release it in their
   * destructor. Returns false and fills #error for missing assets, missing
   * fields or asset type mismatches.
   */
  virtual bool ResolveAssetBinding(KX_AssetRegistry &registry, std::string *error)
  {
    (void)registry;
    (void)error;
    return true;
  }

  /** Resolved type of a port declared with `dynamic_type`, KX_VALUE_NONE otherwise. */
  virtual KX_ValueType ResolvedPortType(bool input, uint32_t port) const
  {
    (void)input;
    (void)port;
    return KX_VALUE_NONE;
  }

  /** Fill the compiler side debug record after a successful asset resolution. */
  virtual bool GetResolvedAssetBinding(KX_ResolvedAssetBinding *binding) const
  {
    (void)binding;
    return false;
  }

  /**
   * True for poll mode nodes refreshed once per frame before trigger propagation.
   *
   * The compiler puts these nodes in the value node list and does not turn
   * their unconnected trigger inputs into per frame entry points.
   */
  virtual bool IsValueNode() const
  {
    return false;
  }

  /** Read a value input, applying the compiler supplied conversion marker. */
  KX_RuntimeValue ReadInput(KX_ExecutionContext &ctx, uint32_t index) const;
  float ReadInputFloat(KX_ExecutionContext &ctx, uint32_t index) const;
  int ReadInputInt(KX_ExecutionContext &ctx, uint32_t index) const;
  bool ReadInputBool(KX_ExecutionContext &ctx, uint32_t index) const;
  void ReadInputVector(KX_ExecutionContext &ctx, uint32_t index, float out[3]) const;
  void *ReadInputPointer(KX_ExecutionContext &ctx, uint32_t index) const;

  /** Read and clear a trigger input slot. */
  bool ConsumeInputTrigger(KX_ExecutionContext &ctx, uint32_t index) const;

 protected:
  /** Raw value slot connected to value input/output #index. */
  KX_RuntimeValue &InputValue(KX_ExecutionContext &ctx, uint32_t index) const;
  KX_RuntimeValue &OutputValue(KX_ExecutionContext &ctx, uint32_t index) const;

  /** Trigger flag connected to trigger input/output #index. */
  bool InputTrigger(KX_ExecutionContext &ctx, uint32_t index) const;
  void SetOutputTrigger(KX_ExecutionContext &ctx, uint32_t index, bool value) const;

  template<typename T> T *State(KX_ExecutionContext &ctx) const
  {
    return ctx.Data().State<T>(state_offset_);
  }

 private:
  uint32_t input_offsets_[KX_MAX_NODE_INPUTS] = {};
  uint32_t output_offsets_[KX_MAX_NODE_OUTPUTS] = {};
  KX_ValueType input_conversions_[KX_MAX_NODE_INPUTS] = {};
  uint32_t input_count_ = 0;
  uint32_t output_count_ = 0;
  uint32_t state_offset_ = KX_INVALID_OFFSET;
  KX_NodeTypeId type_id_ = KX_INVALID_NODE_TYPE_ID;
};
