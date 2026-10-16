/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_esky_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.is_function_node();

  b.add_input<decl::Vector>("Direction"_ustr)
      .default_value(float3(0.0f, 0.0f, 1.0f))
      .hide_value()
      .description("World-space view direction. Connect Texture Coordinate.Normal.");
  b.add_input<decl::Float>("Time"_ustr).default_value(0.0f).subtype(PROP_TIME);
  b.add_input<decl::Float>("Sample Index"_ustr).default_value(0.0f).min(0.0f).hide_value();
  b.add_input<decl::Float>("Camera Height (km)"_ustr).default_value(0.002f).min(0.0f).max(100.0f);
  b.add_input<decl::Float>("Planet Radius (km)"_ustr)
      .default_value(6360.0f)
      .min(100.0f)
      .max(100000.0f);
  b.add_input<decl::Float>("Latitude"_ustr).default_value(35.0f).min(-90.0f).max(90.0f).subtype(PROP_ANGLE);
  b.add_input<decl::Float>("Longitude"_ustr).default_value(105.0f).min(-180.0f).max(180.0f).subtype(PROP_ANGLE);
  b.add_input<decl::Float>("Star Density"_ustr).default_value(1.0f).min(0.1f).max(50.0f);
  b.add_input<decl::Float>("Star Brightness"_ustr).default_value(2.0f).min(0.0f).max(50.0f);

  b.add_input<decl::Bool>("Use Sun System"_ustr).default_value(true);
  b.add_input<decl::Float>("Sun Height"_ustr).default_value(45.0f).min(-30.0f).max(120.0f).subtype(PROP_ANGLE);
  b.add_input<decl::Float>("Sun Rotation"_ustr).default_value(0.0f).min(0.0f).max(360.0f).subtype(PROP_ANGLE);
  b.add_input<decl::Float>("Sun Temperature"_ustr).default_value(5778.0f).min(2000.0f).max(30000.0f);
  b.add_input<decl::Float>("Sun Size"_ustr).default_value(1.0f).min(0.1f).max(5.0f);
  b.add_input<decl::Float>("Sun Bloom"_ustr).default_value(1.0f).min(0.0f).max(3.0f);
  b.add_input<decl::Float>("Horizon Height"_ustr).default_value(0.0f).min(-5.0f).max(5.0f).subtype(PROP_ANGLE);

  b.add_input<decl::Bool>("Use Atmosphere"_ustr).default_value(true);
  b.add_input<decl::Float>("Air Density"_ustr).default_value(1.0f).min(0.5f).max(2.0f);
  b.add_input<decl::Float>("Aerosol"_ustr).default_value(0.3f).min(0.0f).max(2.0f);
  b.add_input<decl::Float>("Ozone"_ustr).default_value(1.0f).min(0.3f).max(1.5f);
  b.add_input<decl::Float>("SO2"_ustr).default_value(0.0f).min(0.0f).max(2.0f);
  b.add_input<decl::Float>("PM2.5"_ustr).default_value(0.0f).min(0.0f).max(5.0f);
  b.add_input<decl::Float>("Sun Glow Suppress"_ustr).default_value(0.5f).min(0.0f).max(1.0f);
  b.add_input<decl::Float>("Atmosphere Steps"_ustr).default_value(4.0f).min(1.0f).max(32.0f);

  b.add_input<decl::Float>("Sun Intensity"_ustr).default_value(3.0f).min(0.0f).max(100.0f);
  b.add_input<decl::Color>("Zenith Color"_ustr).default_value(ColorGeometry4f(0.015f, 0.10f, 0.35f, 1.0f));
  b.add_input<decl::Color>("Horizon Color"_ustr).default_value(ColorGeometry4f(0.30f, 0.55f, 0.85f, 1.0f));

  b.add_output<decl::Color>("Color"_ustr);
}

static int node_shader_gpu_esky(GPUMaterial *mat,
                                bNode *node,
                                bNodeExecData * /*execdata*/,
                                GPUNodeStack *in,
                                GPUNodeStack *out)
{
  return GPU_stack_link(mat, node, "node_esky", in, out);
}

}  // namespace nodes::node_shader_esky_cc

void register_node_type_sh_esky()
{
  namespace file_ns = nodes::node_shader_esky_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeESky"_ustr, SH_NODE_ESKY);
  ntype.ui_name = "ESky";
  ntype.ui_description = "Procedural sky, atmosphere, sun and night sky (EEVEE only)";
  ntype.enum_name_legacy = "ESKY";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = world_shader_nodes_poll;
  ntype.gpu_fn = file_ns::node_shader_gpu_esky;

  bke::node_register_type(ntype);
}

}  // namespace blender
