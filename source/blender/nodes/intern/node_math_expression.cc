/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Implementation of the small math expression engine declared in
 * `NOD_math_expression.hh`.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"
#include "BLI_vector.hh"

#include "NOD_math_expression.hh"

namespace blender::nodes::math_expr {

const char *type_name(Type type)
{
  switch (type) {
    case Type::Float:
      return "Float";
    case Type::Int:
      return "Int";
    case Type::Vec3:
      return "Vec3";
    case Type::Vec4:
      return "Vec4";
    case Type::Mat4:
      return "Mat4";
  }
  return "Float";
}

namespace {

/* -------------------------------------------------------------------- */
/** \name Lexer
 * \{ */

struct Token {
  enum class Kind : uint8_t {
    Number,
    Ident,
    Port,
    Op,
    LParen,
    RParen,
    Comma,
    End,
  };

  Kind kind = Kind::End;
  double number = 0.0;
  bool is_int = false;
  int port = -1;
  std::string text;
};

static bool is_ident_start(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static bool tokenize(StringRef text, Vector<Token> &r_tokens, std::string &r_error)
{
  const char *data = text.data();
  const size_t size = text.size();
  size_t i = 0;
  while (i < size) {
    const char c = data[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      i++;
      continue;
    }
    if (is_digit(c) || (c == '.' && i + 1 < size && is_digit(data[i + 1]))) {
      const size_t begin = i;
      bool is_int = true;
      while (i < size && is_digit(data[i])) {
        i++;
      }
      if (i < size && data[i] == '.') {
        is_int = false;
        i++;
        while (i < size && is_digit(data[i])) {
          i++;
        }
      }
      if (i < size && (data[i] == 'e' || data[i] == 'E')) {
        const size_t save = i;
        i++;
        if (i < size && (data[i] == '+' || data[i] == '-')) {
          i++;
        }
        if (i < size && is_digit(data[i])) {
          is_int = false;
          while (i < size && is_digit(data[i])) {
            i++;
          }
        }
        else {
          i = save;
        }
      }
      const std::string sub(data + begin, i - begin);
      Token token;
      token.kind = Token::Kind::Number;
      token.number = std::strtod(sub.c_str(), nullptr);
      token.is_int = is_int;
      r_tokens.append(std::move(token));
      continue;
    }
    if (c == '{') {
      i++;
      const size_t begin = i;
      while (i < size && is_digit(data[i])) {
        i++;
      }
      if (i == begin || i >= size || data[i] != '}') {
        r_error = "Invalid port reference, expected {number}";
        return false;
      }
      const std::string sub(data + begin, i - begin);
      i++;
      Token token;
      token.kind = Token::Kind::Port;
      token.port = std::atoi(sub.c_str());
      r_tokens.append(std::move(token));
      continue;
    }
    if (is_ident_start(c)) {
      const size_t begin = i;
      while (i < size && (is_ident_start(data[i]) || is_digit(data[i]))) {
        i++;
      }
      Token token;
      token.kind = Token::Kind::Ident;
      token.text = std::string(data + begin, i - begin);
      r_tokens.append(std::move(token));
      continue;
    }
    if (c == '(' || c == ')' || c == ',') {
      Token token;
      token.kind = (c == '(') ? Token::Kind::LParen :
                  (c == ')') ? Token::Kind::RParen : Token::Kind::Comma;
      i++;
      r_tokens.append(std::move(token));
      continue;
    }
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
      Token token;
      token.kind = Token::Kind::Op;
      token.text = std::string(1, c);
      i++;
      r_tokens.append(std::move(token));
      continue;
    }
    r_error = std::string("Unexpected character '") + c + "'";
    return false;
  }
  Token end;
  end.kind = Token::Kind::End;
  r_tokens.append(std::move(end));
  return true;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Parser
 * \{ */

static std::unique_ptr<ExprNode> make_binary(char op,
                                             std::unique_ptr<ExprNode> a,
                                             std::unique_ptr<ExprNode> b)
{
  std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
  node->kind = ExprNode::Kind::Binary;
  node->op = op;
  node->args.append(std::move(a));
  node->args.append(std::move(b));
  return node;
}

class Parser {
 private:
  const Vector<Token> &tokens_;
  size_t pos_ = 0;
  int input_count_ = 0;
  std::string error_;

 public:
  Parser(const Vector<Token> &tokens, int input_count)
      : tokens_(tokens), input_count_(input_count)
  {
  }

  std::unique_ptr<ExprNode> parse(std::string &r_error)
  {
    if (peek().kind == Token::Kind::End) {
      r_error = "Expression is empty";
      return nullptr;
    }
    std::unique_ptr<ExprNode> root = parse_add_sub();
    if (!error_.empty()) {
      r_error = error_;
      return nullptr;
    }
    if (!root) {
      r_error = "Invalid expression";
      return nullptr;
    }
    if (peek().kind != Token::Kind::End) {
      r_error = "Unexpected token at end of expression";
      return nullptr;
    }
    return root;
  }

 private:
  const Token &peek() const
  {
    return tokens_[pos_];
  }

  const Token &consume()
  {
    return tokens_[pos_++];
  }

  std::unique_ptr<ExprNode> parse_add_sub()
  {
    std::unique_ptr<ExprNode> left = parse_mul();
    if (!left) {
      return nullptr;
    }
    while (error_.empty() && peek().kind == Token::Kind::Op) {
      const char op = peek().text[0];
      if (op != '+' && op != '-') {
        break;
      }
      consume();
      std::unique_ptr<ExprNode> right = parse_mul();
      if (!right) {
        return nullptr;
      }
      left = make_binary(op, std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExprNode> parse_mul()
  {
    std::unique_ptr<ExprNode> left = parse_unary();
    if (!left) {
      return nullptr;
    }
    while (error_.empty() && peek().kind == Token::Kind::Op) {
      const char op = peek().text[0];
      if (op != '*' && op != '/' && op != '%') {
        break;
      }
      consume();
      std::unique_ptr<ExprNode> right = parse_unary();
      if (!right) {
        return nullptr;
      }
      left = make_binary(op, std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExprNode> parse_unary()
  {
    if (peek().kind == Token::Kind::Op && (peek().text[0] == '-' || peek().text[0] == '+')) {
      const char op = peek().text[0];
      consume();
      std::unique_ptr<ExprNode> arg = parse_unary();
      if (!arg) {
        return nullptr;
      }
      if (op == '+') {
        return arg;
      }
      std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
      node->kind = ExprNode::Kind::Unary;
      node->op = op;
      node->args.append(std::move(arg));
      return node;
    }
    return parse_power();
  }

  std::unique_ptr<ExprNode> parse_power()
  {
    std::unique_ptr<ExprNode> left = parse_primary();
    if (!left) {
      return nullptr;
    }
    if (error_.empty() && peek().kind == Token::Kind::Op && peek().text[0] == '^') {
      consume();
      std::unique_ptr<ExprNode> right = parse_unary();
      if (!right) {
        return nullptr;
      }
      left = make_binary('^', std::move(left), std::move(right));
    }
    return left;
  }

  std::unique_ptr<ExprNode> parse_primary()
  {
    const Token &token = peek();
    if (token.kind == Token::Kind::Number) {
      std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
      node->kind = ExprNode::Kind::Number;
      node->number = token.number;
      node->is_int_literal = token.is_int;
      consume();
      return node;
    }
    if (token.kind == Token::Kind::Port) {
      if (token.port < 0 || token.port >= input_count_) {
        error_ = "Port {" + std::to_string(token.port) + "} does not exist";
        return nullptr;
      }
      std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
      node->kind = ExprNode::Kind::Port;
      node->port = token.port;
      consume();
      return node;
    }
    if (token.kind == Token::Kind::Ident) {
      if (token.text == "pi") {
        std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
        node->kind = ExprNode::Kind::Number;
        node->number = 3.14159265358979323846;
        node->is_int_literal = false;
        consume();
        return node;
      }
      if (token.text == "e") {
        std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
        node->kind = ExprNode::Kind::Number;
        node->number = 2.71828182845904523536;
        node->is_int_literal = false;
        consume();
        return node;
      }
      if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == Token::Kind::LParen) {
        return parse_call();
      }
      error_ = "Unknown identifier '" + token.text + "'";
      return nullptr;
    }
    if (token.kind == Token::Kind::LParen) {
      consume();
      std::unique_ptr<ExprNode> inner = parse_add_sub();
      if (!inner) {
        return nullptr;
      }
      if (peek().kind != Token::Kind::RParen) {
        error_ = "Expected ')'";
        return nullptr;
      }
      consume();
      return inner;
    }
    error_ = "Unexpected token, expected a number, a port reference or a function call";
    return nullptr;
  }

  std::unique_ptr<ExprNode> parse_call()
  {
    std::unique_ptr<ExprNode> node = std::make_unique<ExprNode>();
    node->kind = ExprNode::Kind::Call;
    node->func = peek().text;
    consume(); /* function name */
    consume(); /* '(' */
    if (peek().kind == Token::Kind::RParen) {
      consume();
      return node;
    }
    while (true) {
      std::unique_ptr<ExprNode> arg = parse_add_sub();
      if (!arg) {
        return nullptr;
      }
      node->args.append(std::move(arg));
      if (peek().kind == Token::Kind::Comma) {
        consume();
        continue;
      }
      if (peek().kind == Token::Kind::RParen) {
        consume();
        break;
      }
      error_ = "Expected ',' or ')' in function call";
      return nullptr;
    }
    return node;
  }
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Type system
 * \{ */

static bool is_scalar_type(Type type)
{
  return type == Type::Float || type == Type::Int;
}

static bool is_vector_type(Type type)
{
  return type == Type::Vec3 || type == Type::Vec4;
}

static int component_count(Type type)
{
  switch (type) {
    case Type::Float:
    case Type::Int:
      return 1;
    case Type::Vec3:
      return 3;
    case Type::Vec4:
      return 4;
    case Type::Mat4:
      return 16;
  }
  return 1;
}

static Type promote_scalar(Type a, Type b)
{
  return (a == Type::Int && b == Type::Int) ? Type::Int : Type::Float;
}

static Type binary_type(char op, Type a, Type b, std::string &r_error)
{
  if (op == '+' || op == '-') {
    if (is_scalar_type(a) && is_scalar_type(b)) {
      return promote_scalar(a, b);
    }
    if (a == b && (is_vector_type(a) || a == Type::Mat4)) {
      return a;
    }
    if (is_scalar_type(a) && is_vector_type(b)) {
      return b;
    }
    if (is_vector_type(a) && is_scalar_type(b)) {
      return a;
    }
  }
  else if (op == '*') {
    if (is_scalar_type(a) && is_scalar_type(b)) {
      return promote_scalar(a, b);
    }
    if (is_scalar_type(a) && is_vector_type(b)) {
      return b;
    }
    if (is_vector_type(a) && is_scalar_type(b)) {
      return a;
    }
    if (is_scalar_type(a) && b == Type::Mat4) {
      return Type::Mat4;
    }
    if (a == Type::Mat4 && is_scalar_type(b)) {
      return Type::Mat4;
    }
    if (a == b && is_vector_type(a)) {
      return a;
    }
    if (a == Type::Mat4 && b == Type::Mat4) {
      return Type::Mat4;
    }
    if (a == Type::Mat4 && b == Type::Vec4) {
      return Type::Vec4;
    }
  }
  else if (op == '/') {
    if (is_scalar_type(a) && is_scalar_type(b)) {
      return promote_scalar(a, b);
    }
    if (is_vector_type(a) && is_scalar_type(b)) {
      return a;
    }
    if (a == b && is_vector_type(a)) {
      return a;
    }
    if (a == Type::Mat4 && is_scalar_type(b)) {
      return Type::Mat4;
    }
  }
  else if (op == '%') {
    if (is_scalar_type(a) && is_scalar_type(b)) {
      return promote_scalar(a, b);
    }
  }
  else if (op == '^') {
    if (is_scalar_type(a) && is_scalar_type(b)) {
      return Type::Float;
    }
  }

  r_error = std::string("Type mismatch: ") + type_name(a) + " " + op + " " + type_name(b);
  return Type::Float;
}

/** Type of a function call, based on the already inferred argument types. */
static Type call_result_type(const std::string &func, Span<Type> arg_types, std::string &r_error)
{
  auto expect_args = [&](int min_args, int max_args) -> bool {
    if (int(arg_types.size()) < min_args || int(arg_types.size()) > max_args) {
      r_error = "Function '" + func + "' expects " + std::to_string(min_args) +
                (max_args == min_args ? "" : " to " + std::to_string(max_args)) + " argument(s)";
      return false;
    }
    return true;
  };
  auto shape = [&]() -> Type {
    Type result = Type::Float;
    for (const Type type : arg_types) {
      if (type == Type::Mat4) {
        r_error = "Function '" + func + "' does not accept matrices";
        return Type::Float;
      }
      if (is_vector_type(type)) {
        if (result != Type::Float && result != type) {
          r_error = "Function '" + func + "' got mismatching vector types";
          return Type::Float;
        }
        result = type;
      }
    }
    return result;
  };

  if (func == "length" || func == "normalize") {
    if (!expect_args(1, 1)) {
      return Type::Float;
    }
    if (!is_vector_type(arg_types[0])) {
      r_error = "Function '" + func + "' expects a vector argument";
      return Type::Float;
    }
    return (func == "length") ? Type::Float : arg_types[0];
  }
  if (func == "dot" || func == "cross") {
    if (!expect_args(2, 2)) {
      return Type::Float;
    }
    if (!is_vector_type(arg_types[0]) || arg_types[0] != arg_types[1]) {
      r_error = "Function '" + func + "' expects two vectors of the same type";
      return Type::Float;
    }
    if (func == "cross" && arg_types[0] != Type::Vec3) {
      r_error = "Function 'cross' expects Vec3 arguments";
      return Type::Float;
    }
    return (func == "dot") ? Type::Float : Type::Vec3;
  }
  if (func == "clamp") {
    if (!expect_args(3, 3)) {
      return Type::Float;
    }
    const Type type = shape();
    if (!r_error.empty()) {
      return Type::Float;
    }
    if (type != Type::Float) {
      return type;
    }
    return (arg_types[0] == Type::Int && arg_types[1] == Type::Int &&
            arg_types[2] == Type::Int) ?
               Type::Int :
               Type::Float;
  }
  if (func == "min" || func == "max") {
    if (!expect_args(2, 2)) {
      return Type::Float;
    }
    const Type type = shape();
    if (!r_error.empty()) {
      return Type::Float;
    }
    if (type != Type::Float) {
      return type;
    }
    return promote_scalar(arg_types[0], arg_types[1]);
  }
  if (func == "atan2" || func == "pow" || func == "mod") {
    if (!expect_args(2, 2)) {
      return Type::Float;
    }
    return shape();
  }

  /* Single argument element-wise functions. */
  if (func == "abs" || func == "sign") {
    if (!expect_args(1, 1)) {
      return Type::Float;
    }
    const Type type = arg_types[0];
    if (type == Type::Mat4) {
      r_error = "Function '" + func + "' does not accept matrices";
      return Type::Float;
    }
    return type;
  }

  static const char *unary_float_funcs[] = {
      "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "exp", "log", "log2", "exp2",
      "fract", "floor", "ceil", "round",
  };
  for (const char *name : unary_float_funcs) {
    if (func == name) {
      if (!expect_args(1, 1)) {
        return Type::Float;
      }
      const Type type = arg_types[0];
      if (type == Type::Mat4) {
        r_error = "Function '" + func + "' does not accept matrices";
        return Type::Float;
      }
      return is_scalar_type(type) ? Type::Float : type;
    }
  }

  r_error = "Unknown function '" + func + "'";
  return Type::Float;
}

static Type infer(const ExprNode &node, Span<Type> input_types, std::string &r_error);

static Type infer_call(const ExprNode &node, Span<Type> input_types, std::string &r_error)
{
  Vector<Type> arg_types;
  for (const std::unique_ptr<ExprNode> &arg : node.args) {
    arg_types.append(infer(*arg, input_types, r_error));
    if (!r_error.empty()) {
      return Type::Float;
    }
  }
  return call_result_type(node.func, arg_types.as_span(), r_error);
}

static Type infer(const ExprNode &node, Span<Type> input_types, std::string &r_error)
{
  switch (node.kind) {
    case ExprNode::Kind::Number:
      return node.is_int_literal ? Type::Int : Type::Float;
    case ExprNode::Kind::Port:
      return input_types[node.port];
    case ExprNode::Kind::Unary: {
      const Type type = infer(*node.args[0], input_types, r_error);
      return type;
    }
    case ExprNode::Kind::Binary: {
      const Type a = infer(*node.args[0], input_types, r_error);
      if (!r_error.empty()) {
        return Type::Float;
      }
      const Type b = infer(*node.args[1], input_types, r_error);
      if (!r_error.empty()) {
        return Type::Float;
      }
      return binary_type(node.op, a, b, r_error);
    }
    case ExprNode::Kind::Call:
      return infer_call(node, input_types, r_error);
  }
  return Type::Float;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name CPU evaluation
 * \{ */

static float component_get(const Value &value, int index)
{
  switch (value.type) {
    case Type::Float:
    case Type::Int:
      return value.s;
    case Type::Vec3:
      return value.v3[index];
    case Type::Vec4:
      return value.v4[index];
    case Type::Mat4:
      return value.m4[index / 4][index % 4];
  }
  return 0.0f;
}

static void component_set(Value &value, int index, float v)
{
  switch (value.type) {
    case Type::Float:
    case Type::Int:
      value.s = v;
      break;
    case Type::Vec3:
      value.v3[index] = v;
      break;
    case Type::Vec4:
      value.v4[index] = v;
      break;
    case Type::Mat4:
      value.m4[index / 4][index % 4] = v;
      break;
  }
}

static float eval_float_func(const std::string &func, float x)
{
  if (func == "sin") {
    return std::sin(x);
  }
  if (func == "cos") {
    return std::cos(x);
  }
  if (func == "tan") {
    return std::tan(x);
  }
  if (func == "asin") {
    return std::asin(x);
  }
  if (func == "acos") {
    return std::acos(x);
  }
  if (func == "atan") {
    return std::atan(x);
  }
  if (func == "sqrt") {
    return std::sqrt(x);
  }
  if (func == "exp") {
    return std::exp(x);
  }
  if (func == "exp2") {
    return std::exp2(x);
  }
  if (func == "log") {
    return std::log(x);
  }
  if (func == "log2") {
    return std::log2(x);
  }
  if (func == "fract") {
    return x - std::floor(x);
  }
  if (func == "floor") {
    return std::floor(x);
  }
  if (func == "ceil") {
    return std::ceil(x);
  }
  if (func == "round") {
    return std::round(x);
  }
  if (func == "abs") {
    return std::fabs(x);
  }
  if (func == "sign") {
    return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f);
  }
  return x;
}

static Value eval_node(const ExprNode &node, Span<Value> inputs, std::string &r_error);

static Value eval_call(const ExprNode &node, Span<Value> inputs, std::string &r_error)
{
  Vector<Value> args;
  for (const std::unique_ptr<ExprNode> &arg_node : node.args) {
    Value value = eval_node(*arg_node, inputs, r_error);
    if (!r_error.empty()) {
      return Value{};
    }
    args.append(std::move(value));
  }
  Vector<Type> arg_types;
  for (const Value &value : args) {
    arg_types.append(value.type);
  }
  const Type result_type = call_result_type(node.func, arg_types.as_span(), r_error);
  if (!r_error.empty()) {
    return Value{};
  }

  Value result;
  result.type = result_type;

  if (node.func == "length") {
    float sum = 0.0f;
    for (int i = 0; i < component_count(args[0].type); i++) {
      const float c = component_get(args[0], i);
      sum += c * c;
    }
    result.s = std::sqrt(sum);
    return result;
  }
  if (node.func == "dot") {
    float sum = 0.0f;
    for (int i = 0; i < component_count(args[0].type); i++) {
      sum += component_get(args[0], i) * component_get(args[1], i);
    }
    result.s = sum;
    return result;
  }
  if (node.func == "cross") {
    const float3 a = args[0].v3;
    const float3 b = args[1].v3;
    result.v3 = float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    return result;
  }
  if (node.func == "normalize") {
    float sum = 0.0f;
    const int n = component_count(args[0].type);
    for (int i = 0; i < n; i++) {
      const float c = component_get(args[0], i);
      sum += c * c;
    }
    const float len = std::sqrt(sum);
    if (len == 0.0f) {
      if (args[0].type == Type::Vec3) {
        result.v3 = float3(0.0f);
      }
      else {
        result.v4 = float4(0.0f);
      }
      return result;
    }
    for (int i = 0; i < n; i++) {
      component_set(result, i, component_get(args[0], i) / len);
    }
    return result;
  }

  const int n = component_count(result_type);
  for (int i = 0; i < n; i++) {
    Vector<float> xs;
    for (const Value &value : args) {
      xs.append((component_count(value.type) == 1) ? component_get(value, 0) :
                                                     component_get(value, i));
    }
    float v = 0.0f;
    if (node.func == "atan2") {
      v = std::atan2(xs[0], xs[1]);
    }
    else if (node.func == "pow") {
      v = std::pow(xs[0], xs[1]);
    }
    else if (node.func == "mod") {
      v = std::fmod(xs[0], xs[1]);
    }
    else if (node.func == "min") {
      v = std::min(xs[0], xs[1]);
    }
    else if (node.func == "max") {
      v = std::max(xs[0], xs[1]);
    }
    else if (node.func == "clamp") {
      v = xs[0] < xs[1] ? xs[1] : ((xs[0] > xs[2]) ? xs[2] : xs[0]);
    }
    else {
      v = eval_float_func(node.func, xs[0]);
    }
    if (result_type == Type::Int) {
      v = std::trunc(v);
    }
    component_set(result, i, v);
  }
  return result;
}

static Value eval_node(const ExprNode &node, Span<Value> inputs, std::string &r_error)
{
  switch (node.kind) {
    case ExprNode::Kind::Number: {
      Value value;
      value.type = node.is_int_literal ? Type::Int : Type::Float;
      value.s = float(node.number);
      return value;
    }
    case ExprNode::Kind::Port:
      return inputs[node.port];
    case ExprNode::Kind::Unary: {
      Value value = eval_node(*node.args[0], inputs, r_error);
      if (!r_error.empty()) {
        return Value{};
      }
      const int n = component_count(value.type);
      for (int i = 0; i < n; i++) {
        component_set(value, i, -component_get(value, i));
      }
      return value;
    }
    case ExprNode::Kind::Binary: {
      Value a = eval_node(*node.args[0], inputs, r_error);
      if (!r_error.empty()) {
        return Value{};
      }
      Value b = eval_node(*node.args[1], inputs, r_error);
      if (!r_error.empty()) {
        return Value{};
      }
      const Type type = binary_type(node.op, a.type, b.type, r_error);
      if (!r_error.empty()) {
        return Value{};
      }
      Value result;
      result.type = type;
      if (node.op == '*' && type == Type::Mat4 && a.type == Type::Mat4 &&
          b.type == Type::Mat4)
      {
        result.m4 = a.m4 * b.m4;
        return result;
      }
      if (node.op == '*' && type == Type::Vec4 && a.type == Type::Mat4) {
        result.v4 = a.m4 * b.v4;
        return result;
      }
      const int n = component_count(type);
      for (int i = 0; i < n; i++) {
        const float x = (component_count(a.type) == 1) ? component_get(a, 0) : component_get(a, i);
        const float y = (component_count(b.type) == 1) ? component_get(b, 0) : component_get(b, i);
        float v = 0.0f;
        switch (node.op) {
          case '+':
            v = x + y;
            break;
          case '-':
            v = x - y;
            break;
          case '*':
            v = x * y;
            break;
          case '/':
            if (type == Type::Int) {
              v = (y == 0.0f) ? 0.0f : std::trunc(x / y);
            }
            else {
              v = x / y;
            }
            break;
          case '%': {
            if (type == Type::Int) {
              const int iy = int(y);
              v = (iy == 0) ? 0.0f : float(int(x) % iy);
            }
            else {
              v = std::fmod(x, y);
            }
            break;
          }
          case '^':
            v = std::pow(x, y);
            break;
          default:
            break;
        }
        component_set(result, i, v);
      }
      return result;
    }
    case ExprNode::Kind::Call:
      return eval_call(node, inputs, r_error);
  }
  return Value{};
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name GLSL emission
 * \{ */

static std::string format_float(double v)
{
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.9g", v);
  std::string result(buf);
  if (result.find_first_of(".eE") == std::string::npos) {
    result += ".0";
  }
  return result;
}

static std::string cast_scalar_to_float(const std::string &expr, Type type)
{
  return (type == Type::Int) ? "float(" + expr + ")" : expr;
}

static std::string emit_cast_arg(const std::string &expr, Type from, Type to)
{
  if (from == to) {
    return expr;
  }
  if (to == Type::Vec3 && is_scalar_type(from)) {
    return "vec3(" + cast_scalar_to_float(expr, from) + ")";
  }
  if (to == Type::Vec4 && is_scalar_type(from)) {
    return "vec4(" + cast_scalar_to_float(expr, from) + ")";
  }
  if (is_scalar_type(to) && from == Type::Int) {
    return "float(" + expr + ")";
  }
  return expr;
}

static std::string glsl_cast(const std::string &expr, Type from, Type to)
{
  if (from == to) {
    return expr;
  }
  const std::string src = "(" + expr + ")";
  if (to == Type::Float) {
    if (is_scalar_type(from)) {
      return (from == Type::Int) ? "float(" + expr + ")" : expr;
    }
    if (is_vector_type(from)) {
      return src + ".x";
    }
    return src + "[0][0]";
  }
  if (to == Type::Int) {
    return "int(" + glsl_cast(expr, from, Type::Float) + ")";
  }
  if (to == Type::Vec3) {
    if (from == Type::Vec4) {
      return src + ".xyz";
    }
    if (from == Type::Mat4) {
      return src + "[0].xyz";
    }
    return "vec3(" + glsl_cast(expr, from, Type::Float) + ")";
  }
  if (to == Type::Vec4) {
    if (from == Type::Vec3) {
      return "vec4(" + src + ", 0.0)";
    }
    if (from == Type::Mat4) {
      return src + "[0]";
    }
    return "vec4(" + glsl_cast(expr, from, Type::Float) + ")";
  }
  /* Matrix. */
  if (from == Type::Float || from == Type::Int) {
    return "mat4(" + glsl_cast(expr, from, Type::Float) + ")";
  }
  if (from == Type::Vec3) {
    return "mat4(vec4(" + src + ".x, 0.0, 0.0, 0.0), vec4(0.0, " + src +
           ".y, 0.0, 0.0), vec4(0.0, 0.0, " + src + ".z, 0.0), vec4(0.0, 0.0, 0.0, 1.0))";
  }
  if (from == Type::Vec4) {
    return "mat4(vec4(" + src + ".x, 0.0, 0.0, 0.0), vec4(0.0, " + src +
           ".y, 0.0, 0.0), vec4(0.0, 0.0, " + src + ".z, 0.0), vec4(0.0, 0.0, 0.0, " + src +
           ".w))";
  }
  return expr;
}

static bool emit_node(const ExprNode &node,
                      Span<Type> input_types,
                      std::string &r_expr,
                      Type &r_type,
                      std::string &r_error)
{
  switch (node.kind) {
    case ExprNode::Kind::Number: {
      if (node.is_int_literal) {
        r_expr = std::to_string(int64_t(node.number));
        r_type = Type::Int;
      }
      else {
        r_expr = format_float(node.number);
        r_type = Type::Float;
      }
      return true;
    }
    case ExprNode::Kind::Port: {
      r_expr = "v" + std::to_string(node.port);
      r_type = input_types[node.port];
      return true;
    }
    case ExprNode::Kind::Unary: {
      std::string arg;
      Type arg_type;
      if (!emit_node(*node.args[0], input_types, arg, arg_type, r_error)) {
        return false;
      }
      r_expr = "-(" + arg + ")";
      r_type = arg_type;
      return true;
    }
    case ExprNode::Kind::Binary: {
      std::string a;
      std::string b;
      Type a_type;
      Type b_type;
      if (!emit_node(*node.args[0], input_types, a, a_type, r_error)) {
        return false;
      }
      if (!emit_node(*node.args[1], input_types, b, b_type, r_error)) {
        return false;
      }
      const Type type = binary_type(node.op, a_type, b_type, r_error);
      if (!r_error.empty()) {
        return false;
      }
      if (node.op == '^') {
        r_expr = "pow(" + cast_scalar_to_float(a, a_type) + ", " +
                cast_scalar_to_float(b, b_type) + ")";
      }
      else if (node.op == '%' && type == Type::Float) {
        r_expr = "fmod(" + a + ", " + b + ")";
      }
      else {
        r_expr = "(" + a + " " + std::string(1, node.op) + " " + b + ")";
      }
      r_type = type;
      return true;
    }
    case ExprNode::Kind::Call: {
      Vector<std::string> arg_exprs;
      Vector<Type> arg_types;
      for (const std::unique_ptr<ExprNode> &arg_node : node.args) {
        std::string expr;
        Type type;
        if (!emit_node(*arg_node, input_types, expr, type, r_error)) {
          return false;
        }
        arg_exprs.append(std::move(expr));
        arg_types.append(type);
      }
      const Type type = call_result_type(node.func, arg_types.as_span(), r_error);
      if (!r_error.empty()) {
        return false;
      }
      if (node.func == "atan2") {
        r_expr = "atan(" + emit_cast_arg(arg_exprs[0], arg_types[0], type) + ", " +
                emit_cast_arg(arg_exprs[1], arg_types[1], type) + ")";
      }
      else {
        std::string args;
        for (int i = 0; i < int(arg_exprs.size()); i++) {
          if (i > 0) {
            args += ", ";
          }
          args += emit_cast_arg(arg_exprs[i], arg_types[i], type);
        }
        r_expr = node.func + "(" + args + ")";
      }
      r_type = type;
      return true;
    }
  }
  return false;
}

/** \} */

}  // namespace

/* -------------------------------------------------------------------- */
/** \name Public API
 * \{ */

ParseResult parse_expression(StringRef text, int input_count)
{
  ParseResult result;
  Vector<Token> tokens;
  if (!tokenize(text, tokens, result.error)) {
    return result;
  }
  Parser parser(tokens, input_count);
  result.root = parser.parse(result.error);
  return result;
}

Type infer_type(const ExprNode &root, Span<Type> input_types, std::string &r_error)
{
  return infer(root, input_types, r_error);
}

bool evaluate(const ExprNode &root,
              Span<Value> inputs,
              Value &r_result,
              std::string &r_error)
{
  Vector<Type> input_types;
  for (const Value &value : inputs) {
    input_types.append(value.type);
  }
  infer(root, input_types.as_span(), r_error);
  if (!r_error.empty()) {
    return false;
  }
  r_result = eval_node(root, inputs, r_error);
  return r_error.empty();
}

std::string to_glsl(const ExprNode &root,
                    Type out_type,
                    Span<Type> input_types,
                    std::string &r_error)
{
  std::string expr;
  Type type;
  if (!emit_node(root, input_types, expr, type, r_error)) {
    return {};
  }
  if (type != out_type) {
    expr = glsl_cast(expr, type, out_type);
  }
  return expr;
}

bool convert(Value &value, Type target, std::string &r_error)
{
  if (value.type == target) {
    return true;
  }
  const Value src = value;
  Value result;
  result.type = target;
  switch (target) {
    case Type::Float:
      result.s = component_get(src, 0);
      break;
    case Type::Int:
      result.s = std::trunc(component_get(src, 0));
      break;
    case Type::Vec3: {
      if (is_scalar_type(src.type)) {
        const float x = component_get(src, 0);
        result.v3 = float3(x, x, x);
      }
      else if (src.type == Type::Vec4) {
        result.v3 = float3(src.v4.x, src.v4.y, src.v4.z);
      }
      else if (src.type == Type::Mat4) {
        result.v3 = float3(src.m4[0].x, src.m4[0].y, src.m4[0].z);
      }
      break;
    }
    case Type::Vec4: {
      if (is_scalar_type(src.type)) {
        const float x = component_get(src, 0);
        result.v4 = float4(x, x, x, x);
      }
      else if (src.type == Type::Vec3) {
        result.v4 = float4(src.v3.x, src.v3.y, src.v3.z, 0.0f);
      }
      else if (src.type == Type::Mat4) {
        result.v4 = src.m4[0];
      }
      break;
    }
    case Type::Mat4: {
      if (is_scalar_type(src.type)) {
        const float x = component_get(src, 0);
        result.m4 = float4x4(float4(x, 0.0f, 0.0f, 0.0f),
                             float4(0.0f, x, 0.0f, 0.0f),
                             float4(0.0f, 0.0f, x, 0.0f),
                             float4(0.0f, 0.0f, 0.0f, x));
      }
      else if (src.type == Type::Vec3) {
        result.m4 = float4x4(float4(src.v3.x, 0.0f, 0.0f, 0.0f),
                             float4(0.0f, src.v3.y, 0.0f, 0.0f),
                             float4(0.0f, 0.0f, src.v3.z, 0.0f),
                             float4(0.0f, 0.0f, 0.0f, 1.0f));
      }
      else if (src.type == Type::Vec4) {
        result.m4 = float4x4(float4(src.v4.x, 0.0f, 0.0f, 0.0f),
                             float4(0.0f, src.v4.y, 0.0f, 0.0f),
                             float4(0.0f, 0.0f, src.v4.z, 0.0f),
                             float4(0.0f, 0.0f, 0.0f, src.v4.w));
      }
      break;
    }
  }
  value = result;
  (void)r_error;
  return true;
}

/** \} */

}  // namespace blender::nodes::math_expr
