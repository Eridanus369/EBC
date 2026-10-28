/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Based on Frosbite Unified Volumetric.
 * https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite */

#pragma once

#include "eevee_camera_lib.bsl.hh"
#include "eevee_colorspace_lib.bsl.hh"
#include "eevee_hiz.bsl.hh"
#include "eevee_light_eval.bsl.hh" /* IWYU pragma: export */
#include "eevee_light_lib.bsl.hh"
#include "eevee_light_shared.hh"
#include "eevee_lightprobe_volume.bsl.hh"
#include "eevee_photon_shared.hh"
#include "eevee_renderpass.bsl.hh"
#include "eevee_shadow.bsl.hh"
#include "eevee_volume_lib.bsl.hh"
#include "eevee_volume_shared.hh"
#include "gpu_shader_fullscreen.bsl.hh"

namespace eevee::volume {

float3 volume_light(LightData light, const bool is_directional, LightVector lv)
{
  float power = 1.0f;
  if (!is_directional) {
    float light_radius = light.local().local.shape_radius;
    /**
     * Using "Point Light Attenuation Without Singularity" from Cem Yuksel
     * http://www.cemyuksel.com/research/pointlightattenuation/pointlightattenuation.pdf
     * http://www.cemyuksel.com/research/pointlightattenuation/
     */
    float d = lv.dist;
    float d_sqr = square(d);
    float r_sqr = square(light_radius);

    /* Using reformulation that has better numerical precision. */
    power = 2.0f / (d_sqr + r_sqr + d * sqrt(d_sqr + r_sqr));

    if (light.type == LIGHT_RECT || light.type == LIGHT_ELLIPSE) {
      /* Modulate by light plane orientation / solid angle. */
      power *= saturate(dot(light.z_axis(), lv.L));
    }
  }
  return light.color * eevee::light::power_get(light, LIGHT_VOLUME) * power;
}

#define VOLUMETRIC_SHADOW_MAX_STEP 128.0f

float3 volume_shadow([[resource_table]] const Uniform &uni,
                     const ViewMatrices &view,
                     LightData /*ld*/,
                     const bool is_directional,
                     float3 P,
                     LightVector lv,
                     sampler3D extinction_tx)
{
  if (uni.uniform_buf.volumes.shadow_steps == 0) {
    return float3(1.0f);
  }

  /* Heterogeneous volume shadows. */
  float dd = lv.dist / uni.uniform_buf.volumes.shadow_steps;
  float3 L = lv.L * lv.dist / uni.uniform_buf.volumes.shadow_steps;

  if (is_directional) {
    if (is_panoramic(uni.uniform_buf.camera.type)) {
      /* Panoramic cameras render 6 identical but differently oriented frustums, so we can't use
       * NDC box to bound the ray like below. Use distance to the camera instead because it's
       * view-independent. */
      float3 L_world = lv.L * distance(P, view.position());
      L = L_world / uni.uniform_buf.volumes.shadow_steps;
      dd = length(L);
    }
    else {
      /* For sun light we scan the whole frustum. So we need to get the correct endpoints. */
      float3 ndcP = view.point_world_to_ndc(P);
      float3 ndcL = view.point_world_to_ndc(P + lv.L * lv.dist) - ndcP;

      float3 ndc_frustum_isect = ndcP + ndcL * line_unit_box_intersect_dist_safe(ndcP, ndcL);

      L = view.point_ndc_to_world(ndc_frustum_isect) - P;
      L /= uni.uniform_buf.volumes.shadow_steps;
      dd = length(L);
    }
  }

  /* TODO use shadow maps instead. */
  float3 shadow = float3(1.0f);
  for (float t = 1.0f; t < VOLUMETRIC_SHADOW_MAX_STEP && t <= uni.uniform_buf.volumes.shadow_steps;
       t += 1.0f)
  {
    float3 w_pos = P + L * t;

    float3 v_pos = view.point_world_to_view(w_pos);
    float3 volume_co = volume_view_to_jitter(uni, view, v_pos);
    /* Let the texture be clamped to edge. This reduce visual glitches. */
    float3 s_extinction = texture(extinction_tx, volume_co).rgb;

    shadow *= exp(-s_extinction * dd);
  }
  return shadow;
}

struct Scatter {
  [[compilation_constant]] const bool use_volume_light;

  [[resource_table]] srt_t<Uniform> uniforms;
  [[resource_table]] srt_t<draw::View> views_;
  [[resource_table]] srt_t<LightRenderData> light_data;
  [[resource_table]] srt_t<ShadowRenderData> shadow_data;
  [[resource_table]] srt_t<LightprobeVolumeRenderData> lightprobe_volume_data;

