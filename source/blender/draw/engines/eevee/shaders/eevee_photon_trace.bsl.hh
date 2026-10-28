/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_compat.hh"
#include "eevee_photon_shared.hh"
#include "eevee_spectral_lib.glsl"

namespace eevee::photon {

struct Resources {
  [[storage(0, read_write)]] Photon (&photons)[];
  [[storage(1, read)]] const CausticTriangleData (&triangles)[];
  /* Per material slot table referenced by `CausticTriangleData::material_index`. */
  [[storage(2, read)]] const CausticMaterialData (&materials)[];
  [[acceleration_structure(0)]] const accelerationStructureEXT scene_as;
  /* World space volumetric caustic accumulation, one single channel image per color channel since
   * image atomics only exist for `r32ui`. */
  [[image(0, read_write, UINT_32)]] uimage3DAtomic caustic_volume_r_img;
  [[image(1, read_write, UINT_32)]] uimage3DAtomic caustic_volume_g_img;
  [[image(2, read_write, UINT_32)]] uimage3DAtomic caustic_volume_b_img;
  /* Throughput probe, read back once per frame for diagnostics. Index meaning:
   * [0] deposited photons, [1] lost photons, [2] bounces, [3] refractions, [4] total internal
   * reflections, [5] suspected self intersections (hit closer than the origin offset). */
  [[storage(3, read_write)]] uint (&stats)[];
};

struct PushConstants {
  [[push_constant]] int photon_count;
  [[push_constant]] int max_bounces;
  /* World space offset applied after each interface to avoid self intersection. */
  [[push_constant]] float epsilon;
  /* xyz: world space minimum bound of the volumetric grid. w: user gain (0 disables it). */
  [[push_constant]] float4 volume_min_gain;
  /* xyz: inverse size of the volumetric grid. w: maximum distance used by the parity traversal. */
  [[push_constant]] float4 volume_scale;
  [[push_constant]] int3 volume_res;
  /* Camera position, used by the Henyey-Greenstein phase function of the volume deposition. */
  [[push_constant]] float4 camera_position;
};

/* Unpolarized Fresnel reflectance, Schlick approximation. `eta` is the ratio of the incident over
 * the transmitted index of refraction. The refracted photon only keeps `1 - F` of its energy, which
 * is what gives real glass its finite caustic brightness (grazing interfaces lose most energy). */
float caustic_fresnel(float cos_theta, float eta)
{
  float r0 = (eta - 1.0f) / (eta + 1.0f);
  r0 *= r0;
  float c = 1.0f - clamp(cos_theta, 0.0f, 1.0f);
  float c2 = c * c;
  return clamp(r0 + (1.0f - r0) * c2 * c2 * c, 0.0f, 1.0f);
}

/* Henyey-Greenstein phase function. */
float caustic_phase_hg(float cos_theta)
{
  const float g = 0.3f;
  float denom = 1.0f + g * g - 2.0f * g * cos_theta;
  return (1.0f - g * g) / (4.0f * 3.14159265358979323846f * pow(max(denom, 1e-6f), 1.5f));
}

/* Deposit the energy carried by the photon along one refracted path segment into the world space
 * volumetric caustic grid, so that participating media (water, smoke) also receive caustics. */
void caustic_deposit_volume([[resource_table]] Resources &srt,
                            [[resource_table]] const PushConstants &pc,
                            const Photon p,
                            float3 from,
                            float3 to,
                            float3 dir,
                            uint idx)
{
  float gain = pc.volume_min_gain.w;
  if (gain <= 0.0f) { return; }

  float3 seg = to - from;
  if (dot(seg, seg) <= 1e-16f) { return; }

  float3 min_bound = pc.volume_min_gain.xyz;
  float3 inv_size = pc.volume_scale.xyz;
  float3 weights = xyz_to_rgb(cie_1931_cmf(p.wavelength)) * p.power * gain;
  const float inv_samples = 1.0f / float(CAUSTIC_VOLUME_SAMPLES);
  /* Golden ratio phase jitter per photon to hide the regular sampling pattern. */
  float jitter = fract(float(idx) * 0.618033988749895f);

  for (int i = 0; i < CAUSTIC_VOLUME_SAMPLES; i++) {
    float t = (float(i) + jitter) * inv_samples;
    float3 p_voxel = from + seg * t;

    float3 uvw = (p_voxel - min_bound) * inv_size;
    if (uvw.x < 0.0f || uvw.y < 0.0f || uvw.z < 0.0f) { continue; }
    if (uvw.x > 1.0f || uvw.y > 1.0f || uvw.z > 1.0f) { continue; }

    float3 to_cam = pc.camera_position.xyz - p_voxel;
    float dist_cam = length(to_cam);
    float cos_theta = (dist_cam > 1e-6f) ? dot(dir, to_cam / dist_cam) : 0.0f;
    float hg = caustic_phase_hg(cos_theta);

    int3 cell = int3(int(uvw.x * float(pc.volume_res.x)),
                     int(uvw.y * float(pc.volume_res.y)),
                     int(uvw.z * float(pc.volume_res.z)));
    cell = clamp(cell, int3(0, 0, 0),
                 int3(pc.volume_res.x - 1, pc.volume_res.y - 1, pc.volume_res.z - 1));

    float3 contrib = max(weights * (hg * inv_samples * CAUSTIC_VOLUME_FIXED_SCALE), float3(0.0f));
    imageAtomicAdd(srt.caustic_volume_r_img, cell, uint(contrib.x));
    imageAtomicAdd(srt.caustic_volume_g_img, cell, uint(contrib.y));
    imageAtomicAdd(srt.caustic_volume_b_img, cell, uint(contrib.z));
  }
}

/* Even-odd (ray parity) medium lookup. Casts a bounded ray from the surface exit point and counts
 * how many times it crosses the first object it hits. An odd count means the probe point lies
 * inside that object, an even count means it is outside (air). This replaces the previous smallest
 * bounding box heuristic which misclassified cavities (water surface inside a glass). */
int caustic_parity_count([[resource_table]] Resources &srt,
                         float3 origin,
                         float3 dir,
                         float epsilon,
                         float max_dist,
                         int &r_first_object,
                         int &r_first_material)
{
  r_first_object = -1;
  r_first_material = 0;

  const int max_steps = 16;
  int crossings = 0;
  float t_offset = 0.0f;

  for (int i = 0; i < max_steps; i++) {
    float remaining = max_dist - t_offset;
    if (remaining <= epsilon) { break; }

    float3 start = origin + dir * t_offset;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, srt.scene_as, 0u, 0xFFu, start, 0.0f, dir, remaining);
    for (int step = 0; step < 8; step++) {
      if (!rayQueryProceedEXT(query)) { break; }
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
      break;
    }

    float hit_t = rayQueryGetIntersectionTEXT(query, true);
    if (hit_t <= 0.0f) { break; }

    uint prim_index = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
    CausticTriangleData tri = srt.triangles[prim_index];

    if (r_first_object < 0) {
      r_first_object = tri.object_index;
      r_first_material = tri.material_index;
    }
    else if (tri.object_index != r_first_object) {
      /* Left the first object volume, no need to count any further. */
      break;
    }

    crossings += 1;
    t_offset += hit_t + epsilon;
  }

