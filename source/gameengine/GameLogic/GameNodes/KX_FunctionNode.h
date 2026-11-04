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

/** \file KX_FunctionNode.h
 *  \ingroup gamelogic
 *
 *  Table driven runtime nodes.
 *
 *  Most nodes only differ by their port list and a small piece of logic. Instead
 *  of writing one C++ class per editor node, the node library declares a
 *  #KX_FunctionSpec (ports + callbacks) and the registry instantiates
 *  #KX_FunctionNode. This keeps the batch port of the editor catalog compact
 *  while preserving the same compiled contract as the hand written nodes.
 */

#pragma once

#include <cstdint>
#include <string>

#include "KX_LogicNodeRuntime.h"
#include "KX_RuntimeData.h"
#include "KX_RuntimeValue.h"

struct KX_ExecutionContext;
class KX_FunctionNode;
class KX_GameObject;

/** One declared port of a table driven node. */
struct KX_FuncPort {
  const char *name;
  /** KX_VALUE_NONE for trigger ports. */
  KX_ValueType type;
  bool trigger;
};

using KX_FuncExecute = void (*)(KX_FunctionNode &node, KX_ExecutionContext &ctx);
using KX_FuncConfigure = bool (*)(KX_FunctionNode &node,
                                  const std::string &config,
                                  std::string *error);
using KX_FuncTick = void (*)(KX_FunctionNode &node,
                             KX_ExecutionContext &ctx,
                             float delta_time);
using KX_FuncPendingCount = uint32_t (*)(KX_FunctionNode &node, KX_ExecutionContext &ctx);
using KX_FuncPendingElement = void (*)(KX_FunctionNode &node,
                                       KX_ExecutionContext &ctx,
                                       uint32_t index);
using KX_FuncPendingFinish = void (*)(KX_FunctionNode &node, KX_ExecutionContext &ctx);

/** Static description of a table driven node type. */
struct KX_FunctionSpec {
  /** Runtime id, also used as the IR `type_id`. */
  const char *type_id;
  const KX_FuncPort *inputs = nullptr;
  uint32_t input_count = 0;
  const KX_FuncPort *outputs = nullptr;
  uint32_t output_count = 0;
  KX_FuncExecute execute = nullptr;
  KX_FuncConfigure configure = nullptr;
  KX_FuncTick tick = nullptr;
  uint32_t state_size = 0;
  uint32_t state_alignment = KX_RUNTIME_DATA_ALIGNMENT;
  bool is_value_node = false;
  /** Stateful nodes advertise that they may emit triggers from #Tick. */
  bool needs_tick = false;
  bool breaks_trigger_cycles = false;
  /**
   * Optional synchronous per-element sequence hooks. ForEach uses them to run
   * the downstream subtree once per list element inside one frame; the generic
   * scheduler calls the hooks through KX_LogicNodeRuntime.
   */
  KX_FuncPendingCount pending_count = nullptr;
  KX_FuncPendingElement pending_element = nullptr;
  KX_FuncPendingFinish pending_finish = nullptr;
  /**
   * Polymorphic nodes take their port value types from the IR instead of the
   * table above: the editor resolves the actual type from the connected
   * socket (or the property definition), so a single runtime node can carry
   * Float, Int, Bool, String or handle values without narrowing to Float.
   */
  bool ports_from_ir = false;
};

class KX_FunctionNode final : public KX_LogicNodeRuntime {
 public:
  explicit KX_FunctionNode(const KX_FunctionSpec *spec) : spec_(spec) {}

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  void Tick(KX_ExecutionContext &ctx, float delta_time) override;
  uint32_t CollectPendingTriggers(KX_ExecutionContext &ctx) override;
  void PreparePendingTrigger(KX_ExecutionContext &ctx, uint32_t index) override;
  void FinishPendingTriggers(KX_ExecutionContext &ctx) override;
  bool Configure(const std::string &config, std::string *error) override;
  bool IsValueNode() const override;

  const KX_FunctionSpec &Spec() const
  {
    return *spec_;
  }
  const std::string &Config() const
  {
    return config_;
  }

