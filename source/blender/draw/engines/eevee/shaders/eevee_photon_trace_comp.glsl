/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_trace)

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

void main() {
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  Photon p = photons[idx];
  
  /* Fake scene: a glass sphere at origin with radius 1.0 */
  float3 sphere_center = float3(0.0f, 0.0f, 0.0f);
  float sphere_radius = 1.0f;
  
  /* Ray-sphere intersection */
  float3 oc = p.position - sphere_center;
  float b = dot(oc, p.direction);
  float c = dot(oc, oc) - sphere_radius * sphere_radius;
  float h = b * b - c;
  
  if (h > 0.0f) {
    float t = -b - sqrt(h);
    if (t > 0.0f) {
      /* Hit the sphere */
      p.position = p.position + p.direction * t;
      float3 normal = normalize(p.position - sphere_center);
      
      /* Cauchy dispersion: IOR depends on wavelength */
      float3 cauchy = float3(1.5f, 0.05f, 0.0f); /* 增大 B 系数让彩虹散开 */
      float eta = spectral_cauchy_ior(p.wavelength, cauchy);
      
      /* Refract (Snell's law) */
      float3 refracted_dir = refract(p.direction, normal, 1.0f / eta);
      if (dot(refracted_dir, refracted_dir) > 0.0f) {
        p.direction = refracted_dir;
      } else {
        /* Total internal reflection */
        p.direction = reflect(p.direction, normal);
      }
      
      /* Push the photon along the new direction to hit the back of the sphere */
      p.position = p.position + p.direction * (2.0f * sphere_radius);
    }
  }
  
  photons[idx] = p;
}
