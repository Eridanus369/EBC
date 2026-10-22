
/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "eevee_caustics.hh"
#include "eevee_instance.hh"

namespace blender::eevee {

CausticModule::CausticModule(Instance &inst) : inst_(inst), caustic_map_(inst) {}

void CausticModule::init()
{
  caustic_map_.init();
}

void CausticModule::sync()
{
  enabled_ = inst_.scene->eevee.caustics.use_spectral_caustics;
  photon_count_ = inst_.scene->eevee.caustics.photon_count;
  spectral_samples_ = inst_.scene->eevee.caustics.dispersion_enabled ? 8 : 1;
  caustic_map_.sync(inst_.scene->eevee.caustics.caustic_resolution);
}

void CausticModule::render()
{
  if (!enabled_) {
    return;
  }

  /* Ensure the GPU pool matches the requested count to avoid over-dispatch. */
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
  }

  /* 2. Trace Pass */
  {
    PassSimple &pass = photon_trace_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_TRACE));
    
    pass.bind_ssbo("photons", photon_pool_);
    pass.push_constant("photon_count", &active_count);
    
    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_STORAGE);
  }

  /* 3. Splat Pass */
  for (int i = 0; i < CausticMap::LEVELS; ++i) {
    PassSimple &pass = photon_splat_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_SPLAT));
    
    pass.bind_ssbo("photons", photon_pool_);
    pass.bind_image("caustic_color_img", &caustic_map_.color_texture());
    pass.bind_image("caustic_count_img", &caustic_map_.count_texture());
    pass.bind_ssbo("level_data", caustic_map_.level_data());
    
    pass.push_constant("level_idx", &i);
    pass.push_constant("photon_count", &active_count);
    
    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_TEXTURE_FETCH);
  }
}

}  // namespace blender::eevee
