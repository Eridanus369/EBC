/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief Native `PhysTimeNodeCXX`: exposes the simulation time of the world.
 *
 * The values are read by downstream nodes from the Scene level step. Nothing
 * is stepped here; the node is a pure input marker, matching the geometry
 * node convention that inputs never advance a simulation.
 */

#include "BKE_node.hh"

#include "DNA_node_types.h"

#include "NOD_node_declaration.hh"
#include "NOD_register.hh"
#include "NOD_socket_declarations.hh"

namespace blender::nodes::node_phys_time_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_layout([](ui::Layout &layout, bContext * /*C*/, PointerRNA * /*ptr*/) {
    layout.label("Driven by the Scene sim op (read-only outputs)", ICON_NONE);
  });
  b.add_output<decl::Float>("Time"_ustr)
      .subtype(PROP_TIME)
      .description("Simulation time in seconds (s)");
  b.add_output<decl::Float>("Delta Time"_ustr)
      .subtype(PROP_TIME)
      .description("Duration of the last step in seconds (s)");
  b.add_output<decl::Int>("Frame"_ustr)
      .description("Scene frame at the last simulation step");
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

static void node_register()
{
  static bke::bNodeType ntype;
  node_type_base(ntype, "PhysTimeNodeCXX"_ustr);
  ntype.ui_name = "Time";
  ntype.ui_description =
      "Simulation time, delta time and frame of the native physics world.";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = node_declare;
  ntype.poll = node_poll;
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_phys_time_cc
