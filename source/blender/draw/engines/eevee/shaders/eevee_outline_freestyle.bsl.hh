/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR: Freestyle edge outline seed pass.
 *
 * Renders the freestyle_edges line batch into the outline seed buffer. Depth is compared manually
 * against the scene depth (the detect framebuffer has no depth attachment), so covered segments
 * are discarded. The seed layout matches the detect pass: .r = width modulation factor (1.0 =
 * unmodulated), .a = full line width. Color is read from outline_color_tx in the resolve pass. */

#pragma once

#include "draw_model.bsl.hh"
#include "draw_view.bsl.hh"
#include "eevee_defines.hh"
#include "eevee_outline.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "gpu_shader_utildefines.bsl.hh"

namespace eevee::outline {

struct FreestyleVertIn {
  [[attribute(0)]] float3 pos;
};

struct FreestyleResources {
  [[sampler(OUTLINE_DEPTH_TEX_SLOT)]] sampler2DDepth depth_tx;
  [[sampler(OUTLINE_COLOR_TEX_SLOT)]] sampler2D outline_color_tx;
  [[sampler(OUTLINE_INFO_TEX_SLOT)]] usampler2D outline_info_tx;
};

struct FreestyleFragOut {
  [[frag_color(0)]] float4 seed;
};

[[vertex]] [[clip_control]]
void freestyle_vert([[resource_table]] const draw::View &views,
                    [[resource_table]] const draw::Model &models,
                    [[resource_table]] const draw::Resource &res_id,
                    [[instance_index]] const int inst_index,
                    [[in]] const FreestyleVertIn &v_in,
                    [[position]] float4 &out_position)
{
  const draw::ID id = res_id.get(inst_index);
  const uint resource_id = id.resource_id<1>();
  const ObjectMatrices obj = models.get(resource_id);
  const ViewMatrices view = views.get(0);
  const float3 world_pos = obj.point_object_to_world(v_in.pos);
  out_position = reverse_z::transform(view.point_world_to_homogenous(world_pos));
}

[[fragment]]
void freestyle_frag([[resource_table]] const FreestyleResources &srt,
                    [[frag_coord]] const float4 frag_co,
                    [[out]] FreestyleFragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const int2 extent = textureSize(srt.depth_tx, 0);
  if (any(lessThan(texel, int2(0))) || any(greaterThanEqual(texel, extent))) {
    gpu_discard_fragment();
    return;
  }

  const float line_depth = reverse_z::read(frag_co.z);
  const float scene_depth = reverse_z::read(texelFetch(srt.depth_tx, texel, 0).r);
  if (line_depth > scene_depth + 1.0e-5f) {
    gpu_discard_fragment();
    return;
  }

  const float4 outline_color = texelFetch(srt.outline_color_tx, texel, 0);
  const uint4 outline_info = texelFetch(srt.outline_info_tx, texel, 0);
  const float line_width = outline_width_unpack(outline_info.r);
  if (line_width <= 0.0f || outline_color.a <= 0.0f ||
      !outline_freestyle_edge_unpack(outline_info.b))
  {
    gpu_discard_fragment();
    return;
  }

  frag_out.seed = float4(1.0f, 0.0f, 0.0f, line_width);
}

}  // namespace eevee::outline

PipelineGraphic eevee_outline_freestyle(eevee::outline::freestyle_vert,
                                        eevee::outline::freestyle_frag);
