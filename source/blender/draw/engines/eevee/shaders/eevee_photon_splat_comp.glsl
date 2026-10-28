/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_splat)

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

#define PHOTON_FIXED_SCALE 4096.0
/* Upper bound of the Gaussian kernel radius, in texels. The actual radius is user controlled so
 * that the splat size can be traded for performance. */
#define SPLAT_RADIUS_MAX 4

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  Photon p = photons[idx];
  /* Only photons that ended their path on an opaque surface contribute to the caustic map. */
  if (p.flags != CAUSTIC_PHOTON_DEPOSITED) { return; }

  /* Reject splats whose receiver faces away from the camera. The caustic map is screen space, so
   * without this test a caustic also shows through the back side of a receiver (a floor looked at
   * from below). `viewmat` maps world to view, so the camera position is `-R^T t`, which is written
   * with dot products because the shader parser does not accept a matrix type cast. */
  float3 cam_pos = -float3(dot(viewmat[0].xyz, viewmat[3].xyz),
                           dot(viewmat[1].xyz, viewmat[3].xyz),
                           dot(viewmat[2].xyz, viewmat[3].xyz));
  if (dot(p.normal, cam_pos - p.position) < 0.0f) { return; }

  float4 clip_pos = winmat * viewmat * float4(p.position, 1.0f);
  if (clip_pos.w <= 1e-6f) { return; }

  float2 ndc = clip_pos.xy / clip_pos.w;
  float2 uv = ndc * 0.5f + 0.5f;
  if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) { return; }

  const int2 accum_size = int2(imageSize(caustic_accum_r_img).xy);
  /* Continuous texel space center: `-0.5` puts the origin on the center of texel 0 so that the
   * kernel is not snapped to the texel grid, which would produce visible stair stepping. */
  const float2 center = uv * float2(accum_size) - 0.5f;
  const int2 texel = int2(floor(center));
  const float2 frac = center - float2(texel);
  const int2 texel_max = accum_size - 1;

  /* Camera space depth of the photon, used by the deferred combine to reject occluded caustics. */
  float cam_dist = length((viewmat * float4(p.position, 1.0f)).xyz);
  uint depth_bits = floatBitsToUint(cam_dist);

  int radius = int(clamp(splat_radius, 0.0f, float(SPLAT_RADIUS_MAX)));
  float sigma = max(float(radius) * 0.6f, 0.5f);

  float3 rgb = spectral_cmf_weight(p.wavelength) * p.power * splat_gain;
  if (abs(rgb.x) + abs(rgb.y) + abs(rgb.z) <= 0.0f) { return; }

  /* The 2D kernel weight sum is the square of the 1D one (separable Gaussian). */
  float row_sum = 0.0f;
  for (int d = -radius; d <= radius; d++) {
    row_sum += exp(-0.5f * float(d * d) / (sigma * sigma));
  }
  float weight_sum = max(row_sum * row_sum, 1.0f);

  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      int2 t = clamp(texel + int2(dx, dy), int2(0), texel_max);
      float2 d = float2(dx, dy) - frac;
      float w = exp(-0.5f * dot(d, d) / (sigma * sigma)) / weight_sum;
      uint3 fixed_rgb = uint3(max(rgb * (w * PHOTON_FIXED_SCALE), float3(0.0f)));
      imageAtomicAdd(caustic_accum_r_img, t, fixed_rgb.x);
      imageAtomicAdd(caustic_accum_g_img, t, fixed_rgb.y);
      imageAtomicAdd(caustic_accum_b_img, t, fixed_rgb.z);
      /* Keep the closest photon distance so occluders in front of the caustic can reject it. */
      imageAtomicMin(caustic_accum_depth_img, t, depth_bits);
    }
  }
}
