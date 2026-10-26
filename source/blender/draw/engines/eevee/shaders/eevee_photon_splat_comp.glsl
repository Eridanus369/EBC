/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_splat)

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

/* Fixed-point scale of the integer accumulation targets. */
#define PHOTON_FIXED_SCALE 4096.0
/* Artistic gain: a single photon only carries `light_power / photon_count`. */
#define PHOTON_GAIN 2.0

void main() {
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  Photon p = photons[idx];

  /* 使用真实的相机矩阵投影，而不是硬编码 */
  float4 clip_pos = winmat * viewmat * float4(p.position, 1.0f);
  if (clip_pos.w <= 1e-6f) { return; }
  
  float2 ndc = clip_pos.xy / clip_pos.w;
  float2 uv = ndc * 0.5f + 0.5f; 
  
  /* 如果投影在屏幕外，丢弃 */
  if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) { return; }

  int2 texel = int2(uv * float(imageSize(caustic_accum_r_img).x));

  float3 rgb = xyz_to_rgb(cie_1931_cmf(p.wavelength)) * p.power;
  uint3 fixed_rgb = uint3(max(rgb * (PHOTON_GAIN * PHOTON_FIXED_SCALE), float3(0.0f)));

  /* Overlapping photons have to be summed atomically. */
  imageAtomicAdd(caustic_accum_r_img, texel, fixed_rgb.x);
  imageAtomicAdd(caustic_accum_g_img, texel, fixed_rgb.y);
  imageAtomicAdd(caustic_accum_b_img, texel, fixed_rgb.z);
}
