/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2026 EBC.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file KX_UIRenderer.h
 *  \ingroup gamelogic
 *
 *  Screen space renderer for the B4.1 UI elements.
 *
 *  Layout and drawing are separated: layout runs every frame but is cheap,
 *  while the per element GPU work is rebuilt only when the element revision
 *  changes. The bench harness uses the same cache with the GPU pass disabled,
 *  so the reported numbers measure the dirty classification itself.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "KX_UIElement.h"

struct KX_UIRenderStats {
  double idle_ms = 0.0;
  double full_ms = 0.0;
  /** Batches rebuilt during the idle pass; must stay zero. */
  int idle_rebuilds = 0;
  /** Batches rebuilt during the full update pass. */
  int full_rebuilds = 0;
  int elements = 0;
};

class KX_UIRenderer {
 public:
  KX_UIRenderer();
  ~KX_UIRenderer();

  /** Resolve layout and draw; #use_gpu is false in headless benches. */
  void Draw(const std::vector<KX_UIElement> &elements, int screen_w, int screen_h, bool use_gpu);

  /** Drop every cached rect and texture. */
  void Invalidate();

  const KX_UIRenderStats &Stats() const;

  /**
   * Synthetic 100 label + 50 button + 10 image scene.
   *
   * Runs two passes, one with unchanged revisions and one with every revision
   * bumped, and writes a report to #log_path (/tmp/ui_renderer_bench.log).
   */
  static KX_UIRenderStats Bench(const std::string &log_path,
                                int labels,
                                int buttons,
                                int images);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
