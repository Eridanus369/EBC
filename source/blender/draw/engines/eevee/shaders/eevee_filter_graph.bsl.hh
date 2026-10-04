/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * NPR Filter graph stitching passes.
 *
 * Fullscreen passes that copy one filter graph stage output into the shared filter graph
 * input layer array (`filter_graph_input_tx`) and resolve one stage output handle against
 * the scene sources. Used by the EEVEE FilterMaterialModule between the evaluation of two
 * filter materials (see eevee_filter_material_lib.bsl.hh).
 */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_defines.hh"
#include "eevee_filter_material_lib.bsl.hh"
#include "eevee_filter_material_shared.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_uniform.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_fullscreen.bsl.hh"

namespace eevee {

namespace filter_graph {

[[vertex]] void fullscreen_vert([[vertex_id]] const int vert_id,
                                [[position]] float4 &out_position)
{
  fullscreen_vertex(vert_id, out_position);
}

struct FilterGraphFragOut {
  [[frag_color(0)]] float4 out_color;
};

/* Copy a graph texture layer into the filter graph input layer array. */
struct InputCopyResources {
  [[sampler(0)]] sampler2DArray input_tx;
  [[push_constant]] int input_layer;
  [[push_constant]] int2 target_extent;
  [[push_constant]] int resample_mode;
};

[[fragment]]
void input_copy_frag([[resource_table]] const InputCopyResources &srt,
                     [[frag_coord]] const float4 frag_co,
                     [[out]] FilterGraphFragOut &frag_out)
{
  int2 source_extent = int2(textureSize(srt.input_tx, 0).xy);
  float2 uv = frag_co.xy / float2(srt.target_extent);
  uv = clamp(uv, float2(0.0f), float2(1.0f));

  if (srt.resample_mode == FILTER_GRAPH_RESAMPLE_LINEAR) {
    frag_out.out_color = texture(srt.input_tx, float3(uv, float(srt.input_layer)));
    return;
  }

  int2 texel = clamp(int2(uv * float2(source_extent)), int2(0), source_extent - int2(1));
  frag_out.out_color = texelFetch(srt.input_tx, int3(texel, srt.input_layer), 0);
}

/* Resolve one stage output handle against the scene sources. */
struct ResolveResources {
  [[sampler(0)]] sampler2D scene_color_tx;
  [[sampler(1)]] sampler2DArray rp_color_tx;
  [[sampler(2)]] sampler2DArray rp_value_tx;
  [[sampler(3)]] sampler2DDepth depth_tx;
  [[sampler(4)]] sampler2DArray filter_graph_input_tx;
  [[uniform(FILTER_GRAPH_INPUT_BUF_SLOT)]] const FilterGraphInputHandleData (
      &filter_graph_input_buf)[FILTER_GRAPH_INPUT_MAX];
  [[push_constant]] int2 target_extent;
  [[push_constant]] int resolve_mode;
};

float filter_graph_scene_depth_value(float2 uv)
{
  [[resource_table]] const ResolveResources &srt = resource_table_get(ResolveResources);
  return reverse_z::read(texture(srt.depth_tx, uv).r);
}

float filter_graph_scene_depth_linear(float2 uv)
{
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  return -view.depth_screen_to_view(filter_graph_scene_depth_value(uv));
}

float4 filter_graph_scene_depth_color(float2 uv)
{
  float depth = filter_graph_scene_depth_linear(uv);
  return float4(depth.xxx, 1.0f);
}

float4 filter_graph_scene_normal_color(int2 texel, float2 uv)
{
  [[resource_table]] const Uniform &uni = resource_table_get(Uniform);
  [[resource_table]] const ResolveResources &srt = resource_table_get(ResolveResources);
  if (uni.uniform_buf.render_pass.normal_id >= 0) {
    return float4(
        texelFetch(srt.rp_color_tx, int3(texel, uni.uniform_buf.render_pass.normal_id), 0).rgb,
        1.0f);
  }

  float depth = filter_graph_scene_depth_value(uv);
  if (depth >= 1.0f) {
    return float4(0.0f);
  }

  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  float3 position = view.point_screen_to_world(float3(uv, depth));
  float3 normal = normalize(cross(gpu_dfdx(position), gpu_dfdy(position)));
  return float4(normal, 1.0f);
}

float4 filter_graph_scene_position_color(int2 texel, float2 uv)
{
  [[resource_table]] const Uniform &uni = resource_table_get(Uniform);
  [[resource_table]] const ResolveResources &srt = resource_table_get(ResolveResources);
  if (uni.uniform_buf.render_pass.position_id >= 0) {
    return float4(
        texelFetch(srt.rp_color_tx, int3(texel, uni.uniform_buf.render_pass.position_id), 0).rgb,
        1.0f);
  }

  float depth = filter_graph_scene_depth_value(uv);
  if (depth >= 1.0f) {
    return float4(0.0f);
  }

  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  return float4(view.point_screen_to_world(float3(uv, depth)), 1.0f);
}

float4 filter_graph_eval_handle(TextureHandle tex, int source_kind)
{
  [[resource_table]] const ResolveResources &srt = resource_table_get(ResolveResources);
  float2 uv = gl_FragCoord.xy / float2(srt.target_extent);
  uv = clamp(uv, float2(0.0f), float2(1.0f));

  switch (tex.type) {
    case TEX_HANDLE_RP_COLOR: {
      int2 extent = int2(textureSize(srt.rp_color_tx, 0).xy);
      if (filter_graph_use_linear_resample(source_kind)) {
        return texture(srt.rp_color_tx, float3(uv, float(tex.index)));
      }
      return texelFetch(srt.rp_color_tx, int3(filter_graph_source_texel(uv, extent), int(tex.index)), 0);
    }
    case TEX_HANDLE_RP_VALUE: {
      int2 extent = int2(textureSize(srt.rp_value_tx, 0).xy);
      float value = texelFetch(
                        srt.rp_value_tx, int3(filter_graph_source_texel(uv, extent), int(tex.index)), 0)
                        .r;
      return float4(value.xxx, 1.0f);
    }
    case TEX_HANDLE_FILTER_GRAPH_TEXTURE: {
      int2 extent = int2(textureSize(srt.filter_graph_input_tx, 0).xy);
      if (filter_graph_use_linear_resample(source_kind)) {
        return texture(srt.filter_graph_input_tx, float3(uv, float(tex.index)));
      }
      return texelFetch(srt.filter_graph_input_tx,
                        int3(filter_graph_source_texel(uv, extent), int(tex.index)),
                        0);
    }
    case TEX_HANDLE_SCENE:
      if (tex.index == 0) {
        int2 extent = textureSize(srt.scene_color_tx, 0);
        if (filter_graph_use_linear_resample(source_kind)) {
          return texture(srt.scene_color_tx, uv);
        }
        int2 texel = filter_graph_source_texel(uv, extent);
        return texelFetch(srt.scene_color_tx, texel, 0);
      }
      if (tex.index == 1) {
        return filter_graph_scene_depth_color(uv);
      }
      if (tex.index == 2) {
        int2 texel = filter_graph_source_texel(uv, textureSize(srt.scene_color_tx, 0));
        return filter_graph_scene_normal_color(texel, uv);
      }
      if (tex.index == 4) {
        int2 texel = filter_graph_source_texel(uv, textureSize(srt.scene_color_tx, 0));
        return filter_graph_scene_position_color(texel, uv);
      }
      break;
  }

  return float4(0.0f);
}

float4 filter_graph_resolve_stage_output(TextureHandle tex, int alpha_mode)
{
  [[resource_table]] const ResolveResources &srt = resource_table_get(ResolveResources);
  float4 color = filter_graph_eval_handle(tex, srt.filter_graph_input_buf[0].source_kind);

  if (alpha_mode == FILTER_GRAPH_ALPHA_MODE_TRANSMITTANCE) {
    return color;
  }

  float opacity = color.a;
  if (alpha_mode == FILTER_GRAPH_ALPHA_MODE_DEPTH) {
    opacity = color.r;
  }

  return float4(color.rgb, saturate(1.0f - opacity));
}

[[fragment]]
void graph_resolve_frag([[resource_table]] const ResolveResources &srt,
                        [[resource_table]] const Uniform &uni,
                        [[resource_table]] const draw::View &views,
                        [[frag_coord]] const float4 frag_co,
                        [[out]] FilterGraphFragOut &frag_out)
{
  TextureHandle tex = TextureHandle(srt.filter_graph_input_buf[0].type,
                                    srt.filter_graph_input_buf[0].index);
  if (srt.resolve_mode == FILTER_GRAPH_RESOLVE_RAW) {
    frag_out.out_color = filter_graph_eval_handle(tex, srt.filter_graph_input_buf[0].source_kind);
    return;
  }
  frag_out.out_color = filter_graph_resolve_stage_output(tex,
                                                         srt.filter_graph_input_buf[0]
                                                             .alpha_mode);
}

}  // namespace filter_graph

}  // namespace eevee

PipelineGraphic eevee_filter_graph_input_copy(eevee::filter_graph::fullscreen_vert,
                                              eevee::filter_graph::input_copy_frag);
PipelineGraphic eevee_filter_graph_resolve(eevee::filter_graph::fullscreen_vert,
                                           eevee::filter_graph::graph_resolve_frag);
