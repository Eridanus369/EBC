/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief Native `PhysColliderNodeCXX`: a static collider body.
 *
 * The node declares the collision shape and its parameters. Contact
 * generation itself is part of the step-3 solver work; this node only feeds
 * a static `BodyRecord` into the Scene level world.
 */

#include "BKE_node.hh"
#include "BKE_node_enum.hh"
#include "BKE_node_runtime.hh"

#include "DNA_node_types.h"

#include "NOD_node_declaration.hh"
#include "NOD_register.hh"
#include "NOD_socket_declarations.hh"

namespace blender::nodes::node_phys_collider_cc {

enum ColliderShape : int {
  COLLIDER_SHAPE_MESH = 0,
  COLLIDER_SHAPE_SPHERE = 1,
  COLLIDER_SHAPE_BOX = 2,
};

static const EnumPropertyItem collider_shape_items[] = {
    {COLLIDER_SHAPE_MESH,
     "MESH",
     0,
     N_("Mesh"),
     N_("Use the object mesh triangles as the collider")},
    {COLLIDER_SHAPE_SPHERE,
     "SPHERE",
     0,
     N_("Sphere"),
     N_("Analytic sphere collider")},
    {COLLIDER_SHAPE_BOX,
     "BOX",
     0,
     N_("Box"),
     N_("Analytic box collider")},
    {0, nullptr, 0, nullptr, nullptr},
};

static int collider_shape(const bNode &node)
{
  for (const bNodeSocket *socket = node.inputs.first(); socket != nullptr; socket = socket->next) {
    if (!STREQ(socket->identifier, "Shape")) {
      continue;
    }
    const bNodeSocketValueMenu *value = static_cast<const bNodeSocketValueMenu *>(
        socket->default_value);
    return value != nullptr ? value->value : COLLIDER_SHAPE_MESH;
  }
  return COLLIDER_SHAPE_MESH;
}

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  PanelDeclarationBuilder &shape_panel = b.add_panel("Shape"_ustr, 1);
  shape_panel.description("Collision shape used by the native physics world");

  shape_panel.add_input<decl::Menu>("Shape"_ustr)
      .static_items(collider_shape_items)
      .default_value(COLLIDER_SHAPE_MESH)
      .optional_label();
  shape_panel.add_input<decl::Object>("Object"_ustr)
      .description("Mesh object used by the Mesh shape (ignored by analytic shapes)");

  const bNode *node = b.node_or_null();
  const int shape = node != nullptr ? collider_shape(*node) : COLLIDER_SHAPE_MESH;
  if (shape == COLLIDER_SHAPE_SPHERE) {
    shape_panel.add_input<decl::Float>("Radius"_ustr)
        .default_value(0.5f)
        .min(0.0f)
        .subtype(PROP_DISTANCE)
        .description("Analytic sphere radius in metres (m)");
  }
  else if (shape == COLLIDER_SHAPE_BOX) {
    shape_panel.add_input<decl::Vector>("Size"_ustr)
        .default_value(float3(1.0f))
        .subtype(PROP_DISTANCE)
        .description("Analytic box half extents in metres (m)");
  }

  PanelDeclarationBuilder &contact_panel = b.add_panel("Contact"_ustr, 2);
  contact_panel.description("Contact response of this collider");
  contact_panel.add_input<decl::Float>("Friction"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Coulomb friction coefficient (0 = frictionless, 1 = no sliding)");
  contact_panel.add_input<decl::Float>("Restitution"_ustr)
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Bounce factor (0 = fully inelastic, 1 = perfectly elastic)");

  b.add_output<decl::Object>("Body"_ustr)
      .description("Resolved static body; connect it to an output node");
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

static void node_update(bNodeTree * /*ntree*/, bNode *node)
{
  bNodeSocket *shape_socket = nullptr;
  for (bNodeSocket *socket = node->inputs.first(); socket != nullptr; socket = socket->next) {
    if (STREQ(socket->identifier, "Shape")) {
      shape_socket = socket;
      break;
    }
  }
  if (shape_socket == nullptr || shape_socket->type != SOCK_MENU) {
    return;
  }
  bNodeSocketValueMenu *value = static_cast<bNodeSocketValueMenu *>(shape_socket->default_value);
  if (value->enum_items != nullptr) {
    return;
  }
  bke::RuntimeNodeEnumItems *items = new bke::RuntimeNodeEnumItems();
  for (const EnumPropertyItem *item = collider_shape_items; item->identifier; item++) {
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
  node_type_base(ntype, "PhysColliderNodeCXX"_ustr);
  ntype.ui_name = "Collider";
  ntype.ui_description =
      "Static collider body. Mesh, sphere and box shapes feed the native physics world.";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = node_declare;
  ntype.poll = node_poll;
  ntype.updatefunc = node_update;
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_phys_collider_cc
