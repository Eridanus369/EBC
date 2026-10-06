/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * NPR evaluation pass for deferred materials with an attached NPR tree.
 *
 * Runs after the deferred combine pass, re-drawing the deferred geometry. It reads the
 * pre-NPR combined radiance copy and the GBuffer, evaluates the NPR node tree, and
 * overwrites the combined radiance.
 */
#pragma once

#include "infos/eevee_geom_infos.hh"
#include "infos/eevee_nodetree_infos.hh"

#include "draw_view.bsl.hh" /* IWYU pragma: export. For nodetree functions. */
#include "eevee_colorspace_lib.bsl.hh"
#include "eevee_gbuffer_read.bsl.hh"
#include "eevee_light_data.bsl.hh"
#include "eevee_hiz.bsl.hh"
#include "eevee_lightprobe.bsl.hh"
#include "eevee_nodetree_frag_lib.glsl"
#include "eevee_renderpass.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "eevee_surf_common.bsl.hh"
#include "gpu_shader_shared_exponent.bsl.hh"

#define TEX_HANDLE_NULL 0u
#define TEX_HANDLE_RP_COLOR 1u
#define TEX_HANDLE_RP_VALUE 2u
#define TEX_HANDLE_SCENE 30u
#define TEX_HANDLE_COMBINED_COLOR 10u
#define TEX_HANDLE_DIFFUSE_COLOR 11u
#define TEX_HANDLE_DIFFUSE_DIRECT 12u
#define TEX_HANDLE_DIFFUSE_INDIRECT 13u
#define TEX_HANDLE_SPECULAR_COLOR 14u
#define TEX_HANDLE_SPECULAR_DIRECT 15u
#define TEX_HANDLE_SPECULAR_INDIRECT 16u
#define TEX_HANDLE_POSITION 17u
#define TEX_HANDLE_NORMAL 18u

namespace eevee {

struct NprDeferred {
  [[legacy_info]] ShaderCreateInfo eevee_geom_iface_info;

  [[specialization_constant(true)]] bool use_split_radiance;

  /* Copy of the combined radiance made before the NPR pass. Reading through this input avoids
   * the read/write hazard with the combined target the pass renders into. */
  [[sampler(0)]] sampler2D radiance_tx;
  /* Per-closure-bin direct/indirect radiance, shared with the deferred combine pass. */
  [[sampler(1)]] usampler2D direct_radiance_1_tx;
  [[sampler(2)]] usampler2D direct_radiance_2_tx;
  [[sampler(3)]] usampler2D direct_radiance_3_tx;
  [[sampler(4)]] sampler2D indirect_radiance_1_tx;
  [[sampler(5)]] sampler2D indirect_radiance_2_tx;
  [[sampler(6)]] sampler2D indirect_radiance_3_tx;

  float3 load_radiance_direct(int2 texel, uchar i) const
  {
    uint data = 0u;
    switch (i) {
      case 0:
        data = texelFetch(direct_radiance_1_tx, texel, 0).r;
        break;
      case 1:
        data = texelFetch(direct_radiance_2_tx, texel, 0).r;
        break;
      case 2:
        data = texelFetch(direct_radiance_3_tx, texel, 0).r;
        break;
      default:
        break;
    }
    return rgb9e5_decode(data);
  }