  [[sampler(0)]] sampler3D scattering_history_tx;
  [[sampler(1)]] sampler3D extinction_history_tx;
  /* Alias of UnifiedVolumeProperties::in_extinction_img but as a sampler. */
  [[sampler(9), condition(use_volume_light)]] sampler3D extinction_tx;

  [[image(5, write, UFLOAT_11_11_10)]] image3D out_scattering_img;
  [[image(6, write, UFLOAT_11_11_10)]] image3D out_extinction_img;

  /* World space volumetric caustic grid, written by the photon trace pass. */
  [[image(7, read, UINT_32)]] uimage3DAtomic caustic_volume_r_img;
  [[image(8, read, UINT_32)]] uimage3DAtomic caustic_volume_g_img;
  [[image(9, read, UINT_32)]] uimage3DAtomic caustic_volume_b_img;

  float3 volume_lightprobe_eval([[resource_table]] const Sampling &sampling,
                                float3 P,
                                float3 V,
                                float s_anisotropy)
  {
    [[resource_table]] const LightprobeVolumeRenderData &volume_data = lightprobe_volume_data;
    [[resource_table]] const Uniform &uni = uniforms;

    SphericalHarmonicL1<float4> phase_sh = volume_phase_function_as_sh_L1(V, s_anisotropy);
    SphericalHarmonicL1<float4> volume_radiance_sh = volume_data.sample_probe_no_bias(sampling, P);

    float clamp_indirect = uni.uniform_buf.clamp.volume_indirect;
    volume_radiance_sh = spherical_harmonics::clamp_energy(volume_radiance_sh, clamp_indirect);

    return spherical_harmonics::dot(volume_radiance_sh, phase_sh).xyz;
  }
};

/* World space volumetric caustic grid parameters, matching the photon trace push constants. */
struct CausticVolumeParams {
  /* xyz: grid minimum bound. w: user gain (0 disables volumetric caustics). */
  [[push_constant]] float4 caustic_volume_min_gain;
  /* xyz: inverse grid size. w: unused by the volume pass. */
  [[push_constant]] float4 caustic_volume_scale;
  [[push_constant]] int3 caustic_volume_res;
};

float3 caustic_volume_texel([[resource_table]] Scatter &srt, int3 c)
{
  return float3(float(imageLoad(srt.caustic_volume_r_img, c).r),
                float(imageLoad(srt.caustic_volume_g_img, c).r),
                float(imageLoad(srt.caustic_volume_b_img, c).r));
}

/* Trilinear sample of the world space volumetric caustic grid (uint fixed point). */
float3 caustic_volume_sample([[resource_table]] Scatter &srt,
                             [[resource_table]] const CausticVolumeParams &cvp,
                             float3 P)
{
  float3 uvw = (P - cvp.caustic_volume_min_gain.xyz) * cvp.caustic_volume_scale.xyz;
  if (uvw.x < 0.0f || uvw.y < 0.0f || uvw.z < 0.0f) { return float3(0.0f); }
  if (uvw.x > 1.0f || uvw.y > 1.0f || uvw.z > 1.0f) { return float3(0.0f); }

  int3 res = cvp.caustic_volume_res;
  float3 c = clamp(uvw * float3(res), float3(0.0f), float3(res) - float3(1.0f));
  int3 i0 = int3(int(c.x), int(c.y), int(c.z));
  int3 i1 = min(i0 + int3(1, 1, 1), res - int3(1, 1, 1));
  float3 f = float3(c.x - float(i0.x), c.y - float(i0.y), c.z - float(i0.z));

  float3 v000 = caustic_volume_texel(srt, int3(i0.x, i0.y, i0.z));
  float3 v100 = caustic_volume_texel(srt, int3(i1.x, i0.y, i0.z));
  float3 v010 = caustic_volume_texel(srt, int3(i0.x, i1.y, i0.z));
  float3 v110 = caustic_volume_texel(srt, int3(i1.x, i1.y, i0.z));
  float3 v001 = caustic_volume_texel(srt, int3(i0.x, i0.y, i1.z));
  float3 v101 = caustic_volume_texel(srt, int3(i1.x, i0.y, i1.z));
  float3 v011 = caustic_volume_texel(srt, int3(i0.x, i1.y, i1.z));
  float3 v111 = caustic_volume_texel(srt, int3(i1.x, i1.y, i1.z));

  float3 v00 = mix(v000, v100, f.x);
  float3 v10 = mix(v010, v110, f.x);
  float3 v01 = mix(v001, v101, f.x);
  float3 v11 = mix(v011, v111, f.x);
  float3 v0 = mix(v00, v10, f.y);
  float3 v1 = mix(v01, v11, f.y);
  return mix(v0, v1, f.z) / CAUSTIC_VOLUME_FIXED_SCALE;
}

struct LightEvalCtx {
  float3 radiance;
  float3 P;
  float3 V;
  float anisotropy;

