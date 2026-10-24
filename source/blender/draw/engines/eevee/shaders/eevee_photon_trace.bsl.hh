/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "draw_view.bsl.hh"
#include "eevee_uniform.bsl.hh"
#include "eevee_ray_trace_screen_lib.bsl.hh"
#include "eevee_gbuffer_read.bsl.hh"
#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.hh"

namespace eevee::photon {

struct Resources {
  [[storage(0, read_write)]] Photon (&photons)[];
  [[sampler(0)]] sampler2D hiz_tx;
};

struct PushConstants {
  [[push_constant]] int photon_count;
};

[[compute, local_size(256, 1, 1)]]
void trace([[resource_table]] Resources &srt,
           [[resource_table]] const Uniform &uni,
           [[resource_table]] const draw::View &views,
           [[resource_table]] const gbuffer::Reader &reader,
           [[resource_table]] PushConstants &pc,
           [[work_group_id]] const uint3 group_id,
           [[local_invocation_id]] const uint3 local_id)
{
  uint idx = (group_id.x * 256) + local_id.x;
  if (idx >= uint(pc.photon_count)) { return; }

  Photon p = srt.photons[idx];
  
  const ViewMatrices view = views.get(0);
  
  Ray ray;
  ray.origin = p.position;
  ray.direction = p.direction;
  ray.max_time = abs(view.far() - view.near());

  Ray ray_view;
  ray_view.origin = transform_point(view.viewmat, ray.origin);
  ray_view.direction = transform_direction(view.viewmat, ray.direction);
  ray_view.max_time = ray.max_time;

  ScreenTraceHitData hit = raytrace_screen(view,
                                           uni.raytrace_buf,
                                           uni.uniform_buf.hiz,
                                           srt.hiz_tx,
                                           float(idx) * 0.618033988749f,
                                           0.0f,
                                           true,
                                           ray_view);

  if (hit.valid) {
    float3 hit_P = transform_point(view.viewinv, hit.v_hit_P);
    p.position = hit_P;

    /* 先只更新位置，不做折射，保证编译通过 */
    p.position = hit_P;
  }

  srt.photons[idx] = p;
}

} // namespace eevee::photon

PipelineCompute eevee_photon_trace(eevee::photon::trace);
