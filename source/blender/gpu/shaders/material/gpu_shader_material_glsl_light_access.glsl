/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR GLSL Light Access (reduced): exposes EEVEE lights to GLSL Function nodes.
 * Light Shader per-light parameters are not available in this build. */

#define GLSL_LIGHT_TYPE_INVALID 0
#define GLSL_LIGHT_TYPE_SUN 1
#define GLSL_LIGHT_TYPE_POINT 2
#define GLSL_LIGHT_TYPE_SPOT 3
#define GLSL_LIGHT_TYPE_AREA_RECT 4
#define GLSL_LIGHT_TYPE_AREA_ELLIPSE 5

struct GLSLLight {
  bool valid;
  uint index;
  uint shader_parameter_uid;
  int type;
  int lightgroup_id;
  float3 vector;
  float3 position;
  float3 direction;
  float distance;
  float3 diffuse_color;
  float3 specular_color;
  float attenuation;
  float influence_radius;
};

GLSLLight glsl_light_default()
{
  GLSLLight light;
  light.valid = false;
  light.index = 0u;
  light.shader_parameter_uid = 0u;
  light.type = GLSL_LIGHT_TYPE_INVALID;
  light.lightgroup_id = 0;
  light.vector = float3(0.0f, 0.0f, 1.0f);
  light.position = float3(0.0f);
  light.direction = float3(0.0f);
  light.distance = 0.0f;
  light.diffuse_color = float3(0.0f);
  light.specular_color = float3(0.0f);
  light.attenuation = 0.0f;
  light.influence_radius = 0.0f;
  return light;
}

#if defined(GPU_FRAGMENT_SHADER) && defined(MAT_GLSL_LIGHT_ACCESS) && \
    defined(EEVEE_LIGHT_DATA_AVAILABLE)

bool glsl_light_lookup(uint light_index,
                       bool is_local,
                       out LightData light,
                       out LightVector light_vector,
                       out bool is_directional)
{
  if (light_index >= light_cull_buf.items_count) {
    return false;
  }
  light = light_buf[light_index];
  if (all(lessThanEqual(abs(light.color), float3(1e-8f)))) {
    return false;
  }
  is_directional = !is_local;
  light_vector = light_vector_get(light, is_directional, g_data.P);
  return true;
}

bool glsl_light_loop_accept(uint light_index, bool is_local)
{
  LightData light;
  LightVector light_vector;
  bool is_directional;
  if (!glsl_light_lookup(light_index, is_local, light, light_vector, is_directional)) {
    return false;
  }
  return max(light.power[LIGHT_DIFFUSE], light.power[LIGHT_SPECULAR]) >=
         LIGHT_ATTENUATION_THRESHOLD;
}

int glsl_light_public_type(LightData light)
{
  if (is_sun_light(light.type)) {
    return GLSL_LIGHT_TYPE_SUN;
  }
  if (is_spot_light(light.type)) {
    return GLSL_LIGHT_TYPE_SPOT;
  }
  if (light.type == LIGHT_RECT) {
    return GLSL_LIGHT_TYPE_AREA_RECT;
  }
  if (light.type == LIGHT_ELLIPSE) {
    return GLSL_LIGHT_TYPE_AREA_ELLIPSE;
  }
  return GLSL_LIGHT_TYPE_POINT;
}

GLSLLight glsl_light_build(uint light_index, bool is_local, uint public_index)
{
  GLSLLight result = glsl_light_default();
  LightData light;
  LightVector light_vector;
  bool is_directional;
  if (!glsl_light_lookup(light_index, is_local, light, light_vector, is_directional)) {
    return result;
  }

  result.valid = true;
  result.index = public_index;
  result.type = glsl_light_public_type(light);
  result.vector = light_vector.L;
  result.position = is_directional ? float3(0.0f) : light.position();
  if (is_directional) {
    result.direction = light.sun().direction;
  }
  else if (is_spot_light(light.type) || is_area_light(light.type)) {
    result.direction = light.z_axis();
  }
  result.distance = light_vector.dist;
  result.diffuse_color = light.color * light.power[LIGHT_DIFFUSE];
  result.specular_color = light.color * light.power[LIGHT_SPECULAR];
  result.attenuation = light_attenuation_common(light, is_directional, light_vector.L) *
                       light_attenuation_surface(light, is_directional, light_vector);
  return result;
}