  float3 load_radiance_indirect(int2 texel, uchar i) const
  {
    switch (i) {
      case 0:
        return texelFetch(indirect_radiance_1_tx, texel, 0).rgb;
      case 1:
        return texelFetch(indirect_radiance_2_tx, texel, 0).rgb;
      case 2:
        return texelFetch(indirect_radiance_3_tx, texel, 0).rgb;
      default:
        return float3(0);
    }
    return float3(0);
  }
};

struct NprFragOut {
  [[frag_color(0)]] float4 radiance;
};

/* Semantic per-texel inputs exposed to the NPR node tree. Alpha is kept inverted (same
 * convention as #swap_alpha) so that semantic handles read as fully opaque. */
float4 g_npr_combined_color;
float4 g_npr_diffuse_color;
float4 g_npr_diffuse_direct;
float4 g_npr_diffuse_indirect;
float4 g_npr_specular_color;
float4 g_npr_specular_direct;
float4 g_npr_specular_indirect;
float3 g_npr_average_normal;

struct NprDeferredCombine {
  float3 diffuse_color;
  float3 diffuse_direct;
  float3 diffuse_indirect;
  float3 specular_color;
  float3 specular_direct;
  float3 specular_indirect;
  float3 average_normal;
};

/* Recombine the deferred lighting at an arbitrary texel (adapted from the deferred combine
 * pass). Returns zeroed values for background texels. */
NprDeferredCombine npr_deferred_combine(int2 texel)
{
  NprDeferredCombine dc;
  dc.diffuse_color = float3(0.0f);
  dc.diffuse_direct = float3(0.0f);
  dc.diffuse_indirect = float3(0.0f);
  dc.specular_color = float3(0.0f);
  dc.specular_direct = float3(0.0f);
  dc.specular_indirect = float3(0.0f);
  dc.average_normal = float3(0.0f);

  [[resource_table]] const NprDeferred &srt = resource_table_get(eevee::NprDeferred);
  [[resource_table]] const gbuffer::Reader &reader = resource_table_get(gbuffer::Reader);
  [[resource_table]] const Uniform &uni = resource_table_get(eevee::Uniform);

  const gbuffer::Layers gbuf = reader.read_layers(texel);
  const uchar closure_count = gbuf.header.closure_len();
  if (closure_count == 0) {
    return dc;
  }
  const uint3 bin_indices = gbuf.header.bin_index_per_layer();

  for (int i = 0; i < 3 /* GBUFFER_LAYER_MAX */; i++) [[unroll]] {
    if (i < closure_count) {
      ClosureUndetermined cl = gbuf.layer[i];
      if (cl.type != CLOSURE_NONE_ID) {
        uchar layer_index = bin_indices[i];
        float3 closure_direct_light = srt.load_radiance_direct(texel, layer_index);
        float3 closure_indirect_light = srt.load_radiance_indirect(texel, layer_index);

        float closure_weight = reduce_add(cl.color);
        dc.average_normal += cl.N * closure_weight;

        switch (cl.type) {
          case CLOSURE_BSDF_TRANSLUCENT_ID:
          case CLOSURE_BSSRDF_BURLEY_ID:
          case CLOSURE_BSDF_DIFFUSE_ID:
            dc.diffuse_color += cl.color;
            dc.diffuse_direct += closure_direct_light * cl.color;
            dc.diffuse_indirect += closure_indirect_light * cl.color;
            break;
          case CLOSURE_BSDF_MICROFACET_GGX_REFLECTION_ID:
          case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:
          case CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID:
            dc.specular_color += cl.color;
            dc.specular_direct += closure_direct_light * cl.color;
            dc.specular_indirect += closure_indirect_light * cl.color;
            break;
          default:
            break;
        }
      }
    }
  }

  float normal_len = length(dc.average_normal);
  dc.average_normal = (normal_len < 1e-5f) ? gbuf.surface_N() : (dc.average_normal / normal_len);

  /* Mirror the combine pass clamping. */
  float clamp_direct = uni.uniform_buf.clamp.surface_direct;
  float clamp_indirect = uni.uniform_buf.clamp.surface_indirect;
  dc.diffuse_direct = colorspace::brightness_clamp_max(dc.diffuse_direct, clamp_direct);
  dc.diffuse_indirect = colorspace::brightness_clamp_max(dc.diffuse_indirect, clamp_indirect);
  dc.specular_direct = colorspace::brightness_clamp_max(dc.specular_direct, clamp_direct);
  dc.specular_indirect = colorspace::brightness_clamp_max(dc.specular_indirect, clamp_indirect);
  dc.diffuse_direct *= uni.uniform_buf.clamp.direct_scale;
  dc.diffuse_indirect *= uni.uniform_buf.clamp.indirect_scale;
  dc.specular_direct *= uni.uniform_buf.clamp.direct_scale;
  dc.specular_indirect *= uni.uniform_buf.clamp.indirect_scale;
  return dc;
}

void npr_globals_load(int2 texel)
{
  /* Named binding: the shader tool only rewrites resource-table member access through named
   * locals (inline `resource_table_get(...).field` compiles against the empty stub struct). */
  [[resource_table]] const NprDeferred &srt = resource_table_get(eevee::NprDeferred);
  g_npr_combined_color = float4(texelFetch(srt.radiance_tx, texel, 0).rgb, 1.0f);
  NprDeferredCombine dc = npr_deferred_combine(texel);
  g_npr_diffuse_color = float4(dc.diffuse_color, 1.0f);
  g_npr_diffuse_direct = float4(dc.diffuse_direct, 1.0f);
  g_npr_diffuse_indirect = float4(dc.diffuse_indirect, 1.0f);
  g_npr_average_normal = dc.average_normal;
  g_npr_specular_color = float4(dc.specular_color, 1.0f);
  g_npr_specular_direct = float4(dc.specular_direct, 1.0f);
  g_npr_specular_indirect = float4(dc.specular_indirect, 1.0f);
}

void npr_input_impl(out TextureHandle combined_color,
                    out TextureHandle diffuse_color,
                    out TextureHandle diffuse_direct,
                    out TextureHandle diffuse_indirect,
                    out TextureHandle specular_color,
                    out TextureHandle specular_direct,
                    out TextureHandle specular_indirect,
                    out TextureHandle position,
                    out TextureHandle normal)
{
  combined_color = TextureHandle(TEX_HANDLE_COMBINED_COLOR, 0);
  diffuse_color = TextureHandle(TEX_HANDLE_DIFFUSE_COLOR, 0);
  diffuse_direct = TextureHandle(TEX_HANDLE_DIFFUSE_DIRECT, 0);
  diffuse_indirect = TextureHandle(TEX_HANDLE_DIFFUSE_INDIRECT, 0);
  specular_color = TextureHandle(TEX_HANDLE_SPECULAR_COLOR, 0);
  specular_direct = TextureHandle(TEX_HANDLE_SPECULAR_DIRECT, 0);
  specular_indirect = TextureHandle(TEX_HANDLE_SPECULAR_INDIRECT, 0);
  position = TextureHandle(TEX_HANDLE_POSITION, 0);
  normal = TextureHandle(TEX_HANDLE_NORMAL, 0);
}

void input_aov_impl(uint hash, out TextureHandle color, out TextureHandle value)
{
  [[resource_table]] const Uniform &uni = resource_table_get(eevee::Uniform);
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
    uint aov_index = hash_index -
                     (is_value ? uint(uni.uniform_buf.render_pass.aovs.color_len) : 0u);
    int render_pass_index = (is_value ? uni.uniform_buf.render_pass.value_len :
                                        uni.uniform_buf.render_pass.color_len) +
                            int(aov_index);
    color = is_value ? TextureHandle(TEX_HANDLE_NULL, 0) :
                       TextureHandle(TEX_HANDLE_RP_COLOR, render_pass_index);
    value = is_value ? TextureHandle(TEX_HANDLE_RP_VALUE, render_pass_index) :
                       TextureHandle(TEX_HANDLE_NULL, 0);
    return;
  }

