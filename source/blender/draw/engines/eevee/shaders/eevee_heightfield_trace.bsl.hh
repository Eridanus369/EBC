/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Heightfield displacement trace pass.
 *
 * Fullscreen pass run after the deferred G-Buffer pass. For every pixel it rebuilds the camera ray,
 * intersects it against every active heightfield using a maximum mipmap pyramid (hierarchical
 * stepping) followed by a binary search, writes the hit depth back to the depth buffer, and outputs
 * the analytic normal, ray marched soft shadow and ambient occlusion factors to two auxiliary
 * targets consumed by the deferred lighting / combine passes.
 */

#pragma once

#include "eevee_hiz.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_sampling_lib.bsl.hh"
#include "gpu_shader_fullscreen.bsl.hh"
#include "gpu_shader_math_base.bsl.hh"
#include "eevee_heightfield_shared.hh"

namespace eevee::heightfield {

struct VertOut {
  [[smooth]] float2 screen_uv;
};

[[vertex]]
void fullscreen_vert([[vertex_id]] const int vert_id,
                     [[position]] float4 &out_position,
                     [[out]] VertOut &v_out)
{
  fullscreen_vertex(vert_id, out_position, v_out.screen_uv);
  out_position = reverse_z::transform(out_position);
}

struct HeightfieldBufs {
  /* Must match HF_INSTANCE_BUF_SLOT / HF_PYRAMID_BUF_SLOT (literals are required here as the
   * generated create info does not include the shared header). */
  [[storage(0, read)]] const HeightfieldData (&instances)[];
  [[storage(1, read)]] const float (&pyramid)[];
};

struct HeightfieldPush {
  [[push_constant]] const int instance_count;
  [[push_constant]] const float4x4 clip_to_world;
  [[push_constant]] const float4x4 world_to_clip;
  /* xyz: camera position, w: 1 if perspective projection. */
  [[push_constant]] const float4 camera;
  /* xyz: direction towards the sun, w: 1 if a sun light exists. */
  [[push_constant]] const float4 sun;
};

struct FragOut {
  [[frag_color(0)]] float4 geom;
  [[frag_color(1)]] float4 shade;
};

/* -------------------------------------------------------------------- */
/** \name Pyramid access
 * \{ */

int hf_mip_offset(HeightfieldData hf, int level)
{
  int4 v = hf.mip_offsets[level >> 2];
  int sub = level & 3;
  if (sub == 0) {
    return v.x;
  }
  if (sub == 1) {
    return v.y;
  }
  if (sub == 2) {
    return v.z;
  }
  return v.w;
}

float hf_fetch_level(HeightfieldData hf,
                     [[resource_table]] const HeightfieldBufs &bufs,
                     float2 uv,
                     int level)
{
  int lw = max(1, (hf.tex_info.x + (1 << level) - 1) >> level);
  int lh = max(1, (hf.tex_info.y + (1 << level) - 1) >> level);
  int x = clamp(int(uv.x * float(lw)), 0, lw - 1);
  int y = clamp(int(uv.y * float(lh)), 0, lh - 1);
  int index = hf.tex_info.w + hf_mip_offset(hf, level) + y * lw + x;
  return bufs.pyramid[index];
}

/** Minimum height of the 2x2 footprint at `level` (level 0 aliases the maximum pyramid). */
float hf_fetch_level_min(HeightfieldData hf,
                         [[resource_table]] const HeightfieldBufs &bufs,
                         float2 uv,
                         int level)
{
  int lw = max(1, (hf.tex_info.x + (1 << level) - 1) >> level);
  int lh = max(1, (hf.tex_info.y + (1 << level) - 1) >> level);
  int x = clamp(int(uv.x * float(lw)), 0, lw - 1);
  int y = clamp(int(uv.y * float(lh)), 0, lh - 1);
  int4 offs = hf.mip_offsets_min[level >> 2];
  int sub = level & 3;
  int offset = (sub == 0) ? offs.x : ((sub == 1) ? offs.y : ((sub == 2) ? offs.z : offs.w));
  int index = hf.tex_info.w + offset + y * lw + x;
  return bufs.pyramid[index];
}

float hf_fetch_height(HeightfieldData hf,
                      [[resource_table]] const HeightfieldBufs &bufs,
                      float2 uv)
{
  return hf_fetch_level(hf, bufs, uv, 0);
}

/**
 * Bilinear reconstruction of the height surface. The pyramid is a max / min envelope, so only the
 * actual surface reconstruction uses filtered taps: the interval tests stay nearest neighbour and
 * conservative. Without this the hit height is a per texel constant, which shows up as a staircase
 * in the shading normal at grazing angles.
 */
float hf_fetch_bilinear(HeightfieldData hf,
                        [[resource_table]] const HeightfieldBufs &bufs,
                        float2 uv,
                        int level)
{
  int lw = max(1, (hf.tex_info.x + (1 << level) - 1) >> level);
  int lh = max(1, (hf.tex_info.y + (1 << level) - 1) >> level);
  float2 coord = uv * float2(lw, lh) - 0.5f;
  float2 base = floor(coord);
  float2 f = coord - base;
  int x0 = clamp(int(base.x), 0, lw - 2);
  int y0 = clamp(int(base.y), 0, lh - 2);
  float2 inv = float2(1.0f / float(lw), 1.0f / float(lh));
  float h00 = hf_fetch_level(hf, bufs, (float2(x0, y0) + 0.5f) * inv, level);
  float h10 = hf_fetch_level(hf, bufs, (float2(x0 + 1, y0) + 0.5f) * inv, level);
  float h01 = hf_fetch_level(hf, bufs, (float2(x0, y0 + 1) + 0.5f) * inv, level);
  float h11 = hf_fetch_level(hf, bufs, (float2(x0 + 1, y0 + 1) + 0.5f) * inv, level);
  return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
}

/** Surface height at `uv`: bilinear at level 0, nearest at the coarser envelope levels. */
float hf_fetch_surface(HeightfieldData hf,
                       [[resource_table]] const HeightfieldBufs &bufs,
                       float2 uv,
                       int level)
{
  return (level == 0) ? hf_fetch_bilinear(hf, bufs, uv, 0) :
                        hf_fetch_level(hf, bufs, uv, level);
}

float hf_height_to_z(HeightfieldData hf, float h)
{
  return hf.bounds_min.z + hf.params.x + h * (hf.params.y - hf.params.x);
}

/**
 * Lateral extent of the heightmap domain inside the carrier bounds.
 *
 * `hf.shadow3.y` widens the carrier mesh by that many texels on every side without stretching the
 * heightmap: the domain is the bounds inset by the band, and the band repeats the border cell
 * height (all fetches clamp their indices). Zero (the default) makes the domain the full bounds,
 * which is the behaviour of every configuration before this change.
 *
 * The texel count over the band is the same as inside, so one texel is exactly `1 / size` in
 * carrier uv space and the march cell bookkeeping is unchanged.
 */
float2 hf_domain_ext(HeightfieldData hf)
{
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  if (hf.shadow3.y <= 0.0f) {
    return ext;
  }
  const float2 texels = max(float2(float(hf.tex_info.x), float(hf.tex_info.y)), float2(1.0f));
  const float2 texel = ext / (texels + 2.0f * hf.shadow3.y);
  return max(ext - 2.0f * texel * hf.shadow3.y, float2(1.0e-6f));
}

/** Heightmap domain uv. Values outside `[0, 1]` fall inside the clamped border band. */
float2 hf_local_to_uv(HeightfieldData hf, float2 p)
{
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  const float2 dom = hf_domain_ext(hf);
  const float2 margin = 0.5f * (ext - dom);
  return (p - hf.bounds_min.xy - margin) / dom;
}

/** Carrier uv over the full object bounds (border band included). */
float2 hf_local_to_uv_wide(HeightfieldData hf, float2 p)
{
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  return (p - hf.bounds_min.xy) / ext;
}

/* Defined with the shading helpers below; the sphere trace needs it for its penumbra phase. */
float3 hf_bicubic(HeightfieldData hf,
                  [[resource_table]] const HeightfieldBufs &bufs,
                  float2 uv);

/**
 * Conservative lower bound of the distance from `p` (object space) to the height surface.
 * Positive above the surface, zero or negative inside the solid.
 *
 * The height surface is a graph `z = H(x, y)`, so the vertical gap at the same `(x, y)` can be split
 * by the steepest possible tangent: `|p - surface| >= (p.z - H) / sqrt(1 + L^2)` with
 * `L = sup |grad H|`. `hf.sdf.x` stores that constant. This bound is smooth in `p` (no per texel
 * classification), which is what makes the sphere traced shadow free of the contour aligned bands
 * of the interval march.
 */
float hf_surface_distance(HeightfieldData hf,
                          [[resource_table]] const HeightfieldBufs &bufs,
                          float3 p)
{
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  const float2 uv_wide = hf_local_to_uv_wide(hf, p.xy);
  const float2 outside = max(max(-uv_wide, uv_wide - float2(1.0f)), float2(0.0f));
  if (any(greaterThan(outside, float2(0.0f)))) {
    /* Outside the carrier the heightmap is a thin sheet: the lateral distance to the rectangle is a
     * lower bound of the distance to it, and it is large enough to skip the whole empty region. The
     * clamped border band counts as surface, so the early out only triggers past the carrier. */
    return length(outside * ext);
  }
  const float h = hf_fetch_bilinear(
      hf, bufs, clamp(hf_local_to_uv(hf, p.xy), float2(0.0f), float2(1.0f)), 0);
  return (p.z - hf_height_to_z(hf, h)) / hf.sdf.x;
}

/**
 * Same distance bound as `hf_surface_distance`, but against the C1 bicubic surface.
 *
 * The bilinear height is only C0: its gradient is piecewise constant, so the bound has a kink at
 * every texel border. Sphere tracing turns those kinks into a texel periodic decision pattern, which
 * reads as fine stripes on the lit slopes and as a serrated shadow edge (it survives 128 TAA
 * samples, so it is structural, not noise). The Catmull-Rom surface is C1, so the bound is smooth and
 * the reported closest approach is a continuous function of the ray. Used only inside the penumbra
 * phase, where sub texel accuracy matters and the 16 taps are affordable.
 */
float hf_surface_distance_smooth(HeightfieldData hf,
                                 [[resource_table]] const HeightfieldBufs &bufs,
                                 float3 p)
{
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  const float2 uv_wide = hf_local_to_uv_wide(hf, p.xy);
  const float2 outside = max(max(-uv_wide, uv_wide - float2(1.0f)), float2(0.0f));
  if (any(greaterThan(outside, float2(0.0f)))) {
    return length(outside * ext);
  }
  const float h = hf_bicubic(
      hf, bufs, clamp(hf_local_to_uv(hf, p.xy), float2(0.0f), float2(1.0f))).x;
  return (p.z - hf_height_to_z(hf, h)) / hf.sdf.x;
}

/**
 * Sphere tracing against the conservative distance bound, with a soft terminator.
 *
 * Returns the occlusion in [0, 1]: 1 when the ray provably enters the solid, 0 when it leaves the
 * domain untouched, and `1 - phi_min / margin` while it skims the surface.
 *
 * The trace runs in two phases. Far from the surface the conservative bound is a safe step, so a
 * `min_step` many texels long is used to reach distant occluders with a small iteration budget.
 * Once the ray is inside the penumbra (`phi < margin`, a distance of the same order as that coarse
 * step) it switches to sub texel steps and keeps the closest approach. Evaluating `phi` only at the
 * coarse sample positions is what produced the residual dot pattern along the shadow edge: the
 * `phi` sequence is smooth, but the sample phase differs per pixel, so each pixel reported a
 * different point of the ramp. Tracking the actual minimum removes that sampling phase.
 */
float hf_sphere_trace(HeightfieldData hf,
                      [[resource_table]] const HeightfieldBufs &bufs,
                      float3 o,
                      float3 d,
                      float t_enter,
                      float t_exit,
                      int steps)
{
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  const float2 texel_uv = 1.0f / max(float2(hf.tex_info.x, hf.tex_info.y), float2(1.0f));
  const float texel_world = min(texel_uv.x * ext.x, texel_uv.y * ext.y);
  const float min_step = max(hf.sdf.y * texel_world, 1.0e-6f);
  /* Half a texel: the finest step the level 0 reconstruction can resolve, so the minimum it finds
   * is accurate to the surface detail that is actually stored. */
  const float fine_step = max(0.5f * texel_world, 1.0e-6f);
  /* Tangent of the sun half angle: the penumbra widens with the distance of the occluder. One
   * deterministic ray plus this analytic width replaces the previous two jittered binary rays, which
   * could only produce 0 / 0.5 / 1 and left salt and pepper noise inside the shadow. */
  const float cone = max(hf.shadow3.x, 1.0e-4f);
  float t = max(t_enter, 0.0f);
  for (int i = 0; i < steps && t < t_exit; i++) {
    const float3 p = o + t * d;
    const float phi = hf_surface_distance(hf, bufs, p);
    if (phi <= 0.0f) {
      return 1.0f;
    }
    const float margin = max(texel_world, cone * t);
    if (phi < margin) {
      /* Penumbra phase: walk the last stretch with sub texel steps and report the closest approach
       * instead of the first sample that happens to fall below the margin. */
      float phi_min = phi;
      float t_min = t;
      float t_search = t;
      for (int j = 0; j < 12; j++) {
        t_search += fine_step;
        if (t_search >= t_exit) {
          break;
        }
        const float pj = hf_surface_distance_smooth(hf, bufs, o + t_search * d);
        if (pj <= 0.0f) {
          return 1.0f;
        }
        if (pj < phi_min) {
          phi_min = pj;
          t_min = t_search;
        }
        else if (pj > phi_min + 2.0f * fine_step) {
          /* Passed the closest approach: the remaining distance only grows. */
          break;
        }
      }
      const float penumbra = max(texel_world, cone * t_min);
      return clamp(1.0f - phi_min / penumbra, 0.0f, 1.0f);
    }
    /* Never step further than the free distance reported by the bound: the old `min_step` floor
     * could jump over a ridge that was closer than the floor, which is what broke the long shadows
     * at low sun elevations. The floor is now capped by the penumbra width. */
    t += max(phi, min(min_step, margin));
  }
  return 0.0f;
}

/**
 * Damped Newton refinement of a camera hit against the bilinear surface. The hierarchical march
 * solves the crossing once per texel, so the reconstructed hit is C0 but not C1 across texel
 * borders, which reads as a staircase on grazing walls. Re-solving at the actual hit position moves
 * it off the texel grid without any extra marching steps.
 */
float hf_refine_hit(HeightfieldData hf,
                    [[resource_table]] const HeightfieldBufs &bufs,
                    float3 o,
                    float3 d,
                    float t,
                    float t_end,
                    int iterations)
{
  float tt = t;
  for (int i = 0; i < iterations; i++) {
    const float3 p = o + tt * d;
    const float2 uv = clamp(hf_local_to_uv(hf, p.xy), float2(0.0f), float2(1.0f));
    const float hz = hf_height_to_z(hf, hf_fetch_bilinear(hf, bufs, uv, 0));
    const float gap = hz + hf.params.w - p.z;
    if (abs(d.z) < 1.0e-9f) {
      break;
    }
    const float dt = 0.5f * gap / d.z;
    if (abs(dt) <= hf.params.w) {
      break;
    }
    tt = clamp(tt + dt, t, t_end);
  }
  return tt;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Intersection
 * \{ */

bool hf_ray_box(float3 o, float3 d, float3 bmin, float3 bmax, float &t0, float &t1)
{
  t0 = 0.0f;
  t1 = 1.0e38f;

  if (abs(d.x) < 1.0e-12f) {
    if (o.x < bmin.x || o.x > bmax.x) {
      return false;
    }
  }
  else {
    float ta = (bmin.x - o.x) / d.x;
    float tb = (bmax.x - o.x) / d.x;
    t0 = max(t0, min(ta, tb));
    t1 = min(t1, max(ta, tb));
  }
  if (abs(d.y) < 1.0e-12f) {
    if (o.y < bmin.y || o.y > bmax.y) {
      return false;
    }
  }
  else {
    float ta = (bmin.y - o.y) / d.y;
    float tb = (bmax.y - o.y) / d.y;
    t0 = max(t0, min(ta, tb));
    t1 = min(t1, max(ta, tb));
  }
  if (abs(d.z) < 1.0e-12f) {
    if (o.z < bmin.z || o.z > bmax.z) {
      return false;
    }
  }
  else {
    float ta = (bmin.z - o.z) / d.z;
    float tb = (bmax.z - o.z) / d.z;
    t0 = max(t0, min(ta, tb));
    t1 = min(t1, max(ta, tb));
  }
  return t0 <= t1;
}

/**
 * Distance along the ray at which it enters the inset heightmap domain.
 * Returns a negative diagnostic code when it never does (the caller maps it to `hf_miss`):
 * -1: the ray is parallel to one axis and already outside its slab,
 * -2: the entry parameter is not positive (the ray is on / inside the slab frontier),
 * -3: the entry is behind the ray (the slabs do not overlap ahead).
 * Only valid when the origin is outside the domain.
 */
float hf_air_entry(float2 uv, float2 duv, float2 inset)
{
  float t0 = 0.0f;
  float t1 = 1.0e38f;
  if (abs(duv.x) < 1.0e-12f) {
    if (uv.x < inset.x || uv.x > 1.0f - inset.x) {
      return -1.0f;
    }
  }
  else {
    const float ta = (inset.x - uv.x) / duv.x;
    const float tb = (1.0f - inset.x - uv.x) / duv.x;
    t0 = max(t0, min(ta, tb));
    t1 = min(t1, max(ta, tb));
  }
  if (abs(duv.y) < 1.0e-12f) {
    if (uv.y < inset.y || uv.y > 1.0f - inset.y) {
      return -1.0f;
    }
  }
  else {
    const float ta = (inset.y - uv.y) / duv.y;
    const float tb = (1.0f - inset.y - uv.y) / duv.y;
    t0 = max(t0, min(ta, tb));
    t1 = min(t1, max(ta, tb));
  }
  if (t0 > 1.0e-6f && t0 <= t1) {
    return t0;
  }
  /* Distinguish the frontier contact from a genuine backwards miss: the former is a rounding
   * artefact of a previous snap onto the domain border, the latter is a correct early out. */
  return (t0 <= 1.0e-6f && t1 >= 0.0f) ? -2.0f : -3.0f;
}

/** Distance along the ray at which it leaves the cell of `level` that contains `uv`. */
float hf_cell_exit(HeightfieldData hf, float2 uv, float2 duv, int level)
{
  int lw = max(1, (hf.tex_info.x + (1 << level) - 1) >> level);
  int lh = max(1, (hf.tex_info.y + (1 << level) - 1) >> level);
  float2 cell = float2(1.0f / float(lw), 1.0f / float(lh));
  float2 cell_origin = floor(uv / cell) * cell;
  float2 next_uv = cell_origin + cell * step(float2(0.0f), duv);
  float tx = (abs(duv.x) > 1.0e-12f) ? (next_uv.x - uv.x) / duv.x : 1.0e38f;
  float ty = (abs(duv.y) > 1.0e-12f) ? (next_uv.y - uv.y) / duv.y : 1.0e38f;
  return min(tx, ty);
}

/** Miss code: `-1 - code`, so the caller can decode `int(-t - 1)`. */
float hf_miss(int code)
{
  return -1.0f - float(code);
}

/**
 * Traces `o + t * d` against the heightfield inside `[t_enter, t_exit]`.
 * Returns the ray parameter of the first surface crossing, or a negative value if there is none.
 *
 * Conservative interval march over the maximum / minimum mipmap pair:
 * - The ray is above the cell maximum for the whole visit: it cannot cross the surface inside the
 *   cell and the cell is skipped in one step (ascending one level when the parent is clear too).
 * - The ray is below the cell minimum for the whole visit: it is inside the solid, so the crossing
 *   already happened (used by the shadow / AO rays as an early occluded test).
 * - Otherwise the level is halved until a single texel remains; there the surface is a constant
 *   height, so the crossing is solved analytically instead of with a fixed binary search.
 *
 * `min_level` floors the hierarchy: shadow rays query a smoothed envelope so that the texel scale
 * staircase of the displacement does not self shadow into contour aligned bands.
 *
 * `accept_tangent` enables the nearest approach receiver. A camera ray that hugs a ridge makes the
 * conservative interval test fail in nearly every cell, so it crawls one texel per step and can
 * exhaust the budget while the surface is still just below it. That is a genuine crossing found by
 * the reference march, not a real miss, so the closest approach is accepted when it lies within the
 * vertical distance the ray travels over one texel. Shadow / AO rays must pass false: for them a
 * near miss is an unoccluded ray, not a hit.
 */
float hf_march(HeightfieldData hf,
               [[resource_table]] const HeightfieldBufs &bufs,
               float3 o,
               float3 d,
               float t_enter,
               float t_exit,
               int steps,
               float jitter,
               int min_level,
               bool accept_tangent)
{
  float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  float2 texel_uv = 1.0f / max(float2(hf.tex_info.x, hf.tex_info.y), float2(1.0f));
  float2 uv_inset = 0.5f * texel_uv;
  float2 duv = d.xy / ext;
  const float eps = hf.params.w;
  /* Cap the coarse level: level 8 spans the whole texture domain, large enough to skip entire
   * empty regions at grazing angles. */
  const int max_level = min(min(hf.tex_info.z - 1, HF_MAX_MIPS - 1), 8);

  /* One final visit exactly on the traversal boundary is allowed: a surface that lies on it (the
   * flat carrier plane at the bottom of the box) can only be detected there. Killing the ray as
   * soon as `t >= t_exit` dropped almost every low-height carrier pixel (see `hf_reference.march`,
   * which implements the same convention). */
  bool on_exit = false;

  /* Closest approach to the reconstructed level 0 surface (see `accept_tangent`). */
  float best_gap = 1.0e30f;
  float best_gap_t = -1.0f;

  /* Misses return a negative code instead of a plain -1 so the caller can report which give-up path
   * fired (diagnostic hook, `hf.shadow3.w`). Hits are always `t >= 0`. */
  float t = max(t_enter, 0.0f);
  if (jitter > 0.0f) {
    /* Sub texel phase offset of the first step. It is smaller than one texel, so it cannot skip a
     * crossing; TAA averages the resulting noise into a flat field. */
    const float lateral = length(d.xy);
    const float texel_world = min(texel_uv.x * ext.x, texel_uv.y * ext.y);
    if (lateral > 1.0e-6f) {
      t += jitter * texel_world / lateral;
    }
  }
  if (t >= t_exit) {
    return hf_miss(1);
  }

  /* Start at the level matching the number of texels the ray spans over its visible length. A
   * straight down ray starts at 0, a grazing ray at a coarse level: that is the adaptive part which
   * replaces a larger fixed step count. */
  float span_texels = length(duv) * t_exit * max(float(hf.tex_info.x), float(hf.tex_info.y));
  int level = clamp(int(floor(log2(max(span_texels, 1.0f)))), min_level, max_level);

  for (int i = 0; i < steps; i++) {
    float3 p = o + t * d;
    /* Traversal and the domain test use the carrier uv (border band included, the band is surface).
     * Every height fetch uses the domain uv, whose clamped indices repeat the border cell height
     * across the band. With `hf.shadow3.y == 0` both are identical. */
    float2 uv = hf_local_to_uv_wide(hf, p.xy);
    float2 uv_s = hf_local_to_uv(hf, p.xy);
    /* Tolerance of a few thousandths of a texel. The air-entry snap lands the ray exactly on the
     * inset frontier; reconstructing `p` from that `t` can leave the uv a rounding hair outside it,
     * which used to re-enter this branch and be reported as a miss (74% of all misses). Landing on
     * the frontier is an entry, not an absence of one. */
    const float2 uv_pad = float2(1.0e-5f);
    const bool inside = all(greaterThanEqual(uv, uv_inset - uv_pad)) &&
                        all(lessThanEqual(uv, float2(1.0f) - uv_inset + uv_pad));

    if (!inside) {
      /* The heightmap is a thin sheet, not a solid block: outside the domain the ray keeps its
       * height and only advances to the point where it enters the inset domain. */
      const float t_in = hf_air_entry(uv, duv, uv_inset);
      if (t_in < 0.0f) {
        return hf_miss(8 + int(-t_in - 1.0f));
      }
      if (t_in >= t_exit - t) {
        /* The ray reaches the carrier plane before it enters the inset domain: it lands in the outer
         * half texel strip of the carrier, where the height field repeats its border cell. With the
         * border taper that strip is flat on the plane, so the plane itself is the surface and this
         * is a hit, not an open border. Only do this when the traversal really ends on the carrier
         * plane (`t_exit` clamped to 1); ending on a lateral box exit is genuine background. */
        if (accept_tangent && t_exit >= 1.0f - 1.0e-3f) {
          /* Keep it below the caller's `best_t` (1.0) or the hit is rejected as behind the carrier. */
          return min(t_exit, 1.0f - 1.0e-6f);
        }
        return hf_miss(7);
      }
      t += t_in;
      continue;
    }

    int l = clamp(level, min_level, max_level);
    bool advanced = false;
    for (int k = 0; k < 24; k++) {
      const float t_cell = max(hf_cell_exit(hf, uv, duv, l), 1.0e-6f);
      const float z_exit = p.z + t_cell * d.z;
      const float hz_max = hf_height_to_z(hf, hf_fetch_level(hf, bufs, uv_s, l));

      /* Nearest approach for the receiver. Level 0 uses the per texel height (no filtering that would
       * hide a crest). Above it, the receiver records the *envelope* touch instead of the filtered
       * surface: a grazing ray that stays at levels 1..2 touches a coarse envelope whose crest lies
       * somewhere inside the cell, and that is exactly the silhouette the reference march fills. The
       * over estimate is bounded by the cell height span (a few texels), which is the same order as
       * the sub texel silhouette error. */
      if (accept_tangent && l <= 2) {
        const float gap = ((l == 0) ? min(p.z, z_exit) : p.z) - hz_max;
        if (gap < best_gap) {
          best_gap = gap;
          best_gap_t = t;
        }
      }

      /* Safe space: above every height of the cell during the whole visit. */
      if (min(p.z, z_exit) > hz_max + eps) {
        if (l < max_level) {
          const float t_up = max(hf_cell_exit(hf, uv, duv, l + 1), 1.0e-6f);
          const float z_up = p.z + t_up * d.z;
          const float hz_up = hf_height_to_z(hf, hf_fetch_level(hf, bufs, uv_s, l + 1));
          if (min(p.z, z_up) > hz_up + eps) {
            l++;
            continue;
          }
        }
        level = l;
        t = min(t + t_cell, t_exit);
        advanced = true;
        break;
      }

      /* Conservative "inside the solid": below the minimum height of the whole cell. The coarse
       * envelope is wider than the true surface, so do not report an occluder straight away: descend
       * and refine until the envelope is tight, otherwise the discrete mip borders of the envelope
       * print as contour aligned bands. */
      const float hz_min = hf_height_to_z(hf, hf_fetch_level_min(hf, bufs, uv_s, l));
      if (max(p.z, z_exit) < hz_min - eps) {
        if (l > min_level) {
          l--;
          continue;
        }
        return t;
      }

      if (l > min_level) {
        /* Descending ray dipping into this cell's envelope: jump straight to the point where it
         * reaches that envelope. Keep going in the same outer step instead of breaking: the jump
         * lands just above a coarse envelope, and spending one outer step per level is what left
         * grazing rays stuck at levels 1..4 when the budget ran out (they never sampled level 0,
         * so the nearest approach receiver had nothing to record). */
        const float t_height = (d.z < -1.0e-12f) ? (hz_max + eps - p.z) / d.z : -1.0f;
        if (t_height > 0.0f && t_height < t_cell) {
          t += t_height;
          l--;
          level = l;
          advanced = true;
          p = o + t * d;
          uv = hf_local_to_uv_wide(hf, p.xy);
          uv_s = hf_local_to_uv(hf, p.xy);
          continue;
        }
        l--;
        continue;
      }

      /* Floor of the hierarchy. The crossing is solved analytically against the filtered surface,
       * not the per texel constant of the envelope, so the reconstructed hit height is continuous
       * across texel borders. */
      const float hz_surf = hf_fetch_surface(hf, bufs, uv_s, l);
      const float hz_surf_z = hf_height_to_z(hf, hz_surf);
      if (p.z <= hz_surf_z + eps) {
        return t;
      }
      if (z_exit <= hz_surf_z + eps) {
        const float t_hit = t + (hz_surf_z + eps - p.z) / d.z;
        return clamp(t_hit, t, t + t_cell);
      }
      level = l;
      t = min(t + t_cell, t_exit);
      advanced = true;
      break;
    }
    if (!advanced) {
      /* The level search did not settle: report a miss instead of a wrong hit. */
      return hf_miss(3);
    }
    if (t >= t_exit) {
      if (on_exit) {
        return hf_miss(4);
      }
      on_exit = true;
    }
  }
  /* Nearest approach receiver: a ray that ran out of budget (or reached the traversal end) while
   * grazing a ridge still crossed the surface if its closest sampled approach is within the
   * vertical drop of one texel. Accept that point instead of erasing the pixel.
   *
   * The tolerance has two terms: `2 * eps` covers the height query offset, and `|d.z| * t_texel`
   * covers the height drop the ray sweeps while it travels one texel laterally. A grazing ray
   * crosses the true silhouette between two samples, so one texel of lateral travel is exactly the
   * scale at which the sampled nearest approach can miss the crossing. */
  if (accept_tangent && best_gap_t >= 0.0f) {
    const float lateral = length(d.xy);
    const float texel_world = min(texel_uv.x * ext.x, texel_uv.y * ext.y);
    const float t_texel = (lateral > 1.0e-6f) ? (texel_world / lateral) : 0.0f;
    if (best_gap <= 2.0f * eps + abs(d.z) * t_texel) {
      return best_gap_t;
    }
  }
  /* Diagnostic split of the budget exhaustion: 11 = the ray reached the traversal end without a
   * crossing, 12 = it ran out of steps short of it (the march is not advancing enough). */
  return hf_miss((t >= t_exit) ? 11 : 12);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Shading helpers
 * \{ */

/* Catmull-Rom basis (a = -0.5) for taps ordered [-1, 0, 1, 2]. */
float4 hf_cr_basis(float f)
{
  const float f2 = f * f;
  const float f3 = f2 * f;
  return float4(-0.5f * f3 + f2 - 0.5f * f,
                1.5f * f3 - 2.5f * f2 + 1.0f,
                -1.5f * f3 + 2.0f * f2 + 0.5f,
                0.5f * f3 - 0.5f * f2);
}

float4 hf_cr_basis_derivative(float f)
{
  const float f2 = f * f;
  return float4(-1.5f * f2 + 2.0f * f - 0.5f,
                4.5f * f2 - 5.0f * f,
                -4.5f * f2 + 4.0f * f + 0.5f,
                1.5f * f2 - f);
}

/**
 * Bicubic (Catmull-Rom) reconstruction of the level 0 surface and its analytic gradient.
 * Returns `(height, dheight/du, dheight/dv)` in normalised height per unit uv.
 *
 * Bilinear reconstruction is only C0: its gradient is piecewise constant, so the shading normal
 * jumps at every texel border. At grazing angles those jumps project into long stripes, which is
 * the "curtain" staircase reported on the walls. Catmull-Rom is C1 and costs 16 taps, no extra
 * march step.
 */
float3 hf_bicubic(HeightfieldData hf,
                  [[resource_table]] const HeightfieldBufs &bufs,
                  float2 uv)
{
  const int lw = max(1, hf.tex_info.x);
  const int lh = max(1, hf.tex_info.y);
  const float2 coord = uv * float2(lw, lh) - 0.5f;
  const float2 base = floor(coord);
  const float2 f = coord - base;
  const float4 wx = hf_cr_basis(f.x);
  const float4 dwx = hf_cr_basis_derivative(f.x);
  const float4 wy = hf_cr_basis(f.y);
  const float4 dwy = hf_cr_basis_derivative(f.y);
  const float2 inv = float2(1.0f / float(lw), 1.0f / float(lh));
  float h = 0.0f;
  float dhdx = 0.0f;
  float dhdy = 0.0f;
  for (int j = 0; j < 4; j++) {
    const float wj = (j == 0) ? wy.x : ((j == 1) ? wy.y : ((j == 2) ? wy.z : wy.w));
    const float dwj = (j == 0) ? dwy.x : ((j == 1) ? dwy.y : ((j == 2) ? dwy.z : dwy.w));
    for (int i = 0; i < 4; i++) {
      const float wi = (i == 0) ? wx.x : ((i == 1) ? wx.y : ((i == 2) ? wx.z : wx.w));
      const float dwi = (i == 0) ? dwx.x : ((i == 1) ? dwx.y : ((i == 2) ? dwx.z : dwx.w));
      const float2 tap = float2(base.x + float(i) - 1.0f, base.y + float(j) - 1.0f);
      const float hv = hf_fetch_level(hf, bufs, (tap + 0.5f) * inv, 0);
      h += wj * wi * hv;
      dhdx += wj * dwi * hv;
      dhdy += dwj * wi * hv;
    }
  }
  return float3(h, dhdx, dhdy);
}

float3 hf_analytic_normal(HeightfieldData hf,
                          [[resource_table]] const HeightfieldBufs &bufs,
                          float2 uv,
                          int level,
                          float footprint)
{
  float2 texel = 1.0f / max(float2(hf.tex_info.x, hf.tex_info.y), float2(1.0f));
  /* Gradients are per unit uv of the heightmap domain (inside the clamped border band). */
  float2 ext = hf_domain_ext(hf);
  float range = hf.params.y - hf.params.x;
  float2 dh = float2(0.0f);
  if (level == 0) {
    /* `footprint` is the texel coverage of one pixel.
     * - Below one texel the surface is magnified: the bilinear gradient is constant per texel, so
     *   its jumps line up with the texel grid and read as a staircase. The C1 bicubic
     *   reconstruction removes those jumps without adding a march step.
     * - Above one texel the surface is minified: widen the stencil to a box of the pixel footprint,
     *   which filters the fine height detail instead of aliasing it into hatch bands.
     *
     * The two estimates disagree slightly even at the crossover, so a hard switch at
     * `footprint == 1` turns that disagreement into a visible seam along the iso-footprint curve.
     * Crossfade over 0.75 .. 1.5 texels instead; each side is only evaluated when its weight is
     * non zero. */
    const float blend = clamp((footprint - 0.75f) / 0.75f, 0.0f, 1.0f);
    if (blend < 1.0f) {
      const float3 g = hf_bicubic(hf, bufs, uv);
      /* `g.yz` is the gradient per *texel*, while the box branch below returns it per uv. Convert
       * with the texel count (one uv unit spans `tex_info` texels): a slope in world units is
       * `dH/dtexel * range / (ext / texels)`. Missing that factor made this branch 1/256 of the
       * other one, and the hard switch between two estimates of different scale printed
       * iso-footprint bands across the whole sheet (the concentric stripes in the form mode
       * render). */
      const float2 grad = g.yz * range * float2(max(float(hf.tex_info.x), 1.0f),
                                                max(float(hf.tex_info.y), 1.0f)) /
                          ext;
      dh = grad * (1.0f - blend);
    }
    if (blend > 0.0f) {
      const float2 step_uv = texel * clamp(footprint, 1.0f, 8.0f);
      float h_l = hf_fetch_bilinear(hf, bufs, uv - float2(step_uv.x, 0.0f), 0);
      float h_r = hf_fetch_bilinear(hf, bufs, uv + float2(step_uv.x, 0.0f), 0);
      float h_d = hf_fetch_bilinear(hf, bufs, uv - float2(0.0f, step_uv.y), 0);
      float h_u = hf_fetch_bilinear(hf, bufs, uv + float2(0.0f, step_uv.y), 0);
      dh += float2((h_r - h_l) / (2.0f * step_uv.x) * range / ext.x,
                   (h_u - h_d) / (2.0f * step_uv.y) * range / ext.y) *
            blend;
    }
  }
  else {
    float2 step_uv = texel * float(1 << level);
    float h_l = hf_fetch_surface(hf, bufs, uv - float2(step_uv.x, 0.0f), level);
    float h_r = hf_fetch_surface(hf, bufs, uv + float2(step_uv.x, 0.0f), level);
    float h_d = hf_fetch_surface(hf, bufs, uv - float2(0.0f, step_uv.y), level);
    float h_u = hf_fetch_surface(hf, bufs, uv + float2(0.0f, step_uv.y), level);
    dh = float2((h_r - h_l) / (2.0f * step_uv.x) * range / ext.x,
                (h_u - h_d) / (2.0f * step_uv.y) * range / ext.y);
  }

  float3 n_local = normalize(float3(-dh.x * hf.params.z, -dh.y * hf.params.z, 1.0f));
  return normalize((hf.local_to_world * float4(n_local, 0.0f)).xyz);
}

float hf_shadow(HeightfieldData hf,
                [[resource_table]] const HeightfieldBufs &bufs,
                float3 p_local,
                float3 n_world,
                float3 l_local,
                float3 bmin,
                float3 bmax,
                int steps,
                int min_level,
                float2 rng)
{
  if (hf.shading.x <= 0.0f) {
    return 1.0f;
  }
  /* One deterministic ray for the sphere traced path: the penumbra comes from the analytic sun cone
   * width inside the trace, so a second jittered ray only adds binary noise. The interval march
   * fallback still uses two jittered rays. */
  const int ray_count = (hf.sdf.w > 0.5f) ? 1 : 2;
  const float2 ext = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
  const float2 texel_uv = 1.0f / max(float2(hf.tex_info.x, hf.tex_info.y), float2(1.0f));
  const float texel_world = min(texel_uv.x * ext.x, texel_uv.y * ext.y);
  /* A finite trace distance keeps far, undersampled rays from turning into false dark bands, and
   * costs nothing when the ray already left the box. */
  const float max_dist = (hf.shadow2.x > 0.0f) ? hf.shadow2.x * texel_world : 1.0e38f;
  /* Occluders fade out with distance (cheap PCSS stand in): the penumbra of a distant blocker is
   * wider than one texel, so the shadow is weighted instead of returned as a hard zero. */
  const float soft_dist = hf.shadow2.y * texel_world;
  const float jitter_scale = hf.shadow2.z * texel_world;
  float3 up = (abs(l_local.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
  float3 tx = normalize(cross(l_local, up));
  float3 ty = cross(l_local, tx);
  float occluded = 0.0f;
  /* Tangent of the sun half angle. Two rays cannot sample a cone that is much wider than the sun
   * disc; the previous code used the AO radius (0.25, ~10x the 3 degree disc) as the cone radius,
   * which turns the shadow into binary noise instead of a penumbra. */
  const float cone = max(hf.shadow3.x, 1.0e-4f);
  for (int i = 0; i < ray_count; i++) {
    const bool sdf_mode = hf.sdf.w > 0.5f;
    float radius = (sdf_mode) ? 0.0f : cone * sqrt(fract(rng.x + float(i) * 0.5f));
    float phi = 6.28318530718f * fract(rng.y + float(i) * 0.61803398875f);
    float3 dir = normalize(l_local + (tx * cos(phi) + ty * sin(phi)) * radius);
    /* Bias the origin along the shading normal as well as along the light. A pure light direction
     * bias of several texels displaces the sample sideways, so the shoal of a slope flips in and
     * out of the surface pixel by pixel and prints a dot lattice along the iso-slope contours at the
     * terminator. The normal bias moves the sample off the surface without walking into the slope. */
    const float bias = 4.0f * texel_world;
    const float3 n_local = normalize((hf.world_to_local * float4(n_world, 0.0f)).xyz);
    float t_start = (sdf_mode) ? 0.0f :
                    jitter_scale * fract(rng.x * 7.539f + rng.y * 3.117f + float(i) * 0.618f);
    const float3 o = p_local + dir * (t_start + bias) + n_local * bias;
    float t0, t1;
    if (!hf_ray_box(o, dir, bmin, bmax, t0, t1)) {
      continue;
    }
    t1 = min(t1, max_dist);
    if (t1 <= t0) {
      continue;
    }
    if (sdf_mode) {
      occluded += hf_sphere_trace(hf, bufs, o, dir, t0, t1, int(hf.sdf.z));
    }
    else {
      const float t_occ = hf_march(hf, bufs, o, dir, t0, t1, steps, 0.0f, min_level, false);
      if (t_occ >= 0.0f) {
        const float w = (soft_dist > 0.0f) ? clamp(1.0f - t_occ / soft_dist, 0.0f, 1.0f) : 1.0f;
        occluded += w;
      }
    }
  }
  return 1.0f - hf.shading.x * (occluded / float(ray_count));
}

float hf_occlusion(HeightfieldData hf,
                   [[resource_table]] const HeightfieldBufs &bufs,
                   float3 p_local,
                   float3 n_local,
                   float3 bmin,
                   float3 bmax,
                   int rays,
                   float2 rng)
{
  if (hf.shading.y <= 0.0f || rays <= 0) {
    return 1.0f;
  }
  float radius = max(hf.shading.z, 1.0e-4f);
  float3 t = normalize(cross(n_local, abs(n_local.z) < 0.9f ? float3(0, 0, 1) : float3(1, 0, 0)));
  float3 b = cross(n_local, t);
  float occluded = 0.0f;
  const float3 o = p_local + n_local * (8.0f * hf.params.w);
  for (int i = 0; i < rays; i++) {
    float a = (float(i) + rng.x) / float(rays);
    float b01 = fract(float(i) * 0.61803398875f + rng.y);
    float cos_theta = sqrt(1.0f - a);
    float sin_theta = sqrt(a);
    float phi = 6.28318530718f * b01;
    float3 dir = normalize(t * (cos(phi) * sin_theta) + b * (sin(phi) * sin_theta) +
                           n_local * cos_theta);
    float t0, t1;
    if (!hf_ray_box(o, dir, bmin, bmax, t0, t1)) {
      continue;
    }
    t1 = min(t1, radius);
    if (t1 <= t0) {
      continue;
    }
    if (hf_march(hf, bufs, o, dir, t0, t1, max(4, rays * 2), 0.0f, 0, false) >= 0.0f) {
      occluded += 1.0f;
    }
  }
  return 1.0f - hf.shading.y * (occluded / float(rays));
}

/** \} */

[[fragment]]
void trace_frag([[resource_table]] const HeightfieldBufs &bufs,
                [[resource_table]] const HeightfieldPush &pc,
                [[resource_table]] const Sampling &sampl,
                [[resource_table]] const HiZ &hiz,
                [[frag_coord]] const float4 frag_co,
                [[frag_depth(any)]] float &frag_depth,
                [[in]] const VertOut v_out,
                [[out]] FragOut &frag_out)
{
  const int2 texel = int2(frag_co.xy);
  const float base_depth = texelFetch(hiz.hiz_tx, texel, 0).r;

  /* Default: leave the depth and the G-Buffer untouched. */
  frag_out.geom = float4(0.0f, 0.0f, 0.0f, 0.0f);
  frag_out.shade = float4(1.0f, 1.0f, 0.0f, 0.0f);
  frag_depth = 1.0f - base_depth;
  /* Diagnostic (`hf.shadow3.w`): surface the raw HiZ prepass depth of every pixel. */
  const bool dbg_miss = (pc.instance_count > 0) && (bufs.instances[0].shadow3.w > 0.0f);
  if (dbg_miss) {
    frag_out.geom = float4(base_depth, -9.0f, 0.0f, 0.0f);
  }
  if (base_depth >= 0.999999f) {
    return;
  }

  const float2 ndc = v_out.screen_uv * 2.0f - 1.0f;
  const float4 near_h = pc.clip_to_world * float4(ndc, -1.0f, 1.0f);
  const float3 near_p = near_h.xyz / near_h.w;
  const float3 ray_o = (pc.camera.w > 0.5f) ? pc.camera.xyz : near_p;
  const float4 base_h = pc.clip_to_world * float4(ndc, base_depth * 2.0f - 1.0f, 1.0f);
  const float3 base_p = base_h.xyz / base_h.w;
  const float3 ray_d = base_p - ray_o;

  float best_t = 1.0f;
  float3 best_normal = float3(0.0f, 0.0f, 1.0f);
  float best_shadow = 1.0f;
  float best_ao = 1.0f;
  float best_height = 0.0f;
  bool hit = false;
  /* Diagnostic only: last `hf_march` miss code, surfaced through the normal target when
   * `hf.shadow3.w > 0`. */
  float miss_code = 0.0f;
  bool debug_reason = false;

  const float2 rng = sampl.rng_2D_get(SAMPLING_GBUFFER_U);

  /* True when the fragment shows the flat carrier raster of an enabled instance. If the trace then
   * misses the displaced surface, the pixel is not covered by the sheet at all (see below). */
  bool carrier = false;
  bool thin = false;
  /* Diagnostic: local z residual of the prepass point (see `hf.shadow3.w`). */
  float debug_resid = 0.0f;
  /* Diagnostic: which path left the depth untouched (0 = none). 1 = not a carrier pixel,
   * 2 = ray box reject, 3 = march returned a t rejected by `t_hit >= best_t`, 4 + k = `hf_miss(k)`. */
  float miss_path = 0.0f;
  int debug_code = 0;

  for (int i = 0; i < pc.instance_count; i++) {
    HeightfieldData hf = bufs.instances[i];
    if ((hf.quality.z & 1) == 0) {
      continue;
    }

    const float3 base_local = (hf.world_to_local * float4(base_p, 1.0f)).xyz;
    const float base_resid = base_local.z - hf.bounds_min.z;
    debug_resid = base_resid;
    debug_reason = (hf.shadow3.w > 0.0f);
    /* Only rebuild pixels that actually show the carrier plane. Occluders in front (or objects
     * behind) are left untouched. */
    if (abs(base_resid) > hf.params.w) {
      /* Diagnostic: selector 1 marks every pixel this instance did not claim as carrier. */
      if (hf.shadow3.w >= 0.5f && hf.shadow3.w < 1.5f) {
        frag_depth = 0.0f;
      }
      continue;
    }
    carrier = true;
    thin = (hf.shadow3.z > 0.0f);
    debug_code = int(hf.shadow3.w);

    const float3 o = (hf.world_to_local * float4(ray_o, 1.0f)).xyz;
    const float3 d = (hf.world_to_local * float4(ray_d, 0.0f)).xyz;
    /* Silhouette closing (plan A): widen the lateral domain by `extras.y` texels. `hf_fetch_level`
     * clamps the UV, so the widened band repeats the edge cell height instead of exposing the
     * vertical cut of the carrier rectangle at grazing angles. */
    const float2 ext_xy = max(hf.bounds_max.xy - hf.bounds_min.xy, float2(1.0e-6f));
    const float2 texel_xy = ext_xy /
                            max(float2(float(hf.tex_info.x), float(hf.tex_info.y)),
                                float2(1.0f));
    const float2 margin = texel_xy * hf.extras.y;
    const float3 bmin = float3(hf.bounds_min.xy - margin, hf_height_to_z(hf, 0.0f));
    const float3 bmax = float3(hf.bounds_max.xy + margin, hf_height_to_z(hf, 1.0f));

    float t0, t1;
    if (!hf_ray_box(o, d, bmin, bmax, t0, t1)) {
      /* Diagnostic: selector 2 marks the ray box reject. */
      if (hf.shadow3.w >= 1.5f && hf.shadow3.w < 2.5f) {
        frag_depth = 0.0f;
      }
      continue;
    }
    t1 = min(t1, 1.0f + 1.0e-3f);
    if (t1 <= t0) {
      if (hf.shadow3.w >= 1.5f && hf.shadow3.w < 2.5f) {
        frag_depth = 0.0f;
      }
      continue;
    }

    const int steps = max(hf.quality.x, 1);
    float t_hit = hf_march(hf, bufs, o, d, t0, t1, steps, hf.extras.w * rng.x, 0, true);
    if (t_hit < 0.0f) {
      miss_code = t_hit;
      miss_path = 4.0f + (-t_hit - 1.0f);
    }
    else {
      miss_path = 3.0f;
    }
    /* Sub texel refinement of the marched hit (bits 8..15 of the flags). Re-solves the crossing
     * against the filtered surface at the hit position instead of the per texel linearisation, which
     * removes the staircase of grazing walls without any extra march step. */
    if (t_hit >= 0.0f) {
      const int refine = (hf.quality.z >> 8) & 0xff;
      if (refine > 0) {
        t_hit = hf_refine_hit(hf, bufs, o, d, t_hit, t1, refine);
      }
    }

    /* Screen space skirt (plan A continuation): a ray that leaves the lateral domain without
     * crossing the top surface would otherwise keep the flat carrier depth and expose the open
     * border. If it enters the side wall of the heightfield (its height at the boundary is below
     * the edge cell height), project that boundary point instead. This makes the silhouette follow
     * the displaced edge height at any view angle without adding geometry. */
    if (t_hit < 0.0f && hf.extras.y > 0.0f) {
      const float2 duv = d.xy / ext_xy;
      const float2 uv_o = hf_local_to_uv(hf, o.xy);
      float t_bx = 1.0e38f;
      float t_by = 1.0e38f;
      if (duv.x > 1.0e-12f) {
        t_bx = (1.0f - uv_o.x) / duv.x;
      }
      else if (duv.x < -1.0e-12f) {
        t_bx = (0.0f - uv_o.x) / duv.x;
      }
      if (duv.y > 1.0e-12f) {
        t_by = (1.0f - uv_o.y) / duv.y;
      }
      else if (duv.y < -1.0e-12f) {
        t_by = (0.0f - uv_o.y) / duv.y;
      }
      const float t_b = min(t_bx, t_by);
      if (t_b >= t0 - 1.0e-4f && t_b <= t1) {
        const float3 p_b = o + t_b * d;
        const float2 uv_b = clamp(hf_local_to_uv(hf, p_b.xy), float2(0.0f), float2(1.0f));
        if (p_b.z <= hf_height_to_z(hf, hf_fetch_height(hf, bufs, uv_b)) + hf.params.w) {
          t_hit = t_b;
        }
      }
    }

    if (t_hit < 0.0f || t_hit >= best_t) {
      continue;
    }

    const float3 p_local = o + t_hit * d;
    const float2 uv = hf_local_to_uv(hf, p_local.xy);
    const float2 uv_wide = hf_local_to_uv_wide(hf, p_local.xy);
    /* Pixel footprint in texels, drives the adaptive surface reconstruction of the normal. */
    const float2 n_texels = max(float2(hf.tex_info.x, hf.tex_info.y), float2(1.0f));
    const float footprint = max(abs(dFdx(uv.x)) * n_texels.x, abs(dFdy(uv.y)) * n_texels.y);
    const float3 n_world = hf_analytic_normal(hf, bufs, uv, 0, footprint);
    /* Near tangent views: one screen pixel covers many texels, so the level 0 finite difference
     * aliases into stripes. Blend to a coarse gradient above the grazing threshold; the blend is
     * continuous (no hard path switch, so no discontinuity band). The real skirt geometry is
     * shaded by the regular pipeline and keeps its true normals. */
    float3 n_shaded = n_world;
    if (hf.extras.z > 0.0f && hf.quality.w > 0) {
      const float sin_el = abs(d.z) / max(length(d), 1.0e-6f);
      const float w_grazing = clamp(
          (hf.extras.z - sin_el) / max(hf.extras.z * 0.5f, 1.0e-6f), 0.0f, 1.0f);
      if (w_grazing > 0.0f) {
        const float3 n_coarse = hf_analytic_normal(hf, bufs, uv, hf.quality.w, footprint);
        n_shaded = normalize(n_world * (1.0f - w_grazing) + n_coarse * w_grazing);
      }
    }
    const float3 n_local = normalize((hf.world_to_local * float4(n_shaded, 0.0f)).xyz);

    float shadow = 1.0f;
    if (pc.sun.w > 0.5f) {
      const float3 l_local = normalize((hf.world_to_local * float4(pc.sun.xyz, 0.0f)).xyz);
      shadow = hf_shadow(hf, bufs, p_local, n_shaded, l_local, bmin, bmax, max(8, steps),
                         hf.quality.y, rng);
    }
    float ao = hf_occlusion(hf, bufs, p_local, n_local, bmin, bmax, 3, rng);

    /* The skirt is a zero thickness sheet and cannot be a VSM caster without acne, so the wall
     * casts no real shadow. Fake the missing contact shadow as a darkened band just inside the
     * border on the side the light comes from: the wall a light ray crosses at that border would
     * shade exactly this strip. */
    if (pc.sun.w > 0.5f && hf.shadow2.w > 0.0f) {
      const float3 l_local = normalize((hf.world_to_local * float4(pc.sun.xyz, 0.0f)).xyz);
      const float2 ln = normalize(l_local.xy + float2(1.0e-9f));
      const float2 n_texels = max(float2(hf.tex_info.x, hf.tex_info.y), float2(1.0f));
      const float band = 12.0f;
      const float2 d_lo = uv_wide * n_texels;
      const float2 d_hi = (float2(1.0f) - uv_wide) * n_texels;
      float edge = clamp(1.0f - d_lo.y / band, 0.0f, 1.0f) * max(ln.y, 0.0f) +
                   clamp(1.0f - d_hi.y / band, 0.0f, 1.0f) * max(-ln.y, 0.0f) +
                   clamp(1.0f - d_lo.x / band, 0.0f, 1.0f) * max(ln.x, 0.0f) +
                   clamp(1.0f - d_hi.x / band, 0.0f, 1.0f) * max(-ln.x, 0.0f);
      ao *= 1.0f - hf.shadow2.w * min(edge, 1.0f);
    }

    /* SPOM style pixel level self occlusion / contact darkening. The coarse maximum mip levels hold
     * the highest sample of the surrounding 4x4 and 16x16 texel blocks: a hit clearly below that
     * maximum sits inside a crevice or at the foot of a step and is darkened locally. Flat and
     * convex areas have no deficit and stay untouched, so this does not dim the whole surface. */
    if (hf.extras.x > 0.0f) {
      const float h_center = hf_fetch_height(hf, bufs, uv);
      const float h_near = hf_fetch_level(hf, bufs, uv, min(2, hf.tex_info.z - 1));
      const float h_far = hf_fetch_level(hf, bufs, uv, min(4, hf.tex_info.z - 1));
      const float range = max(hf.params.y - hf.params.x, 1.0e-4f);
      const float deficit = (0.35f * max(h_near - h_center, 0.0f) +
                             0.65f * max(h_far - h_center, 0.0f)) /
                            range;
      /* The threshold keeps the shallow rolling hills of the reference untouched and reserves the
       * darkening for actual depressions. */
      const float cavity = clamp((deficit - 0.05f) * hf.extras.x, 0.0f, 0.9f);
      ao *= 1.0f - cavity;
    }

    best_t = t_hit;
    best_normal = n_shaded;
    best_shadow = shadow;
    best_ao = ao;
    best_height = uv.y;
    hit = true;
  }

  if (!hit) {
    /* Diagnostic: selector `hf.shadow3.w` erases the depth of this particular miss path only, so
     * the Z pass alone can be used to count each path. */
    if (debug_code > 0 && int(miss_path) == debug_code) {
      frag_depth = 0.0f;
      return;
    }
    /* Thin sheet coverage (`hf.shadow3.z`): the carrier plane is only the rasterization and shading
     * proxy of the displaced sheet, not a solid surface. A pixel that shows the carrier but whose
     * ray never crosses the sheet is the open border seen past the displaced edge. Keeping the flat
     * carrier depth there is what reads as a visible side wall / extruded slab, because the flat
     * rest pose fills the strip under the silhouette. Treat it as empty instead: clear the depth to
     * the far value so the world background pass (depth equal at the clear value) takes the pixel,
     * and drop the normal / shading auxiliary so no surface is reconstructed from it. */
    if (carrier && thin) {
      /* The world background is a fullscreen quad at the depth buffer clear value, drawn with
       * depth-equal after the combine. Write that value here (reverse-z: the cleared depth is the
       * maximum) so the background pass takes the pixel instead of leaving the flat carrier color. */
      frag_depth = 0.0f;
      frag_out.geom = float4(0.0f, 0.0f, 1.0f, 0.0f);
      frag_out.shade = float4(1.0f, 1.0f, 1.0f, 0.0f);
    }
    return;
  }

  const float3 p_world = ray_o + best_t * ray_d;
  float4 clip = pc.world_to_clip * float4(p_world, 1.0f);
  float screen_depth = (clip.z / clip.w) * 0.5f + 0.5f;

  /* The pass runs with `DRW_STATE_DEPTH_ALWAYS` because the thin sheet erase has to write the far
   * value, which the previous `DEPTH_GREATER` test rejected. That test also used to protect pixels
   * whose G-Buffer depth is real geometry in front of the carrier (the skirt wall): a traced hit
   * behind it was dropped. Reproduce that here: only carrier fragments own the depth, everything
   * else keeps the prepass value. */
  frag_depth = carrier ? (1.0f - screen_depth + 1.0e-6f) : (1.0f - base_depth);

  if (best_height < 0.0f) {
    best_height = 0.0f;
  }
  frag_out.geom = float4(best_normal, 1.0f);
  frag_out.shade = float4(best_shadow, best_ao, best_t, best_height);
}

}  // namespace eevee::heightfield

PipelineGraphic eevee_heightfield_trace(eevee::heightfield::fullscreen_vert,
                                        eevee::heightfield::trace_frag);
