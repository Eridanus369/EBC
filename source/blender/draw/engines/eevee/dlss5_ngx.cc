/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Runtime loader for the NVIDIA NGX SDK (Vulkan). See dlss5_ngx.hh for the design note.
 */

#include "dlss5_ngx.hh"

#ifdef WITH_DLSS

#  include <cstring>
#  include <dlfcn.h>

#  include "BLI_string.hh"

#  include "CLG_log.h"

namespace blender::eevee::ngx {

namespace {

void *load_symbol(void *handle, const char *name)
{
  if (handle == nullptr) {
    return nullptr;
  }
  return dlsym(handle, name);
}

void set_status(Runtime &rt, const char *message)
{
  STRNCPY(rt.status, message);
  rt.status[sizeof(rt.status) - 1] = '\0';
}

/* Candidate locations for the NGX feature library. The SDK ships it under
 * `<root>/lib/Linux_x86_64/rel/`, but the driver may also expose it in the default
 * loader search path once NGX core initialized the feature path. We try both. */
const char *const feature_library_candidates[] = {
    "/home/eridanusepsilon/dlss-sdk-latest/DLSS-310.2.1/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.310.2.1",
    "/home/eridanusepsilon/dlss-sdk-latest/DLSS-310.2.1/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.310.2.1",
    "libnvidia-ngx-dlss.so.1",
};

}  // namespace

Runtime *runtime_get()
{
  static Runtime rt;
  if (rt.loaded) {
    return &rt;
  }

  rt.core_lib = dlopen("libnvidia-ngx.so.1", RTLD_NOW | RTLD_LOCAL);
  if (rt.core_lib == nullptr) {
    set_status(rt, "NGX core library (libnvidia-ngx.so.1) not found");
    return &rt;
  }

  rt.feature_lib = nullptr;
  for (const char *candidate : feature_library_candidates) {
    rt.feature_lib = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
    if (rt.feature_lib != nullptr) {
      break;
    }
  }
  if (rt.feature_lib == nullptr) {
    set_status(rt, "NGX DLSS feature library (libnvidia-ngx-dlss.so.1) not found");
    dlclose(rt.core_lib);
    rt.core_lib = nullptr;
    return &rt;
  }

  rt.init = reinterpret_cast<PFN_Init>(load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_Init"));
  rt.shutdown = reinterpret_cast<PFN_Shutdown1>(load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_Shutdown1"));
  rt.get_parameters = reinterpret_cast<PFN_GetParameters>(
      load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_GetParameters"));
  rt.create_feature1 = reinterpret_cast<PFN_CreateFeature1>(
      load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_CreateFeature1"));
  rt.release_feature = reinterpret_cast<PFN_ReleaseFeature>(
      load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_ReleaseFeature"));
  rt.evaluate_feature = reinterpret_cast<PFN_EvaluateFeature>(
      load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_EvaluateFeature"));
  rt.allocate_parameters = reinterpret_cast<PFN_AllocateParameters>(
      load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_AllocateParameters"));
  rt.destroy_parameters = reinterpret_cast<PFN_DestroyParameters>(
      load_symbol(rt.core_lib, "NVSDK_NGX_VULKAN_DestroyParameters"));

  if (rt.init == nullptr || rt.get_parameters == nullptr || rt.create_feature1 == nullptr ||
      rt.evaluate_feature == nullptr || rt.release_feature == nullptr ||
      rt.allocate_parameters == nullptr || rt.destroy_parameters == nullptr)
  {
    set_status(rt, "NGX library loaded but required symbols are missing");
    dlclose(rt.feature_lib);
    dlclose(rt.core_lib);
    rt.feature_lib = nullptr;
    rt.core_lib = nullptr;
    return &rt;
  }

  rt.loaded = true;
  set_status(rt, "NGX runtime loaded (not initialized)");
  return &rt;
}

bool runtime_init(Runtime &rt,
                  VkInstance vk_instance,
                  VkPhysicalDevice vk_phys_device,
                  VkDevice vk_device,
                  void *gipa,
                  void *gdpa)
{
  if (!rt.loaded) {
    set_status(rt, "NGX runtime not loaded");
    return false;
  }
  if (rt.initialized) {
    return true;
  }
  if (rt.init == nullptr) {
    set_status(rt, "NGX init entry point missing");
    return false;
  }

  fprintf(stderr, "[NGX-DBG] gipa=%p gdpa=%p vk_inst=%p vk_pdev=%p vk_dev=%p\n",
          gipa, gdpa, (void*)vk_instance, (void*)vk_phys_device, (void*)vk_device);

  /* Blender's NGX application id; any non-zero value is accepted by the loader. */
  constexpr unsigned long long app_id = 0xEBC00001ull;
  const int result = rt.init(app_id,
                             L"/tmp",
                             vk_instance,
                             vk_phys_device,
                             vk_device,
                             gipa,
                             gdpa,
                             nullptr,
                             /*NVSDK_NGX_Version_API*/ 0x0000000F);
  fprintf(stderr, "[NGX-DBG] Init returned 0x%08x (success=0x1)\n", result);
  if (result != RESULT_SUCCESS) {
    char message[160];
    SNPRINTF(message, "NGX Vulkan init failed (0x%08x)", result);
    set_status(rt, message);
    return false;
  }

  rt.initialized = true;
  set_status(rt, "NGX initialized");
  return true;
}

}  // namespace blender::eevee::ngx

#endif /* WITH_DLSS */
