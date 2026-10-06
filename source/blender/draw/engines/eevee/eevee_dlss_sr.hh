/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Optional DLSS Super Resolution (DLSS SR) integration boundary for EEVEE.
 *
 * Performs low-resolution render input → display-resolution output upscaling through an
 * NGX-backed vendor executor. Unsupported backends preserve native EEVEE output.
 */

#pragma once

#include <memory>

#include "BLI_math_vector_types.hh"

#include "DRW_gpu_wrapper.hh"
#include "draw_pass.hh"

namespace blender::gpu {
class Texture;
}

namespace blender::eevee {

class Instance;

struct DlssSRFrameInputs {
  gpu::Texture *color = nullptr;
  gpu::Texture *depth = nullptr;
  gpu::Texture *velocity = nullptr;
  int2 input_extent = int2(0);
  int2 output_extent = int2(0);
  float2 jitter = float2(0.0f);
  bool reset_history = false;
  bool is_viewport = false;
  float sharpness = 0.3f;
  float mvec_scale = 1.0f;
  bool use_anti_ghost = true;
  int quality_preset = 4;
  float exposure_scale = 1.0f;
};

class DlssSRVulkanSession {
 public:
  DlssSRVulkanSession();
  ~DlssSRVulkanSession();

  bool available() const;
  const char *status() const;
  double gpu_time_ms() const;

  bool ensure_resources(const int2 &input_extent,
                        const int2 &output_extent,
                        int quality_preset,
                        bool depth_reverse_z,
                        bool force_recreate);
  void retry_initialization();

  gpu::Texture *color_texture();
  gpu::Texture *depth_texture();
  gpu::Texture *velocity_texture();
  gpu::Texture *output_texture();

  bool copy_inputs_and_evaluate(const DlssSRFrameInputs &frame, bool color_is_scene_linear);
  bool wait_for_output();
  void warmup();
};

class DlssSRModule {
 private:
  Instance &inst_;
  bool reported_ = false;
  bool active_reported_ = false;
  bool failure_reported_ = false;
  bool retry_blocked_ = false;
  bool force_history_reset_ = true;
  int2 retry_input_extent_ = int2(-1);
  int2 retry_output_extent_ = int2(-1);
  std::unique_ptr<DlssSRVulkanSession> vulkan_session_;
  gpu::Texture *last_display_texture_ = nullptr;
  draw::Texture sr_output_tx_ = {"DLSS_SR.Output"};
  draw::Framebuffer sr_input_stage_fb_ = {"DLSS_SR.InputStage"};
  draw::PassSimple sr_input_stage_ps_ = {"DLSS_SR.InputStage"};

  void publish_status(bool viewport, const char *status);

 public:
  explicit DlssSRModule(Instance &inst);
  ~DlssSRModule();

  gpu::Texture *process(const DlssSRFrameInputs &inputs, draw::View &view);
  void warmup();

  void invalidate()
  {
    last_display_texture_ = nullptr;
    retry_blocked_ = false;
    force_history_reset_ = true;
  }
  bool available() const;
  const char *status() const;
  gpu::Texture *display_texture() const;
};

}  // namespace blender::eevee
