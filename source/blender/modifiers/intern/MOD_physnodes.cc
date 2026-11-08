/* SPDX-FileCopyrightText: 2026 EBC Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 * \brief Physics node tree sink modifier (tier A2).
 *
 * The simulation world is stepped by a Scene level depsgraph operation
 * (`OperationCode::PHYS_SIM`, see `physnodes.cc`). This modifier never
 * steps anything itself: it reads the frame state of the referenced tree
 * and writes the result back to the mesh. That keeps evaluation
 * deterministic even when the graph is evaluated several times per frame
 * (redraw, parameter change, render).
 *
 * Collider mesh source is decided by the modifier stack position, never
 * inferred: a physics modifier *before* geometry nodes modifiers receives the
 * original mesh, one *after* receives the mesh those modifiers produced. If a
 * body penetrates another, check the modifier order first.
 */

#include <cmath>

#include "BLI_listbase_wrapper.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "BLT_translation.hh"

#include "BKE_mesh.hh"
#include "BKE_modifier.hh"
#include "BKE_physnodes.hh"

#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "RNA_prototypes.hh"

#include "UI_resources.hh"

#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  PhysNodesModifierData *pmd = reinterpret_cast<PhysNodesModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(pmd, modifier);
  pmd->tree_name[0] = '\0';
  pmd->mode = PHYSNODES_MODE_PASSTHROUGH;
  pmd->flags = 0;
}

/**
 * Signed volume of a closed mesh in object local space (divergence theorem,
 * fan triangulated polygons). Used to verify the collider mesh source policy:
 * the modifier measures the mesh it actually received, so "before geometry
 * nodes" yields the original mesh volume and "after geometry nodes" yields the
 * deformed one.
 */
static float mesh_volume(const Mesh &mesh, Span<float3> positions)
{
  const OffsetIndices<int> faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  double volume = 0.0;
  for (const int face_index : faces.index_range()) {
    const IndexRange face = faces[face_index];
    if (face.size() < 3) {
      continue;
    }
    const int i0 = corner_verts[face[0]];
    for (int k = 1; k + 1 < face.size(); k++) {
      const int i1 = corner_verts[face[k]];
      const int i2 = corner_verts[face[k + 1]];
      volume += double(math::dot(positions[i0], math::cross(positions[i1], positions[i2])));
    }
  }
  return float(std::fabs(volume) / 6.0);
}

static void deform_verts(ModifierData *md,
                         const ModifierEvalContext *ctx,
                         Mesh *mesh,
                         MutableSpan<float3> positions)
{
  PhysNodesModifierData *pmd = reinterpret_cast<PhysNodesModifierData *>(md);
  if (pmd->tree_name[0] == '\0') {
    return;
  }

  /* Collider mesh source policy: the mesh this modifier receives is the mesh
   * at its stack position. Geometry nodes modifiers before it mean the mesh
   * is evaluated (deformed); none before it means the original mesh. */
  bool geometry_nodes_before = false;
  int stack_index = -1;
  int index = 0;
  if (ctx != nullptr && ctx->object != nullptr) {
    for (ModifierData &item : ctx->object->modifiers.items_mutable()) {
      if (&item == md) {
        stack_index = index;
        break;
      }
      if (item.type == eModifierType_Nodes) {
        geometry_nodes_before = true;
      }
      index++;
    }
  }
  bke::physnodes::note_collider(
      pmd->tree_name, mesh_volume(*mesh, positions), geometry_nodes_before ? 1 : 0, stack_index);

  if (pmd->mode != PHYSNODES_MODE_SINK) {
    /* Pass-through: the mesh is left untouched. This is the ABI/staging
     * mode used before the world integration is wired in. */
    return;
  }

  if (ctx == nullptr || ctx->object == nullptr) {
    return;
  }
  float3 offset(0.0f);
  if (!bke::physnodes::object_offset(pmd->tree_name, ctx->object->id.name + 2, &offset)) {
    /* This object is not an active body of that world (or the world has no
     * state yet): never invent an offset, just pass through. */
    return;
  }

  if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) {
    return;
  }
  threading::parallel_for(positions.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      positions[i] += offset;
    }
  });
}

static bool depends_on_time(Scene * /*scene*/, ModifierData * /*md*/)
{
  /* The sink must be re-evaluated on frame changes to pick up new world
   * state. It is still cheap: without state it is a no-op. */
  return true;
}

ModifierTypeInfo modifierType_PhysNodes = {
    /*idname*/ "PhysNodes",
    /*name*/ N_("Physics Nodes"),
    /*struct_name*/ "PhysNodesModifierData",
    /*struct_size*/ sizeof(PhysNodesModifierData),
    /*srna*/ &RNA_PhysNodesModifier,
    /*type*/ ModifierTypeType::OnlyDeform,

    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_Single,
    /*icon*/ ICON_PHYSICS,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ deform_verts,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ nullptr,
    /*depends_on_time*/ depends_on_time,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ nullptr,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ nullptr,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
};

}  // namespace blender
