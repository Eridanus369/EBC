/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ESky procedural sky, adapted from a GLSL shader by eridanusepsilon.
 * Cloud textures from the original Flower Cloud reference are replaced
 * with procedural noise; the rest follows the original implementation.
 */

#pragma once

#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

static float fc_sat(float x)
{
  return clamp(x, 0.0f, 1.0f);
}

static float fc_remap(float value, float old_min, float old_max, float new_min, float new_max)
{
  return new_min + fc_sat((value - old_min) / max(old_max - old_min, 1.0e-5f)) *
                       (new_max - new_min);
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
    if (i >= octaves) {
      break;
    }
    r += a * fc_cloud_noise(p);
    p *= 2.02f;
    a *= 0.5f;
  }
  return r;
}

static float fc_hg(float g, float cosine_angle)
{
  float g2 = g * g;
  return (1.0f - g2) /
         (4.0f * M_PI * pow(max(1.0f + g2 - 2.0f * g * cosine_angle, 1.0e-4f), 1.5f));
}

static float fc_dual_lobe(float forward_g, float backward_g, float mix_factor, float cosine_angle)
{
  return mix(fc_hg(backward_g, cosine_angle), fc_hg(forward_g, cosine_angle), fc_sat(mix_factor));
}

static float3 fc_blackbody(float temperature_k)
{
  float t = max(temperature_k, 2000.0f);
  float tn = clamp((t - 2000.0f) / 28000.0f, 0.0f, 1.0f);
  float3 lo = float3(1.0f, 0.2f, 0.0f);
  float3 hi = float3(0.3f, 0.5f, 1.0f);
  float3 warm = mix(float3(1.0f, 0.6f, 0.1f), float3(1.0f, 0.95f, 0.85f), tn);
  float3 cool = mix(warm, hi, smoothstep(0.35f, 1.0f, tn));
  return mix(lo, cool, smoothstep(0.0f, 0.4f, tn));
}

static float3 fc_calc_sun_direction(float sun_height_deg, float sun_rotation_deg)
{
  float h = radians(sun_height_deg);
  float r = radians(sun_rotation_deg);
  return float3(cos(h) * cos(r), cos(h) * sin(r), sin(h));
}

static float3 fc_sun_disc(float3 rd,
                          float3 sun_dir,
                          float3 sun_color,
                          float sun_intensity,
                          float sun_size,
                          float bloom_intensity,
                          float horizon_height_deg)
{
  float horizon_offset = sin(radians(horizon_height_deg));
  if (rd.z < horizon_offset) {
    return float3(0.0f);
  }
  float sun_dot = max(dot(rd, sun_dir), 0.0f);
  float angle = acos(clamp(sun_dot, -1.0f, 1.0f));
  float disc_angle = sun_size * 0.004f;
  float bloom_angle = disc_angle * (4.0f + bloom_intensity * 6.0f);
  float disc = smoothstep(disc_angle, 0.0f, angle);
  float bloom = smoothstep(bloom_angle, 0.0f, angle);
  float disc_profile = pow(disc, 0.2f);
  float bloom_profile = pow(bloom, 3.0f) * (1.0f - disc) * bloom_intensity * 0.5f;
  float brightness = disc_profile * 1.2f + bloom_profile;
  float sun_above = sun_dir.z - horizon_offset;
  float fade = clamp(sun_above / (disc_angle * 3.0f) + 0.5f, 0.0f, 1.0f);
  return pow(sun_color, float3(0.7f)) * sun_intensity * brightness * 2.0f * fade;
}

