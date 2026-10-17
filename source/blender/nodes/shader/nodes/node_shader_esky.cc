/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 */

#include <string>
#include "node_shader_util.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_global.hh"
#include "GPU_material.hh"

namespace blender {

namespace nodes::node_shader_esky_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_output<decl::Color>("Color"_ustr);
  b.add_output<decl::Float>("Cloud Alpha"_ustr);
  b.add_output<decl::Float>("Cloud Density"_ustr);
  b.add_output<decl::Float>("Cloud Transmittance"_ustr);

  b.add_input<decl::Vector>("view_dir"_ustr).default_value(float3(0.0f, 0.0f, 1.0f)).hide_value();
  b.add_input<decl::Float>("time_seconds"_ustr).default_value(0.0f);
  b.add_input<decl::Float>("sample_index"_ustr).default_value(0.0f).hide_value();
  
  PanelDeclarationBuilder &world = b.add_panel("世界与时间"_ustr).default_closed(true);
  world.add_input<decl::Float>("camera_height"_ustr).default_value(0.002f);
  world.add_input<decl::Float>("planet_radius"_ustr).default_value(6360.0f);
  world.add_input<decl::Float>("latitude"_ustr).default_value(35.0f);
  world.add_input<decl::Float>("longitude"_ustr).default_value(105.0f);
  world.add_input<decl::Float>("star_density"_ustr).default_value(1.0f);
  world.add_input<decl::Float>("star_brightness"_ustr).default_value(2.0f);
  world.add_input<decl::Bool>("aurora_enabled"_ustr).default_value(true);
  world.add_input<decl::Float>("aurora_intensity"_ustr).default_value(1.0f);
  world.add_input<decl::Float>("aurora_speed"_ustr).default_value(0.5f);
  world.add_input<decl::Float>("aurora_color_factor"_ustr).default_value(0.5f);

  PanelDeclarationBuilder &sun = b.add_panel("太阳日轮"_ustr).default_closed(true);
  sun.add_input<decl::Bool>("use_sun_system"_ustr).default_value(true);
  sun.add_input<decl::Float>("sun_height"_ustr).default_value(890.0f); /* 内置缩放 */
  sun.add_input<decl::Float>("sun_rotation"_ustr).default_value(57.3f); /* 内置缩放 */
  sun.add_input<decl::Float>("sun_temperature"_ustr).default_value(5778.0f);
  sun.add_input<decl::Float>("sun_size"_ustr).default_value(1.0f);
  sun.add_input<decl::Float>("sun_bloom"_ustr).default_value(1.0f);
  sun.add_input<decl::Float>("horizon_height"_ustr).default_value(10.0f); /* 内置缩放 */

