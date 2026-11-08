/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief C++ rigid body node for `PhysNodeTree`.
 *
 * Unlike the Python mirror in `_physnodes`, this node has a real C++
 * declaration. Sockets are rebuilt from the node's `Type` menu value, so
 * `Mass` / `Velocity` / damping only exist for active bodies while static
 * bodies only expose friction and restitution.
 *
 * The idname carries a `CXX` suffix on purpose: the Python package registers
 * `PhysRigidBodyNode` and must keep working unchanged as a fallback path. The
 * two node types are deliberately separate so the Python schema is never
 * mutated by C++ socket rebuilding.
 */

#include "BKE_node.hh"
#include "BKE_node_enum.hh"
#include "BKE_node_runtime.hh"

#include "DNA_node_types.h"

#include "NOD_node_declaration.hh"
#include "NOD_register.hh"
#include "NOD_rna_define.hh"
#include "NOD_socket_declarations.hh"

namespace blender::nodes::node_phys_rigid_body_cc {

enum RigidBodyType : int {
  RIGID_BODY_ACTIVE = 0,
  RIGID_BODY_STATIC = 1,
  RIGID_BODY_PASSIVE = 2,
};

static const EnumPropertyItem rigid_body_type_items[] = {
    {RIGID_BODY_ACTIVE,
     "Active",
     0,
     N_("Active"),
     N_("Dynamic body simulated by the solver")},
    {RIGID_BODY_STATIC,
     "Static",
     0,
     N_("Static"),
     N_("Immobile collider; only friction and restitution are relevant")},
    {RIGID_BODY_PASSIVE,
     "Passive",
     0,
     N_("Passive"),
     N_("Kinematic body driven by animation or manual transforms")},
    {0, nullptr, 0, nullptr, nullptr},
};

static int rigid_body_type(const bNode &node)
{
  for (const bNodeSocket *socket = node.inputs.first(); socket != nullptr; socket = socket->next) {
    if (!STREQ(socket->identifier, "Type")) {
      continue;
    }
    const bNodeSocketValueMenu *value = static_cast<const bNodeSocketValueMenu *>(
        socket->default_value);
    return value != nullptr ? value->value : RIGID_BODY_ACTIVE;
  }
  return RIGID_BODY_ACTIVE;
}

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  /* Stable panel identifiers keep socket matching intact when the dynamic
   * sockets are rebuilt after a `Type` change. */
  PanelDeclarationBuilder &body_panel = b.add_panel("Body"_ustr, 1);
  body_panel.description("Body definition and the object it drives");

  /* Declared for every body type. */
  body_panel.add_input<decl::Menu>("Type"_ustr)
      .static_items(rigid_body_type_items)
      .default_value(RIGID_BODY_ACTIVE)
      .optional_label();
  /* The body object is a socket (not a node property) so the runtime can
   * resolve it from the tree without depending on Python-only node RNA. */
  body_panel.add_input<decl::Object>("Object"_ustr)
      .description("Object that receives this body's simulated transform");

  /* `node_or_null()` marks the declaration as context dependent. Without it
   * the static declaration would be built once and sockets could never
   * change. When the static declaration is built, the node pointer is null
   * and the default (active) layout is used. */
  const bNode *node = b.node_or_null();
  const int type = node != nullptr ? rigid_body_type(*node) : RIGID_BODY_ACTIVE;

