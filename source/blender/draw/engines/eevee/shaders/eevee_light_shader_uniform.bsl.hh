/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Evaluate a point-independent Eevee light data-block node tree once per light.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_light_data.bsl.hh"
#include "eevee_light_shader_common.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

namespace eevee::light_shader {


struct UniformData {
  [[push_constant]] const int light_index;
  [[storage(LIGHT_SHADER_UNIFORM_BUF_SLOT, write)]] float4 (&out_light_shader_buf)[];
};

[[compute, local_size(1)]]
void light_shader_uniform_comp([[resource_table]] const LightRenderData &lrd,
                               [[resource_table]] const UniformData &data,
                               [[resource_table]] const draw::View &views)
{
  const LightData light = lrd.light_buf[data.light_index];

  light_shader_globals_init();

  const ViewMatrices view = views.get(0);
  const float3 P = light.position();
  g_data.P = P;
  g_data.N = g_data.Ni = float3(0.0f, 0.0f, 1.0f);
  g_data.Ng = g_data.N;
  g_data.ray_length = distance(P, view.position());

  data.out_light_shader_buf[data.light_index] = light_shader_result_clamp(
      nodetree_light_shader());
}

}  // namespace eevee::light_shader

PipelineCompute eevee_light_shader_uniform(eevee::light_shader::light_shader_uniform_comp);
