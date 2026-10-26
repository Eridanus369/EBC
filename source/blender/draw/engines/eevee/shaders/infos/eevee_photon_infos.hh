/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

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
STORAGE_BUF(0, write, Photon, photons[])
PUSH_CONSTANT(int, photon_count)
PUSH_CONSTANT(float3, light_position)
PUSH_CONSTANT(float3, light_direction)
PUSH_CONSTANT(float, light_radius)
PUSH_CONSTANT(float, light_power)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_photon_splat)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(256, 1, 1)
COMPUTE_SOURCE("eevee_photon_splat_comp.glsl")
STORAGE_BUF(0, read, Photon, photons[])
/* Integer fixed-point accumulation. Image atomics only exist for single channel `r32ui` images, so
 * each color channel gets its own texture. */
IMAGE(1, UINT_32, read_write, uimage2D, caustic_accum_r_img)
IMAGE(2, UINT_32, read_write, uimage2D, caustic_accum_g_img)
IMAGE(3, UINT_32, read_write, uimage2D, caustic_accum_b_img)
PUSH_CONSTANT(int, photon_count)
PUSH_CONSTANT(float4x4, viewmat)
PUSH_CONSTANT(float4x4, winmat)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_photon_resolve)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(8, 8, 1)
COMPUTE_SOURCE("eevee_photon_resolve_comp.glsl")
IMAGE(1, UINT_32, read, uimage2D, caustic_accum_r_img)
IMAGE(2, UINT_32, read, uimage2D, caustic_accum_g_img)
IMAGE(3, UINT_32, read, uimage2D, caustic_accum_b_img)
IMAGE(4, SFLOAT_16_16_16_16, write, image2D, caustic_color_img)
GPU_SHADER_CREATE_END()
