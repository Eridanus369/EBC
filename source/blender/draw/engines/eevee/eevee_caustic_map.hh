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
  
  int resolution_ = 0;

  /* Stacked texture holding all cascade levels vertically. */
  Texture color_tx_ = {"caustic_color_tx"};
  /* Integer accumulation targets of the photon splat pass. Overlapping photons can only be summed
   * with atomics, and image atomics only exist for single channel `r32ui` images, hence one texture
   * per color channel. Resolved into `color_tx_` by the photon resolve pass before the deferred
   * combine reads it. */
  Texture accum_r_tx_ = {"caustic_accum_r_tx"};
  Texture accum_g_tx_ = {"caustic_accum_g_tx"};
  Texture accum_b_tx_ = {"caustic_accum_b_tx"};
  
  /* Bounds and resolution data for each level. */
  UniformArrayBuffer<CausticLevelData, LEVELS> level_data_buf_ = {"caustic_levels"};

 public:
  CausticMap(Instance &inst) : inst_(inst) {}
  ~CausticMap() {}

  void init();
  void sync(int resolution);
  void clear();
  
  int resolution() const { return resolution_; }
  Texture &color_texture() { return color_tx_; }
  Texture &accum_r_texture() { return accum_r_tx_; }
  Texture &accum_g_texture() { return accum_g_tx_; }
  Texture &accum_b_texture() { return accum_b_tx_; }
  UniformArrayBuffer<CausticLevelData, LEVELS> &level_data() { return level_data_buf_; }
};

} // namespace blender::eevee
