/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#pragma once

#include "gpu_shader_create_info.hh"

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"
#  include "eevee_photon_shared.hh"
#endif

GPU_SHADER_CREATE_INFO(eevee_photon_emit)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(256, 1, 1)
COMPUTE_SOURCE("eevee_photon_emit_comp.glsl")
TYPEDEF_SOURCE("eevee_photon_shared.hh")
STORAGE_BUF(0, write, Photon, photons[])
PUSH_CONSTANT(int, photon_count)
PUSH_CONSTANT(float3, light_position)
PUSH_CONSTANT(float3, light_direction)
PUSH_CONSTANT(float, light_radius)
PUSH_CONSTANT(float, light_power)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_photon_trace)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(256, 1, 1)
COMPUTE_SOURCE("eevee_photon_trace_comp.glsl")
TYPEDEF_SOURCE("eevee_photon_shared.hh")
STORAGE_BUF(0, read_write, Photon, photons[])
PUSH_CONSTANT(int, photon_count)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_photon_splat)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(256, 1, 1)
COMPUTE_SOURCE("eevee_photon_splat_comp.glsl")
TYPEDEF_SOURCE("eevee_photon_shared.hh")
STORAGE_BUF(0, read, Photon, photons[])
IMAGE(1, SFLOAT_16_16_16_16, write, image2D, caustic_color_img)
IMAGE(2, SFLOAT_16, write, image2D, caustic_count_img)
STORAGE_BUF(3, read, CausticLevelData, level_data[])
PUSH_CONSTANT(int, level_idx)
PUSH_CONSTANT(int, photon_count)
GPU_SHADER_CREATE_END()
