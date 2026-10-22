/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "GPU_shader_shared.hh"

#ifndef GPU_SHADER
namespace blender::eevee {
#endif

struct [[host_shared]] Photon {
  float3 position;
  float wavelength;
  float3 direction;
  int flags;
  float3 power;
  float pad;
};

struct [[host_shared]] CausticLevelData {
  float3 min_bound;
  float pad0;
  float3 max_bound;
  float pad1;
  int resolution;
  int pad2;
  int pad3;
  int pad4;
};

#ifndef GPU_SHADER
}  // namespace blender::eevee
#endif
