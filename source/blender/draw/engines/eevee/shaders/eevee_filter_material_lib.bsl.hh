/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * NPR Filter material graph evaluation library.
 *
 * Helpers used by the generated filter material sources and by the filter graph stitching
 * passes. Kept free of any entry point so the file can be used as a shader dependency of the
 * generated `eevee_nodetree_frag_lib.glsl` (which must see these definitions before the
 * generated code that calls them).
 *
 * The scene-level filter graph (`scene.eevee.filter_graph`) is evaluated as a series of
 * world-geometry material passes (MAT_PIPE_FILTER + MAT_GEOM_WORLD). Each filter material
 * renders all of its Filter Output nodes into the `filter_graph_output_img` layer array,
 * one layer per output. The graph is then stitched together by the
 * `eevee_filter_graph_input_copy` and `eevee_filter_graph_resolve` static shaders
 * (see eevee_filter_graph.bsl.hh).
 */

#pragma once

#include "infos/eevee_geom_infos.hh"
#include "infos/eevee_nodetree_infos.hh"

#include "draw_view.bsl.hh"
#include "eevee_attributes_world_lib.glsl"
#include "eevee_colorspace_lib.bsl.hh"
#include "eevee_defines.hh"
#include "eevee_filter_material_shared.hh"
#include "eevee_pipeline.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_uniform.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_utildefines.bsl.hh"

namespace eevee {

/**
 * Resource table for the filter material pipeline.
 * All the scene sources the filter graph can sample from.
 */
struct FilterMaterial {
  [[legacy_info]] ShaderCreateInfo eevee_geom_iface_info;

  [[sampler(FILTER_SCENE_COLOR_TEX_SLOT)]] sampler2D scene_color_tx;
  [[sampler(FILTER_AOV_COLOR_TEX_SLOT)]] sampler2DArray rp_color_tx;
  [[sampler(FILTER_AOV_VALUE_TEX_SLOT)]] sampler2DArray rp_value_tx;
  [[sampler(FILTER_DEPTH_TEX_SLOT)]] sampler2DDepth depth_tx;
  [[sampler(FILTER_CRYPTOMATTE_TEX_SLOT)]] sampler2D cryptomatte_tx;
  [[sampler(FILTER_GRAPH_INPUT_TEX_SLOT)]] sampler2DArray filter_graph_input_tx;

  [[image(FILTER_GRAPH_OUTPUT_IMG_SLOT, write, SFLOAT_16_16_16_16)]] image2DArray
      filter_graph_output_img;

  [[uniform(FILTER_OBJECT_INFO_BUF_SLOT)]] const FilterObjectInfoData (
      &filter_object_buf)[FILTER_OBJECT_INFO_MAX];
  [[uniform(FILTER_GRAPH_INPUT_BUF_SLOT)]] const FilterGraphInputHandleData (
      &filter_graph_input_buf)[FILTER_GRAPH_INPUT_MAX];
};

/* Forward declarations for the material graph generated code. */
float4 filter_texture_eval(TextureHandle tex, float2 offset, bool texel_offset);
float4 filter_texture_eval(TextureHandle tex);
float4 filter_texture_eval_uv(TextureHandle tex, float2 uv);
bool TextureHandle_stores_transmittance_alpha(TextureHandle tex);
bool TextureHandle_is_scene_depth(TextureHandle tex);
void input_aov_impl(uint hash, out TextureHandle color, out TextureHandle value);

/* Filter materials do not produce closures. */
float4 closure_to_rgba(Closure /*cl*/)
{
  return float4(0.0f);
}

/* -------------------------------------------------------------------- */
/** \name Filter graph scene sources
 * \{ */

int2 filter_graph_output_extent()
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  return int2(imageSize(srt.filter_graph_output_img).xy);
}

int2 filter_graph_source_texel(float2 uv, int2 source_extent)
{
  return clamp(int2(uv * float2(source_extent)), int2(0), source_extent - int2(1));
}

float filter_scene_depth_value(float2 uv)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  return reverse_z::read(texture(srt.depth_tx, uv).r);
}

float filter_scene_depth_linear(float2 uv)
{
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  return -view.depth_screen_to_view(filter_scene_depth_value(uv));
}

float4 filter_scene_depth_color(float2 uv)
{
  float depth = filter_scene_depth_linear(uv);
  return float4(depth.xxx, 1.0f);
}

float4 filter_scene_normal_color(int2 texel, float2 uv)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  [[resource_table]] const Uniform &uni = resource_table_get(Uniform);
  if (uni.uniform_buf.render_pass.normal_id >= 0) {
    return float4(
        texelFetch(srt.rp_color_tx, int3(texel, uni.uniform_buf.render_pass.normal_id), 0).rgb,
        1.0f);
  }

  float depth = filter_scene_depth_value(uv);
  if (depth >= 1.0f) {
    return float4(0.0f);
  }

  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  float3 position = view.point_screen_to_world(float3(uv, depth));
  float3 normal = normalize(cross(gpu_dfdx(position), gpu_dfdy(position)));
  return float4(normal, 1.0f);
}

