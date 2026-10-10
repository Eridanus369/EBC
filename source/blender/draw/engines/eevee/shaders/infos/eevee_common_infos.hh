/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_view_infos.hh"
#endif

#ifdef GLSL_CPP_STUBS
#  define MAT_TRANSPARENT
#  define MAT_RAYCAST
#endif


#ifdef GPU_SHADER
#  include "eevee_render_texture_shared.hh"
#endif

#include "eevee_defines.hh"

#include "gpu_shader_create_info.hh"

/* -------------------------------------------------------------------- */
/** \name Common
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_render_texture_data)
TYPEDEF_SOURCE("eevee_render_texture_shared.hh")
STORAGE_BUF(RENDER_TEXTURE_BUF_SLOT, read, RenderTextureData, render_texture_buf[])
SAMPLER(RENDER_TEXTURE_COLOR_TX_SLOT_0, sampler2D, render_texture_color_tx_0)
SAMPLER(RENDER_TEXTURE_COLOR_TX_SLOT_1, sampler2D, render_texture_color_tx_1)
SAMPLER(RENDER_TEXTURE_COLOR_TX_SLOT_2, sampler2D, render_texture_color_tx_2)
SAMPLER(RENDER_TEXTURE_COLOR_TX_SLOT_3, sampler2D, render_texture_color_tx_3)
SAMPLER(RENDER_TEXTURE_HISTORY_TX_SLOT_0, sampler2D, render_texture_color_history_tx_0)
SAMPLER(RENDER_TEXTURE_HISTORY_TX_SLOT_1, sampler2D, render_texture_color_history_tx_1)
SAMPLER(RENDER_TEXTURE_HISTORY_TX_SLOT_2, sampler2D, render_texture_color_history_tx_2)
SAMPLER(RENDER_TEXTURE_HISTORY_TX_SLOT_3, sampler2D, render_texture_color_history_tx_3)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_raycast)
DEFINE("MAT_RAYCAST")
SAMPLER(RAYCAST_DEPTH_TEX_SLOT, sampler2D, raycast_depth_tx)
SAMPLER(OBJECT_ID_TEX_SLOT, usampler2D, object_id_tx)
SAMPLER(PREPASS_NORMAL_TEX_SLOT, sampler2D, prepass_normal_tx)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surface Velocity
 *
 * Combined with the depth pre-pass shader.
 * Outputs the view motion vectors for animated objects.
 * \{ */

/* Pass world space deltas to the fragment shader.
 * This is to make sure that the resulting motion vectors are valid even with displacement.
 * WARNING: The next value is invalid when rendering the viewport. */
GPU_SHADER_NAMED_INTERFACE_INFO(eevee_velocity_surface_iface, motion)
SMOOTH(float3, prev)
SMOOTH(float3, next)
GPU_SHADER_NAMED_INTERFACE_END(motion)

/* WORKAROUND: Until we get condition support for interfaces. */
GPU_SHADER_CREATE_INFO(eevee_velocity_iface_info)
VERTEX_OUT(eevee_velocity_surface_iface)
GPU_SHADER_CREATE_END()

/** \} */
