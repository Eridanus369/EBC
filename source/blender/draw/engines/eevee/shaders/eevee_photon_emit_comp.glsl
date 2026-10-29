/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/eevee_photon_infos.hh"

COMPUTE_SHADER_CREATE_INFO(eevee_photon_emit)

#include "gpu_shader_compat.hh"
#include "eevee_spectral_lib.glsl"

#define CAUSTIC_2PI 6.283185307179586

float hash(uint x)
{
  x = (x ^ 61u) ^ (x >> 16u);
  x *= 9u;
  x = x ^ (x >> 4u);
  x *= 0x27d4eb2du;
  x = x ^ (x >> 15u);
  return float(x) / 4294967296.0;
}

float3 sample_sphere(float u, float v)
{
  float z = 1.0 - 2.0 * u;
  float radius = sqrt(max(0.0, 1.0 - z * z));
  float phi = CAUSTIC_2PI * v;
  return float3(radius * cos(phi), radius * sin(phi), z);
}

/* Uniform sampling inside a cone. `cos_half` is the cosine of the half angle. The resulting solid
 * angle density is `1 / (2 * PI * (1 - cos_half))` which is exactly what the emit power
 * normalization assumes. */
float3 sample_cone(float3 axis, float cos_half, float u, float v)
{
  float cos_theta = 1.0 - u * (1.0 - cos_half);
  float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
  float phi = CAUSTIC_2PI * v;

  float3 helper = abs(axis.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
  float3 tangent = normalize(cross(axis, helper));
  float3 bitangent = cross(axis, tangent);
  return normalize(axis * cos_theta + (tangent * cos(phi) + bitangent * sin(phi)) * sin_theta);
}

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  if (idx >= uint(photon_count)) { return; }

  /* Decorrelate the photon set between TAA samples (`0x9E3779B9` is the golden ratio increment). */
  uint seed = uint(photon_seed) * 0x9E3779B9u;

  Photon p;
  p.wavelength = SPECTRAL_LAMBDA_MIN +
                 (SPECTRAL_LAMBDA_MAX - SPECTRAL_LAMBDA_MIN) * hash(idx * 13u + seed);
  p.flags = CAUSTIC_PHOTON_IN_FLIGHT;
  p.power = float3(0.0);
  p.pad = 0.0;

  int li = 0;
  int ti = -1;
  float select = hash(idx * 5u + 11u + seed);

  if (target_count > 0) {
    /* Pick a (light, target) pair. Pairs are weighted by `light power * solid angle`, which makes
     * the per photon power independent of the pair (see below). */
    ti = target_count - 1;
    for (int i = 0; i < target_count; i++) {
      if (select <= targets[i].cdf) {
        ti = i;
        break;
      }
    }
    li = targets[ti].light_index;
  }
  else if (light_count > 0) {
    li = light_count - 1;
    for (int i = 0; i < light_count; i++) {
      if (select <= lights[i].select_cdf) {
        li = i;
        break;
      }
    }
  }
  else {
    p.position = float3(0.0);
    p.direction = float3(0.0);
    p.flags = CAUSTIC_PHOTON_LOST;
    photons[idx] = p;
    return;
  }

  CausticLightData light = lights[li];

  /* Sample a point on the light emitter. `size_x` / `size_y` are half extents (rectangles) or
   * radii (disks / ellipses). */
  float3 src = light.position;
  if (light.type == 3) {
    if (light.area_shape == 1) {
      float r = sqrt(hash(idx * 23u + 3u + seed));
      float phi = CAUSTIC_2PI * hash(idx * 29u + 7u + seed);
      src += light.axis_x * (r * cos(phi) * light.size_x);
      src += light.axis_y * (r * sin(phi) * light.size_y);
    }
    else {
      src += light.axis_x * (hash(idx * 23u + 3u + seed) - 0.5) * 2.0 * light.size_x;
      src += light.axis_y * (hash(idx * 29u + 7u + seed) - 0.5) * 2.0 * light.size_y;
    }
  }
  else if (light.radius > 0.0) {
    /* Sphere shaped emitter (point / spot soft shadows). */
    float3 offset = sample_sphere(hash(idx * 37u + 5u + seed), hash(idx * 41u + 9u + seed));
    offset *= light.radius * pow(hash(idx * 43u + 13u + seed), 1.0 / 3.0);
    src += offset;
  }

  float u = hash(idx * 7u + 1u + seed);
  float v = hash(idx * 11u + 3u + seed);
  bool valid = true;
  float3 dir;

  if (ti >= 0) {
    /* Aim at the bounding sphere of the transmissive object. */
    float3 to_target = targets[ti].center - src;
    float dist = length(to_target);
    float3 axis = to_target / max(dist, 1.0e-4);
    float sin_half = clamp(targets[ti].radius / max(dist, 1.0e-4), 0.0, 1.0);
    float cos_half = sqrt(max(0.0, 1.0 - sin_half * sin_half));
    if (dist <= targets[ti].radius) {
      /* The emitter is inside the bounding sphere: sample the whole sphere. */
      cos_half = -1.0;
    }
    dir = sample_cone(axis, cos_half, u, v);
  }
  else if (light.type == 1) {
    float3 axis = normalize(light.direction);
    float sin_half = clamp(sin(max(light.sun_angle, 1.0e-4)), 0.0, 1.0);
    float cos_half = sqrt(max(0.0, 1.0 - sin_half * sin_half));
    dir = sample_cone(axis, cos_half, u, v);
  }
  else if (light.type == 2) {
    dir = sample_cone(normalize(light.direction), light.cos_angle, u, v);
  }
  else {
    dir = sample_sphere(u, v);
  }

  /* Spot cones must be clipped against the real cone, not only the target direction. */
  if (light.type == 2 && dot(dir, light.direction) < light.cos_angle) {
    valid = false;
  }

  p.position = src;
  p.direction = valid ? dir : float3(0.0);
  p.flags = valid ? CAUSTIC_PHOTON_IN_FLIGHT : CAUSTIC_PHOTON_LOST;
  p.power = light.color * photon_power;
  photons[idx] = p;
}
