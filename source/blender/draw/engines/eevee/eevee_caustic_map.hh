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
  Texture count_tx_ = {"caustic_count_tx"};
  
  /* Bounds and resolution data for each level. */
  UniformArrayBuffer<CausticLevelData, LEVELS> level_data_buf_ = {"caustic_levels"};

 public:
  CausticMap(Instance &inst) : inst_(inst) {}
  ~CausticMap() {}

  void init();
  void sync(int resolution);
  void clear();
  
  Texture &color_texture() { return color_tx_; }
  Texture &count_texture() { return count_tx_; }
  UniformArrayBuffer<CausticLevelData, LEVELS> &level_data() { return level_data_buf_; }
};

} // namespace blender::eevee
