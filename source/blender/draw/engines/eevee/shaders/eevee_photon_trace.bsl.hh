/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.hh"
#include "eevee_spectral_lib.glsl"

namespace eevee::photon {

struct Resources {
  [[storage(0, read_write)]] Photon (&photons)[];
  [[sampler(0)]] sampler2D hiz_tx;
  [[sampler(1)]] usampler2DArray gbuf_header_tx;
  [[sampler(2)]] sampler2DArray gbuf_normal_tx;
};

struct PushConstants {
  [[push_constant]] int photon_count;
  [[push_constant]] float4x4 viewmat;
  [[push_constant]] float4x4 winmat;
  [[push_constant]] float4x4 viewinv;
};

[[compute, local_size(256, 1, 1)]]
void trace([[resource_table]] Resources &srt,
           [[resource_table]] PushConstants &pc,
           [[work_group_id]] const uint3 group_id,
           [[local_invocation_id]] const uint3 local_id)
{
  uint idx = (group_id.x * 256) + local_id.x;
  if (idx >= uint(pc.photon_count)) { return; }

  Photon p = srt.photons[idx];

  float3 ray_pos = p.position;
  float3 ray_dir = normalize(p.direction);

  const int max_steps = 64;
  const float step_size = 0.08f;
  float phase = fract(float(idx) * 0.61803398875f);
  ray_pos += ray_dir * (phase * step_size);

  bool hit = false;
  
  /* 最多允许 2 次表面交互（比如进入玻璃、射出玻璃） */
  for (int bounce = 0; bounce < 2; bounce++) {
    float prev_dist = -1.0f;
    float3 prev_pos = ray_pos;
    
    for (int i = 0; i < max_steps; ++i) {
      float3 next_pos = ray_pos + ray_dir * step_size;
      float4 clip_pos = pc.winmat * pc.viewmat * float4(next_pos, 1.0f);
      
      if (clip_pos.w > 1e-6f) {
        float2 uv = (clip_pos.xy / clip_pos.w) * 0.5f + 0.5f;
        if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f) {
          float ray_depth = (clip_pos.z / clip_pos.w) * 0.5f + 0.5f;
          float scene_depth = texture(srt.hiz_tx, uv).r;
          
          if (ray_depth > scene_depth) {
            /* 命中表面，读取 Gbuffer 法线 */
            int2 texel = int2(uv * float2(textureSize(srt.hiz_tx, 0)));
            float3 vN = texelFetch(srt.gbuf_normal_tx, int3(texel, 0), 0).xyz;
            vN = vN * 2.0f - 1.0f;
            float3 N = normalize((pc.viewinv * float4(vN, 0.0f)).xyz);

            /* 柯西色散 (玻璃) */
            float3 cauchy = float3(1.5f, 0.05f, 0.0f);
            float eta = spectral_cauchy_ior(p.wavelength, cauchy);

            /* 折射 */
            float3 refr_dir = refract(ray_dir, N, 1.0f / eta);
            if (dot(refr_dir, refr_dir) > 0.0f) {
              ray_dir = refr_dir;
            } else {
              ray_dir = reflect(ray_dir, N); /* 全反射 */
            }

            /* 步进规避自身表面 */
            ray_pos = prev_pos;
            ray_dir = normalize(ray_dir);
            break;
          }
        }
      }
      
      prev_pos = next_pos;
      ray_pos = next_pos;
      
      if (i == max_steps - 1 || bounce == 1) {
        hit = true; /* 如果最终没穿透，就记录落点 */
      }
    }
    
    if (!hit) {
      break; /* 如果发生了折射，继续下一轮 bounce */
    } else {
      break; /* 已经得到最终落点 */
    }
  }

  p.position = ray_pos;
  srt.photons[idx] = p;
}

} // namespace eevee::photon

PipelineCompute eevee_photon_trace(eevee::photon::trace);
