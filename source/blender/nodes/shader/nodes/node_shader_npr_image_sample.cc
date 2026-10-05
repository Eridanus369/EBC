/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * NPR: Image Sample node for the Eevee filter material domain. Samples an image handle
 * (TextureHandle) produced by Pass Input or Scene Color nodes, either at the current
 * pixel or at an explicit UV. */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_npr_image_sample_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Image>("Image"_ustr, "Image"_ustr);
  b.add_input<decl::Vector>("UV"_ustr).hide_value();
  b.add_output<decl::Color>("Color"_ustr);
  b.add_output<decl::Float>("Alpha"_ustr);
}

static int node_shader_gpu_image_sample(GPUMaterial *mat,
                                        bNode *node,
                                        bNodeExecData * /*execdata*/,
                                        GPUNodeStack *in,
                                        GPUNodeStack *out)
{
  /* Image sockets do not use the bNodeStack system. Resolve the handle from the origin
   * socket manually. */
  bNodeSocket *image_socket = bke::node_find_socket(*node, SOCK_IN, "Image"_ustr);
  const bNodeLink *origin_link = node_shader_filter_image_origin_link(image_socket);
  GPUNodeLink *image = node_shader_gpu_filter_image_handle(mat, origin_link);
  if (image == nullptr) {
    /* Unconnected or unsupported source: default handle gives black/opaque. */
    GPU_link(mat, "node_filter_image_default", &image);
  }

  float zero[3] = {0.0f, 0.0f, 0.0f};
  GPUNodeLink *uv_link = in[1].link ? in[1].link : GPU_constant(zero);
  bool use_uv = in[1].link != nullptr;
  GPUNodeLink *use_uv_link = GPU_constant(&use_uv);

  GPUNodeLink *color = nullptr;
  GPUNodeLink *alpha = nullptr;
  GPU_link(mat, "node_npr_image_sample", image, uv_link, use_uv_link, &color, &alpha);
  out[0].link = color;
  out[1].link = alpha;
  return true;
}

}  // namespace nodes::node_shader_npr_image_sample_cc

void register_node_type_sh_npr_image_sample()
{
  namespace file_ns = nodes::node_shader_npr_image_sample_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeNPRImageSample"_ustr, SH_NODE_NPR_IMAGE_SAMPLE);
  ntype.ui_name = "Image Sample";
  ntype.ui_description = "Sample an image from the filter graph";
  ntype.enum_name_legacy = "NPR_IMAGE_SAMPLE";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_shader_gpu_image_sample;
  ntype.add_ui_poll = filter_eevee_shader_nodes_poll;

  bke::node_register_type(ntype);
}

}  // namespace blender
