/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * NPR: Filter Object Mask node for the Eevee filter material domain. Outputs a per-pixel
 * mask of a single object or all geometry objects of a collection, using the object
 * cryptomatte layer. */

#include "BKE_collection.hh"
#include "BKE_object.hh"

#include "DNA_collection_types.h"
#include "DNA_object_types.h"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_filter_object_mask_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Object>("Object"_ustr);
  b.add_input<decl::Collection>("Collection"_ustr);
  b.add_output<decl::Float>("Mask"_ustr);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  node->custom1 = SHD_FILTER_MASK_SINGLE_OBJECT;
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "mode", ui::ITEM_R_SPLIT_EMPTY_NAME, std::nullopt, ICON_NONE);
}

static int node_shader_gpu_object_mask(GPUMaterial *mat,
                                       bNode *node,
                                       bNodeExecData * /*execdata*/,
                                       GPUNodeStack * /*in*/,
                                       GPUNodeStack *out)
{
  /* Objects are stored in a de-duplicated global buffer. Each mask node covers the
   * contiguous buffer range spanning the objects it selected; selecting an object already
   * registered by another node may include the objects in between. */
  int start_index = -1;
  int end_index = -1;

  auto append_object = [&](Object *object) {
    if (object == nullptr || !OB_TYPE_IS_GEOMETRY(object->type)) {
      return;
    }
    const int index = GPU_material_filter_mask_object_append(mat, object);
    if (index < 0) {
      return;
    }
    if (start_index < 0) {
      start_index = end_index = index;
    }
    else {
      start_index = min_ii(start_index, index);
      end_index = max_ii(end_index, index);
    }
  };

  if (node->custom1 == SHD_FILTER_MASK_COLLECTION) {
    const bNodeSocket *collection_socket = bke::node_find_socket(
        *node, SOCK_IN, "Collection"_ustr);
    Collection *collection = collection_socket ?
                                 static_cast<Collection *>(collection_socket->default_value) :
                                 nullptr;
    if (collection != nullptr) {
      FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, object) {
        append_object(object);
      }
      FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
    }
  }
  else {
    const bNodeSocket *object_socket = bke::node_find_socket(*node, SOCK_IN, "Object"_ustr);
    Object *object = object_socket ? static_cast<Object *>(object_socket->default_value) : nullptr;
    append_object(object);
  }

  float start_f = float(start_index);
  float count_f = float(start_index >= 0 ? end_index - start_index + 1 : 0);
  GPUNodeLink *start_link = GPU_constant(&start_f);
  GPUNodeLink *count_link = GPU_constant(&count_f);

  GPUNodeLink *mask = nullptr;
  GPU_link(mat, "node_filter_object_mask", start_link, count_link, &mask);
  out[0].link = mask;
  return true;
}

}  // namespace nodes::node_shader_filter_object_mask_cc

void register_node_type_sh_filter_object_mask()
{
  namespace file_ns = nodes::node_shader_filter_object_mask_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeFilterObjectMask"_ustr, SH_NODE_FILTER_OBJECT_MASK);
  ntype.ui_name = "Filter Object Mask";
  ntype.ui_description = "Per-pixel mask of an object or collection in a filter material";
  ntype.enum_name_legacy = "FILTER_OBJECT_MASK";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.initfunc = file_ns::node_init;
  ntype.draw_buttons = file_ns::node_layout;
  ntype.gpu_fn = file_ns::node_shader_gpu_object_mask;
  ntype.add_ui_poll = filter_eevee_shader_nodes_poll;

  bke::node_register_type(ntype);
}

}  // namespace blender
