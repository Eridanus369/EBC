/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.hh"
#include "eevee_spectral_lib.glsl"

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 0) buffer PhotonBuffer {
  Photon photons[];
};

/* Simple sphere tracer for testing. In production, use scene BVH. */
bool trace_scene(float3 ro, float3 rd, out float3 hit_pos, out float3 hit_nor) {
  /* Hardcoded glass sphere at origin, radius 1.0 */
  float a = dot(rd, rd);
  float b = 2.0 * dot(ro, rd);
  float c = dot(ro, ro) - 1.0;
  float delta = b * b - 4.0 * a * c;
  
  if (delta > 0.0) {
    float t = (-b - sqrt(delta)) / (2.0 * a);
    if (t > 0.0) {
      hit_pos = ro + rd * t;
      hit_nor = normalize(hit_pos);
      return true;
    }
  }
  return false;
}

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) {
    return;
  }

  Photon p = photons[idx];
  float3 hit_pos;
  float3 hit_nor;

  if (trace_scene(p.position, p.direction, hit_pos, hit_nor)) {
    /* Dispersion: compute IOR based on wavelength. */
    float3 cauchy = float3(1.50, 0.00420, 0.0); /* BK7 glass approx */
    float ior = spectral_cauchy_ior(p.wavelength, cauchy);
    
    /* Snell's law refraction. */
    float3 refr_dir = refract(p.direction, hit_nor, 1.0 / ior);
    
    if (dot(refr_dir, refr_dir) < 1e-6) {
      /* Total internal reflection. */
      refr_dir = reflect(p.direction, hit_nor);
    } else {
      /* Fresnel effect (simplified). */
      float cos_i = abs(dot(p.direction, hit_nor));
      float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
      float fresnel = f0 + (1.0 - f0) * pow(1.0 - cos_i, 5.0);
      p.power *= (1.0 - fresnel);
    }

    p.position = hit_pos + refr_dir * 0.001; /* Offset to avoid self-intersection */
    p.direction = refr_dir;
  } else {
    /* Photon escaped; kill it. */
    p.power = float3(0.0);
  }

  photons[idx] = p;
}
