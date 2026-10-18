/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#pragma once

#include "gpu_shader_create_info.hh"

/* ---- Photon Emit ---- */
GPU_SHADER_CREATE_INFO(eevee_photon_emit)
DO_STATIC_COMPILATION()
COMPUTE_SOURCE("eevee_photon_emit_comp.glsl")
TYPEDEF_SOURCE("eevee_photon_shared.hh")
STORAGE_BUFFER(0, Type::VEC4, "photons") /* Photon is 3x vec4 aligned */
PUSH_CONSTANT(INT, "photon_count")
PUSH_CONSTANT(VEC3, "light_position")
PUSH_CONSTANT(VEC3, "light_direction")
PUSH_CONSTANT(FLOAT, "light_radius")
PUSH_CONSTANT(FLOAT, "light_power")
GPU_SHADER_CREATE_INFO(eevee_photon_emit);

/* ---- Photon Trace ---- */
GPU_SHADER_CREATE_INFO(eevee_photon_trace)
DO_STATIC_COMPILATION()
COMPUTE_SOURCE("eevee_photon_trace_comp.glsl")
TYPEDEF_SOURCE("eevee_photon_shared.hh")
STORAGE_BUFFER(0, Type::VEC4, "photons")
PUSH_CONSTANT(INT, "photon_count")
GPU_SHADER_CREATE_INFO(eevee_photon_trace);

/* ---- Photon Splat ---- */
GPU_SHADER_CREATE_INFO(eevee_photon_splat)
DO_STATIC_COMPILATION()
COMPUTE_SOURCE("eevee_photon_splat_comp.glsl")
TYPEDEF_SOURCE("eevee_photon_shared.hh")
STORAGE_BUFFER(0, Type::VEC4, "photons")
IMAGE(1, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "caustic_color_img")
IMAGE(2, GPU_R16F, Qualifier::WRITE, ImageType::FLOAT_2D, "caustic_count_img")
STORAGE_BUFFER(3, Type::VEC4, "level_data")
PUSH_CONSTANT(INT, "level_idx")
PUSH_CONSTANT(INT, "photon_count")
GPU_SHADER_CREATE_INFO(eevee_photon_splat);
