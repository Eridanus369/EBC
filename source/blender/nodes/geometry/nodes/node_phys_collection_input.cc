/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief C++ collection input node for `PhysNodeTree` (step 2).
 *
 * Expands a Blender collection into a batch of rigid bodies so users do not
 * wire one rigid body node per object. The contract is identical to the
 * Python `PhysCollectionInputNode`:
 *
 * - uid = `<this node name>/<object name>`; names, never pointers;
 * - only `MESH` objects become bodies, other types are skipped with a
 *   readable diagnostic;
 * - `Recursive=false` reads direct members, `true` also reads child
 *   collections;
 * - membership is re-derived every evaluation and diffed by uid, so removals
 *   drop bodies, additions create them and survivors keep their uid.
 */

#include "BKE_node.hh"
#include "BKE_node_enum.hh"
#include "BKE_node_runtime.hh"

#include "DNA_node_types.h"

#include "NOD_node_declaration.hh"
#include "NOD_register.hh"
#include "NOD_socket_declarations.hh"

namespace blender::nodes::node_phys_collection_input_cc {

enum CollectionBodyType : int {
  COLLECTION_BODY_ACTIVE = 0,
  COLLECTION_BODY_STATIC = 1,
  COLLECTION_BODY_PASSIVE = 2,
};

static const EnumPropertyItem collection_body_type_items[] = {
    {COLLECTION_BODY_ACTIVE,
     "ACTIVE",
     0,
     N_("Active"),
     N_("Dynamic bodies simulated by the solver")},
    {COLLECTION_BODY_STATIC,
     "STATIC",
     0,
     N_("Static"),
     N_("Immobile colliders; only friction and restitution are relevant")},
    {COLLECTION_BODY_PASSIVE,
     "PASSIVE",
     0,
     N_("Passive"),
     N_("Kinematic bodies driven by their own object animation")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  PanelDeclarationBuilder &source_panel = b.add_panel("Collection"_ustr, 1);
  source_panel.description("Objects expanded into rigid bodies");
  source_panel.add_input<decl::Menu>("Type"_ustr)
      .static_items(collection_body_type_items)
      .default_value(COLLECTION_BODY_ACTIVE)
      .optional_label();
  source_panel.add_input<decl::Collection>("Collection"_ustr)
      .description("Collection whose MESH members become bodies");
  source_panel.add_input<decl::Bool>("Recursive"_ustr)
      .default_value(false)
      .description("Also include child collections");

  PanelDeclarationBuilder &material_panel = b.add_panel("Material"_ustr, 2);
  material_panel.description("Surface response and density applied to every member");
  material_panel.add_input<decl::Float>("Friction"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Coulomb friction coefficient (0 = frictionless, 1 = no sliding)");
  material_panel.add_input<decl::Float>("Density"_ustr)
      .default_value(500.0f)
      .min(0.0f)
      .description("Volumetric density in kilograms per cubic metre (kg/m³)");

  PanelDeclarationBuilder &solver_panel = b.add_panel("Solver"_ustr, 3);
  solver_panel.description("Simulation cost for this batch");
  solver_panel.add_input<decl::Int>("Substeps"_ustr)
      .default_value(4)
      .min(1)
      .description("Solver substeps per scene frame");

  b.add_output<decl::Object>("Bodies"_ustr)
      .description("Resolved bodies; connect to a collider or output node");
}

static bool node_poll(const bke::bNodeType * /*ntype*/,
                      const bNodeTree *nodetree,
                      const char **r_disabled_hint)
{
  if (nodetree == nullptr) {
    *r_disabled_hint = "No active node tree";
    return false;
  }
  if (!STREQ(nodetree->idname, "PhysNodeTree")) {
    *r_disabled_hint = "Only available in a Physics Node Tree";
    return false;
  }
  return true;
}

/* Custom node trees do not get enum definitions propagated by the generic
 * tree update, so the menu socket is filled in here (same reason as the
 * rigid body node). */
static void node_update(bNodeTree * /*ntree*/, bNode *node)
{
  bNodeSocket *type_socket = nullptr;
  for (bNodeSocket *socket = node->inputs.first(); socket != nullptr; socket = socket->next) {
    if (STREQ(socket->identifier, "Type")) {
      type_socket = socket;
      break;
    }
  }
  if (type_socket == nullptr || type_socket->type != SOCK_MENU) {
    return;
  }
  bNodeSocketValueMenu *value = static_cast<bNodeSocketValueMenu *>(type_socket->default_value);
  if (value->enum_items != nullptr) {
    return;
  }
  bke::RuntimeNodeEnumItems *items = new bke::RuntimeNodeEnumItems();
  for (const EnumPropertyItem *item = collection_body_type_items; item->identifier; item++) {
    bke::RuntimeNodeEnumItem runtime_item;
    runtime_item.name = item->name;
    runtime_item.description = item->description;
    runtime_item.identifier = item->value;
    runtime_item.icon = item->icon;
    items->items.append(std::move(runtime_item));
  }
  value->enum_items = items;
}

static void node_register()
{
  static bke::bNodeType ntype;
  node_type_base(ntype, "PhysCollectionInputNodeCXX"_ustr);
  ntype.ui_name = "Collection Input (C++)";
  ntype.ui_description =
      "Batch rigid bodies from a Collection. One body per MESH object; uid=<node>/<object>. "
      "Membership is re-derived every frame and diffed by uid; non-mesh members are skipped.";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = node_declare;
  ntype.poll = node_poll;
  ntype.updatefunc = node_update;
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_phys_collection_input_cc
