/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"
#include "gpu_shader_material_interface.bsl.hh"

static float fc_sat(float x) { return clamp(x, 0.0f, 1.0f); }
static float fc_remap(float v, float a0, float a1, float b0, float b1)
{
  return b0 + fc_sat((v - a0) / max(a1 - a0, 1.0e-5f)) * (b1 - b0);
}
static float3 fc_safe_normalize_dir(float3 v)
{
  float l = length(v);
  return l > 1.0e-6f ? v / l : float3(0.0f, 0.0f, 1.0f);
}
static float fc_hash13(float3 p)
{
  p = fract(p * float3(0.1031f, 0.11369f, 0.13787f));
  p += dot(p, p.yzx + 19.19f);
  return fract((p.x + p.y) * p.z);
}
static float fc_hash(float n) { return fract(sin(n) * 758.5453f); }
static float fc_cloud_noise(float3 p)
{
  float3 i = floor(p);
  float3 f = fract(p);
  f = f * f * (3.0f - 2.0f * f);
  float n000 = fc_hash13(i);
  float n100 = fc_hash13(i + float3(1.0f, 0.0f, 0.0f));
  float n010 = fc_hash13(i + float3(0.0f, 1.0f, 0.0f));
  float n110 = fc_hash13(i + float3(1.0f, 1.0f, 0.0f));
  float n001 = fc_hash13(i + float3(0.0f, 0.0f, 1.0f));
  float n101 = fc_hash13(i + float3(1.0f, 0.0f, 1.0f));
  float n011 = fc_hash13(i + float3(0.0f, 1.0f, 1.0f));
  float n111 = fc_hash13(i + float3(1.0f, 1.0f, 1.0f));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
             f.z);
}
static float fc_cloud_fbm(float3 p, int octaves)
{
  float a = 0.5f;
  float r = 0.0f;
  for (int i = 0; i < 6; i++) {
    if (i >= octaves) { break; }
    r += a * fc_cloud_noise(p);
    p *= 2.02f;
    a *= 0.5f;
  }
  return r;
}
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
static float3 fc_sun_dir(float h_deg, float r_deg)
{
  float h = radians(h_deg);
  float r = radians(r_deg);
  return float3(cos(h) * cos(r), cos(h) * sin(r), sin(h));
}
static float3 fc_sun_disc(float3 rd, float3 sd, float3 sc, float si, float ss, float bi, float hh)
{
  float ho = sin(radians(hh));
  if (rd.z < ho) { return float3(0.0f); }
  float ang = acos(clamp(dot(rd, sd), -1.0f, 1.0f));
  float da = ss * 0.004f;
  float ba = da * (4.0f + bi * 6.0f);
  float disc = smoothstep(da, 0.0f, ang);
  float bloom = smoothstep(ba, 0.0f, ang);
  float b = pow(disc, 0.2f) * 1.2f + pow(bloom, 3.0f) * (1.0f - disc) * bi * 0.5f;
  float fade = clamp((sd.z - ho) / (da * 3.0f) + 0.5f, 0.0f, 1.0f);
  return pow(sc, float3(0.7f)) * si * b * 2.0f * fade;
}
static float3 fc_atmosphere(float3 rd, float3 sd, float ad, float ac, float oz, float so2, float pm, float sgs, float si, float steps_f)
{
  rd = normalize(rd);
  sd = normalize(sd);
  float ct = max(dot(rd, sd), 0.0f);
  float zenith = max(rd.z, 0.0f);
  float shf = smoothstep(-0.3f, 0.05f, sd.z);
  float rf = (3.0f / (16.0f * M_PI)) * (1.0f + ct * ct);
  float g = 0.76f;
  float mf = (3.0f / (8.0f * M_PI)) * ((1.0f - g * g) * (1.0f + ct * ct)) /
             ((2.0f + g * g) * pow(1.0f + g * g - 2.0f * g * ct, 1.5f));
  float3 kR = float3(5.5e-6f, 13.0e-6f, 22.4e-6f) * ad;
  float kM = 21e-6f * (0.5f + ac * 0.5f);
  float shR = 8.0f;
  float shM = 1.2f;
  float am = 1.0f / max(zenith, 1.0f / 40.0f);
  float steps = max(steps_f, 1.0f);
  float ds = am / steps;
  float3 tR = float3(0.0f);
  float3 tM = float3(0.0f);
  float odR = 0.0f;
  float odM = 0.0f;
  for (int i = 0; i < 32; i++) {
    if (float(i) >= steps) { break; }
    float ih = max(float(i) * ds * shR, 0.0f);
    float sR = exp(-ih / shR) * ds;
    float sM = exp(-ih / shM) * ds;
    odR += sR;
    odM += sM;
    float js = max(0.005f, (1.0f + sd.z * 3.0f) / steps);
    float jR = 0.0f;
    float jM = 0.0f;
    for (int j = 0; j < 16; j++) {
      if (float(j) >= steps) { break; }
      float jh = max(ih + float(j) * js * shR * max(0.0f, sd.z), 0.0f);
      jR += exp(-jh / shR) * js;
      jM += exp(-jh / shM) * js;
    }
    float sumR = min(odR + jR, 20.0f);
    float sumM = min(odM + jM, 20.0f);
    float3 attn = exp(-(kM * sumM + kR * sumR * 1000.0f));
    tR += sR * attn;
    tM += sM * attn;
  }
  float3 sky = si * 22.0f * (rf * kR * tR + mf * kM * tM) * 1000.0f;
  float oza = exp(-oz * 0.0015f * am);
  sky *= float3(1.0f, oza, oza * 0.95f);
  sky += float3(0.15f, 0.12f, 0.05f) * (so2 * 0.3f) * (1.0f - zenith);
  sky += float3(pm * 0.12f, pm * 0.11f, pm * 0.08f) * (1.0f - zenith * zenith);
  float hf = exp(-ad * (1.0f - zenith) * 5.0f);
  sky += float3(0.1f, 0.15f, 0.25f) * ad * (1.0f - hf) * 0.5f;
  float sg = smoothstep(0.998f, 1.0f, ct);
  float sn = smoothstep(0.995f, 1.0f, ct);
  sky *= 1.0f - sn * sgs * 0.6f;
  sky *= shf;
  return sky + float3(1.0f, 0.9f, 0.7f) * sg * 3.0f;
}
static float3 fc_stars(float3 rd, float nf, float lat, float lon, float time_s, float density, float brightness)
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
    if (i >= count) { break; }
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
static float fc_cloud_map(float3 pos_km, float h01, float time_s, float cov, float den, float2 wind, float speed, float nscale, float ndetail)
{
  float3 rel = pos_km - float3(0.0f, 0.0f, 6360.0f);
  float3 wdir = fc_safe_normalize_dir(float3(wind.x, 0.0f, wind.y));
  float3 rel_m = rel * 1000.0f + wdir * h01 * 500.0f;
  rel = rel_m * 0.001f;
  float3 woff = (wdir + float3(0.0f, 0.1f, 0.0f)) * time_s * speed;
  float weather = fc_cloud_fbm(float3(rel.x, 0.0f, rel.y) * 0.05f, 2);
  float curl = fc_cloud_noise(float3(rel.x * 0.02f + time_s * speed * 0.05f, rel.y * 0.02f, 0.5f));
  float local_cov = fc_sat(curl * 3.0f - 0.75f) * 0.2f;
  float coverage = fc_sat(cov * (local_cov + weather));
  float grad = fc_remap(h01, 0.0f, 0.1f, 0.1f, 1.0f) * fc_remap(h01, 0.1f, 0.8f, 1.0f, 0.2f);
  float basic = fc_cloud_fbm((rel + woff) * nscale, 4);
  float basic_cov = coverage * fc_remap(grad * basic, 1.0f - coverage, 1.0f, 0.0f, 1.0f);
  float3 dpos = rel - woff * 0.15f;
  float detail = fc_cloud_noise(dpos * nscale * 4.0f);
  float dmix = 0.2f * mix(detail, 1.0f - detail, fc_sat(h01 * 10.0f));
  float shape = fc_sat(0.01f + h01 * 1.15f) * den * fc_remap(h01, 0.0f, 0.1f, 0.0f, 1.0f) * fc_remap(h01, 0.8f, 1.0f, 1.0f, 0.0f);
  return max(0.0f, fc_remap(basic_cov, dmix, 1.0f, 0.0f, 1.0f) * shape);
}
static float fc_light_trans(float3 pos_km, float3 sun, float radius, float base, float thick, float time_s, float cov, float den, float2 wind, float speed, float nscale, float ndetail, float step, float mult, int steps, float ext)
{
  float od = 0.0f;
  float s = max(step, 1.0e-4f);
  float d = s * 0.5f;
  int n = clamp(steps, 1, 16);
  for (int i = 0; i < 16; i++) {
    if (i >= n) { break; }
    float3 sp = pos_km + sun * d;
    float alt = length(sp) - radius;
    float h01 = (alt - base) / max(thick, 1.0e-4f);
    if (h01 >= 0.0f && h01 <= 1.0f) { od += fc_cloud_map(sp, h01, time_s, cov, den, wind, speed, nscale, ndetail) * s * ext; }
    d += s;
    s *= max(mult, 1.0f);
  }
  return exp(-od * 1000.0f);
}
struct FcCloudAccum { float trans; float3 scattered; float dens_sum; float used; };
struct FcInterval { float t0; float t1; bool hit; };
static FcInterval fc_interval(float3 ro, float3 rd, float r)
{
  FcInterval iv;
  float b = dot(ro, rd);
  float c = dot(ro, ro) - r * r;
  float h = b * b - c;
  if (h <= 0.0f) { iv.t0 = 0.0f; iv.t1 = 0.0f; iv.hit = false; return iv; }
  h = sqrt(h);
  iv.t0 = -b - h;
  iv.t1 = -b + h;
  iv.hit = iv.t1 > 0.0f;
  return iv;
}
static FcCloudAccum fc_trace_cloud(float3 ro, float3 rd, float3 sun, float radius, float base, float thick, float type, int fam, float cov_g, float den_g, float nscale, float ndetail, float2 wind, float speed, float time_s, float jitter, float si, float3 sun_col, float3 albedo, float3 amb_col, float amb_str, float p0, float p1, float powder, float ms_ext, float ms_str, float day_night, float max_dist, int ray_steps, float lstep, float lmult, int lsteps, float trans_in, float3 scattered_in, float dens_sum_in, float used_in)
{
  FcCloudAccum acc;
  acc.trans = trans_in; acc.scattered = scattered_in; acc.dens_sum = dens_sum_in; acc.used = used_in;
  FcInterval inner = fc_interval(ro, rd, radius + base);
  FcInterval outer = fc_interval(ro, rd, radius + base + thick);
  float st = inner.hit ? max(inner.t1, 0.0f) : max(outer.t0, 0.0f);
  float et = min(outer.t1, st + max_dist);
  if (!outer.hit || et <= st || rd.z <= -0.02f) { return acc; }
  float lc = cov_g * fc_cov(type, fam);
  float ld = den_g * fc_den(type, fam);
  float ls = nscale * fc_bsc(type, fam);
  float ds = ndetail * fc_dsc(type, fam);
  float sl = (et - st) / float(ray_steps);
  float t = st + sl * jitter;
  for (int i = 0; i < 128; i++) {
    if (i >= ray_steps || t > et || acc.trans < 0.005f) { break; }
    float3 pos = ro + rd * t;
    float alt = length(pos) - radius;
    float h01 = (alt - base) / max(thick, 1.0e-4f);
    float d = fc_cloud_map(pos, h01, time_s, lc, ld, wind, speed, ls, ds);
    if (d > 1.0e-5f) {
      float lt0 = fc_light_trans(pos, sun, radius, base, thick, time_s, lc, ld, wind, speed, ls, ds, lstep, lmult, lsteps, 1.0f);
      float lt1 = fc_light_trans(pos, sun, radius, base, thick, time_s, lc, ld, wind, speed, ls, ds, lstep, lmult, lsteps, ms_ext);
      float pd = pow(fc_sat(d * 8.0f), fc_remap(h01, 0.3f, 0.85f, 0.5f, 2.0f));
      float pw = sqrt(max(pd * 20.0f, 0.0f)) * powder;
      float3 am = amb_col * max(amb_str, 0.0f) * fc_sat(0.5f + h01) * (0.2f + day_night * 0.8f);
      float3 l0 = sun_col * si * day_night * lt0 * p0 * pw + am;
      float3 l1 = sun_col * si * day_night * lt1 * p1 * pw + am * ms_str;
      float s0 = max(d, 1.0e-6f);
      float s1 = max(d * ms_ext, 1.0e-6f);
      float st0 = exp(-s0 * sl * 1000.0f);
      float st1 = exp(-s1 * sl * 1000.0f);
      float3 sc0 = albedo * d * l0;
      float3 sc1 = albedo * d * ms_str * l1;
      acc.scattered += acc.trans * ((sc0 - sc0 * st0) / s0 + (sc1 - sc1 * st1) / s1);
      acc.trans *= st0;
      acc.dens_sum += d;
    }
    acc.used += 1.0f;
    t += sl;
  }
  return acc;
}
static float fc_lightning_flash(float time, float frequency)
{
  if (frequency < 0.01f) { return 0.0f; }
  float lightning_interval = 5.0f / frequency;
  float time_since_last = mod(time, lightning_interval);
  float flash_duration = 0.05f + frequency * 0.05f;
  float flash_decay = smoothstep(0.0f, flash_duration, time_since_last) * (1.0f - smoothstep(flash_duration, flash_duration * 2.0f, time_since_last));
  return flash_decay * (0.8f + fract(sin(time * 1000.0f) * 43758.5453f) * 0.4f);
}
static float fc_rain_drop(float2 uv, float time, float intensity)
{
  float drop_speed = 8.0f + intensity * 12.0f;
  float drop_length = 0.01f + intensity * 0.015f;
  float drop_width = 0.0015f + intensity * 0.001f;
  float hash = fract(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453f);
  float drop_t = mod(time * drop_speed + hash * 100.0f, 1.0f);
  float drop_y = uv.y + drop_t - 1.0f;
  float drop_x = uv.x + sin(hash * 6.283f) * 0.02f;
  float drop_dist = abs(drop_x - uv.x) / drop_width;
  float drop = exp(-drop_dist * drop_dist * 50.0f);
  drop *= smoothstep(0.0f, drop_length, -drop_y) * smoothstep(-drop_length, 0.0f, -drop_y);
  return drop * intensity * 0.3f;
}
static float fc_snow_flake(float2 uv, float time, float intensity)
{
  float flake_speed = 1.5f + intensity * 2.0f;
  float flake_size = 0.003f + intensity * 0.005f;
  float hash = fract(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453f);
  float flake_t = mod(time * flake_speed + hash * 100.0f, 1.0f);
  float flake_y = uv.y + flake_t - 1.0f;
  float flake_x = uv.x + sin(time * 0.5f + hash * 6.283f) * 0.03f;
  float flake_dist = length(float2(flake_x - uv.x, flake_y));
  float flake = exp(-flake_dist * flake_dist / (flake_size * flake_size));
  return flake * intensity * 0.4f;
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
                 mix(fc_aurora_hash(n + 857.0f), fc_aurora_hash(n + 858.0f), f.x), f.y), f.z);
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
  return a < 0.0f ? 0.0f : a;
}
static float3 fc_aurora(float3 rd, float nf, float time_s, float intensity, float speed, float color_factor)
{
  if (nf < 0.001f) { return float3(0.0f); }
  float3 aurora = float3(0.0f);
  float3 rotated_rd = float3(rd.x, rd.z, -rd.y);
  aurora += float3(0.2f, 0.8f, 0.3f) * fc_get_aurora(float3(rotated_rd.xy * float2(1.2f, 1.0f), time_s * speed * 0.22f)) * 0.9f;
  aurora += float3(0.3f, 0.6f, 0.9f) * fc_get_aurora(float3(rotated_rd.xy * float2(1.0f, 0.7f), time_s * speed * 0.27f)) * 0.9f;
  aurora += float3(0.5f, 0.3f, 0.8f) * fc_get_aurora(float3(rotated_rd.xy * float2(0.8f, 0.6f), time_s * speed * 0.29f)) * 0.5f;
  aurora += float3(0.4f, 0.5f, 0.7f) * fc_get_aurora(float3(rotated_rd.xy * float2(0.9f, 0.5f), time_s * speed * 0.20f)) * 0.57f;
  return aurora * intensity * nf;
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


[[node]]
void node_esky(
  float3 view_dir,
  float time_seconds,
  float sample_index,
  float camera_height,
  float planet_radius,
  float latitude,
  float longitude,
  float star_density,
  float star_brightness,
  bool aurora_enabled,
  float aurora_intensity,
  float aurora_speed,
  float aurora_color_factor,
  bool use_sun_system,
  float sun_height,
  float sun_rotation,
  float sun_temperature,
  float sun_size,
  float sun_bloom,
  float horizon_height,
  bool use_atmosphere,
  float air_density,
  float aerosol_concentration,
  float ozone_amount,
  float so2_concentration,
  float pm25_concentration,
  float sun_glow_suppress,
  float atmosphere_steps,
  float4 zenith_color,
  float4 horizon_color,
  bool enable_high_cloud,
  float high_cloud_base,
  float high_cloud_thickness,
  float high_cloud_type,
  bool enable_mid_cloud,
  float mid_cloud_base,
  float mid_cloud_thickness,
  float mid_cloud_type,
  bool enable_low_cloud,
  float low_cloud_base,
  float low_cloud_thickness,
  float low_cloud_type,
  bool enable_vertical_cloud,
  float vertical_cloud_base,
  float vertical_cloud_top,
  float vertical_cloud_type,
  bool enable_special_cloud,
  float special_cloud_type,
  float special_cloud_base,
  float special_cloud_thickness,
  float weather_uv_scale,
  float coverage,
  float density,
  float basic_noise_scale,
  float detail_noise_scale,
  float wind_direction_x,
  float wind_direction_y,
  float cloud_speed,
  float weather_state,
  float rain_intensity,
  float snow_intensity,
  float lightning_frequency,
  float sun_intensity,
  float4 cloud_albedo,
  float4 ambient_color,
  float ambient_strength,
  float phase_forward,
  float phase_backward,
  float phase_mix,
  float powder_scale,
  float multi_scatter_extinction,
  float multi_scatter_strength,
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
  out float4 color,
  out float out_cloud_alpha,
  out float out_cloud_density,
  out float out_cloud_transmittance)
{
  int ray_steps_i = int(ray_steps);
  int light_steps_i = int(light_steps);
  float2 uv_scale = float2(weather_uv_scale, weather_uv_scale);
  float2 wind_direction = float2(wind_direction_x, wind_direction_y);

  float3 rd = fc_safe_normalize(-view_dir);
  float3 sun = fc_safe_normalize(fc_calc_sun_direction(sun_height, sun_rotation));
  float3 final_sun_color = fc_blackbody(sun_temperature);
  
  float horizon_rad = radians(horizon_height);
  float horizon_offset = sin(horizon_rad);
  float sun_height_above_horizon = sun.z - horizon_offset;
  float day_night_factor = smoothstep(-0.15f, 0.05f, sun_height_above_horizon);
  
  float radius = max(planet_radius, 100.0f);
  float3 ro = float3(0.0f, 0.0f, radius + max(camera_height, 0.0f));
  float3 sky = fc_sky(rd, sun, zenith_color.rgb, horizon_color.rgb, final_sun_color, sun_intensity, sun_size, sun_bloom, air_density, aerosol_concentration, ozone_amount, so2_concentration, pm25_concentration, sun_glow_suppress, atmosphere_steps, use_atmosphere ? 1.0f : 0.0f, horizon_height);

  float view_light_cosine = dot(rd, sun);
  float phase0 = fc_dual_lobe(phase_forward, phase_backward, phase_mix, -view_light_cosine);
  float phase1 = mix(1.0f / (4.0f * M_PI), phase0, 0.5f);

  float2 blue_uv = fract(float2(atan(rd.y, rd.x) / (2.0f * M_PI) + 0.5f, rd.z * 0.5f + 0.5f) * 64.0f + float2(sample_index * 0.754877669f, sample_index * 0.569840296f));
  float jitter = texture(blue_noise, blue_uv).r;

  float transmittance = 1.0f;
  float3 scattered_light = float3(0.0f);
  float density_sum = 0.0f;
  float used_steps = 0.0f;

  if (enable_low_cloud && transmittance > 0.005f) {
    float lb_low = low_cloud_base;
    float lt_low = max(low_cloud_thickness, 0.01f);
    float lc_low = coverage * fc_cloud_param_coverage(low_cloud_type, 2);
    float ld_low = density * fc_cloud_param_density(low_cloud_type, 2);
    float ls_low = basic_noise_scale * fc_cloud_param_basic_scale(low_cloud_type, 2);
    float ds_low = detail_noise_scale * fc_cloud_param_detail_scale(low_cloud_type, 2);
    
    float in_low, if_low, on_low, of_low;
    bool ih_low = fc_sphere_interval(ro, rd, radius + lb_low, in_low, if_low);
    bool oh_low = fc_sphere_interval(ro, rd, radius + lb_low + lt_low, on_low, of_low);
    float st_low = ih_low ? max(if_low, 0.0f) : max(on_low, 0.0f);
    float et_low = min(of_low, st_low + max(max_trace_distance, 1.0f));
    
    if (oh_low && et_low > st_low && st_low <= max(trace_start_limit, 1.0f) && rd.z > -0.02f) {
      int stps_low = clamp(ray_steps_i, 16, 128);
      float sl_low = (et_low - st_low) / float(stps_low);
      float t_low = st_low + sl_low * jitter;
      
      for (int i = 0; i < 128; ++i) {
        if (i >= stps_low || t_low > et_low || transmittance < 0.005f) { break; }
        float3 pos_low = ro + rd * t_low;
        float alt_low = length(pos_low) - radius;
        float h01_low = (alt_low - lb_low) / max(lt_low, 1.0e-4f);
        
        float den_low = fc_cloud_map(pos_low, h01_low, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_low, ld_low, wind_direction, cloud_speed, ls_low, ds_low);
        
        if (den_low > 1.0e-5f) {
          float lt0_low = fc_light_transmittance(pos_low, sun, radius, lb_low, lt_low, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_low, ld_low, wind_direction, cloud_speed, ls_low, ds_low, light_step, light_step_multiplier, light_steps_i, 1.0f);
          float lt1_low = fc_light_transmittance(pos_low, sun, radius, lb_low, lt_low, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_low, ld_low, wind_direction, cloud_speed, ls_low, ds_low, light_step, light_step_multiplier, light_steps_i, multi_scatter_extinction);
          float pd_low = pow(fc_sat(den_low * 8.0f), fc_remap(h01_low, 0.3f, 0.85f, 0.5f, 2.0f));
          float pw_low = sqrt(max(pd_low * 20.0f, 0.0f)) * powder_scale;
          float3 at_low = float3(1.0f);
          if (use_atmosphere) {
            float mt_low = aerosol_concentration + so2_concentration * 0.5f + pm25_concentration * 0.3f;
            at_low = mix(float3(1.0f), float3(0.9f, 0.85f, 0.7f) + float3(0.1f, 0.1f, -0.2f) * so2_concentration, mt_low);
            float ot_low = (ozone_amount - 1.0f) * 0.3f;
            at_low += float3(0.0f, ot_low, ot_low * 2.0f);
            float pt_low = pm25_concentration * 0.15f;
            at_low += float3(0.0f, pt_low, pt_low * 2.0f);
          }
          
          float3 am_low = ambient_color.rgb * max(ambient_strength, 0.0f) * fc_sat(0.5f + h01_low) * (0.2f + day_night_factor * 0.8f);
          float3 l0_low = final_sun_color * at_low * sun_intensity * day_night_factor * lt0_low * phase0 * pw_low + am_low;
          float3 l1_low = final_sun_color * at_low * sun_intensity * day_night_factor * lt1_low * phase1 * pw_low + am_low * multi_scatter_strength;
          float sig0_low = max(den_low, 1.0e-6f);
          float sig1_low = max(den_low * multi_scatter_extinction, 1.0e-6f);
          float st0_low = exp(-sig0_low * sl_low * 1000.0f);
          float st1_low = exp(-sig1_low * sl_low * 1000.0f);
          float3 sc0_low = cloud_albedo.rgb * den_low * l0_low;
          float3 sc1_low = cloud_albedo.rgb * den_low * multi_scatter_strength * l1_low;
          scattered_light += transmittance * ((sc0_low - sc0_low * st0_low) / sig0_low + (sc1_low - sc1_low * st1_low) / sig1_low);
          transmittance *= st0_low;
          density_sum += den_low;
        }
        used_steps += 1.0f;
        t_low += sl_low;
      }
    }
  }

  if (enable_mid_cloud && transmittance > 0.005f) {
    float lb_mid = mid_cloud_base;
    float lt_mid = max(mid_cloud_thickness, 0.01f);
    float lc_mid = coverage * fc_cloud_param_coverage(mid_cloud_type, 1);
    float ld_mid = density * fc_cloud_param_density(mid_cloud_type, 1);
    float ls_mid = basic_noise_scale * fc_cloud_param_basic_scale(mid_cloud_type, 1);
    float ds_mid = detail_noise_scale * fc_cloud_param_detail_scale(mid_cloud_type, 1);
    
    float in_mid, if_mid, on_mid, of_mid;
    bool ih_mid = fc_sphere_interval(ro, rd, radius + lb_mid, in_mid, if_mid);
    bool oh_mid = fc_sphere_interval(ro, rd, radius + lb_mid + lt_mid, on_mid, of_mid);
    float st_mid = ih_mid ? max(if_mid, 0.0f) : max(on_mid, 0.0f);
    float et_mid = min(of_mid, st_mid + max(max_trace_distance, 1.0f));
    
    if (oh_mid && et_mid > st_mid && st_mid <= max(trace_start_limit, 1.0f) && rd.z > -0.02f) {
      int stps_mid = clamp(ray_steps_i, 16, 128);
      float sl_mid = (et_mid - st_mid) / float(stps_mid);
      float t_mid = st_mid + sl_mid * jitter;
      
      for (int i = 0; i < 128; ++i) {
        if (i >= stps_mid || t_mid > et_mid || transmittance < 0.005f) { break; }
        float3 pos_mid = ro + rd * t_mid;
        float alt_mid = length(pos_mid) - radius;
        float h01_mid = (alt_mid - lb_mid) / max(lt_mid, 1.0e-4f);
        
        float den_mid = fc_cloud_map(pos_mid, h01_mid, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_mid, ld_mid, wind_direction, cloud_speed, ls_mid, ds_mid);
        
        if (den_mid > 1.0e-5f) {
          float lt0_mid = fc_light_transmittance(pos_mid, sun, radius, lb_mid, lt_mid, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_mid, ld_mid, wind_direction, cloud_speed, ls_mid, ds_mid, light_step, light_step_multiplier, light_steps_i, 1.0f);
          float lt1_mid = fc_light_transmittance(pos_mid, sun, radius, lb_mid, lt_mid, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_mid, ld_mid, wind_direction, cloud_speed, ls_mid, ds_mid, light_step, light_step_multiplier, light_steps_i, multi_scatter_extinction);
          float pd_mid = pow(fc_sat(den_mid * 8.0f), fc_remap(h01_mid, 0.3f, 0.85f, 0.5f, 2.0f));
          float pw_mid = sqrt(max(pd_mid * 20.0f, 0.0f)) * powder_scale;
          float3 at_mid = float3(1.0f);
          if (use_atmosphere) {
            float mt_mid = aerosol_concentration + so2_concentration * 0.5f + pm25_concentration * 0.3f;
            at_mid = mix(float3(1.0f), float3(0.9f, 0.85f, 0.7f) + float3(0.1f, 0.1f, -0.2f) * so2_concentration, mt_mid);
          }
          float3 am_mid = ambient_color.rgb * max(ambient_strength, 0.0f) * fc_sat(0.5f + h01_mid) * (0.2f + day_night_factor * 0.8f);
          float3 l0_mid = final_sun_color * at_mid * sun_intensity * day_night_factor * lt0_mid * phase0 * pw_mid + am_mid;
          float3 l1_mid = final_sun_color * at_mid * sun_intensity * day_night_factor * lt1_mid * phase1 * pw_mid + am_mid * multi_scatter_strength;
          float sig0_mid = max(den_mid, 1.0e-6f);
          float sig1_mid = max(den_mid * multi_scatter_extinction, 1.0e-6f);
          float st0_mid = exp(-sig0_mid * sl_mid * 1000.0f);
          float st1_mid = exp(-sig1_mid * sl_mid * 1000.0f);
          float3 sc0_mid = cloud_albedo.rgb * den_mid * l0_mid;
          float3 sc1_mid = cloud_albedo.rgb * den_mid * multi_scatter_strength * l1_mid;
          scattered_light += transmittance * ((sc0_mid - sc0_mid * st0_mid) / sig0_mid + (sc1_mid - sc1_mid * st1_mid) / sig1_mid);
          transmittance *= st0_mid;
          density_sum += den_mid;
        }
        used_steps += 1.0f;
        t_mid += sl_mid;
      }
    }
  }

  if (enable_high_cloud && transmittance > 0.005f) {
    float lb_high = high_cloud_base;
    float lt_high = max(high_cloud_thickness, 0.01f);
    float lc_high = coverage * fc_cloud_param_coverage(high_cloud_type, 0);
    float ld_high = density * fc_cloud_param_density(high_cloud_type, 0);
    float ls_high = basic_noise_scale * fc_cloud_param_basic_scale(high_cloud_type, 0);
    float ds_high = detail_noise_scale * fc_cloud_param_detail_scale(high_cloud_type, 0);
    
    float in_high, if_high, on_high, of_high;
    bool ih_high = fc_sphere_interval(ro, rd, radius + lb_high, in_high, if_high);
    bool oh_high = fc_sphere_interval(ro, rd, radius + lb_high + lt_high, on_high, of_high);
    float st_high = ih_high ? max(if_high, 0.0f) : max(on_high, 0.0f);
    float et_high = min(of_high, st_high + max(max_trace_distance, 1.0f));
    
    if (oh_high && et_high > st_high && st_high <= max(trace_start_limit, 1.0f) && rd.z > -0.02f) {
      int stps_high = clamp(ray_steps_i, 16, 128);
      float sl_high = (et_high - st_high) / float(stps_high);
      float t_high = st_high + sl_high * jitter;
      
      for (int i = 0; i < 128; ++i) {
        if (i >= stps_high || t_high > et_high || transmittance < 0.005f) { break; }
        float3 pos_high = ro + rd * t_high;
        float alt_high = length(pos_high) - radius;
        float h01_high = (alt_high - lb_high) / max(lt_high, 1.0e-4f);
        
        float den_high = fc_cloud_map(pos_high, h01_high, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_high, ld_high, wind_direction, cloud_speed, ls_high, ds_high);
        
        if (den_high > 1.0e-5f) {
          float lt0_high = fc_light_transmittance(pos_high, sun, radius, lb_high, lt_high, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_high, ld_high, wind_direction, cloud_speed, ls_high, ds_high, light_step, light_step_multiplier, light_steps_i, 1.0f);
          float lt1_high = fc_light_transmittance(pos_high, sun, radius, lb_high, lt_high, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_high, ld_high, wind_direction, cloud_speed, ls_high, ds_high, light_step, light_step_multiplier, light_steps_i, multi_scatter_extinction);
          float pd_high = pow(fc_sat(den_high * 8.0f), fc_remap(h01_high, 0.3f, 0.85f, 0.5f, 2.0f));
          float pw_high = sqrt(max(pd_high * 20.0f, 0.0f)) * powder_scale;
          float3 at_high = float3(1.0f);
          if (use_atmosphere) {
            float mt_high = aerosol_concentration + so2_concentration * 0.5f + pm25_concentration * 0.3f;
            at_high = mix(float3(1.0f), float3(0.9f, 0.85f, 0.7f) + float3(0.1f, 0.1f, -0.2f) * so2_concentration, mt_high);
          }
          float3 am_high = ambient_color.rgb * max(ambient_strength, 0.0f) * fc_sat(0.5f + h01_high) * (0.2f + day_night_factor * 0.8f);
          float3 l0_high = final_sun_color * at_high * sun_intensity * day_night_factor * lt0_high * phase0 * pw_high + am_high;
          float3 l1_high = final_sun_color * at_high * sun_intensity * day_night_factor * lt1_high * phase1 * pw_high + am_high * multi_scatter_strength;
          float sig0_high = max(den_high, 1.0e-6f);
          float sig1_high = max(den_high * multi_scatter_extinction, 1.0e-6f);
          float st0_high = exp(-sig0_high * sl_high * 1000.0f);
          float st1_high = exp(-sig1_high * sl_high * 1000.0f);
          float3 sc0_high = cloud_albedo.rgb * den_high * l0_high;
          float3 sc1_high = cloud_albedo.rgb * den_high * multi_scatter_strength * l1_high;
          scattered_light += transmittance * ((sc0_high - sc0_high * st0_high) / sig0_high + (sc1_high - sc1_high * st1_high) / sig1_high);
          transmittance *= st0_high;
          density_sum += den_high;
        }
        used_steps += 1.0f;
        t_high += sl_high;
      }
    }
  }

  if (enable_vertical_cloud && transmittance > 0.005f) {
    float lb_vt = vertical_cloud_base;
    float lt_vt = max(vertical_cloud_top - vertical_cloud_base, 0.01f);
    float lc_vt = coverage * fc_cloud_param_coverage(vertical_cloud_type, 3);
    float ld_vt = density * fc_cloud_param_density(vertical_cloud_type, 3);
    float ls_vt = basic_noise_scale * fc_cloud_param_basic_scale(vertical_cloud_type, 3);
    float ds_vt = detail_noise_scale * fc_cloud_param_detail_scale(vertical_cloud_type, 3);
    
    float in_vt, if_vt, on_vt, of_vt;
    bool ih_vt = fc_sphere_interval(ro, rd, radius + lb_vt, in_vt, if_vt);
    bool oh_vt = fc_sphere_interval(ro, rd, radius + lb_vt + lt_vt, on_vt, of_vt);
    float st_vt = ih_vt ? max(if_vt, 0.0f) : max(on_vt, 0.0f);
    float et_vt = min(of_vt, st_vt + max(max_trace_distance, 1.0f));
    
    if (oh_vt && et_vt > st_vt && st_vt <= max(trace_start_limit, 1.0f) && rd.z > -0.02f) {
      int stps_vt = clamp(ray_steps_i, 16, 128);
      float sl_vt = (et_vt - st_vt) / float(stps_vt);
      float t_vt = st_vt + sl_vt * jitter;
      
      for (int i = 0; i < 128; ++i) {
        if (i >= stps_vt || t_vt > et_vt || transmittance < 0.005f) { break; }
        float3 pos_vt = ro + rd * t_vt;
        float alt_vt = length(pos_vt) - radius;
        float h01_vt = (alt_vt - lb_vt) / max(lt_vt, 1.0e-4f);
        
        float den_vt = fc_cloud_map(pos_vt, h01_vt, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_vt, ld_vt, wind_direction, cloud_speed, ls_vt, ds_vt);
        
        if (den_vt > 1.0e-5f) {
          float lt0_vt = fc_light_transmittance(pos_vt, sun, radius, lb_vt, lt_vt, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_vt, ld_vt, wind_direction, cloud_speed, ls_vt, ds_vt, light_step, light_step_multiplier, light_steps_i, 1.0f);
          float lt1_vt = fc_light_transmittance(pos_vt, sun, radius, lb_vt, lt_vt, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_vt, ld_vt, wind_direction, cloud_speed, ls_vt, ds_vt, light_step, light_step_multiplier, light_steps_i, multi_scatter_extinction);
          float pd_vt = pow(fc_sat(den_vt * 8.0f), fc_remap(h01_vt, 0.3f, 0.85f, 0.5f, 2.0f));
          float pw_vt = sqrt(max(pd_vt * 20.0f, 0.0f)) * powder_scale;
          float3 at_vt = float3(1.0f);
          if (use_atmosphere) {
            float mt_vt = aerosol_concentration + so2_concentration * 0.5f + pm25_concentration * 0.3f;
            at_vt = mix(float3(1.0f), float3(0.9f, 0.85f, 0.7f) + float3(0.1f, 0.1f, -0.2f) * so2_concentration, mt_vt);
          }
          float3 am_vt = ambient_color.rgb * max(ambient_strength, 0.0f) * fc_sat(0.5f + h01_vt) * (0.2f + day_night_factor * 0.8f);
          float3 l0_vt = final_sun_color * at_vt * sun_intensity * day_night_factor * lt0_vt * phase0 * pw_vt + am_vt;
          float3 l1_vt = final_sun_color * at_vt * sun_intensity * day_night_factor * lt1_vt * phase1 * pw_vt + am_vt * multi_scatter_strength;
          float sig0_vt = max(den_vt, 1.0e-6f);
          float sig1_vt = max(den_vt * multi_scatter_extinction, 1.0e-6f);
          float st0_vt = exp(-sig0_vt * sl_vt * 1000.0f);
          float st1_vt = exp(-sig1_vt * sl_vt * 1000.0f);
          float3 sc0_vt = cloud_albedo.rgb * den_vt * l0_vt;
          float3 sc1_vt = cloud_albedo.rgb * den_vt * multi_scatter_strength * l1_vt;
          scattered_light += transmittance * ((sc0_vt - sc0_vt * st0_vt) / sig0_vt + (sc1_vt - sc1_vt * st1_vt) / sig1_vt);
          transmittance *= st0_vt;
          density_sum += den_vt;
        }
        used_steps += 1.0f;
        t_vt += sl_vt;
      }
    }
  }

  if (enable_special_cloud && transmittance > 0.005f) {
    float lb_sp = special_cloud_base;
    float lt_sp = max(special_cloud_thickness, 0.01f);
    float lc_sp = coverage * fc_cloud_param_coverage(special_cloud_type, 4);
    float ld_sp = density * fc_cloud_param_density(special_cloud_type, 4);
    float ls_sp = basic_noise_scale * fc_cloud_param_basic_scale(special_cloud_type, 4);
    float ds_sp = detail_noise_scale * fc_cloud_param_detail_scale(special_cloud_type, 4);
    
    float in_sp, if_sp, on_sp, of_sp;
    bool ih_sp = fc_sphere_interval(ro, rd, radius + lb_sp, in_sp, if_sp);
    bool oh_sp = fc_sphere_interval(ro, rd, radius + lb_sp + lt_sp, on_sp, of_sp);
    float st_sp = ih_sp ? max(if_sp, 0.0f) : max(on_sp, 0.0f);
    float et_sp = min(of_sp, st_sp + max(max_trace_distance, 1.0f));
    
    if (oh_sp && et_sp > st_sp && st_sp <= max(trace_start_limit, 1.0f) && rd.z > -0.02f) {
      int stps_sp = clamp(ray_steps_i, 16, 128);
      float sl_sp = (et_sp - st_sp) / float(stps_sp);
      float t_sp = st_sp + sl_sp * jitter;
      
      for (int i = 0; i < 128; ++i) {
        if (i >= stps_sp || t_sp > et_sp || transmittance < 0.005f) { break; }
        float3 pos_sp = ro + rd * t_sp;
        float alt_sp = length(pos_sp) - radius;
        float h01_sp = (alt_sp - lb_sp) / max(lt_sp, 1.0e-4f);
        
        float den_sp = fc_cloud_map(pos_sp, h01_sp, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_sp, ld_sp, wind_direction, cloud_speed, ls_sp, ds_sp);
        
        if (den_sp > 1.0e-5f) {
          float lt0_sp = fc_light_transmittance(pos_sp, sun, radius, lb_sp, lt_sp, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_sp, ld_sp, wind_direction, cloud_speed, ls_sp, ds_sp, light_step, light_step_multiplier, light_steps_i, 1.0f);
          float lt1_sp = fc_light_transmittance(pos_sp, sun, radius, lb_sp, lt_sp, basic_noise, detail_noise, weather_map, curl_noise, time_seconds, uv_scale, lc_sp, ld_sp, wind_direction, cloud_speed, ls_sp, ds_sp, light_step, light_step_multiplier, light_steps_i, multi_scatter_extinction);
          float pd_sp = pow(fc_sat(den_sp * 8.0f), fc_remap(h01_sp, 0.3f, 0.85f, 0.5f, 2.0f));
          float pw_sp = sqrt(max(pd_sp * 20.0f, 0.0f)) * powder_scale;
          float3 at_sp = float3(1.0f);
          if (use_atmosphere) {
            float mt_sp = aerosol_concentration + so2_concentration * 0.5f + pm25_concentration * 0.3f;
            at_sp = mix(float3(1.0f), float3(0.9f, 0.85f, 0.7f) + float3(0.1f, 0.1f, -0.2f) * so2_concentration, mt_sp);
          }
          float3 am_sp = ambient_color.rgb * max(ambient_strength, 0.0f) * fc_sat(0.5f + h01_sp) * (0.2f + day_night_factor * 0.8f);
          float3 l0_sp = final_sun_color * at_sp * sun_intensity * day_night_factor * lt0_sp * phase0 * pw_sp + am_sp;
          float3 l1_sp = final_sun_color * at_sp * sun_intensity * day_night_factor * lt1_sp * phase1 * pw_sp + am_sp * multi_scatter_strength;
          float sig0_sp = max(den_sp, 1.0e-6f);
          float sig1_sp = max(den_sp * multi_scatter_extinction, 1.0e-6f);
          float st0_sp = exp(-sig0_sp * sl_sp * 1000.0f);
          float st1_sp = exp(-sig1_sp * sl_sp * 1000.0f);
          float3 sc0_sp = cloud_albedo.rgb * den_sp * l0_sp;
          float3 sc1_sp = cloud_albedo.rgb * den_sp * multi_scatter_strength * l1_sp;
          scattered_light += transmittance * ((sc0_sp - sc0_sp * st0_sp) / sig0_sp + (sc1_sp - sc1_sp * st1_sp) / sig1_sp);
          transmittance *= st0_sp;
          density_sum += den_sp;
        }
        used_steps += 1.0f;
        t_sp += sl_sp;
      }
    }
  }

  out_cloud_alpha = 1.0f - transmittance;
  out_cloud_density = density_sum / max(used_steps, 1.0f);
  out_cloud_transmittance = transmittance;
  float3 result = sky * transmittance + scattered_light;
  
  float is_rain = smoothstep(0.5f, 1.5f, weather_state);
  float is_thunder = smoothstep(1.5f, 2.5f, weather_state);
  float is_snow = smoothstep(2.5f, 3.5f, weather_state);
  
  float lightning = fc_lightning_flash(time_seconds, lightning_frequency * is_thunder);
  if (is_thunder > 0.01f) {
    result += float3(lightning * 0.5f);
    float cloud_bottom_tint = max(0.0f, 1.0f - rd.z) * is_thunder;
    result = mix(result, result + float3(0.3f, 0.05f, 0.0f) * cloud_bottom_tint, is_thunder * 0.3f);
  }
  
  float2 screen_uv = float2(atan(rd.y, rd.x) / (2.0f * M_PI) + 0.5f, rd.z * 0.5f + 0.5f);
  float rain_effect = 0.0f;
  if (is_rain > 0.01f) {
    for (int i = 0; i < 8; ++i) {
      float offset = float(i) * 0.125f;
      rain_effect += fc_rain_drop(screen_uv + float2(offset, 0.0f), time_seconds + offset, rain_intensity * is_rain);
    }
    result += float3(0.3f, 0.5f, 0.8f) * rain_effect * 2.0f;
  }
  
  float snow_effect = 0.0f;
  if (is_snow > 0.01f) {
    for (int i = 0; i < 6; ++i) {
      float offset = float(i) * 0.1667f;
      snow_effect += fc_snow_flake(screen_uv + float2(offset, offset), time_seconds + offset, snow_intensity * is_snow);
    }
    result += float3(0.9f, 0.95f, 1.0f) * snow_effect * 1.5f;
  }
  
  float night_factor_for_stars = smoothstep(-0.05f, -0.2f, sun_height_above_horizon);
  float view_above_horizon_for_stars = step(horizon_offset, rd.z);
  float night_sky_factor = night_factor_for_stars * view_above_horizon_for_stars;
  
  float3 stars = fc_stars(rd, night_sky_factor, latitude, longitude, time_seconds, star_density, star_brightness);
  float3 aurora = float3(0.0f);
  if (aurora_enabled) {
    aurora = fc_aurora(rd, night_sky_factor, time_seconds, aurora_intensity, aurora_speed, aurora_color_factor);
  }
  
  stars *= transmittance;
  aurora *= transmittance;
  result += stars + aurora;
  
  color = float4(max(result, float3(0.0f)), 1.0f);
}