  float3 light_eval_single([[resource_table]] Scatter &srt,
                           LightData light,
                           const bool is_directional)
  {
    [[resource_table]] ShadowRenderData &srd = srt.shadow_data;
    [[resource_table]] const Uniform &uni = srt.uniforms;
    [[resource_table]] const draw::View &views = srt.views_;

    /* TODO(fclem): Own light list for volume without lights that have 0 volume influence. */
    if (eevee::light::power_get(light, LIGHT_VOLUME) == 0.0f) {
      return float3(0);
    }

    LightVector lv = light_shape_vector_get(light, is_directional, P);

    float attenuation = light_attenuation_volume(light, is_directional, lv);
    if (attenuation < LIGHT_ATTENUATION_THRESHOLD) {
      return float3(0);
    }

    float visibility = attenuation;
    if (light.tilemap_index != LIGHT_NO_SHADOW) {
      float delta = srd.shadow_sample(is_directional, light, P);
      if (delta > 0.0f) {
        return float3(0);
      }
    }
    visibility *= volume_phase_function(-V, lv.L, anisotropy);
    if (visibility < LIGHT_ATTENUATION_THRESHOLD) {
      return float3(0);
    }

    float3 Li = volume_light(light, is_directional, lv) * visibility;

    if (light.tilemap_index != LIGHT_NO_SHADOW) {
      Li *= volume_shadow(uni, views.get(0), light, is_directional, P, lv, srt.extinction_tx);
    }

    return Li;
  }

  void eval_directional([[resource_table]] Scatter &srd, uint /*l_idx*/, LightData light)
  {
    radiance += light_eval_single(srd, light, true);
  }

  void eval_local([[resource_table]] Scatter &srd, uint /*l_idx*/, LightData light)
  {
    radiance += light_eval_single(srd, light, false);
  }
};

}  // namespace eevee::volume

namespace eevee::light {
template void foreach_visible<volume::LightEvalCtx, volume::Scatter>(
    const LightRenderData &, float2, float, volume::LightEvalCtx &, volume::Scatter &);
}

