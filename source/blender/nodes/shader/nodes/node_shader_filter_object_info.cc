/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * NPR: Filter Object Info node for the Eevee filter material domain. Exposes the world
 * transform and object color of a scene object, as filled by the FilterMaterial module
 * into the filter object info uniform buffer. */

#include "DNA_object_types.h"

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_filter_object_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Object>("Object"_ustr);
  b.add_output<decl::Vector>("Location"_ustr);
  b.add_output<decl::Vector>("Rotation"_ustr);
  b.add_output<decl::Vector>("Scale"_ustr);
  b.add_output<decl::Color>("Color"_ustr);
}

static int node_shader_gpu_object_info(GPUMaterial *mat,
                                       bNode *node,
                                       bNodeExecData * /*execdata*/,
                                       GPUNodeStack * /*in*/,
                                       GPUNodeStack *out)
{
  Object *object = nullptr;
  const bNodeSocket *object_socket = bke::node_find_socket(*node, SOCK_IN, "Object"_ustr);
  if (object_socket != nullptr) {
    object = static_cast<Object *>(object_socket->default_value);
  }

  int index = -1;
  if (object != nullptr) {
    index = GPU_material_filter_object_info_ensure(mat, object);
  }
  float index_f = float(index);
  GPUNodeLink *index_link = GPU_constant(&index_f);

  GPUNodeLink *location = nullptr;
  GPUNodeLink *rotation = nullptr;
  GPUNodeLink *scale = nullptr;
  GPUNodeLink *color = nullptr;
  GPU_link(mat,
           "node_filter_object_info",
           index_link,
           &location,
           &rotation,
           &scale,
           &color);

  out[0].link = location;
  out[1].link = rotation;
  out[2].link = scale;
  out[3].link = color;
  return true;
}

}  // namespace nodes::node_shader_filter_object_info_cc

void register_node_type_sh_filter_object_info()
{
  namespace file_ns = nodes::node_shader_filter_object_info_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeFilterObjectInfo"_ustr, SH_NODE_FILTER_OBJECT_INFO);
  ntype.ui_name = "Filter Object Info";
  ntype.ui_description = "Transform and color of an object inside a filter material";
  ntype.enum_name_legacy = "FILTER_OBJECT_INFO";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_shader_gpu_object_info;
  ntype.add_ui_poll = filter_eevee_shader_nodes_poll;

  bke::node_register_type(ntype);
}

}  // namespace blender