static float3 fc_atmosphere(float3 rd,
                            float3 sun_dir,
                            float air_density,
                            float aerosol_concentration,
                            float ozone_amount,
                            float so2_concentration,
                            float pm25_concentration,
                            float sun_glow_suppress,
                            float sun_intensity,
                            float atmosphere_steps)
{
  rd = normalize(rd);
  sun_dir = normalize(sun_dir);
  float cos_theta = max(dot(rd, sun_dir), 0.0f);
  float zenith = max(rd.z, 0.0f);
  float sun_height_factor = smoothstep(-0.3f, 0.05f, sun_dir.z);
  float rayleigh_factor = (3.0f / (16.0f * M_PI)) * (1.0f + cos_theta * cos_theta);
  float mie_g = 0.76f;
  float mie_factor = (3.0f / (8.0f * M_PI)) *
                     ((1.0f - mie_g * mie_g) * (1.0f + cos_theta * cos_theta)) /
                     ((2.0f + mie_g * mie_g) *
                      pow(1.0f + mie_g * mie_g - 2.0f * mie_g * cos_theta, 1.5f));
  float3 kRlh = float3(5.5e-6f, 13.0e-6f, 22.4e-6f) * air_density;
  float kMie = 21e-6f * (0.5f + aerosol_concentration * 0.5f);
  float shRlh = 8.0f;
  float shMie = 1.2f;
  float air_mass = 1.0f / max(0.001f, zenith);
  float steps = max(atmosphere_steps, 1.0f);
  float iStepSize = air_mass / steps;
  float3 totalRlh = float3(0.0f);
  float3 totalMie = float3(0.0f);
  float iOdRlh = 0.0f;
  float iOdMie = 0.0f;
  for (int i = 0; i < 32; i++) {
    if (float(i) >= steps) {
      break;
    }
    float iTime = float(i) * iStepSize;
    float iHeight = max(iTime * shRlh, 0.0f);
    float odStepRlh = exp(-iHeight / shRlh) * iStepSize;
    float odStepMie = exp(-iHeight / shMie) * iStepSize;
    iOdRlh += odStepRlh;
    iOdMie += odStepMie;
    float jStepSize = max(0.005f, (1.0f + sun_dir.z * 3.0f) / steps);
    float jOdRlh = 0.0f;
    float jOdMie = 0.0f;
    for (int j = 0; j < 16; j++) {
      if (float(j) >= steps) {
        break;
      }
      float jTime = float(j) * jStepSize;
      float jHeight = max(iHeight + jTime * shRlh * max(0.0f, sun_dir.z), 0.0f);
      jOdRlh += exp(-jHeight / shRlh) * jStepSize;
      jOdMie += exp(-jHeight / shMie) * jStepSize;
    }
    float odRlhSum = min(iOdRlh + jOdRlh, 20.0f);
    float odMieSum = min(iOdMie + jOdMie, 20.0f);
    float3 attn = exp(-(kMie * odMieSum + kRlh * odRlhSum * 1000.0f));
    totalRlh += odStepRlh * attn;
    totalMie += odStepMie * attn;
  }
  float iSun = sun_intensity * 22.0f;
  float3 sky = iSun * (rayleigh_factor * kRlh * totalRlh + mie_factor * kMie * totalMie) * 1000.0f;
  float ozone_absorption = exp(-ozone_amount * 0.0015f * air_mass);
  sky *= float3(1.0f, ozone_absorption, ozone_absorption * 0.95f);
  float so2_tint = so2_concentration * 0.3f;
  sky += float3(0.15f, 0.12f, 0.05f) * so2_tint * (1.0f - zenith);
  float pm25_tint = pm25_concentration * 0.2f;
  sky += float3(pm25_tint * 0.6f, pm25_tint * 0.55f, pm25_tint * 0.4f) * (1.0f - zenith * zenith);
  float horizon_fog = exp(-air_density * (1.0f - zenith) * 5.0f);
  float horizon_brightness = 1.0f - horizon_fog;
  sky += float3(0.1f, 0.15f, 0.25f) * air_density * horizon_brightness * 0.5f;
  float sun_glow = smoothstep(0.998f, 1.0f, cos_theta);
  float3 sun_glow_color = float3(1.0f, 0.9f, 0.7f) * sun_glow * 3.0f;
  float sun_near = smoothstep(0.995f, 1.0f, cos_theta);
  sky *= 1.0f - sun_near * sun_glow_suppress * 0.6f;
  sky *= sun_height_factor;
  return sky + sun_glow_color;
}

static float fc_star_hash(float3 x)
{
  float3 p = float3(dot(x, float3(214.1f, 127.7f, 125.4f)),
                    dot(x, float3(260.5f, 183.3f, 954.2f)),
                    dot(x, float3(209.5f, 571.3f, 961.2f)));
  return fract(sin(p).x * 43758.5453123f);
}

static float fc_hash(float n)
{
  return fract(sin(n) * 758.5453f);
}

