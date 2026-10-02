/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Evaluate an Eevee light data-block node tree at volume froxel positions.
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_light_shader_common.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_uniform.bsl.hh"
#include "eevee_volume_lib.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"

namespace eevee::light_shader {


struct VolumeData {
  [[push_constant]] const int light_index;
  [[image(0, read_write, SFLOAT_32_32_32_32)]] image2DArray out_light_shader_img;
};

[[compute, local_size(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]]
void light_shader_volume_comp([[resource_table]] VolumeData &data,
                              [[resource_table]] const Uniform &uni,
                              [[resource_table]] const Sampling &sampling,
                              [[resource_table]] const draw::View &views,
                              [[global_invocation_id]] const uint3 global_id)
{
  const int3 froxel = int3(global_id);

  if (any(greaterThanEqual(froxel, uni.uniform_buf.volumes.tex_size))) {
    return;
  }

  light_shader_globals_init();

  const ViewMatrices view = views.get(0);

  const float offset = sampling.rng_1D_get(SAMPLING_VOLUME_W);
  const float jitter = volume_froxel_jitter(froxel.xy, offset);
  const float3 uvw = (float3(froxel) + float3(0.5f, 0.5f, 0.5f - jitter)) *
                     uni.uniform_buf.volumes.inv_tex_size;
  const float3 vP = volume_jitter_to_view(uni, view, uvw);
  const float3 P = view.point_view_to_world(vP);

  g_data.P = P;
  g_data.N = g_data.Ni = view.world_incident_vector(P);
  g_data.Ng = g_data.N;
  g_data.ray_length = distance(P, view.position());

  const int layer = data.light_index * uni.uniform_buf.volumes.tex_size.z + froxel.z;
  imageStoreFast(data.out_light_shader_img,
                 int3(froxel.xy, layer),
                 light_shader_result_clamp(::nodetree_light_shader()));
}

}  // namespace eevee::light_shader

PipelineCompute eevee_light_shader_volume(eevee::light_shader::light_shader_volume_comp);
