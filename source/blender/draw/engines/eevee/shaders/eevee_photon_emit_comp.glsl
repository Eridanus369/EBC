/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

float hash(uint x) {
  x = (x ^ 61u) ^ (x >> 16u);
  x *= 9u;
  x = x ^ (x >> 4u);
  x *= 0x27d4eb2du;
  x = x ^ (x >> 15u);
  return float(x) / 4294967296.0;
}

void main() {
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  float lambda = SPECTRAL_LAMBDA_MIN + (SPECTRAL_LAMBDA_MAX - SPECTRAL_LAMBDA_MIN) * hash(idx * 13u);
  float u = hash(idx * 7u + 1u);
  float v = hash(idx * 11u + 3u);
  float r = sqrt(u);
  float theta = 2.0 * 3.14159265 * v;
  float3 offset = float3(r * cos(theta), r * sin(theta), 0.0) * light_radius;

  Photon p;
  p.position = light_position + offset;
  p.direction = light_direction;
  p.wavelength = lambda;
  p.flags = 0;
  p.power = float3(light_power / float(photon_count));
  p.pad = 0.0;

  photons[idx] = p;
}
