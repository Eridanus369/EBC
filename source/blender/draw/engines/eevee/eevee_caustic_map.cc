/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "eevee_caustic_map.hh"
#include "eevee_instance.hh"

namespace blender::eevee {

void CausticMap::init()
{
  /* Lazy initialization in sync(). */
}

void CausticMap::sync(int resolution)
{
  /* The deferred pipeline always binds these textures, so they must exist even when
   * caustics are disabled (in which case `caustic_resolution` can be zero). */
  const int res = (resolution > 0) ? resolution : 64;
  if (resolution_ != res || !color_tx_.is_valid() || !accum_r_tx_.is_valid()) {
    resolution_ = res;
    
    /* Stack the 4 cascade levels vertically into a single texture. */
    int2 extent = int2(resolution_, resolution_ * LEVELS);
    
    color_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, extent, GPU_TEXTURE_USAGE_GENERAL);
    /* Level 0 only: the splat pass works in screen space. */
    const int2 accum_extent = int2(resolution_);
    accum_r_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);
    accum_g_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);
    accum_b_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);
    
    for (int i = 0; i < LEVELS; ++i) {
      CausticLevelData &data = level_data_buf_[i];
      float scale = powf(2.0f, float(i)); /* Placeholder bounds */
      data.min_bound = float3(-scale, -scale, -scale);
      data.max_bound = float3(scale, scale, scale);
      data.resolution = resolution_;
    }
    level_data_buf_.push_update();
  }
}

void CausticMap::clear()
{
  const float zero_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  GPU_texture_clear(color_tx_, GPU_DATA_FLOAT, zero_color);
  const uint32_t zero_accum[4] = {0u, 0u, 0u, 0u};
  GPU_texture_clear(accum_r_tx_, GPU_DATA_UINT, zero_accum);
  GPU_texture_clear(accum_g_tx_, GPU_DATA_UINT, zero_accum);
  GPU_texture_clear(accum_b_tx_, GPU_DATA_UINT, zero_accum);
}

} // namespace blender::eevee
