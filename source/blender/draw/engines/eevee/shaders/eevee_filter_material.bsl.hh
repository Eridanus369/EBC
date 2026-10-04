/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * NPR Filter material graph evaluation entry point.
 *
 * The scene-level filter graph (`scene.eevee.filter_graph`) is evaluated as a series of
 * world-geometry material passes (MAT_PIPE_FILTER + MAT_GEOM_WORLD). Each filter material
 * renders all of its Filter Output nodes into the `filter_graph_output_img` layer array,
 * one layer per output (see eevee_filter_material_lib.bsl.hh for the helpers and
 * eevee_filter_graph.bsl.hh for the stitching passes).
 */

#pragma once

#include "infos/eevee_geom_infos.hh"
#include "infos/eevee_nodetree_infos.hh"

#include "draw_view.bsl.hh"
#include "eevee_attributes_world_lib.glsl"
#include "eevee_colorspace_lib.bsl.hh"
#include "eevee_defines.hh"
#include "eevee_filter_material_lib.bsl.hh"
#include "eevee_filter_material_shared.hh"
#include "eevee_nodetree_frag_lib.glsl"
#include "eevee_pipeline.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_surf_common.bsl.hh"
#include "eevee_uniform.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_utildefines.bsl.hh"

namespace eevee {

/* Defined by the generated material sources. Writes every filter output to its own layer
 * of `filter_graph_output_img`. */
void nodetree_filter_outputs(int2 frag_texel);

/* -------------------------------------------------------------------- */
/** \name Filter material entry point
 * \{ */

struct FilterMaterialFragOut {
  [[frag_color(0)]] float4 out_color;
};

[[fragment]]
void eevee_filter_material([[resource_table]] PipelineConstants & /*pipe*/,
                           [[resource_table]] FilterMaterial &srt,
                           [[resource_table]] const Uniform &uni,
                           [[resource_table]] const UtilityTexture & /*util_tx*/,
                           [[resource_table]] const draw::View &views,
                           [[frag_coord]] const float4 frag_co,
                           [[out]] FilterMaterialFragOut &frag_out)
{
  FRAGMENT_SHADER_CREATE_INFO(eevee_geom_iface_info);

  const ViewMatrices view = views.get(0);
  init_globals(uni, view, true);
  g_data.N = view.normal_view_to_world(view.view_incident_vector(interp.P));
  g_data.Ni = g_data.N;
  g_data.Ng = g_data.N;
  g_data.P = -g_data.N;
  attrib_load(WorldPoint{g_data.P});

  /* Generated code. Stores each filter output into `filter_graph_output_img`. */
  nodetree_filter_outputs(int2(frag_co.xy));

  /* The framebuffer has a dummy color attachment. Actual result goes to the image. */
  frag_out.out_color = float4(0.0f);
}

/** \} */

}  // namespace eevee
