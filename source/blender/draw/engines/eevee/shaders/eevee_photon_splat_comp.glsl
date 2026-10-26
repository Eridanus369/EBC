/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_splat)

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

#define PHOTON_FIXED_SCALE 4096.0
#define PHOTON_GAIN 12.0
/* Gaussian splat radius in texels. A single texel only receives a handful of photons, so spreading
 * each one over a small kernel is what turns the accumulation into a smooth patch. */
#define SPLAT_RADIUS 2
#define SPLAT_SIGMA 1.2

void main() {
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  Photon p = photons[idx];

  float4 clip_pos = winmat * viewmat * float4(p.position, 1.0f);
  if (clip_pos.w <= 1e-6f) { return; }

  float2 ndc = clip_pos.xy / clip_pos.w;
  float2 uv = ndc * 0.5f + 0.5f;

  if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) { return; }

  /* 将 uv 映射到最底层（level 0） */
  int2 texel = int2(uv * float(imageSize(caustic_accum_r_img).x));
  int2 texel_max = int2(imageSize(caustic_accum_r_img).xy) - 1;

  float3 rgb = xyz_to_rgb(cie_1931_cmf(p.wavelength)) * p.power * PHOTON_GAIN;

  /* 归一化高斯核权重固定，能量守恒（每个光子总权重为 1）。 */
  float weight_sum = 0.0f;
  for (int dy = -SPLAT_RADIUS; dy <= SPLAT_RADIUS; dy++) {
    for (int dx = -SPLAT_RADIUS; dx <= SPLAT_RADIUS; dx++) {
      float2 delta = float2(dx, dy);
      float w = exp(-0.5f * dot(delta, delta) / (SPLAT_SIGMA * SPLAT_SIGMA));
      weight_sum += w;
    }
  }

  for (int dy = -SPLAT_RADIUS; dy <= SPLAT_RADIUS; dy++) {
    for (int dx = -SPLAT_RADIUS; dx <= SPLAT_RADIUS; dx++) {
      int2 t = clamp(texel + int2(dx, dy), int2(0), texel_max);
      float2 delta = float2(dx, dy);
      float w = exp(-0.5f * dot(delta, delta) / (SPLAT_SIGMA * SPLAT_SIGMA)) / weight_sum;
      uint3 fixed_rgb = uint3(max(rgb * (w * PHOTON_FIXED_SCALE), float3(0.0f)));
      imageAtomicAdd(caustic_accum_r_img, t, fixed_rgb.x);
      imageAtomicAdd(caustic_accum_g_img, t, fixed_rgb.y);
      imageAtomicAdd(caustic_accum_b_img, t, fixed_rgb.z);
    }
  }
}
