/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "infos/eevee_common_infos.hh"

#include "eevee_pipeline.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_math_vector_reduce.bsl.hh"

packed_float3 g_emission;
/* Scoped by the generated input-branch function, not shared with sibling shaders. */
float4 g_npr_diffusion = float4(0.0f);
float g_npr_diffusion_weight = 1.0f;
bool g_npr_diffusion_scope = false;
/* Art-directed radiance is not emission and must not enter the surfel emission capture. */
packed_float3 g_npr_rim;
packed_float3 g_transmittance;
float g_holdout;

packed_float3 g_volume_scattering;
float g_volume_anisotropy;
packed_float3 g_volume_absorption;

/* For Light Accumulation. */
#if (defined(GPU_FRAGMENT_SHADER) && defined(SRT_CONSTANT_use_lighting_nodes)) || \
    defined(GLSL_CPP_STUBS)
packed_float3 g_diffuse_light;
packed_float3 g_diffuse_color;
packed_float3 g_glossy_light;
packed_float3 g_glossy_color;
packed_float3 g_transmission_light;
packed_float3 g_transmission_color;
int g_light_accumulation_count;
#endif

/* The Closure type is never used. Use float as dummy type. */
#define Closure float
#define CLOSURE_DEFAULT 0.0f

#ifdef GLSL_CPP_STUBS
#  define CLOSURE_BIN_COUNT 3
#endif

/* Maximum number of picked closure. */
#ifndef CLOSURE_BIN_COUNT
#  define CLOSURE_BIN_COUNT 1
#endif

template<typename T> struct Reservoir {
  T data;
  float rand;
  float chosen_weight;
  float total_weight;

  void reset(float rng)
  {
    this->rand = rng;
    this->chosen_weight = 0.0f;
    this->total_weight = 0.0f;
    data = T{};
  }

  void add(T candidate, float candidate_weight)
  {
    if (candidate_weight < 1e-5f) {
      return;
    }
    total_weight += candidate_weight;

    float x = candidate_weight / total_weight;
    bool chosen = (rand < x);

    if (chosen) {
      data = candidate;
      chosen_weight = candidate_weight;
    }
    /* Assuming that if r is in the interval [0,x] or [x,1], it's still uniformly distributed
     * within that interval, so remapping to [0,1] again to explore this space of probability. */
    rand = (chosen) ? (rand / x) : ((rand - x) / (1.0f - x));
  }

  float get_final_weight() const
  {
    if (chosen_weight <= 0.0f) {
      return 0.0f;
    }
    return total_weight / chosen_weight;
  }
};

template struct Reservoir<ClosureUndetermined>;

/* Sampled closure parameters. */
Reservoir<ClosureUndetermined> g_closure_bins[CLOSURE_BIN_COUNT];

Reservoir<ClosureUndetermined> g_closure_get(uchar i)
{
  switch (i) {
    case 0:
      return g_closure_bins[0];
#if CLOSURE_BIN_COUNT > 1
    case 1:
      return g_closure_bins[1];
#endif
#if CLOSURE_BIN_COUNT > 2
    case 2:
      return g_closure_bins[2];
#endif
  }
  /* Unreachable. */
  assert(false);
  return g_closure_bins[0];
}

ClosureUndetermined g_closure_get_resolved(uchar i, float additional_weight)
{
  Reservoir<ClosureUndetermined> r = g_closure_get(i);
  ClosureUndetermined cl = r.data;
  cl.color *= r.get_final_weight() * additional_weight;
  cl.npr.additive *= r.get_final_weight() * additional_weight;
  return cl;
}

ClosureType closure_type_get(ClosureDiffuse /*cl*/)
{
  return CLOSURE_BSDF_DIFFUSE_ID;
}

ClosureType closure_type_get(ClosureTranslucent /*cl*/)
{
  return CLOSURE_BSDF_TRANSLUCENT_ID;
}

