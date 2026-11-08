/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief Native C++ `PhysNodeTree` registration.
 *
 * Registers the tree type at the same level as `GeometryNodeTree`, so the
 * Node Editor can create and open physics trees without any addon. The node
 * types themselves are registered by `register_node_type_*`; this file only
 * owns the tree type, its socket policy and the context lookup.
 */

#include "MEM_guardedalloc.h"

#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_object.hh"

#include "BLI_listbase_iterator.hh"
#include "BLI_string_ref.hh"

#include "BLT_translation.hh"

#include "DNA_ID.h"
#include "DNA_modifier_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_space_types.h"

#include "NOD_geometry.hh"

#include "RNA_prototypes.hh"

#include "UI_resources.hh"

namespace blender {

bke::bNodeTreeType *ntreeType_Phys;

/**
 * Open the tree referenced by the active object's physics modifier. This
 * mirrors `geometry_node_tree_get_from_context`: the tree is looked up by
 * name, never by pointer, and the object becomes the ID shown in the
 * Node Editor header.
 */
static void phys_node_tree_get_from_context(const bContext *C,
                                            bke::bNodeTreeType * /*treetype*/,
                                            bNodeTree **r_ntree,
                                            ID **r_id,
                                            ID **r_from)
{
  Main *bmain = CTX_data_main(C);
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(*bmain, scene, view_layer);
  Object *object = BKE_view_layer_active_object_get(view_layer);
  if (object == nullptr) {
    return;
  }

  const PhysNodesModifierData *pmd = nullptr;
  for (const ModifierData &md : object->modifiers) {
    if (md.type == eModifierType_PhysNodes) {
      pmd = reinterpret_cast<const PhysNodesModifierData *>(&md);
      break;
    }
  }
  if (pmd == nullptr || pmd->tree_name[0] == '\0') {
    return;
  }

  ID *id = BKE_libblock_find_name(bmain, ID_NT, pmd->tree_name, std::nullopt);
  bNodeTree *tree = reinterpret_cast<bNodeTree *>(id);
  if (tree == nullptr || StringRef(tree->idname) != "PhysNodeTree") {
    return;
  }

  *r_ntree = tree;
  *r_id = &object->id;
  *r_from = &object->id;
}

static void phys_node_tree_foreach_nodeclass(void *calldata, bke::bNodeClassCallback func)
{
  func(calldata, NODE_CLASS_INPUT, N_("Input"));
  func(calldata, NODE_CLASS_GEOMETRY, N_("Geometry"));
  func(calldata, NODE_CLASS_OUTPUT, N_("Output"));
}

static bool phys_node_tree_validate_link(eNodeSocketDatatype type_a,
                                         eNodeSocketDatatype type_b)
{
  /* Numeric sockets convert between each other, everything else must match. */
  if (ELEM(type_a, SOCK_FLOAT, SOCK_INT, SOCK_VECTOR, SOCK_BOOLEAN) &&
      ELEM(type_b, SOCK_FLOAT, SOCK_INT, SOCK_VECTOR, SOCK_BOOLEAN))
  {
    return true;
  }
  return type_a == type_b;
}

static bool phys_node_tree_socket_type_valid(bke::bNodeTreeType * /*treetype*/,
                                             bke::bNodeSocketType *socket_type)
{
  return bke::node_is_static_socket_type(*socket_type) &&
         ELEM(socket_type->type,
              SOCK_FLOAT,
              SOCK_INT,
              SOCK_BOOLEAN,
              SOCK_VECTOR,
              SOCK_STRING,
              SOCK_OBJECT,
              SOCK_COLLECTION,
              SOCK_MENU,
              SOCK_GEOMETRY);
}

void register_node_tree_type_phys()
{
  bke::bNodeTreeType *tt = ntreeType_Phys = MEM_new<bke::bNodeTreeType>(__func__);
  tt->type = NTREE_CUSTOM;
  tt->idname = "PhysNodeTree"_ustr;
  tt->ui_name = N_("Physics Node Editor");
  tt->ui_icon = ICON_PHYSICS;
  tt->ui_description =
      N_("Physics simulation node tree. Native C++ tree type, no addon required.");
  tt->rna_ext.srna = RNA_PhysNodeTree;
  tt->get_from_context = phys_node_tree_get_from_context;
  tt->foreach_nodeclass = phys_node_tree_foreach_nodeclass;
  tt->valid_socket_type = phys_node_tree_socket_type_valid;
  tt->validate_link = phys_node_tree_validate_link;

  bke::node_tree_type_add(*tt);
}

}  // namespace blender