  color = TextureHandle(TEX_HANDLE_NULL, 0);
  value = TextureHandle(TEX_HANDLE_NULL, 0);
}

float4 swap_alpha(float4 v)
{
  v.a = 1.0f - saturate(v.a);
  return v;
}

int2 npr_texture_texel_from_uv(float2 uv, int2 extent)
{
  uv = clamp(uv, float2(0.0f), float2(1.0f));
  return clamp(int2(uv * float2(extent)), int2(0), extent - int2(1));
}

float4 npr_scene_handle_eval(TextureHandle tex, int2 texel, float2 screen_uv)
{
  [[resource_table]] const Uniform &uni = resource_table_get(eevee::Uniform);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  /* Named bindings: inline `resource_table_get(...).field` compiles against the empty stub
   * struct; only named-local member access is rewritten to `srt_access` by the shader tool. */
  [[resource_table]] const HiZ &hiz = resource_table_get(eevee::HiZ);
  const ViewMatrices view = views.get(0);
  if (tex.index == 1) {
    /* Scene depth. */
    float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
    float3 P = view.point_screen_to_world(float3(screen_uv, depth));
    return float4((-P.z).xxx, 1.0f);
  }
  if (tex.index == 2) {
    /* Scene normal. */
    if (uni.uniform_buf.render_pass.normal_id >= 0) {
      [[resource_table]] const RenderPassOutput &render_passes = resource_table_get(
          eevee::RenderPassOutput);
      return float4(imageLoad(render_passes.rp_color_img,
                              int3(texel, uni.uniform_buf.render_pass.normal_id))
                        .rgb,
                    1.0f);
    }
    float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
    if (depth >= 1.0f) {
      return float4(0.0f);
    }
    float3 position = view.point_screen_to_world(float3(screen_uv, depth));
    float3 normal = normalize(cross(gpu_dfdx(position), gpu_dfdy(position)));
    return float4(normal, 1.0f);
  }
  if (tex.index == 4) {
    /* Scene position. */
    if (uni.uniform_buf.render_pass.position_id >= 0) {
      [[resource_table]] const RenderPassOutput &render_passes = resource_table_get(
          eevee::RenderPassOutput);
      return float4(imageLoad(render_passes.rp_color_img,
                              int3(texel, uni.uniform_buf.render_pass.position_id))
                        .rgb,
                    1.0f);
    }
    float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
    if (depth >= 1.0f) {
      return float4(0.0f);
    }
    return float4(view.point_screen_to_world(float3(screen_uv, depth)), 1.0f);
  }
  return float4(0.0f);
}

