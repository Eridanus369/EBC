/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "infos/eevee_nodetree_infos.hh"

#include "eevee_nodetree_frag_lib.glsl"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_fullscreen.bsl.hh"

/* Generated at runtime by light_create_info_amend. */
float4 nodetree_light_shader();

namespace eevee::light_shader {

struct VertOut {
  [[smooth]] float2 screen_uv;
};

[[vertex]]
void fullscreen_vert([[vertex_id]] const int vert_id,
                     [[position]] float4 &out_position,
                     [[out]] VertOut &v_out)
{
  fullscreen_vertex(vert_id, out_position, v_out.screen_uv);
}

struct FragOut {
  [[frag_color(0)]] float4 out_light_shader;
};

}  // namespace eevee::light_shader

void light_shader_globals_init()
{
  g_data.is_strand = false;
  g_data.hair_diameter = 0.0f;
  g_data.hair_strand_id = 0;
  g_data.ray_type = uchar(RAY_TYPE_CAMERA);
  g_data.ray_depth = 0.0f;
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);
  g_data.curve_T = float3(0.0f);
  g_data.curve_B = float3(0.0f);
  g_data.curve_N = float3(0.0f);
}

float4 light_shader_result_clamp(float4 result)
{
  return float4(max(result.rgb, float3(0.0f)), max(result.a, 0.0f));
}