float4 filter_scene_position_color(int2 texel, float2 uv)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  [[resource_table]] const Uniform &uni = resource_table_get(Uniform);
  if (uni.uniform_buf.render_pass.position_id >= 0) {
    return float4(
        texelFetch(srt.rp_color_tx, int3(texel, uni.uniform_buf.render_pass.position_id), 0).rgb,
        1.0f);
  }

  float depth = filter_scene_depth_value(uv);
  if (depth >= 1.0f) {
    return float4(0.0f);
  }

  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);
  return float4(view.point_screen_to_world(float3(uv, depth)), 1.0f);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Filter graph handle resolution
 * \{ */

void input_aov_impl(uint hash, out TextureHandle color, out TextureHandle value)
{
  [[resource_table]] const Uniform &uni = resource_table_get(Uniform);
  uint total_len = uni.uniform_buf.render_pass.aovs.color_len +
                   uni.uniform_buf.render_pass.aovs.value_len;
  uint hash_index;
  for (hash_index = 0u; hash_index < AOV_MAX && hash_index < total_len; hash_index += 4u) {
    bool4 cmp_mask = equal(uni.uniform_buf.render_pass.aovs.hash[hash_index >> 2u], uint4(hash));
    if (any(cmp_mask)) {
      hash_index += (cmp_mask[0] ? 0u : (cmp_mask[1] ? 1u : (cmp_mask[2] ? 2u : 3u)));
      break;
    }
  }

  if (hash_index < total_len) {
    bool is_value = hash_index >= uint(uni.uniform_buf.render_pass.aovs.color_len);
    uint aov_index = hash_index - (is_value ? uint(uni.uniform_buf.render_pass.aovs.color_len) :
                                              0u);
    int render_pass_index = (is_value ? uni.uniform_buf.render_pass.value_len :
                                        uni.uniform_buf.render_pass.color_len) +
                            int(aov_index);
    color = is_value ? TEXTURE_HANDLE_DEFAULT :
                       TextureHandle(TEX_HANDLE_RP_COLOR, render_pass_index);
    value = is_value ? TextureHandle(TEX_HANDLE_RP_VALUE, render_pass_index) :
                       TEXTURE_HANDLE_DEFAULT;
    return;
  }

  color = TEXTURE_HANDLE_DEFAULT;
  value = TEXTURE_HANDLE_DEFAULT;
}

TextureHandle filter_graph_input_resolve(TextureHandle tex)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  if (tex.type != TEX_HANDLE_FILTER_GRAPH_INPUT) {
    return tex;
  }
  if (tex.index < 0 || tex.index >= FILTER_GRAPH_INPUT_MAX) {
    return TEXTURE_HANDLE_DEFAULT;
  }

  TextureHandle resolved = TextureHandle(srt.filter_graph_input_buf[tex.index].type,
                                         srt.filter_graph_input_buf[tex.index].index);
  return (resolved.type != TEX_HANDLE_FILTER_GRAPH_INPUT) ? resolved : TEXTURE_HANDLE_DEFAULT;
}

int TextureHandle_alpha_mode(TextureHandle tex)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  if (tex.type == TEX_HANDLE_FILTER_GRAPH_INPUT) {
    if (tex.index < 0 || tex.index >= FILTER_GRAPH_INPUT_MAX) {
      return FILTER_GRAPH_ALPHA_MODE_OPACITY;
    }
    return srt.filter_graph_input_buf[tex.index].alpha_mode;
  }

  tex = filter_graph_input_resolve(tex);
  if (tex.type == TEX_HANDLE_SCENE && tex.index == 0) {
    return FILTER_GRAPH_ALPHA_MODE_TRANSMITTANCE;
  }
  if (tex.type == TEX_HANDLE_SCENE && tex.index == 1) {
    return FILTER_GRAPH_ALPHA_MODE_DEPTH;
  }
  return FILTER_GRAPH_ALPHA_MODE_OPACITY;
}

bool TextureHandle_stores_transmittance_alpha(TextureHandle tex)
{
  return TextureHandle_alpha_mode(tex) == FILTER_GRAPH_ALPHA_MODE_TRANSMITTANCE;
}

bool TextureHandle_is_scene_depth(TextureHandle tex)
{
  return TextureHandle_alpha_mode(tex) == FILTER_GRAPH_ALPHA_MODE_DEPTH;
}

