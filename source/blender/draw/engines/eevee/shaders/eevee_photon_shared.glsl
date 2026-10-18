/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Photon data structures shared between host and device.
 */

#pragma once

#include "GPU_shader_shared_utils.hh"

#ifndef GPU_SHADER
namespace blender::eevee {
#endif

struct [[host_shared]] Photon {
  /* World-space position of the hit. */
  packed_float3 position;
  /* Wavelength in nanometers. */
  float wavelength;
  /* Incident direction (normalized). */
  packed_float3 direction;
  /* Flag: 0 = surface, 1 = volume. */
  int flags;
  /* Spectral power (RGB-encoded in equal-energy upsampling). */
  packed_float3 power;
  float pad;
};

/* Caustic map level data layout. */
struct [[host_shared]] CausticLevelData {
  /* World-space bounds of this cascade. */
  packed_float3 min_bound;
  float pad0;
  packed_float3 max_bound;
  float pad1;
  /* Texture resolution. */
  int resolution;
  int pad2;
  int pad3;
  int pad4;
};

#ifndef GPU_SHADER
}  // namespace blender::eevee
#endif
