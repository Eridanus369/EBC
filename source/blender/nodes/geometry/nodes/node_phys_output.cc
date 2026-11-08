/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief Native `PhysOutputNodeCXX`: output marker for a physics world.
 *
 * Both modes are declared, but only the mesh path currently writes through
 * the existing `PhysNodesModifier` sink. Volume/particle payloads are not
 * implemented in the C++ data layer yet; the node does not fabricate them.
 */

#include "BKE_node.hh"
#include "BKE_node_enum.hh"
#include "BKE_node_runtime.hh"

#include "DNA_node_types.h"

#include "NOD_node_declaration.hh"
#include "NOD_register.hh"
#include "NOD_socket_declarations.hh"

namespace blender::nodes::node_phys_output_cc {

enum OutputMode : int {
  OUTPUT_POINTCLOUD = 0,
  OUTPUT_MESH = 1,
};

static const EnumPropertyItem output_mode_items[] = {
    {OUTPUT_POINTCLOUD,
     "POINTCLOUD",
     0,
     N_("Point Cloud"),
     N_("Particles are counted for diagnostics only; no point cloud object is written yet")},
    {OUTPUT_MESH,
     "MESH",
     0,
     N_("Mesh"),
     N_("Write the particle-to-mesh result into the Object below, or into the "
        "modifier's object when Object is empty")},
    {0, nullptr, 0, nullptr, nullptr},
};

static int output_mode(const bNode &node)
{
  for (const bNodeSocket *socket = node.inputs.first(); socket != nullptr; socket = socket->next) {
    if (!STREQ(socket->identifier, "Mode")) {
      continue;
    }
    const bNodeSocketValueMenu *value = static_cast<const bNodeSocketValueMenu *>(
        socket->default_value);
    return value != nullptr ? value->value : OUTPUT_MESH;
  }
  return OUTPUT_MESH;
}

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  PanelDeclarationBuilder &target_panel = b.add_panel("Output"_ustr, 1);
  target_panel.description("Write the simulated state to an object");
  target_panel.add_input<decl::Menu>("Mode"_ustr)
      .static_items(output_mode_items)
      .default_value(OUTPUT_MESH)
      .optional_label()
      .description("Mesh writes the particle-to-mesh result; Point Cloud only counts particles");
  target_panel.add_input<decl::Object>("Object"_ustr)
      .description(
          "Mesh mode: mesh object that receives the particle-to-mesh result. "
          "Empty means PHYSMesh_<tree name>. Point Cloud mode does not write "
          "to this socket.");

  const bNode *node = b.node_or_null();
  if (node != nullptr && output_mode(*node) == OUTPUT_POINTCLOUD) {
    PanelDeclarationBuilder &points_panel = b.add_panel("Points"_ustr, 2);
    points_panel.description("Point cloud display radius");
    points_panel.add_input<decl::Float>("Point Radius"_ustr)
        .default_value(0.01f)
        .min(0.0f)
        .subtype(PROP_DISTANCE)
        .description("Radius of each rendered point in metres (m)");
  }
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
  bNodeSocket *mode_socket = nullptr;
  for (bNodeSocket *socket = node->inputs.first(); socket != nullptr; socket = socket->next) {
    if (STREQ(socket->identifier, "Mode")) {
      mode_socket = socket;
      break;
    }
  }
  if (mode_socket == nullptr || mode_socket->type != SOCK_MENU) {
    return;
  }
  bNodeSocketValueMenu *value = static_cast<bNodeSocketValueMenu *>(mode_socket->default_value);
  if (value->enum_items != nullptr) {
    return;
  }
  bke::RuntimeNodeEnumItems *items = new bke::RuntimeNodeEnumItems();
  for (const EnumPropertyItem *item = output_mode_items; item->identifier; item++) {
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
  node_type_base(ntype, "PhysOutputNodeCXX"_ustr);
  ntype.ui_name = "Output";
  ntype.ui_description =
      "Physics world output. Mesh output writes through the modifier sink.";
  ntype.nclass = NODE_CLASS_OUTPUT;
  ntype.declare = node_declare;
  ntype.poll = node_poll;
  ntype.updatefunc = node_update;
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_phys_output_cc
