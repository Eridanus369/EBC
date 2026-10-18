/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.glsl"

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) writeonly buffer PhotonBuffer {
  Photon photons[];
};

/* Uniforms provided by the host. */
[[push_constant]] int photon_count;
[[push_constant]] float3 light_position;
[[push_constant]] float3 light_direction;
[[push_constant]] float light_radius;
[[push_constant]] float light_power;

/* Pseudo-random hash. */
float hash(uint x)
{
  x = (x ^ 61u) ^ (x >> 16u);
  x *= 9u;
  x = x ^ (x >> 4u);
  x *= 0x27d4eb2du;
  x = x ^ (x >> 15u);
  return float(x) / 4294967296.0;
}

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) {
    return;
  }

  /* Sample wavelength uniformly across visible range. */
  float lambda = SPECTRAL_LAMBDA_MIN + (SPECTRAL_LAMBDA_MAX - SPECTRAL_LAMBDA_MIN) * hash(idx * 13u);

  /* Sample position on light disk. */
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
