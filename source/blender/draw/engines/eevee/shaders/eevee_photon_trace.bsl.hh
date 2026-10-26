/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.hh"
#include "eevee_spectral_lib.glsl"

namespace eevee::photon {

struct Resources {
  [[storage(0, read_write)]] Photon (&photons)[];
};

struct PushConstants {
  [[push_constant]] int photon_count;
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

  /* 中心原点放置一个半径 1.0 的玻璃球 */
  float3 sphere_center = float3(0.0f, 0.0f, 0.0f);
  float sphere_radius = 1.0f;
  
  bool inside_glass = false;

  /* 最多反弹 4 次 */
  for (int bounce = 0; bounce < 4; bounce++) {
    float3 oc = ray_pos - sphere_center;
    float b = dot(oc, ray_dir);
    float c = dot(oc, oc) - sphere_radius * sphere_radius;
    float h = b * b - c;
    
    /* 如果没有交点，说明射线飞出去了 */
    if (h < 0.0f) {
      /* 打向地面 (z=-1.5) */
      if (ray_dir.z < 0.0f) {
        float t_ground = (-1.5f - ray_pos.z) / ray_dir.z;
        ray_pos += ray_dir * t_ground;
      }
      break; 
    }
    
    /* 求出交点 */
    float t = -b - sqrt(h);
    if (inside_glass) {
      /* 如果已经在球体内部，需要求正向的交点 */
      t = -b + sqrt(h);
    }
    
    float3 hit_pos = ray_pos + ray_dir * t;
    float3 hit_normal = normalize(hit_pos - sphere_center);
    ray_pos = hit_pos;

    /* 根据介质计算折射率 */
    float3 cauchy = float3(1.5f, 0.15f, 0.0f);
    float eta_glass = spectral_cauchy_ior(p.wavelength, cauchy);
    
    float eta = inside_glass ? eta_glass : (1.0f / eta_glass);
    float3 refr_dir = refract(ray_dir, hit_normal, eta);

    if (dot(refr_dir, refr_dir) < 1e-6f) {
      /* 全内反射 */
      ray_dir = reflect(ray_dir, hit_normal);
    } else {
      /* 正常折射 */
      ray_dir = refr_dir;
      inside_glass = !inside_glass;
    }
    
    /* 将起点推离表面一点点 */
    ray_pos += ray_dir * 0.01f;
  }

  p.position = ray_pos;
  srt.photons[idx] = p;
}

} // namespace eevee::photon

PipelineCompute eevee_photon_trace(eevee::photon::trace);