  /* Port accessors used by the table callbacks. */
  KX_RuntimeValue In(KX_ExecutionContext &ctx, uint32_t index) const
  {
    return ReadInput(ctx, index);
  }
  float InFloat(KX_ExecutionContext &ctx, uint32_t index) const
  {
    return ReadInputFloat(ctx, index);
  }
  int InInt(KX_ExecutionContext &ctx, uint32_t index) const
  {
    return ReadInputInt(ctx, index);
  }
  bool InBool(KX_ExecutionContext &ctx, uint32_t index) const
  {
    return ReadInputBool(ctx, index);
  }
  void InVector(KX_ExecutionContext &ctx, uint32_t index, float out[3]) const
  {
    ReadInputVector(ctx, index, out);
  }
  void *InPointer(KX_ExecutionContext &ctx, uint32_t index) const
  {
    return ReadInputPointer(ctx, index);
  }
  KX_GameObject *InObject(KX_ExecutionContext &ctx, uint32_t index) const;

  bool InTrigger(KX_ExecutionContext &ctx, uint32_t index) const
  {
    return ConsumeInputTrigger(ctx, index);
  }

  void Out(KX_ExecutionContext &ctx, uint32_t index, const KX_RuntimeValue &value) const
  {
    OutputValue(ctx, index) = value;
  }
  void OutFloat(KX_ExecutionContext &ctx, uint32_t index, float value) const
  {
    Out(ctx, index, KX_RuntimeValue::Float(value));
  }
  void OutInt(KX_ExecutionContext &ctx, uint32_t index, int value) const
  {
    Out(ctx, index, KX_RuntimeValue::Int(value));
  }
  void OutBool(KX_ExecutionContext &ctx, uint32_t index, bool value) const
  {
    Out(ctx, index, KX_RuntimeValue::Bool(value));
  }
  void OutVector(KX_ExecutionContext &ctx, uint32_t index, float x, float y, float z) const
  {
    Out(ctx, index, KX_RuntimeValue::Vector(x, y, z));
  }
  void OutColor(KX_ExecutionContext &ctx, uint32_t index, float r, float g, float b, float a) const
  {
    Out(ctx, index, KX_RuntimeValue::Color(r, g, b, a));
  }
  void OutString(KX_ExecutionContext &ctx, uint32_t index, const std::string &value) const
  {
    Out(ctx, index, KX_RuntimeValue::String(ctx.Pool().Intern(value)));
  }
  void OutObject(KX_ExecutionContext &ctx, uint32_t index, KX_GameObject *object) const
  {
    Out(ctx, index, KX_RuntimeValue::Object(object));
  }
  void OutTrigger(KX_ExecutionContext &ctx, uint32_t index, bool value) const
  {
    SetOutputTrigger(ctx, index, value);
  }
  /** Raise a trigger from #Tick; the scheduler forwards it after the tick pass. */
  void PendingTrigger(KX_ExecutionContext &ctx, uint32_t index)
  {
    EmitPendingTrigger(ctx, index);
  }

  template<typename T> T *StateBlock(KX_ExecutionContext &ctx) const
  {
    return this->State<T>(ctx);
  }

  /* Configuration helpers, all with a fallback value. */
  float ConfigFloat(const char *key, float fallback) const;
  int ConfigInt(const char *key, int fallback) const;
  bool ConfigBool(const char *key, bool fallback) const;
  std::string ConfigString(const char *key) const;
  void ConfigVector(const char *key, float out[3]) const;

 private:
  const KX_FunctionSpec *spec_ = nullptr;
  std::string config_;
};

/** Register every table driven node into the global registry. Idempotent. */
void RegisterFunctionGameNodes();

/** Register the second batch of table driven nodes. Idempotent. */
void RegisterExtendedFunctionGameNodes();

/** Register the B2 math/vector, colour, string and list node family. Idempotent. */
void RegisterB2FunctionGameNodes();

/** Register the B3 animation node family. Idempotent. */
void RegisterB3FunctionGameNodes();

/** Register the B4 save/file/JSON node family. Idempotent. */
void RegisterB4FileFunctionGameNodes();

/** Register the B4.1 UI node family. Idempotent. */
void RegisterB4UIFunctionGameNodes();
