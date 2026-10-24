/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_splat)

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

void main() {
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  Photon p = photons[idx];
  
  /* B: 将世界坐标投影到 NDC */
  float4 clip_pos = winmat * viewmat * float4(p.position, 1.0f);
  if (clip_pos.w <= 0.0f) { return; } // 在相机背面
  
  float2 ndc = clip_pos.xy / clip_pos.w;
  float2 uv = ndc * 0.5f + 0.5f; 
  
  if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) { return; }

  int2 texel = int2(uv * 512.0f);
  
  /* A: 真实光谱能量，乘数调整到 100 以防太暗，但不至于纯白过曝 */
  float3 xyz = cie_1931_cmf(p.wavelength);
  float3 rgb = xyz_to_rgb(xyz) * 5.0f; /* 转换成 RGB 并降低能量防止过曝 */ /* 降低亮度防过曝 */
  
  imageStore(caustic_color_img, texel, float4(rgb, 1.0f));
}