ClosureType closure_type_get(ClosureReflection /*cl*/)
{
  return CLOSURE_BSDF_MICROFACET_GGX_REFLECTION_ID;
}

ClosureType closure_type_get(ClosureRefraction /*cl*/)
{
  return CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID;
}

ClosureType closure_type_get(ClosureSubsurface /*cl*/)
{
  return CLOSURE_BSSRDF_BURLEY_ID;
}

ClosureType closure_type_get(ClosureThinRefraction /*cl*/)
{
  return CLOSURE_BSDF_THIN_GLASS_TRANSMISSION_ID;
}

/**
 * Assign `candidate` to the reservoir based on a random value and the respective weights.
 * NPR surface diffusion metadata boosts the selection weight so that black source pixels still
 * receive neighboring radiance, while the stored color and additive terms are normalized by the
 * same factor to preserve the reservoir weighting semantics.
 */
void closure_select(Reservoir<ClosureUndetermined> &reservoir, ClosureUndetermined candidate)
{
  candidate.weight *= g_npr_diffusion_weight;
#if defined(MAT_NPR_SURFACE_DIFFUSION) && !defined(MAT_CAPTURE) && !defined(MAT_BAKE_COLOR)
  candidate.npr.diffusion = g_npr_diffusion;
#endif
  float candidate_color_weight = average(abs(candidate.color));
#if defined(MAT_NPR_SURFACE_DIFFUSION) && !defined(MAT_CAPTURE) && !defined(MAT_BAKE_COLOR)
  /* Black source pixels must still receive neighboring radiance. Do not discard their
   * diffusion metadata based only on this pixel's pre-diffusion energy. */
  if (candidate.npr.diffusion.w > 0.0f && reduce_max(candidate.npr.diffusion.xyz) > 0.0f) {
    candidate_color_weight = max(candidate_color_weight, 1.0f);
  }
#endif
#ifndef MAT_CAPTURE
  /* Surfel capture stores physical albedo, not view-dependent surface radiance. */
  if (candidate.npr.enabled) {
    candidate_color_weight = max(
        candidate_color_weight,
        average(abs(candidate.color * candidate.npr.multiplier + candidate.npr.additive)));
  }
#endif
  candidate.color /= candidate_color_weight;
  candidate.npr.additive /= candidate_color_weight;
  reservoir.add(candidate, candidate.weight * candidate_color_weight);
}

void closure_weights_reset(float closure_rand)
{
  g_closure_bins[0].reset(closure_rand);
  g_closure_bins[0].data.npr = closure_npr_direct_default();
#if CLOSURE_BIN_COUNT > 1
  g_closure_bins[1].reset(closure_rand);
  g_closure_bins[1].data.npr = closure_npr_direct_default();
#endif
#if CLOSURE_BIN_COUNT > 2
  g_closure_bins[2].reset(closure_rand);
  g_closure_bins[2].data.npr = closure_npr_direct_default();
#endif

  g_volume_scattering = float3(0.0f);
  g_volume_anisotropy = 0.0f;
  g_volume_absorption = float3(0.0f);

  g_emission = float3(0.0f);
  g_npr_rim = float3(0.0f);
  g_transmittance = float3(0.0f);
  g_volume_scattering = float3(0.0f);
  g_volume_absorption = float3(0.0f);
  g_holdout = 0.0f;

#if defined(GPU_FRAGMENT_SHADER) || defined(GLSL_CPP_STUBS)
  /* clang-format off */ /* Multi-line macros would break line count. */
  [[resource_table]] const eevee::PipelineConstants &pipe = resource_table_get(eevee::PipelineConstants);
  /* clang-format on */
  if (pipe.use_lighting_nodes) [[static_branch]] {
    g_diffuse_light = float3(0.0f);
    g_diffuse_color = float3(0.0f);
    g_glossy_light = float3(0.0f);
    g_glossy_color = float3(0.0f);
    g_transmission_light = float3(0.0f);
    g_transmission_color = float3(0.0f);
    g_light_accumulation_count = 0;
  }
#endif
}