bool glsl_light_find_ordinal(int light_ordinal, out uint r_light_index, out bool r_is_local)
{
  if (light_ordinal < 0) {
    return false;
  }
  int current = 0;
  for (uint i = 0u; i < light_cull_buf.local_lights_len; i++) {
    if (!glsl_light_loop_accept(i, true)) {
      continue;
    }
    if (current == light_ordinal) {
      r_light_index = i;
      r_is_local = true;
      return true;
    }
    current += 1;
  }
  for (uint i = light_cull_buf.local_lights_len; i < light_cull_buf.items_count; i++) {
    if (!glsl_light_loop_accept(i, false)) {
      continue;
    }
    if (current == light_ordinal) {
      r_light_index = i;
      r_is_local = false;
      return true;
    }
    current += 1;
  }
  return false;
}

int glsl_light_count()
{
  int count = 0;
  for (uint i = 0u; i < light_cull_buf.local_lights_len; i++) {
    if (glsl_light_loop_accept(i, true)) {
      count += 1;
    }
  }
  for (uint i = light_cull_buf.local_lights_len; i < light_cull_buf.items_count; i++) {
    if (glsl_light_loop_accept(i, false)) {
      count += 1;
    }
  }
  return count;
}

GLSLLight glsl_light_get(int light_ordinal)
{
  uint light_index = 0u;
  bool is_local = false;
  if (!glsl_light_find_ordinal(light_ordinal, light_index, is_local)) {
    return glsl_light_default();
  }
  return glsl_light_build(light_index, is_local, uint(light_ordinal));
}

float glsl_light_shadow(int light_ordinal, float3 shading_normal)
{
#  if defined(MAT_GLSL_LIGHT_SHADOW_ACCESS)
  uint light_index = 0u;
  bool is_local = false;
  if (!glsl_light_find_ordinal(light_ordinal, light_index, is_local)) {
    return 0.0f;
  }
  LightData light;
  LightVector light_vector;
  bool is_directional;
  if (!glsl_light_lookup(light_index, is_local, light, light_vector, is_directional)) {
    return 0.0f;
  }
  if (light.tilemap_index == LIGHT_NO_SHADOW) {
    return 1.0f;
  }
  ObjectInfos object_infos = object_infos_get();
  float3 geometry_normal = normalize(g_data.Ng);
  float3 resolved_shading_normal = normalize(shading_normal);
  return eevee_shadow_eval(light,
                           is_directional,
                           false,
                           false,
                           0.0f,
                           g_data.P,
                           geometry_normal,
                           resolved_shading_normal,
                           object_infos.shadow_terminator_normal_offset,
                           object_infos.shadow_terminator_geometry_offset,
                           uniform_buf.shadow.ray_count,
                           uniform_buf.shadow.step_count);
#  else
  UNUSED_VARS(light_ordinal);
  UNUSED_VARS(shading_normal);
  return 0.0f;
#  endif
}

#else  /* !(fragment && MAT_GLSL_LIGHT_ACCESS && EEVEE_LIGHT_DATA_AVAILABLE) */

int glsl_light_count()
{
  return 0;
}

GLSLLight glsl_light_get(int light_ordinal)
{
  UNUSED_VARS(light_ordinal);
  return glsl_light_default();
}

float glsl_light_shadow(int light_ordinal, float3 shading_normal)
{
  UNUSED_VARS(light_ordinal);
  UNUSED_VARS(shading_normal);
  return 0.0f;
}

#endif

/* Per-light shader parameters are not available in this build. */
float glsl_light_parameter_float(GLSLLight light, uint lo, uint hi, float fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
int glsl_light_parameter_int(GLSLLight light, uint lo, uint hi, int fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
bool glsl_light_parameter_bool(GLSLLight light, uint lo, uint hi, bool fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
float2 glsl_light_parameter_vec2(GLSLLight light, uint lo, uint hi, float2 fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
float3 glsl_light_parameter_vec3(GLSLLight light, uint lo, uint hi, float3 fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
float4 glsl_light_parameter_vec4(GLSLLight light, uint lo, uint hi, float4 fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
float4 glsl_light_parameter_color(GLSLLight light, uint lo, uint hi, float4 fallback, out bool valid)
{
  UNUSED_VARS(light);
  UNUSED_VARS(lo);
  UNUSED_VARS(hi);
  valid = false;
  return fallback;
}
