/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Heightfield displacement: builds the maximum mipmap pyramid of every enabled heightmap on the
 * CPU (once per heightmap change), then ray marches the camera rays in a fullscreen pass, writes
 * back the hit depth and exposes the analytic normal / ray marched shadow / AO to the deferred
 * lighting and combine passes.
 */

#include "BLI_math_base.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_string.hh"

#include <cstdio>
#include <cstdlib>

#include "BKE_idprop.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_object.hh"

#include "DNA_image_types.h"
#include "DNA_light_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"

#include "eevee_instance.hh"
#include "eevee_shader.hh"

namespace blender::eevee {

/* -------------------------------------------------------------------- */
/** \name ID property helpers
 *
 * The settings are stored as ID custom properties. This avoids any DNA / RNA / file-format
 * footprint (the physics node line is concurrently editing the DNA) while still exposing a native
 * panel in the Render and Object properties.
 * \{ */

static const IDProperty *hf_prop_get(const ID *id, const char *name)
{
  if (id == nullptr || id->properties == nullptr) {
    return nullptr;
  }
  return IDP_GetPropertyFromGroup(id->properties, name);
}

static bool hf_prop_bool(const ID *id, const char *name, bool fallback)
{
  const IDProperty *prop = hf_prop_get(id, name);
  if (prop == nullptr) {
    return fallback;
  }
  /* Be tolerant to the property type: Python may create an int instead of a boolean. */
  switch (prop->type) {
    case IDP_BOOLEAN:
      return IDP_bool_get(prop);
    case IDP_INT:
      return IDP_int_get(prop) != 0;
    case IDP_FLOAT:
      return IDP_float_get(prop) != 0.0f;
    default:
      return fallback;
  }
}

static int hf_prop_int(const ID *id, const char *name, int fallback)
{
  const IDProperty *prop = hf_prop_get(id, name);
  if (prop == nullptr) {
    return fallback;
  }
  switch (prop->type) {
    case IDP_INT:
      return IDP_int_get(prop);
    case IDP_BOOLEAN:
      return IDP_bool_get(prop) ? 1 : 0;
    case IDP_FLOAT:
      return int(IDP_float_get(prop));
    case IDP_DOUBLE:
      return int(IDP_double_get(prop));
    default:
      return fallback;
  }
}

static float hf_prop_float(const ID *id, const char *name, float fallback)
{
  const IDProperty *prop = hf_prop_get(id, name);
  if (prop == nullptr) {
    return fallback;
  }
  switch (prop->type) {
    case IDP_FLOAT:
      return IDP_float_get(prop);
    case IDP_DOUBLE:
      return float(IDP_double_get(prop));
    case IDP_INT:
      return float(IDP_int_get(prop));
    case IDP_BOOLEAN:
      return IDP_bool_get(prop) ? 1.0f : 0.0f;
    default:
      return fallback;
  }
}

static const char *hf_prop_string(const ID *id, const char *name)
{
  const IDProperty *prop = hf_prop_get(id, name);
  if (prop == nullptr || prop->type != IDP_STRING) {
    return nullptr;
  }
  return IDP_string_get(prop);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Maximum mipmap pyramid
 * \{ */

static void hf_pack_offset(int4 &packed, int sub, int value)
{
  if (sub == 0) {
    packed.x = value;
  }
  else if (sub == 1) {
    packed.y = value;
  }
  else if (sub == 2) {
    packed.z = value;
  }
  else {
    packed.w = value;
  }
}

static int hf_mip_count(int width, int height)
{
  int size = max_ii(width, height);
  int levels = 1;
  while (size > 1) {
    size = (size + 1) / 2;
    levels++;
  }
  return min_ii(levels, HF_MAX_MIPS);
}

void HeightfieldModule::sync_pyramids()
{
  /* Assign buffer offsets and concatenate every valid pyramid. */
  pyramid_data_.clear();
  for (HeightfieldPyramidCache &cache : cache_) {
    if (!cache.valid) {
      continue;
    }
    cache.base_offset = pyramid_data_.size();
    pyramid_data_.extend(cache.data);
  }

  if (pyramid_data_.is_empty()) {
    pyramid_data_.append(0.0f);
  }

  pyramid_buf_.resize(pyramid_data_.size());
  memcpy(pyramid_buf_.data(), pyramid_data_.data(), pyramid_data_.size() * sizeof(float));
  pyramid_buf_.push_update();
}

HeightfieldPyramidCache *HeightfieldModule::cache_get(blender::Image *image,
                                                      int rebuild_key,
                                                      float taper,
                                                      int taper_mode,
                                                      bool &rebuilt)
{
  rebuilt = false;
  HeightfieldPyramidCache *cache = nullptr;
  for (HeightfieldPyramidCache &entry : cache_) {
    if (entry.image == image && entry.taper == taper && entry.taper_mode == taper_mode) {
      cache = &entry;
      break;
    }
  }
  if (cache == nullptr) {
    cache_.append(HeightfieldPyramidCache());
    cache = &cache_.last();
    cache->image = image;
  }

  int width = 0, height = 0;
  BKE_image_get_size(image, nullptr, &width, &height);
  const uint64_t stamp = image->id.session_uid;
  if (cache->valid && cache->size == int2(width, height) && cache->stamp == stamp &&
      cache->rebuild_key == rebuild_key)
  {
    return cache;
  }

  cache->valid = false;
  cache->size = int2(width, height);
  cache->stamp = stamp;
  cache->rebuild_key = rebuild_key;
  cache->taper = taper;
  cache->taper_mode = taper_mode;
  cache->data.clear();

  if (width <= 0 || height <= 0) {
    return cache;
  }

  void *lock = nullptr;
  ImBuf *ibuf = BKE_image_acquire_ibuf(image, nullptr, &lock);
  if (ibuf == nullptr) {
    return cache;
  }

  const float *float_pixels = ibuf->float_buffer.data;
  const uchar *byte_pixels = ibuf->byte_buffer.data;
  const int channels = max_ii(ibuf->channels, 1);
  const int byte_channels = 4;

  if (float_pixels == nullptr && byte_pixels == nullptr) {
    BKE_image_release_ibuf(image, ibuf, lock);
    return cache;
  }

  const int mip_count = hf_mip_count(width, height);
  cache->mip_count = mip_count;

  float *level = nullptr;
  cache->data.resize(size_t(width) * size_t(height));
  level = cache->data.data();
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const size_t index = size_t(y) * size_t(width) + size_t(x);
      float value;
      if (float_pixels != nullptr) {
        value = float_pixels[index * size_t(channels)];
      }
      else {
        value = float(byte_pixels[index * size_t(byte_channels)]) / 255.0f;
      }
      level[index] = clamp(value, 0.0f, 1.0f);
    }
  }

  /* Border taper: fade the height to zero over `taper` texels at the border, so the displaced
   * sheet closes onto the carrier plane instead of ending at a nonzero height (which a real skirt
   * had to hide). The outermost texel row is exactly zero, so the sheet meets the plane at the
   * carrier edge and the silhouette is one continuous surface without any vertical face.
   *
   * This is applied to the field *before* the max / min pyramids and the Lipschitz constant are
   * derived, so the envelope, the trace, the shading normal and the shadow all see the same
   * surface. Tapering on the fetch instead would leave the envelope describing the untapered
   * field and reintroduce conservative-test inconsistencies near the border. */
  if (taper > 0.0f) {
    const int edge_x = width - 1;
    const int edge_y = height - 1;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        const int d = min_ii(min_ii(x, edge_x - x), min_ii(y, edge_y - y));
        float t = clamp(float(d) / taper, 0.0f, 1.0f);
        float w;
        switch (taper_mode) {
          case 1:
            /* Linear: constant slope, but the slope starts with a crease at d=0. */
            w = t;
            break;
          case 2:
            /* Quintic: zero value, slope and curvature at both ends, the softest border. */
            w = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            break;
          default:
            /* Smoothstep: C1 at both ends, the middle ground. */
            w = t * t * (3.0f - 2.0f * t);
            break;
        }
        level[size_t(y) * size_t(width) + size_t(x)] *= w;
      }
    }
  }

  BKE_image_release_ibuf(image, ibuf, lock);

  /* Largest per texel gradient of the normalised height. The sphere traced shadow divides a
   * vertical gap by sqrt(1 + L^2) (L in world units) to get a conservative lower bound on the
   * distance to the surface, so this is the Lipschitz constant of the height field. */
  cache->lip_texel = 0.0f;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const float h0 = level[size_t(y) * size_t(width) + size_t(x)];
      if (x + 1 < width) {
        cache->lip_texel = max_ff(cache->lip_texel,
                                  fabsf(level[size_t(y) * size_t(width) + size_t(x + 1)] - h0));
      }
      if (y + 1 < height) {
        cache->lip_texel = max_ff(cache->lip_texel,
                                  fabsf(level[size_t(y + 1) * size_t(width) + size_t(x)] - h0));
      }
    }
  }

  if (getenv("HF_DEBUG")) {
    float hmax = 0.0f;
    for (float v : cache->data) {
      hmax = max_ff(hmax, v);
    }
    fprintf(stderr,
            "[hf] image %dx%d float=%d byte=%d channels=%d max=%.4f\n",
            width,
            height,
            int(float_pixels != nullptr),
            int(byte_pixels != nullptr),
            channels,
            hmax);
  }

  /* Build the maximum pyramid. */
  int level_width = width;
  int level_height = height;
  int src_offset = 0;
  for (int mip = 1; mip < mip_count; mip++) {
    const int next_width = max_ii((level_width + 1) / 2, 1);
    const int next_height = max_ii((level_height + 1) / 2, 1);
    const int base = cache->data.size();
    cache->data.resize(base + size_t(next_width) * size_t(next_height));
    const float *src = cache->data.data() + src_offset;
    float *dst = cache->data.data() + base;
    for (int y = 0; y < next_height; y++) {
      for (int x = 0; x < next_width; x++) {
        const int sx = min_ii(x * 2, level_width - 1);
        const int sy = min_ii(y * 2, level_height - 1);
        const int sx1 = min_ii(sx + 1, level_width - 1);
        const int sy1 = min_ii(sy + 1, level_height - 1);
        const float a = src[size_t(sy) * level_width + sx];
        const float b = src[size_t(sy) * level_width + sx1];
        const float c = src[size_t(sy1) * level_width + sx];
        const float d = src[size_t(sy1) * level_width + sx1];
        dst[size_t(y) * next_width + x] = max_ff(max_ff(a, b), max_ff(c, d));
      }
    }
    src_offset = base;
    level_width = next_width;
    level_height = next_height;
  }

  /* Build the minimum pyramid. Level 0 aliases the maximum pyramid (a single texel has
   * min == max); only the downsampled levels are stored, in a second block of the same buffer.
   * The trace uses it to prove that a ray lies below the whole cell (inside the solid), which
   * turns the per-texel crawl under a peak into a single step. */
  cache->max_size = int(cache->data.size());
  level_width = width;
  level_height = height;
  src_offset = 0;
  for (int mip = 1; mip < mip_count; mip++) {
    const int next_width = max_ii((level_width + 1) / 2, 1);
    const int next_height = max_ii((level_height + 1) / 2, 1);
    const int base = int(cache->data.size());
    cache->data.resize(size_t(base) + size_t(next_width) * size_t(next_height));
    const float *src = cache->data.data() + src_offset;
    float *dst = cache->data.data() + base;
    for (int y = 0; y < next_height; y++) {
      for (int x = 0; x < next_width; x++) {
        const int sx = min_ii(x * 2, level_width - 1);
        const int sy = min_ii(y * 2, level_height - 1);
        const int sx1 = min_ii(sx + 1, level_width - 1);
        const int sy1 = min_ii(sy + 1, level_height - 1);
        const float a = src[size_t(sy) * level_width + sx];
        const float b = src[size_t(sy) * level_width + sx1];
        const float c = src[size_t(sy1) * level_width + sx];
        const float d = src[size_t(sy1) * level_width + sx1];
        dst[size_t(y) * next_width + x] = min_ff(min_ff(a, b), min_ff(c, d));
      }
    }
    src_offset = base;
    level_width = next_width;
    level_height = next_height;
  }

  /* Precompute the per-level offsets inside the pyramid. */
  int offset = 0;
  int mip_width = width;
  int mip_height = height;
  for (int mip = 0; mip < HF_MAX_MIPS; mip++) {
    int4 &packed = cache->mip_offsets[mip >> 2];
    hf_pack_offset(packed, mip & 3, offset);
    if (mip < mip_count) {
      offset += max_ii(mip_width, 1) * max_ii(mip_height, 1);
      mip_width = (mip_width + 1) / 2;
      mip_height = (mip_height + 1) / 2;
    }
  }

  /* Minimum pyramid offsets: level 0 aliases the maximum level 0 (offset 0), the rest start at
   * `max_size` and follow the same level sizes. */
  offset = cache->max_size;
  mip_width = (width + 1) / 2;
  mip_height = (height + 1) / 2;
  for (int mip = 0; mip < HF_MAX_MIPS; mip++) {
    int4 &packed = cache->mip_offsets_min[mip >> 2];
    hf_pack_offset(packed, mip & 3, (mip == 0) ? 0 : offset);
    if (mip >= 1 && mip < mip_count) {
      offset += max_ii(mip_width, 1) * max_ii(mip_height, 1);
      mip_width = (mip_width + 1) / 2;
      mip_height = (mip_height + 1) / 2;
    }
  }

  cache->valid = true;
  rebuilt = true;
  return cache;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Module
 * \{ */

void HeightfieldModule::init()
{
  /* The trace pass is re-recorded every frame (see `render()`): the draw command must come after
   * the resource bindings, so the pass cannot be built once at init time. */
}

void HeightfieldModule::sync()
{
  enabled_ = hf_prop_bool(&inst_.scene->id, "hf_enabled", false);
  rendered_ = false;
  instance_count_ = 0;
  sun_ = float4(0.0f, 0.0f, 1.0f, 0.0f);

  if (!enabled_) {
    return;
  }

  DEGObjectIterSettings iter_settings = {nullptr};
  iter_settings.depsgraph = inst_.depsgraph;
  iter_settings.flags = DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY |
                        DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET;
  const int scene_steps = clamp_i(hf_prop_int(&inst_.scene->id, "hf_steps", 12), 1, 32);
  const int scene_binary = clamp_i(hf_prop_int(&inst_.scene->id, "hf_binary_steps", 5), 0, 8);
  const bool use_shadow = hf_prop_bool(&inst_.scene->id, "hf_shadow", true);
  const bool use_ao = hf_prop_bool(&inst_.scene->id, "hf_ao", true);
  const float shadow_jitter = max_ff(hf_prop_float(&inst_.scene->id, "hf_shadow_jitter", 0.02f),
                                     0.0f);

  Main *bmain = DEG_get_bmain(inst_.depsgraph);
  bool pyramid_rebuilt = false;
  bool has_instance = false;
  /* Cache used by each instance, to patch the pyramid base offsets after `sync_pyramids()`. */
  Vector<std::pair<int, HeightfieldPyramidCache *>> instance_caches;

  DEG_OBJECT_ITER_BEGIN (&iter_settings, ob) {
    if (ob->type != OB_MESH || !hf_prop_bool(&ob->id, "hf_enabled", false)) {
      continue;
    }
    if (instance_count_ >= HF_MAX_INSTANCES) {
      break;
    }
    const char *image_name = hf_prop_string(&ob->id, "hf_heightmap");
    if (image_name == nullptr || image_name[0] == '\0') {
      continue;
    }
    blender::Image *image = reinterpret_cast<blender::Image *>(
        BKE_libblock_find_name(bmain, ID_IM, image_name));
    if (image == nullptr) {
      continue;
    }
    const float4x4 object_to_world = ob->object_to_world();
    if (math::determinant(object_to_world) == 0.0f) {
      continue;
    }

    const std::optional<Bounds<float3>> bounds = BKE_object_boundbox_get(ob);
    if (!bounds.has_value()) {
      continue;
    }
    const float3 extent = bounds->max - bounds->min;
    if (extent.x <= 1.0e-6f || extent.y <= 1.0e-6f) {
      continue;
    }

    const int rebuild_key = hf_prop_int(&ob->id, "hf_rebuild", 0);
    /* Border taper in heightmap texels (0 = off, the historic field). Baked into the pyramid so
     * every consumer sees the same surface; see `cache_get`. */
    const float taper = clamp_f(hf_prop_float(&ob->id, "hf_border_taper", 0.0f), 0.0f, 1024.0f);
    const int taper_mode = clamp_i(hf_prop_int(&ob->id, "hf_border_taper_mode", 0), 0, 2);
    bool rebuilt = false;
    HeightfieldPyramidCache *cache = cache_get(image, rebuild_key, taper, taper_mode, rebuilt);
    pyramid_rebuilt |= rebuilt;
    if (cache == nullptr || !cache->valid) {
      continue;
    }

    const float min_height = hf_prop_float(&ob->id, "hf_min_height", 0.0f);
    const float max_height = hf_prop_float(&ob->id, "hf_max_height", 1.0f);
    if (max_height <= min_height) {
      continue;
    }

    const int steps = clamp_i(hf_prop_int(&ob->id, "hf_steps", scene_steps), 1, 32);
    const int binary_steps = clamp_i(hf_prop_int(&ob->id, "hf_binary_steps", scene_binary), 0, 8);
    /* Shadow rays test a smoothed envelope: at level 0 the texel staircase of the displacement
     * self shadows into contour aligned bands. */
    const int shadow_level = clamp_i(hf_prop_int(&ob->id, "hf_shadow_level", 0), 0, 4);

    const float4x4 world_to_object = math::invert(object_to_world);
    const float epsilon = max_ff(
        1.0e-4f, 1.0e-3f * max_ff(max_ff(extent.x, extent.y), extent.z));

    HeightfieldData &data = instance_buf_.data()[instance_count_];
    data.world_to_local = world_to_object;
    data.local_to_world = object_to_world;
    data.bounds_min = float4(bounds->min, 1.0f);
    data.bounds_max = float4(bounds->max, 1.0f);
    data.params = float4(
        min_height, max_height, hf_prop_float(&ob->id, "hf_normal_strength", 1.0f), epsilon);
    data.tex_info = int4(
        cache->size.x, cache->size.y, cache->mip_count, cache->base_offset);
    /* z: flags, bit0 = enabled, bits 8..15 = sub texel hit refinement iterations. */
    const int refine_iters = clamp_i(hf_prop_int(&ob->id, "hf_refine", 2), 0, 8);
    data.quality = int4(steps, shadow_level, 1 | (refine_iters << 8), 0);
    data.shading = float4(
        use_shadow ? hf_prop_float(&ob->id, "hf_shadow_strength", 0.8f) : 0.0f,
        use_ao ? hf_prop_float(&ob->id, "hf_ao_strength", 0.6f) : 0.0f,
        max_ff(hf_prop_float(&ob->id, "hf_ao_radius", 0.25f), 1.0e-4f),
        shadow_jitter);
    /* x: cavity strength, y: screen space silhouette extension (texels, 0 when a real skirt
     * exists), z: grazing elevation threshold, w: march start jitter in texels. */
    data.extras = float4(max_ff(hf_prop_float(&ob->id, "hf_cavity", 0.0f), 0.0f),
                         max_ff(hf_prop_float(&ob->id, "hf_edge_extend", 1.0f), 0.0f),
                         clamp_f(hf_prop_float(&ob->id, "hf_grazing_sin", 0.18f), 0.0f, 1.0f),
                         max_ff(hf_prop_float(&ob->id, "hf_march_jitter", 0.0f), 0.0f));
    /* w: level of the coarse normal used for the grazing blend (0 = always level 0). */
    data.quality.w = clamp_i(hf_prop_int(&ob->id, "hf_normal_mip", 0), 0, 6);
    /* x: max shadow trace distance in texels, y: penumbra fade distance in texels, z: shadow ray
     * start jitter in texels, w: border contact darkening strength. */
    data.shadow2 = float4(max_ff(hf_prop_float(&ob->id, "hf_shadow_max", 32.0f), 0.0f),
                          max_ff(hf_prop_float(&ob->id, "hf_shadow_soft", 6.0f), 0.0f),
                          max_ff(hf_prop_float(&ob->id, "hf_shadow_jitter", 1.0f), 0.0f),
                          clamp_f(hf_prop_float(&ob->id, "hf_edge_ao", 0.35f), 0.0f, 1.0f));
    /* Conservative distance bound of the height surface, used by the sphere traced shadow.
     *
     * For a graph `z = H(x, y)` the distance from a point above it is at least
     * `(z - H) / sqrt(1 + L^2)` where `L = sup |grad H|` (the vertical gap is split by the steepest
     * possible tangent). `lip_texel` is the largest per texel height difference of the normalised
     * height, so multiplying by the world height range over the smaller texel width gives the world
     * space Lipschitz constant. */
    const float2 texel_xy = float2(extent.x / float(max_ii(cache->size.x, 1)),
                                   extent.y / float(max_ii(cache->size.y, 1)));
    const float texel_min = max_ff(min_ff(texel_xy.x, texel_xy.y), 1.0e-6f);
    const float grad_world = cache->lip_texel * (max_height - min_height) / texel_min;
    const bool sdf_shadow = hf_prop_int(&ob->id, "hf_shadow_mode", 1) != 0;
    data.sdf = float4(sqrtf(1.0f + grad_world * grad_world),
                      max_ff(hf_prop_float(&ob->id, "hf_sdf_min", 4.0f), 1.0e-3f),
                      float(clamp_i(hf_prop_int(&ob->id, "hf_sdf_iters", 32), 4, 128)),
                      sdf_shadow ? 1.0f : 0.0f);
    if (getenv("HF_DEBUG")) {
      fprintf(stderr, "[hf] sdf lip=%.3f min=%.3f iters=%.0f mode=%.0f\n",
              data.sdf.x, data.sdf.y, data.sdf.z, data.sdf.w);
    }
    /* x: sun cone tangent, y: widened carrier band in texels (the domain is inset by that band and
     * the band repeats the border height, 0 = the carrier is the domain), z: thin sheet coverage
     * (1 = uncovered carrier pixels are empty, 0 = the flat carrier stays visible), w: miss path
     * diagnostic selector (0 = off, plain debugging hook).
     *
     * Thin sheet stays off by default: with 24 march steps about 4.7% of the compared carrier
     * pixels are ridge tangency misses, and erasing those would punch dark speckles along every
     * crest. Revisit the default only once the miss rate is low enough that the erase is invisible
     * (see the heightfield documentation, miss path root cause section). */
    data.shadow3 = float4(max_ff(hf_prop_float(&ob->id, "hf_shadow_cone", 0.026f), 1.0e-4f),
                          max_ff(hf_prop_float(&ob->id, "hf_carrier_band", 0.0f), 0.0f),
                          (hf_prop_int(&ob->id, "hf_thin_sheet", 0) != 0) ? 1.0f : 0.0f,
                          float(clamp_i(hf_prop_int(&ob->id, "hf_debug_reason", 0), 0, 4096)));
    for (int i = 0; i < HF_MAX_MIPS / 4; i++) {
      data.mip_offsets[i] = cache->mip_offsets[i];
      data.mip_offsets_min[i] = cache->mip_offsets_min[i];
    }

    instance_caches.append({instance_count_, cache});
    instance_count_++;
    has_instance = true;
  }
  DEG_OBJECT_ITER_END;

  /* Find the first sun light. It is used for the ray marched self shadowing. */
  if (use_shadow && has_instance) {
    DEG_OBJECT_ITER_BEGIN (&iter_settings, ob) {
      if (ob->type != OB_LAMP || ob->data == nullptr) {
        continue;
      }
      const blender::Light *light = reinterpret_cast<const blender::Light *>(ob->data);
      if (light->type != LA_SUN) {
        continue;
      }
      const float3 direction = math::normalize(ob->object_to_world().z_axis());
      sun_ = float4(direction, 1.0f);
      break;
    }
    DEG_OBJECT_ITER_END;
  }

  if (!has_instance) {
    enabled_ = false;
    return;
  }

  if (pyramid_rebuilt || pyramid_data_.is_empty()) {
    sync_pyramids();
  }
  /* The pyramid buffer layout is only known after the concatenation: patch the per instance base
   * offsets (they can change when another heightfield is added or invalidated). */
  for (const auto &[index, instance_cache] : instance_caches) {
    instance_buf_.data()[index].tex_info.w = instance_cache->base_offset;
  }

  /* The instance buffer is small (a few KB), always refresh it. */
  instance_buf_.push_update();
  if (getenv("HF_DEBUG")) {
    fprintf(stderr,
            "[hf] sync enabled=%d instances=%d rebuilt=%d steps=%d binary=%d sun=%.3f "
            "sun_dir=(%.2f %.2f %.2f) shade=(%.2f %.2f %.2f %.2f)\n",
            int(enabled_),
            instance_count_,
            int(pyramid_rebuilt),
            scene_steps,
            scene_binary,
            sun_.w,
            sun_.x,
            sun_.y,
            sun_.z,
            instance_buf_.data()[0].shading.x,
            instance_buf_.data()[0].shading.y,
            instance_buf_.data()[0].shading.z,
            instance_buf_.data()[0].shading.w);
  }
}

bool HeightfieldModule::render(View &view)
{
  const int2 extent = inst_.render_buffers.extent_get();

  /* The trace must run once per TAA sample: the camera projection is jittered per sample and the
   * shadow / AO jitter comes from the sampling RNG. `sync()` is only called once per frame, so the
   * once-per-frame guard is reset whenever the sample index changes. Within a sample the refraction
   * layer must not trace a depth buffer that is already displaced. */
  if (inst_.sampling.sample_index() != traced_sample_) {
    traced_sample_ = inst_.sampling.sample_index();
    rendered_ = false;
  }

  if (!enabled_) {
    /* Bind a valid 1x1 zero texture so the deferred passes can always sample the resources. */
    geom_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                       int2(1, 1),
                       GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ);
    shade_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                        int2(1, 1),
                        GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ);
    geom_tx_.clear(float4(0.0f));
    shade_tx_.clear(float4(1.0f, 1.0f, 1.0f, 0.0f));
    return false;
  }

  /* Only trace once per TAA sample. The second deferred layer (refraction) would otherwise ray
   * march a depth buffer that already contains the displaced surface. */
  if (rendered_ || instance_count_ == 0) {
    return false;
  }
  rendered_ = true;

  geom_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                     extent,
                     GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ);
  shade_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                      extent,
                      GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ);

  trace_fb_.ensure(GPU_ATTACHMENT_TEXTURE(inst_.render_buffers.depth_tx),
                   GPU_ATTACHMENT_TEXTURE(geom_tx_),
                   GPU_ATTACHMENT_TEXTURE(shade_tx_));

  const float4x4 clip_to_world = view.viewinv() * view.wininv();
  const float4x4 world_to_clip = view.winmat() * view.viewmat();
  const float4 camera = float4(view.location(), (view.winmat()[3][3] < 0.5f) ? 1.0f : 0.0f);
  const int instance_count = instance_count_;

  /* Record the pass every frame. A `PassSimple` executes its recorded command list in order: the
   * state, shader, framebuffer and resource bindings must all be recorded *before* the draw, and
   * the draw command carries the push constant upload. Recording the draw once in `init()` and
   * only appending bindings per frame leaves `instance_count` at its default value. */
  trace_ps_.init();
  /* `ALWAYS`: the pass also has to be able to write the far depth for carrier pixels the trace does
   * not hit (thin sheet coverage). Those writes are strictly farther than the current depth, so the
   * previous `DEPTH_GREATER` state silently dropped them. The shader only writes pixels whose
   * current depth is the carrier plane itself, so no real geometry can be overwritten. */
  trace_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_ALWAYS);
  trace_ps_.shader_set(inst_.shaders.static_shader_get(HEIGHTFIELD_TRACE));
  trace_ps_.framebuffer_set(&trace_fb_);
  trace_ps_.bind_resources(inst_.hiz_buffer.front);
  trace_ps_.bind_ssbo(HF_INSTANCE_BUF_SLOT, &instance_buf_);
  trace_ps_.bind_ssbo(HF_PYRAMID_BUF_SLOT, &pyramid_buf_);
  trace_ps_.bind_resources(inst_.sampling);
  trace_ps_.push_constant("instance_count", instance_count);
  trace_ps_.push_constant("clip_to_world", clip_to_world);
  trace_ps_.push_constant("world_to_clip", world_to_clip);
  trace_ps_.push_constant("camera", camera);
  trace_ps_.push_constant("sun", sun_);
  trace_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
  /* Make the depth buffer writes visible to the HiZ rebuild that follows. */
  trace_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH);

  if (getenv("HF_DEBUG")) {
    fprintf(stderr,
            "[hf] render count=%d extent=%dx%d persp=%d sun=%.2f\n",
            instance_count,
            extent.x,
            extent.y,
            int(camera.w),
            sun_.w);
  }
  inst_.manager->submit(trace_ps_, view);
  return true;
}

/** \} */

}  // namespace blender::eevee