/** Evaluate an NPR texture handle at an offset from the current fragment. */
float4 TextureHandle_eval_impl(TextureHandle tex, float2 offset, bool texel_offset)
{
  if (tex.type == TEX_HANDLE_NULL) {
    return float4(0.0f);
  }

  [[resource_table]] const NprDeferred &srt = resource_table_get(eevee::NprDeferred);
  [[resource_table]] const HiZ &hiz = resource_table_get(eevee::HiZ);
  [[resource_table]] const RenderPassOutput &render_passes = resource_table_get(
      eevee::RenderPassOutput);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);

  if (all(equal(offset, float2(0.0f)))) {
    switch (tex.type) {
      case TEX_HANDLE_COMBINED_COLOR:
        return g_npr_combined_color;
      case TEX_HANDLE_DIFFUSE_COLOR:
        return g_npr_diffuse_color;
      case TEX_HANDLE_DIFFUSE_DIRECT:
        return g_npr_diffuse_direct;
      case TEX_HANDLE_DIFFUSE_INDIRECT:
        return g_npr_diffuse_indirect;
      case TEX_HANDLE_SPECULAR_COLOR:
        return g_npr_specular_color;
      case TEX_HANDLE_SPECULAR_DIRECT:
        return g_npr_specular_direct;
      case TEX_HANDLE_SPECULAR_INDIRECT:
        return g_npr_specular_indirect;
      case TEX_HANDLE_NORMAL:
        return float4(g_npr_average_normal, 0.0f);
      case TEX_HANDLE_POSITION:
        return float4(g_data.P, 0.0f);
      default:
        break;
    }
  }

  int2 texel = int2(gl_FragCoord.xy);
  int2 extent = textureSize(srt.radiance_tx, 0);
  if (texel_offset) {
    texel += int2(offset);
  }
  else {
    float3 vP = view.point_world_to_view(g_data.P);
    float2 uv = view.point_view_to_screen(vP + float3(offset, 0.0f)).xy;
    texel = int2(uv * float2(extent));
  }
  texel = clamp(texel, int2(0), extent - int2(1));

  switch (tex.type) {
    case TEX_HANDLE_RP_COLOR:
      /* AOV color passes are data buffers; keep them opaque when exposed through NPR output. */
      return float4(imageLoad(render_passes.rp_color_img, int3(texel, tex.index)).rgb, 0.0f);
    case TEX_HANDLE_RP_VALUE:
      return float4(imageLoad(render_passes.rp_value_img, int3(texel, tex.index)).rrr, 0.0f);
    case TEX_HANDLE_SCENE:
      return npr_scene_handle_eval(tex, texel, (float2(texel) + 0.5f) / float2(extent));
    case TEX_HANDLE_POSITION: {
      float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
      return float4(view.point_screen_to_world(float3((float2(texel) + 0.5f) / float2(extent), depth)),
                    0.0f);
    }
    default:
      break;
  }

  /* Semantic deferred inputs at an offset texel. */
  NprDeferredCombine dc = npr_deferred_combine(texel);
  switch (tex.type) {
    case TEX_HANDLE_COMBINED_COLOR:
      return texelFetch(srt.radiance_tx, texel, 0);
    case TEX_HANDLE_DIFFUSE_COLOR:
      return float4(dc.diffuse_color, 1.0f);
    case TEX_HANDLE_DIFFUSE_DIRECT:
      return float4(dc.diffuse_direct, 1.0f);
    case TEX_HANDLE_DIFFUSE_INDIRECT:
      return float4(dc.diffuse_indirect, 1.0f);
    case TEX_HANDLE_SPECULAR_COLOR:
      return float4(dc.specular_color, 1.0f);
    case TEX_HANDLE_SPECULAR_DIRECT:
      return float4(dc.specular_direct, 1.0f);
    case TEX_HANDLE_SPECULAR_INDIRECT:
      return float4(dc.specular_indirect, 1.0f);
    case TEX_HANDLE_NORMAL:
      return float4(dc.average_normal, 0.0f);
    default:
      return float4(0.0f);
  }
}