static float3 fc_stars(float3 rd,
                       float night_factor,
                       float latitude,
                       float longitude,
                       float time_s,
                       float density,
                       float brightness)
{
  if (night_factor < 0.001f) {
    return float3(0.0f);
  }
  float3 stars = float3(0.0f);
  float lat_rad = radians(latitude);
  float lon_rad = radians(longitude);
  float cos_lat = cos(lat_rad);
  float sin_lat = sin(lat_rad);
  float cos_lon = cos(lon_rad);
  float sin_lon = sin(lon_rad);
  int star_count = int(300.0f * density);
  star_count = clamp(star_count, 50, 800);
  for (int i = 0; i < 800; i++) {
    if (i >= star_count) {
      break;
    }
    float star_id = float(i);
    float seed1 = fc_hash(star_id * 137.5f);
    float seed2 = fc_hash(star_id * 83.3f + 1.0f);
    float seed3 = fc_hash(star_id * 47.9f + 2.0f);
    float ra = seed1 * 6.283f;
    float dec = acos(2.0f * seed2 - 1.0f) - 1.5708f;
    float x = cos(dec) * cos(ra);
    float y = cos(dec) * sin(ra);
    float z = sin(dec);
    float nx = x * cos_lon + z * sin_lon;
    float ny = y;
    float nz = -x * sin_lon + z * cos_lon;
    x = nx * cos_lat - ny * sin_lat;
    y = nx * sin_lat + ny * cos_lat;
    z = nz;
    float visible = step(0.0f, z);
    float star_dot = dot(rd, float3(x, y, z));
    float star_angle = acos(clamp(star_dot, -1.0f, 1.0f));
    float star_size = 0.0003f + seed3 * 0.001f;
    float val = smoothstep(star_size, 0.0f, star_angle);
    float b = pow(max(seed3, 0.01f), 0.8f);
    float twinkle = 0.7f + sin(time_s * 2.0f + star_id * 2.0f) * 0.3f;
    stars += float3(b * val * visible * twinkle * brightness);
  }
  return stars * night_factor;
}

[[node]]
void node_esky(float3 view_dir,
               float time_seconds,
               float sample_index,
               float camera_height,
               float planet_radius,
               float latitude,
               float longitude,
               float star_density,
               float star_brightness,
               float use_sun_system,
               float sun_height,
               float sun_rotation,
               float sun_temperature,
               float sun_size,
               float sun_bloom,
               float horizon_height,
               float use_atmosphere,
               float air_density,
               float aerosol_concentration,
               float ozone_amount,
               float so2_concentration,
               float pm25_concentration,
               float sun_glow_suppress,
               float atmosphere_steps,
               float sun_intensity,
               float3 zenith_color,
               float3 horizon_color,
               out float4 color)
{
  (void)sample_index;
  (void)camera_height;
  (void)planet_radius;
  (void)use_sun_system;

  float3 rd = fc_safe_normalize_dir(-view_dir);
  float3 sun = fc_safe_normalize_dir(fc_calc_sun_direction(sun_height, sun_rotation));
  float3 sun_color = fc_blackbody(sun_temperature);
  float horizon_offset = sin(radians(horizon_height));
  float sun_above = sun.z - horizon_offset;
  float day_night = smoothstep(-0.15f, 0.05f, sun_above);

  float3 sky;
  if (use_atmosphere > 0.5f) {
    sky = fc_atmosphere(rd,
                        sun,
                        air_density,
                        aerosol_concentration,
                        ozone_amount,
                        so2_concentration,
                        pm25_concentration,
                        sun_glow_suppress,
                        sun_intensity,
                        atmosphere_steps);
  }
  else {
    float horizon = pow(1.0f - fc_sat(rd.z), 1.35f);
    sky = mix(zenith_color, horizon_color, horizon);
  }

  sky *= day_night;

  float sunset_dist = sun.z - horizon_offset;
  float sunset_factor = clamp(1.0f - sunset_dist / 0.15f, 0.0f, 1.0f);
  float horizon_blend = smoothstep(-0.05f, 0.05f, rd.z - horizon_offset);
  float sun_visible = step(horizon_offset, sun.z);
  float sunset_glow = sunset_factor * sun_visible * horizon_blend;
  float3 sunset_color = mix(float3(1.0f, 0.4f, 0.1f), float3(1.0f, 0.7f, 0.3f),
                            clamp(sunset_factor, 0.0f, 1.0f));
  sky += sunset_color * sunset_glow * sun_intensity * 0.3f;

  if (sun_intensity > 0.0f) {
    sky += fc_sun_disc(rd, sun, sun_color, sun_intensity, sun_size, sun_bloom, horizon_height);
  }

  float night_factor = smoothstep(-0.05f, -0.2f, sun_above);
  float view_above = step(horizon_offset, rd.z);
  float night_sky = night_factor * view_above;
  sky += fc_stars(rd, night_sky, latitude, longitude, time_seconds, star_density, star_brightness);

  color = float4(max(sky, float3(0.0f)), 1.0f);
}
