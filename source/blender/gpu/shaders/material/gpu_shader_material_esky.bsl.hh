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
  float basic = fc_cloud_fbm((rel + woff) * nscale, int(ndetail + 0.5f));
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
               float cam_alt,
               float exposure,
               float hue,
               float saturation,
               float brightness,
               float sun_height,
               float sun_rotation,
               float sun_intensity,
               float sun_temp,
               float sun_size,
               float sun_bloom,
               float horizon_height,
               float planet_radius,
               float star_density,
               float star_brightness,
               float latitude,
               float longitude,
               float aurora_on,
               float aurora_intensity,
               float aurora_speed,
               float aurora_color,
               float atm_on,
               float air_density,
               float aerosol,
               float ozone,
               float so2,
               float pm25,
               float sun_glow_suppress,
               float4 zenith_color,
               float4 horizon_color,
               float atm_steps,
               float weather_scale,
               float coverage,
               float density,
               float basic_scale,
               float detail_scale,
               float wind_x,
               float wind_y,
               float cloud_speed,
               float high_on,
               float high_type,
               float high_base,
               float high_thick,
               float mid_on,
               float mid_type,
               float mid_base,
               float mid_thick,
               float low_on,
               float low_type,
               float low_base,
               float low_thick,
               float vert_on,
               float vert_type,
               float vert_base,
               float vert_top,
               float special_on,
               float special_type,
               float special_base,
               float special_thick,
               float4 cloud_albedo,
               float4 ambient_light,
               float ambient_strength,
               float phase_forward,
               float phase_backward,
               float phase_mix,
               float powder_scale,
               float ms_extinction,
               float ms_strength,
               float rain_intensity,
               float snow_intensity,
               float lightning_freq,
               float max_trace_dist,
               float trace_start,
               float ray_steps_f,
               float light_step,
               float light_mult,
               float light_steps_f,
               float weather_state,
               out float4 color,
               out float out_cloud_alpha,
               out float out_cloud_density,
               out float out_cloud_transmittance)
{
  int rsteps = int(ray_steps_f);
  int lsteps = int(light_steps_f);

  float3 v = view_dir;
#if defined(GPU_FRAGMENT_SHADER) && \
    (defined(MAT_DEFERRED) || defined(MAT_FORWARD) || defined(NPR_SHADER))
  if (dot(v, v) < 1e-8f) {
    v = coordinate_incoming(g_data.P);
  }
#endif
  v = fc_safe_normalize_dir(v);
  float3 rd = -v;

  float3 sun = fc_safe_normalize_dir(fc_sun_dir(sun_height, sun_rotation));
  float3 sun_col = fc_blackbody(sun_temp);
  float ho = sin(radians(horizon_height));
  float sun_above = sun.z - ho;
  float day_night = smoothstep(-0.15f, 0.05f, sun_above);

  float3 sky;
  if (atm_on > 0.5f) {
    sky = fc_atmosphere(rd, sun, air_density, aerosol, ozone, so2, pm25, sun_glow_suppress, sun_intensity, atm_steps);
  }
  else {
    float h = pow(1.0f - fc_sat(rd.z), 1.35f);
    sky = mix(zenith_color.rgb, horizon_color.rgb, h);
  }
  sky *= day_night;

  float sdist = sun.z - ho;
  float sfact = clamp(1.0f - sdist / 0.15f, 0.0f, 1.0f);
  float hblend = smoothstep(-0.05f, 0.05f, rd.z - ho);
  float svis = step(ho, sun.z);
  sky += mix(float3(1.0f, 0.4f, 0.1f), float3(1.0f, 0.7f, 0.3f), sfact) * (sfact * svis * hblend) * sun_intensity * 0.3f;
  if (sun_intensity > 0.0f) {
    sky += fc_sun_disc(rd, sun, sun_col, sun_intensity, sun_size, sun_bloom, horizon_height);
  }

  float nf = smoothstep(-0.05f, -0.2f, sun_above);
  sky += fc_stars(rd, nf * step(ho, rd.z), latitude, longitude, time_seconds, star_density, star_brightness);

  float trans = 1.0f;
  float3 scattered = float3(0.0f);
  float dens_sum = 0.0f;
  float used = 0.0f;

  float3 ro = float3(0.0f, 0.0f, max(planet_radius, 100.0f) + max(cam_alt, 0.0f));
  float jitter = fc_hash(sample_index * 0.754877f + time_seconds * 0.137f);

  float vlc = dot(rd, sun);
  float ph0 = fc_dual_lobe(phase_forward, phase_backward, phase_mix, -vlc);
  float ph1 = mix(1.0f / (4.0f * M_PI), ph0, 0.5f);
  float2 wind = float2(wind_x, wind_y);

  if (high_on > 0.5f) {
    FcCloudAccum acc = fc_trace_cloud(ro, rd, sun, planet_radius, high_base, max(high_thick, 0.01f), high_type, 0, coverage, density, basic_scale, detail_scale, wind, cloud_speed, time_seconds, jitter, sun_intensity, sun_col, cloud_albedo.rgb, ambient_light.rgb, ambient_strength, ph0, ph1, powder_scale, ms_extinction, ms_strength, day_night, max_trace_dist, rsteps, light_step, light_mult, lsteps, trans, scattered, dens_sum, used);
    trans = acc.trans; scattered = acc.scattered; dens_sum = acc.dens_sum; used = acc.used;
  }
  if (mid_on > 0.5f) {
    FcCloudAccum acc = fc_trace_cloud(ro, rd, sun, planet_radius, mid_base, max(mid_thick, 0.01f), mid_type, 1, coverage, density, basic_scale, detail_scale, wind, cloud_speed, time_seconds, jitter, sun_intensity, sun_col, cloud_albedo.rgb, ambient_light.rgb, ambient_strength, ph0, ph1, powder_scale, ms_extinction, ms_strength, day_night, max_trace_dist, rsteps, light_step, light_mult, lsteps, trans, scattered, dens_sum, used);
    trans = acc.trans; scattered = acc.scattered; dens_sum = acc.dens_sum; used = acc.used;
  }
  if (low_on > 0.5f) {
    FcCloudAccum acc = fc_trace_cloud(ro, rd, sun, planet_radius, low_base, max(low_thick, 0.01f), low_type, 2, coverage, density, basic_scale, detail_scale, wind, cloud_speed, time_seconds, jitter, sun_intensity, sun_col, cloud_albedo.rgb, ambient_light.rgb, ambient_strength, ph0, ph1, powder_scale, ms_extinction, ms_strength, day_night, max_trace_dist, rsteps, light_step, light_mult, lsteps, trans, scattered, dens_sum, used);
    trans = acc.trans; scattered = acc.scattered; dens_sum = acc.dens_sum; used = acc.used;
  }
  if (vert_on > 0.5f) {
    FcCloudAccum acc = fc_trace_cloud(ro, rd, sun, planet_radius, vert_base, max(vert_top - vert_base, 0.01f), vert_type, 3, coverage, density, basic_scale, detail_scale, wind, cloud_speed, time_seconds, jitter, sun_intensity, sun_col, cloud_albedo.rgb, ambient_light.rgb, ambient_strength, ph0, ph1, powder_scale, ms_extinction, ms_strength, day_night, max_trace_dist, rsteps, light_step, light_mult, lsteps, trans, scattered, dens_sum, used);
    trans = acc.trans; scattered = acc.scattered; dens_sum = acc.dens_sum; used = acc.used;
  }
  if (special_on > 0.5f) {
    FcCloudAccum acc = fc_trace_cloud(ro, rd, sun, planet_radius, special_base, max(special_thick, 0.01f), special_type, 4, coverage, density, basic_scale, detail_scale, wind, cloud_speed, time_seconds, jitter, sun_intensity, sun_col, cloud_albedo.rgb, ambient_light.rgb, ambient_strength, ph0, ph1, powder_scale, ms_extinction, ms_strength, day_night, max_trace_dist, rsteps, light_step, light_mult, lsteps, trans, scattered, dens_sum, used);
    trans = acc.trans; scattered = acc.scattered; dens_sum = acc.dens_sum; used = acc.used;
  }

  out_cloud_alpha = 1.0f - trans;
  out_cloud_density = dens_sum / max(used, 1.0f);
  out_cloud_transmittance = trans;

  float3 result = sky * trans + scattered;

  if (aurora_on > 0.5f) {
    float nf_aurora = smoothstep(-0.05f, -0.2f, sun_above) * step(ho, rd.z);
    result += fc_aurora(rd, nf_aurora, time_seconds, aurora_intensity, aurora_speed, aurora_color) * trans;
  }

  float is_rain = smoothstep(0.5f, 1.5f, weather_state);
  float is_thunder = smoothstep(1.5f, 2.5f, weather_state);
  float is_snow = smoothstep(2.5f, 3.5f, weather_state);

  float lightning = fc_lightning_flash(time_seconds, lightning_freq * is_thunder);
  if (is_thunder > 0.01f) {
    result += float3(lightning * 0.5f);
    float cloud_bottom_tint = max(0.0f, 1.0f - rd.z) * is_thunder;
    result = mix(result, result + float3(0.3f, 0.05f, 0.0f) * cloud_bottom_tint, is_thunder * 0.3f);
  }

  float2 screen_uv = float2(atan(rd.y, rd.x) / (2.0f * M_PI) + 0.5f, rd.z * 0.5f + 0.5f);
#if defined(GPU_FRAGMENT_SHADER) && \
    (defined(MAT_DEFERRED) || defined(MAT_FORWARD) || defined(NPR_SHADER))
  screen_uv = coordinate_screen(g_data.P).xy;
#endif

  float rain_effect = 0.0f;
  if (is_rain > 0.01f) {
    for (int i = 0; i < 8; i++) {
      float offset = float(i) * 0.125f;
      rain_effect += fc_rain_drop(screen_uv + float2(offset, 0.0f), time_seconds + offset, rain_intensity * is_rain);
    }
    result += float3(0.3f, 0.5f, 0.8f) * rain_effect * 2.0f;
  }

  float snow_effect = 0.0f;
  if (is_snow > 0.01f) {
    for (int i = 0; i < 6; i++) {
      float offset = float(i) * 0.1667f;
      snow_effect += fc_snow_flake(screen_uv + float2(offset, offset), time_seconds + offset, snow_intensity * is_snow);
    }
    result += float3(0.9f, 0.95f, 1.0f) * snow_effect * 1.5f;
  }

  float3 hsv = fc_rgb_to_hsv(result);
  hsv.x = fract(hsv.x + hue);
  hsv.y = saturate(hsv.y * saturation);
  hsv.z = max(0.0f, hsv.z * brightness);
  result = fc_hsv_to_rgb(hsv);

  color = float4(max(result, float3(0.0f)), 1.0f);
}