float4 TextureHandle_eval(TextureHandle tex, float2 offset, bool texel_offset)
{
  return swap_alpha(TextureHandle_eval_impl(tex, offset, texel_offset));
}

float4 TextureHandle_eval_uv_impl(TextureHandle tex, float2 uv)
{
  if (tex.type == TEX_HANDLE_NULL) {
    return float4(0.0f);
  }

  [[resource_table]] const NprDeferred &srt = resource_table_get(eevee::NprDeferred);
  [[resource_table]] const HiZ &hiz = resource_table_get(eevee::HiZ);
  [[resource_table]] const RenderPassOutput &render_passes = resource_table_get(
      eevee::RenderPassOutput);
  [[resource_table]] const draw::View &views = resource_table_get(draw::View);
  const ViewMatrices view = views.get(0);

  int2 extent = textureSize(srt.radiance_tx, 0);
  int2 texel = npr_texture_texel_from_uv(uv, extent);

  switch (tex.type) {
    case TEX_HANDLE_RP_COLOR:
      return float4(imageLoad(render_passes.rp_color_img, int3(texel, tex.index)).rgb, 0.0f);
    case TEX_HANDLE_RP_VALUE:
      return float4(imageLoad(render_passes.rp_value_img, int3(texel, tex.index)).rrr, 0.0f);
    case TEX_HANDLE_SCENE:
      return npr_scene_handle_eval(tex, texel, (float2(texel) + 0.5f) / float2(extent));
    case TEX_HANDLE_POSITION: {
      float depth = texelFetch(hiz.hiz_tx, texel, 0).r;
      return float4(view.point_screen_to_world(float3((float2(texel) + 0.5f) / float2(extent), depth)),
                    0.0f);
    }
    default:
      break;
  }

  if (g_npr_combined_color.a == 1.0f && tex.type == TEX_HANDLE_COMBINED_COLOR &&
      all(equal(texel, int2(gl_FragCoord.xy))))
  {
    /* Fast path: own texel, already loaded. */
    return g_npr_combined_color;
  }

  NprDeferredCombine dc = npr_deferred_combine(texel);
  switch (tex.type) {
    case TEX_HANDLE_COMBINED_COLOR:
      return texelFetch(srt.radiance_tx, texel, 0);
    case TEX_HANDLE_DIFFUSE_COLOR:
      return float4(dc.diffuse_color, 1.0f);
    case TEX_HANDLE_DIFFUSE_DIRECT:
      return float4(dc.diffuse_direct, 1.0f);
    case TEX_HANDLE_DIFFUSE_INDIRECT:
      return float4(dc.diffuse_indirect, 1.0f);
    case TEX_HANDLE_SPECULAR_COLOR:
      return float4(dc.specular_color, 1.0f);
    case TEX_HANDLE_SPECULAR_DIRECT:
      return float4(dc.specular_direct, 1.0f);
    case TEX_HANDLE_SPECULAR_INDIRECT:
      return float4(dc.specular_indirect, 1.0f);
    case TEX_HANDLE_NORMAL:
      return float4(dc.average_normal, 0.0f);
    default:
      return float4(0.0f);
  }
}