int TextureHandle_source_kind(TextureHandle tex)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  if (tex.type == TEX_HANDLE_FILTER_GRAPH_INPUT) {
    if (tex.index < 0 || tex.index >= FILTER_GRAPH_INPUT_MAX) {
      return FILTER_GRAPH_SOURCE_COLOR;
    }
    return srt.filter_graph_input_buf[tex.index].source_kind;
  }

  tex = filter_graph_input_resolve(tex);
  if (tex.type == TEX_HANDLE_SCENE) {
    if (tex.index == 0) {
      return FILTER_GRAPH_SOURCE_COLOR;
    }
    if (tex.index == 1) {
      return FILTER_GRAPH_SOURCE_DEPTH;
    }
    return FILTER_GRAPH_SOURCE_DATA;
  }
  if (tex.type == TEX_HANDLE_RP_VALUE) {
    return FILTER_GRAPH_SOURCE_VALUE;
  }
  if (tex.type == TEX_HANDLE_FILTER_GRAPH_TEXTURE) {
    return FILTER_GRAPH_SOURCE_INTERMEDIATE;
  }
  return FILTER_GRAPH_SOURCE_COLOR;
}

bool filter_graph_use_linear_resample(int source_kind)
{
  return source_kind == FILTER_GRAPH_SOURCE_COLOR ||
         source_kind == FILTER_GRAPH_SOURCE_INTERMEDIATE;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Filter texture evaluation
 * \{ */

float4 filter_texture_eval(TextureHandle tex, float2 offset, bool texel_offset)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  int source_kind = TextureHandle_source_kind(tex);
  tex = filter_graph_input_resolve(tex);
  if (tex.type == TEX_HANDLE_NULL) {
    return float4(0.0f);
  }

  int2 output_extent = filter_graph_output_extent();
  float2 uv = gl_FragCoord.xy / float2(output_extent);
  if (texel_offset) {
    uv += offset / float2(output_extent);
  }
  else {
    uv += offset;
  }
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
      return float4(texelFetch(srt.rp_value_tx,
                               int3(filter_graph_source_texel(uv, extent), int(tex.index)),
                               0)
                        .rrr,
                    1.0f);
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
        /* Return raw scene color. Alpha (transmittance) inversion is handled
         * by the Image Sample node, matching the original Scene Color behavior. */
        int2 extent = textureSize(srt.scene_color_tx, 0);
        if (filter_graph_use_linear_resample(source_kind)) {
          return texture(srt.scene_color_tx, uv);
        }
        int2 texel = filter_graph_source_texel(uv, extent);
        return texelFetch(srt.scene_color_tx, texel, 0);
      }
      if (tex.index == 1) {
        return filter_scene_depth_color(uv);
      }
      if (tex.index == 2) {
        int2 texel = filter_graph_source_texel(uv, int2(textureSize(srt.rp_color_tx, 0).xy));
        return filter_scene_normal_color(texel, uv);
      }
      if (tex.index == 4) {
        int2 texel = filter_graph_source_texel(uv, int2(textureSize(srt.rp_color_tx, 0).xy));
        return filter_scene_position_color(texel, uv);
      }
      return float4(0.0f);
    default:
      return float4(0.0f);
  }
}

float4 filter_texture_eval(TextureHandle tex)
{
  return filter_texture_eval(tex, float2(0.0f), true);
}

/* Absolute UV sampling: use the provided uv directly (0-1 range), ignoring
 * gl_FragCoord. Enables procedural coordinate sampling (Voronoi, screen
 * coordinate nodes, etc.), matching the old Scene Color Vector input. */
float4 filter_texture_eval_uv(TextureHandle tex, float2 uv)
{
  [[resource_table]] FilterMaterial &srt = resource_table_get(FilterMaterial);
  int source_kind = TextureHandle_source_kind(tex);
  tex = filter_graph_input_resolve(tex);
  if (tex.type == TEX_HANDLE_NULL) {
    return float4(0.0f);
  }

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
      return float4(texelFetch(srt.rp_value_tx,
                               int3(filter_graph_source_texel(uv, extent), int(tex.index)),
                               0)
                        .rrr,
                    1.0f);
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
        return filter_scene_depth_color(uv);
      }
      if (tex.index == 2) {
        int2 texel = filter_graph_source_texel(uv, int2(textureSize(srt.rp_color_tx, 0).xy));
        return filter_scene_normal_color(texel, uv);
      }
      if (tex.index == 4) {
        int2 texel = filter_graph_source_texel(uv, int2(textureSize(srt.rp_color_tx, 0).xy));
        return filter_scene_position_color(texel, uv);
      }
      return float4(0.0f);
    default:
      return float4(0.0f);
  }
}

/** \} */

}  // namespace eevee

/* Called by the generated material sources. Stores one filter output into its own layer of
 * `filter_graph_output_img`. Global scope so that generated GLSL code can call it without
 * namespace mangling. */
void filter_graph_output_store(int index, float4 result, int2 frag_texel)
{
  [[resource_table]] eevee::FilterMaterial &srt = resource_table_get(eevee::FilterMaterial);
  imageStore(srt.filter_graph_output_img,
             int3(frag_texel, index),
             float4(result.rgb, saturate(result.a)));
}
