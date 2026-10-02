/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Evaluate an Eevee light data-block node tree for a UV-space color bake surface.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_light_shader_common.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

namespace eevee::light_shader {


struct BakeData {
  [[sampler(HIZ_TEX_SLOT)]] sampler2D bake_light_shader_position_tx;
  [[sampler(PREPASS_NORMAL_TEX_SLOT)]] sampler2D bake_light_shader_normal_tx;
};

[[fragment]]
void light_shader_bake_frag([[resource_table]] const BakeData &data,
                            [[resource_table]] const draw::View &views,
                            [[frag_coord]] const float4 frag_co,
                            [[in]] const VertOut v_out,
                            [[out]] FragOut &frag_out)
{
  light_shader_globals_init();
  UNUSED_VARS(v_out);

  const int2 texel = int2(frag_co.xy);
  const int2 extent = textureSize(data.bake_light_shader_position_tx, 0);
  if (any(greaterThanEqual(texel, extent))) {
    frag_out.out_light_shader = float4(1.0f);
    return;
  }

  const float4 position = texelFetch(data.bake_light_shader_position_tx, texel, 0);
  const float4 normal = texelFetch(data.bake_light_shader_normal_tx, texel, 0);
  if (position.a <= 0.0f || normal.a <= 0.0f ||
      !any(greaterThan(abs(normal.xyz), float3(0.0f))))
  {
    frag_out.out_light_shader = float4(1.0f);
    return;
  }

  const ViewMatrices view = views.get(0);
  g_data.P = position.xyz;
  g_data.N = g_data.Ni = normalize(normal.xyz);
  g_data.Ng = g_data.N;
  g_data.ray_length = distance(position.xyz, view.position());

  ::attrib_load(WorldPoint{g_data.P});

  frag_out.out_light_shader = light_shader_result_clamp(::nodetree_light_shader());
}

}  // namespace eevee::light_shader

PipelineGraphic eevee_light_shader_bake(eevee::light_shader::fullscreen_vert,
                                        eevee::light_shader::light_shader_bake_frag);
