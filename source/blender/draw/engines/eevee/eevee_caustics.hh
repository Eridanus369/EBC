/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Spectral caustics module.
 *
 * Photon-based caustics with spectral dispersion. Photons are emitted from the real scene lights,
 * traced through the scene using hardware ray queries (VK_KHR_ray_query) against a bottom level
 * acceleration structure built from all visible meshes, refracted at every transmissive interface
 * using Cauchy's equation for their own wavelength, and splatted into the caustic map which is then
 * queried by the deferred combine pass.
 */

#pragma once

#include "GPU_index_buffer.hh"
#include "GPU_ray_tracing.hh"
#include "GPU_vertex_buffer.hh"

#include "draw_pass.hh"
#include "eevee_caustic_map.hh"

namespace blender::eevee {

using namespace draw;

class Instance;

/* -------------------------------------------------------------------- */
/** \name CausticModule
 * \{ */

class CausticModule {
 private:
  Instance &inst_;

  bool enabled_ = false;
  /* Set every frame by sync() as a validity guard for render() (the caustic buffers are only valid
   * after a sync). It is not consumed: the passes are dispatched on every TAA sample so that the
   * per sample photon seed and the progressive accumulation of the caustic map actually work. */
  bool render_pending_ = false;
  /* Zero based index of the current sample inside the frame. Reset by sync() and incremented by
   * render() on every dispatch. Used as photon seed, progressive average weight and map reset
   * trigger. `Sampling::sample_index()` cannot be used: sync() runs once per frame, not per
   * sample, so it always reports the first sample index. */
  int caustic_sequence_sample_ = 0;
  /* View matrix of the last sync. The caustic map is a screen space buffer: as soon as the view
   * changes, everything accumulated in it belongs to the previous framing and would be composited
   * over the new one (the light trail seen while navigating). Used to restart the accumulation. */
  float4x4 last_viewmat_ = float4x4::identity();
  bool has_last_viewmat_ = false;
  int photon_count_ = 0;
  int spectral_samples_ = 0;
  int max_bounces_ = 4;
  float epsilon_ = 1.0e-5f;

  /* Splat kernel size in texels and its brightness multiplier. */
  float splat_gain_ = 0.1f;
  float splat_radius_ = 2.0f;

  /* Radiance carried by a single photon. Derived from the scene lights and the importance
   * sampling distribution, so that the accumulated caustic map is energy normalized. */
  float photon_power_ = 0.0f;

  CausticMap caustic_map_;

  /* Persistent GPU buffers to avoid CPU round-trips. */
  StorageArrayBuffer<Photon, 4096> photon_pool_ = {"photon_pool"};
  StorageArrayBuffer<CausticObjectData, 32> object_buf_ = {"caustic_objects"};
  StorageArrayBuffer<CausticTriangleData, 1024> triangle_buf_ = {"caustic_triangles"};
  StorageArrayBuffer<CausticMaterialData, 512> material_buf_ = {"caustic_materials"};
  StorageArrayBuffer<CausticLightData, 16> light_buf_ = {"caustic_lights"};
  StorageArrayBuffer<CausticTargetData, 32> target_buf_ = {"caustic_targets"};
  /* Tight world space bounding sphere per object (xyz center, w radius), indexed like
   * `object_buf_`. Photons are aimed at it instead of at the axis aligned bounding box, whose
   * diagonal overestimates the radius of compact lenses and wastes most of the photons. */
  Vector<float4> target_spheres_;
  /* Diagnostic counters written by the trace pass (index meaning is documented in the shader).
   * Read back on the next frame, never inside the draw. */
  StorageArrayBuffer<uint, 8> stats_buf_ = {"caustic_stats"};
  bool stats_pending_ = false;
  int stats_frame_counter_ = 0;

  int object_count_ = 0;
  /* Transmissive objects are stored first in `object_buf_`. */
  int transmissive_count_ = 0;
  int triangle_count_ = 0;
  int material_count_ = 0;
  int light_count_ = 0;
  int target_count_ = 0;

  /* World space bounds of the volumetric caustic accumulation grid. */
  float3 volume_min_ = float3(0.0f);
  float3 volume_max_ = float3(1.0f);
  float3 volume_size_inv_ = float3(1.0f);
  /* User controlled brightness multiplier of the volumetric caustics. Zero disables the feature. */
  float volume_gain_ = 0.0f;

  /* Hash of the geometry state. The acceleration structure is only rebuilt when it changes. */
  uint64_t geometry_hash_ = 0;
  float3 scene_center_ = float3(0.0f);
  float scene_radius_ = 1.0f;

  gpu::TopLevelASPtr tlas_;
  gpu::BottomLevelASPtr blas_;
  gpu::VertBufPtr vertex_buf_;
  gpu::IndexBufPtr index_buf_;

  PassSimple photon_emit_ps_ = {"PhotonEmit"};
  PassSimple photon_trace_ps_ = {"PhotonTrace"};
  PassSimple photon_splat_ps_ = {"PhotonSplat"};
  PassSimple photon_resolve_ps_ = {"PhotonResolve"};

  void sync_geometry();
  void sync_lights();

 public:
  CausticModule(Instance &inst);
  ~CausticModule() {};

  void init();
  void sync();
  void render(View &view);
  Texture &color_texture();

  /* Volumetric caustic grid, consumed by the volume scatter pass. */
  Texture &volume_accum_r_texture();
  Texture &volume_accum_g_texture();
  Texture &volume_accum_b_texture();
  int volume_resolution() const;
  const float3 &volume_min() const
  {
    return volume_min_;
  }
  const float3 &volume_size_inv() const
  {
    return volume_size_inv_;
  }
  float volume_gain() const
  {
    return volume_gain_;
  }

  bool enabled() const
  {
    return enabled_;
  }
};

/** \} */

}  // namespace blender::eevee
