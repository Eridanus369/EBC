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

/** \file KX_RuntimeNodes.h
 *  \ingroup gamelogic
 *
 *  Built-in runtime node classes.
 *
 *  Float Math and Vector Math are pure value nodes. Branch, Delay, Raycast and
 *  the asset nodes are trigger nodes. Delay owns a small state block and is the
 *  only P1 node ticked before trigger propagation. AssetGet can run in poll
 *  mode, in which case it joins the per frame value node list.
 */

#pragma once

#include <cstdint>
#include <string>

#include "KX_GameDataAsset.h"
#include "KX_GameNodeRuntime.h"
#include "KX_NodeRegistry.h"

/** Register the built-in P1 and P2 nodes. Idempotent. */
void RegisterBuiltinGameNodes();

class KX_Node_FloatMath : public KX_GameNodeRuntime {
 public:
  enum class Op : uint8_t {
    Add = 0,
    Sub,
    Mul,
    Div,
    Min,
    Max,
    Pow,
  };

  enum Input : uint32_t {
    IN_A = 0,
    IN_B = 1,
  };
  enum Output : uint32_t {
    OUT_RESULT = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool ExecuteValueOnly(KX_ExecutionContext &ctx) override
  {
    Execute(ctx);
    return true;
  }
  bool Configure(const std::string &config, std::string *error) override;

  Op Operation() const;
  /** True once a division by zero was observed. Diagnostic, never fatal. */
  bool Warning() const;

 private:
  Op op_ = Op::Add;
  bool warning_ = false;
};

class KX_Node_VectorMath : public KX_GameNodeRuntime {
 public:
  enum class Op : uint8_t {
    Add = 0,
    Sub,
    Mul,
    Dot,
    Cross,
    Normalize,
    Length,
  };

  enum Input : uint32_t {
    IN_A = 0,
    IN_B = 1,
  };
  enum Output : uint32_t {
    OUT_RESULT = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool ExecuteValueOnly(KX_ExecutionContext &ctx) override
  {
    Execute(ctx);
    return true;
  }
  bool Configure(const std::string &config, std::string *error) override;

  Op Operation() const;

 private:
  Op op_ = Op::Add;
};

class KX_Node_Branch : public KX_GameNodeRuntime {
 public:
  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_CONDITION = 1,
  };
  enum Output : uint32_t {
    OUT_TRUE = 0,
    OUT_FALSE = 1,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
};

struct KX_DelayState {
  float remaining_sec = 0.0f;
  bool pending = false;
};

class KX_Node_Delay : public KX_GameNodeRuntime {
 public:
  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_DURATION = 1,
  };
  enum Output : uint32_t {
    OUT_DONE = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  void Tick(KX_ExecutionContext &ctx, float delta_time) override;
};

class KX_Node_Raycast : public KX_GameNodeRuntime {
 public:
  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_ORIGIN = 1,
    IN_DIRECTION = 2,
    IN_MAX_DIST = 3,
  };
  enum Output : uint32_t {
    OUT_HIT_TRIGGER = 0,
    OUT_MISS_TRIGGER = 1,
    OUT_HIT_OBJECT = 2,
    OUT_HIT_POINT = 3,
    OUT_HIT_NORMAL = 4,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
};

/** Read one field of a shared game data asset. */
class KX_Node_AssetGet : public KX_GameNodeRuntime {
 public:
  enum class Mode : uint8_t {
    Poll = 0,
    Trigger = 1,
  };

  enum Input : uint32_t {
    IN_TRIGGER = 0,
  };
  enum Output : uint32_t {
    OUT_VALUE = 0,
    OUT_READ = 1,
  };

  static KX_NodeSignature Signature();

  ~KX_Node_AssetGet() override;

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool Configure(const std::string &config, std::string *error) override;

  bool HasAssetBinding() const override;
  bool ResolveAssetBinding(KX_AssetRegistry &registry, std::string *error) override;
  KX_ValueType ResolvedPortType(bool input, uint32_t port) const override;
  bool GetResolvedAssetBinding(KX_ResolvedAssetBinding *binding) const override;
  bool IsValueNode() const override;

  Mode GetMode() const;
  KX_AssetHandle Handle() const;

 private:
  std::string asset_ref_;
  std::string field_name_;
  std::string expected_asset_type_;
  Mode mode_ = Mode::Poll;

  KX_AssetHandle handle_ = KX_INVALID_ASSET_HANDLE;
  uint32_t field_index_ = 0;
  KX_AssetFieldType field_type_ = KX_AssetFieldType::Float;
  KX_ValueType field_value_type_ = KX_VALUE_FLOAT;
  uint32_t registry_generation_ = 0;
};

/** Write one field of a shared game data asset. */
class KX_Node_AssetSet : public KX_GameNodeRuntime {
 public:
  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_VALUE = 1,
  };
  enum Output : uint32_t {
    OUT_WRITTEN = 0,
  };