namespace eevee::volume {

/* Step 2 : Evaluate all light scattering for each froxels.
 * Also do the temporal reprojection to fight aliasing artifacts. */
[[compute, local_size(VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE, VOLUME_GROUP_SIZE)]]
void scatter_main([[resource_table]] Scatter &srt,
                  [[resource_table]] const CausticVolumeParams &cvp,
                  [[resource_table]] const Uniform &uni,
                  [[resource_table]] const draw::View &views,
                  [[resource_table]] const Sampling &sampling,
                  [[resource_table]] UnifiedVolumeProperties &props,
                  [[global_invocation_id]] const uint3 global_id)
{
  int3 froxel = int3(global_id);

  if (any(greaterThanEqual(froxel, uni.uniform_buf.volumes.tex_size))) {
    return;
  }

  const ViewMatrices view = views.get(0);

  /* Emission. */
  float3 scattering = imageLoadFast(props.in_emission_img, froxel).rgb;
  float3 extinction = imageLoadFast(props.in_extinction_img, froxel).rgb;

  float3 s_scattering = imageLoadFast(props.in_scattering_img, froxel).rgb;

  float offset = sampling.rng_1D_get(SAMPLING_VOLUME_W);
  float jitter = volume_froxel_jitter(froxel.xy, offset);
  float3 uvw = (float3(froxel) + float3(0.5f, 0.5f, 0.5f - jitter)) *
               uni.uniform_buf.volumes.inv_tex_size;
  float3 vP = volume_jitter_to_view(uni, view, uvw);

  float3 P = view.point_view_to_world(vP);
  float3 V = view.world_incident_vector(P);

  float phase = imageLoadFast(props.in_phase_img, froxel).r;
  float phase_weight = imageLoadFast(props.in_phase_weight_img, froxel).r;
  /* Divide by phase total weight, to compute the mean anisotropy. */
  float s_anisotropy = phase / max(1.0f, phase_weight);

  float3 direct_radiance = float3(0.0f);

  if (srt.use_volume_light) [[static_branch]] {
    if (reduce_max(s_scattering) > 0.0f) {
      volume::LightEvalCtx ctx = {
          .radiance = float3(0.0f),
          .P = P,
          .V = V,
          .anisotropy = s_anisotropy,
      };

      float2 pixel = ((float2(froxel.xy) + 0.5f) * uni.uniform_buf.volumes.inv_tex_size.xy) *
                     uni.uniform_buf.volumes.main_view_extent;

      light::foreach_visible(srt.light_data, pixel, vP.z, ctx, srt);
      direct_radiance = ctx.radiance;
    }
  }

  float3 indirect_radiance = srt.volume_lightprobe_eval(sampling, P, V, s_anisotropy).xyz;

  /* Volumetric caustics: photons scatter inside participating media (water, smoke, ...). */
  if (cvp.caustic_volume_min_gain.w > 0.0f) {
    float3 caustic_radiance = caustic_volume_sample(srt, cvp, P) * cvp.caustic_volume_min_gain.w;
    direct_radiance += caustic_radiance;
  }

  direct_radiance *= s_scattering;
  indirect_radiance *= s_scattering;

  float clamp_direct = uni.uniform_buf.clamp.volume_direct;
  float clamp_indirect = uni.uniform_buf.clamp.volume_indirect;
  direct_radiance = colorspace::brightness_clamp_max(direct_radiance, clamp_direct);
  indirect_radiance = colorspace::brightness_clamp_max(indirect_radiance, clamp_indirect);

  direct_radiance *= uni.uniform_buf.clamp.direct_scale;
  indirect_radiance *= uni.uniform_buf.clamp.indirect_scale;

  scattering += direct_radiance + indirect_radiance;

  if (uni.uniform_buf.volumes.history_opacity > 0.0f) {
    /* Temporal reprojection. */
    float3 uvw_history = volume_history_uvw_get(uni, view, froxel);
    if (uvw_history.x != -1.0f) {
      float3 scattering_history = texture(srt.scattering_history_tx, uvw_history).rgb;
      float3 extinction_history = texture(srt.extinction_history_tx, uvw_history).rgb;
      scattering = mix(scattering, scattering_history, uni.uniform_buf.volumes.history_opacity);
      extinction = mix(extinction, extinction_history, uni.uniform_buf.volumes.history_opacity);
    }
  }

  /* Catch NaNs. */
  if (any(isnan(scattering)) || any(isnan(extinction))) {
    scattering = float3(0.0f);
    extinction = float3(0.0f);
  }

  imageStoreFast(srt.out_scattering_img, froxel, float4(scattering, 1.0f));
  imageStoreFast(srt.out_extinction_img, froxel, float4(extinction, 1.0f));
}
struct Integrate {
  [[sampler(0)]] sampler3D in_scattering_tx;
  [[sampler(1)]] sampler3D in_extinction_tx;

