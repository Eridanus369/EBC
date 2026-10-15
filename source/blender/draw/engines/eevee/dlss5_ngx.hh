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

/* `NVSDK_NGX_VULKAN_Init_Ext2`, matching its declaration in `nvsdk_ngx_vk.h`. Do *not* call
 * the plain `NVSDK_NGX_VULKAN_Init` export: on driver 595.99.02 that symbol is compiled with
 * the legacy six-argument `(appId, appPath, instance, physDevice, device, InSDKVersion)`
 * signature, so it takes the API version from the sixth argument register and leaves the
 * GIPA/GDPA/FeatureInfo arguments unset. See the note in dlss5_ngx.cc. */
using PFN_Init = int (*)(unsigned long long app_id,
                         const wchar_t *app_path,
                         VkInstance vk_instance,
                         VkPhysicalDevice vk_phys_device,
                         VkDevice vk_device,
                         void *gipa,
                         void *gdpa,
                         int sdk_version,
                         const NVSDK_NGX_Parameter *in_parameters);

/* The public SDK header declares `NVSDK_NGX_VULKAN_Shutdown1(VkDevice)`, but the driver
 * implementation forwards its second argument register to an internal out-parameter and
 * dereferences it unconditionally (verified against libnvidia-ngx.so.595.99.02: the wrapper
 * moves `%rsi` into `%rcx`, the internal shutdown stores that at `0x28(%rsp)` and then writes
 * the remaining instance count through it). A one-argument call therefore passes an
 * indeterminate pointer and crashes inside the driver. Pass a real `unsigned int` to match
 * what the implementation actually reads. */
using PFN_Shutdown1 = int (*)(VkDevice vk_device, unsigned int *out_devices_left);
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