  PanelDeclarationBuilder &atm = b.add_panel("大气环境"_ustr).default_closed(true);
  atm.add_input<decl::Bool>("use_atmosphere"_ustr).default_value(true);
  atm.add_input<decl::Float>("air_density"_ustr).default_value(1.0f);
  atm.add_input<decl::Float>("aerosol_concentration"_ustr).default_value(0.3f);
  atm.add_input<decl::Float>("ozone_amount"_ustr).default_value(1.0f);
  atm.add_input<decl::Float>("so2_concentration"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("pm25_concentration"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("sun_glow_suppress"_ustr).default_value(0.5f);
  atm.add_input<decl::Float>("atmosphere_steps"_ustr).default_value(4.0f);
  atm.add_input<decl::Color>("zenith_color"_ustr).default_value(ColorGeometry4f(0.015f, 0.10f, 0.35f, 1.0f));
  atm.add_input<decl::Color>("horizon_color"_ustr).default_value(ColorGeometry4f(0.30f, 0.55f, 0.85f, 1.0f));

  PanelDeclarationBuilder &high = b.add_panel("高云族"_ustr).default_closed(true);
  high.add_input<decl::Bool>("enable_high_cloud"_ustr).default_value(false);
  high.add_input<decl::Float>("high_cloud_base"_ustr).default_value(8.0f);
  high.add_input<decl::Float>("high_cloud_thickness"_ustr).default_value(1.0f);
  high.add_input<decl::Float>("high_cloud_type"_ustr).default_value(0.0f);

  PanelDeclarationBuilder &mid = b.add_panel("中云族"_ustr).default_closed(true);
  mid.add_input<decl::Bool>("enable_mid_cloud"_ustr).default_value(false);
  mid.add_input<decl::Float>("mid_cloud_base"_ustr).default_value(4.0f);
  mid.add_input<decl::Float>("mid_cloud_thickness"_ustr).default_value(1.5f);
  mid.add_input<decl::Float>("mid_cloud_type"_ustr).default_value(0.0f);

  PanelDeclarationBuilder &low = b.add_panel("低云族"_ustr).default_closed(true);
  low.add_input<decl::Bool>("enable_low_cloud"_ustr).default_value(true);
  low.add_input<decl::Float>("low_cloud_base"_ustr).default_value(0.5f);
  low.add_input<decl::Float>("low_cloud_thickness"_ustr).default_value(1.5f);
  low.add_input<decl::Float>("low_cloud_type"_ustr).default_value(1.0f);

  PanelDeclarationBuilder &vert = b.add_panel("直展云族"_ustr).default_closed(true);
  vert.add_input<decl::Bool>("enable_vertical_cloud"_ustr).default_value(false);
  vert.add_input<decl::Float>("vertical_cloud_base"_ustr).default_value(0.5f);
  vert.add_input<decl::Float>("vertical_cloud_top"_ustr).default_value(8.0f);
  vert.add_input<decl::Float>("vertical_cloud_type"_ustr).default_value(0.0f);

  PanelDeclarationBuilder &special = b.add_panel("特殊云种"_ustr).default_closed(true);
  special.add_input<decl::Bool>("enable_special_cloud"_ustr).default_value(false);
  special.add_input<decl::Float>("special_cloud_type"_ustr).default_value(3.0f);
  special.add_input<decl::Float>("special_cloud_base"_ustr).default_value(1.5f);
  special.add_input<decl::Float>("special_cloud_thickness"_ustr).default_value(3.5f);

  PanelDeclarationBuilder &glob = b.add_panel("云层全局参数"_ustr).default_closed(true);
  glob.add_input<decl::Float>("weather_uv_scale"_ustr).default_value(0.02f); /* 内置缩放 */
  glob.add_input<decl::Float>("coverage"_ustr).default_value(0.5f);
  glob.add_input<decl::Float>("density"_ustr).default_value(1.0f);
  glob.add_input<decl::Float>("basic_noise_scale"_ustr).default_value(0.3f);
  glob.add_input<decl::Float>("detail_noise_scale"_ustr).default_value(0.6f);
  glob.add_input<decl::Float>("wind_direction_x"_ustr).default_value(0.894427f);
  glob.add_input<decl::Float>("wind_direction_y"_ustr).default_value(0.447214f);
  glob.add_input<decl::Float>("cloud_speed"_ustr).default_value(0.05f);

  PanelDeclarationBuilder &weather = b.add_panel("天气状态"_ustr).default_closed(true);
  weather.add_input<decl::Float>("weather_state"_ustr).default_value(0.0f);
  weather.add_input<decl::Float>("rain_intensity"_ustr).default_value(0.5f);
  weather.add_input<decl::Float>("snow_intensity"_ustr).default_value(0.5f);
  weather.add_input<decl::Float>("lightning_frequency"_ustr).default_value(0.3f);

  PanelDeclarationBuilder &light = b.add_panel("云层光照"_ustr).default_closed(true);
  light.add_input<decl::Float>("sun_intensity"_ustr).default_value(3.0f);
  light.add_input<decl::Color>("cloud_albedo"_ustr).default_value(ColorGeometry4f(1.0f, 1.0f, 1.0f, 1.0f));
  light.add_input<decl::Color>("ambient_color"_ustr).default_value(ColorGeometry4f(0.10f, 0.18f, 0.32f, 1.0f));
  light.add_input<decl::Float>("ambient_strength"_ustr).default_value(0.55f);
  light.add_input<decl::Float>("phase_forward"_ustr).default_value(0.5f);
  light.add_input<decl::Float>("phase_backward"_ustr).default_value(-0.5f);
  light.add_input<decl::Float>("phase_mix"_ustr).default_value(0.5f);
  light.add_input<decl::Float>("powder_scale"_ustr).default_value(1.0f);
  light.add_input<decl::Float>("multi_scatter_extinction"_ustr).default_value(0.175f);
  light.add_input<decl::Float>("multi_scatter_strength"_ustr).default_value(1.0f);

  PanelDeclarationBuilder &qual = b.add_panel("质量"_ustr).default_closed(true);
  qual.add_input<decl::Float>("max_trace_distance"_ustr).default_value(50.0f);
  qual.add_input<decl::Float>("trace_start_limit"_ustr).default_value(350.0f);
  qual.add_input<decl::Float>("ray_steps"_ustr).default_value(96.0f);
  qual.add_input<decl::Float>("light_step"_ustr).default_value(0.025f);
  qual.add_input<decl::Float>("light_step_multiplier"_ustr).default_value(1.5f);
  qual.add_input<decl::Float>("light_steps"_ustr).default_value(12.0f);
}

static void node_shader_buts_esky(ui::Layout &layout, bContext * /*C*/, PointerRNA * /*ptr*/)
{
  layout.label("All Flower textures are internal.", ICON_INFO);
}

static int node_shader_gpu_esky(GPUMaterial *mat, bNode *node, bNodeExecData * /*execdata*/, GPUNodeStack *in, GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_LIGHTPROBE_ACCESS);
  
  const char* base = "/home/eridanusepsilon/Code/EBC/EBC5.3/Pic/Flower/";
  Image *img_basic = BKE_image_load_exists(G_MAIN, (std::string(base) + "cloud_basic_noise.png").c_str());
  Image *img_detail = BKE_image_load_exists(G_MAIN, (std::string(base) + "Flower_detail_noise.png").c_str());
  Image *img_weather = BKE_image_load_exists(G_MAIN, (std::string(base) + "Flower_weather.tga").c_str());
  Image *img_curl = BKE_image_load_exists(G_MAIN, (std::string(base) + "Flower_curl.png").c_str());
  Image *img_blue = BKE_image_load_exists(G_MAIN, (std::string(base) + "Flower_blue_noise.png").c_str());

  return GPU_stack_link(mat, node, "node_esky", in, out, 
    GPU_image(mat, img_basic, nullptr, GPUSamplerState::default_sampler()),
    GPU_image(mat, img_detail, nullptr, GPUSamplerState::default_sampler()),
    GPU_image(mat, img_weather, nullptr, GPUSamplerState::default_sampler()),
    GPU_image(mat, img_curl, nullptr, GPUSamplerState::default_sampler()),
    GPU_image(mat, img_blue, nullptr, GPUSamplerState::default_sampler()));
}

}  // namespace nodes::node_shader_esky_cc

void register_node_type_sh_esky()
{
  namespace file_ns = nodes::node_shader_esky_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeESky"_ustr, SH_NODE_ESKY);
  ntype.ui_name = "ESky";
  ntype.ui_description = "Procedural sky with built-in Flower textures";
  ntype.enum_name_legacy = "ESKY";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_shader_buts_esky;
  ntype.add_ui_poll = world_shader_nodes_poll;
  ntype.gpu_fn = file_ns::node_shader_gpu_esky;
  bke::node_register_type(ntype);
}

}  // namespace blender