  static KX_NodeSignature Signature();

  ~KX_Node_AssetSet() override;

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool Configure(const std::string &config, std::string *error) override;

  bool HasAssetBinding() const override;
  bool ResolveAssetBinding(KX_AssetRegistry &registry, std::string *error) override;
  KX_ValueType ResolvedPortType(bool input, uint32_t port) const override;
  bool GetResolvedAssetBinding(KX_ResolvedAssetBinding *binding) const override;

  KX_AssetHandle Handle() const;

 private:
  std::string asset_ref_;
  std::string field_name_;
  std::string expected_asset_type_;

  KX_AssetHandle handle_ = KX_INVALID_ASSET_HANDLE;
  uint32_t field_index_ = 0;
  KX_AssetFieldType field_type_ = KX_AssetFieldType::Float;
  KX_ValueType field_value_type_ = KX_VALUE_FLOAT;
  uint32_t registry_generation_ = 0;
};

/** Read one game property of an object through the generation counted cache. */
class KX_Node_ObjectGet : public KX_GameNodeRuntime {
 public:
  enum class Mode : uint8_t {
    Poll = 0,
    Trigger = 1,
  };

  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_OBJECT = 1,
  };
  enum Output : uint32_t {
    OUT_VALUE = 0,
    OUT_READ = 1,
    OUT_MISSING = 2,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool Configure(const std::string &config, std::string *error) override;
  KX_ValueType ResolvedPortType(bool input, uint32_t port) const override;
  bool IsValueNode() const override;

  Mode GetMode() const;
  const std::string &PropertyName() const;

 private:
  std::string property_name_;
  Mode mode_ = Mode::Poll;
  /** Type declared by the graph; missing properties read as its zero value. */
  KX_ValueType value_type_ = KX_VALUE_FLOAT;
};

/** Write one game property of an object through the generation counted cache. */
class KX_Node_ObjectSet : public KX_GameNodeRuntime {
 public:
  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_OBJECT = 1,
    IN_VALUE = 2,
  };
  enum Output : uint32_t {
    OUT_WRITTEN = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool Configure(const std::string &config, std::string *error) override;
  KX_ValueType ResolvedPortType(bool input, uint32_t port) const override;

  const std::string &PropertyName() const;
  bool CreatesMissingProperty() const;

 private:
  std::string property_name_;
  /** Type declared by the graph for the value input and for created properties. */
  KX_ValueType value_type_ = KX_VALUE_FLOAT;
  bool create_missing_ = true;
  /** Creating a misspelled property is reported once per node instance. */
  bool creation_warned_ = false;
};

/* ------------------------------------------------------------------------- */
/* Graph entry nodes                                                          */
/* ------------------------------------------------------------------------- */

/** Emits its trigger exactly once, on the first frame of a graph instance. */
class KX_Node_OnInit : public KX_GameNodeRuntime {
 public:
  enum Output : uint32_t {
    OUT_TRIGGER = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;

 private:
  bool fired_ = false;
};

/** Emits its trigger every frame. */
class KX_Node_OnUpdate : public KX_GameNodeRuntime {
 public:
  enum Output : uint32_t {
    OUT_TRIGGER = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
};

/* ------------------------------------------------------------------------- */
/* Constant value nodes                                                       */
/* ------------------------------------------------------------------------- */

/** Feeds a float literal into the graph. The value comes from config `value`. */
class KX_Node_ConstFloat : public KX_GameNodeRuntime {
 public:
  enum Output : uint32_t {
    OUT_VALUE = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool ExecuteValueOnly(KX_ExecutionContext &ctx) override
  {
    Execute(ctx);
    return true;
  }
  bool Configure(const std::string &config, std::string *error) override;

 private:
  float value_ = 0.0f;
};

/** Feeds a bool literal into the graph. The value comes from config `value`. */
class KX_Node_ConstBool : public KX_GameNodeRuntime {
 public:
  enum Output : uint32_t {
    OUT_VALUE = 0,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool ExecuteValueOnly(KX_ExecutionContext &ctx) override
  {
    Execute(ctx);
    return true;
  }
  bool Configure(const std::string &config, std::string *error) override;

 private:
  bool value_ = false;
};

/* ------------------------------------------------------------------------- */
/* Sink                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * Records the last value it consumed and a running count.
 *
 * It exists so the E2E tests can assert "the trigger arrived N times with
 * value V" without a rendering side effect.
 */
class KX_Node_Sink : public KX_GameNodeRuntime {
 public:
  enum Input : uint32_t {
    IN_TRIGGER = 0,
    IN_VALUE = 1,
  };

  static KX_NodeSignature Signature();

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;

  uint32_t Count() const;
  KX_RuntimeValue LastValue() const;

 private:
  uint32_t count_ = 0;
  KX_RuntimeValue last_value_;
};
