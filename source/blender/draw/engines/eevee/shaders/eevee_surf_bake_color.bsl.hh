/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Eevee UV-space color baking.
 *
 * Evaluates the normal Eevee GPUMaterial graph for a mesh surface and writes the local material
 * color into a bake target. Screen-space inputs and final view post-processing are rejected by
 * the bake callback before this shader runs.
 */
#pragma once

#include "infos/eevee_geom_infos.hh"
#include "infos/eevee_nodetree_infos.hh"

#include "draw_curves_lib.glsl" /* IWYU pragma: export. For nodetree functions. */
#include "draw_view.bsl.hh"     /* IWYU pragma: export. For nodetree functions. */
#include "eevee_forward_lib.bsl.hh"
#include "eevee_light_data.bsl.hh"
#include "eevee_light_iter.bsl.hh"
#include "eevee_lightprobe.bsl.hh"
#include "eevee_nodetree_frag_lib.glsl"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_surf_common.bsl.hh"
#include "eevee_thickness_lib.bsl.hh"

namespace eevee {

struct SurfaceBakeColor {
  [[legacy_info]] ShaderCreateInfo draw_view_culling;
  [[legacy_info]] ShaderCreateInfo eevee_geom_iface_info;
};

struct SurfaceBakeColorFragOut {
  [[frag_color(0)]] float4 out_color;
};

}  // namespace eevee

uint bake_resource_id_get()
{
  auto &interp_flat = interface_get(eevee_geom_iface_info, interp_flat);
  draw::ID id{interp_flat.resource_id_raw};
  return id.resource_id<1>();
}

uint bake_view_id_get()
{
  auto &interp_flat = interface_get(eevee_geom_iface_info, interp_flat);
  draw::ID id{interp_flat.resource_id_raw};
  return id.view_id<1>();
}

void bake_init_globals()
{
  [[resource_table]] const eevee::Uniform &uni = resource_table_get(eevee::Uniform);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  auto &interp = interface_get(eevee_geom_iface_info, interp);
  const ViewMatrices view = views.get(bake_view_id_get());

  g_data.P = interp.P;
  g_data.Ni = interp.N;
  g_data.N = safe_normalize(interp.N);
  g_data.Ng = g_data.N;
  g_data.is_strand = false;
  g_data.hair_diameter = 0.0f;
  g_data.hair_strand_id = 0;
  g_data.ray_type = uni.pipeline_buf.ray_type;
  g_data.ray_depth = 0.0f;
  g_data.ray_length = distance(g_data.P, view.position());
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);

  g_data.N = gl_FrontFacing ? g_data.N : -g_data.N;
  g_data.Ni = gl_FrontFacing ? g_data.Ni : -g_data.Ni;
  g_data.Ng = safe_normalize(cross(gpu_dfdx(g_data.P), gpu_dfdy(g_data.P)));
  if (uni.pipeline_buf.is_main_view_inverted) {
    g_data.Ng = -g_data.Ng;
  }
}

float4 bake_closure_to_rgba()
{
  [[resource_table]] const eevee::Sampling &sampling = resource_table_get(eevee::Sampling);
  [[resource_table]] const UtilityTexture &util_tx = resource_table_get(UtilityTexture);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);

  float3 radiance, transmittance;
  eevee::forward_lighting_eval(
      views.get(0), bake_resource_id_get(), g_thickness, gl_FragCoord.xy, radiance, transmittance);

  /* Reset for the next closure tree, matching the regular forward path. */
  float noise = util_tx.fetch(gl_FragCoord.xy, UTIL_BLUE_NOISE_LAYER).r;
  float closure_rand = fract(noise + sampling.rng_1D_get(SAMPLING_CLOSURE));
  closure_weights_reset(closure_rand);
  g_closure_reflection_bin = true;
  g_bake_principled_depth = 0;

  return float4(radiance, saturate(1.0f - average(transmittance)));
}

float4 closure_to_rgba(Closure /*cl_unused*/)
{
  return bake_closure_to_rgba();
}

[[fragment]] [[early_fragment_tests]]
void surf_bake_color([[resource_table]] PipelineConstants & /*pipe*/,
                     [[resource_table]] eevee::SurfaceBakeColor & /*srt*/,
                     [[resource_table]] LightEvalIterator & /*lights*/,
                     [[resource_table]] LightprobeRenderData & /*lightprobes*/,
                     [[resource_table]] LightprobePlaneRenderData & /*lightprobe_planes*/,
                     [[resource_table]] const draw::View & /*views*/,
                     [[resource_table]] const draw::Model & /*models*/,
                     [[resource_table]] const draw::Infos & /*infos*/,
                     [[resource_table]] const UnifiedVolumeData & /*volumes*/,
                     [[resource_table]] const eevee::Uniform & /*uni*/,
                     [[resource_table]] const eevee::Sampling & /*sampling*/,
                     [[resource_table]] const UtilityTexture & /*util_tx*/,
                     [[resource_table]] const ::gbuffer::PackParameters & /*gbuf_params*/,
                     [[frag_coord]] const float4 /*frag_co*/,
                     [[out]] eevee::SurfaceBakeColorFragOut &frag_out,
                     [[front_facing]] const bool /*front_face*/)
{
  FRAGMENT_SHADER_CREATE_INFO(eevee_geom_iface_info);

  bake_init_globals();
  fragment_displacement();

  float closure_rand = 0.0f;
  {
    [[resource_table]] const eevee::Sampling &sampling = resource_table_get(eevee::Sampling);
    [[resource_table]] const UtilityTexture &util_tx = resource_table_get(UtilityTexture);
    float noise = util_tx.fetch(gl_FragCoord.xy, UTIL_BLUE_NOISE_LAYER).r;
    closure_rand = fract(noise + sampling.rng_1D_get(SAMPLING_CLOSURE));
  }

  g_thickness = Thickness::from(nodetree_thickness(), thickness_mode);

  closure_weights_reset(closure_rand);
  g_closure_reflection_bin = true;
  g_bake_principled_depth = 0;

  nodetree_surface(closure_rand);

  float4 shaded = bake_closure_to_rgba();
  frag_out.out_color = shaded;
}
