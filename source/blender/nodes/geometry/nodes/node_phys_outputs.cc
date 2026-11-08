/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 * \brief Native particle/volume output nodes.
 *
 * These three nodes declare the output contract of the C++ data layer
 * (`bke::physnodes::ParticleSet`, `DensityField`). The C++ sink
 * (`blenkernel/intern/physnodes.cc`) produces those payloads: it collects
 * particles from the object or particle system, splats a dense field, extracts
 * a narrow-band marching-cubes surface, and writes the density field into a
 * Volume datablock with an OpenVDB grid.
 *
 * The read-only `status` property reports the reason when no payload could be
 * produced instead of fabricating geometry.
 *
 * The reason strings come from `BKE_physnodes.hh` so the UI and the kernel
 * cannot disagree.
 */

#include "BKE_node.hh"
#include "BKE_node_enum.hh"
#include "BKE_node_runtime.hh"
#include "BKE_physnodes.hh"

#include "DNA_node_types.h"

#include "NOD_node_declaration.hh"
#include "NOD_register.hh"
#include "NOD_socket_declarations.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include <string>

namespace blender::nodes::node_phys_outputs_cc {

using namespace bke::physnodes::output_reasons;

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

/* ---------------------------------------------------------------------------
 * Particle to Mesh
 * ------------------------------------------------------------------------- */

static void particle_to_mesh_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  PanelDeclarationBuilder &source_panel = b.add_panel("Particles"_ustr, 1);
  source_panel.description("Particle source read by the native sink");
  source_panel.add_input<decl::Object>("Particle Source"_ustr)
      .description(
          "Object providing particles: a particle system, an OB_POINTCLOUD "
          "(liquid output) or mesh vertices (droplets)");
  source_panel.add_input<decl::Float>("Radius Scale"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .subtype(PROP_FACTOR)
      .description("Multiplier applied to the particle radii before meshing");

  PanelDeclarationBuilder &surface_panel = b.add_panel("Surface"_ustr, 2);
  surface_panel.description("Narrow-band marching-cubes extraction");
  surface_panel.add_input<decl::Float>("Voxel Size"_ustr)
      .default_value(0.02f)
      .min(1.0e-4f)
      .subtype(PROP_DISTANCE)
      .description("Narrow-band voxel size in metres (m)");
  surface_panel.add_input<decl::Float>("Iso Value"_ustr)
      .default_value(0.5f)
      .subtype(PROP_FACTOR)
      .description("Iso surface threshold in the 0..1 density field");

  b.add_output<decl::Geometry>("Mesh"_ustr)
      .description("Extracted surface mesh written to the sink object");
}

enum OutputSlot { OUTPUT_MESH, OUTPUT_VOLUME_FIELD, OUTPUT_VOLUME };

static std::string output_status_from_runtime(PointerRNA *ptr, OutputSlot slot)
{
  const ID *id = ptr->owner_id;
  if (id == nullptr) {
    return "";
  }
  bke::physnodes::FrameState state;
  if (!bke::physnodes::frame_state(id->name + 2, &state)) {
    return "";
  }
  switch (slot) {
    case OUTPUT_MESH:
      return state.output.particle_mesh;
    case OUTPUT_VOLUME_FIELD:
      return state.output.particle_volume;
    case OUTPUT_VOLUME:
      return state.output.volume_output;
  }
  return "";
}

static std::string particle_to_mesh_status(PointerRNA *ptr, PropertyRNA * /*prop*/)
{
  return output_status_from_runtime(ptr, OUTPUT_MESH);
}

static void particle_to_mesh_rna(StructRNA *srna)
{
  PropertyRNA *prop = RNA_def_property(srna, "status", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs_runtime(prop, particle_to_mesh_status, nullptr, nullptr, nullptr, nullptr);
  RNA_def_property_ui_text(
      prop, "Status", "Empty when the output was produced, otherwise the missing input (read-only)");
}

static void particle_to_mesh_register()
{
  static bke::bNodeType ntype;
  node_type_base(ntype, "PhysParticleToMeshNodeCXX"_ustr);
  ntype.ui_name = "Particle to Mesh";
  ntype.ui_description =
      "Extract a surface mesh from an object's particles (liquid or droplets) "
      "with the B17.3 narrow-band marching-tetrahedra extractor.";
  ntype.nclass = NODE_CLASS_OUTPUT;
  ntype.declare = particle_to_mesh_declare;
  ntype.poll = node_poll;
  bke::node_register_type(ntype);
  particle_to_mesh_rna(ntype.rna_ext.srna);
}
NOD_REGISTER_NODE(particle_to_mesh_register)

/* ---------------------------------------------------------------------------
 * Particle to Volume
 * ------------------------------------------------------------------------- */

static void particle_to_volume_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  PanelDeclarationBuilder &source_panel = b.add_panel("Particles"_ustr, 1);
  source_panel.description("Particle source splatted into a dense field");
  source_panel.add_input<decl::Object>("Particle Source"_ustr)
      .description(
          "Object providing particles: a particle system, an OB_POINTCLOUD "
          "(liquid output) or mesh vertices (droplets)");
  source_panel.add_input<decl::Float>("Radius Scale"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .subtype(PROP_FACTOR)
      .description("Splat radius multiplier in metres (m)");

  PanelDeclarationBuilder &field_panel = b.add_panel("Field"_ustr, 2);
  field_panel.description("Dense field resolution and amplitude");
  field_panel.add_input<decl::Float>("Voxel Size"_ustr)
      .default_value(0.02f)
      .min(1.0e-4f)
      .subtype(PROP_DISTANCE)
      .description("Dense grid voxel size in metres (m)");
  field_panel.add_input<decl::Float>("Density Scale"_ustr)
      .default_value(1.0f)
      .min(0.0f)
      .description("Multiplier applied to the splatted density (unitless)");

  b.add_output<decl::Geometry>("Volume"_ustr)
      .description("Dense field consumed by the Volume Output node");
}

static std::string particle_to_volume_status(PointerRNA *ptr, PropertyRNA * /*prop*/)
{
  return output_status_from_runtime(ptr, OUTPUT_VOLUME_FIELD);
}

static void particle_to_volume_rna(StructRNA *srna)
{
  PropertyRNA *prop = RNA_def_property(srna, "status", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs_runtime(prop, particle_to_volume_status, nullptr, nullptr, nullptr, nullptr);
  RNA_def_property_ui_text(
      prop, "Status", "Empty when the output was produced, otherwise the missing input (read-only)");
}

static void particle_to_volume_register()
{
  static bke::bNodeType ntype;
  node_type_base(ntype, "PhysParticleToVolumeNodeCXX"_ustr);
  ntype.ui_name = "Particle to Volume";
  ntype.ui_description =
      "Splat particles into a dense density field consumed by Volume Output.";
  ntype.nclass = NODE_CLASS_OUTPUT;
  ntype.declare = particle_to_volume_declare;
  ntype.poll = node_poll;
  bke::node_register_type(ntype);
  particle_to_volume_rna(ntype.rna_ext.srna);
}
NOD_REGISTER_NODE(particle_to_volume_register)

/* ---------------------------------------------------------------------------
 * Volume Output
 * ------------------------------------------------------------------------- */

static void volume_output_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  PanelDeclarationBuilder &source_panel = b.add_panel("Density"_ustr, 1);
  source_panel.description("Dense field published as a Volume object");
  source_panel.add_input<decl::Geometry>("Density"_ustr)
      .description(
          "Dense field from this tree (Particle to Volume). The sink also "
          "accepts a smoke density grid published through the C++ data layer.");
  source_panel.add_input<decl::Object>("Volume Object"_ustr)
      .description(
          "Optional existing Volume object to write into. Leave empty to let "
          "the sink create and link a dedicated Volume object.");

  PanelDeclarationBuilder &cache_panel = b.add_panel("Cache"_ustr, 2);
  cache_panel.description("Optional VDB cache write-out");
  cache_panel.add_input<decl::String>("Cache Path"_ustr)
      .description("Directory for .vdb frame files (empty = in-memory only)");

  b.add_output<decl::Geometry>("Volume"_ustr)
      .description("Volume datablock written by the native sink");
}

static std::string volume_output_status(PointerRNA *ptr, PropertyRNA * /*prop*/)
{
  return output_status_from_runtime(ptr, OUTPUT_VOLUME);
}

static void volume_output_rna(StructRNA *srna)
{
  PropertyRNA *prop = RNA_def_property(srna, "status", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs_runtime(prop, volume_output_status, nullptr, nullptr, nullptr, nullptr);
  RNA_def_property_ui_text(
      prop, "Status", "Empty when the output was produced, otherwise the missing input (read-only)");
}

static void volume_output_register()
{
  static bke::bNodeType ntype;
  node_type_base(ntype, "PhysVolumeOutputNodeCXX"_ustr);
  ntype.ui_name = "Volume Output";
  ntype.ui_description =
      "Publish the tree's dense field as a Volume object (OpenVDB density grid) "
      "written by the C++ sink.";
  ntype.nclass = NODE_CLASS_OUTPUT;
  ntype.declare = volume_output_declare;
  ntype.poll = node_poll;
  bke::node_register_type(ntype);
  volume_output_rna(ntype.rna_ext.srna);
}
NOD_REGISTER_NODE(volume_output_register)

}  // namespace blender::nodes::node_phys_outputs_cc
