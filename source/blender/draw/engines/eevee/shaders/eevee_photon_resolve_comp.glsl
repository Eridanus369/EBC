/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_resolve)

#include "gpu_shader_compat.hh"

/* Fixed-point scale used by the photon splat pass. Must match `PHOTON_FIXED_SCALE`. */
#define PHOTON_FIXED_SCALE 4096.0
/* Highlight roll-off of the resolved caustic. The energy of a photons is deposited as a radiometric
 * power, so a focused caustic can be orders of magnitude brighter than the direct lighting of the
 * surface it lands on. */
#define CAUSTIC_KNEE 6.0
#define CAUSTIC_MAX 8.0

void main() {
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (texel.x >= imageSize(caustic_accum_r_img).x || texel.y >= imageSize(caustic_accum_r_img).y) {
    return;
  }

  /* A single photon covers one or two texels of the map, which is sub pixel in the render: the
   * deposits would read as isolated aliased dots instead of a continuous caustic. Spread them with a
   * separable 1-2-1 blur (energy preserving, so the total caustic power is unchanged). */
  const int2 max_texel = int2(imageSize(caustic_accum_r_img).xy) - int2(1, 1);
  float3 accum = float3(0.0f);
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int2 t = clamp(texel + int2(dx, dy), int2(0, 0), max_texel);
      float w = ((dx == 0) ? 2.0f : 1.0f) * ((dy == 0) ? 2.0f : 1.0f);
      accum.x += float(imageLoad(caustic_accum_r_img, t).r) * w;
      accum.y += float(imageLoad(caustic_accum_g_img, t).r) * w;
      accum.z += float(imageLoad(caustic_accum_b_img, t).r) * w;
    }
  }
  float3 color = accum / (PHOTON_FIXED_SCALE * 16.0f);
  /* Hue preserving highlight roll-off. A hard per channel clamp drives every channel towards the
   * same value, so a caustic that is a few times brighter than the calibrated level collapses into a
   * flat white slab and loses all of its spectral color. This knee compresses the highlight while
   * keeping the channel ratios, so the dispersion fringes stay readable however far the gain is
   * pushed. Values well below the knee remain essentially linear. */
  color = color / (float3(1.0f) + color / CAUSTIC_KNEE);
  color = min(color, float3(CAUSTIC_MAX));

  /* Alpha stores the camera distance of the closest photon in this texel. Zero means there is no
   * caustic here. The value is clamped to stay within the half float range. */
  uint depth_bits = imageLoad(caustic_accum_depth_img, texel).r;
  float hit_dist = (depth_bits == 0x7F800000u) ? 0.0f : min(uintBitsToFloat(depth_bits), 60000.0f);

  /* Level 0 occupies the bottom quarter of the stacked caustic map. */
  float4 out_color = float4(color, hit_dist);
  if (caustic_sample_index > 0u) {
    /* Progressive averaging: a single frame only carries a few photons per texel, which shows up as
     * sparse colored speckles. Averaging the samples of the TAA sequence over the same map removes
     * the speckle and converges to the actual caustic irradiance. */
    float4 prev = imageLoad(caustic_color_img, texel);
    float weight = 1.0f / float(caustic_sample_index + 1u);
    float3 avg = mix(prev.rgb, color, weight);
    /* Keep the closest photon of the sequence for the occlusion test of the combine pass. */
    float depth_avg = prev.a;
    if (hit_dist > 0.0f) {
      depth_avg = (prev.a > 0.0f) ? min(prev.a, hit_dist) : hit_dist;
    }
    out_color = float4(avg, depth_avg);
  }
  imageStore(caustic_color_img, texel, out_color);
}
