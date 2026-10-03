/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR: Screen-space outline post-process passes.
 *
 * Detect -> (Freestyle edge) -> Factor blur -> JFA init -> JFA steps -> Resolve.
 *
 * Material shaders write per-pixel outline parameters into outline_color_tx / outline_info_tx.
 * The detect pass turns depth / normal / id discontinuities into seed pixels, a jump-flood
 * flood fills the nearest-seed coordinate field, and the resolve pass paints line coverage. */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_defines.hh"
#include "eevee_gbuffer_read.bsl.hh"
#include "eevee_outline.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "gpu_shader_fullscreen.bsl.hh"
#include "gpu_shader_utildefines.bsl.hh"

namespace eevee::outline {

/* -------------------------------------------------------------------- */
/** \name Fullscreen vertex
 * \{ */

[[vertex]]
void fullscreen_vert([[vertex_id]] const int vert_id, [[position]] float4 &out_position)
{
  fullscreen_vertex(vert_id, out_position);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shared helpers
 * \{ */

float4 source_color_fetch(sampler2D outline_color_tx, int2 texel)
{
  return texelFetch(outline_color_tx, texel, 0);
}

uint4 source_info_fetch(usampler2D outline_info_tx, int2 texel)
{
  return texelFetch(outline_info_tx, texel, 0);
}

float screen_depth_fetch(sampler2DDepth depth_tx, int2 texel)
{
  return reverse_z::read(texelFetch(depth_tx, texel, 0).r);
}

/* Malt-style width factor: returns a factor in [0, edge_width] used to modulate the drawn radius.
 * - strength <= threshold : 0 (no edge drawn)
 * - range <= 0            : edge_width (hard on/off, full width as soon as threshold is crossed)
 * - range > 0             : linear taper (strength - threshold) / range * edge_width, clamped to
 *                           edge_width once strength >= threshold + range. */
float edge_width_factor(float strength, float threshold, float range, float edge_width)
{
  if (strength <= threshold) {
    return 0.0f;
  }
  if (range <= 0.0f) {
    return edge_width;
  }
  const float t = saturate((strength - threshold) / range);
  return t * edge_width;
}

float3 screen_to_view(const ViewMatrices &view, int2 extent, int2 texel, float screen_depth)
{
  const float2 uv = (float2(texel) + 0.5f) / float2(extent);
  return view.point_screen_to_view(float3(uv, screen_depth));
}

float3 reconstruct_normal(const ViewMatrices &view,
                          sampler2DDepth depth_tx,
                          int2 texel,
                          int2 extent,
                          float3 current_view_direction)
{
  const int2 texel_min = int2(0);
  const int2 texel_max = extent - int2(1);

  const float3 t0 = screen_to_view(view, extent, texel, screen_depth_fetch(depth_tx, texel));
  const int2 x1_texel = clamp(texel + int2(-1, 0), texel_min, texel_max);
  const int2 x2_texel = clamp(texel + int2(1, 0), texel_min, texel_max);
  const int2 y1_texel = clamp(texel + int2(0, -1), texel_min, texel_max);
  const int2 y2_texel = clamp(texel + int2(0, 1), texel_min, texel_max);
  const float3 x1 = screen_to_view(view, extent, x1_texel, screen_depth_fetch(depth_tx, x1_texel));
  const float3 x2 = screen_to_view(view, extent, x2_texel, screen_depth_fetch(depth_tx, x2_texel));
  const float3 y1 = screen_to_view(view, extent, y1_texel, screen_depth_fetch(depth_tx, y1_texel));
  const float3 y2 = screen_to_view(view, extent, y2_texel, screen_depth_fetch(depth_tx, y2_texel));

  const float x_distance_1 = abs(x1.z - t0.z);
  const float x_distance_2 = abs(x2.z - t0.z);
  const float y_distance_1 = abs(y1.z - t0.z);
  const float y_distance_2 = abs(y2.z - t0.z);

  const float3 x = (x_distance_1 < x_distance_2) ? x1 : x2;
  const float3 y = (y_distance_1 < y_distance_2) ? y1 : y2;

  float3 n = normalize(cross(x - t0, y - t0));
  n = (dot(n, current_view_direction) < 0.0f) ? n : -n;
  return view.normal_view_to_world(n);
}

float pixel_world_size_at(const ViewMatrices &view,
                          sampler2DDepth depth_tx,
                          float depth,
                          int2 extent,
                          int2 texel)
{
  float2 uv = (float2(texel) + 0.5f) / float2(extent);
  int2 next_texel = min(texel + int2(1, 0), extent - int2(1));
  float2 next_uv = (float2(next_texel) + 0.5f) / float2(extent);
  return distance(view.point_screen_to_view(float3(uv, depth)),
                  view.point_screen_to_view(float3(next_uv, depth)));
}

float3 surface_normal_or_reconstructed(const gbuffer::Reader &reader,
                                        sampler2D prepass_normal_tx,
                                        int2 texel,
                                        int2 extent,
                                        float3 reconstructed_normal,
                                        bool use_prepass_normal,
                                        out bool has_surface_normal)
{
  const gbuffer::Header header = reader.read_header(texel);
  if (!header.is_empty()) {
    has_surface_normal = true;
    return reader.read_normal(texel);
  }

  if (use_prepass_normal) {
    const float3 packed_normal = texelFetch(prepass_normal_tx, texel, 0).rgb;
    if (any(greaterThan(packed_normal, float3(0.0f)))) {
      has_surface_normal = true;
      return normalize(packed_normal * 2.0f - 1.0f);
    }
  }

  has_surface_normal = false;
  return reconstructed_normal;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Detect pass
 * \{ */

struct DetectResources {
  [[sampler(OUTLINE_DEPTH_TEX_SLOT)]] sampler2DDepth depth_tx;
  [[sampler(PREPASS_NORMAL_TEX_SLOT)]] sampler2D prepass_normal_tx;
  [[sampler(OUTLINE_COLOR_TEX_SLOT)]] sampler2D outline_color_tx;
  [[sampler(OUTLINE_INFO_TEX_SLOT)]] usampler2D outline_info_tx;
};

struct DetectFragOut {
  [[frag_color(0)]] float4 seed;
};

[[fragment]]
void detect_frag([[resource_table]] const DetectResources &srt,
                 [[resource_table]] const draw::View &views,
                 [[resource_table]] const ::gbuffer::Reader &reader,
                 [[frag_coord]] const float4 frag_co,
                 [[out]] DetectFragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const int2 extent = textureSize(srt.depth_tx, 0);
  const ViewMatrices view = views.get(0);
  const bool use_prepass_normal = all(
      equal(int2(textureSize(srt.prepass_normal_tx, 0)), extent));

  const float4 outline_color = source_color_fetch(srt.outline_color_tx, texel);
  const uint4 outline_info = source_info_fetch(srt.outline_info_tx, texel);
  const float line_width = outline_width_unpack(outline_info.r);
  const float depth_threshold_input = outline_depth_threshold_unpack(outline_info.g);
  const float depth_threshold_range_input = outline_depth_threshold_range_unpack(outline_info.r);
  const float depth_edge_width = outline_depth_edge_width_unpack(outline_info.r);
  const float normal_threshold_input = outline_normal_threshold_unpack(outline_info.b);
  const float normal_threshold_range = outline_normal_threshold_range_unpack(outline_info.g);
  const float normal_edge_width = outline_normal_edge_width_unpack(outline_info.g);
  const float id_edge_width = outline_id_edge_width_unpack(outline_info.b);
  const bool use_depth_outline = depth_threshold_input < 1.0f;
  const bool use_normal_outline = normal_threshold_input < 1.0f;
  const bool use_geometry_outline = use_depth_outline || use_normal_outline;
  const bool center_id_edge = outline_id_edge_unpack(outline_info.a);
  const float depth_threshold = use_depth_outline ?
                                    pow(depth_threshold_input, 10.0f) * 999.0f + 1.0f :
                                    0.0f;
  /* Remap the depth range with the same pow curve as depth_threshold (Malt: pow(r,10) * 1000). */
  const float depth_threshold_range = (use_depth_outline && depth_threshold_range_input > 0.0f) ?
                                          pow(depth_threshold_range_input, 10.0f) * 1000.0f :
                                          0.0f;
  const float normal_threshold = max(0.01f, normal_threshold_input);

  frag_out.seed = float4(0.0f);
  if (line_width <= 0.0f || outline_color.a <= 0.0f) {
    return;
  }
  if (!center_id_edge && !use_geometry_outline) {
    return;
  }

  const float center_depth = screen_depth_fetch(srt.depth_tx, texel);
  if (center_depth >= 1.0f) {
    return;
  }

  const uint center_outline_id = outline_id_unpack(outline_info.a);

  float3 center_position = float3(0.0f);
  float3 center_normal = float3(0.0f);
  float3 true_normal_camera = float3(0.0f);
  float3 current_view_direction = float3(0.0f);

  if (use_geometry_outline) {
    center_position = screen_to_view(view, extent, texel, center_depth);
    const float2 center_uv = (float2(texel) + 0.5f) / float2(extent);
    current_view_direction = view.point_screen_to_view(float3(center_uv, 1.0f));

    float3 true_normal = float3(0.0f);
    for (int x = -1; x <= 1; x++) {
      for (int y = -1; y <= 1; y++) {
        const int2 sample_texel = texel + int2(x, y);
        if (any(lessThan(sample_texel, int2(0))) ||
            any(greaterThanEqual(sample_texel, extent)))
        {
          continue;
        }
        true_normal += reconstruct_normal(
            view, srt.depth_tx, sample_texel, extent, current_view_direction);
      }
    }
    true_normal = normalize(true_normal);
    if (any(isnan(true_normal))) {
      return;
    }
    bool center_has_surface_normal = false;
    const float3 center_surface_normal = surface_normal_or_reconstructed(
        reader,
        srt.prepass_normal_tx,
        texel,
        extent,
        true_normal,
        use_prepass_normal,
        center_has_surface_normal);
    const float center_normal_alignment = dot(center_surface_normal, true_normal);
    center_normal = (center_has_surface_normal && center_normal_alignment > 0.5f) ?
                        center_surface_normal :
                        true_normal;
    true_normal_camera = view.normal_world_to_view(true_normal);
  }

  const int2 offsets[4] = {int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
  float max_delta_distance = 0.0f;
  float max_delta_angle = 0.0f;
  bool has_id_edge = false;
  float seed_line_width = line_width;

  for (int i = 0; i < 4; i++) {
    const int2 offset = offsets[i];
    const int2 sample_texel = texel + offset;
    if (any(lessThan(sample_texel, int2(0))) ||
        any(greaterThanEqual(sample_texel, extent)))
    {
      continue;
    }

    const float sample_depth = screen_depth_fetch(srt.depth_tx, sample_texel);
    const bool center_is_not_behind_sample = center_depth <= sample_depth + 1.0e-5f;

    if (center_id_edge && center_is_not_behind_sample) {
      const uint4 sample_outline_info = source_info_fetch(srt.outline_info_tx, sample_texel);
      const float sample_line_width = outline_width_unpack(sample_outline_info.r);
      const uint sample_outline_id = outline_id_unpack(sample_outline_info.a);
      if (sample_outline_id != center_outline_id) {
        has_id_edge = true;
        seed_line_width = max(seed_line_width, sample_line_width);
      }
    }

    /* Forward/dithered outline materials can miss GBuffer normals; depth-reconstructed geometry
     * still carries valid depth and normal deltas for Malt-style depth/normal edges. */
    if (use_geometry_outline && center_is_not_behind_sample) {
      const float3 sample_true_normal = reconstruct_normal(
          view, srt.depth_tx, sample_texel, extent, current_view_direction);
      bool sample_has_surface_normal = false;
      const float3 sample_surface_normal = surface_normal_or_reconstructed(
          reader,
          srt.prepass_normal_tx,
          sample_texel,
          extent,
          sample_true_normal,
          use_prepass_normal,
          sample_has_surface_normal);
      const float sample_normal_alignment = dot(sample_surface_normal, sample_true_normal);
      const float3 sample_normal = (sample_has_surface_normal &&
                                    sample_normal_alignment > 0.5f) ?
                                       sample_surface_normal :
                                       sample_true_normal;
      const float3 sample_position = screen_to_view(
          view, extent, sample_texel, sample_depth);
      const float delta_normal = dot(center_normal, sample_normal);
      const float plane_distance = dot(true_normal_camera, center_position);
      const float sample_plane_distance = dot(true_normal_camera, sample_position);
      const float pixel_world_size = max(
          pixel_world_size_at(view, srt.depth_tx, sample_depth, extent, sample_texel), 1e-6f);
      const float delta_distance = abs(plane_distance - sample_plane_distance) / pixel_world_size;

      if (use_depth_outline) {
        max_delta_distance = max(max_delta_distance, delta_distance);
      }
      if (use_normal_outline) {
        max_delta_angle = max(max_delta_angle, 1.0f - delta_normal);
      }
    }
  }

  const bool has_silhouette = has_id_edge ||
                              (use_depth_outline && max_delta_distance > depth_threshold);
  const bool has_internal_edge = use_normal_outline && max_delta_angle > normal_threshold;

  if (has_silhouette || has_internal_edge) {
    /* Each edge class contributes its own width factor independently (Malt-style). */
    float width_factor = 0.0f;
    if (has_id_edge) {
      width_factor = max(width_factor, id_edge_width);
    }
    if (has_silhouette && !has_id_edge) {
      width_factor = max(width_factor,
                         edge_width_factor(max_delta_distance,
                                           depth_threshold,
                                           depth_threshold_range,
                                           depth_edge_width));
    }
    if (has_internal_edge) {
      width_factor = max(width_factor,
                         edge_width_factor(
                             max_delta_angle, normal_threshold, normal_threshold_range,
                             normal_edge_width));
    }
    /* Decouple coverage geometry from width modulation: the JFA flood and the resolve coverage
     * test must use a single uniform radius (the full seed_line_width) to stay hole-free. */
    frag_out.seed = float4(width_factor, 0.0f, 0.0f, seed_line_width);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Factor blur pass
 *
 * Smooth the per-seed width-variation factor (seed.r) ALONG the contour, using 8-neighbour seed
 * connectivity. \{ */

struct FactorBlurResources {
  [[sampler(OUTLINE_SEED_TEX_SLOT)]] sampler2D outline_seed_tx;
  [[sampler(OUTLINE_INFO_TEX_SLOT)]] usampler2D outline_info_tx;
};

struct FactorBlurFragOut {
  [[frag_color(0)]] float4 seed;
};

[[fragment]]
void factor_blur_frag([[resource_table]] const FactorBlurResources &srt,
                      [[frag_coord]] const float4 frag_co,
                      [[out]] FactorBlurFragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const float4 center = texelFetch(srt.outline_seed_tx, texel, 0);

  /* Non-seed pixels are passed through unchanged (preserves .a == 0 emptiness). */
  if (center.a <= 0.0f) {
    frag_out.seed = center;
    return;
  }

  const uint center_id = outline_id_unpack(texelFetch(srt.outline_info_tx, texel, 0).a);
  float factor_sum = clamp(center.r, 0.0f, 1.0f);
  float weight_sum = 1.0f;

  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      const int2 tap = texel + int2(dx, dy);
      const float4 neighbor = texelFetch(srt.outline_seed_tx, tap, 0);
      if (neighbor.a <= 0.0f) {
        continue; /* Not a seed pixel -> not part of the contour. */
      }
      /* Same outline id only: never bleed the factor across different objects' contours. */
      if (outline_id_unpack(texelFetch(srt.outline_info_tx, tap, 0).a) != center_id) {
        continue;
      }
      factor_sum += clamp(neighbor.r, 0.0f, 1.0f);
      weight_sum += 1.0f;
    }
  }

  /* Blur only .r; pass .a (full line width) through unchanged for hole-free coverage. */
  frag_out.seed = float4(factor_sum / weight_sum, 0.0f, 0.0f, center.a);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name JFA init pass
 * \{ */

struct JfaInitResources {
  [[sampler(OUTLINE_SEED_TEX_SLOT)]] sampler2D outline_seed_tx;
};

struct JfaInitFragOut {
  [[frag_color(0)]] float2 jfa_coord;
};

[[fragment]]
void jfa_init_frag([[resource_table]] const JfaInitResources &srt,
                   [[frag_coord]] const float4 frag_co,
                   [[out]] JfaInitFragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const float4 seed = texelFetch(srt.outline_seed_tx, texel, 0);
  if (seed.a > 0.0f) {
    frag_out.jfa_coord = float2(texel) + 0.5f;
  }
  else {
    frag_out.jfa_coord = float2(-1e10f);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name JFA step pass (compute)
 * \{ */

struct JfaStepResources {
  [[push_constant]] int jfa_step_size;
  [[image(OUTLINE_JFA_IN_IMG_SLOT, read, SFLOAT_32_32)]] image2D jfa_in_img;
  [[image(OUTLINE_JFA_OUT_IMG_SLOT, write, SFLOAT_32_32)]] image2D jfa_out_img;
};

[[compute, local_size(OUTLINE_JFA_STEP_GROUP_SIZE, OUTLINE_JFA_STEP_GROUP_SIZE)]]
void jfa_step_comp([[resource_table]] const JfaStepResources &srt,
                   [[global_invocation_id]] const uint3 global_id)
{
  const int2 texel = int2(global_id.xy);
  const int2 extent = imageSize(srt.jfa_out_img);
  if (any(greaterThanEqual(texel, extent))) {
    return;
  }

  float2 best_coord = imageLoad(srt.jfa_in_img, texel).rg;
  float best_dist_sq = 1e20f;
  if (best_coord.x > -1e9f) {
    float2 diff = float2(texel) + 0.5f - best_coord;
    best_dist_sq = dot(diff, diff);
  }

  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      const int2 sample_texel = texel + int2(dx, dy) * srt.jfa_step_size;
      if (any(lessThan(sample_texel, int2(0))) ||
          any(greaterThanEqual(sample_texel, extent)))
      {
        continue;
      }
      const float2 sample_coord = imageLoad(srt.jfa_in_img, sample_texel).rg;
      if (sample_coord.x < -1e9f) {
        continue;
      }
      const float2 diff = float2(texel) + 0.5f - sample_coord;
      const float dist_sq = dot(diff, diff);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_coord = sample_coord;
      }
    }
  }

  imageStore(srt.jfa_out_img, texel, float4(best_coord, 0.0f, 0.0f));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Resolve pass
 * \{ */

struct ResolveResources {
  [[push_constant]] int use_outline_occlusion_depth;
  [[sampler(OUTLINE_DEPTH_TEX_SLOT)]] sampler2DDepth depth_tx;
  [[sampler(OUTLINE_VECTOR_TEX_SLOT)]] sampler2D vector_tx;
  [[sampler(OUTLINE_OCCLUSION_DEPTH_TEX_SLOT)]] sampler2DDepth outline_occlusion_depth_tx;
  [[sampler(OUTLINE_SEED_TEX_SLOT)]] sampler2D outline_seed_tx;
  [[sampler(OUTLINE_COLOR_TEX_SLOT)]] sampler2D outline_color_tx;
  [[sampler(OUTLINE_INFO_TEX_SLOT)]] usampler2D outline_info_tx;
  [[sampler(OUTLINE_JFA_TEX_SLOT)]] sampler2D jfa_tx;
};

struct ResolveFragOut {
  [[frag_color(0)]] float4 color;
  [[frag_color(1)]] float depth;
  [[frag_color(2)]] float4 velocity;
};

[[fragment]]
void resolve_frag([[resource_table]] const ResolveResources &srt,
                  [[frag_coord]] const float4 frag_co,
                  [[out]] ResolveFragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const float center_depth = reverse_z::read(texelFetch(srt.depth_tx, texel, 0).r);
  const float center_occlusion_depth = reverse_z::read(
      texelFetch(srt.outline_occlusion_depth_tx, texel, 0).r);
  const uint center_outline_id = outline_id_unpack(
      source_info_fetch(srt.outline_info_tx, texel).a);
  float4 outline_pass_color = float4(0.0f);
  float outline_pass_depth = texelFetch(srt.depth_tx, texel, 0).r;
  float4 outline_pass_velocity = float4(0.0f);

  const float2 seed_coord = texelFetch(srt.jfa_tx, texel, 0).rg;
  if (seed_coord.x >= -1e9f) {
    const int2 seed_texel = int2(seed_coord);
    const float2 offset = float2(texel) + 0.5f - seed_coord;
    const float offset_length = length(offset) - 0.5f;

    const float4 seed = texelFetch(srt.outline_seed_tx, seed_texel, 0);
    /* .a = full line width (uniform across all seeds -> JFA coverage has no holes).
     * .r = per-seed width modulation factor in (0, 1], already smoothed along the contour. */
    const float seed_width = seed.a;
    const float width_factor = clamp(seed.r, 0.0f, 1.0f);
    const float effective_width = seed_width * width_factor;
    if (effective_width > 0.0f && offset_length <= effective_width * 0.5f) {
      const float4 outline_color = source_color_fetch(srt.outline_color_tx, seed_texel);
      float alpha = outline_color.a;
      if (offset_length <= 0.0f && effective_width <= 1.0f) {
        alpha *= effective_width;
      }
      else {
        alpha *= clamp(effective_width * 0.5f - offset_length, 0.0f, 1.0f);
      }

      const float sample_depth = reverse_z::read(texelFetch(srt.depth_tx, seed_texel, 0).r);
      const float sample_occlusion_depth = reverse_z::read(
          texelFetch(srt.outline_occlusion_depth_tx, seed_texel, 0).r);
      const uint sample_outline_id = outline_id_unpack(
          source_info_fetch(srt.outline_info_tx, seed_texel).a);
      const bool different_outline_id = sample_outline_id != center_outline_id;
      const bool blocked_by_scene_surface = different_outline_id && center_depth < sample_depth;
      const bool center_in_occluder_mask = center_occlusion_depth < 1.0f - 1e-5f;
      const bool sample_in_occluder_mask = sample_occlusion_depth < 1.0f - 1e-5f;
      const bool blocked_by_forward_occluder = srt.use_outline_occlusion_depth != 0 &&
                                               ((different_outline_id &&
                                                 sample_in_occluder_mask) ||
                                                (!different_outline_id &&
                                                 (center_in_occluder_mask ||
                                                  sample_in_occluder_mask)));

      if (alpha > 0.0f && !(blocked_by_scene_surface || blocked_by_forward_occluder)) {
        outline_pass_color = float4(outline_color.rgb * alpha, alpha);
        outline_pass_depth = texelFetch(srt.depth_tx, seed_texel, 0).r;
        outline_pass_velocity = texelFetch(srt.vector_tx, seed_texel, 0);
      }
    }
  }
  frag_out.color = outline_pass_color;
  frag_out.depth = outline_pass_depth;
  frag_out.velocity = outline_pass_velocity;
}

/** \} */

}  // namespace eevee::outline

PipelineGraphic eevee_outline_detect(eevee::outline::fullscreen_vert,
                                     eevee::outline::detect_frag);
PipelineGraphic eevee_outline_factor_blur(eevee::outline::fullscreen_vert,
                                          eevee::outline::factor_blur_frag);
PipelineGraphic eevee_outline_jfa_init(eevee::outline::fullscreen_vert,
                                       eevee::outline::jfa_init_frag);
PipelineCompute eevee_outline_jfa_step(eevee::outline::jfa_step_comp);
PipelineGraphic eevee_outline_resolve(eevee::outline::fullscreen_vert,
                                      eevee::outline::resolve_frag);