  return crossings;
}

/* Trace a single photon through the scene using hardware ray queries (VK_KHR_ray_query).
 *
 * Every interface hit compares the current medium IOR with the IOR of the material actually assigned
 * to the hit triangle, and refracts the photon using Cauchy's equation for its own wavelength.
 * Opaque hits deposit the photon which is then splatted into the caustic map. Refracted path
 * segments also deposit energy into the volumetric caustic grid. */
[[compute, local_size(256, 1, 1)]]
void trace([[resource_table]] Resources &srt,
           [[resource_table]] const PushConstants &pc,
           [[work_group_id]] const uint3 group_id,
           [[local_invocation_id]] const uint3 local_id)
{
  uint idx = (group_id.x * 256) + local_id.x;
  if (idx >= uint(pc.photon_count)) { return; }

  Photon p = srt.photons[idx];
  if (p.flags != CAUSTIC_PHOTON_IN_FLIGHT) { return; }

  float3 ray_pos = p.position;
  float3 ray_dir = p.direction;
  if (dot(ray_dir, ray_dir) < 1e-12f) {
    p.flags = CAUSTIC_PHOTON_LOST;
    srt.photons[idx] = p;
    return;
  }
  ray_dir = normalize(ray_dir);

  /* Medium the photon is currently travelling in, and a small stack of the enclosing media so that
   * nested dielectrics (water inside a glass) exit back into the correct medium instead of air. */
  float current_ior = 1.0f;
  float4 medium_ior = float4(1.0f);
  int medium_depth = 0;
  bool deposited = false;
  int bounces = 0;
  int refractions = 0;
  int tir_count = 0;
  int self_hits = 0;
  /* Set as soon as the photon changes medium. Photons that only bounce off surfaces (or are
   * reflected by total internal reflection) carry no caustic information: their contribution is the
   * regular direct lighting already handled by the EEVEE deferred pass. Splatting them would double
   * count the scene lighting, so they are discarded. */
  bool transmitted = false;

  for (int bounce = 0; bounce < pc.max_bounces; bounce++) {
    bounces = bounce + 1;
    rayQueryEXT query;
    /* `tmin` skips the surface the ray was offset from, which prevents a photon from hitting the
     * same triangle again and burning its whole bounce budget on a single interface. */
    rayQueryInitializeEXT(query, srt.scene_as, 0u, 0xFFu, ray_pos, pc.epsilon, ray_dir, 1.0e30f);
    /* A single `Proceed` resolves all triangle candidates, but stay safe with a bounded loop. */
    for (int step = 0; step < 8; step++) {
      if (!rayQueryProceedEXT(query)) { break; }
    }

    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
      break;
    }

    float hit_t = rayQueryGetIntersectionTEXT(query, true);
    if (hit_t <= 0.0f) { break; }
    if (hit_t <= pc.epsilon * 4.0f) { self_hits += 1; }

    float3 hit_pos = ray_pos + ray_dir * hit_t;
    uint prim_index = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
    CausticTriangleData tri = srt.triangles[prim_index];
    CausticMaterialData mat = srt.materials[tri.material_index];

    /* Deposit the energy of the already refracted part of the path into the volume grid. */
    if (transmitted) {
      caustic_deposit_volume(srt, pc, p, ray_pos, hit_pos, ray_dir, idx);
    }

    /* Barycentrics are weighted toward the second and third triangle vertices. */
    float2 bary = rayQueryGetIntersectionBarycentricsEXT(query, true);
    float3 normal = tri.n0 * (1.0f - bary.x - bary.y) + tri.n1 * bary.x + tri.n2 * bary.y;
    if (dot(normal, normal) < 1e-12f) { break; }
    normal = normalize(normal);

    if (mat.transmissive == 0) {
      /* Opaque surface: the photon ends its path here. Only photons that went through at least one
       * refractive interface contribute to the caustic map. */
      p.position = hit_pos;
      deposited = transmitted;
      break;
    }

    bool entering = dot(ray_dir, normal) < 0.0f;
    float3 facing = entering ? normal : -normal;

    /* Refractive index of the medium the photon is about to enter. Entering pushes the hit material
     * onto the medium stack, exiting pops back to the enclosing medium. */
    float target_ior;
    if (entering) {
      target_ior = mat.ior;
      if (mat.cauchy_b != 0.0f || mat.cauchy_c != 0.0f) {
        target_ior = spectral_cauchy_ior(
            p.wavelength, float3(mat.ior, mat.cauchy_b, mat.cauchy_c));
      }
      medium_depth = min(medium_depth + 1, 3);
      medium_ior[medium_depth] = target_ior;
    }
    else {
      medium_depth = max(medium_depth - 1, 0);
      target_ior = medium_ior[medium_depth];
    }

    float eta = current_ior / max(target_ior, 1.0e-3f);
    float3 refracted = refract(ray_dir, facing, eta);

    if (dot(refracted, refracted) < 1.0e-8f) {
      /* Total internal reflection keeps the photon inside its current medium. */
      ray_dir = reflect(ray_dir, facing);
      tir_count += 1;
    }
    else {
      /* Energy split at the interface: only the refracted part keeps travelling. */
      float cos_theta = abs(dot(ray_dir, facing));
      p.power = p.power * (1.0f - caustic_fresnel(cos_theta, eta));
      ray_dir = refracted;
      current_ior = target_ior;
      transmitted = true;
      refractions += 1;
    }

    /* Offset in the direction of travel, which is `-facing` since `facing` always points back toward
     * the incoming ray. Offsetting along `facing` would push the photon back into the medium it came
     * from, making it re-hit the same triangle and burn its whole bounce budget there. */
    ray_pos = hit_pos - facing * pc.epsilon;
  }

  /* A deposited photon already stores its impact point. Only update the position of photons that
   * ran out of bounces, otherwise the last interface offset would overwrite the deposit point and
   * the splat would land on the lens instead of the receiver surface. */
  if (!deposited) {
    p.position = ray_pos;
  }
  p.direction = ray_dir;
  p.flags = deposited ? CAUSTIC_PHOTON_DEPOSITED : CAUSTIC_PHOTON_LOST;
  srt.photons[idx] = p;

  if (deposited) {
    atomicAdd(srt.stats[0], 1u);
  }
  else {
    atomicAdd(srt.stats[1], 1u);
  }
  atomicAdd(srt.stats[2], uint(bounces));
  atomicAdd(srt.stats[3], uint(refractions));
  atomicAdd(srt.stats[4], uint(tir_count));
  atomicAdd(srt.stats[5], uint(self_hits));
}

}  // namespace eevee::photon

PipelineCompute eevee_photon_trace(eevee::photon::trace);
