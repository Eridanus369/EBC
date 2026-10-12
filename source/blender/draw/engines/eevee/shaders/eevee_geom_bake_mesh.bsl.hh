/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Eevee color bake geometry pass.
 *
 * Rasterizes geometry into UV space using the bake UV layer as clip-space position. World
 * position/normal and the primitive id are forwarded to the bake fragment shaders so that they
 * can evaluate the material graph at the correct surface point and reject margin pixels.
 */
#pragma once

#include "infos/eevee_geom_infos.hh"
#include "infos/eevee_nodetree_infos.hh"

#include "draw_model.bsl.hh"
#include "draw_view.bsl.hh"
#include "eevee_attributes_mesh_lib.glsl"
#include "eevee_nodetree_vert_lib.glsl"
#include "eevee_pipeline.bsl.hh"
#include "eevee_reverse_z_lib.bsl.hh"
#include "eevee_surf_common.bsl.hh"

namespace eevee {

struct GeomBakeMesh {
  [[legacy_info]] ShaderCreateInfo eevee_geom_iface_info;
  [[legacy_info]] ShaderCreateInfo eevee_bake_geom_iface_info;
};

struct GeomBakeMeshVertIn {
  [[attribute(0)]] float3 pos;
  [[attribute(1)]] float3 nor;
  [[attribute(2)]] float2 bake_uv;
  [[attribute(3)]] float3 geom_nor;
  [[attribute(4)]] int primitive_id;
};

[[vertex]] void geom_bake_mesh(
    [[resource_table]] const GeomBakeMesh & /*srt*/,
    [[resource_table]] const Uniform &uni,
    [[instance_index]] const int inst_index,
    [[resource_table]] const draw::View &views,
    [[resource_table]] const draw::Model &models,
    [[resource_table]] const draw::Infos &infos,
    [[resource_table]] const draw::Resource &res_id,
    [[in]] const GeomBakeMeshVertIn &vert_in,
    [[vertex_id]] const int /*vert_id*/,
    [[position]] float4 &out_position)
{
  draw::ID id = res_id.get(inst_index);
  uint view_id = 0;
  uint resource_id = id.resource_id<1>();

  const ViewMatrices view = views.get(view_id);
  const ObjectMatrices obj = models.get(resource_id);

  auto &interp = interface_get(eevee_geom_iface_info, interp);
  auto &interp_flat = interface_get(eevee_geom_iface_info, interp_flat);
  auto &bake_interp = interface_get(eevee_bake_geom_iface_info, bake_interp);

  init_interface(id.raw_id);
  interp_flat.resource_id_raw = id.raw_id;

  interp.P = obj.point_object_to_world(vert_in.pos);
  interp.N = normalize(obj.normal_object_to_world(vert_in.nor));
  bake_interp.Ng = normalize(obj.normal_object_to_world(vert_in.geom_nor));
  bake_interp.primitive_id = vert_in.primitive_id;

  ObjectInfos ob_infos = infos.get(resource_id);
  float3 lP_orco = vert_in.pos * ob_infos.orco_mul + ob_infos.orco_add;

  init_globals(uni, view, true);
  attrib_load(MeshVertex{vert_in.pos, to_float3x3(obj.model_inverse), lP_orco});

  interp.P += nodetree_displacement();

  out_position = float4(vert_in.bake_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

/* Same as above but without init_globals / displacement — used for the light-shader surface pass
 * where only world position and normal are needed. */
[[vertex]] void geom_bake_mesh_basic(
    [[resource_table]] const GeomBakeMesh & /*srt*/,
    [[instance_index]] const int inst_index,
    [[resource_table]] const draw::View & /*views*/,
    [[resource_table]] const draw::Model &models,
    [[resource_table]] const draw::Resource &res_id,
    [[in]] const GeomBakeMeshVertIn &vert_in,
    [[vertex_id]] const int /*vert_id*/,
    [[position]] float4 &out_position)
{
  draw::ID id = res_id.get(inst_index);
  uint resource_id = id.resource_id<1>();
  const ObjectMatrices obj = models.get(resource_id);

  auto &interp = interface_get(eevee_geom_iface_info, interp);
  auto &interp_flat = interface_get(eevee_geom_iface_info, interp_flat);
  auto &bake_interp = interface_get(eevee_bake_geom_iface_info, bake_interp);

  init_interface(id.raw_id);
  interp_flat.resource_id_raw = id.raw_id;

  interp.P = obj.point_object_to_world(vert_in.pos);
  interp.N = normalize(obj.normal_object_to_world(vert_in.nor));
  bake_interp.Ng = normalize(obj.normal_object_to_world(vert_in.geom_nor));
  bake_interp.primitive_id = vert_in.primitive_id;

  out_position = float4(vert_in.bake_uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

}  // namespace eevee
