# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: BSD-3-Clause

# Find the DLSS SDK. This module defines:
#  DLSS_INCLUDE_DIR       - NGX headers (nvsdk_ngx.h)
#  DLSS_VULKAN_LIB        - Linux libnvidia-ngx-dlss.so (runtime, dlopen'd)
#  DLSS_FOUND             - if the headers are found

find_path(DLSS_INCLUDE_DIR
  NAMES "nvsdk_ngx.h"
  PATHS
    "${DLSS_SDK_ROOT}/include"
    "$ENV{DLSS_SDK_ROOT}/include"
    "$ENV{HOME}/dlss-sdk/include"
)

find_library(DLSS_VULKAN_LIB
  NAMES nvidia-ngx-dlss libnvidia-ngx-dlss
  PATHS
    "${DLSS_SDK_ROOT}/lib/Linux_x86_64/rel"
    "$ENV{DLSS_SDK_ROOT}/lib/Linux_x86_64/rel"
    "$ENV{HOME}/dlss-sdk/lib/Linux_x86_64/rel"
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(DLSS REQUIRED_VARS DLSS_INCLUDE_DIR)
