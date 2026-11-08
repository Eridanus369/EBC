/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * Heightfield displacement screen space result, shared by the deferred lighting and combine passes.
 *
 * `geom.xyz` is the analytic normal (world space) of the displaced surface at the ray marched hit
 * point, `geom.w` is the coverage mask (1 = the pixel owns a heightfield hit). `shade.x` is the
 * ray marched micro shadow factor, `shade.y` the ambient occlusion factor.
 *
 * The struct lives in a `.bsl.hh` file so that the shader tool generates the matching create info
 * that the resource table declarations need. Its accessors are methods because the shader tool only
 * rewrites the sampler member accesses of methods, not of free functions.
 */

#pragma once

#include "gpu_shader_compat.hh"

/* Must match `eevee_heightfield_shared.hh` (kept in sync there because the generated create info
 * does not include that header). */
#ifndef HF_GEOM_TEX_SLOT
#  define HF_GEOM_TEX_SLOT 11
#endif
#ifndef HF_SHADE_TEX_SLOT
#  define HF_SHADE_TEX_SLOT 12
#endif

namespace eevee {

struct HeightfieldSample {
  [[sampler(HF_GEOM_TEX_SLOT)]] sampler2D geom_tx;
  [[sampler(HF_SHADE_TEX_SLOT)]] sampler2D shade_tx;

  /** True if this pixel owns a heightfield hit. */
  bool is_valid(int2 texel)
  {
    return texelFetch(geom_tx, texel, 0).a > 0.5f;
  }

  /** Returns the analytic normal of the displaced surface, or the fallback normal. */
  float3 override_normal(int2 texel, float3 N)
  {
    float4 geom = texelFetch(geom_tx, texel, 0);
    return (geom.a > 0.5f) ? geom.xyz : N;
  }

  /** Returns shadow * ao for the displaced surface, or 1 for untouched pixels. */
  float shade_factor(int2 texel)
  {
    if (!this->is_valid(texel)) {
      return 1.0f;
    }
    float4 shade = texelFetch(shade_tx, texel, 0);
    return clamp(shade.x * shade.y, 0.0f, 1.0f);
  }
};

}  // namespace eevee
