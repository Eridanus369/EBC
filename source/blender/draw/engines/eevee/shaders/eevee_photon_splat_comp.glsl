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
  
  float2 uv = p.position.xy * 0.5f + 0.5f; 
  
  /* 不丢弃，强制钳制在 [0,1] 范围内 */
  uv = clamp(uv, 0.0f, 1.0f);

  int2 texel = int2(uv * 512.0f);
  
  /* 真实的波长转 RGB，乘 50000 放大亮度，确保肉眼绝对可见 */
  float3 cmf = cie_1931_cmf(p.wavelength);
  float3 rgb = max(cmf, float3(0.0f)) * 50000.0f;
  
  /* 直接覆盖写入 */
  imageStore(caustic_color_img, texel, float4(rgb, 1.0f));
}
