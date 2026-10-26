/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "eevee_caustics.hh"
#include "eevee_instance.hh"
#include "draw_view.hh"

namespace blender::eevee {

CausticModule::CausticModule(Instance &inst) : inst_(inst), caustic_map_(inst) {}

Texture &CausticModule::color_texture()
{
  return caustic_map_.color_texture();
}

void CausticModule::init()
{
  caustic_map_.init();
}

void CausticModule::sync()
{
  enabled_ = inst_.scene->eevee.caustics.use_spectral_caustics;
  /* Startup files authored before the caustic settings existed load with these fields zeroed, so
   * fall back to the intended defaults instead of silently rendering nothing. */
  photon_count_ = math::max(inst_.scene->eevee.caustics.photon_count, 100000);
  spectral_samples_ = inst_.scene->eevee.caustics.dispersion_enabled ? 8 : 1;
  
  int res = inst_.scene->eevee.caustics.caustic_resolution;
  if (res <= 0) {
    res = 512;
  }
  caustic_map_.sync(res);

  render_pending_ = true;
}

void CausticModule::render(View &view)
{
  if (!render_pending_) {
    return;
  }
  render_pending_ = false;

  /* The combine pass samples this map unconditionally, so it must never keep stale data. */
  caustic_map_.clear();

  if (!enabled_) {
    return;
  }

  int active_count = math::min(photon_count_, 131072);
  if (active_count <= 0) {
    return;
  }

  int dispatch_groups = (active_count + 255) / 256;

  /* 1. Emit Pass */
  {
    PassSimple &pass = photon_emit_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_EMIT));
    
    pass.bind_ssbo("photons", photon_pool_);
    
    float3 light_pos = float3(0.0f, 0.0f, 5.0f);
    float3 light_dir = float3(0.0f, 0.0f, -1.0f);
    float light_radius = 0.5f;
    float light_power = 1000.0f;
    
    pass.push_constant("photon_count", &active_count);
    pass.push_constant("light_position", &light_pos);
    pass.push_constant("light_direction", &light_dir);
    pass.push_constant("light_radius", &light_radius);
    pass.push_constant("light_power", &light_power);
    
    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_STORAGE);
    inst_.manager->submit(pass, view);
  }

  /* 2. Trace Pass */
  {
    PassSimple &pass = photon_trace_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_TRACE));
    
    pass.bind_ssbo("photons", photon_pool_);
    pass.bind_texture("hiz_tx", &inst_.hiz_buffer.front.ref_tx_);
    pass.bind_texture("gbuf_header_tx", &inst_.gbuffer.header_tx);
    pass.bind_texture("gbuf_normal_tx", &inst_.gbuffer.normal_tx);
    
    pass.push_constant("photon_count", &active_count);
    const float4x4 &viewmat = view.viewmat();
    const float4x4 &winmat = view.winmat();
    const float4x4 &viewinv = view.viewinv();
    pass.push_constant("viewmat", &viewmat);
    pass.push_constant("winmat", &winmat);
    pass.push_constant("viewinv", &viewinv);
    
    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_STORAGE);
    inst_.manager->submit(pass, view);
  }

  /* 3. Splat Pass. Screen space only, so a single dispatch into the level 0 region. */
  {
    PassSimple &pass = photon_splat_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_SPLAT));

    pass.bind_ssbo("photons", photon_pool_);
    pass.bind_image("caustic_accum_r_img", &caustic_map_.accum_r_texture());
    pass.bind_image("caustic_accum_g_img", &caustic_map_.accum_g_texture());
    pass.bind_image("caustic_accum_b_img", &caustic_map_.accum_b_texture());

    pass.push_constant("photon_count", &active_count);

    const float4x4 &viewmat = view.viewmat();
    const float4x4 &winmat = view.winmat();
    pass.push_constant("viewmat", &viewmat);
    pass.push_constant("winmat", &winmat);

    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
    inst_.manager->submit(pass, view);
  }

  /* 4. Resolve Pass: convert the integer accumulation into the float map sampled by the
   * deferred combine. */
  {
    PassSimple &pass = photon_resolve_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_RESOLVE));

    pass.bind_image("caustic_accum_r_img", &caustic_map_.accum_r_texture());
    pass.bind_image("caustic_accum_g_img", &caustic_map_.accum_g_texture());
    pass.bind_image("caustic_accum_b_img", &caustic_map_.accum_b_texture());
    pass.bind_image("caustic_color_img", &caustic_map_.color_texture());

    const int res = caustic_map_.resolution();
    const int res_xy = (res + 7) / 8;
    pass.dispatch(int3(res_xy, res_xy, 1));
    pass.barrier(GPU_BARRIER_TEXTURE_FETCH);
    inst_.manager->submit(pass, view);
  }
}

}  // namespace blender::eevee
