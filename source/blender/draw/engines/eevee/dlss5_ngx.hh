/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Minimal runtime loader for the NVIDIA NGX SDK (Vulkan).
 *
 * NGX is only available at runtime: the driver ships `libnvidia-ngx.so.1`, and the
 * DLSS Super Resolution feature lives in `libnvidia-ngx-dlss.so.1`. We dlopen both
 * and resolve the small set of entry points needed by the EEVEE SR executor so the
 * build never needs to link against a proprietary static library.
 *
 * `NVSDK_NGX_Parameter` is a C++ abstract class: the parameter setters are virtual
 * methods dispatched through its vtable, so we use the real SDK header for those.
 */

#pragma once

#ifdef WITH_DLSS

#  include <cstdint>

struct VkDevice_T;
struct VkInstance_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;
struct VkCommandBuffer_T;
struct VkImage_T;
struct VkImageView_T;
struct VkSemaphore_T;
struct VkFence_T;

using VkDevice = VkDevice_T *;
using VkInstance = VkInstance_T *;
using VkPhysicalDevice = VkPhysicalDevice_T *;
using VkQueue = VkQueue_T *;
using VkCommandBuffer = VkCommandBuffer_T *;

/* Keep the SDK structure opaque; the actual vtable-bearing type comes from the real header
 * included by the .cc translation unit that talks to NGX. */
struct NVSDK_NGX_Parameter;

namespace blender::eevee::ngx {

constexpr int FEATURE_SUPER_SAMPLING = 1;
constexpr int RESULT_SUCCESS = 0x1;

using PFN_Init = int (*)(unsigned long long app_id,
                         const wchar_t *app_path,
                         VkInstance vk_instance,
                         VkPhysicalDevice vk_phys_device,
                         VkDevice vk_device,
                         void *gipa,
                         void *gdpa,
                         const void *feature_info,
                         int sdk_version);
using AppLogCallback = void (*)(const char *message, int level, int source);
struct FeatureCommonInfo;

using PFN_Shutdown1 = int (*)(VkDevice vk_device);
using PFN_GetParameters = int (*)(NVSDK_NGX_Parameter **out);
using PFN_AllocateParameters = int (*)(NVSDK_NGX_Parameter **out);
using PFN_DestroyParameters = int (*)(NVSDK_NGX_Parameter *params);
using PFN_CreateFeature1 = int (*)(VkDevice vk_device,
                                   VkCommandBuffer vk_command_buffer,
                                   int feature_id,
                                   NVSDK_NGX_Parameter *params,
                                   void **out_handle);
using PFN_ReleaseFeature = int (*)(void *handle);
using PFN_EvaluateFeature = int (*)(VkCommandBuffer vk_command_buffer,
                                    const void *handle,
                                    NVSDK_NGX_Parameter *params,
                                    void *callback);

struct Runtime {
  void *core_lib = nullptr;
  void *feature_lib = nullptr;

  PFN_Init init = nullptr;
  PFN_Shutdown1 shutdown = nullptr;
  PFN_GetParameters get_parameters = nullptr;
  PFN_AllocateParameters allocate_parameters = nullptr;
  PFN_DestroyParameters destroy_parameters = nullptr;
  PFN_CreateFeature1 create_feature1 = nullptr;
  PFN_ReleaseFeature release_feature = nullptr;
  PFN_EvaluateFeature evaluate_feature = nullptr;

  bool loaded = false;
  bool initialized = false;
  char status[192] = "NGX not initialized";
};

Runtime *runtime_get();

bool runtime_init(Runtime &rt,
                  VkInstance vk_instance,
                  VkPhysicalDevice vk_phys_device,
                  VkDevice vk_device,
                  void *gipa,
                  void *gdpa);

}  // namespace blender::eevee::ngx

#endif /* WITH_DLSS */
