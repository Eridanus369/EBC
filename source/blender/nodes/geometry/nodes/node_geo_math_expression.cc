/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * A native math expression node. The node has a dynamic number of inputs and outputs. Each output
 * stores an expression that is evaluated from the input ports, which are referenced with `{0}`,
 * `{1}`, ... . Expressions are evaluated in C++ for geometry nodes and the compositor, and are
 * translated to GLSL for shader nodes.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "BLI_hash_mm2a.hh"
#include "BLI_listbase.hh"
#include "BLI_math_matrix_types.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_string.hh"
#include "BLI_string_ref.hh"
#include "BLI_vector.hh"

#include "BKE_node.hh"

#include "COM_node_operation.hh"
#include "COM_result.hh"

#include "GPU_material.hh"
#include "GPU_shader.hh"

#include "MEM_guardedalloc.h"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "WM_api.hh"

#include "NOD_math_expression.hh"
#include "NOD_socket_items_ops.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_math_expression_cc {

NODE_STORAGE_FUNCS(NodeMathExpression)

static constexpr int initial_inputs_num = 3;

/* -------------------------------------------------------------------- */
/** \name Helpers
 * \{ */

static const bNodeSocket *socket_at(const bNode &node, const bool output, const int index)
{
  int i = 0;
  if (output) {
    for (const bNodeSocket &socket : node.outputs) {
      if (i == index) {
        return &socket;
      }
      i++;
    }
  }
  else {
    for (const bNodeSocket &socket : node.inputs) {
      if (i == index) {
        return &socket;
      }
      i++;
    }
  }
  return nullptr;
}

static eNodeSocketDatatype socket_type_at(const bNode &node, const bool output, const int index)
{
  const bNodeSocket *socket = socket_at(node, output, index);
  return socket ? socket->type : SOCK_FLOAT;
}

/** Returns the type that can be used in the given node tree, or #SOCK_FLOAT as a fallback. */
static eNodeSocketDatatype safe_socket_type(const short storage_type, const int ntree_type)
{
  const eNodeSocketDatatype type = eNodeSocketDatatype(storage_type);
  switch (type) {
    case SOCK_FLOAT:
    case SOCK_INT:
    case SOCK_VECTOR:
    case SOCK_RGBA:
    case SOCK_MATRIX:
      if (bke::node_tree_type_supports_socket_type_static(ntree_type, type)) {
        return type;
      }
      return SOCK_FLOAT;
    default:
      return SOCK_FLOAT;
  }
}

static math_expr::Type math_expr_type_for_socket(const eNodeSocketDatatype type)
{
  switch (type) {
    case SOCK_INT:
      return math_expr::Type::Int;
    case SOCK_VECTOR:
      return math_expr::Type::Vec3;
    case SOCK_RGBA:
      return math_expr::Type::Vec4;
    case SOCK_MATRIX:
      return math_expr::Type::Mat4;
    case SOCK_FLOAT:
    default:
      return math_expr::Type::Float;
  }
}

static UString socket_identifier(const bool output, const int index)
{
  return UString((output ? "Output_" : "Input_") + std::to_string(index));
}

static UString socket_name(const bool output, const int index)
{
  return UString((output ? "Out " : "In ") + std::to_string(index));
}

static bool expression_references_port(const math_expr::ExprNode &node, const int port)
{
  if (node.kind == math_expr::ExprNode::Kind::Port) {
    return node.port == port;
  }
  for (const std::unique_ptr<math_expr::ExprNode> &arg : node.args) {
    if (arg && expression_references_port(*arg, port)) {
      return true;
    }
  }
  return false;
}

/** Input port type follows the first output port that references it. */
static eNodeSocketDatatype infer_input_type(const NodeMathExpression &storage,
                                            const int port,
                                            const int ntree_type)
{
  for (int j = 0; j < std::clamp(storage.outputs_num, 0, MATH_EXPR_MAX_OUTPUTS); j++) {
    const math_expr::ParseResult parsed = math_expr::parse_expression(
        storage.outputs[j].expression, std::clamp(storage.inputs_num, 0, MATH_EXPR_MAX_INPUTS));
    if (parsed.root && expression_references_port(*parsed.root, port)) {
      return safe_socket_type(storage.outputs[j].socket_type, ntree_type);
    }
  }
  return SOCK_FLOAT;
}

