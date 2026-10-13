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
 */

#pragma once

#ifdef WITH_DLSS

struct NVSDK_NGX_Parameter;

namespace blender::eevee::ngx {

/* Feature identifiers we care about (mirror of NVSDK_NGX_Feature_SuperSampling). */
constexpr int FEATURE_SUPER_SAMPLING = 1;

/* Result code returned on success by NVSDK_NGX_Result. */
constexpr int RESULT_SUCCESS = 0x1;

using PFN_Init = int (*)(unsigned long long app_id,
                         const wchar_t *app_path,
                         void *vk_instance,
                         void *vk_phys_device,
                         void *vk_device,
                         void *gipa,
                         void *gdpa,
                         const void *feature_info,
                         int sdk_version);
using PFN_Shutdown = int (*)(void *vk_device);
using PFN_GetParameters = int (*)(NVSDK_NGX_Parameter **out);
using PFN_CreateFeature1 = int (*)(void *vk_device,
                                   void *vk_command_buffer,
                                   int feature_id,
                                   const NVSDK_NGX_Parameter *params,
                                   void **out_handle);
using PFN_ReleaseFeature = int (*)(void *handle);
using PFN_EvaluateFeature = int (*)(void *vk_command_buffer,
                                    const void *handle,
                                    const NVSDK_NGX_Parameter *params,
                                    void *callback);
using PFN_AllocateParameters = int (*)(NVSDK_NGX_Parameter **out);
using PFN_DestroyParameters = int (*)(NVSDK_NGX_Parameter *params);

/* Parameter setters are exported as plain C functions returning void. */
using PFN_Parameter_SetI = void (*)(NVSDK_NGX_Parameter *, const char *, int);
using PFN_Parameter_SetUI = void (*)(NVSDK_NGX_Parameter *, const char *, unsigned);
using PFN_Parameter_SetF = void (*)(NVSDK_NGX_Parameter *, const char *, float);
using PFN_Parameter_SetULL = void (*)(NVSDK_NGX_Parameter *, const char *, unsigned long long);

/** Loaded NGX runtime. Thread-affine, single instance per process. */
struct Runtime {
  void *core_lib = nullptr;  /* libnvidia-ngx.so.1 */
  void *feature_lib = nullptr; /* libnvidia-ngx-dlss.so.1 */

  PFN_Init init = nullptr;
  PFN_Shutdown shutdown = nullptr;
  PFN_GetParameters get_parameters = nullptr;
  PFN_CreateFeature1 create_feature1 = nullptr;
  PFN_ReleaseFeature release_feature = nullptr;
  PFN_EvaluateFeature evaluate_feature = nullptr;
  PFN_AllocateParameters allocate_parameters = nullptr;
  PFN_DestroyParameters destroy_parameters = nullptr;

  PFN_Parameter_SetI set_i = nullptr;
  PFN_Parameter_SetUI set_ui = nullptr;
  PFN_Parameter_SetF set_f = nullptr;
  PFN_Parameter_SetULL set_ull = nullptr;

  bool loaded = false;
  bool initialized = false;
  char status[192] = "NGX not initialized";
};

/** Load the NGX runtime shared libraries (idempotent). */
Runtime *runtime_get();

/** Ensure the process-wide NGX runtime is initialized for the given Vulkan device. */
bool runtime_init(Runtime &rt,
                  void *vk_instance,
                  void *vk_phys_device,
                  void *vk_device,
                  void *gipa,
                  void *gdpa);

}  // namespace blender::eevee::ngx

#endif /* WITH_DLSS */
