/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Minimal math expression engine used by the #GeometryNodeMathExpression node.
 *
 * The expression is parsed into an AST once and then either evaluated on the CPU (geometry
 * nodes and compositor) or emitted as GLSL source (shader nodes).
 */

#pragma once

#include <memory>
#include <string>

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"
#include "BLI_vector.hh"

namespace blender::nodes::math_expr {

enum class Type : uint8_t {
  Float,
  Int,
  Vec3,
  Vec4,
  Mat4,
};

const char *type_name(Type type);

struct Value {
  Type type = Type::Float;
  /** Used for #Type::Float and #Type::Int (integer part is used for the latter). */
  float s = 0.0f;
  float3 v3 = float3(0.0f);
  float4 v4 = float4(0.0f);
  float4x4 m4 = float4x4::identity();
};

struct ExprNode {
  enum class Kind : uint8_t {
    Number,
    Port,
    Unary,
    Binary,
    Call,
  };

  Kind kind = Kind::Number;
  /** Operator character for #Kind::Unary and #Kind::Binary. */
  char op = 0;
  double number = 0.0;
  bool is_int_literal = false;
  /** Input index for #Kind::Port. */
  int port = -1;
  /** Function name for #Kind::Call. */
  std::string func;
  Vector<std::unique_ptr<ExprNode>> args;
};

struct ParseResult {
  std::unique_ptr<ExprNode> root;
  std::string error;
};

/** Parses the expression. Port references are validated against #input_count. */
ParseResult parse_expression(StringRef text, int input_count);

/** Returns the result type of the expression, or #Type::Float on error. */
Type infer_type(const ExprNode &root, Span<Type> input_types, std::string &r_error);

/** Evaluates the expression on the CPU. */
bool evaluate(const ExprNode &root,
              Span<Value> inputs,
              Value &r_result,
              std::string &r_error);

/**
 * Emits the expression as a GLSL expression string, converted to #out_type.
 * Input ports are emitted as `v0`, `v1`, ... .
 */
std::string to_glsl(const ExprNode &root,
                    Type out_type,
                    Span<Type> input_types,
                    std::string &r_error);

/** Converts a value to the given type in place. */
bool convert(Value &value, Type target, std::string &r_error);

}  // namespace blender::nodes::math_expr