  [[image(0, write, UFLOAT_11_11_10)]] image3D out_scattering_img;
  [[image(1, write, UFLOAT_11_11_10)]] image3D out_transmittance_img;
};

/* Step 3 : Integrate for each froxel the final amount of light
 * scattered back to the viewer and the amount of transmittance. */
[[compute, local_size(VOLUME_INTEGRATION_GROUP_SIZE, VOLUME_INTEGRATION_GROUP_SIZE, 1)]]
void integration_main([[resource_table]] Integrate &srt,
                      [[resource_table]] const draw::View &views,
                      [[resource_table]] const Uniform &uni,
                      [[global_invocation_id]] const uint3 global_id)
{
  int2 texel = int2(global_id.xy);

  if (any(greaterThanEqual(texel, uni.uniform_buf.volumes.tex_size.xy))) {
    return;
  }

  const ViewMatrices view = views.get(0);

  /* Start with full transmittance and no scattered light. */
  float3 scattering = float3(0.0f);
  float3 transmittance = float3(1.0f);

  /* Compute view ray. Note that jittering the position of the first voxel doesn't bring any
   * benefit here. */
  float3 uvw = (float3(float2(texel), 0.0f) + float3(0.5f, 0.5f, 0.0f)) *
               uni.uniform_buf.volumes.inv_tex_size;
  float3 view_cell = volume_jitter_to_view(uni, view, uvw);

  const bool uses_radial_depth = volume_uses_radial_depth(uni);

  float prev_ray_len;
  float orig_ray_len = 1.0f;
  if (uses_radial_depth) {
    /* Camera distance at froxel Z = 0, negated: `volume_z_to_view_z` returns a view-space Z
     * (negative, forward is -Z), but the per-step diff below expects a positive distance. */
    prev_ray_len = -volume_z_to_view_z(uni, view, 0.0f);
  }
  else if (view.is_perspective()) {
    prev_ray_len = length(view_cell);
    orig_ray_len = prev_ray_len / view_cell.z;
  }
  else {
    prev_ray_len = view_cell.z;
  }

  for (int i = 0; i <= uni.uniform_buf.volumes.tex_size.z; i++) {
    int3 froxel = int3(texel, i);

    float3 froxel_scattering = texelFetch(srt.in_scattering_tx, froxel, 0).rgb;
    float3 extinction = texelFetch(srt.in_extinction_tx, froxel, 0).rgb;

    float step_len;
    if (uses_radial_depth) {
      float ray_len = -volume_z_to_view_z(
          uni, view, (float(i) + 1.0f) * uni.uniform_buf.volumes.inv_tex_size.z);
      step_len = abs(ray_len - prev_ray_len);
      prev_ray_len = ray_len;
    }
    else {
      float cell_depth = volume_z_to_view_z(
          uni, view, (float(i) + 1.0f) * uni.uniform_buf.volumes.inv_tex_size.z);
      float ray_len = orig_ray_len * cell_depth;

      /* Evaluate Scattering. */
      step_len = abs(ray_len - prev_ray_len);
      prev_ray_len = ray_len;
    }
    float3 froxel_transmittance = exp(-extinction * step_len);
    /* NOTE: Original calculation carries precision issues when compiling for AMD GPUs
     * and running Metal. This version of the equation retains precision well for all
     * macOS HW configurations.
     * Here is the original for reference:
     * `Lscat = (Lscat - Lscat * Tr) / safe_rcp(s_extinction)` */
    float3 froxel_opacity = 1.0f - froxel_transmittance;
    float3 froxel_step_opacity = froxel_opacity * safe_rcp(extinction);

    /* Emission does not work if there is no extinction because
     * `froxel_transmittance` evaluates to 1.0 leading to `froxel_opacity = 0.0 and 0.4 depth.`.
     * (See #65771) To avoid fiddling with numerical values, take the limit of
     * `froxel_step_opacity` as `extinction` approaches zero which is simply `step_len`. */
    bool3 is_invalid_extinction = equal(extinction, float3(0.0f));
    froxel_step_opacity = mix(froxel_step_opacity, float3(step_len), is_invalid_extinction);

    /* Integrate along the current step segment. */
    froxel_scattering = froxel_scattering * froxel_step_opacity;

    /* Accumulate and also take into account the transmittance from previous steps. */
    scattering += transmittance * froxel_scattering;
    transmittance *= froxel_transmittance;

    imageStoreFast(srt.out_scattering_img, froxel, float4(scattering, 1.0f));
    imageStoreFast(srt.out_transmittance_img, froxel, float4(transmittance, 1.0f));
  }
}

struct FragOut {
  [[frag_color(0), index(0)]] float4 radiance;
  [[frag_color(0), index(1)]] float4 transmittance;
};

[[vertex]]
void resolve_vert([[vertex_id]] const int vert_id, [[position]] float4 &out_position)
{
  fullscreen_vertex(vert_id, out_position);
}

/* Step 4 : Apply final integration on top of the scene color.
 * This is only for opaque geometry.
 *
 * Panoramic: each cubemap face resolves its own volumetric contribution independently. No
 * blending across face edges/corners, so results can disagree right at a face seam. */
[[fragment]]
void resolve_frag([[resource_table]] const Uniform &uni,
                  [[resource_table]] const UnifiedVolumeData &volumes,
                  [[resource_table]] RenderPassOutput &render_passes,
                  [[resource_table]] const HiZ &hiz,
                  [[frag_coord]] const float4 frag_co,
                  [[out]] FragOut &out_frag)
{
  float2 uvs = frag_co.xy * uni.uniform_buf.volumes.main_view_extent_inv;
  float scene_depth = texelFetch(hiz.hiz_tx, int2(frag_co.xy), 0).r;

  VolumeResolveSample vol = volumes.resolve(float3(uvs, scene_depth));

  out_frag.radiance = float4(vol.scattering, 0.0f);
  out_frag.transmittance = float4(vol.transmittance, saturate(average(vol.transmittance)));

  render_passes.store_color(
      int2(frag_co.xy), uni.uniform_buf.render_pass.volume_light_id, float4(vol.scattering, 1.0f));
}

PipelineCompute scatter(scatter_main, Scatter{.use_volume_light = false});
PipelineCompute scatter_with_lights(scatter_main, Scatter{.use_volume_light = true});
PipelineCompute integration(integration_main);
PipelineGraphic resolve(resolve_vert, resolve_frag);
}  // namespace eevee::volume
