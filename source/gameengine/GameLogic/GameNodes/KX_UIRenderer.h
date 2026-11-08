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
  /** B6.4: virtualized elements skipped because they are outside their clip. */
  int culled = 0;
  /** B6.6: filled circle draws requested; one per visible slider knob. */
  int circle_calls = 0;
  /** B6.6: segment count of the most recent knob circle (16 means 16-gon). */
  int circle_segments = 0;
  /** B6.6: radius of the most recent knob circle, in pixels. */
  float circle_radius = 0.0f;
  /** B6.6: max |vertex distance - radius| over the emitted fan vertices. */
  float circle_radius_deviation = 0.0f;
};

/** B6.6: one run of label text with its own color. */
struct KX_UIRichSegment {
  std::string text;
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  /**
   * B6.7/G.3: per-run font size from `{size:N}`. Zero means "use the element
   * font size", which is also the value for runs outside any size tag.
   */
  float size = 0.0f;
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
   * B6.6: place `children` evenly on a circle.
   *
   * `angle_i = start_angle + i * 360 / count` (degrees, screen space so y
   * grows downwards) and `position = center + radius * (cos, sin)`. A single
   * child or radius 0 are valid and collapse onto the center.
   */
  static void LayoutPolar(const std::vector<KX_UIElement *> &children,
                          const float center[2],
                          float radius,
                          float start_angle_degrees);

  /**
   * B6.6: split `{color:red}...{/color}` / `{color:#RRGGBB}...{/color}`
   * markup into colored runs. G.3 adds `{size:N}...{/size}` runs.
   *
   * Color nesting keeps the B6.6 contract: the innermost color wins and the
   * renderer warns once. Size nesting is stack based, so `{/size}` restores
   * the enclosing size. Bold/italic/underline have no BLF equivalent and are
   * deliberately not accepted as markup.
   */
  static std::vector<KX_UIRichSegment> ParseRichText(const std::string &text,
                                                     const float base_color[4]);

  /** B6.6: 0 until a nested `{color:` run was seen, then 1 (warned once). */
  static int RichTextNestedWarnings();

  /**
   * Resolved screen rectangle of a handle after the last Draw().
   *
   * Returns false when the handle was never laid out. Used by the headless
   * harness to assert layout results (polar placement, anchors).
   */
  bool ResolvedRect(int handle, float out[4]) const;

  /**
   * B6.7: load an extra font face and expose it under `name`.
   *
   * Uses BLF_load_unique, so the default font stack (loaded once by the
   * adapter) keeps its single-load guard. Returns false on an empty name or
   * path, or when BLF rejects the file.
   */
  static bool LoadFont(const std::string &name, const std::string &path);
  /** B6.7: bind an already loaded BLF font id to `name`. */
  static void RegisterFont(const std::string &name, int font_id);
  /** B6.7: 0 means "default face"; an unknown name warns once per process. */
  static int ResolveFont(const std::string &name);
  /** B6.7: 0 or 1, the single unknown-font warning. */
  static int MissingFontWarnings();

  /**
   * Test seam: text rows and row heights as laid out by the last Draw().
   *
   * Vertical text produces one row per UTF-8 character with the row height
   * equal to the font size; horizontal text keeps the newline rows.
   */
  bool LaidOutLines(int handle, std::vector<std::string> *lines, std::vector<float> *heights) const;

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
