/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Stage-1 skeleton of the NPR GLSL Function node.
 * See docs/plans/glsl-function-port-plan.md for the full port plan.
 */

#include "node_shader_util.hh"
#include "node_util.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {

namespace nodes::node_shader_glsl_function_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  /* Stage-1 skeleton: static socket layout. Real sockets will be derived
   * from the parsed GLSL function signature in a later porting stage. */
  b.is_function_node();
  b.add_input<decl::Float>("Value"_ustr).default_value(0.0f);
  b.add_output<decl::Float>("Value"_ustr);
}

static void node_init(bNodeTree * /*ntree*/, bNode *node)
{
  node->storage = MEM_new<NodeShaderGLSLFunction>("NodeShaderGLSLFunction");
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA * /*ptr*/)
{
  layout.label("GLSL Function (stage-1 skeleton, not yet functional)", ICON_INFO);
}

static int node_shader_gpu_glsl_function(GPUMaterial * /*mat*/,
                                         bNode * /*node*/,
                                         bNodeExecData * /*execdata*/,
                                         GPUNodeStack * /*in*/,
                                         GPUNodeStack * /*out*/)
{
  /* Stage-1 skeleton: no GPU implementation yet. Returning 0 makes the
   * link fail cleanly instead of feeding wrong data downstream. */
  return 0;
}

}  // namespace nodes::node_shader_glsl_function_cc

void register_node_type_sh_glsl_function()
{
  namespace file_ns = nodes::node_shader_glsl_function_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeGLSLFunction"_ustr, SH_NODE_GLSL_FUNCTION);
  ntype.ui_name = "GLSL Function";
  ntype.ui_description =
      "Evaluate a user-authored GLSL function on the GPU.\n"
      "(stage-1 skeleton, not yet functional)";
  ntype.enum_name_legacy = "GLSL_FUNCTION";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_layout;
  ntype.initfunc = file_ns::node_init;
  ntype.gpu_fn = file_ns::node_shader_gpu_glsl_function;
  bke::node_type_storage(
      ntype, "NodeShaderGLSLFunction", node_free_standard_storage, node_copy_standard_storage);

  bke::node_register_type(ntype);
}

}  // namespace blender
