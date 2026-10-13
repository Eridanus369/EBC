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

#ifdef WITH_DLSS
#  include "dlss5_ngx.hh"
#  ifdef WITH_VULKAN_BACKEND
#    include "vk_backend.hh"
#    include "vk_device.hh"
#    include "vk_image_view.hh"
#    include "vk_texture.hh"
#    include "vk_common.hh"
#  endif
#endif

namespace blender::eevee {

/* -------------------------------------------------------------------- */
/** 
ame DlssSRVulkanSession
 * \{ */

struct DlssSRVulkanSession::Impl {
  ngx::Runtime *runtime = nullptr;
  NVSDK_NGX_Parameter *parameters = nullptr;
  void *feature = nullptr;

  gpu::Texture *color_tx = nullptr;
  gpu::Texture *depth_tx = nullptr;
  gpu::Texture *velocity_tx = nullptr;
  gpu::Texture *output_tx = nullptr;

  int2 input_extent = int2(0);
  int2 output_extent = int2(0);
  int quality_preset = 0;
  bool depth_reverse_z = true;
  int retry_count = 0;

  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;

  double gpu_time_ms = -1.0;
  char status[192] = "DLSS SR: idle";
};


DlssSRVulkanSession::DlssSRVulkanSession() : impl_(new Impl())
{
  Impl &impl = *impl_;
  impl.runtime = ngx::runtime_get();
  STRNCPY(impl.status, "DLSS SR: Vulkan session idle");
}

DlssSRVulkanSession::~DlssSRVulkanSession()
{
  Impl &impl = *impl_;
  if (impl.feature != nullptr && impl.runtime != nullptr && impl.runtime->release_feature) {
    impl.runtime->release_feature(impl.feature);
    impl.feature = nullptr;
  }
  if (impl.parameters != nullptr && impl.runtime != nullptr &&
      impl.runtime->destroy_parameters)
  {
    impl.runtime->destroy_parameters(impl.parameters);
    impl.parameters = nullptr;
  }
  /* Vulkan command pool / buffer / fences are released by the device at shutdown. */
  delete impl_;
}

bool DlssSRVulkanSession::available() const
{
  return impl_->feature != nullptr;
}

const char *DlssSRVulkanSession::status() const
{
  return impl_->status;
}

double DlssSRVulkanSession::gpu_time_ms() const
{
  return impl_->gpu_time_ms;
}

bool DlssSRVulkanSession::ensure_resources(const int2 &input_extent,
                                           const int2 &output_extent,
                                           const int quality_preset,
                                           const bool depth_reverse_z,
                                           const bool force_recreate)
{
  Impl &impl = *impl_;

  if (impl.feature != nullptr && !force_recreate && impl.input_extent == input_extent &&
      impl.output_extent == output_extent && impl.quality_preset == quality_preset)
  {
    return true;
  }

  if (input_extent.x < 32 || input_extent.y < 32 || output_extent.x < 32 ||
      output_extent.y < 32)
  {
    STRNCPY(impl.status, "DLSS SR: extents too small (< 32)");
    return false;
  }

#if !defined(WITH_VULKAN_BACKEND) || !defined(WITH_DLSS)
  STRNCPY(impl.status, "DLSS SR: build lacks Vulkan backend or DLSS SDK");
  return false;
#else
  /* Release the previous feature if any. */
  if (impl.feature != nullptr && impl.runtime && impl.runtime->release_feature) {
    impl.runtime->release_feature(impl.feature);
    impl.feature = nullptr;
  }

  ngx::Runtime *runtime = impl.runtime;
  if (runtime == nullptr || !runtime->loaded) {
    STRNCPY(impl.status, runtime ? runtime->status : "DLSS SR: NGX runtime unavailable");
    return false;
  }

  gpu::VKBackend &vk_backend = gpu::VKBackend::get();
  gpu::VKDevice &vk_device = vk_backend.device;
  if (!vk_device.is_initialized()) {
    STRNCPY(impl.status, "DLSS SR: Vulkan device not initialized");
    return false;
  }

  const VkInstance vk_instance = vk_device.instance_get();
  const VkPhysicalDevice vk_phys_device = vk_device.physical_device_get();
  const VkDevice vk_handle = vk_device.vk_handle();

  /* Initialize NGX runtime once per process (idempotent). */
  if (!ngx::runtime_init(*runtime,
                         vk_instance,
                         vk_phys_device,
                         vk_handle,
                         reinterpret_cast<void *>(vkGetInstanceProcAddr),
                         reinterpret_cast<void *>(vkGetDeviceProcAddr)))
  {
    STRNCPY(impl.status, runtime->status);
    return false;
  }

  /* Create the parameter object used to set inputs. */
  if (impl.parameters == nullptr) {
    if (runtime->allocate_parameters(&impl.parameters) != ngx::RESULT_SUCCESS) {
      STRNCPY(impl.status, "DLSS SR: AllocateParameters failed");
      return false;
    }
  }

  /* Allocate the GPU textures that back the NGX inputs and output. If they already
   * exist with the right extent, keep them; otherwise reallocate. */
  if (impl.color_tx == nullptr || impl.input_extent != input_extent) {
    if (impl.color_tx != nullptr) {
      GPU_texture_free(impl.color_tx);
    }
    impl.color_tx = GPU_texture_create_2d("DLSS_SR.Color",
                                          input_extent.x,
                                          input_extent.y,
                                          1,
                                          gpu::TextureFormat::SFLOAT_16_16_16_16,
                                          GPU_TEXTURE_USAGE_SHADER_READ |
                                              GPU_TEXTURE_USAGE_SHADER_WRITE |
                                              GPU_TEXTURE_USAGE_ATTACHMENT,
                                          nullptr);
    if (impl.color_tx == nullptr) {
      STRNCPY(impl.status, "DLSS SR: color texture allocation failed");
      return false;
    }
  }
  if (impl.depth_tx == nullptr || impl.input_extent != input_extent) {
    if (impl.depth_tx != nullptr) {
      GPU_texture_free(impl.depth_tx);
    }
    impl.depth_tx = GPU_texture_create_2d("DLSS_SR.Depth",
                                          input_extent.x,
                                          input_extent.y,
                                          1,
                                          gpu::TextureFormat::SFLOAT_32,
                                          GPU_TEXTURE_USAGE_SHADER_READ |
                                              GPU_TEXTURE_USAGE_ATTACHMENT,
                                          nullptr);
    if (impl.depth_tx == nullptr) {
      STRNCPY(impl.status, "DLSS SR: depth texture allocation failed");
      return false;
    }
  }
  if (impl.velocity_tx == nullptr || impl.input_extent != input_extent) {
    if (impl.velocity_tx != nullptr) {
      GPU_texture_free(impl.velocity_tx);
    }
    impl.velocity_tx = GPU_texture_create_2d("DLSS_SR.Velocity",
                                             input_extent.x,
                                             input_extent.y,
                                             1,
                                             gpu::TextureFormat::SFLOAT_16_16,
                                             GPU_TEXTURE_USAGE_SHADER_READ |
                                                 GPU_TEXTURE_USAGE_ATTACHMENT,
                                             nullptr);
    if (impl.velocity_tx == nullptr) {
      STRNCPY(impl.status, "DLSS SR: velocity texture allocation failed");
      return false;
    }
  }
  if (impl.output_tx == nullptr || impl.output_extent != output_extent) {
    if (impl.output_tx != nullptr) {
      GPU_texture_free(impl.output_tx);
    }
    impl.output_tx = GPU_texture_create_2d("DLSS_SR.Output",
                                           output_extent.x,
                                           output_extent.y,
                                           1,
                                           gpu::TextureFormat::SFLOAT_16_16_16_16,
                                           GPU_TEXTURE_USAGE_SHADER_READ |
                                               GPU_TEXTURE_USAGE_SHADER_WRITE,
                                           nullptr);
    if (impl.output_tx == nullptr) {
      STRNCPY(impl.status, "DLSS SR: output texture allocation failed");
      return false;
    }
  }

  /* Build the create-feature parameter set. */
  NVSDK_NGX_Parameter *p = impl.parameters;
  runtime->set_ui(p, "Width", unsigned(input_extent.x));
  runtime->set_ui(p, "Height", unsigned(input_extent.y));
  runtime->set_ui(p, "OutWidth", unsigned(output_extent.x));
  runtime->set_ui(p, "OutHeight", unsigned(output_extent.y));
  runtime->set_ui(p, "InWidth", unsigned(input_extent.x));
  runtime->set_ui(p, "InHeight", unsigned(input_extent.y));
  runtime->set_i(p, "PerfQualityValue", quality_preset);
  /* Depth is EEVEE's reverse-Z [0..1]; NGX expects reverse-Z with DLSS SR. */
  runtime->set_i(p, "DepthInverted", depth_reverse_z ? 1 : 0);
  runtime->set_i(p, "JitterOffsetInPixels", 1);
  runtime->set_i(p, "MotionVectorsInPixels", 1);
  runtime->set_ui(p, "CreationNodeMask", 1u);
  runtime->set_ui(p, "VisibilityNodeMask", 1u);
  runtime->set_ull(p, "FeatureCreateFlags", 0ull);

  /* Command pool + buffer + fence for the NGX submission. */
  if (impl.command_pool == VK_NULL_HANDLE) {
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = vk_device.queue_family_get();
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vk_device.functions.vkCreateCommandPool(vk_handle, &pool_info, nullptr, &impl.command_pool) != VK_SUCCESS) {
      STRNCPY(impl.status, "DLSS SR: vkCreateCommandPool failed");
      return false;
    }
  }
  if (impl.command_buffer == VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo cb_info = {};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_info.commandPool = impl.command_pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;
    if (vk_device.functions.vkAllocateCommandBuffers(vk_handle, &cb_info, &impl.command_buffer) != VK_SUCCESS) {
      STRNCPY(impl.status, "DLSS SR: vkAllocateCommandBuffers failed");
      return false;
    }
  }
  if (impl.fence == VK_NULL_HANDLE) {
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vk_device.functions.vkCreateFence(vk_handle, &fence_info, nullptr, &impl.fence) != VK_SUCCESS) {
      STRNCPY(impl.status, "DLSS SR: vkCreateFence failed");
      return false;
    }
  }

  /* Create the SR feature. NGX wants a VkCommandBuffer for this call. */
  {
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vk_device.functions.vkBeginCommandBuffer(impl.command_buffer, &begin_info);

    const int result = runtime->create_feature1(vk_handle,
                                                impl.command_buffer,
                                                ngx::FEATURE_SUPER_SAMPLING,
                                                p,
                                                &impl.feature);

    VkResult end_result = vk_device.functions.vkEndCommandBuffer(impl.command_buffer);
    if (result != ngx::RESULT_SUCCESS || impl.feature == nullptr) {
      char msg[160];
      SNPRINTF(msg, "DLSS SR: CreateFeature failed (0x%08x)", result);
      STRNCPY(impl.status, msg);
      impl.feature = nullptr;
      return false;
    }
    if (end_result != VK_SUCCESS) {
      STRNCPY(impl.status, "DLSS SR: command buffer end failed after CreateFeature");
      runtime->release_feature(impl.feature);
      impl.feature = nullptr;
      return false;
    }

    /* Submit the empty command buffer as required by NGX. */
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl.command_buffer;
    vk_device.functions.vkResetFences(vk_handle, 1, &impl.fence);
    VkQueue queue = vk_device.queue_handle_get();
    vk_device.functions.vkQueueSubmit(queue, 1, &submit, impl.fence);
    vk_device.functions.vkWaitForFences(vk_handle, 1, &impl.fence, VK_TRUE, UINT64_MAX);
    vk_device.functions.vkResetCommandBuffer(impl.command_buffer, 0);
  }

  impl.input_extent = input_extent;
  impl.output_extent = output_extent;
  impl.quality_preset = quality_preset;
  impl.depth_reverse_z = depth_reverse_z;
  SNPRINTF(impl.status,
           "DLSS SR active: %dx%d -> %dx%d quality=%d",
           input_extent.x,
           input_extent.y,
           output_extent.x,
           output_extent.y,
           quality_preset);
  return true;
