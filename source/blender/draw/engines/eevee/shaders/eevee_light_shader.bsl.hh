/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Evaluate an Eevee light data-block node tree as a screen-space direct-light modifier.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_gbuffer_read.bsl.hh"
#include "eevee_hiz.bsl.hh"
#include "eevee_light_shader_common.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_fullscreen.bsl.hh"

namespace eevee::light_shader {


[[fragment]]
void light_shader_frag([[resource_table]] const HiZ &hiz,
                       [[resource_table]] const gbuffer::Reader &reader,
                       [[resource_table]] const draw::View &views,
                       [[frag_coord]] const float4 frag_co,
                       [[in]] const VertOut v_out,
                       [[out]] FragOut &frag_out)
{
  light_shader_globals_init();

  const int2 texel = int2(frag_co.xy);
  const float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
  const gbuffer::Layers gbuf = reader.read_layers(texel);

  if (gbuf.has_no_closure()) {
    frag_out.out_light_shader = float4(1.0f);
    return;
  }

  const ViewMatrices view = views.get(0);
  const float3 P = view.point_screen_to_world(float3(v_out.screen_uv, depth));

  g_data.P = P;
  g_data.N = g_data.Ni = gbuf.surface_N();
  g_data.Ng = gbuf.header.geometry_normal(g_data.N);
  g_data.ray_length = distance(P, view.position());

  frag_out.out_light_shader = light_shader_result_clamp(nodetree_light_shader());
}

}  // namespace eevee::light_shader

PipelineGraphic eevee_light_shader(eevee::light_shader::fullscreen_vert,
                                   eevee::light_shader::light_shader_frag);
