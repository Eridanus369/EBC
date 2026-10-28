/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "GPU_shader_shared.hh"

/* Photon life cycle, stored in `Photon::flags`. Photons start in flight, are either deposited on an
 * opaque surface (and become splattable) or lost (escaped the scene / total internal reflection
 * loop). */
#define CAUSTIC_PHOTON_IN_FLIGHT 0
#define CAUSTIC_PHOTON_DEPOSITED 1
#define CAUSTIC_PHOTON_LOST 2

/* Resolution of the world space volumetric caustic grid (one texel is one froxel of the volume
 * in-scattering lookup). */
#define CAUSTIC_VOLUME_RES 64
/* Fixed point scale used by the volumetric caustic accumulation to be able to use image atomics.
 * It must be large enough that a single photon contribution does not truncate to zero when stored
 * into the `r32ui` grid (a photon carries a radiometric power of ~1e-3). */
#define CAUSTIC_VOLUME_FIXED_SCALE 1048576.0f
/* Number of energy deposition samples along one refracted photon path segment. */
#define CAUSTIC_VOLUME_SAMPLES 8

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

/* One entry per mesh object taking part in the photon simulation. Transmissive objects are stored
 * first so the trace shader can limit its medium lookup to `[0, transmissive_count)`. */
struct [[host_shared]] CausticObjectData {
  /* World space axis aligned bounding box. Used to resolve nested media. */
  packed_float3 min_bound;
  /* 1 if the object refracts (glass, water, ...). */
  int transmissive;
  packed_float3 max_bound;
  /* Range of the object material entries inside the global material table. */
  int material_offset;
  /* Base index of refraction and Cauchy dispersion coefficients of the object default material. */
  float ior;
  float cauchy_b;
  float cauchy_c;
  /* Number of material slots of the object. */
  int material_count;
};

/* One entry per material slot of every mesh taking part in the simulation. Referenced by
 * `CausticTriangleData::material_index` so that every triangle uses the material actually assigned
 * to its face in Blender. */
struct [[host_shared]] CausticMaterialData {
  float ior;
  float cauchy_b;
  float cauchy_c;
  /* 1 if the material refracts (glass, water, ...). */
  int transmissive;
};

/* One entry per triangle. Normals are pre-transformed into world space and are the interpolated
 * per corner normals (respecting sharp edges / custom normals). */
struct [[host_shared]] CausticTriangleData {
  packed_float3 n0;
  int object_index;
  packed_float3 n1;
  /* Index of the triangle material inside the global material table. */
  int material_index;
  packed_float3 n2;
  float pad0;
};

struct [[host_shared]] CausticLightData {
  /* Emission origin. For sun lights this is a virtual point far away along the light direction. */
  packed_float3 position;
  /* 0: point, 1: sun, 2: spot, 3: area. */
  int type;
  /* Forward axis (sun / spot / area). */
  packed_float3 direction;
  /* Soft source radius for point and spot lights. */
  float radius;
  packed_float3 axis_x;
  float size_x;
  packed_float3 axis_y;
  float size_y;
  /* Light color normalized to unit luminance. Multiply by the per photon power. */
  packed_float3 color;
  /* Cosine of the spot cone half angle. */
  float cos_angle;
  float spot_blend;
  float sun_angle;
  /* Normalized cumulative probability used to select this light when no target is available. */
  float select_cdf;
  /* Area light shape (0: square/rectangle, 1: disk/ellipse). Unused for other light types. */
  int area_shape;
};

/* One entry per (light, transmissive object) pair. Directional importance sampling aims every
 * photon at one of those bounding spheres, which keeps the trace efficient for small glass objects
 * without introducing any bias (see the emit shader for the weight derivation). */
struct [[host_shared]] CausticTargetData {
  /* World space bounding sphere. */
  packed_float3 center;
  float radius;
  int light_index;
  /* Solid angle subtended by the bounding sphere. */
  float omega;
  /* Normalized cumulative probability. */
  float cdf;
  float pad0;
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
