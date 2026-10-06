/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_velocity.bsl.hh"
#include "gpu_shader_fullscreen_lib.glsl"

namespace eevee::dlss_sr {

struct VertOut {
  [[smooth]] float2 screen_uv;
};

[[vertex]]
void fullscreen_vert([[vertex_id]] const int vert_id,
                     [[position]] float4 &out_position,
                     [[out]] VertOut &v_out)
{
  fullscreen_vertex(vert_id, out_position, v_out.screen_uv);
  out_position = reverse_z::transform(out_position);
}

struct ColorResources {
  [[sampler(0)]] sampler2D color_tx;
  [[push_constant]] float mvec_scale;
};

struct DepthResources {
  [[sampler(0)]] sampler2DDepth depth_tx;
};

struct VelocityResources {
  [[sampler(0)]] sampler2D velocity_tx;
  [[sampler(1)]] sampler2DDepth depth_tx;
  [[resource_table]] srt_t<CameraVelocity> camera;
  [[push_constant]] float mvec_scale;
};

struct ResolveResources {
  [[push_constant]] float sharpness;
  [[push_constant]] int anti_ghost;
  [[sampler(0)]] sampler2D sr_output_tx;
  [[sampler(1)]] sampler2D source_tx;
};

struct FragOut {
  [[frag_color(0)]] float4 color;
};

struct VelocityFragOut {
  [[frag_color(0)]] float4 velocity;
};

float4 sr_sample_bilinear(sampler2D tex, float2 uv)
{
  return textureLod(tex, clamp(uv, 0.0f.xx, float2(1.0f - 1e-4f)), 0.0f);
}

[[fragment]]
void input_stage_frag([[resource_table]] const ColorResources &resources,
                      [[in]] const VertOut &v_in,
                      [[out]] FragOut &frag_out)
{
  frag_out.color = textureLod(resources.color_tx, v_in.screen_uv, 0.0f);
}

[[fragment]]
void depth_stage_frag([[resource_table]] const DepthResources &resources,
                      [[in]] const VertOut &v_in,
                      [[out]] FragOut &frag_out)
{
  const float d = textureLod(resources.depth_tx, v_in.screen_uv, 0.0f).x;
  frag_out.color = float4(reverse_z::read(d));
}

[[fragment]]
void velocity_stage_frag([[resource_table]] const VelocityResources &resources,
                         [[resource_table]] const draw::View &views,
                         [[frag_coord]] const float4 frag_co,
                         [[out]] VelocityFragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const float depth = reverse_z::read(texelFetch(resources.depth_tx, texel, 0).r);
  [[resource_table]] const CameraVelocity &camera = resources.camera;
  const float2 motion = camera.resolve(views, resources.velocity_tx, texel, depth).xy;
  const float2 pix_motion = -motion * float2(textureSize(resources.velocity_tx, 0)) *
                            resources.mvec_scale;
  frag_out.velocity = float4(pix_motion, 0.0f, 1.0f);
}

[[fragment]]
void resolve_frag([[resource_table]] const ResolveResources &resources,
                  [[in]] const VertOut &v_in,
                  [[out]] FragOut &frag_out)
{
  const float2 uv = v_in.screen_uv;
  const float4 sr_sample = textureLod(resources.sr_output_tx, uv, 0.0f);
  const float4 src_sample = textureLod(resources.source_tx, uv, 0.0f);
  const float sharp = clamp(resources.sharpness, 0.0f, 1.0f);

  float3 sharpened = sr_sample.rgb;
  if (sharp > 1e-4f) {
    const float2 texel = float2(1.0f) / float2(textureSize(resources.sr_output_tx, 0).xy);
    const float3 c = sr_sample.rgb;
    const float3 sum = textureLodOffset(resources.sr_output_tx, uv, 0.0f, int2(-1, 0)).rgb +
                      textureLodOffset(resources.sr_output_tx, uv, 0.0f, int2(1, 0)).rgb +
                      textureLodOffset(resources.sr_output_tx, uv, 0.0f, int2(0, -1)).rgb +
                      textureLodOffset(resources.sr_output_tx, uv, 0.0f, int2(0, 1)).rgb;
    const float3 blurred = (c * 4.0f + sum) * (1.0f / 8.0f);
    sharpened = mix(c, c + (c - blurred) * 2.0f, sharp);
  }

  if (resources.anti_ghost == 0) {
    frag_out.color = float4(max(sharpened, float3(0.0f)), sr_sample.a);
    return;
  }

  const float3 lo = min(src_sample.rgb * 0.95f, sharpened);
  const float3 hi = max(src_sample.rgb * 1.05f, sharpened);
  frag_out.color = float4(clamp(sharpened, min(lo, hi), max(lo, hi)), sr_sample.a);
}

}  // namespace eevee::dlss_sr

PipelineGraphic eevee_dlss_sr_input_stage(eevee::dlss_sr::fullscreen_vert,
                                           eevee::dlss_sr::input_stage_frag);
PipelineGraphic eevee_dlss_sr_depth_stage(eevee::dlss_sr::fullscreen_vert,
                                           eevee::dlss_sr::depth_stage_frag);
PipelineGraphic eevee_dlss_sr_velocity_stage(eevee::dlss_sr::fullscreen_vert,
                                              eevee::dlss_sr::velocity_stage_frag);
PipelineGraphic eevee_dlss_sr_resolve(eevee::dlss_sr::fullscreen_vert,
                                      eevee::dlss_sr::resolve_frag);
