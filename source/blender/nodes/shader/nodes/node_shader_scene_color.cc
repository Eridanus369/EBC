/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * NPR: Scene Color node for the Eevee filter material domain. Exposes the scene color,
 * depth, normal and position sources as image handles (TextureHandle in generated GLSL),
 * plus legacy Color/Alpha value outputs for the selected source. */

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_scene_color_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("UV"_ustr).hide_value();
  b.add_output<decl::Color>("Color"_ustr);
  b.add_output<decl::Float>("Alpha"_ustr);
  b.add_output<decl::Image>("Depth Image"_ustr, "Depth Image"_ustr);
  b.add_output<decl::Image>("Normal Image"_ustr, "Normal Image"_ustr);
  b.add_output<decl::Image>("Position Image"_ustr, "Position Image"_ustr);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  node->custom1 = SHD_SCENE_SOURCE_COLOR;
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "source", ui::ITEM_R_SPLIT_EMPTY_NAME, std::nullopt, ICON_NONE);
}

static int node_shader_gpu_scene_color(GPUMaterial *mat,
                                       bNode *node,
                                       bNodeExecData * /*execdata*/,
                                       GPUNodeStack *in,
                                       GPUNodeStack *out)
{
  /* Source enum maps to scene texture indices: color=0, depth=1, normal=2, position=4
   * (3 is reserved). */
  float source = 0.0f;
  switch (node->custom1) {
    case SHD_SCENE_SOURCE_DEPTH:
      source = 1.0f;
      break;
    case SHD_SCENE_SOURCE_NORMAL:
      source = 2.0f;
      break;
    case SHD_SCENE_SOURCE_POSITION:
      source = 3.0f;
      break;
    case SHD_SCENE_SOURCE_COLOR:
    default:
      source = 0.0f;
      break;
  }

  float zero[3] = {0.0f, 0.0f, 0.0f};
  GPUNodeLink *source_link = GPU_constant(&source);
  GPUNodeLink *uv_link = in[0].link ? in[0].link : GPU_constant(zero);
  int use_uv = in[0].link ? 1 : 0;
  GPUNodeLink *use_uv_link = GPU_constant(&use_uv);

  GPUNodeLink *color = nullptr;
  GPUNodeLink *alpha = nullptr;
  GPU_link(mat, "node_scene_color", source_link, uv_link, use_uv_link, &color, &alpha);
  out[0].link = color;
  out[1].link = alpha;
  return true;
}

}  // namespace nodes::node_shader_scene_color_cc

void register_node_type_sh_scene_color()
{
  namespace file_ns = nodes::node_shader_scene_color_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeSceneColor"_ustr, SH_NODE_SCENE_COLOR);
  ntype.ui_name = "Scene Color";
  ntype.ui_description = "Sample scene color, depth, normal or position in a filter material";
  ntype.enum_name_legacy = "SCENE_COLOR";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.initfunc = file_ns::node_init;
  ntype.draw_buttons = file_ns::node_layout;
  ntype.gpu_fn = file_ns::node_shader_gpu_scene_color;
  ntype.add_ui_poll = filter_eevee_shader_nodes_poll;

  bke::node_register_type(ntype);
}

}  // namespace blender
