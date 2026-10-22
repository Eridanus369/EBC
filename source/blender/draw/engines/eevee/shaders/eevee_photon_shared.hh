/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "GPU_shader_shared_utils.hh"

#ifndef GPU_SHADER
namespace blender::eevee {
#endif

struct [[host_shared]] Photon {
  packed_float3 position;
  float wavelength;
  packed_float3 direction;
  int flags;
  packed_float3 power;
  float pad;
};

struct [[host_shared]] CausticLevelData {
  packed_float3 min_bound;
  float pad0;
  packed_float3 max_bound;
  float pad1;
  int resolution;
  int pad2;
  int pad3;
  int pad4;
};

#ifndef GPU_SHADER
}  // namespace blender::eevee
#endif
