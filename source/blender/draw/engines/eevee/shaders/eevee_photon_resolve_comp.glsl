/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_resolve)

#include "gpu_shader_compat.hh"

/* Fixed-point scale used by the photon splat pass. Must match `PHOTON_FIXED_SCALE`. */
#define PHOTON_FIXED_SCALE 4096.0

void main() {
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (texel.x >= imageSize(caustic_accum_r_img).x || texel.y >= imageSize(caustic_accum_r_img).y) {
    return;
  }

  uint3 accum = uint3(imageLoad(caustic_accum_r_img, texel).r,
                      imageLoad(caustic_accum_g_img, texel).r,
                      imageLoad(caustic_accum_b_img, texel).r);
  float3 color = float3(accum) / PHOTON_FIXED_SCALE;
  /* Clamp fireflies so a few stray photons cannot blow out the whole patch. */
  color = min(color, float3(16.0f));

  /* Level 0 occupies the bottom quarter of the stacked caustic map. */
  imageStore(caustic_color_img, texel, float4(color, 1.0f));
}
