/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "eevee_caustics.hh"
#include "eevee_instance.hh"
#include "draw_view.hh"

#include <cstdlib>

#include "MEM_guardedalloc.h"

#include "BLI_math_constants.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_vector.hh"

#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"

#include "BKE_light.h"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_runtime.hh"
#include "BKE_object.hh"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "GPU_capabilities.hh"
#include "GPU_vertex_format.hh"

namespace blender::eevee {

/* Converts the radiometric photon power (in Watt) into the light intensity scale used by the
 * deferred pipeline. Without it, a physically normalized photon carries a value that is orders of
 * magnitude below what the framebuffer expects. */
static constexpr float CAUSTIC_RADIOMETRIC_SCALE = 2000.0f;
static constexpr int CAUSTIC_PHOTON_MAX = 4000000;
/* Cauchy first coefficient of BK7 crown glass, in um^2. Used when dispersion is enabled but the
 * scene stores no coefficients (files saved before the panel got a default), so that enabling the
 * feature does not silently produce a monochrome caustic. */
static constexpr float CAUSTIC_DEFAULT_CAUCHY_B = 0.0042f;

/* -------------------------------------------------------------------- */
/** \name Helpers
 * \{ */

struct CausticMeshEntry {
  Mesh *mesh;
  float4x4 world;
  float3 min_bound;
  float3 max_bound;
  bool transmissive;
  float ior;
  int table_index;
  int tri_count;
  int tri_offset;
  /* Global material table range covered by the material slots of this mesh. */
  int material_offset;
  int material_count;
  /* One entry per material slot, to be uploaded into the global material table. */
  Vector<CausticMaterialData> materials;
};

static uint64_t caustic_hash_mix(uint64_t hash, uint64_t value)
{
  return (hash ^ value) * 1099511628211ULL;
}

static uint64_t caustic_hash_float(uint64_t hash, float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return caustic_hash_mix(hash, bits);
}

static float3 caustic_safe_normalize(const float3 &v, const float3 &fallback)
{
  const float len = math::length(v);
  return (len > 1.0e-12f) ? (v / len) : fallback;
}

/* Reads the default value of a float socket. A linked socket has no meaningful scalar value on the
 * CPU side, so `fallback` is returned in that case. */
static float caustic_socket_value(const bNodeSocket *sock, float fallback)
{
  if (sock == nullptr || sock->default_value == nullptr) {
    return fallback;
  }
  return static_cast<const bNodeSocketValueFloat *>(sock->default_value)->value;
}

/* Reads the activation weight of a float socket. A linked socket is driven by a node tree that
 * cannot be evaluated without building the GPU material, so it is assumed to enable the feature
 * instead of silently disabling it. */
static float caustic_socket_weight(const bNodeSocket *sock, float fallback)
{
  if (sock != nullptr && sock->link != nullptr) {
    return 1.0f;
  }
  return caustic_socket_value(sock, fallback);
}

/* Returns the node feeding the given input socket, or nullptr when it is unlinked. */
static const bNode *caustic_input_node(const bNode &node, UString identifier)
{
  const bNodeSocket *sock = bke::node_find_socket(node, SOCK_IN, identifier);
  return (sock != nullptr && sock->link != nullptr) ? sock->link->fromnode : nullptr;
}

/* Walks one shader node and returns its transmission weight in [0, 1]. Mix and Add shader nodes are
 * followed recursively so that the result reflects the shaders actually wired to the output. */
static float caustic_shader_weight(const bNode *node, int depth, bool &r_transmissive, float &r_ior)
{
  if (node == nullptr || depth > 8) {
    return 0.0f;
  }

  if (node->type_legacy == SH_NODE_BSDF_PRINCIPLED) {
    const float weight = caustic_socket_weight(
        bke::node_find_socket(*node, SOCK_IN, "Transmission Weight"_ustr), 0.0f);
    if (weight > 1.0e-3f) {
      r_transmissive = true;
      r_ior = clamp_f(caustic_socket_value(bke::node_find_socket(*node, SOCK_IN, "IOR"_ustr), 1.45f),
                      1.0f,
                      4.0f);
    }
    return clamp_f(weight, 0.0f, 1.0f);
  }

  if (node->type_legacy == SH_NODE_BSDF_GLASS || node->type_legacy == SH_NODE_BSDF_REFRACTION) {
    r_transmissive = true;
    r_ior = clamp_f(caustic_socket_value(bke::node_find_socket(*node, SOCK_IN, "IOR"_ustr), 1.45f),
                    1.0f,
                    4.0f);
    return 1.0f;
  }

  if (node->type_legacy == SH_NODE_BSDF_TRANSPARENT ||
      node->type_legacy == SH_NODE_BSDF_TRANSLUCENT)
  {
    /* Light goes straight through without being refracted (transparent / diffuse transmission). */
    r_transmissive = true;
    r_ior = 1.0f;
    return 1.0f;
  }

  if (node->type_legacy == SH_NODE_MIX_SHADER) {
    const float fac = clamp_f(
        caustic_socket_value(bke::node_find_socket(*node, SOCK_IN, "Fac"_ustr), 0.5f), 0.0f, 1.0f);

    bool trans_a = false, trans_b = false;
    float ior_a = 1.45f, ior_b = 1.45f;
    const float weight_a = caustic_shader_weight(caustic_input_node(*node, "Shader"_ustr),
                                                 depth + 1,
                                                 trans_a,
                                                 ior_a) *
                           (1.0f - fac);
    const float weight_b = caustic_shader_weight(caustic_input_node(*node, "Shader_001"_ustr),
                                                 depth + 1,
                                                 trans_b,
                                                 ior_b) *
                           fac;

    if (weight_b > weight_a) {
      r_transmissive = trans_b && weight_b > 1.0e-3f;
      r_ior = ior_b;
    }
    else {
      r_transmissive = trans_a && weight_a > 1.0e-3f;
      r_ior = ior_a;
    }
    return weight_a + weight_b;
  }

  if (node->type_legacy == SH_NODE_ADD_SHADER) {
    bool trans_a = false, trans_b = false;
    float ior_a = 1.45f, ior_b = 1.45f;
    const float weight_a = caustic_shader_weight(caustic_input_node(*node, "Shader"_ustr),
                                                 depth + 1,
                                                 trans_a,
                                                 ior_a);
    const float weight_b = caustic_shader_weight(caustic_input_node(*node, "Shader_001"_ustr),
                                                 depth + 1,
                                                 trans_b,
                                                 ior_b);

    if (trans_b && (!trans_a || weight_b > weight_a)) {
      r_transmissive = true;
      r_ior = ior_b;
    }
    else if (trans_a) {
      r_transmissive = true;
      r_ior = ior_a;
    }
    return clamp_f(weight_a + weight_b, 0.0f, 1.0f);
  }

  return 0.0f;
}

/* Resolve the index of refraction and the transmission amount of a single material. Only the shader
 * connected to the `Material Output` node contributes: nodes that are not wired into the surface
 * must not turn an object into a lens. */
static void caustic_single_material_get(::blender::Material *mat,
                                        bool &r_transmissive,
                                        float &r_ior)
{
  r_transmissive = false;
  r_ior = 1.45f;

  if (mat == nullptr || mat->nodetree == nullptr) {
    return;
  }

  for (const bNode *node : mat->nodetree->all_nodes()) {
    if (node->type_legacy != SH_NODE_OUTPUT_MATERIAL) {
      continue;
    }
    const bNodeSocket *surface = bke::node_find_socket(*node, SOCK_IN, "Surface"_ustr);
    if (surface == nullptr || surface->link == nullptr) {
      continue;
    }

    bool transmissive = false;
    float ior = 1.45f;
    const float weight = caustic_shader_weight(surface->link->fromnode, 0, transmissive, ior);
    if (transmissive && weight > 1.0e-3f) {
      r_transmissive = true;
      r_ior = ior;
      return;
    }
  }
}

/* Returns the index of refraction and the transmission amount of the most refractive material of
 * the object. Kept for the object level light importance sampling. */
static void caustic_material_info_get(Object *ob, bool &r_transmissive, float &r_ior)
{
  r_transmissive = false;
  r_ior = 1.45f;

  const int mat_count = ob->totcol;
  for (int i = 0; i < mat_count; i++) {
    ::blender::Material *mat = BKE_object_material_get_eval(ob, short(i + 1));
    bool slot_transmissive;
    float slot_ior;
    caustic_single_material_get(mat, slot_transmissive, slot_ior);
    if (slot_transmissive && !r_transmissive) {
      r_ior = slot_ior;
      r_transmissive = true;
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Construction
 * \{ */

CausticModule::CausticModule(Instance &inst) : inst_(inst), caustic_map_(inst) {}

Texture &CausticModule::color_texture()
{
  return caustic_map_.color_texture();
}

Texture &CausticModule::volume_accum_r_texture()
{
  return caustic_map_.volume_accum_r_texture();
}

Texture &CausticModule::volume_accum_g_texture()
{
  return caustic_map_.volume_accum_g_texture();
}

Texture &CausticModule::volume_accum_b_texture()
{
  return caustic_map_.volume_accum_b_texture();
}

int CausticModule::volume_resolution() const
{
  return CAUSTIC_VOLUME_RES;
}

void CausticModule::init()
{
  caustic_map_.init();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Sync
 * \{ */

void CausticModule::sync()
{
  /* Photon throughput probe. Reading is only legal outside the draw, so the counters written by the
   * previous frame are consumed here. Sample every frame when explicitly debugging, otherwise
   * rarely, since the blocking read stalls the GPU. Set `EBC_CAUSTIC_PROBE=1` to force it and run
   * with `--log-level 3 --log eevee` to see the result. */
  static const bool probe_always = (std::getenv("EBC_CAUSTIC_PROBE") != nullptr);
  if (stats_pending_ && (probe_always || (++stats_frame_counter_ % 32) == 0)) {
    stats_pending_ = false;
    stats_buf_.read();
    const uint deposited = stats_buf_[0];
    const uint lost = stats_buf_[1];
    CLOG_INFO(&Instance::log,
              "Caustics: deposited %u / %u photons (%.1f%%), bounces %u, refractions %u, "
              "TIR %u, self-hits %u",
              deposited,
              deposited + lost,
              (deposited + lost) > 0 ? 100.0 * double(deposited) / double(deposited + lost) : 0.0,
              stats_buf_[2],
              stats_buf_[3],
              stats_buf_[4],
              stats_buf_[5]);

    if (probe_always) {
      /* World space extent of the deposited photons: tells whether they land where the caustic is
       * expected (on the floor under the lens) or somewhere else entirely. */
      photon_pool_.read();
      float3 mn(1.0e30f), mx(-1.0e30f), sum(0.0f);
      int count = 0;
      for (int i = 0; i < photon_count_; i++) {
        const Photon &p = photon_pool_[i];
        if (p.flags != CAUSTIC_PHOTON_DEPOSITED) {
          continue;
        }
        const float3 pos(p.position.x, p.position.y, p.position.z);
        mn = math::min(mn, pos);
        mx = math::max(mx, pos);
        sum += pos;
        count += 1;
      }
      if (count > 0) {
        const float3 mean = sum / float(count);
        CLOG_INFO(&Instance::log,
                  "Caustics deposit bounds min (%.2f %.2f %.2f) max (%.2f %.2f %.2f) "
                  "mean (%.2f %.2f %.2f)",
                  mn.x,
                  mn.y,
                  mn.z,
                  mx.x,
                  mx.y,
                  mx.z,
                  mean.x,
                  mean.y,
                  mean.z);
      }
    }
  }

  const EEVEE_SpectralCausticsSettings &settings = inst_.scene->eevee.caustics;

  /* Ray queries are mandatory: screen space tracing cannot penetrate a mesh and reach the back
   * faces, which is required to resolve nested media (glass + water). */
  enabled_ = (settings.use_spectral_caustics != 0) && GPU_ray_query_support();

  /* Startup files authored before the caustic settings existed load with these fields zeroed, so
   * fall back to the intended defaults instead of silently rendering nothing. */
  photon_count_ = settings.photon_count;
  if (photon_count_ <= 0) {
    photon_count_ = 2000000; /* 默认 200 万光子 */
  }
  photon_count_ = clamp_i(photon_count_, 1000, CAUSTIC_PHOTON_MAX);

  spectral_samples_ = settings.dispersion_enabled ? 8 : 1;
  max_bounces_ = clamp_i(settings.max_bounces, 1, 16);
  splat_gain_ = settings.splat_gain;
  if (splat_gain_ <= 0.0f) {
    /* 0 means "use the automatic level". The photon deposit is a radiometric power, not an
     * irradiance, so a gain of 1 can make a focused caustic several times brighter than the direct
     * lighting of the scene. A dispersive object (a monkey shaped lens) spreads the same energy over
     * a much larger area than a prism, so a level calibrated on the focused case leaves it
     * invisible. 0.5 is the compromise; the resolve pass softly rolls off the highlights, which is
     * what keeps a focused caustic from clipping to a flat white slab at this level. */
    splat_gain_ = 0.5f;
  }
  splat_radius_ = clamp_f(settings.splat_radius, 0.0f, 4.0f);
  if (splat_radius_ <= 0.0f) {
    /* Two texels wide: one texel of a 1024 map is sub pixel in a 1080p frame, which leaves the
     * sparse photon deposits as isolated aliased dots. */
    splat_radius_ = 2.0f;
  }
  volume_gain_ = math::max(settings.caustic_volume_gain, 0.0f);

  /* Never let the map fall below 1024 wide: at lower resolutions the bilinear upscale to the render
   * resolution turns the caustic into a blurry, aliased blob. */
  int res = clamp_i(math::max(settings.caustic_resolution, 1024), 1024, 2048);
  /* Match the render aspect ratio so the caustic texels stay square. A square map on a widescreen
   * frame would be stretched vertically and look blurry. `render_extent_get()` lags one frame
   * behind (the film is synced after the caustics), which is harmless for a resolution change. */
  int2 caustic_extent = int2(res);
  const int2 render_extent = inst_.film.render_extent_get();
  if (render_extent.x > 0 && render_extent.y > 0) {
    caustic_extent.y = math::max(
        1, int(math::round(float(res) * float(render_extent.y) / float(render_extent.x))));
  }
  caustic_map_.sync(caustic_extent);

  if (enabled_) {
    sync_geometry();
    sync_lights();
  }

  /* The caustic map is a screen space buffer: everything accumulated in it only makes sense for the
   * framing it was computed with. The viewport never resets its sample counter while navigating, so
   * without this check the map of the previous framing is progressively averaged into the new one
   * and the caustics stay glued to the screen while the camera moves (the light trail). */
  const float4x4 viewmat = inst_.camera.data_get().viewmat;
  bool view_changed = !has_last_viewmat_;
  if (has_last_viewmat_) {
    view_changed = !compare_m4m4(viewmat.ptr(), last_viewmat_.ptr(), 1.0e-5f);
  }
  last_viewmat_ = viewmat;
  has_last_viewmat_ = true;

  /* Also restart when the engine explicitly resets its sampling (viewport redraw, scene update), or
   * when the sample counter is back at its first sample (interactive sequence restart). `sync()`
   * also runs in the middle of a sequence (motion blur steps), where the counter must be kept so
   * that the progressive average of the caustic map keeps converging. */
  const bool sampling_restarted = (inst_.sampling.sample_index() == 0) ||
                                  (inst_.is_viewport() && inst_.sampling.is_reset());
  if (view_changed || sampling_restarted) {
    caustic_sequence_sample_ = 0;
  }
  render_pending_ = true;
}

void CausticModule::sync_geometry()
{
  const EEVEE_SpectralCausticsSettings &settings = inst_.scene->eevee.caustics;
  /* Resolve the Cauchy coefficients once. Scenes saved before the panel got a default store zero,
   * which would disable dispersion entirely (monochrome caustics); fall back to BK7 crown glass.
   * `dispersion_enabled` remains the actual on/off switch. */
  float cauchy_b = settings.cauchy_b;
  float cauchy_c = settings.cauchy_c;
  if (settings.dispersion_enabled != 0 && cauchy_b == 0.0f && cauchy_c == 0.0f) {
    cauchy_b = CAUSTIC_DEFAULT_CAUCHY_B;
  }
  const bool dispersion = settings.dispersion_enabled != 0 &&
                          (cauchy_b != 0.0f || cauchy_c != 0.0f);

  const eEvaluationMode eval_mode = DEG_get_mode(inst_.depsgraph);

  DEGObjectIterSettings iter_settings = {nullptr};
  iter_settings.depsgraph = inst_.depsgraph;
  iter_settings.flags = DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY |
                        DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET;

  Vector<CausticMeshEntry> entries;

  uint64_t hash = 1469598103934665603ULL;

  DEG_OBJECT_ITER_BEGIN (&iter_settings, ob) {
    if (ob->type != OB_MESH) {
      continue;
    }
    if (!DEG_iterator_object_is_visible(eval_mode, ob)) {
      continue;
    }
    if (!(BKE_object_visibility(ob, eval_mode) & OB_VISIBLE_SELF)) {
      continue;
    }

    Mesh *mesh = BKE_object_get_evaluated_mesh(ob);
    if (mesh == nullptr || mesh->faces_num == 0 || mesh->verts_num == 0) {
      continue;
    }

    bool transmissive;
    float ior;
    caustic_material_info_get(ob, transmissive, ior);

    CausticMeshEntry entry;
    entry.mesh = mesh;
    entry.world = ob->object_to_world();
    entry.min_bound = float3(FLT_MAX);
    entry.max_bound = float3(-FLT_MAX);
    entry.transmissive = transmissive;
    entry.ior = ior;
    entry.table_index = -1;
    entry.tri_count = int(mesh->corner_tris().size());
    entry.tri_offset = 0;
    entry.material_offset = 0;
    entry.material_count = 0;

    /* Resolve every material slot individually so that each triangle can use the material actually
     * assigned to its face. Objects without slots fall back to the object level result. */
    const int mat_count = ob->totcol;
    if (mat_count == 0) {
      CausticMaterialData data;
      data.ior = ior;
      data.cauchy_b = dispersion ? cauchy_b : 0.0f;
      data.cauchy_c = dispersion ? cauchy_c : 0.0f;
      data.transmissive = transmissive ? 1 : 0;
      entry.materials.append(data);
    }
    else {
      for (int slot = 0; slot < mat_count; slot++) {
        ::blender::Material *mat = BKE_object_material_get_eval(ob, short(slot + 1));
        bool slot_transmissive;
        float slot_ior;
        caustic_single_material_get(mat, slot_transmissive, slot_ior);
        CausticMaterialData data;
        data.ior = slot_ior;
        data.cauchy_b = dispersion ? cauchy_b : 0.0f;
        data.cauchy_c = dispersion ? cauchy_c : 0.0f;
        data.transmissive = slot_transmissive ? 1 : 0;
        entry.materials.append(data);
      }
    }
    entry.material_count = int(entry.materials.size());
    entries.append(entry);

    hash = caustic_hash_mix(hash, uint64_t(mesh));
    hash = caustic_hash_mix(hash, uint64_t(mesh->verts_num));
    hash = caustic_hash_mix(hash, uint64_t(mesh->corners_num));
    hash = caustic_hash_mix(hash, transmissive ? 1u : 0u);
    hash = caustic_hash_float(hash, ior);
    hash = caustic_hash_mix(hash, uint64_t(entry.material_count));
    for (const CausticMaterialData &data : entry.materials) {
      hash = caustic_hash_float(hash, data.ior);
      hash = caustic_hash_float(hash, data.cauchy_b);
      hash = caustic_hash_float(hash, data.cauchy_c);
      hash = caustic_hash_mix(hash, uint64_t(data.transmissive));
    }
    /* Sample a few positions so that deformations invalidate the acceleration structure even when
     * the mesh data-block itself is reused in place. */
    {
      const Span<float3> positions = mesh->vert_positions();
      const int stride = math::max(1, int(positions.size()) / 16);
      for (int i = 0; i < int(positions.size()); i += stride) {
        const float3 &p = positions[i];
        hash = caustic_hash_float(hash, p.x);
        hash = caustic_hash_float(hash, p.y);
        hash = caustic_hash_float(hash, p.z);
      }
    }
    const float4x4 &world = entry.world;
    for (int c = 0; c < 4; c++) {
      for (int r = 0; r < 4; r++) {
        hash = caustic_hash_float(hash, world[c][r]);
      }
    }
  }
  DEG_OBJECT_ITER_END;

  if (hash == geometry_hash_ && bool(tlas_)) {
    return;
  }
  geometry_hash_ = hash;

  /* Release the acceleration structures before their geometry buffers. */
  tlas_ = nullptr;
  blas_ = nullptr;
  index_buf_ = nullptr;
  vertex_buf_ = nullptr;

  object_count_ = int(entries.size());
  transmissive_count_ = 0;
  triangle_count_ = 0;
  material_count_ = 0;

  /* Reset the volumetric grid bounds until the new geometry bounds are computed. */
  volume_min_ = float3(0.0f);
  volume_max_ = float3(1.0f);
  volume_size_inv_ = float3(1.0f);

  if (entries.is_empty()) {
    return;
  }

  /* Transmissive objects are stored first so the trace shader can limit its medium lookup to
   * `[0, transmissive_count)`. */
  for (int i = 0; i < int(entries.size()); i++) {
    if (entries[i].transmissive) {
      entries[i].table_index = transmissive_count_++;
    }
  }
  {
    int next_opaque = transmissive_count_;
    for (int i = 0; i < int(entries.size()); i++) {
      if (!entries[i].transmissive) {
        entries[i].table_index = next_opaque++;
      }
    }
  }

  for (CausticMeshEntry &entry : entries) {
    entry.tri_offset = triangle_count_;
    triangle_count_ += entry.tri_count;
    entry.material_offset = material_count_;
    material_count_ += entry.material_count;
  }

  object_buf_.resize(math::max(object_count_, 1));
  target_spheres_.resize(object_count_);
  triangle_buf_.resize(math::max(triangle_count_, 1));
  material_buf_.resize(math::max(material_count_, 1));

  /* Upload the per slot material table referenced by the triangles. */
  {
    int mat_index = 0;
    for (const CausticMeshEntry &entry : entries) {
      for (const CausticMaterialData &data : entry.materials) {
        material_buf_[mat_index++] = data;
      }
    }
    material_buf_.push_update();
  }

  scene_center_ = float3(0.0f);
  scene_radius_ = 1.0f;

  if (triangle_count_ == 0) {
    return;
  }

  /* Merge every mesh into a single world space geometry. A single BLAS keeps the emitted
   * `primitiveIndex` equal to the global triangle index. */
  static const GPUVertFormat pos_format = GPU_vertformat_from_attribute(
      "pos", gpu::VertAttrType::SFLOAT_32_32_32);
  vertex_buf_ = gpu::VertBufPtr(GPU_vertbuf_create_with_format(pos_format));
  const int vertex_count = triangle_count_ * 3;
  GPU_vertbuf_data_alloc(*vertex_buf_, uint(vertex_count));
  MutableSpan<float3> vertex_data = vertex_buf_->data<float3>();

  index_buf_ = gpu::IndexBufPtr(GPU_indexbuf_calloc());
  uint32_t *indices = MEM_new_array<uint32_t>(size_t(vertex_count), __func__);
  for (int i = 0; i < vertex_count; i++) {
    indices[i] = uint32_t(i);
  }
  index_buf_->init(uint(vertex_count), indices, 0, uint(vertex_count - 1), GPU_PRIM_TRIS, false);

  const float3 dispersion_coeffs = float3(0.0f,
                                          dispersion ? cauchy_b : 0.0f,
                                          dispersion ? cauchy_c : 0.0f);

  float3 global_min(FLT_MAX);
  float3 global_max(-FLT_MAX);

  int vertex_offset = 0;
  for (const CausticMeshEntry &entry : entries) {
    const Mesh &mesh = *entry.mesh;
    const Span<float3> positions = mesh.vert_positions();
    const Span<int> corner_verts = mesh.corner_verts();
    const Span<float3> corner_normals = mesh.corner_normals();
    const Span<int3> tris = mesh.corner_tris();
    const Span<int> tri_faces = mesh.corner_tri_faces();
    const bke::AttributeAccessor attributes = mesh.attributes();
    const VArray<int> material_indices = *attributes.lookup_or_default<int>(
        "material_index", bke::AttrDomain::Face, 0);
    const float4x4 &world = entry.world;

    /* World space bounds, used both for the medium lookup and for the light importance sampling. */
    float3 min_bound(FLT_MAX);
    float3 max_bound(-FLT_MAX);
    for (const float3 &p : positions) {
      const float3 wp = math::transform_point(world, p);
      min_bound = math::min(min_bound, wp);
      max_bound = math::max(max_bound, wp);
    }

    /* Tight bounding sphere of the vertices. The box diagonal used previously is sqrt(3) times too
     * large for compact lenses such as a sphere, which aimed two thirds of the photons past it. */
    const float3 sphere_center = (min_bound + max_bound) * 0.5f;
    float sphere_radius2 = 0.0f;
    for (const float3 &p : positions) {
      const float3 d = math::transform_point(world, p) - sphere_center;
      sphere_radius2 = math::max(sphere_radius2, math::dot(d, d));
    }
    target_spheres_[entry.table_index] = float4(
        sphere_center.x, sphere_center.y, sphere_center.z, math::sqrt(sphere_radius2));

    float3x3 m3(world);
    bool invert_ok = false;
    const float3x3 normal_matrix = math::transpose(math::invert(m3, invert_ok));
    const bool normals_valid = invert_ok && !corner_normals.is_empty();

    CausticObjectData &object = object_buf_[entry.table_index];
    object.min_bound = min_bound;
    object.max_bound = max_bound;
    object.transmissive = entry.transmissive ? 1 : 0;
    object.ior = entry.ior;
    object.cauchy_b = dispersion_coeffs.y;
    object.cauchy_c = dispersion_coeffs.z;
    object.material_offset = entry.material_offset;
    object.material_count = entry.material_count;

    global_min = math::min(global_min, min_bound);
    global_max = math::max(global_max, max_bound);

    for (int t = 0; t < entry.tri_count; t++) {
      const int3 tri = tris[t];
      float3 n[3];
      for (int k = 0; k < 3; k++) {
        const int corner = (&tri.x)[k];
        const float3 p = math::transform_point(world, positions[corner_verts[corner]]);
        vertex_data[vertex_offset + k] = p;
        n[k] = normals_valid ? caustic_safe_normalize(normal_matrix * corner_normals[corner],
                                                      float3(0.0f, 0.0f, 1.0f)) :
                               float3(0.0f, 0.0f, 1.0f);
      }

      CausticTriangleData &data = triangle_buf_[entry.tri_offset + t];
      data.n0 = n[0];
      data.n1 = n[1];
      data.n2 = n[2];
      data.object_index = entry.table_index;
      /* Per face material lookup so a mesh can mix transmissive and opaque materials. */
      const int face = tri_faces[t];
      const int slot = clamp_i(material_indices[face], 0, entry.material_count - 1);
      data.material_index = entry.material_offset + slot;

      vertex_offset += 3;
    }
  }

  object_buf_.push_update();
  triangle_buf_.push_update();

  GPU_indexbuf_use(index_buf_.get());
  GPU_vertbuf_use(vertex_buf_.get());

  blas_ = gpu::BottomLevelASPtr(GPU_ray_tracing_blas_alloc("CausticBLAS"));
  blas_->add_geometry(*index_buf_, *vertex_buf_);
  blas_->build();

  tlas_ = gpu::TopLevelASPtr(GPU_ray_tracing_tlas_alloc("CausticTLAS"));
  tlas_->add_instance(*blas_, float4x4::identity());
  tlas_->build();

  scene_center_ = (global_min + global_max) * 0.5f;
  scene_radius_ = math::max(math::length(global_max - global_min) * 0.5f, 0.01f);
  epsilon_ = math::max(1.0e-5f, scene_radius_ * 1.0e-5f);

  /* Volumetric caustic grid: tightly fitted around the scene with a small margin. */
  {
    const float3 extent = global_max - global_min;
    const float max_extent = math::max(extent.x, math::max(extent.y, extent.z));
    const float margin = math::max(max_extent * 0.01f, 1.0e-3f);
    volume_min_ = global_min - float3(margin);
    volume_max_ = global_max + float3(margin);
    volume_size_inv_ = 1.0f / math::max(volume_max_ - volume_min_, float3(1.0e-6f));
  }
}

void CausticModule::sync_lights()
{
  const eEvaluationMode eval_mode = DEG_get_mode(inst_.depsgraph);

  DEGObjectIterSettings iter_settings = {nullptr};
  iter_settings.depsgraph = inst_.depsgraph;
  iter_settings.flags = DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY |
                        DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET;

  Vector<CausticLightData> lights;
  Vector<float> powers;

  /* Sun lights are placed on a virtual dome. The distance only needs to be large compared to the
   * scene for the cone approximation to hold, and the emitted power is scaled accordingly. */
  const float sun_distance = math::max(scene_radius_ * 8.0f, 10.0f);

  DEG_OBJECT_ITER_BEGIN (&iter_settings, ob) {
    if (ob->type != OB_LAMP) {
      continue;
    }
    if (!DEG_iterator_object_is_visible(eval_mode, ob)) {
      continue;
    }

    const ::blender::Light *la = reinterpret_cast<const ::blender::Light *>(ob->data);
    if (la == nullptr) {
      continue;
    }

    float power = BKE_light_power(*la);
    float3 color = BKE_light_color(*la);
    const float luminance = math::dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    if (power <= 0.0f || luminance <= 0.0f) {
      continue;
    }
    color /= luminance;

    const float4x4 &world = ob->object_to_world();
    const float3 location = world.location();
    const float3 axis_z = caustic_safe_normalize(world.z_axis(), float3(0.0f, 0.0f, 1.0f));
    /* Lights emit toward their local -Z axis. */
    const float3 travel = -axis_z;

    CausticLightData light = {};
    light.position = location;
    light.direction = travel;
    light.axis_x = caustic_safe_normalize(world.x_axis(), float3(1.0f, 0.0f, 0.0f));
    light.axis_y = caustic_safe_normalize(world.y_axis(), float3(0.0f, 1.0f, 0.0f));
    light.radius = la->radius;
    light.color = color;
    light.cos_angle = -1.0f;
    light.spot_blend = la->spotblend;
    light.sun_angle = la->sun_angle;
    light.select_cdf = 0.0f;
    light.area_shape = 0;

    switch (la->type) {
      case LA_SUN:
        light.type = 1;
        light.position = scene_center_ - travel * sun_distance;
        /* `energy` is an irradiance, so the total emitted power over the sphere is E * 4PI * d^2. */
        power *= 4.0f * float(M_PI) * sun_distance * sun_distance;
        break;
      case LA_SPOT:
        light.type = 2;
        light.cos_angle = cosf(clamp_f(la->spotsize * 0.5f, 0.0f, float(M_PI)));
        break;
      case LA_AREA: {
        light.type = 3;
        /* The shader only distinguishes rectangles (0) from disks / ellipses (1). */
        const bool round_shape = (la->area_shape == LA_AREA_DISK || la->area_shape == LA_AREA_ELLIPSE);
        light.area_shape = round_shape ? 1 : 0;
        const float sx = math::length(world.x_axis());
        const float sy = math::length(world.y_axis());
        const bool irregular = (la->area_shape == LA_AREA_RECT || la->area_shape == LA_AREA_ELLIPSE);
        /* Half extents (rectangles) or radii (disks). */
        light.size_x = la->area_size * sx * 0.5f;
        light.size_y = (irregular ? la->area_sizey : la->area_size) * sy * 0.5f;
        break;
      }
      case LA_LOCAL:
      default:
        light.type = 0;
        break;
    }

    lights.append(light);
    powers.append(power);
  }
  DEG_OBJECT_ITER_END;

  light_count_ = int(lights.size());
  target_count_ = 0;

  if (light_count_ == 0) {
    light_buf_.clear_to_zero();
    target_buf_.clear_to_zero();
    photon_power_ = 0.0f;
    return;
  }

  /* Cumulative distribution used when no transmissive target is available. */
  float total_power = 0.0f;
  for (float power : powers) {
    total_power += power;
  }
  {
    float accum = 0.0f;
    for (int i = 0; i < light_count_; i++) {
      accum += powers[i] / total_power;
      lights[i].select_cdf = accum;
    }
  }

  /* Build the (light, transmissive object) target list. Pairs are weighted by emitted power times
   * the solid angle subtended by the object bounding sphere. */
  Vector<CausticTargetData> targets;
  Vector<float> weights;
  float weight_sum = 0.0f;

  for (int li = 0; li < light_count_; li++) {
    const CausticLightData &light = lights[li];
    for (int ti = 0; ti < transmissive_count_; ti++) {
      const CausticObjectData &object = object_buf_[ti];
      /* Prefer the tight bounding sphere when available: the axis aligned box diagonal overestimates
       * the radius of compact lenses, which sends most photons past the object. Rays that still miss
       * only add variance, the estimator itself stays unbiased since the cone sampling is uniform. */
      float3 center = (object.min_bound + object.max_bound) * 0.5f;
      float radius = math::length(object.max_bound - object.min_bound) * 0.5f;
      if (ti < int(target_spheres_.size()) && target_spheres_[ti].w > 0.0f) {
        const float4 sphere = target_spheres_[ti];
        center = float3(sphere.x, sphere.y, sphere.z);
        radius = sphere.w;
      }
      if (radius <= 0.0f) {
        continue;
      }

      const float3 to_target = center - light.position;
      const float dist = math::length(to_target);
      if (dist <= 1.0e-6f) {
        continue;
      }
      /* A spot light must actually cover the target, or the photons would be discarded. */
      if (light.type == 2) {
        const float cos_dir = math::dot(to_target / dist, light.direction);
        if (cos_dir < light.cos_angle) {
          continue;
        }
      }

      float omega;
      if (dist <= radius) {
        omega = 4.0f * float(M_PI);
      }
      else {
        const float sin_half = radius / dist;
        omega = 2.0f * float(M_PI) * (1.0f - sqrtf(math::max(0.0f, 1.0f - sin_half * sin_half)));
      }

      CausticTargetData target = {};
      target.center = center;
      target.radius = radius;
      target.light_index = li;
      target.omega = omega;
      target.cdf = 0.0f;
      targets.append(target);

      const float weight = powers[li] * omega;
      weights.append(weight);
      weight_sum += weight;
    }
  }

  if (targets.is_empty() || weight_sum <= 0.0f) {
    /* Nothing to aim at: fall back to uniform light sampling. */
    targets.clear_and_shrink();
    target_count_ = 0;
    target_buf_.clear_to_zero();
    photon_power_ = total_power / float(photon_count_) * CAUSTIC_RADIOMETRIC_SCALE;
  }
  else {
    float accum = 0.0f;
    for (int i = 0; i < int(targets.size()); i++) {
      accum += weights[i] / weight_sum;
      targets[i].cdf = accum;
    }
    target_count_ = int(targets.size());
    target_buf_.resize(target_count_);
    for (int i = 0; i < target_count_; i++) {
      target_buf_[i] = targets[i];
    }
    target_buf_.push_update();
    photon_power_ = weight_sum / (4.0f * float(M_PI) * float(photon_count_)) *
                    CAUSTIC_RADIOMETRIC_SCALE;
  }

  light_buf_.resize(light_count_);
  for (int i = 0; i < light_count_; i++) {
    light_buf_[i] = lights[i];
  }
  light_buf_.push_update();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Render
 * \{ */

void CausticModule::render(View &view)
{
  if (!render_pending_) {
    return;
  }

  /* While the viewport is being navigated the camera moves every redraw: the photon set would be
   * invalidated before it is even displayed, and the dispatch cost itself is what makes the
   * viewport feel like it lags behind. Clearing the map keeps the accumulated caustics of the
   * previous framing from being composited over the moving image. */
  const bool navigated = inst_.is_viewport() && inst_.is_navigating;
  const bool active = !navigated && enabled_ && tlas_ && triangle_count_ != 0 &&
                      light_count_ != 0 && photon_power_ > 0.0f && photon_count_ > 0;
  /* Dispatch on every TAA sample: each sample emits a decorrelated photon set and the resolve pass
   * averages it into the caustic map, which is what turns the sparse photon deposits into a smooth
   * caustic. */
  const int sample_index = caustic_sequence_sample_++;

  /* The combine pass samples this map unconditionally, so it must never keep stale data. It is only
   * reset at the first sample of a sequence (or while the module is inactive) because the resolve
   * pass progressively averages the map over the TAA samples to remove the photon speckle. */
  caustic_map_.clear(!active || sample_index == 0);
  /* Reset the throughput probe. The trace pass accumulates into it this frame and `sync()` reads it
   * back on the next one. */
  stats_buf_.clear_to_zero();
  stats_pending_ = true;

  if (!active) {
    return;
  }

  const int active_count = photon_count_;
  if (active_count <= 0) {
    return;
  }

  if (photon_pool_.size() < active_count) {
    photon_pool_.resize(active_count);
  }

  const int dispatch_groups = (active_count + 255) / 256;

  /* 1. Emit Pass */
  {
    PassSimple &pass = photon_emit_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_EMIT));

    pass.bind_ssbo("photons", photon_pool_);
    pass.bind_ssbo("lights", light_buf_);
    pass.bind_ssbo("targets", target_buf_);

    const int light_count = light_count_;
    const int target_count = target_count_;

    pass.push_constant("photon_count", &active_count);
    pass.push_constant("light_count", &light_count);
    pass.push_constant("target_count", &target_count);
    pass.push_constant("photon_power", &photon_power_);
    pass.push_constant("photon_seed", &sample_index);

    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_STORAGE);
    inst_.manager->submit(pass, view);
  }

  /* 2. Trace Pass. Hardware ray queries against every visible mesh. */
  {
    PassSimple &pass = photon_trace_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_TRACE));

    pass.bind_ssbo("photons", photon_pool_);
    pass.bind_ssbo("triangles", triangle_buf_);
    pass.bind_ssbo("materials", material_buf_);
    pass.bind_ssbo("stats", &stats_buf_);
    pass.bind_tlas("scene_as", tlas_.get());
    pass.bind_image("caustic_volume_r_img", &caustic_map_.volume_accum_r_texture());
    pass.bind_image("caustic_volume_g_img", &caustic_map_.volume_accum_g_texture());
    pass.bind_image("caustic_volume_b_img", &caustic_map_.volume_accum_b_texture());

    const int max_bounces = max_bounces_;

    pass.push_constant("photon_count", &active_count);
    pass.push_constant("max_bounces", &max_bounces);
    pass.push_constant("epsilon", &epsilon_);

    const float4 volume_min_gain = float4(volume_min_, volume_gain_);
    const float4 volume_scale = float4(volume_size_inv_, scene_radius_ * 8.0f);
    const int3 volume_res = int3(CAUSTIC_VOLUME_RES, CAUSTIC_VOLUME_RES, CAUSTIC_VOLUME_RES);
    const float4 camera_position = float4(view.location(), 0.0f);
    pass.push_constant("volume_min_gain", &volume_min_gain);
    pass.push_constant("volume_scale", &volume_scale);
    pass.push_constant("volume_res", &volume_res);
    pass.push_constant("camera_position", &camera_position);

    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_STORAGE);
    inst_.manager->submit(pass, view);
  }

  /* 3. Splat Pass. Screen space only, so a single dispatch into the level 0 region. */
  {
    PassSimple &pass = photon_splat_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_SPLAT));

    pass.bind_ssbo("photons", photon_pool_);
    pass.bind_image("caustic_accum_r_img", &caustic_map_.accum_r_texture());
    pass.bind_image("caustic_accum_g_img", &caustic_map_.accum_g_texture());
    pass.bind_image("caustic_accum_b_img", &caustic_map_.accum_b_texture());
    pass.bind_image("caustic_accum_depth_img", &caustic_map_.accum_depth_texture());

    pass.push_constant("photon_count", &active_count);

    const float4x4 &viewmat = view.viewmat();
    const float4x4 &winmat = view.winmat();
    pass.push_constant("viewmat", &viewmat);
    pass.push_constant("winmat", &winmat);
    pass.push_constant("splat_gain", &splat_gain_);
    pass.push_constant("splat_radius", &splat_radius_);

    pass.dispatch(int3(dispatch_groups, 1, 1));
    pass.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
    inst_.manager->submit(pass, view);
  }

  /* 4. Resolve Pass: convert the integer accumulation into the float map sampled by the
   * deferred combine. */
  {
    PassSimple &pass = photon_resolve_ps_;
    pass.init();
    pass.shader_set(inst_.shaders.static_shader_get(PHOTON_RESOLVE));

    pass.bind_image("caustic_accum_r_img", &caustic_map_.accum_r_texture());
    pass.bind_image("caustic_accum_g_img", &caustic_map_.accum_g_texture());
    pass.bind_image("caustic_accum_b_img", &caustic_map_.accum_b_texture());
    pass.bind_image("caustic_accum_depth_img", &caustic_map_.accum_depth_texture());
    pass.bind_image("caustic_color_img", &caustic_map_.color_texture());

    /* The progressive average weight must follow the module sequence counter, which restarts with
     * the framing. `Sampling::sample_index()` keeps growing across viewport redraws, which would
     * give the freshest samples an almost zero weight and keep the previous framing visible. */
    pass.push_constant("caustic_sample_index", &sample_index);

    const int2 res = caustic_map_.resolution();
    const int2 res_xy = math::divide_ceil(res, int2(8));
    pass.dispatch(int3(res_xy, 1));
    pass.barrier(GPU_BARRIER_TEXTURE_FETCH);
    inst_.manager->submit(pass, view);
  }
}

/** \} */

}  // namespace blender::eevee