#endif
}

void DlssSRVulkanSession::retry_initialization()
{
  Impl &impl = *impl_;
  if (impl.feature != nullptr && impl.runtime && impl.runtime->release_feature) {
    impl.runtime->release_feature(impl.feature);
    impl.feature = nullptr;
  }
  impl.retry_count++;
  STRNCPY(impl.status, "DLSS SR: retrying initialization");
}

gpu::Texture *DlssSRVulkanSession::color_texture()
{
  return impl_->color_tx;
}

gpu::Texture *DlssSRVulkanSession::depth_texture()
{
  return impl_->depth_tx;
}

gpu::Texture *DlssSRVulkanSession::velocity_texture()
{
  return impl_->velocity_tx;
}

gpu::Texture *DlssSRVulkanSession::output_texture()
{
  return impl_->output_tx;
}

bool DlssSRVulkanSession::copy_inputs_and_evaluate(const DlssSRFrameInputs &frame,
                                                   const bool /*color_is_scene_linear*/)
{
  Impl &impl = *impl_;
  ngx::Runtime *runtime = impl.runtime;
  if (impl.feature == nullptr || runtime == nullptr || !runtime->loaded) {
    return false;
  }

#if !defined(WITH_VULKAN_BACKEND) || !defined(WITH_DLSS)
  return false;
#else
  gpu::VKBackend &vk_backend = gpu::VKBackend::get();
  gpu::VKDevice &vk_device = vk_backend.device;
  const VkDevice vk_handle = vk_device.vk_handle();

  /* Bind GPU textures by Vulkan image views. Reuse the images created in
   * ensure_resources; NGX reads them through the parameters below. */
  gpu::VKTexture *vk_color = gpu::unwrap(impl.color_tx);
  gpu::VKTexture *vk_depth = gpu::unwrap(impl.depth_tx);
  gpu::VKTexture *vk_velocity = gpu::unwrap(impl.velocity_tx);
  gpu::VKTexture *vk_output = gpu::unwrap(impl.output_tx);
  if (!vk_color || !vk_depth || !vk_velocity || !vk_output) {
    STRNCPY(impl.status, "DLSS SR: GPU textures not Vulkan-backed");
    return false;
  }

  const VkImageView color_view = vk_color->image_view_get(gpu::VKImageViewArrayed::NOT_ARRAYED,
                                                          gpu::VKImageViewFlags::DEFAULT)
                                     .vk_handle();
  const VkImageView depth_view = vk_depth->image_view_get(gpu::VKImageViewArrayed::NOT_ARRAYED,
                                                          gpu::VKImageViewFlags::DEFAULT)
                                     .vk_handle();
  const VkImageView vel_view = vk_velocity->image_view_get(gpu::VKImageViewArrayed::NOT_ARRAYED,
                                                           gpu::VKImageViewFlags::DEFAULT)
                                   .vk_handle();
  const VkImageView out_view = vk_output->image_view_get(gpu::VKImageViewArrayed::NOT_ARRAYED,
                                                         gpu::VKImageViewFlags::DEFAULT)
                                   .vk_handle();

  NVSDK_NGX_Parameter *p = impl.parameters;
  runtime->set_ptr(p, "Color", reinterpret_cast<void *>(color_view));
  runtime->set_ptr(p, "Depth", reinterpret_cast<void *>(depth_view));
  runtime->set_ptr(p, "MotionVectors", reinterpret_cast<void *>(vel_view));
  runtime->set_ptr(p, "Output", reinterpret_cast<void *>(out_view));

  runtime->set_f(p, "Sharpness", frame.sharpness);
  runtime->set_f(p, "MV.Scale.X", frame.mvec_scale);
  runtime->set_f(p, "MV.Scale.Y", frame.mvec_scale);
  runtime->set_f(p, "JitterOffset.X", frame.jitter.x);
  runtime->set_f(p, "JitterOffset.Y", frame.jitter.y);
  runtime->set_i(p, "Reset", frame.reset_history ? 1 : 0);

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk_device.functions.vkBeginCommandBuffer(impl.command_buffer, &begin_info);

  const int result = runtime->evaluate_feature(impl.command_buffer, impl.feature, p, nullptr);

  vk_device.functions.vkEndCommandBuffer(impl.command_buffer);
  if (result != ngx::RESULT_SUCCESS) {
    char msg[160];
    SNPRINTF(msg, "DLSS SR: EvaluateFeature failed (0x%08x)", result);
    STRNCPY(impl.status, msg);
    vk_device.functions.vkResetCommandBuffer(impl.command_buffer, 0);
    return false;
  }

  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &impl.command_buffer;
  vk_device.functions.vkResetFences(vk_handle, 1, &impl.fence);
  vk_device.functions.vkQueueSubmit(vk_device.queue_handle_get(), 1, &submit, impl.fence);
  return true;
#endif
}

bool DlssSRVulkanSession::wait_for_output()
{
  Impl &impl = *impl_;
  if (impl.feature == nullptr || impl.fence == VK_NULL_HANDLE) {
    return false;
  }
#if !defined(WITH_VULKAN_BACKEND) || !defined(WITH_DLSS)
  return false;
#else
  gpu::VKBackend &vk_backend = gpu::VKBackend::get();
  gpu::VKDevice &vk_device = vk_backend.device;
  const VkResult wait_result = vk_device.functions.vkWaitForFences(
      vk_device.vk_handle(), 1, &impl.fence, VK_TRUE, UINT64_MAX);
  vk_device.functions.vkResetCommandBuffer(impl.command_buffer, 0);
  if (wait_result != VK_SUCCESS) {
    STRNCPY(impl.status, "DLSS SR: fence wait failed");
    return false;
  }
  return true;
#endif
}

void DlssSRVulkanSession::warmup()
{
  /* Warmup is a no-op: NGX handles its own lazy initialization. */
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