static void parse_and_evaluate(const bNode &node,
                               const int output_index,
                               Span<math_expr::Value> inputs,
                               math_expr::Value &r_value,
                               std::string &r_error)
{
  const NodeMathExpression &storage = node_storage(node);
  const std::string expression = storage.outputs[output_index].expression;
  const math_expr::ParseResult parsed = math_expr::parse_expression(
      expression, storage.inputs_num);
  if (!parsed.root) {
    r_error = parsed.error;
    return;
  }
  if (!math_expr::evaluate(*parsed.root, inputs, r_value, r_error)) {
    return;
  }
  const math_expr::Type target = math_expr_type_for_socket(
      socket_type_at(node, true, output_index));
  math_expr::convert(r_value, target, r_error);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Node declaration
 * \{ */

static void node_declare(NodeDeclarationBuilder &b)
{
  const bNode *node = b.node_or_null();
  const bNodeTree *tree = b.tree_or_null();
  if (!node || !tree || !node->storage) {
    for (int i = 0; i < initial_inputs_num; i++) {
      b.add_input(SOCK_FLOAT, socket_name(false, i), socket_identifier(false, i));
    }
    b.add_output(SOCK_FLOAT, socket_name(true, 0), socket_identifier(true, 0));
    return;
  }

  const NodeMathExpression &storage = node_storage(*node);

  /* Guard against garbage data. */
  int in_num = std::min(storage.inputs_num, MATH_EXPR_MAX_INPUTS);
  int out_num = std::min(storage.outputs_num, MATH_EXPR_MAX_OUTPUTS);

  /* Input socket types follow the output port that references them. */
  for (int i = 0; i < in_num; i++) {
    b.add_input(infer_input_type(storage, i, tree->type),
                socket_name(false, i),
                socket_identifier(false, i));
  }

  for (int i = 0; i < out_num; i++) {
    b.add_output(safe_socket_type(storage.outputs[i].socket_type, tree->type),
                 socket_name(true, i),
                 socket_identifier(true, i));
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Node UI
 * \{ */

static void draw_expression_error(ui::Layout &layout, const bNode &node, const int index)
{
  const NodeMathExpression &storage = node_storage(node);
  std::string error;
  Vector<math_expr::Type> input_types;
  for (int i = 0; i < std::clamp(storage.inputs_num, 0, 16); i++) {
    input_types.append(
        math_expr_type_for_socket(socket_type_at(node, false, i)));
  }
  const math_expr::ParseResult parsed = math_expr::parse_expression(
      storage.outputs[index].expression, storage.inputs_num);
  if (!parsed.root) {
    error = parsed.error;
  }
  else {
    math_expr::infer_type(*parsed.root, input_types.as_span(), error);
  }
  if (!error.empty()) {
    layout.label(std::move(error), ICON_ERROR);
  }
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  bNode &node = *ptr->data_as<bNode>();
  if (node.storage == nullptr) {
    return;
  }
  NodeMathExpression &storage = node_storage(node);


  for (int i = 0; i < std::clamp(storage.outputs_num, 0, 8); i++) {
    ui::Layout &row = layout.row(true);
    row.label(std::string("Out ") + std::to_string(i), ICON_NONE);
    PointerRNA type_ptr = RNA_pointer_create_discrete(
        ptr->owner_id, RNA_GeometryNodeMathExpressionItem, &storage.outputs[i]);
    row.prop(&type_ptr, "socket_type", UI_ITEM_NONE, std::nullopt, ICON_NONE);

    PointerRNA expr_ptr = RNA_pointer_create_discrete(
        ptr->owner_id, RNA_GeometryNodeMathExpressionItem, &storage.outputs[i]);
    layout.prop(&expr_ptr, "expression", ui::ITEM_R_SPLIT_EMPTY_NAME, std::nullopt, ICON_NONE);
    draw_expression_error(layout, node, i);
  }

  ui::Layout &buttons = layout.row(true);
  PointerRNA op_ptr = buttons.op("NODE_OT_math_expression_add_input", "", ICON_ADD);
  RNA_int_set(&op_ptr, "node_identifier", node.identifier);
  if (storage.inputs_num > initial_inputs_num) {
    op_ptr = buttons.op("NODE_OT_math_expression_remove_input", "", ICON_REMOVE);
    RNA_int_set(&op_ptr, "node_identifier", node.identifier);
  }
  PointerRNA out_op_ptr = buttons.op("NODE_OT_math_expression_add_output", "", ICON_ADD);
  RNA_int_set(&out_op_ptr, "node_identifier", node.identifier);
  if (storage.outputs_num > 1) {
    out_op_ptr = buttons.op("NODE_OT_math_expression_remove_output", "", ICON_REMOVE);
    RNA_int_set(&out_op_ptr, "node_identifier", node.identifier);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Operators
 * \{ */

static wmOperatorStatus math_expression_add_input_exec(bContext *C, wmOperator *op)
{
  PointerRNA node_ptr = socket_items::ops::get_active_node_to_operate_on(
      C, op, "GeometryNodeMathExpression");
  if (!node_ptr) {
    return OPERATOR_CANCELLED;
  }
  bNode &node = *static_cast<bNode *>(node_ptr.data);
  NodeMathExpression &storage = node_storage(node);
  if (storage.inputs_num >= MATH_EXPR_MAX_INPUTS) {
    return OPERATOR_CANCELLED;
  }
  storage.inputs[storage.inputs_num] = NodeMathExpressionItem{};
  storage.inputs_num++;
  socket_items::ops::update_after_node_change(C, node_ptr);
  return OPERATOR_FINISHED;
}

static wmOperatorStatus math_expression_remove_input_exec(bContext *C, wmOperator *op)
{
  PointerRNA node_ptr = socket_items::ops::get_active_node_to_operate_on(
      C, op, "GeometryNodeMathExpression");
  if (!node_ptr) {
    return OPERATOR_CANCELLED;
  }
  bNode &node = *static_cast<bNode *>(node_ptr.data);
  NodeMathExpression &storage = node_storage(node);
  if (storage.inputs_num <= initial_inputs_num) {
    return OPERATOR_CANCELLED;
  }
  storage.inputs_num--;
  socket_items::ops::update_after_node_change(C, node_ptr);
  return OPERATOR_FINISHED;
}

static wmOperatorStatus math_expression_add_output_exec(bContext *C, wmOperator *op)
{
  PointerRNA node_ptr = socket_items::ops::get_active_node_to_operate_on(
      C, op, "GeometryNodeMathExpression");
  if (!node_ptr) {
    return OPERATOR_CANCELLED;
  }
  bNode &node = *static_cast<bNode *>(node_ptr.data);
  NodeMathExpression &storage = node_storage(node);
  if (storage.outputs_num >= MATH_EXPR_MAX_OUTPUTS) {
    return OPERATOR_CANCELLED;
  }
  NodeMathExpressionItem &item = storage.outputs[storage.outputs_num];
  item = NodeMathExpressionItem{};
  STRNCPY(item.expression, "{0}");
  storage.outputs_num++;
  socket_items::ops::update_after_node_change(C, node_ptr);
  return OPERATOR_FINISHED;
}

static wmOperatorStatus math_expression_remove_output_exec(bContext *C, wmOperator *op)
{
  PointerRNA node_ptr = socket_items::ops::get_active_node_to_operate_on(
      C, op, "GeometryNodeMathExpression");
  if (!node_ptr) {
    return OPERATOR_CANCELLED;
  }
  bNode &node = *static_cast<bNode *>(node_ptr.data);
  NodeMathExpression &storage = node_storage(node);
  if (storage.outputs_num <= 1) {
    return OPERATOR_CANCELLED;
  }
  storage.outputs_num--;
  socket_items::ops::update_after_node_change(C, node_ptr);
  return OPERATOR_FINISHED;
}

template<typename ExecFn>
static void register_node_operator(wmOperatorType *ot,
                                   const char *name,
                                   const char *idname,
                                   const char *description,
                                   ExecFn exec)
{
  ot->name = name;
  ot->idname = idname;
  ot->description = description;
  ot->poll = socket_items::ops::editable_node_active_poll<int>;
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
  ot->exec = exec;
  socket_items::ops::add_node_identifier_property(ot);
}

static void math_expression_add_input(wmOperatorType *ot)
{
  register_node_operator(
      ot, "Add Input", "NODE_OT_math_expression_add_input", "Add an input port", math_expression_add_input_exec);
}

static void math_expression_remove_input(wmOperatorType *ot)
{
  register_node_operator(ot,
                         "Remove Input",
                         "NODE_OT_math_expression_remove_input",
                         "Remove the last input port",
                         math_expression_remove_input_exec);
}

static void math_expression_add_output(wmOperatorType *ot)
{
  register_node_operator(ot,
                         "Add Output",
                         "NODE_OT_math_expression_add_output",
                         "Add an output port",
                         math_expression_add_output_exec);
}

static void math_expression_remove_output(wmOperatorType *ot)
{
  register_node_operator(ot,
                         "Remove Output",
                         "NODE_OT_math_expression_remove_output",
                         "Remove the last output port",
                         math_expression_remove_output_exec);
}

static void node_operators()
{
  WM_operatortype_append(math_expression_add_input);
  WM_operatortype_append(math_expression_remove_input);
  WM_operatortype_append(math_expression_add_output);
  WM_operatortype_append(math_expression_remove_output);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Geometry execution
 * \{ */

static math_expr::Value read_geometry_input(GeoNodeExecParams &params,
                                            const int index,
                                            const eNodeSocketDatatype type)
{
  math_expr::Value value;
  value.type = math_expr_type_for_socket(type);
  const UString identifier = socket_identifier(false, index);
  switch (value.type) {
    case math_expr::Type::Float:
      value.s = params.extract_input<float>(identifier);
      break;
    case math_expr::Type::Int:
      value.s = float(params.extract_input<int>(identifier));
      break;
    case math_expr::Type::Vec3:
      value.v3 = params.extract_input<float3>(identifier);
      break;
    case math_expr::Type::Vec4:
      value.v4 = params.extract_input<float4>(identifier);
      break;
    case math_expr::Type::Mat4:
      value.m4 = params.extract_input<float4x4>(identifier);
      break;
  }
  return value;
}

static void set_geometry_output(GeoNodeExecParams &params,
                                const int index,
                                const eNodeSocketDatatype type,
                                const math_expr::Value &value)
{
  const UString identifier = socket_identifier(true, index);
  switch (type) {
    case SOCK_INT:
      params.set_output(identifier, int(value.s));
      break;
    case SOCK_VECTOR:
      params.set_output(identifier, value.v3);
      break;
    case SOCK_RGBA:
      params.set_output(identifier, value.v4);
      break;
    case SOCK_MATRIX:
      params.set_output(identifier, value.m4);
      break;
    case SOCK_FLOAT:
    default:
      params.set_output(identifier, value.s);
      break;
  }
}

static void node_geo_exec(GeoNodeExecParams params)
{
  const bNode &node = params.node();
  if (node.storage == nullptr) {
    params.set_default_remaining_outputs();
    return;
  }
  const NodeMathExpression &storage = node_storage(node);

  Vector<math_expr::Value> input_values;
  for (int i = 0; i < std::clamp(storage.inputs_num, 0, 16); i++) {
    input_values.append(read_geometry_input(
        params, i, socket_type_at(node, false, i)));
  }

  std::string error;
  for (int i = 0; i < std::clamp(storage.outputs_num, 0, 8); i++) {
    math_expr::Value value;
    parse_and_evaluate(node, i, input_values.as_span(), value, error);
    if (!error.empty()) {
      params.error_message_add(NodeWarningType::Error,
                               "Output " + std::to_string(i) + ": " + error);
      error.clear();
      continue;
    }
    set_geometry_output(params, i, socket_type_at(node, true, i), value);
  }
  params.set_default_remaining_outputs();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Compositor execution
 * \{ */

static math_expr::Value read_compositor_input(const compositor::Result &input,
                                              const eNodeSocketDatatype type)
{
  math_expr::Value value;
  value.type = math_expr_type_for_socket(type);
  switch (value.type) {
    case math_expr::Type::Float:
      value.s = input.get_single_value_default<float>();
      break;
    case math_expr::Type::Int:
      value.s = float(input.get_single_value_default<int32_t>());
      break;
    case math_expr::Type::Vec3:
      value.v3 = input.get_single_value_default<float3>();
      break;
    case math_expr::Type::Vec4: {
      const compositor::Color color = input.get_single_value_default<compositor::Color>();
      value.v4 = float4(color.r, color.g, color.b, color.a);
      break;
    }
    case math_expr::Type::Mat4:
      value.m4 = input.get_single_value_default<float4x4>();
      break;
  }
  return value;
}

static void set_compositor_output(compositor::Result &output,
                                  const eNodeSocketDatatype type,
                                  const math_expr::Value &value)
{
  output.allocate_single_value();
  switch (type) {
    case SOCK_INT:
      output.set_single_value(int32_t(value.s));
      break;
    case SOCK_VECTOR:
      output.set_single_value(value.v3);
      break;
    case SOCK_RGBA:
      output.set_single_value(compositor::Color(value.v4.x, value.v4.y, value.v4.z, value.v4.w));
      break;
    case SOCK_MATRIX:
      output.set_single_value(value.m4);
      break;
    case SOCK_FLOAT:
    default:
      output.set_single_value(value.s);
      break;
  }
}

class MathExpressionOperation : public compositor::NodeOperation {
 public:
  using NodeOperation::NodeOperation;

  void execute() override
  {
    const bNode &node = this->node();
    if (node.storage == nullptr) {
      this->allocate_default_remaining_outputs();
      return;
    }
    const NodeMathExpression &storage = node_storage(node);

    Vector<math_expr::Value> input_values;
    for (int i = 0; i < std::clamp(storage.inputs_num, 0, 16); i++) {
      const compositor::Result &input = this->get_input(socket_identifier(false, i).ref());
      input_values.append(read_compositor_input(input, socket_type_at(node, false, i)));
    }

    for (int i = 0; i < std::clamp(storage.outputs_num, 0, 8); i++) {
      compositor::Result &output = this->get_result(socket_identifier(true, i).ref());
      if (!output.should_compute()) {
        continue;
      }
      math_expr::Value value;
      std::string error;
      parse_and_evaluate(node, i, input_values.as_span(), value, error);
      if (!error.empty()) {
        this->add_warning(NodeWarningType::Error,
                          "Output " + std::to_string(i) + ": " + error);
        output.allocate_invalid();
        continue;
      }
      set_compositor_output(output, socket_type_at(node, true, i), value);
    }
    this->allocate_default_remaining_outputs();
  }
};

static compositor::NodeOperation *get_compositor_operation(compositor::Context &context,
                                                          const bNode &node)
{
  return new MathExpressionOperation(context, node);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shader execution
 * \{ */

static const char *glsl_type_name(const eNodeSocketDatatype type)
{
  switch (type) {
    case SOCK_INT:
      return "int";
    case SOCK_VECTOR:
      return "vec3";
    case SOCK_RGBA:
      return "vec4";
    case SOCK_MATRIX:
      return "mat4";
    case SOCK_FLOAT:
    default:
      return "float";
  }
}

static GPUType gpu_type_for_socket(const eNodeSocketDatatype type)
{
  switch (type) {
    case SOCK_INT:
      return GPU_INT;
    case SOCK_VECTOR:
      return GPU_VEC3;
    case SOCK_RGBA:
      return GPU_VEC4;
    case SOCK_MATRIX:
      return GPU_MAT4;
    case SOCK_FLOAT:
    default:
      return GPU_FLOAT;
  }
}

static const char *glsl_zero_literal(const eNodeSocketDatatype type)
{
  switch (type) {
    case SOCK_INT:
      return "0";
    case SOCK_VECTOR:
      return "vec3(0.0)";
    case SOCK_RGBA:
      return "vec4(0.0)";
    case SOCK_MATRIX:
      return "mat4(0.0)";
    case SOCK_FLOAT:
    default:
      return "0.0";
  }
}

static std::string source_hash_suffix(const bNode &node)
{
  const NodeMathExpression &storage = node_storage(node);
  BLI_HashMurmur2A hash;
  BLI_hash_mm2a_init(&hash, 0);
  BLI_hash_mm2a_add_int(&hash, node.identifier);
  BLI_hash_mm2a_add_int(&hash, storage.inputs_num);
  BLI_hash_mm2a_add_int(&hash, storage.outputs_num);
  for (int i = 0; i < std::clamp(storage.outputs_num, 0, MATH_EXPR_MAX_OUTPUTS); i++) {
    BLI_hash_mm2a_add_int(&hash, storage.outputs[i].socket_type);
    BLI_hash_mm2a_add(&hash,
                      reinterpret_cast<const unsigned char *>(storage.outputs[i].expression),
                      strlen(storage.outputs[i].expression));
  }
  return std::to_string(BLI_hash_mm2a_end(&hash));
}

static std::string build_wrapper_source(const bNode &node, const StringRefNull function_name)
{
  const NodeMathExpression &storage = node_storage(node);
  std::stringstream source;
  source << "void " << function_name << "(";
  bool first_argument = true;
  for (int i = 0; i < std::clamp(storage.inputs_num, 0, 16); i++) {
    if (!first_argument) {
      source << ", ";
    }
    first_argument = false;
    source << glsl_type_name(socket_type_at(node, false, i)) << " v" << i;
  }
  for (int i = 0; i < std::clamp(storage.outputs_num, 0, 8); i++) {
    if (!first_argument) {
      source << ", ";
    }
    first_argument = false;
    source << "out " << glsl_type_name(socket_type_at(node, true, i)) << " o" << i;
  }
  source << ")\n{\n";

  Vector<math_expr::Type> input_types;
  for (int i = 0; i < std::clamp(storage.inputs_num, 0, 16); i++) {
    input_types.append(math_expr_type_for_socket(socket_type_at(node, false, i)));
  }

  for (int i = 0; i < std::clamp(storage.outputs_num, 0, 8); i++) {
    const eNodeSocketDatatype output_type = socket_type_at(node, true, i);
    std::string error;
    std::string glsl;
    const math_expr::ParseResult parsed = math_expr::parse_expression(
        storage.outputs[i].expression, storage.inputs_num);
    if (!parsed.root) {
      error = parsed.error;
    }
    else {
      glsl = math_expr::to_glsl(*parsed.root,
                                math_expr_type_for_socket(output_type),
                                input_types.as_span(),
                                error);
    }
    if (error.empty()) {
      source << "  o" << i << " = " << glsl << ";\n";
    }
    else {
      source << "  o" << i << " = " << glsl_zero_literal(output_type) << ";\n";
    }
  }
  source << "}\n";
  return source.str();
}

static void set_gpu_output_constant(GPUNodeStack *out, const int index, const eNodeSocketDatatype type)
{
  static float zero_float[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  static int zero_int[4] = {0, 0, 0, 0};
  static float zero_matrix[16] = {0.0f};
  if (out == nullptr) {
    return;
  }
  out[index].type = gpu_type_for_socket(type);
  switch (type) {
    case SOCK_INT:
      out[index].link = GPU_constant(zero_int);
      break;
    case SOCK_MATRIX:
      out[index].link = GPU_constant(zero_matrix);
      break;
    default:
      out[index].link = GPU_constant(zero_float);
      break;
  }
}

static int node_shader_gpu(GPUMaterial *mat,
                           bNode *node,
                           bNodeExecData * /*execdata*/,
                           GPUNodeStack *in,
                           GPUNodeStack *out)
{
  if (node->storage == nullptr) {
    set_gpu_output_constant(out, 0, SOCK_FLOAT);
    return true;
  }
  const NodeMathExpression &storage = node_storage(*node);
  for (int i = 0; i < std::clamp(storage.inputs_num, 0, 16); i++) {
    in[i].type = gpu_type_for_socket(socket_type_at(*node, false, i));
  }
  for (int i = 0; i < std::clamp(storage.outputs_num, 0, 8); i++) {
    out[i].type = gpu_type_for_socket(socket_type_at(*node, true, i));
  }

  const std::string hash = source_hash_suffix(*node);
  const std::string function_name = "math_expr_" + std::to_string(node->identifier) + "_" + hash;
  const std::string filename = function_name + ".glsl";

  const std::string source = build_wrapper_source(*node, function_name.c_str());

  GPU_material_generated_source_add(mat, filename.c_str(), {}, source.c_str());
  return GPU_stack_link_custom(mat,
                               node,
                               function_name.c_str(),
                               filename.c_str(),
                               GPU_CUSTOM_NODE_DEPENDENCY_NONE,
                               in,
                               out);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Registration
 * \{ */

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  NodeMathExpression *storage = MEM_new<NodeMathExpression>(__func__);
  storage->inputs_num = initial_inputs_num;
  storage->outputs_num = 1;
  for (int i = 0; i < storage->outputs_num; i++) {
    storage->outputs[i] = NodeMathExpressionItem{};
    STRNCPY(storage->outputs[i].expression, "{0}");
    storage->outputs[i].socket_type = SOCK_FLOAT;
  }
  node->storage = storage;
  node->flag |= NODE_OPTIONS;
}

static void node_update(bNodeTree *ntree, bNode *node)
{
  if (node->storage == nullptr) {
    return;
  }
  NodeMathExpression &storage = node_storage(*node);
  storage.inputs_num = std::max(initial_inputs_num,
                                std::min(storage.inputs_num, MATH_EXPR_MAX_INPUTS));
  storage.outputs_num = std::max(1, std::min(storage.outputs_num, MATH_EXPR_MAX_OUTPUTS));
  for (int i = 0; i < std::clamp(storage.outputs_num, 0, MATH_EXPR_MAX_OUTPUTS); i++) {
    storage.outputs[i].socket_type = safe_socket_type(storage.outputs[i].socket_type, ntree->type);
  }
}

static void node_free_storage(bNode *node)
{
  if (node->storage == nullptr) {
    return;
  }
  MEM_delete(static_cast<NodeMathExpression *>(node->storage));
  node->storage = nullptr;
}

static void node_copy_storage(bNodeTree * /*dst_tree*/, bNode *dst_node, const bNode *src_node)
{
  if (src_node->storage == nullptr) {
    dst_node->storage = nullptr;
    return;
  }
  const NodeMathExpression &src_storage = node_storage(*src_node);
  dst_node->storage = MEM_new<NodeMathExpression>(__func__, dna::shallow_copy(src_storage));
}

static bool node_poll(const bke::bNodeType * /*ntype*/,
                      const bNodeTree *ntree,
                      const char **r_disabled_hint)
{
  if (!STR_ELEM(ntree->idname, "GeometryNodeTree", "ShaderNodeTree", "CompositorNodeTree")) {
    *r_disabled_hint = "Not a geometry, shader or compositor node tree";
    return false;
  }
  return true;
}

static void node_register()
{
  static bke::bNodeType ntype;
  bke::node_type_base(ntype, "GeometryNodeMathExpression"_ustr, std::nullopt);
  ntype.ui_name = "Math Expression";
  ntype.ui_description = "Evaluate math expressions using numbered input ports";
  ntype.enum_name_legacy = "MATH_EXPRESSION";
  ntype.nclass = NODE_CLASS_CONVERTER;
  ntype.poll = node_poll;
  ntype.insert_link = node_insert_link_default;
  ntype.declare = node_declare;
  ntype.draw_buttons = node_layout;
  ntype.initfunc = node_init;
  ntype.updatefunc = node_update;
  ntype.geometry_node_execute = node_geo_exec;
  ntype.get_compositor_operation = get_compositor_operation;
  ntype.gpu_fn = node_shader_gpu;
  ntype.register_operators = node_operators;
  ntype.default_width = bke::NodeWidth::_240;
  ntype.minwidth = bke::NodeWidth::_160;
  bke::node_type_storage(
      ntype, "NodeMathExpression", node_free_storage, node_copy_storage);
  bke::node_register_type(ntype);
}

NOD_REGISTER_NODE(node_register)

/** \} */

}  // namespace blender::nodes::node_geo_math_expression_cc
