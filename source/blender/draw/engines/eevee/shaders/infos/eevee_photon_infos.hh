/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
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
STORAGE_BUF(1, read, CausticLightData, lights[])
STORAGE_BUF(2, read, CausticTargetData, targets[])
PUSH_CONSTANT(int, photon_count)
PUSH_CONSTANT(int, light_count)
PUSH_CONSTANT(int, target_count)
/* Power carried by a single photon. Already includes the importance sampling compensation. */
PUSH_CONSTANT(float, photon_power)
/* Per TAA sample offset mixed into the photon hash. Without it every sample traces the exact same
 * photon set, the caustic noise never converges and the progressive averaging has no effect. */
PUSH_CONSTANT(int, photon_seed)
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
/* Minimum camera distance of the photons accumulated per texel, used by the occlusion test. */
IMAGE(4, UINT_32, read_write, uimage2D, caustic_accum_depth_img)
PUSH_CONSTANT(int, photon_count)
PUSH_CONSTANT(float4x4, viewmat)
PUSH_CONSTANT(float4x4, winmat)
PUSH_CONSTANT(float, splat_gain)
PUSH_CONSTANT(float, splat_radius)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_photon_resolve)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(8, 8, 1)
COMPUTE_SOURCE("eevee_photon_resolve_comp.glsl")
IMAGE(1, UINT_32, read, uimage2D, caustic_accum_r_img)
IMAGE(2, UINT_32, read, uimage2D, caustic_accum_g_img)
IMAGE(3, UINT_32, read, uimage2D, caustic_accum_b_img)
IMAGE(4, UINT_32, read, uimage2D, caustic_accum_depth_img)
IMAGE(5, SFLOAT_16_16_16_16, read_write, image2D, caustic_color_img)
/* 0 based index of the current TAA sample. The resolve pass progressively averages the color map
 * over the sequence, otherwise the sparse photon deposits read as colored speckle. */
PUSH_CONSTANT(int, caustic_sample_index)
GPU_SHADER_CREATE_END()
