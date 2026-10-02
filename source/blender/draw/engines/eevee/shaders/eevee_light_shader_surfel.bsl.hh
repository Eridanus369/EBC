/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Evaluate an Eevee light data-block node tree at Volume Probe bake surfel positions.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_light_shader_common.bsl.hh"
#include "eevee_surfel.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

namespace eevee::light_shader {


struct SurfelCompute {
  [[push_constant]] const int light_index;
  [[storage(LIGHT_SHADER_SURFEL_BUF_SLOT, write)]] float4 (&out_light_shader_buf)[];
  [[resource_table]] srt_t<SurfelData> surfels_data;
};

[[compute, local_size(SURFEL_GROUP_SIZE)]]
void light_shader_surfel_comp([[resource_table]] SurfelCompute &data,
                              [[resource_table]] const draw::View &views,
                              [[global_invocation_id]] const uint3 global_id)
{
  [[resource_table]] const SurfelData &surfels = data.surfels_data;

  const int index = int(global_id.x);
  if (index >= int(surfels.capture_info_buf.surfel_len)) {
    return;
  }

  const Surfel surfel = surfels.surfel_buf[index];

  light_shader_globals_init();

  const ViewMatrices view = views.get(0);
  const float3 P = surfel.position;
  g_data.P = P;
  g_data.N = g_data.Ni = surfel.normal;
  g_data.Ng = surfel.normal;
  g_data.ray_length = distance(P, view.position());

  data.out_light_shader_buf[data.light_index * int(surfels.capture_info_buf.surfel_len) + index] =
      light_shader_result_clamp(nodetree_light_shader());
}

}  // namespace eevee::light_shader

PipelineCompute eevee_light_shader_surfel(eevee::light_shader::light_shader_surfel_comp);
