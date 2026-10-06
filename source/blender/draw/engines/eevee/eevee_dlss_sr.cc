/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_dlss_sr.hh"

#include "CLG_log.h"
#include "DEG_depsgraph_query.hh"
#include "BLI_string.hh"

#include "GPU_context.hh"
#include "GPU_texture.hh"

#include "eevee_instance.hh"
#include "eevee_shader.hh"

namespace blender::eevee {

/* -------------------------------------------------------------------- */
/** \name DlssSRVulkanSession (platform stub)
 *
 * Default Linux / no-NGX build: every ensure_resources returns false so the adapter keeps
 * pass-through. Real Windows/Linux Vulkan NGX backends replace this pimpl with calls to
 * `NVSDK_NGX_VULKAN_Init / CreateFeature(DLSS_SuperSampling) / EvaluateFeature / ReleaseFeature`
 * or a Vulkan→D3D12 interop host that owns `nvngx_dlss.dll`.
 * \{ */

DlssSRVulkanSession::DlssSRVulkanSession() = default;
DlssSRVulkanSession::~DlssSRVulkanSession() = default;

bool DlssSRVulkanSession::available() const
{
  return false;
}

const char *DlssSRVulkanSession::status() const
{
  return "NGX DLSS SR backend not wired in this build (Vulkan/D3D12 interop required)";
}

double DlssSRVulkanSession::gpu_time_ms() const
{
  return -1.0;
}

bool DlssSRVulkanSession::ensure_resources(const int2 & /*input_extent*/,
                                           const int2 & /*output_extent*/,
                                           int /*quality_preset*/,
                                           bool /*depth_reverse_z*/,
                                           bool /*force_recreate*/)
{
  return false;
}

void DlssSRVulkanSession::retry_initialization()
{
}

gpu::Texture *DlssSRVulkanSession::color_texture()
{
  return nullptr;
}

gpu::Texture *DlssSRVulkanSession::depth_texture()
{
  return nullptr;
}

gpu::Texture *DlssSRVulkanSession::velocity_texture()
{
  return nullptr;
}

gpu::Texture *DlssSRVulkanSession::output_texture()
{
  return nullptr;
}

bool DlssSRVulkanSession::copy_inputs_and_evaluate(const DlssSRFrameInputs & /*frame*/,
                                                   bool /*color_is_scene_linear*/)
{
  return false;
}

bool DlssSRVulkanSession::wait_for_output()
{
  return false;
}

void DlssSRVulkanSession::warmup()
{
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name DlssSRModule
 * \{ */

DlssSRModule::DlssSRModule(Instance &inst)
    : inst_(inst), vulkan_session_(std::make_unique<DlssSRVulkanSession>())
{
}

DlssSRModule::~DlssSRModule() = default;

void DlssSRModule::publish_status(const bool viewport, const char *status)
{
  Scene *scene = DEG_get_original(inst_.scene);
  if (scene != nullptr && scene->runtime != nullptr) {
    (void)viewport;
    (void)status;
  }
}

gpu::Texture *DlssSRModule::process(const DlssSRFrameInputs &inputs, draw::View &view)
{
  last_display_texture_ = nullptr;
  if (inst_.scene == nullptr || inst_.scene->eevee.dlss5_mode != SCE_EEVEE_DLSS_SR) {
    retry_blocked_ = false;
    force_history_reset_ = true;
    return inputs.color;
  }

  if (!reported_) {
    CLOG_INFO(&Instance::log,
              "DLSS SR adapter: backend=%s input=%dx%d output=%dx%d viewport=%d quality=%d",
              GPU_backend_get_name(),
              inputs.input_extent.x,
              inputs.input_extent.y,
              inputs.output_extent.x,
              inputs.output_extent.y,
              inputs.is_viewport,
              inputs.quality_preset);
    reported_ = true;
  }

  if (GPU_backend_get_type() != GPU_BACKEND_VULKAN) {
    publish_status(inputs.is_viewport, "Unavailable: Vulkan backend required");
    static bool logged_backend_skip = false;
    if (!logged_backend_skip) {
      logged_backend_skip = true;
      CLOG_WARN(&Instance::log,
                "DLSS SR skipped: backend=%s (need Vulkan and restart)",
                GPU_backend_get_name());
    }
    return inputs.color;
  }

  if (inputs.color == nullptr || inputs.depth == nullptr || inputs.velocity == nullptr ||
      inputs.input_extent.x < 32 || inputs.input_extent.y < 32 ||
      inputs.output_extent.x < 32 || inputs.output_extent.y < 32)
  {
    return inputs.color;
  }
  if (inputs.input_extent == inputs.output_extent) {
    /* SR is requested but render and display match — nothing to upscale. */
    static bool logged_no_scale = false;
    if (!logged_no_scale) {
      logged_no_scale = true;
      CLOG_WARN(&Instance::log,
                "DLSS SR skipped: input==output %dx%d (reduce render scale or increase output)",
                inputs.input_extent.x,
                inputs.input_extent.y);
    }
    return inputs.color;
  }

  if (inst_.dlss_sr_settings_changed() ||
      (retry_blocked_ &&
       (retry_input_extent_ != inputs.input_extent || retry_output_extent_ != inputs.output_extent)))
  {
    retry_blocked_ = false;
    failure_reported_ = false;
    vulkan_session_->retry_initialization();
  }
  if (retry_blocked_) {
    return inputs.color;
  }

  if (!vulkan_session_->ensure_resources(inputs.input_extent,
                                         inputs.output_extent,
                                         inputs.quality_preset,
                                         true,
                                         force_history_reset_))
  {
    retry_blocked_ = true;
    retry_input_extent_ = inputs.input_extent;
    retry_output_extent_ = inputs.output_extent;
    publish_status(inputs.is_viewport, vulkan_session_->status());
    if (!failure_reported_) {
      CLOG_WARN(&Instance::log, "DLSS SR disabled until settings change: %s",
                vulkan_session_->status());
      failure_reported_ = true;
    }
    return inputs.color;
  }

  gpu::Texture *sr_color = vulkan_session_->color_texture();
  gpu::Texture *sr_depth = vulkan_session_->depth_texture();
  gpu::Texture *sr_velocity = vulkan_session_->velocity_texture();
  gpu::Shader *sr_stage = inst_.shaders.static_shader_get(DLSS_SR_INPUT_STAGE);
  if (sr_color == nullptr || sr_depth == nullptr || sr_velocity == nullptr || sr_stage == nullptr)
  {
    CLOG_WARN(&Instance::log, "DLSS SR staging resources unavailable");
    return inputs.color;
  }

  sr_output_tx_.ensure_2d(
      gpu::TextureFormat::SFLOAT_16_16_16_16,
      inputs.output_extent,
      GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  if (sr_output_tx_.gpu_texture() == nullptr) {
    CLOG_WARN(&Instance::log, "DLSS SR output texture unavailable");
    return inputs.color;
  }

  const GPUSamplerState no_filter = GPUSamplerState::default_sampler();
  gpu::Texture *color_tx = inputs.color;
  gpu::Texture *depth_tx = inputs.depth;
  gpu::Texture *velocity_tx = inputs.velocity;
  sr_input_stage_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(sr_color));
  sr_input_stage_ps_.init();
  sr_input_stage_ps_.state_set(DRW_STATE_WRITE_COLOR);
  sr_input_stage_ps_.framebuffer_set(&sr_input_stage_fb_);
  sr_input_stage_ps_.shader_set(sr_stage);
  sr_input_stage_ps_.bind_texture("color_tx", &color_tx, no_filter);
  sr_input_stage_ps_.push_constant("mvec_scale", inputs.mvec_scale);
  sr_input_stage_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
  inst_.manager->submit(sr_input_stage_ps_);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_FRAMEBUFFER);

  gpu::Shader *depth_stage = inst_.shaders.static_shader_get(DLSS_SR_DEPTH_STAGE);
  if (depth_stage != nullptr) {
    sr_input_stage_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(sr_depth));
    sr_input_stage_ps_.init();
    sr_input_stage_ps_.state_set(DRW_STATE_WRITE_COLOR);
    sr_input_stage_ps_.framebuffer_set(&sr_input_stage_fb_);
    sr_input_stage_ps_.shader_set(depth_stage);
    sr_input_stage_ps_.bind_texture("depth_tx", &depth_tx, no_filter);
    sr_input_stage_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    inst_.manager->submit(sr_input_stage_ps_);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_FRAMEBUFFER);
  }
  gpu::Shader *vel_stage = inst_.shaders.static_shader_get(DLSS_SR_VELOCITY_STAGE);
  if (vel_stage != nullptr) {
    sr_input_stage_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(sr_velocity));
    sr_input_stage_ps_.init();
    sr_input_stage_ps_.state_set(DRW_STATE_WRITE_COLOR);
    sr_input_stage_ps_.framebuffer_set(&sr_input_stage_fb_);
    sr_input_stage_ps_.shader_set(vel_stage);
    sr_input_stage_ps_.bind_texture("velocity_tx", &velocity_tx, no_filter);
    sr_input_stage_ps_.bind_texture("depth_tx", &depth_tx, no_filter);
    inst_.velocity.bind_resources(sr_input_stage_ps_);
    sr_input_stage_ps_.push_constant("mvec_scale", inputs.mvec_scale);
    sr_input_stage_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    inst_.manager->submit(sr_input_stage_ps_, view);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_FRAMEBUFFER);
  }

  DlssSRFrameInputs exec_frame = inputs;
  exec_frame.reset_history = inputs.reset_history || force_history_reset_;
  if (!vulkan_session_->copy_inputs_and_evaluate(exec_frame, true)) {
    publish_status(inputs.is_viewport, vulkan_session_->status());
    if (!failure_reported_) {
      CLOG_WARN(&Instance::log, "DLSS SR evaluate skipped: %s", vulkan_session_->status());
      failure_reported_ = true;
    }
    return inputs.color;
  }
  if (!active_reported_) {
    CLOG_INFO(&Instance::log,
              "DLSS SR active: target=%s input=%dx%d output=%dx%d quality=%d",
              inputs.is_viewport ? "viewport" : "render",
              inputs.input_extent.x,
              inputs.input_extent.y,
              inputs.output_extent.x,
              inputs.output_extent.y,
              inputs.quality_preset);
    active_reported_ = true;
  }
  retry_blocked_ = false;
  failure_reported_ = false;
  if (!vulkan_session_->wait_for_output()) {
    return inputs.color;
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);

  gpu::Texture *ngx_out = vulkan_session_->output_texture();
  gpu::Shader *resolve = inst_.shaders.static_shader_get(DLSS_SR_RESOLVE);
  if (ngx_out != nullptr && resolve != nullptr) {
    sr_input_stage_fb_.ensure(GPU_ATTACHMENT_NONE,
                              GPU_ATTACHMENT_TEXTURE(sr_output_tx_.gpu_texture()));
    sr_input_stage_ps_.init();
    sr_input_stage_ps_.state_set(DRW_STATE_WRITE_COLOR);
    sr_input_stage_ps_.framebuffer_set(&sr_input_stage_fb_);
    sr_input_stage_ps_.shader_set(resolve);
    sr_input_stage_ps_.bind_texture("sr_output_tx", &ngx_out, no_filter);
    sr_input_stage_ps_.bind_texture("source_tx", &color_tx, no_filter);
    sr_input_stage_ps_.push_constant("sharpness", inputs.sharpness);
    sr_input_stage_ps_.push_constant("anti_ghost", inputs.use_anti_ghost ? 1 : 0);
    sr_input_stage_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    inst_.manager->submit(sr_input_stage_ps_);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_FRAMEBUFFER);
    last_display_texture_ = sr_output_tx_.gpu_texture();
  }
  else {
    last_display_texture_ = ngx_out != nullptr ? ngx_out : inputs.color;
  }
  force_history_reset_ = false;
  const double gpu_ms = vulkan_session_->gpu_time_ms();
  char status[160];
  if (gpu_ms >= 0.0) {
    SNPRINTF(status, "Active %dx%d | recent SR GPU %.2f ms", inputs.output_extent.x,
             inputs.output_extent.y, gpu_ms);
  }
  else {
    SNPRINTF(status, "Active %dx%d | GPU timing pending", inputs.output_extent.x,
             inputs.output_extent.y);
  }
  publish_status(inputs.is_viewport, status);
  return last_display_texture_;
}

void DlssSRModule::warmup()
{
  vulkan_session_->warmup();
}

bool DlssSRModule::available() const
{
  return vulkan_session_->available();
}

const char *DlssSRModule::status() const
{
  return vulkan_session_->status();
}

gpu::Texture *DlssSRModule::display_texture() const
{
  if (inst_.scene == nullptr || inst_.scene->eevee.dlss5_mode != SCE_EEVEE_DLSS_SR) {
    return nullptr;
  }
  return last_display_texture_;
}

}  // namespace blender::eevee
