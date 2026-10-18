/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.hh"

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) readonly buffer PhotonBuffer {
  Photon photons[];
};

layout(rgba16f, binding = 1) uniform image2D caustic_color_img;
layout(r16f, binding = 2) uniform image2D caustic_count_img;

layout(std430, binding = 3) readonly buffer LevelDataBuffer {
  CausticLevelData level_data[];
};

[[push_constant]] int level_idx;
[[push_constant]] int photon_count;

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) {
    return;
  }

  Photon p = photons[idx];
  if (dot(p.power, p.power) < 1e-10) {
    return;
  }

  CausticLevelData level = level_data[level_idx];
  
  /* Project world position into the caustic map's 2D domain. */
  float3 bmin = level.min_bound;
  float3 bmax = level.max_bound;
  
  /* Guard against zero extents. */
  float3 extent = max(bmax - bmin, float3(1e-6));
  float3 uv = (p.position - bmin) / extent;
  
  if (any(lessThan(uv, float3(0.0))) || any(greaterThan(uv, float3(1.0)))) {
    return;
  }

  int2 texel = int2(uv.x * float(level.resolution), uv.y * float(level.resolution));
  texel = clamp(texel, int2(0), int2(level.resolution - 1));
  
  /* Offset Y by level_idx to write into the stacked texture. */
  int y_offset = level_idx * level.resolution;
  int2 global_texel = int2(texel.x, texel.y + y_offset);
  
  /* Atomically accumulate color and count. */
  float3 current_color = imageLoad(caustic_color_img, global_texel).rgb;
  imageStore(caustic_color_img, global_texel, float4(current_color + p.power, 1.0));
  
  float current_count = imageLoad(caustic_count_img, global_texel).r;
  imageStore(caustic_count_img, global_texel, float4(current_count + 1.0, 0.0, 0.0, 0.0));
}
