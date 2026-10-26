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

  /* 这里对应你场景里真实的玻璃球中心与半径 */
  float3 sphere_center = float3(0.0f, 0.0f, 0.0f);
  float sphere_radius = 1.0f;
  
  bool inside_glass = false;

  /* 最多穿透/反射 4 次 */
  for (int bounce = 0; bounce < 4; bounce++) {
    float3 oc = ray_pos - sphere_center;
    float b = dot(oc, ray_dir);
    float c = dot(oc, oc) - sphere_radius * sphere_radius;
    float h = b * b - c;
    
    /* 没有撞到球，打向地面 z = -2.5 */
    if (h < 0.0f) {
      if (ray_dir.z < 0.0f) {
        float t_ground = (-2.5f - ray_pos.z) / ray_dir.z;
        ray_pos += ray_dir * t_ground;
      }
      break; 
    }
    
    /* 求出交点 */
    float t = -b - sqrt(h);
    if (inside_glass) {
      t = -b + sqrt(h);
    }
    
    float3 hit_pos = ray_pos + ray_dir * t;
    float3 hit_normal = normalize(hit_pos - sphere_center);
    ray_pos = hit_pos;

    /* 关键：折射法线翻转 */
    float cos_i = dot(ray_dir, hit_normal);
    if (cos_i > 0.0f) {
      hit_normal = -hit_normal;
    }

    /* 柯西色散 */
    float3 cauchy = float3(1.5f, 0.15f, 0.0f);
    float eta_glass = spectral_cauchy_ior(p.wavelength, cauchy);
    
    float eta = inside_glass ? eta_glass : (1.0f / eta_glass);
    float3 refr_dir = refract(ray_dir, hit_normal, eta);

    if (dot(refr_dir, refr_dir) < 1e-6f) {
      /* 全反射 */
      ray_dir = reflect(ray_dir, hit_normal);
    } else {
      ray_dir = refr_dir;
      inside_glass = !inside_glass;
    }
    
    ray_pos += ray_dir * 0.01f;
  }

  p.position = ray_pos;
  srt.photons[idx] = p;
}

} // namespace eevee::photon

PipelineCompute eevee_photon_trace(eevee::photon::trace);
