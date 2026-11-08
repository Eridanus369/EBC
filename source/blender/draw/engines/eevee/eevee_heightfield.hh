/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Heightfield displacement module.
 *
 * Rebuilds the deferred G-Buffer of heightfield carrier objects by ray marching the heightmap
 * against the camera ray, using a maximum mipmap pyramid for hierarchical stepping. The resulting
 * hit depth is written back to the depth buffer, the analytic normal is sampled by the deferred
 * lighting pass, and ray marched shadow / AO factors modulate the final radiance.
 */

#pragma once

#include "BLI_map.hh"
#include "BLI_vector.hh"

#include "GPU_texture.hh"

#include "draw_pass.hh"
#include "draw_resource.hh"

#include "DNA_image_types.h"

#include "eevee_heightfield_shared.hh"

namespace blender::eevee {

using namespace draw;

class Instance;

struct HeightfieldPyramidCache {
  blender::Image *image = nullptr;
  int2 size = int2(0);
  int mip_count = 0;
  uint64_t stamp = 0;
  int rebuild_key = 0;
  /* Border taper baked into the height field before the pyramids are built. Part of the cache
   * identity: two objects sharing an image but asking for a different taper need different
   * pyramids. 0 disables the taper, which is byte identical to the historic field. */
  float taper = 0.0f;
  int taper_mode = 0;
  int base_offset = 0;
  bool valid = false;
  /* Size of the maximum pyramid block; the minimum pyramid starts right after it. */
  int max_size = 0;
  int4 mip_offsets[HF_MAX_MIPS / 4] = {};
  /* Offsets relative to the pyramid block start (level 0 aliases the maximum pyramid). */
  int4 mip_offsets_min[HF_MAX_MIPS / 4] = {};
  /* Largest per texel height gradient of level 0 (height units per texel, normalised height). Used
   * to derive a conservative Lipschitz bound for the sphere traced shadow. */
  float lip_texel = 0.0f;
  Vector<float> data;
};

class HeightfieldModule {
 private:
  Instance &inst_;

  bool enabled_ = false;
  /* The trace is only run once per TAA sample: the refraction layer render would otherwise trace an
   * already displaced depth buffer. Reset by sync() and whenever the sample index changes (sync()
   * may only run once for several samples). */
  bool rendered_ = false;
  uint64_t traced_sample_ = uint64_t(-1);
  int instance_count_ = 0;
  /* xyz: direction towards the sun, w: 1 if a sun light exists in the scene. */
  float4 sun_ = float4(0.0f, 0.0f, 1.0f, 0.0f);

  StorageArrayBuffer<HeightfieldData, HF_MAX_INSTANCES> instance_buf_ = {"HeightfieldInstances"};
  StorageArrayBuffer<float, 4> pyramid_buf_ = {"HeightfieldPyramids"};
  Vector<float> pyramid_data_;
  bool pyramid_dirty_ = false;

  Vector<HeightfieldPyramidCache> cache_;
  int cache_rebuild_counter_ = 0;

  PassSimple trace_ps_ = {"Heightfield.Trace"};
  Framebuffer trace_fb_ = {"Heightfield.Trace"};
  /* Auxiliary screen space outputs. Persistent textures so that the deferred lighting and combine
   * passes can bind them even when the feature is disabled. */
  Texture geom_tx_ = {"Heightfield.Geom"};
  Texture shade_tx_ = {"Heightfield.Shade"};

  void sync_instances();
  void sync_pyramids();
  HeightfieldPyramidCache *cache_get(blender::Image *image,
                                     int rebuild_key,
                                     float taper,
                                     int taper_mode,
                                     bool &rebuilt);

 public:
  HeightfieldModule(Instance &inst) : inst_(inst) {}

  void init();
  void sync();
  /** Returns true if the depth buffer was modified this frame (the HiZ must then be rebuilt). */
  bool render(View &view);

  bool enabled() const
  {
    return enabled_;
  }

  template<typename PassType> void bind_resources(PassType &pass) const
  {
    /* Bind by resource name: the BSL tool renumbers sampler slots per entry point, so the
     * declared slot numbers are not usable as GPU binding indices here. */
    pass.bind_texture("geom_tx", &const_cast<Texture &>(geom_tx_));
    pass.bind_texture("shade_tx", &const_cast<Texture &>(shade_tx_));
  }
};

}  // namespace blender::eevee
