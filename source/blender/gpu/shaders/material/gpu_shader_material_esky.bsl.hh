/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "gpu_shader_math_base.bsl.hh"
#include "gpu_shader_math_constants.bsl.hh"
#include "gpu_shader_math_safe.bsl.hh"
#include "gpu_shader_material_interface.bsl.hh"

/* -------------------------------------------------------------------- */
/** \name Helpers
 * \{ */

static float fc_sat(float x) { return clamp(x, 0.0f, 1.0f); }

static float fc_remap(float v, float a0, float a1, float b0, float b1)
{
  return b0 + fc_sat((v - a0) / max(a1 - a0, 1.0e-5f)) * (b1 - b0);
}

static float3 fc_safe_normalize(float3 v)
{
  float l = length(v);
  return l > 1.0e-6f ? v / l : float3(0.0f, 0.0f, 1.0f);
}

static float fc_hash(float n) { return fract(sin(n) * 758.5453f); }

static float fc_hash12(float2 p)
{
  return fract(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name 3D noise LUT strip sampling
 * 贴图为水平切片条带：宽 = slices * res，高 = res。
 * basic_noise: 128 张 128x128 (16384x128)，detail_noise: 64 张 64x64 (4096x64)。
 * \{ */

static float fc_sample_3d_lut(sampler2D tex, float3 p, float slices, float res)
{
  float3 c = fract(p) * slices;
  float3 f = fract(c);
  float z0 = floor(c.z);
  float z1 = mod(z0 + 1.0f, slices);
  float x0 = clamp(c.x, 0.0f, res - 1.0f);
  float y0 = clamp(c.y, 0.0f, res - 1.0f);
  float inv_w = 1.0f / (slices * slices);
  float v = (y0 + 0.5f) / res;
  float u0 = (z0 * slices + x0 + 0.5f) * inv_w;
  float u1 = (z1 * slices + x0 + 0.5f) * inv_w;
  float s0 = texture(tex, float2(u0, v)).r;
  float s1 = texture(tex, float2(u1, v)).r;
  return mix(s0, s1, f.z);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Scattering / color helpers
 * \{ */

static float fc_hg(float g, float c)
{
  float g2 = g * g;
  return (1.0f - g2) / (4.0f * M_PI * pow(max(1.0f + g2 - 2.0f * g * c, 1.0e-4f), 1.5f));
}

static float fc_dual_lobe(float fg, float bg, float m, float c)
{
  return mix(fc_hg(bg, c), fc_hg(fg, c), fc_sat(m));
}

static float3 fc_blackbody(float t_k)
{
  float t = max(t_k, 2000.0f);
  float tn = clamp((t - 2000.0f) / 28000.0f, 0.0f, 1.0f);
  float3 lo = float3(1.0f, 0.2f, 0.0f);
  float3 hi = float3(0.3f, 0.5f, 1.0f);
  float3 warm = mix(float3(1.0f, 0.6f, 0.1f), float3(1.0f, 0.95f, 0.85f), tn);
  float3 cool = mix(warm, hi, smoothstep(0.35f, 1.0f, tn));
  return mix(lo, cool, smoothstep(0.0f, 0.4f, tn));
}

static float3 fc_calc_sun_direction(float sun_height_deg, float sun_rotation_deg)
{
  float height_rad = sun_height_deg * M_PI / 180.0f;
  float rotation_rad = sun_rotation_deg * M_PI / 180.0f;
  return float3(cos(height_rad) * cos(rotation_rad),
                cos(height_rad) * sin(rotation_rad),
                sin(height_rad));
}

static float3 fc_rgb_to_hsv(float3 c)
{
  float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
  float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10f;
  return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

static float3 fc_hsv_to_rgb(float3 c)
{
  float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
  float3 p = abs(fract(c.xxx + K.xyz) * 6.0f - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), c.y);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Sun & atmosphere
 * \{ */

static float3 fc_sun_disc(float3 rd,
                          float3 sd,
                          float3 sc,
                          float si,
                          float ss,
                          float bi,
                          float hh)
{
  float ho = sin(radians(hh));
  if (rd.z < ho) { return float3(0.0f); }
  float ang = acos(clamp(dot(rd, sd), -1.0f, 1.0f));

  /* 真实太阳角半径约 0.2665°，sun_size 做整体缩放。 */
  float da = max(ss * 0.00465f, 1.0e-4f);

  /* 日面 + 临边昏暗：中心最亮，边缘压暗到约 0.38，符合真实太阳的 limb darkening。 */
  float r = ang / da;
  float mu = sqrt(max(1.0f - r * r, 0.0f));
  float limb = 1.0f - 0.62f * (1.0f - mu);
  float disc = smoothstep(da * 1.06f, da * 0.94f, ang);

  /* Mie 前向散射 aureole：连续 1/(1+(θ/θ0)^2) 衰减，没有硬边和放射条纹。
   * 近处紧致光晕 + 远处宽域 veiling glare，符合真实大气散射的太阳光斑形态。 */
  float t1 = ang / (da * 3.5f);
  float t2 = ang / (da * 18.0f);
  float aureole = 0.55f / (1.0f + t1 * t1) + 0.12f / (1.0f + t2 * t2);
  float veil = exp(-ang / (da * 45.0f)) * 0.10f;

  float glow = (aureole + veil) * max(bi, 0.0f) * (1.0f - disc);
  float b = disc * (0.85f + 0.6f * limb) + glow;
  float fade = clamp((sd.z - ho) / (da * 3.0f) + 0.5f, 0.0f, 1.0f);
  return pow(sc, float3(0.7f)) * si * b * 1.6f * fade;
}

/**
 * 单次散射大气：以行星球体几何做真实光程积分。
 * 单位统一为公里：半径 km、高度 km、beta 1/km、scale height km。
 */
static float3 fc_atmosphere(float3 rd,
                            float3 sd,
                            float radius,
                            float altitude,
                            float ad,
                            float ac,
                            float oz,
                            float so2,
                            float pm,
                            float sgs,
                            float si,
                            float steps_f)
{
  rd = normalize(rd);
  sd = normalize(sd);

  float atmo_r = radius + 100.0f;
  float3 ro = float3(0.0f, 0.0f, radius + max(altitude, 0.0f));
  float b = dot(ro, rd);
  float c = dot(ro, ro) - atmo_r * atmo_r;
  float h = b * b - c;
  if (h <= 0.0f) { return float3(0.0f); }
  h = sqrt(h);
  float t0 = max(-b - h, 0.0f);
  float t1 = -b + h;
  int steps = int(clamp(steps_f, 4.0f, 32.0f));
  float ds = (t1 - t0) / float(steps);

  float ct = clamp(dot(rd, sd), -1.0f, 1.0f);
  float ct2 = ct * ct;
  float ph_r = (3.0f / (16.0f * M_PI)) * (1.0f + ct2);
  float g = 0.76f;
  float g2 = g * g;
  float ph_m = (3.0f / (8.0f * M_PI)) * ((1.0f - g2) * (1.0f + ct2)) /
               ((2.0f + g2) * pow(max(1.0f + g2 - 2.0f * g * ct, 1.0e-4f), 1.5f));

  float3 beta_r = float3(5.5e-3f, 13.0e-3f, 22.4e-3f) * max(ad, 0.05f);
  float beta_m = 21.0e-3f * (0.5f + ac * 0.5f);
  float sun_air = 1.0f / max(sd.z, 0.06f);

  float3 sum_r = float3(0.0f);
  float3 sum_m = float3(0.0f);
  float3 tau_v = float3(0.0f);
  float tau_vm = 0.0f;

  for (int i = 0; i < 32; i++) {
    if (i >= steps) { break; }
    float t = t0 + (float(i) + 0.5f) * ds;
    float3 pos = ro + rd * t;
    float hgt = max(length(pos) - radius, 0.0f);
    float dr = exp(-hgt / 8.0f) * ds;
    float dm = exp(-hgt / 1.2f) * ds;
    tau_v += beta_r * dr;
    tau_vm += beta_m * dm;

    float tau_rl = exp(-hgt / 8.0f) * 8.0f * sun_air;
    float tau_ml = exp(-hgt / 1.2f) * 1.2f * sun_air;
    float3 attn = exp(-(tau_v + beta_r * tau_rl) - float3(tau_vm + beta_m * tau_ml));
    sum_r += dr * attn;
    sum_m += dm * attn;
  }

  /* 这个放大系数决定天空的基础亮度。原来 22.0 会让天空在线性空间内逼近
   * 1.0，在 AgX 下被压成一片惨白，太阳光斑和云层都失去对比。 */
  float3 sky = (ph_r * beta_r * sum_r + float3(ph_m * beta_m) * sum_m) * (si * 10.0f);

  float view_air = 1.0f / max(rd.z, 0.06f);
  float oza = exp(-oz * 0.0015f * view_air);
  sky *= float3(1.0f, oza, oza * 0.95f);

  float zen = fc_sat(rd.z);
  sky += float3(0.15f, 0.12f, 0.05f) * (so2 * 0.3f) * (1.0f - zen);
  sky += float3(pm * 0.12f, pm * 0.11f, pm * 0.08f) * (1.0f - zen * zen);

  float hf = exp(-max(ad, 0.0f) * (1.0f - zen) * 5.0f);
  sky += float3(0.1f, 0.15f, 0.25f) * ad * (1.0f - hf) * 0.5f;

  float sun_near = smoothstep(0.995f, 1.0f, ct);
  sky *= 1.0f - sun_near * sgs * 0.6f;

  /* 日面周围的前向散射：用连续的 Mie 型衰减替代原来的硬阈值圆盘，
   * 避免出现一层突兀的亮圈（真实大气中太阳周围是平滑的 aureole）。 */
  float sun_ang = acos(clamp(ct, -1.0f, 1.0f));
  float t_near = sun_ang / 0.030f;
  float t_wide = sun_ang / 0.200f;
  float fwd = 0.35f / (1.0f + t_near * t_near) + 0.15f / (1.0f + t_wide * t_wide);
  float shf = smoothstep(-0.3f, 0.05f, sd.z);
  sky += float3(1.0f, 0.9f, 0.74f) * fwd * (1.0f - sgs * 0.5f);
  return sky * shf;
}

static float3 fc_sky(float3 rd,
                     float3 sun_direction,
                     float3 zenith_color,
                     float3 horizon_color,
                     float3 sun_color,
                     float sun_intensity,
                     float sun_size,
                     float sun_bloom,
                     float radius,
                     float altitude,
                     float air_density,
                     float aerosol_concentration,
                     float ozone_amount,
                     float so2_concentration,
                     float pm25_concentration,
                     float sun_glow_suppress,
                     float atmosphere_steps,
                     float use_atmosphere,
                     float horizon_height_deg,
                     float overcast)
{
  float horizon_offset = sin(radians(horizon_height_deg));
  float view_above_horizon = smoothstep(-0.02f, 0.02f, rd.z - horizon_offset);

  float3 sky = float3(0.0f);
  float night_factor = smoothstep(-0.2f, 0.05f, sun_direction.z);

  if (use_atmosphere > 0.5f) {
    sky = fc_atmosphere(rd, sun_direction, radius, altitude, air_density, aerosol_concentration,
                        ozone_amount, so2_concentration, pm25_concentration, sun_glow_suppress,
                        sun_intensity, atmosphere_steps);
  }
  else {
    float horizon = pow(1.0f - fc_sat(rd.z), 1.35f);
    sky = mix(zenith_color, horizon_color, horizon) * sun_intensity;
  }

  sky *= night_factor;

  /* 阴天/雨天：去色、压暗、加厚地平线霾。 */
  float lum = dot(sky, float3(0.2126f, 0.7152f, 0.0722f));
  sky = mix(sky, float3(lum) * float3(0.85f, 0.9f, 1.0f), overcast * 0.55f);
  sky *= 1.0f - overcast * 0.7f;
  sky += float3(0.10f, 0.11f, 0.13f) * overcast * (1.0f - fc_sat(rd.z));

  float sun_horizon_dist = sun_direction.z - horizon_offset;
  float sunset_factor = clamp(1.0f - sun_horizon_dist / 0.15f, 0.0f, 1.0f);

  float sun_above_horizon = step(horizon_offset, sun_direction.z);
  float sunset_glow = sunset_factor * sun_above_horizon *
                      smoothstep(-0.1f, 0.0f, rd.z - horizon_offset);

  float horizon_glow_angle = abs(rd.z - horizon_offset);
  float horizon_glow = smoothstep(0.0f, 0.15f, horizon_glow_angle) *
                       (1.0f - smoothstep(0.15f, 0.3f, horizon_glow_angle));

  float3 sunset_color = mix(float3(1.0f, 0.4f, 0.1f),
                            float3(1.0f, 0.7f, 0.3f),
                            clamp(sunset_factor, 0.0f, 1.0f));
  sky += sunset_color * sunset_glow * horizon_glow * sun_intensity * 0.5f * (1.0f - overcast);

  float twilight_strength = clamp((sun_direction.z - horizon_offset + 0.3f) / 0.4f, 0.0f, 1.0f);
  twilight_strength *= (1.0f - view_above_horizon);
  sky += float3(0.1f, 0.15f, 0.3f) * twilight_strength * 0.3f;

  float sun_vis = sun_intensity * (1.0f - overcast * 0.9f);
  if (sun_vis > 0.0f) {
    sky += fc_sun_disc(rd, sun_direction, sun_color, sun_vis, sun_size, sun_bloom,
                       horizon_height_deg);
  }

  float below_fade = 1.0f - smoothstep(0.0f, 0.2f, abs(rd.z - horizon_offset));
  float3 below_color = float3(0.02f, 0.03f, 0.06f) * air_density * night_factor;
  below_color += float3(0.05f, 0.03f, 0.01f) * sunset_factor * sun_above_horizon * below_fade *
                 night_factor;

  /* 地平线附近用天空色做霾，避免天地交界出现硬边。 */
  float haze_blend = exp(-abs(rd.z - horizon_offset) * 20.0f);
  below_color = mix(below_color, sky, haze_blend * 0.6f);

  sky = mix(below_color, sky, view_above_horizon);
  return sky;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Stars & aurora
 * \{ */

static float fc_star_hash(float3 x)
{
  float3 p = float3(dot(x, float3(214.1f, 127.7f, 125.4f)),
                    dot(x, float3(260.5f, 183.3f, 954.2f)),
                    dot(x, float3(209.5f, 571.3f, 961.2f)));
  return fract(sin(p.x) * 43758.5453123f);
}

static float3 fc_stars(float3 rd,
                       float nf,
                       float lat,
                       float lon,
                       float time_s,
                       float density,
                       float brightness)
{
  if (nf < 0.001f) { return float3(0.0f); }
  float3 stars = float3(0.0f);
  float lr = radians(lat);
  float gr = radians(lon);
  float cl = cos(lr);
  float sl = sin(lr);
  float cg = cos(gr);
  float sg = sin(gr);
  int count = clamp(int(300.0f * density), 50, 800);
  for (int i = 0; i < 800; i++) {
    if (float(i) >= float(count)) { break; }
    float sid = float(i);
    float s1 = fc_hash(sid * 137.5f);
    float s2 = fc_hash(sid * 83.3f + 1.0f);
    float s3 = fc_hash(sid * 47.9f + 2.0f);
    float ra = s1 * 6.283f;
    float dec = acos(2.0f * s2 - 1.0f) - 1.5708f;
    float x = cos(dec) * cos(ra);
    float y = cos(dec) * sin(ra);
    float z = sin(dec);
    float nx = x * cg + z * sg;
    float ny = y;
    float nz = -x * sg + z * cg;
    x = nx * cl - ny * sl;
    y = nx * sl + ny * cl;
    z = nz;
    float vis = step(0.0f, z);
    float ang = acos(clamp(dot(rd, float3(x, y, z)), -1.0f, 1.0f));
    float size = 0.0003f + s3 * 0.001f;
    float val = smoothstep(size, 0.0f, ang);
    float br = pow(max(s3, 0.01f), 0.8f);
    float tw = 0.7f + sin(time_s * 2.0f + sid * 2.0f) * 0.3f;
    stars += float3(br * val * vis * tw * brightness);
  }
  return stars * nf;
}

static float fc_aurora_hash(float n) { return fract(sin(n) * 758.5453f); }

static float fc_aurora_noise(float3 x)
{
  float3 p = floor(x);
  float3 f = fract(x);
  float n = p.x + p.y * 57.0f + p.z * 800.0f;
  return mix(mix(mix(fc_aurora_hash(n + 0.0f), fc_aurora_hash(n + 1.0f), f.x),
                 mix(fc_aurora_hash(n + 57.0f), fc_aurora_hash(n + 58.0f), f.x), f.y),
             mix(mix(fc_aurora_hash(n + 800.0f), fc_aurora_hash(n + 801.0f), f.x),
                 mix(fc_aurora_hash(n + 857.0f), fc_aurora_hash(n + 858.0f), f.x), f.y),
             f.z);
}

static float fc_aurora_fbm(float3 p)
{
  float f = 0.5f * fc_aurora_noise(p);
  p *= 2.02f; f += 0.25f * fc_aurora_noise(p);
  p *= 2.03f; f += 0.125f * fc_aurora_noise(p);
  p *= 2.01f; f += 0.0625f * fc_aurora_noise(p);
  p *= 2.04f; f += 0.03125f * fc_aurora_noise(p);
  return f * 1.032258f;
}

static float fc_get_aurora(float3 p)
{
  p += fc_aurora_fbm(float3(p.x, p.y, 0.0f) * 0.5f) * 2.25f;
  float a = smoothstep(0.0f, 0.9f, fc_aurora_fbm(p * 2.0f) * 2.2f - 1.1f);
  return max(a, 0.0f);
}

static float3 fc_aurora(float3 rd,
                        float nf,
                        float time_s,
                        float intensity,
                        float speed,
                        float color_factor)
{
  if (nf < 0.001f) { return float3(0.0f); }
  float3 aurora = float3(0.0f);
  float2 rp = float2(rd.x, rd.z);
  aurora += float3(0.2f, 0.8f, 0.3f) *
            fc_get_aurora(float3(rp * float2(1.2f, 1.0f), time_s * speed * 0.22f)) * 0.9f;
  aurora += float3(0.3f, 0.6f, 0.9f) *
            fc_get_aurora(float3(rp * float2(1.0f, 0.7f), time_s * speed * 0.27f)) * 0.9f;
  aurora += float3(0.5f, 0.3f, 0.8f) *
            fc_get_aurora(float3(rp * float2(0.8f, 0.6f), time_s * speed * 0.29f)) * 0.5f;
  aurora += float3(0.4f, 0.5f, 0.7f) *
            fc_get_aurora(float3(rp * float2(0.9f, 0.5f), time_s * speed * 0.20f)) * 0.57f;
  return aurora * intensity * nf * (1.0f + color_factor * 0.2f);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cloud parameters (per family)
 * \{ */

static float fc_cov(float type, int fam)
{
  if (fam == 0) { return type < 0.5f ? 0.3f : (type < 1.5f ? 0.4f : 0.8f); }
  if (fam == 1) { return type < 0.5f ? 0.5f : 0.9f; }
  if (fam == 2) { return type < 0.5f ? 0.95f : (type < 1.5f ? 0.6f : 0.95f); }
  if (fam == 3) { return type < 0.5f ? 0.4f : 0.5f; }
  return type < 0.5f ? 0.4f : (type < 1.5f ? 0.3f : (type < 2.5f ? 0.2f : 0.5f));
}

static float fc_den(float type, int fam)
{
  if (fam == 0) { return type < 0.5f ? 0.2f : (type < 1.5f ? 0.15f : 0.25f); }
  if (fam == 1) { return type < 0.5f ? 0.4f : 0.6f; }
  if (fam == 2) { return type < 0.5f ? 0.3f : (type < 1.5f ? 0.5f : 1.5f); }
  if (fam == 3) { return type < 0.5f ? 0.8f : 2.0f; }
  return type < 0.5f ? 0.6f : (type < 1.5f ? 1.2f : (type < 2.5f ? 0.8f : 1.0f));
}

static float fc_bsc(float type, int fam)
{
  if (fam == 0) { return type < 0.5f ? 0.15f : (type < 1.5f ? 0.1f : 0.08f); }
  if (fam == 1) { return type < 0.5f ? 0.12f : 0.06f; }
  if (fam == 2) { return type < 0.5f ? 0.05f : (type < 1.5f ? 0.18f : 0.04f); }
  if (fam == 3) { return type < 0.5f ? 0.25f : 0.2f; }
  return type < 0.5f ? 0.15f : (type < 1.5f ? 0.2f : (type < 2.5f ? 0.08f : 1.0f));
}

static float fc_dsc(float type, int fam)
{
  if (fam == 0) { return type < 0.5f ? 0.8f : (type < 1.5f ? 1.2f : 0.5f); }
  if (fam == 1) { return type < 0.5f ? 1.0f : 0.6f; }
  if (fam == 2) { return type < 0.5f ? 0.4f : (type < 1.5f ? 0.9f : 0.3f); }
  if (fam == 3) { return type < 0.5f ? 0.7f : 0.8f; }
  return type < 0.5f ? 1.5f : (type < 1.5f ? 0.5f : (type < 2.5f ? 0.6f : 1.0f));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cloud density field
 * \{ */

static float fc_cloud_map(float3 pos_km,
                          float h01,
                          sampler2D basic_noise,
                          sampler2D detail_noise,
                          sampler2D weather_map,
                          sampler2D curl_noise,
                          float time_s,
                          float2 weather_uv_scale,
                          float cov,
                          float den,
                          float2 wind,
                          float speed,
                          float nscale,
                          float ndetail)
{
  /* Flower 噪声为 Y-up，Blender World 为 Z-up：交换竖直轴。 */
  float3 position_km = float3(pos_km.x, pos_km.z, pos_km.y);
  float3 position_meter = position_km * 1000.0f;
  float3 wind_dir = normalize(float3(wind.x, 0.0f, wind.y) + float3(1.0e-5f, 0.0f, 0.0f));
  position_meter += wind_dir * h01 * 500.0f;
  position_km = position_meter * 0.001f;
  float3 wind_offset = (wind_dir + float3(0.0f, 0.1f, 0.0f)) * time_s * speed;

  float4 weather_value = texture(weather_map, fract(position_km.xz * weather_uv_scale));
  float curl = texture(
      curl_noise,
      mod((time_s * speed * 50.0f + position_meter.xz) * 1.0e-6f + 0.5f, 1.0f)).r;
  float local_coverage = fc_sat(curl * 3.0f - 0.75f) * 0.2f;
  float coverage = fc_sat(cov * (local_coverage + weather_value.r));

  /* 竖向剖面与 esky.glsl 的 fc_cloud_map 完全一致：云底 0~10% 淡入、
   * 80% 以上淡出，中段保持满强度。剖面先与噪声相乘，再由覆盖率阈值把
   * 乘积削成云体，因此云底高度由噪声取值决定，不会出现一条贴着层底的
   * 水平切面。 */
  float gradient_shape = fc_remap(h01, 0.00f, 0.10f, 0.10f, 1.0f) *
                         fc_remap(h01, 0.10f, 0.80f, 1.0f, 0.20f);
  float3 basic_position = position_km + wind_offset;
  float basic = fc_sample_3d_lut(basic_noise, basic_position * nscale, 128.0f, 128.0f);
  float basic_cloud = gradient_shape * basic;
  float basic_with_coverage = coverage *
                              fc_remap(basic_cloud, 1.0f - coverage, 1.0f, 0.0f, 1.0f);

  /* 细节噪声按高度在正反两向混合：底部侵蚀云体、顶部蓬松云顶。 */
  float3 detail_position = position_km - wind_offset * 0.15f;
  float detail = fc_sample_3d_lut(detail_noise, detail_position * ndetail, 64.0f, 64.0f);
  float detail_mix = 0.2f * mix(detail, 1.0f - detail, fc_sat(h01 * 10.0f));

  float density_shape = fc_sat(0.01f + h01 * 1.15f) * den *
                        fc_remap(h01, 0.0f, 0.1f, 0.0f, 1.0f) *
                        fc_remap(h01, 0.8f, 1.0f, 1.0f, 0.0f);
  float density = fc_remap(basic_with_coverage, detail_mix, 1.0f, 0.0f, 1.0f);
  return max(0.0f, density * density_shape);
}

static float fc_light_transmittance(float3 position_km,
                                    float3 sun_direction,
                                    float planet_radius,
                                    float cloud_start,
                                    float cloud_thickness,
                                    sampler2D basic_noise,
                                    sampler2D detail_noise,
                                    sampler2D weather_map,
                                    sampler2D curl_noise,
                                    float time_s,
                                    float2 weather_uv_scale,
                                    float cov,
                                    float den,
                                    float2 wind,
                                    float speed,
                                    float nscale,
                                    float ndetail,
                                    float light_step,
                                    float light_step_multiplier,
                                    float light_steps,
                                    float extinction_factor,
                                    float jitter)
{
  float optical_depth = 0.0f;
  float step_length = max(light_step, 1.0e-4f);
  /* 每个像素用蓝噪声抖动首步距离，打散固定采样位置造成的放射状条纹。 */
  float distance_along_light = step_length * jitter;
  int count = int(clamp(light_steps, 1.0f, 16.0f));
  for (int i = 0; i < 16; i++) {
    if (i >= count) { break; }
    float3 sample_position = position_km + sun_direction * distance_along_light;
    float altitude = length(sample_position) - planet_radius;
    float height01 = (altitude - cloud_start) / max(cloud_thickness, 1.0e-4f);
    if (height01 >= 0.0f && height01 <= 1.0f) {
      optical_depth += fc_cloud_map(sample_position,
                                    height01,
                                    basic_noise,
                                    detail_noise,
                                    weather_map,
                                    curl_noise,
                                    time_s,
                                    weather_uv_scale,
                                    cov,
                                    den,
                                    wind,
                                    speed,
                                    nscale,
                                    ndetail) *
                       step_length * extinction_factor;
    }
    distance_along_light += step_length;
    step_length *= max(light_step_multiplier, 1.0f);
  }
  return exp(-optical_depth * 1000.0f);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Cloud ray-marching
 * \{ */

struct fc_interval_res {
  bool hit;
  float near_t;
  float far_t;
};

static fc_interval_res fc_sphere_interval(float3 ro, float3 rd, float radius)
{
  fc_interval_res res;
  float b = dot(ro, rd);
  float c = dot(ro, ro) - radius * radius;
  float h = b * b - c;
  if (h <= 0.0f) {
    res.hit = false;
    res.near_t = 0.0f;
    res.far_t = 0.0f;
    return res;
  }
  h = sqrt(h);
  res.near_t = -b - h;
  res.far_t = -b + h;
  res.hit = res.far_t > 0.0f;
  return res;
}

struct FcCloudOut {
  float trans;
  float3 scattered;
  float dens_sum;
  float used;
};

static FcCloudOut fc_trace_family(float3 ro,
                                  float3 rd,
                                  float3 sun,
                                  float radius,
                                  float cloud_base,
                                  float cloud_thick,
                                  sampler2D basic_noise,
                                  sampler2D detail_noise,
                                  sampler2D weather_map,
                                  sampler2D curl_noise,
                                  float time_s,
                                  float2 uv_scale,
                                  float cov,
                                  float den,
                                  float2 wind,
                                  float speed,
                                  float nscale,
                                  float ndetail,
                                  float max_dist,
                                  float trace_start,
                                  int ray_steps,
                                  float light_step,
                                  float light_mult,
                                  int light_steps,
                                  float3 sun_col,
                                  float sun_int,
                                  float3 albedo,
                                  float3 amb_col,
                                  float amb_str,
                                  float p0,
                                  float p1,
                                  float powder,
                                  float ms_ext,
                                  float ms_str,
                                  float jitter,
                                  float day_night,
                                  float storm,
                                  float flash,
                                  float trans_in,
                                  float3 scattered_in,
                                  float dens_in,
                                  float used_in)
{
  FcCloudOut o;
  o.trans = trans_in;
  o.scattered = scattered_in;
  o.dens_sum = dens_in;
  o.used = used_in;

  float thick = max(cloud_thick, 1.0e-4f);
  fc_interval_res inner = fc_sphere_interval(ro, rd, radius + cloud_base);
  fc_interval_res outer = fc_sphere_interval(ro, rd, radius + cloud_base + thick);
  if (!outer.hit) { return o; }

  float cam_alt = length(ro) - radius;
  float above_layer = step(cloud_base + thick, cam_alt);
  /* 地面视角向下没有云；相机在云层之上时允许俯视云顶。 */
  if (rd.z <= -0.02f && above_layer < 0.5f) { return o; }

  float st = 0.0f;
  float et = 0.0f;
  if (above_layer > 0.5f) {
    /* 相机在云层之上：从云顶(outer.near_t)射入，到云底(inner.far_t)穿出。 */
    st = max(outer.near_t, 0.0f);
    et = min(max(inner.far_t, st), st + max(max_dist, 1.0f));
  }
  else if (cam_alt >= cloud_base) {
    /* 相机位于云层内部：从当前位置开始向外。 */
    st = 0.0f;
    et = min(outer.far_t, max(max_dist, 1.0f));
  }
  else {
    st = inner.hit ? max(inner.far_t, 0.0f) : max(outer.near_t, 0.0f);
    et = min(outer.far_t, st + max(max_dist, 1.0f));
  }
  if (et <= st) { return o; }
  if (st > max(trace_start, 1.0f)) { return o; }

  int steps = clamp(ray_steps, 16, 128);
  float sl = (et - st) / float(steps);
  /* 步进起点固定取步长中点：逐像素固定的蓝噪声抖动会在硬阈值下放大成
   * 满屏颗粒，而没有 sample_index 时抖动也无法被 TAA 收敛。 */
  float t = st + sl * 0.5f;

  for (int i = 0; i < 128; i++) {
    if (i >= steps || t > et || o.trans < 0.005f) { break; }
    float3 pos = ro + rd * t;
    float alt = length(pos) - radius;
    float h01 = (alt - cloud_base) / thick;
    t += sl;

    float d = fc_cloud_map(pos, h01, basic_noise, detail_noise, weather_map, curl_noise,
                           time_s, uv_scale, cov, den, wind, speed, nscale, ndetail);
    d *= 1.0f - smoothstep(0.97f, 1.0f, t / max(et, 1.0f));
    d *= 1.0f - smoothstep(60.0f, 140.0f, t);
    o.used += 1.0f;
    if (d <= 1.0e-5f) { continue; }

    float lt0 = fc_light_transmittance(pos, sun, radius, cloud_base, thick, basic_noise,
                                       detail_noise, weather_map, curl_noise, time_s, uv_scale,
                                       cov, den, wind, speed, nscale, ndetail, light_step,
                                       light_mult, float(light_steps), 1.0f, 0.5f);
    float lt1 = fc_light_transmittance(pos, sun, radius, cloud_base, thick, basic_noise,
                                       detail_noise, weather_map, curl_noise, time_s, uv_scale,
                                       cov, den, wind, speed, nscale, ndetail, light_step,
                                       light_mult, float(light_steps), ms_ext,
                                       0.75f);

    float pd = pow(fc_sat(d * 8.0f), fc_remap(h01, 0.3f, 0.85f, 0.5f, 2.0f));
    float pw = sqrt(max(pd * 20.0f, 0.0f)) * powder;

    float3 am = amb_col * max(amb_str, 0.0f) * fc_sat(0.5f + h01) *
                (0.04f + day_night * 0.96f);
    am = mix(am, am * float3(0.35f, 0.40f, 0.55f), fc_sat(storm * (1.0f - h01) * 0.8f));

    float3 l0 = sun_col * sun_int * day_night * lt0 * p0 * pw + am;
    float3 l1 = sun_col * sun_int * day_night * lt1 * p1 * pw + am * ms_str;

    l0 += float3(0.7f, 0.85f, 1.0f) * flash * (1.0f - h01) * 2.5f;
    l1 += float3(0.7f, 0.85f, 1.0f) * flash * (1.0f - h01) * 1.2f;

    float s0 = max(d, 1.0e-6f);
    float s1 = max(d * ms_ext, 1.0e-6f);
    float st0 = exp(-s0 * sl * 1000.0f);
    float st1 = exp(-s1 * sl * 1000.0f);

    float3 sc0 = albedo * d * l0;
    float3 sc1 = albedo * d * ms_str * l1;
    o.scattered += o.trans *
                   ((sc0 - sc0 * st0) / s0 + (sc1 - sc1 * st1) / s1);
    o.trans *= st0;
    o.dens_sum += d;
  }
  return o;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Weather effects
 * \{ */

static float fc_lightning_flash(float time, float frequency)
{
  float f = fc_sat(frequency);
  if (f < 0.01f) { return 0.0f; }
  float interval = 3.0f + 6.0f * (1.0f - f);
  float cyc = floor(time / interval);
  float local = time - cyc * interval;
  float rnd = fc_hash(cyc * 13.7f + 1.3f);
  float t0 = rnd * interval * 0.6f;
  float bright = 0.55f + 0.45f * fc_hash(cyc * 7.1f + 4.9f);
  float main_flash = exp(-max(local - t0, 0.0f) * 18.0f) * step(t0, local);
  float t1 = t0 + 0.06f + 0.09f * fc_hash(cyc * 3.3f + 2.1f);
  float restrike = exp(-max(local - t1, 0.0f) * 30.0f) * step(t1, local) *
                   (0.4f + 0.4f * fc_hash(cyc * 5.7f + 8.2f));
  return (main_flash + restrike) * bright * f;
}

static float fc_rain_streaks(float2 uv, float t, float intensity, float sky_mask)
{
  float acc = 0.0f;
  for (int layer = 0; layer < 3; layer++) {
    float fl = float(layer);
    float2 gv = float2(uv.x * (70.0f + fl * 26.0f), uv.y * (34.0f + fl * 12.0f));
    gv += float2(fl * 5.3f, t * (6.0f + fl * 2.0f));
    float2 cell = floor(gv);
    float2 f = fract(gv);
    float h = fc_hash12(cell + fl * 37.1f);
    float x = 0.15f + h * 0.7f;
    float streak = smoothstep(0.014f + 0.008f * h, 0.0f, abs(f.x - x));
    float head = smoothstep(0.0f, 0.18f, f.y) * smoothstep(1.0f, 0.6f, f.y);
    acc += streak * head * step(0.55f, h) * (0.4f + 0.6f * h);
  }
  return acc * intensity * sky_mask * 0.55f;
}

static float fc_snow_flakes(float2 uv, float t, float intensity, float sky_mask)
{
  float acc = 0.0f;
  for (int layer = 0; layer < 2; layer++) {
    float fl = float(layer);
    float2 gv = uv * (48.0f + fl * 24.0f);
    gv += float2(sin(t * 0.6f + fl * 2.1f) * 0.35f, -t * (4.0f + fl * 2.0f));
    float2 cell = floor(gv);
    float2 f = fract(gv);
    float h = fc_hash12(cell + fl * 19.3f);
    float2 c = float2(0.2f + h * 0.6f, 0.2f + fc_hash12(cell + 2.7f) * 0.6f);
    float2 d = f - c;
    float flake = exp(-dot(d, d) * (120.0f - 40.0f * h));
    acc += flake * step(0.35f, h) * (0.5f + 0.5f * h);
  }
  return acc * intensity * sky_mask * 0.5f;
}

static float fc_lens_rain(float2 uv, float t, float strength, float length)
{
  float acc = 0.0f;
  for (int layer = 0; layer < 2; layer++) {
    float fl = float(layer);
    float2 gv = float2(uv.x * (7.0f + fl * 5.0f), uv.y * (11.0f + fl * 4.0f));
    gv.y += t * (1.2f + fl * 0.7f) + fl * 3.7f;
    float2 cell = floor(gv);
    float2 f = fract(gv);
    float h = fc_hash12(cell + fl * 23.7f);
    float hx = 0.2f + fc_hash12(cell + 7.3f) * 0.6f;
    float2 d = float2((f.x - hx) * 45.0f, (f.y - 0.55f) * 30.0f);
    float drop = exp(-dot(d, d));
    float tail = smoothstep(0.0f, 0.2f, f.y) * smoothstep(1.0f, 0.35f, f.y);
    float trail = exp(-abs(f.x - hx) * 90.0f) * tail;
    acc += (drop + trail * max(length, 0.0f) * 0.8f) * step(h, strength);
  }
  return fc_sat(acc * 0.9f);
}

static float3 fc_moon(float3 rd,
                      float3 md,
                      float moon_size,
                      float3 moon_col,
                      sampler2D moon_tex,
                      float ext)
{
  float rad = radians(max(moon_size, 0.2f)) * 0.5f;
  float ang = acos(clamp(dot(rd, md), -1.0f, 1.0f));
  /* 月晕可延伸到月面半径数倍之外，先按光晕范围裁剪。 */
  float halo_rad = rad * 4.5f;
  if (ang > halo_rad) { return float3(0.0f); }

  float3 ref = float3(0.0f, 0.0f, 1.0f);
  if (abs(md.z) > 0.9f) { ref = float3(0.0f, 1.0f, 0.0f); }
  float3 right = normalize(cross(ref, md));
  float3 up = cross(md, right);

  float3 moon = float3(0.0f);
  if (ang <= rad) {
    float denom = max(sin(rad), 1.0e-4f);
    float2 p = float2(dot(rd, right), dot(rd, up)) / denom;
    float r2 = dot(p, p);
    if (r2 <= 1.0f) {
      float rn = sqrt(r2);
      float3 tex = texture(moon_tex, p * 0.5f + 0.5f).rgb;
      /* 原贴图对比度极低（几乎平坦），叠加程序化月海/环形山起伏，
       * 让月面在远景下依然能看出结构。 */
      float3 lp = float3(p.x, p.y, 0.0f) * 3.0f;
      float relief = fc_aurora_fbm(lp) - 0.5f;
      float maria = fc_aurora_fbm(lp * 0.45f + float3(2.7f, 5.1f, 0.3f)) - 0.5f;
      float shade = 1.0f + relief * 1.6f + maria * 0.9f;
      /* 乘以过大的系数会让月面直接顶到 1.0，在 AgX 下变成一块白板，
       * 反而看不清贴图细节，因此这里保持中等增益。 */
      tex = tex * max(shade, 0.15f) * 1.5f;
      /* 边缘轻微变暗，增强球体感。 */
      float limb = smoothstep(1.0f, 0.82f, rn);
      float edge = 0.6f + 0.4f * smoothstep(1.0f, 0.5f, rn);
      moon = tex * limb * edge;
    }
  }

  /* 月晕：贴近月面的柔和光晕 + 外层冰晶光环。
   * 光晕整体乘以一个在 halo_rad 处衰减到 0 的窗口，否则会在裁剪半径上
   * 露出一圈生硬的圆盘边缘。 */
  float dist = max(ang - rad, 0.0f);
  float glow = exp(-dist / (rad * 0.6f)) * 0.24f;
  float ring_d = (ang - rad * 2.4f) / (rad * 0.45f);
  float ring = exp(-ring_d * ring_d) * 0.11f;
  float window = 1.0f - smoothstep(halo_rad * 0.7f, halo_rad, ang);
  float halo = (glow + ring) * window;
  float3 halo_col = float3(0.72f, 0.82f, 1.0f);

  return (moon + halo_col * halo * 0.9f) * moon_col * ext;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Node entry
 * \{ */

[[node]]

void node_esky(
  float altitude,
  float exposure,
  float hue,
  float saturation,
  float brightness,
  float sun_height,
  float sun_rotation,
  float sun_intensity,
  float sun_temperature,
  float sun_size,
  float sun_bloom,
  float horizon_height,
  float planet_radius,
  float star_density,
  float star_brightness,
  float latitude,
  float longitude,
  bool aurora_enabled,
  float aurora_intensity,
  float aurora_speed,
  float aurora_color_factor,
  bool moon_enable,
  float moon_height,
  float moon_rotation,
  float moon_size,
  float moon_intensity,
  bool use_atmosphere,
  float air_density,
  float aerosol_concentration,
  float ozone_amount,
  float so2_concentration,
  float pm25_concentration,
  float sun_glow_suppress,
  float4 zenith_color,
  float4 horizon_color,
  float atmosphere_steps,
  float weather_uv_scale,
  float coverage,
  float density,
  float basic_noise_scale,
  float detail_noise_scale,
  float3 wind_direction,
  float cloud_speed,
  float time_speed,
  bool high_cloud_enable,
  int high_cloud_type,
  float high_cloud_base,
  float high_cloud_thickness,
  bool mid_cloud_enable,
  int mid_cloud_type,
  float mid_cloud_base,
  float mid_cloud_thickness,
  bool low_cloud_enable,
  int low_cloud_type,
  float low_cloud_base,
  float low_cloud_thickness,
  bool vertical_cloud_enable,
  int vertical_cloud_type,
  float vertical_cloud_base,
  float vertical_cloud_top,
  bool special_cloud_enable,
  int special_cloud_type,
  float special_cloud_base,
  float special_cloud_thickness,
  float4 cloud_albedo,
  float4 ambient_color,
  float ambient_strength,
  float phase_forward,
  float phase_backward,
  float phase_mix,
  float powder_scale,
  float multi_scatter_extinction,
  float multi_scatter_strength,
  int weather_type,
  float rain_intensity,
  bool lens_rain_enable,
  float lens_rain_strength,
  float lens_rain_length,
  float snow_intensity,
  float lightning_frequency,
  float max_trace_distance,
  float trace_start_limit,
  float ray_steps,
  float light_step,
  float light_step_multiplier,
  float light_steps,
  sampler2D basic_noise,
  sampler2D detail_noise,
  sampler2D weather_map,
  sampler2D curl_noise,
  sampler2D blue_noise,
  sampler2D moon_tex,
  out float4 color,
  out float out_cloud_alpha,
  out float out_cloud_density,
  out float out_cloud_transmittance)
{
  float3 rd = fc_safe_normalize(g_data.P);

  /* 内置场景时间（秒），time_speed 控制云/天气动画快慢。 */
  float scene_seconds = 0.0f;
  float scene_frame = 0.0f;
  scene_time_uniforms(scene_seconds, scene_frame);
  float time_seconds = scene_seconds * max(time_speed, 0.0f);

  float2 uv_scale = float2(weather_uv_scale, weather_uv_scale);
  int ray_steps_i = int(ray_steps);
  int light_steps_i = int(light_steps);

  float3 sun = fc_safe_normalize(fc_calc_sun_direction(sun_height, sun_rotation));
  float3 sun_col = fc_blackbody(sun_temperature);

  float horizon_offset = sin(radians(horizon_height));
  float day_night = smoothstep(-0.15f, 0.05f, sun.z - horizon_offset);

  float w_rain = smoothstep(0.5f, 1.5f, weather_type);
  float w_thunder = smoothstep(1.5f, 2.5f, weather_type);
  float w_snow = smoothstep(2.5f, 3.5f, weather_type);
  float overcast = fc_sat(w_rain * 0.85f + w_thunder * 1.0f + w_snow * 0.8f);
  float storm_f = w_thunder;
  float flash = fc_lightning_flash(time_seconds, lightning_frequency) * w_thunder;

  float radius = max(planet_radius, 100.0f);
  float cam_alt = max(altitude, 0.0f);
  float3 ro = float3(0.0f, 0.0f, radius + cam_alt);

  float3 sky = fc_sky(rd, sun, zenith_color.rgb, horizon_color.rgb, sun_col, sun_intensity,
                      sun_size, sun_bloom, radius, cam_alt, air_density,
                      aerosol_concentration, ozone_amount, so2_concentration,
                      pm25_concentration, sun_glow_suppress, atmosphere_steps,
                      use_atmosphere ? 1.0f : 0.0f, horizon_height, overcast);

  float view_light_cosine = dot(rd, sun);
  float phase0 = fc_dual_lobe(phase_forward, phase_backward, phase_mix, -view_light_cosine);
  float phase1 = mix(1.0f / (4.0f * M_PI), phase0, 0.5f);

  float2 blue_uv = fract(float2(atan(rd.y, rd.x) / (2.0f * M_PI) + 0.5f, rd.z * 0.5f + 0.5f) *
                         64.0f + float2(0.0f, 0.0f));
  float jitter = texture(blue_noise, blue_uv).r;

  float sun_int_eff = sun_intensity * (1.0f - overcast * 0.55f);
  float3 albedo = cloud_albedo.rgb;
  float3 amb_col = ambient_color.rgb;
  float cov_boost = 1.0f + 0.8f * overcast;
  float den_boost = 1.0f + 1.5f * overcast;

  float transmittance = 1.0f;
  float3 scattered = float3(0.0f);
  float dens_sum = 0.0f;
  float used = 0.0f;

  if (low_cloud_enable && transmittance > 0.005f) {
    FcCloudOut o = fc_trace_family(ro, rd, sun, radius, low_cloud_base,
                                   max(low_cloud_thickness, 0.01f), basic_noise, detail_noise,
                                   weather_map, curl_noise, time_seconds, uv_scale,
                                   coverage * fc_cov(low_cloud_type, 2) * cov_boost,
                                   density * fc_den(low_cloud_type, 2) * den_boost,
                                   wind_direction.xy, cloud_speed,
                                   basic_noise_scale * fc_bsc(low_cloud_type, 2),
                                   detail_noise_scale * fc_dsc(low_cloud_type, 2),
                                   max_trace_distance, trace_start_limit, ray_steps_i, light_step,
                                   light_step_multiplier, light_steps_i, sun_col, sun_int_eff,
                                   albedo, amb_col, ambient_strength, phase0, phase1, powder_scale,
                                   multi_scatter_extinction, multi_scatter_strength, jitter,
                                   day_night, storm_f, flash, transmittance, scattered,
                                   dens_sum, used);
    transmittance = o.trans;
    scattered = o.scattered;
    dens_sum = o.dens_sum;
    used = o.used;
  }

  if (mid_cloud_enable && transmittance > 0.005f) {
    FcCloudOut o = fc_trace_family(ro, rd, sun, radius, mid_cloud_base,
                                   max(mid_cloud_thickness, 0.01f), basic_noise, detail_noise,
                                   weather_map, curl_noise, time_seconds, uv_scale,
                                   coverage * fc_cov(mid_cloud_type, 1) * cov_boost,
                                   density * fc_den(mid_cloud_type, 1) * den_boost,
                                   wind_direction.xy, cloud_speed,
                                   basic_noise_scale * fc_bsc(mid_cloud_type, 1),
                                   detail_noise_scale * fc_dsc(mid_cloud_type, 1),
                                   max_trace_distance, trace_start_limit, ray_steps_i, light_step,
                                   light_step_multiplier, light_steps_i, sun_col, sun_int_eff,
                                   albedo, amb_col, ambient_strength, phase0, phase1, powder_scale,
                                   multi_scatter_extinction, multi_scatter_strength, jitter,
                                   day_night, storm_f, flash, transmittance, scattered,
                                   dens_sum, used);
    transmittance = o.trans;
    scattered = o.scattered;
    dens_sum = o.dens_sum;
    used = o.used;
  }

  if (high_cloud_enable && transmittance > 0.005f) {
    FcCloudOut o = fc_trace_family(ro, rd, sun, radius, high_cloud_base,
                                   max(high_cloud_thickness, 0.01f), basic_noise, detail_noise,
                                   weather_map, curl_noise, time_seconds, uv_scale,
                                   coverage * fc_cov(high_cloud_type, 0) * (1.0f + 0.4f * overcast),
                                   density * fc_den(high_cloud_type, 0) * (1.0f + 0.6f * overcast),
                                   wind_direction.xy, cloud_speed,
                                   basic_noise_scale * fc_bsc(high_cloud_type, 0),
                                   detail_noise_scale * fc_dsc(high_cloud_type, 0),
                                   max_trace_distance, trace_start_limit, ray_steps_i, light_step,
                                   light_step_multiplier, light_steps_i, sun_col, sun_int_eff,
                                   albedo, amb_col, ambient_strength, phase0, phase1, powder_scale,
                                   multi_scatter_extinction, multi_scatter_strength, jitter,
                                   day_night, storm_f, flash, transmittance, scattered,
                                   dens_sum, used);
    transmittance = o.trans;
    scattered = o.scattered;
    dens_sum = o.dens_sum;
    used = o.used;
  }

  if (vertical_cloud_enable && transmittance > 0.005f) {
    FcCloudOut o = fc_trace_family(ro, rd, sun, radius, vertical_cloud_base,
                                   max(vertical_cloud_top - vertical_cloud_base, 0.01f),
                                   basic_noise, detail_noise, weather_map, curl_noise,
                                   time_seconds, uv_scale,
                                   coverage * fc_cov(vertical_cloud_type, 3) * cov_boost,
                                   density * fc_den(vertical_cloud_type, 3) * den_boost,
                                   wind_direction.xy, cloud_speed,
                                   basic_noise_scale * fc_bsc(vertical_cloud_type, 3),
                                   detail_noise_scale * fc_dsc(vertical_cloud_type, 3),
                                   max_trace_distance, trace_start_limit, ray_steps_i, light_step,
                                   light_step_multiplier, light_steps_i, sun_col, sun_int_eff,
                                   albedo, amb_col, ambient_strength, phase0, phase1, powder_scale,
                                   multi_scatter_extinction, multi_scatter_strength, jitter,
                                   day_night, storm_f, flash, transmittance, scattered,
                                   dens_sum, used);
    transmittance = o.trans;
    scattered = o.scattered;
    dens_sum = o.dens_sum;
    used = o.used;
  }

  if (special_cloud_enable && transmittance > 0.005f) {
    FcCloudOut o = fc_trace_family(ro, rd, sun, radius, special_cloud_base,
                                   max(special_cloud_thickness, 0.01f), basic_noise, detail_noise,
                                   weather_map, curl_noise, time_seconds, uv_scale,
                                   coverage * fc_cov(special_cloud_type, 4) * cov_boost,
                                   density * fc_den(special_cloud_type, 4) * den_boost,
                                   wind_direction.xy, cloud_speed,
                                   basic_noise_scale * fc_bsc(special_cloud_type, 4),
                                   detail_noise_scale * fc_dsc(special_cloud_type, 4),
                                   max_trace_distance, trace_start_limit, ray_steps_i, light_step,
                                   light_step_multiplier, light_steps_i, sun_col, sun_int_eff,
                                   albedo, amb_col, ambient_strength, phase0, phase1, powder_scale,
                                   multi_scatter_extinction, multi_scatter_strength, jitter,
                                   day_night, storm_f, flash, transmittance, scattered,
                                   dens_sum, used);
    transmittance = o.trans;
    scattered = o.scattered;
    dens_sum = o.dens_sum;
    used = o.used;
  }

  out_cloud_alpha = 1.0f - transmittance;
  out_cloud_density = dens_sum / max(used, 1.0f);
  out_cloud_transmittance = transmittance;

  float3 result = sky * transmittance + scattered;

  /* 闪电照亮整个环境。 */
  result += float3(0.75f, 0.82f, 1.0f) * flash * 0.35f;

  float night_factor = 1.0f - smoothstep(-0.15f, 0.02f, sun.z - horizon_offset);

  float3 stars = fc_stars(rd, night_factor * step(horizon_offset, rd.z), latitude, longitude,
                          time_seconds, star_density, star_brightness);
  float3 aurora = float3(0.0f);
  if (aurora_enabled) {
    aurora = fc_aurora(rd, night_factor * step(horizon_offset, rd.z), time_seconds,
                       aurora_intensity, aurora_speed, aurora_color_factor);
  }
  result += (stars + aurora) * transmittance;

  /* 月亮：太阳落到地平线以下才显示。 */
  if (moon_enable && night_factor > 0.001f) {
    float3 md = fc_safe_normalize(fc_calc_sun_direction(moon_height, moon_rotation));
    float atm_ext = exp(-max(air_density, 0.0f) * 0.15f / max(rd.z, 0.05f));
    float3 moon = fc_moon(rd, md, moon_size, float3(moon_intensity), moon_tex, atm_ext);
    result += moon * transmittance * night_factor;
  }

  /* 屏幕空间：雨丝 / 雪 / 镜头雨。 */
  float3 window = coordinate_screen(g_data.P);
  float2 screen_uv = window.xy;
  if (window.x == 0.0f && window.y == 0.0f) {
    screen_uv = float2(atan(rd.y, rd.x) / (2.0f * M_PI) + 0.5f, rd.z * 0.5f + 0.5f);
  }
  float sky_mask = smoothstep(-0.05f, 0.1f, rd.z);

  float rain_amt = fc_rain_streaks(screen_uv, time_seconds, rain_intensity, sky_mask) *
                   w_rain;
  result += float3(0.55f, 0.68f, 0.85f) * rain_amt * 1.6f;

  float snow_amt = fc_snow_flakes(screen_uv, time_seconds, snow_intensity, sky_mask) * w_snow;
  result += float3(0.9f, 0.95f, 1.0f) * snow_amt * 1.5f;

  float lens = 0.0f;
  if (lens_rain_enable) {
    lens = fc_lens_rain(screen_uv, time_seconds, lens_rain_strength, lens_rain_length) *
           (w_rain + w_thunder);
    result *= 1.0f - 0.18f * lens;
    result += float3(0.55f, 0.65f, 0.75f) * lens * 0.35f;
  }

  result *= max(exposure, 0.0f);
  float3 hsv = fc_rgb_to_hsv(max(result, float3(0.0f)));
  hsv.x = fract(hsv.x + hue - 0.5f);
  hsv.y = fc_sat(hsv.y * max(saturation, 0.0f));
  hsv.z = max(hsv.z * max(brightness, 0.0f), 0.0f);
  result = fc_hsv_to_rgb(hsv);

  color = float4(max(result, float3(0.0f)), 1.0f);
}

/** \} */
