/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

[[node]]
void node_render_info(out float3 frag_coord,
                      out float width,
                      out float height,
                      out float3 resolution,
                      out float current_sample,
                      out float total_samples)
{
  [[resource_table]] const eevee::Uniform &uni = resource_table_get(eevee::Uniform);

  width = float(uni.uniform_buf.film.extent.x);
  height = float(uni.uniform_buf.film.extent.y);
  resolution = float3(width, height, 0.0f);
#ifdef GPU_FRAGMENT_SHADER
  float2 extent = max(float2(width, height), float2(1.0f));
  float2 uv = gl_FragCoord.xy / extent;
  frag_coord = float3(uv * uni.uniform_buf.camera.uv_scale + uni.uniform_buf.camera.uv_bias,
                      gl_FragCoord.z);
#else
  frag_coord = float3(0.0f);
#endif
/* 5.3 SamplingData exposes only dimensions[]; sample counters are not
   * available to material shaders. Fall back to constants. */
  current_sample = 0.0f;
  total_samples = 1.0f;
}
