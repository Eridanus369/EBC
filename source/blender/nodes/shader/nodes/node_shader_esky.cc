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


static const EnumPropertyItem esky_weather_items[] = {
    {0, "CLEAR", 0, "晴朗", ""},
    {1, "RAIN", 0, "下雨", ""},
    {2, "STORM", 0, "雷暴", ""},
    {3, "SNOW", 0, "下雪", ""},
    {0, nullptr, 0, nullptr, nullptr}
};
static const EnumPropertyItem esky_high_items[] = {
    {0, "CIRRUS", 0, "卷云", ""},
    {1, "CIRROCUMULUS", 0, "卷积云", ""},
    {2, "CIRROSTRATUS", 0, "卷层云", ""},
    {0, nullptr, 0, nullptr, nullptr}
};
static const EnumPropertyItem esky_mid_items[] = {
    {0, "ALTOCUMULUS", 0, "高积云", ""},
    {1, "ALTOSTRATUS", 0, "高层云", ""},
    {0, nullptr, 0, nullptr, nullptr}
};
static const EnumPropertyItem esky_low_items[] = {
    {0, "STRATUS", 0, "层云", ""},
    {1, "STRATOCUMULUS", 0, "层积云", ""},
    {2, "NIMBOSTRATUS", 0, "雨层云", ""},
    {0, nullptr, 0, nullptr, nullptr}
};
static const EnumPropertyItem esky_vert_items[] = {
    {0, "CUMULUS", 0, "积云", ""},
    {1, "CUMULONIMBUS", 0, "积雨云", ""},
    {0, nullptr, 0, nullptr, nullptr}
};
static const EnumPropertyItem esky_special_items[] = {
    {0, "ASPERITAS", 0, "糙面云", ""},
    {1, "MAMMATUS", 0, "乳状云", ""},
    {2, "LENTICULAR", 0, "荚状云", ""},
    {3, "FLOWER", 0, "Flower云", ""},
    {0, nullptr, 0, nullptr, nullptr}
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_output<decl::Color>("Color"_ustr);
  b.add_output<decl::Float>("Cloud Alpha"_ustr);
  b.add_output<decl::Float>("Cloud Density"_ustr);
  b.add_output<decl::Float>("Cloud Transmittance"_ustr);

  PanelDeclarationBuilder &vp = b.add_panel("视口"_ustr).default_closed(true);
  vp.add_input<decl::Float>("altitude"_ustr).default_value(0.0f);
  vp.add_input<decl::Float>("exposure"_ustr).default_value(1.0f);
  vp.add_input<decl::Float>("hue"_ustr).default_value(0.5f);
  vp.add_input<decl::Float>("saturation"_ustr).default_value(1.0f);
  vp.add_input<decl::Float>("brightness"_ustr).default_value(1.0f);

  PanelDeclarationBuilder &star = b.add_panel("恒星"_ustr).default_closed(true);
  star.add_input<decl::Float>("sun_height"_ustr).default_value(45.0f);
  star.add_input<decl::Float>("sun_rotation"_ustr).default_value(0.0f);
  star.add_input<decl::Float>("sun_intensity"_ustr).default_value(2.0f);
  star.add_input<decl::Float>("sun_temperature"_ustr).default_value(5778.0f);
  star.add_input<decl::Float>("sun_size"_ustr).default_value(1.0f);
  star.add_input<decl::Float>("sun_bloom"_ustr).default_value(1.0f);
  star.add_input<decl::Float>("horizon_height"_ustr).default_value(0.0f);

  PanelDeclarationBuilder &space = b.add_panel("太空"_ustr).default_closed(true);
  space.add_input<decl::Float>("planet_radius"_ustr).default_value(6360.0f);
  space.add_input<decl::Float>("star_density"_ustr).default_value(1.0f);
  space.add_input<decl::Float>("star_brightness"_ustr).default_value(2.0f);
  space.add_input<decl::Float>("latitude"_ustr).default_value(31.47f);
  space.add_input<decl::Float>("longitude"_ustr).default_value(106.89f);
  
  PanelDeclarationBuilder &aurora = space.add_panel("极光"_ustr).default_closed(true);
  aurora.add_input<decl::Bool>("aurora_enabled"_ustr).default_value(true);
  aurora.add_input<decl::Float>("aurora_intensity"_ustr).default_value(0.2f);
  aurora.add_input<decl::Float>("aurora_speed"_ustr).default_value(0.3f);
  aurora.add_input<decl::Float>("aurora_color_factor"_ustr).default_value(0.0f);

  PanelDeclarationBuilder &moon = space.add_panel("月亮"_ustr).default_closed(true);
  moon.add_input<decl::Bool>("moon_enable"_ustr).default_value(true);
  moon.add_input<decl::Float>("moon_height"_ustr).default_value(30.0f);
  moon.add_input<decl::Float>("moon_rotation"_ustr).default_value(120.0f);
  moon.add_input<decl::Float>("moon_size"_ustr).default_value(4.0f);
  moon.add_input<decl::Float>("moon_intensity"_ustr).default_value(1.2f);

  PanelDeclarationBuilder &atm = b.add_panel("大气"_ustr).default_closed(true);
  atm.add_input<decl::Bool>("use_atmosphere"_ustr).default_value(true);
  atm.add_input<decl::Float>("air_density"_ustr).default_value(1.0f);
  atm.add_input<decl::Float>("aerosol_concentration"_ustr).default_value(0.3f);
  atm.add_input<decl::Float>("ozone_amount"_ustr).default_value(1.0f);
  atm.add_input<decl::Float>("so2_concentration"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("pm25_concentration"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("sun_glow_suppress"_ustr).default_value(0.5f);
  
  PanelDeclarationBuilder &atm_adv = atm.add_panel("高级"_ustr).default_closed(true);
  atm_adv.add_input<decl::Color>("zenith_color"_ustr).default_value(ColorGeometry4f(0.109f, 0.375f, 1.0f, 1.0f));
  atm_adv.add_input<decl::Color>("horizon_color"_ustr).default_value(ColorGeometry4f(0.3f, 0.55f, 0.85f, 1.0f));
  atm_adv.add_input<decl::Float>("atmosphere_steps"_ustr).default_value(12);

  PanelDeclarationBuilder &cloud = b.add_panel("云"_ustr).default_closed(true);
  PanelDeclarationBuilder &cloud_base = cloud.add_panel("基础参数"_ustr).default_closed(true);
  cloud_base.add_input<decl::Float>("weather_uv_scale"_ustr).default_value(2.41f);
  cloud_base.add_input<decl::Float>("coverage"_ustr).default_value(0.426f);
  cloud_base.add_input<decl::Float>("density"_ustr).default_value(2.82f);
  cloud_base.add_input<decl::Float>("basic_noise_scale"_ustr).default_value(0.7f);
  cloud_base.add_input<decl::Float>("detail_noise_scale"_ustr).default_value(1.39f);
  cloud_base.add_input<decl::Vector>("wind_direction"_ustr).default_value(float3(0.894f, 0.447f, 0.0f));
  cloud_base.add_input<decl::Float>("cloud_speed"_ustr).default_value(0.1f);
  cloud_base.add_input<decl::Float>("time_speed"_ustr).default_value(1.0f);
  
  PanelDeclarationBuilder &cloud_types = cloud.add_panel("云种类"_ustr).default_closed(true);
  PanelDeclarationBuilder &high = cloud_types.add_panel("高云族"_ustr).default_closed(true);
  high.add_input<decl::Bool>("high_cloud_enable"_ustr).default_value(true);
  high.add_input<decl::Menu>("high_cloud_type"_ustr).static_items(esky_high_items);
  high.add_input<decl::Float>("high_cloud_base"_ustr).default_value(8.0f);
  high.add_input<decl::Float>("high_cloud_thickness"_ustr).default_value(1.0f);

  PanelDeclarationBuilder &mid = cloud_types.add_panel("中云族"_ustr).default_closed(true);
  mid.add_input<decl::Bool>("mid_cloud_enable"_ustr).default_value(true);
  mid.add_input<decl::Menu>("mid_cloud_type"_ustr).static_items(esky_mid_items);
  mid.add_input<decl::Float>("mid_cloud_base"_ustr).default_value(4.0f);
  mid.add_input<decl::Float>("mid_cloud_thickness"_ustr).default_value(1.5f);

  PanelDeclarationBuilder &low = cloud_types.add_panel("低云族"_ustr).default_closed(true);
  low.add_input<decl::Bool>("low_cloud_enable"_ustr).default_value(true);
  low.add_input<decl::Menu>("low_cloud_type"_ustr).static_items(esky_low_items);
  low.add_input<decl::Float>("low_cloud_base"_ustr).default_value(0.5f);
  low.add_input<decl::Float>("low_cloud_thickness"_ustr).default_value(1.5f);

  PanelDeclarationBuilder &vert = cloud_types.add_panel("直展云族"_ustr).default_closed(true);
  vert.add_input<decl::Bool>("vertical_cloud_enable"_ustr).default_value(true);
  vert.add_input<decl::Menu>("vertical_cloud_type"_ustr).static_items(esky_vert_items);
  vert.add_input<decl::Float>("vertical_cloud_base"_ustr).default_value(0.5f);
  vert.add_input<decl::Float>("vertical_cloud_top"_ustr).default_value(8.0f);

  PanelDeclarationBuilder &special = cloud_types.add_panel("特殊云族"_ustr).default_closed(true);
  special.add_input<decl::Bool>("special_cloud_enable"_ustr).default_value(true);
  special.add_input<decl::Menu>("special_cloud_type"_ustr).static_items(esky_special_items);
  special.add_input<decl::Float>("special_cloud_base"_ustr).default_value(3.0f);
  special.add_input<decl::Float>("special_cloud_thickness"_ustr).default_value(0.5f);

  PanelDeclarationBuilder &cloud_adv = cloud.add_panel("高级"_ustr).default_closed(true);
  cloud_adv.add_input<decl::Color>("cloud_albedo"_ustr).default_value(ColorGeometry4f(0.99f, 0.99f, 0.99f, 1.0f));
  cloud_adv.add_input<decl::Color>("ambient_color"_ustr).default_value(ColorGeometry4f(0.117f, 0.238f, 0.462f, 1.0f));
  cloud_adv.add_input<decl::Float>("ambient_strength"_ustr).default_value(1.24f);
  cloud_adv.add_input<decl::Float>("phase_forward"_ustr).default_value(0.5f);
  cloud_adv.add_input<decl::Float>("phase_backward"_ustr).default_value(-0.5f);
  cloud_adv.add_input<decl::Float>("phase_mix"_ustr).default_value(0.5f);
  cloud_adv.add_input<decl::Float>("powder_scale"_ustr).default_value(0.68f);
  cloud_adv.add_input<decl::Float>("multi_scatter_extinction"_ustr).default_value(0.505f);
  cloud_adv.add_input<decl::Float>("multi_scatter_strength"_ustr).default_value(1.08f);

  PanelDeclarationBuilder &weather = b.add_panel("天气"_ustr).default_closed(true);
  weather.add_input<decl::Menu>("weather_type"_ustr).static_items(esky_weather_items);
  PanelDeclarationBuilder &rain = weather.add_panel("下雨"_ustr).default_closed(true);
  rain.add_input<decl::Float>("rain_intensity"_ustr).default_value(0.498f);
  rain.add_input<decl::Bool>("lens_rain_enable"_ustr).default_value(false);
  rain.add_input<decl::Float>("lens_rain_strength"_ustr).default_value(0.6f);
  rain.add_input<decl::Float>("lens_rain_length"_ustr).default_value(0.5f);
  weather.add_input<decl::Float>("snow_intensity"_ustr).default_value(0.5f);
  weather.add_input<decl::Float>("lightning_frequency"_ustr).default_value(0.854f);

  PanelDeclarationBuilder &sim = b.add_panel("模拟"_ustr).default_closed(true);
  sim.add_input<decl::Float>("max_trace_distance"_ustr).default_value(50.0f);
  sim.add_input<decl::Float>("trace_start_limit"_ustr).default_value(350.0f);
  sim.add_input<decl::Float>("ray_steps"_ustr).default_value(84);
  sim.add_input<decl::Float>("light_step"_ustr).default_value(0.025f);
  sim.add_input<decl::Float>("light_step_multiplier"_ustr).default_value(1.5f);
  sim.add_input<decl::Float>("light_steps"_ustr).default_value(12);

}



/** Load an image, never returning a link built from a null image.
 *  A null image would end up binding a null GPU texture at draw time (SIGSEGV). */
static GPUNodeLink *esky_gpu_image(GPUMaterial *mat, const char *path, Image *fallback)
{
  Image *ima = BKE_image_load_exists(G_MAIN, path);
  if (ima == nullptr) {
    ima = fallback;
  }
  if (ima == nullptr) {
    ima = BKE_image_load_exists(G_MAIN,
                                "/home/eridanusepsilon/Code/EBC/EBC5.3/Pic/Flower/"
                                "Flower_blue_noise.png");
  }
  if (ima == nullptr) {
    return nullptr;
  }
  return GPU_image(mat, ima, nullptr, GPUSamplerState::default_sampler());
}

static int node_shader_gpu_esky(GPUMaterial *mat, bNode *node, bNodeExecData * /*execdata*/, GPUNodeStack *in, GPUNodeStack *out)
{
  const char *dir = "/home/eridanusepsilon/Code/EBC/EBC5.3/Pic/Flower/";
  Image *fallback = BKE_image_load_exists(
      G_MAIN, "/home/eridanusepsilon/Code/EBC/EBC5.3/Pic/Flower/Flower_blue_noise.png");

  GPUNodeLink *img_basic = esky_gpu_image(mat, (std::string(dir) + "cloud_basic_noise.png").c_str(), fallback);
  GPUNodeLink *img_detail = esky_gpu_image(mat, (std::string(dir) + "Flower_detail_noise.png").c_str(), fallback);
  GPUNodeLink *img_weather = esky_gpu_image(mat, (std::string(dir) + "Flower_weather_fixed.png").c_str(), fallback);
  GPUNodeLink *img_curl = esky_gpu_image(mat, (std::string(dir) + "Flower_curl.png").c_str(), fallback);
  GPUNodeLink *img_blue = esky_gpu_image(mat, (std::string(dir) + "Flower_blue_noise.png").c_str(), fallback);
  GPUNodeLink *img_moon = esky_gpu_image(mat, (std::string(dir) + "moon_01_diff_1k_fixed.png").c_str(), fallback);

  if (img_basic == nullptr || img_detail == nullptr || img_weather == nullptr ||
      img_curl == nullptr || img_blue == nullptr || img_moon == nullptr)
  {
    return false;
  }

  return GPU_stack_link(mat, node, "node_esky", in, out, img_basic, img_detail, img_weather, img_curl, img_blue, img_moon);
}





















}  // namespace nodes::node_shader_esky_cc

void register_node_type_sh_esky()
{
  namespace file_ns = nodes::node_shader_esky_cc;
  static bke::bNodeType ntype;
  sh_node_type_base(&ntype, "ShaderNodeESky"_ustr, SH_NODE_ESKY);
  ntype.ui_name = "ESky";
  ntype.ui_description = "Procedural sky with built-in Flower textures";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::node_shader_gpu_esky;
  bke::node_register_type(ntype);
}

}  // namespace blender
