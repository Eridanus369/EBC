/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Cascaded caustic maps for storing and querying photon splats.
 */

#pragma once

#include "draw_pass.hh"
#include "shaders/eevee_photon_shared.hh"

namespace blender::eevee {

using namespace draw;

class Instance;

class CausticMap {
 public:
  static constexpr int LEVELS = 4;

 private:
  Instance &inst_;
  
  int2 resolution_ = int2(0);

  /* Stacked texture holding all cascade levels vertically. */
  Texture color_tx_ = {"caustic_color_tx"};
  /* Integer accumulation targets of the photon splat pass. Overlapping photons can only be summed
   * with atomics, and image atomics only exist for single channel `r32ui` images, hence one texture
   * per color channel. Resolved into `color_tx_` by the photon resolve pass before the deferred
   * combine reads it. */
  Texture accum_r_tx_ = {"caustic_accum_r_tx"};
  Texture accum_g_tx_ = {"caustic_accum_g_tx"};
  Texture accum_b_tx_ = {"caustic_accum_b_tx"};
  /* Minimum camera distance of the photons accumulated in each texel. Used by the deferred combine
   * pass to reject caustics that are hidden behind the visible surface. */
  Texture accum_depth_tx_ = {"caustic_accum_depth_tx"};

  /* World space volumetric caustic accumulation. Sampled by the volume scatter pass so that
   * photons also produce light inside participating media (water, smoke, ...). */
  Texture volume_accum_r_tx_ = {"caustic_volume_accum_r_tx"};
  Texture volume_accum_g_tx_ = {"caustic_volume_accum_g_tx"};
  Texture volume_accum_b_tx_ = {"caustic_volume_accum_b_tx"};

  /* Bounds and resolution data for each level. */
  UniformArrayBuffer<CausticLevelData, LEVELS> level_data_buf_ = {"caustic_levels"};

 public:
  CausticMap(Instance &inst) : inst_(inst) {}
  ~CausticMap() {}

  void init();
  void sync(int2 extent);
  /* `clear_color` is false during a TAA sequence: the color map is progressively averaged over the
   * samples, so only the per frame accumulation targets may be reset. */
  void clear(bool clear_color = true);

  int2 resolution() const { return resolution_; }
  Texture &color_texture() { return color_tx_; }
  Texture &accum_r_texture() { return accum_r_tx_; }
  Texture &accum_g_texture() { return accum_g_tx_; }
  Texture &accum_b_texture() { return accum_b_tx_; }
  Texture &accum_depth_texture() { return accum_depth_tx_; }
  Texture &volume_accum_r_texture() { return volume_accum_r_tx_; }
  Texture &volume_accum_g_texture() { return volume_accum_g_tx_; }
  Texture &volume_accum_b_texture() { return volume_accum_b_tx_; }
  UniformArrayBuffer<CausticLevelData, LEVELS> &level_data() { return level_data_buf_; }
};

} // namespace blender::eevee