  switch (type) {
    case RIGID_BODY_STATIC: {
      PanelDeclarationBuilder &contact_panel = b.add_panel("Contact"_ustr, 2);
      contact_panel.description("Contact response of an immobile collider");
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
      break;
    }
    case RIGID_BODY_PASSIVE: {
      PanelDeclarationBuilder &driver_panel = b.add_panel("Driver"_ustr, 3);
      driver_panel.description("Animation source for a kinematic body");
      driver_panel.add_input<decl::Object>("Driver"_ustr)
          .description("Object whose animation drives this kinematic body");

      PanelDeclarationBuilder &contact_panel = b.add_panel("Contact"_ustr, 2);
      contact_panel.description("Contact response of a kinematic body");
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
      break;
    }
    case RIGID_BODY_ACTIVE:
    default: {
      PanelDeclarationBuilder &dynamics_panel = b.add_panel("Dynamics"_ustr, 4);
      dynamics_panel.description("Rigid body dynamics solved by the Scene sim op");
      dynamics_panel.add_input<decl::Float>("Mass"_ustr)
          .default_value(1.0f)
          .min(0.0f)
          .subtype(PROP_MASS)
          .description("Mass in kilograms (kg). 0 makes the body static");
      dynamics_panel.add_input<decl::Vector>("Velocity"_ustr)
          .default_value(float3(0.0f))
          .subtype(PROP_VELOCITY)
          .description("Initial linear velocity in metres per second (m/s)");
      dynamics_panel.add_input<decl::Float>("Linear Damping"_ustr)
          .default_value(0.0f)
          .min(0.0f)
          .max(1.0f)
          .subtype(PROP_FACTOR)
          .description("Fraction of linear velocity lost per second");
      dynamics_panel.add_input<decl::Float>("Angular Damping"_ustr)
          .default_value(0.0f)
          .min(0.0f)
          .max(1.0f)
          .subtype(PROP_FACTOR)
          .description("Fraction of angular velocity lost per second");
      break;
    }
  }

  b.add_output<decl::Object>("Body"_ustr)
      .description("Resolved body; connect it to an output node");
}

static bool node_poll(const bke::bNodeType * /*ntype*/,
                      const bNodeTree *nodetree,
                      const char **r_disabled_hint)
{
  /* Only offer this node inside the native PhysNodeTree. The tree type
   * itself is unchanged; we match it by idname. */
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

/* Menu sockets of custom (non geometry/shader/compositor) node trees do not
 * get their enum definition propagated by the generic tree update. Fill it in
 * here so `Type` is usable from RNA and the UI in `PhysNodeTree` as well. */
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
  for (const EnumPropertyItem *item = rigid_body_type_items; item->identifier; item++) {
    bke::RuntimeNodeEnumItem runtime_item;
    runtime_item.name = item->name;
    runtime_item.description = item->description;
    runtime_item.identifier = item->value;
    runtime_item.icon = item->icon;
    items->items.append(std::move(runtime_item));
  }
  /* The freshly created sharing data starts with one user, which is the
   * socket's reference. Freed together with the socket default value. */
  value->enum_items = items;
}

/* RNA enum accessor used by the native `Type` property. The menu socket stays
 * the single source of truth, so both the inline node UI and the sidebar and
 * Python see the same value. Changing it through RNA tags the node property,
 * which is what rebuilds the dynamic sockets. */
static int rigid_body_type_rna_get(PointerRNA *ptr, PropertyRNA * /*prop*/)
{
  return rigid_body_type(*static_cast<const bNode *>(ptr->data));
}

static void rigid_body_type_rna_set(PointerRNA *ptr, PropertyRNA * /*prop*/, int value)
{
  bNode &node = *static_cast<bNode *>(ptr->data);
  for (bNodeSocket *socket = node.inputs.first(); socket != nullptr; socket = socket->next) {
    if (!STREQ(socket->identifier, "Type") || socket->type != SOCK_MENU) {
      continue;
    }
    bNodeSocketValueMenu *menu = static_cast<bNodeSocketValueMenu *>(socket->default_value);
    menu->value = value;
    return;
  }
}

static void node_rna(StructRNA *srna)
{
  RNA_def_node_enum(srna,
                    "body_type",
                    "Type",
                    "Body type. Rebuilds the input sockets when changed",
                    rigid_body_type_items,
                    EnumRNAAccessors(rigid_body_type_rna_get, rigid_body_type_rna_set),
                    RIGID_BODY_ACTIVE,
                    nullptr,
                    false);
}

static void node_register()
{
  static bke::bNodeType ntype;
  node_type_base(ntype, "PhysRigidBodyNodeCXX"_ustr);
  ntype.ui_name = "Rigid Body (C++)";
  ntype.ui_description =
      "C++ rigid body node with dynamic sockets. Active bodies expose mass, velocity and damping.";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = node_declare;
  ntype.poll = node_poll;
  ntype.updatefunc = node_update;
  node_rna(ntype.rna_ext.srna);
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_phys_rigid_body_cc
