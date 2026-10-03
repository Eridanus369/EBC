/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "DRW_gpu_wrapper.hh"
#include "draw_pass.hh"

namespace blender::eevee {

class Instance;
class View;

/**
 * \brief Screen-space outline post-process module.
 *
 * Materials with an Outline Control node write line parameters into the outline color/info
 * gbuffers. This module detects edge pixels (depth / normal / outline id discontinuities),
 * floods a nearest-seed field with jump-flood, and resolves final line coverage into color,
 * depth and velocity textures consumed by the film.
 */
class OutlineModule {
 private:
  Instance &inst_;
  bool enabled_ = false;
  bool use_in_combined_ = false;
  /* Set during object sync if a visible material uses an Outline Control node. */
  bool has_visible_outline_materials_ = false;

  PassSimple detect_ps_ = {"Outline.Detect"};
  PassSimple factor_blur_ps_ = {"Outline.FactorBlur"};
  PassSimple jfa_init_ps_ = {"Outline.JFA.Init"};
  PassSimple jfa_step_ps_ = {"Outline.JFA.Step"};
  PassSimple resolve_ps_ = {"Outline.Resolve"};
  PassSimple composite_ps_ = {"Outline.Composite"};

  Framebuffer detect_fb_ = {"Outline.Detect.FB"};
  Framebuffer factor_blur_fb_ = {"Outline.FactorBlur.FB"};
  Framebuffer jfa_init_fb_ = {"Outline.JFA.Init.FB"};
  Framebuffer resolve_fb_ = {"Outline.Resolve.FB"};

  SwapChain<TextureFromPool, 2> edge_seed_tx_;
  SwapChain<TextureFromPool, 2> jfa_tx_;
  TextureFromPool resolved_outline_tx_ = {"Outline.Resolved"};
  TextureFromPool resolved_depth_tx_ = {"Outline.ResolvedDepth"};
  TextureFromPool resolved_velocity_tx_ = {"Outline.ResolvedVelocity"};

  int jfa_step_size_ = 1;
  int3 jfa_dispatch_size_ = int3(1);
  int3 composite_dispatch_size_ = int3(1);

 public:
  OutlineModule(Instance &inst) : inst_(inst) {}

  void begin_sync();
  /** Called from the mesh sync path for objects that have outline materials. */
  void sync_object_marker();

  void sync();
  void render(View &view, int2 extent);
  void release_result();

  bool enabled() const
  {
    return enabled_;
  }

  bool use_in_combined() const
  {
    return enabled_ && use_in_combined_;
  }

  bool has_result() const
  {
    return resolved_outline_tx_.is_valid();
  }

  gpu::Texture *resolved_texture_or(gpu::Texture *fallback)
  {
    return has_result() ? resolved_outline_tx_.gpu_texture() : fallback;
  }

  gpu::Texture *resolved_texture()
  {
    return has_result() ? resolved_outline_tx_.gpu_texture() : nullptr;
  }

  gpu::Texture *resolved_depth_texture()
  {
    return has_result() && resolved_depth_tx_.is_valid() ? resolved_depth_tx_.gpu_texture() :
                                                           nullptr;
  }

  gpu::Texture *resolved_velocity_texture()
  {
    return has_result() && resolved_velocity_tx_.is_valid() ?
               resolved_velocity_tx_.gpu_texture() :
               nullptr;
  }
};

}  // namespace blender::eevee