float4 TextureHandle_eval_uv(TextureHandle tex, float2 uv)
{
  return swap_alpha(TextureHandle_eval_uv_impl(tex, uv));
}

float4 closure_to_rgba(Closure /*cl*/)
{
  /* Workaround for gl_FragCoord. */
  FRAGMENT_SHADER_CREATE_INFO(eevee_nodetree);

  [[resource_table]] const Sampling &sampling = resource_table_get(eevee::Sampling);
  [[resource_table]] const UtilityTexture &util_tx = resource_table_get(UtilityTexture);
  float4 out_color;
  out_color.rgb = g_emission;
  out_color.a = saturate(1.0f - average(g_transmittance));

  /* Reset for the next closure tree. */
  float noise = util_tx.fetch(gl_FragCoord.xy, UTIL_BLUE_NOISE_LAYER).r;
  float closure_rand = fract(noise + sampling.rng_1D_get(SAMPLING_CLOSURE));
  closure_weights_reset(closure_rand);

  return out_color;
}

[[fragment]] [[early_fragment_tests]]
void surf_deferred_npr([[resource_table]] NprDeferred &srt,
                    [[resource_table]] const eevee::LightRenderData &light_data,
                    [[resource_table]] const eevee::LightShaderEvalData &light_shader_data,
                    [[resource_table]] gbuffer::Reader &reader,
                    [[resource_table]] RenderPassOutput &render_passes,
                    [[resource_table]] eevee::LightprobeRenderData & /*lightprobes*/,
                    [[resource_table]] const draw::Infos &infos,
                    [[resource_table]] const draw::View &views,
                    [[resource_table]] const Uniform &uni,
                    [[resource_table]] const Sampling &sampling,
                    [[resource_table]] const UtilityTexture &util_tx,
                    [[resource_table]] const HiZ &hiz,
                    [[frag_coord]] const float4 frag_co,
                    [[front_facing]] const bool front_face,
                    [[out]] NprFragOut &frag_out)
{
  const ViewMatrices view = views.get(0);

  init_globals(uni, view, front_face);

  npr_globals_load(int2(frag_co.xy));

  float4 out_color = nodetree_npr();

  /* Evaluate the closure tree so that node functions relying on the closure state (emission,
   * transmittance) stay consistent with the regular deferred path. */
  nodetree_surface(0.0f);

  /* Keep the same combined-buffer alpha convention as the deferred combine pass. */
  frag_out.radiance = float4(out_color.rgb, 0.0f);
  frag_out.radiance = colorspace::safe_color(frag_out.radiance);
}

}  // namespace eevee
