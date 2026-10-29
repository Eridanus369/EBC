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

void CausticMap::sync(int2 extent)
{
  /* The deferred pipeline always binds these textures, so they must exist even when
   * caustics are disabled (in which case `caustic_resolution` can be zero). */
  int2 res = extent;
  if (res.x <= 0) {
    res.x = 512;
  }
  if (res.y <= 0) {
    res.y = res.x;
  }
  if (resolution_ != res || !color_tx_.is_valid() || !accum_r_tx_.is_valid()) {
    resolution_ = res;
    
    /* Stack the 4 cascade levels vertically into a single texture. */
    int2 color_extent = int2(resolution_.x, resolution_.y * LEVELS);
    
    color_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, color_extent, GPU_TEXTURE_USAGE_GENERAL);
    /* Level 0 only: the splat pass works in screen space. */
    const int2 accum_extent = resolution_;
    accum_r_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);
    accum_g_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);
    accum_b_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);
    accum_depth_tx_.ensure_2d(gpu::TextureFormat::UINT_32, accum_extent, GPU_TEXTURE_USAGE_GENERAL);

    /* World space volumetric accumulation grid, shared with the volume scatter pass. */
    const int3 volume_extent = int3(CAUSTIC_VOLUME_RES);
    const eGPUTextureUsage volume_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                          GPU_TEXTURE_USAGE_SHADER_WRITE |
                                          GPU_TEXTURE_USAGE_ATOMIC;
    volume_accum_r_tx_.ensure_3d(gpu::TextureFormat::UINT_32, volume_extent, volume_usage);
    volume_accum_g_tx_.ensure_3d(gpu::TextureFormat::UINT_32, volume_extent, volume_usage);
    volume_accum_b_tx_.ensure_3d(gpu::TextureFormat::UINT_32, volume_extent, volume_usage);
    
    for (int i = 0; i < LEVELS; ++i) {
      CausticLevelData &data = level_data_buf_[i];
      float scale = powf(2.0f, float(i)); /* Placeholder bounds */
      data.min_bound = float3(-scale, -scale, -scale);
      data.max_bound = float3(scale, scale, scale);
      data.resolution = resolution_.x;
    }
    level_data_buf_.push_update();
  }
}

void CausticMap::clear(bool clear_color)
{
  if (clear_color) {
    const float zero_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GPU_texture_clear(color_tx_, GPU_DATA_FLOAT, zero_color);
  }
  const uint32_t zero_accum[4] = {0u, 0u, 0u, 0u};
  GPU_texture_clear(accum_r_tx_, GPU_DATA_UINT, zero_accum);
  GPU_texture_clear(accum_g_tx_, GPU_DATA_UINT, zero_accum);
  GPU_texture_clear(accum_b_tx_, GPU_DATA_UINT, zero_accum);
  /* Initialize to +Infinity (float bit pattern) so the splat pass can use `imageAtomicMin`. */
  const uint32_t inf_depth[4] = {0x7F800000u, 0u, 0u, 0u};
  GPU_texture_clear(accum_depth_tx_, GPU_DATA_UINT, inf_depth);
  GPU_texture_clear(volume_accum_r_tx_, GPU_DATA_UINT, zero_accum);
  GPU_texture_clear(volume_accum_g_tx_, GPU_DATA_UINT, zero_accum);
  GPU_texture_clear(volume_accum_b_tx_, GPU_DATA_UINT, zero_accum);
}

} // namespace blender::eevee
