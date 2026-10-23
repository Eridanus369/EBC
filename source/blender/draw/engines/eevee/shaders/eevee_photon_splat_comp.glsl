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
  
  /* 将光子的世界坐标 x,y 映射到贴图 UV [0, 1] */
  float2 uv = p.position.xy * 0.5f + 0.5f; 
  
  /* 如果光子跑出贴图范围，就丢弃 */
  if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) { return; }

  int2 texel = int2(uv * float(level_data[level_idx].resolution));
  
  /* 真实的光谱转 RGB，并累加能量 */
  float3 cmf = cie_1931_cmf(p.wavelength);
  float3 rgb = max(cmf, float3(0.0f)) * 0.001f; /* 缩放防止过曝 */
  
  /* 累加写入焦散贴图 */
  float4 prev = imageLoad(caustic_color_img, texel);
  imageStore(caustic_color_img, texel, float4(prev.rgb + p.power * rgb, 1.0f));
}
