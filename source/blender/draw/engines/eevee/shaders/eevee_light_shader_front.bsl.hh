/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Evaluate a screen-space direct-light modifier for forward and Shader to RGB passes.
 *
 * This runs before the GBuffer exists, so it reconstructs the front-most visible surface from
 * the prepass depth and normal buffers.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_hiz.bsl.hh"
#include "eevee_light_shader_common.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

namespace eevee::light_shader {


struct FrontData {
  [[sampler(PREPASS_NORMAL_TEX_SLOT)]] sampler2D prepass_normal_tx;
};

[[fragment]]
void light_shader_front_frag([[resource_table]] const HiZ &hiz,
                             [[resource_table]] const FrontData &data,
                             [[resource_table]] const draw::View &views,
                             [[frag_coord]] const float4 frag_co,
                             [[in]] const VertOut v_out,
                             [[out]] FragOut &frag_out)
{
  light_shader_globals_init();

  const int2 texel = int2(frag_co.xy);
  const int2 extent = textureSize(data.prepass_normal_tx, 0);
  if (any(greaterThanEqual(texel, extent))) {
    frag_out.out_light_shader = float4(1.0f);
    return;
  }

  const float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
  if (depth == 1.0f) {
    frag_out.out_light_shader = float4(1.0f);
    return;
  }

  const float3 packed_normal = texelFetch(data.prepass_normal_tx, texel, 0).rgb;
  if (!any(greaterThan(packed_normal, float3(0.0f)))) {
    frag_out.out_light_shader = float4(1.0f);
    return;
  }

  const ViewMatrices view = views.get(0);
  const float3 P = view.point_screen_to_world(float3(v_out.screen_uv, depth));
  const float3 N = normalize(packed_normal * 2.0f - 1.0f);

  g_data.P = P;
  g_data.N = g_data.Ni = N;
  g_data.Ng = N;
  g_data.ray_length = distance(P, view.position());

  frag_out.out_light_shader = light_shader_result_clamp(nodetree_light_shader());
}

}  // namespace eevee::light_shader

PipelineGraphic eevee_light_shader_front(eevee::light_shader::fullscreen_vert,
                                         eevee::light_shader::light_shader_front_frag);
