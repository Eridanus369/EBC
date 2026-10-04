/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* NPR filter material domain nodes:
 * - node_filter_graph_input / node_output_filter: filter material graph plumbing,
 * - Scene Color (scene color / depth / normal / position sources),
 * - Image Sample (sample an image handle, optionally at an explicit UV),
 * - Filter Object Info (transform and color of a scene object),
 * - Filter Object Mask (per-pixel mask of scene objects / collections).
 *
 * Scene access goes through the global FilterMaterial resource table and the
 * filter_texture_eval family declared in eevee_filter_material_lib.bsl.hh. The functions
 * are only functional inside the MAT_FILTER world pipeline; fallback stubs are provided
 * for every other pipeline (and for shader-tool standalone linting). */

/* -------------------------------------------------------------------- */
/** \name Graph plumbing
 * \{ */

#if defined(MAT_FILTER)
[[node]]
void node_filter_graph_input(float index, out TextureHandle handle)
{
  handle = TextureHandle(TEX_HANDLE_FILTER_GRAPH_INPUT, int(index));
}

/* Returns the final color written to the filter output image. The actual store is
 * emitted by the host as `filter_graph_output_store(...)` around this function. */
[[node]]
void node_output_filter(float4 color, float alpha, out float4 result)
{
  result = float4(color.rgb, alpha);
}

[[node]]
void node_scene_source_handle(float index, out TextureHandle handle)
{
  handle = TextureHandle(TEX_HANDLE_SCENE, int(index));
}

[[node]]
void node_filter_image_default(out TextureHandle handle)
{
  handle = TextureHandle(0u, 0);
}
#else
[[node]]
void node_filter_graph_input(float /*index*/, out TextureHandle handle)
{
  handle = TextureHandle(0u, 0);
}

[[node]]
void node_output_filter(float4 /*color*/, float /*alpha*/, out float4 result)
{
  result = float4(0.0f);
}

[[node]]
void node_scene_source_handle(float /*index*/, out TextureHandle handle)
{
  handle = TextureHandle(0u, 0);
}

[[node]]
void node_filter_image_default(out TextureHandle handle)
{
  handle = TextureHandle(0u, 0);
}
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Scene Color
 * \{ */

#if defined(MAT_FILTER)
[[node]]
void node_scene_color(float source,
                      float3 uv,
                      bool use_uv,
                      out float4 color,
                      out float alpha)
{
  /* Source enum values map to scene texture indices: color=0, depth=1, normal=2,
   * position=4 (3 is reserved). */
  float scene_index = 0.0f;
  if (source == 1.0f) {
    scene_index = 1.0f;
  }
  else if (source == 2.0f) {
    scene_index = 2.0f;
  }
  else if (source == 3.0f) {
    scene_index = 4.0f;
  }

  TextureHandle scene_handle = TextureHandle(TEX_HANDLE_SCENE, int(scene_index));
  if (use_uv) {
    color = eevee::filter_texture_eval_uv(scene_handle, uv.xy);
  }
  else {
    color = eevee::filter_texture_eval(scene_handle);
  }
  /* Raw scene color stores transmittance in alpha (1 means fully transparent).
   * Inversion is handled by the Image Sample node. */
  alpha = color.a;
}
#else
[[node]]
void node_scene_color(float /*source*/,
                      float3 /*uv*/,
                      bool /*use_uv*/,
                      out float4 color,
                      out float alpha)
{
  color = float4(0.0f, 0.0f, 0.0f, 1.0f);
  alpha = 1.0f;
}
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Image Sample
 * \{ */

#if defined(MAT_FILTER)
[[node]]
void node_npr_image_sample(TextureHandle image,
                           float3 uv,
                           bool use_uv,
                           out float4 color,
                           out float alpha)
{
  if (use_uv) {
    color = eevee::filter_texture_eval_uv(image, uv.xy);
  }
  else {
    color = eevee::filter_texture_eval(image);
  }

  if (eevee::TextureHandle_is_scene_depth(image)) {
    /* Depth sources encode coverage in alpha (background is zero alpha). */
    alpha = color.a;
  }
  else if (eevee::TextureHandle_stores_transmittance_alpha(image)) {
    /* Combined scene color stores transmittance; expose regular opacity. */
    alpha = saturate(1.0f - color.a);
  }
  else {
    alpha = color.a;
  }
}
#else
[[node]]
void node_npr_image_sample(TextureHandle /*image*/,
                           float3 /*uv*/,
                           bool /*use_uv*/,
                           out float4 color,
                           out float alpha)
{
  color = float4(0.0f, 0.0f, 0.0f, 1.0f);
  alpha = 1.0f;
}
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Filter Object Info
 * \{ */

#if defined(MAT_FILTER)
[[node]]
void node_filter_object_info(float index,
                             out float3 location,
                             out float3 rotation,
                             out float3 scale,
                             out float4 color)
{
  [[resource_table]] const eevee::FilterMaterial &srt = resource_table_get(
      eevee::FilterMaterial);

  if (index < 0.0f || int(index) >= FILTER_OBJECT_INFO_MAX) {
    location = float3(0.0f);
    rotation = float3(0.0f);
    scale = float3(1.0f);
    color = float4(1.0f);
    return;
  }

  const FilterObjectInfoData info = srt.filter_object_buf[int(index)];
  location = info.location.xyz;
  /* Rotation is stored as Euler angles (radians). */
  rotation = info.rotation.xyz;
  scale = info.scale.xyz;
  color = info.color;
}
#else
[[node]]
void node_filter_object_info(float /*index*/,
                             out float3 location,
                             out float3 rotation,
                             out float3 scale,
                             out float4 color)
{
  location = float3(0.0f);
  rotation = float3(0.0f);
  scale = float3(1.0f);
  color = float4(1.0f);
}
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Filter Object Mask
 * \{ */

#if defined(MAT_FILTER)
[[node]]
void node_filter_object_mask(float start_index, float count, out float mask)
{
  [[resource_table]] const eevee::FilterMaterial &srt = resource_table_get(
      eevee::FilterMaterial);

  mask = 0.0f;
  const int start = int(start_index);
  const int total = int(clamp(count, 0.0f, float(FILTER_OBJECT_INFO_MAX)));
  if (start < 0 || total <= 0) {
    return;
  }

  const uint pixel_hash = floatBitsToUint(
      texelFetch(srt.cryptomatte_tx, int2(gl_FragCoord.xy), 0).r);

  for (int i = 0; i < total; i++) {
    const int object_index = start + i;
    if (object_index >= FILTER_OBJECT_INFO_MAX) {
      break;
    }
    if (floatBitsToUint(srt.filter_object_buf[object_index].metadata.x) == pixel_hash) {
      mask = 1.0f;
      return;
    }
  }
}
#else
[[node]]
void node_filter_object_mask(float /*start_index*/, float /*count*/, out float mask)
{
  mask = 0.0f;
}
#endif

/** \} */
