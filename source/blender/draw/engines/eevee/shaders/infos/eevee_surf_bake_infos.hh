/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "eevee_defines.hh"
#  include "eevee_geom_infos.hh"
#endif

#define EEVEE_BAKE_PRIMITIVE_TEX_SLOT FILTER_AOV_VALUE_TEX_SLOT

#include "eevee_defines.hh"
#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(eevee_bake_light_shader_surface)
FRAGMENT_OUT(0, float4, out_position)
FRAGMENT_OUT(1, float4, out_normal)
SAMPLER(EEVEE_BAKE_PRIMITIVE_TEX_SLOT, isampler2D, bake_primitive_tx)
FRAGMENT_SOURCE("eevee_bake_light_shader_surface_frag.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_bake_light_shader_surface_mesh)
ADDITIONAL_INFO(eevee_geom_bake_mesh_basic_infos_)
ADDITIONAL_INFO(eevee_bake_light_shader_surface)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
