/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#define CURVATURE_VIEW_REFERENCE_PIXELS 384.0f

float2 curvature_rotate(float2 value, float angle)
{
  float s = sin(angle);
  float c = cos(angle);
  return float2(c * value.x - s * value.y, s * value.x + c * value.y);
}

float curvature_radius_scale(float2 sample_radius_uv, float sample_radius)
{
  return max(length(sample_radius_uv) / max(sample_radius, 1e-8f), 1e-8f);
}

[[node]]
void node_screenspace_curvature(float samples,
                                float sample_radius,
                                float thickness,
                                float3 scale,
                                float &scene_curvature,
                                float &scene_rim)
{
  [[resource_table]] const eevee::Sampling &samp = resource_table_get(eevee::Sampling);
  [[resource_table]] const eevee::HiZ &hiz = resource_table_get(eevee::HiZ);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);

#if defined(GPU_FRAGMENT_SHADER) && \
    (defined(MAT_DEFERRED) || defined(MAT_FORWARD) || defined(NPR_SHADER))
  float2 texel_size = 1.0f / float2(textureSize(hiz.hiz_tx, 0));
  float2 sample_radius_uv = texel_size * sample_radius;
  float2 uvs = gl_FragCoord.xy * texel_size;
  float mid_depth = views.depth_screen_to_view(textureLod(hiz.hiz_tx, uvs, 0.0f).r);
  float clamp_range = 0.001f;
  int n_samples = int(max(samples, 1.0f));
  float i_samples = 64.0f / float(n_samples);

  float angle_offset = samp.rng_1D_get(SAMPLING_TRANSPARENCY);
  float accum = 0.0f;
  float rim_accum = 0.0f;

  for (int r = 0; r < 8; r++) {
    float angle = (float(r) + angle_offset) * 3.1415f * 0.25f * 0.5f;
    float2 offset = curvature_rotate(float2(1.0f, 0.0f), angle) * sample_radius_uv * scale.xy;

    for (int i = 1; i <= n_samples; i++) {
      float sample_offset = float(i) * i_samples;
      float left_depth = views.depth_screen_to_view(
          textureLod(hiz.hiz_tx, uvs + offset * sample_offset, 0.0f).r);
      float right_depth = views.depth_screen_to_view(
          textureLod(hiz.hiz_tx, uvs - offset * sample_offset, 0.0f).r);

      float curve = clamp(left_depth - mid_depth, -clamp_range, clamp_range) +
                    clamp(right_depth - mid_depth, -clamp_range, clamp_range);
      float afac = 1.0f - float(i - 1) / float(n_samples);

      accum += curve * afac * 0.001f;
      rim_accum += min(mid_depth - min(left_depth, right_depth), thickness) * afac;
    }
  }

  scene_curvature = -accum / curvature_radius_scale(sample_radius_uv, sample_radius) * i_samples;
  scene_rim = rim_accum / max(sample_radius, 1e-8f) * clamp_range;
#else
  scene_curvature = 0.0f;
  scene_rim = 0.0f;
#endif
}

[[node]]
void node_screenspace_curvature_view(float samples,
                                     float sample_radius,
                                     float thickness,
                                     float3 scale,
                                     float &scene_curvature,
                                     float &scene_rim)
{
  [[resource_table]] const eevee::Sampling &samp = resource_table_get(eevee::Sampling);
  [[resource_table]] const eevee::HiZ &hiz = resource_table_get(eevee::HiZ);
  [[resource_table]] const eevee::Uniform &uni = resource_table_get(eevee::Uniform);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);

#if defined(GPU_FRAGMENT_SHADER) && \
    (defined(MAT_DEFERRED) || defined(MAT_FORWARD) || defined(NPR_SHADER))
  int2 hiz_size = textureSize(hiz.hiz_tx, 0);
  float2 texel_size = 1.0f / float2(hiz_size);
  float2 camera_uv_radius = float2(sample_radius / CURVATURE_VIEW_REFERENCE_PIXELS);
  float2 screen_uv_radius = camera_uv_radius / max(uni.uniform_buf.camera.uv_scale, float2(1e-8f));
  float2 sample_radius_uv = screen_uv_radius *
                            (float2(uni.uniform_buf.film.render_extent) / float2(hiz_size));
  float2 uvs = gl_FragCoord.xy * texel_size;
  float mid_depth = views.depth_screen_to_view(textureLod(hiz.hiz_tx, uvs, 0.0f).r);
  float clamp_range = 0.001f;
  int n_samples = int(max(samples, 1.0f));
  float i_samples = 64.0f / float(n_samples);

  float angle_offset = samp.rng_1D_get(SAMPLING_TRANSPARENCY);
  float accum = 0.0f;
  float rim_accum = 0.0f;

  for (int r = 0; r < 8; r++) {
    float angle = (float(r) + angle_offset) * 3.1415f * 0.25f * 0.5f;
    float2 offset = curvature_rotate(float2(1.0f, 0.0f), angle) * sample_radius_uv * scale.xy;

    for (int i = 1; i <= n_samples; i++) {
      float sample_offset = float(i) * i_samples;
      float left_depth = views.depth_screen_to_view(
          textureLod(hiz.hiz_tx, uvs + offset * sample_offset, 0.0f).r);
      float right_depth = views.depth_screen_to_view(
          textureLod(hiz.hiz_tx, uvs - offset * sample_offset, 0.0f).r);

      float curve = clamp(left_depth - mid_depth, -clamp_range, clamp_range) +
                    clamp(right_depth - mid_depth, -clamp_range, clamp_range);
      float afac = 1.0f - float(i - 1) / float(n_samples);

      accum += curve * afac * 0.001f;
      rim_accum += min(mid_depth - min(left_depth, right_depth), thickness) * afac;
    }
  }

  scene_curvature = -accum / curvature_radius_scale(sample_radius_uv, sample_radius) * i_samples;
  scene_rim = rim_accum / max(sample_radius, 1e-8f) * clamp_range;
#else
  scene_curvature = 0.0f;
  scene_rim = 0.0f;
#endif
}
