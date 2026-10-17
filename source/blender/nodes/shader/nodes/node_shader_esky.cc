/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 */

#include "node_shader_util.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {

namespace nodes::node_shader_esky_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.is_function_node();

  b.add_input<decl::Vector>("Direction"_ustr)
      .default_value(float3(0.0f, 0.0f, 0.0f))
      .hide_value();
  b.add_input<decl::Float>("Time"_ustr).default_value(0.0f);
  b.add_input<decl::Float>("Sample Index"_ustr).default_value(0.0f).hide_value();

  auto &viewport = b.add_panel("视口"_ustr).default_closed(false);
  viewport.add_input<decl::Float>("海拔"_ustr).default_value(0.002f);
  viewport.add_input<decl::Float>("曝光"_ustr).default_value(1.0f);
  viewport.add_input<decl::Float>("Hue"_ustr).default_value(0.5f);
  viewport.add_input<decl::Float>("Saturation"_ustr).default_value(1.0f);
  viewport.add_input<decl::Float>("Brightness"_ustr).default_value(1.6f);

  auto &sun = b.add_panel("恒星"_ustr).default_closed(false);
  sun.add_input<decl::Float>("太阳高度"_ustr).default_value(16.6f);
  sun.add_input<decl::Float>("太阳方位"_ustr).default_value(0.0f);
  sun.add_input<decl::Float>("太阳强度"_ustr).default_value(30.0f);
  sun.add_input<decl::Float>("太阳温度"_ustr).default_value(4000.0f);
  sun.add_input<decl::Float>("太阳尺寸"_ustr).default_value(10.0f);
  sun.add_input<decl::Float>("太阳辉光"_ustr).default_value(0.2f);
  sun.add_input<decl::Float>("地平线高度"_ustr).default_value(-0.5f);

  auto &space = b.add_panel("太空"_ustr).default_closed(false);
  space.add_input<decl::Float>("行星半径"_ustr).default_value(6450.0f);
  space.add_input<decl::Float>("星星密度"_ustr).default_value(50.0f);
  space.add_input<decl::Float>("星星亮度"_ustr).default_value(50.0f);
  space.add_input<decl::Float>("纬度"_ustr).default_value(57.3f);
  space.add_input<decl::Float>("经度"_ustr).default_value(45.8f);
  
  auto &aurora = space.add_panel("极光"_ustr).default_closed(false);
  aurora.add_input<decl::Float>("启用极光"_ustr).default_value(false);
  aurora.add_input<decl::Float>("极光强度"_ustr).default_value(0.2f);
  aurora.add_input<decl::Float>("极光速度"_ustr).default_value(0.5f);
  aurora.add_input<decl::Float>("极光颜色因子"_ustr).default_value(0.0f);

  auto &atm = b.add_panel("大气"_ustr).default_closed(false);
  atm.add_input<decl::Float>("启用大气"_ustr).default_value(false);
  atm.add_input<decl::Float>("大气密度"_ustr).default_value(0.5f);
  atm.add_input<decl::Float>("气溶胶浓度"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("臭氧浓度"_ustr).default_value(1.5f);
  atm.add_input<decl::Float>("SO2浓度"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("PM2.5浓度"_ustr).default_value(0.0f);
  atm.add_input<decl::Float>("太阳辉光抑制"_ustr).default_value(0.0f);
  
  auto &atm_adv = atm.add_panel("高级"_ustr).default_closed(false);
  atm_adv.add_input<decl::Color>("天顶颜色"_ustr).default_value(ColorGeometry4f(0.3f, 0.6f, 1.0f, 1.0f));
  atm_adv.add_input<decl::Color>("地平线颜色"_ustr).default_value(ColorGeometry4f(0.05f, 0.05f, 0.1f, 1.0f));
  atm_adv.add_input<decl::Float>("大气步进数"_ustr).default_value(5.0f);

  auto &cloud = b.add_panel("云"_ustr).default_closed(false);
  auto &cloud_basic = cloud.add_panel("基础参数"_ustr).default_closed(false);
  cloud_basic.add_input<decl::Float>("天气图缩放"_ustr).default_value(2.51f);
  cloud_basic.add_input<decl::Float>("覆盖率"_ustr).default_value(0.426f);
  cloud_basic.add_input<decl::Float>("密度"_ustr).default_value(4.62f);
  cloud_basic.add_input<decl::Float>("基础噪声缩放"_ustr).default_value(0.7f);
  cloud_basic.add_input<decl::Float>("细节噪声缩放"_ustr).default_value(1.39f);
  cloud_basic.add_input<decl::Float>("风向X"_ustr).default_value(1.0f);
  cloud_basic.add_input<decl::Float>("风向Y"_ustr).default_value(0.0f);
  cloud_basic.add_input<decl::Float>("云速"_ustr).default_value(1.0f);

  auto &cloud_types = cloud.add_panel("云种类"_ustr).default_closed(false);
  auto &high = cloud_types.add_panel("高云族"_ustr).default_closed(false);
  high.add_input<decl::Float>("启用高云"_ustr).default_value(false);
  high.add_input<decl::Float>("高云类型"_ustr).default_value(2.0f);
  high.add_input<decl::Float>("高云底高度"_ustr).default_value(8.8f);
  high.add_input<decl::Float>("高云厚度"_ustr).default_value(1.0f);
  
  auto &mid = cloud_types.add_panel("中云族"_ustr).default_closed(false);
  mid.add_input<decl::Float>("启用中云"_ustr).default_value(false);
  mid.add_input<decl::Float>("中云类型"_ustr).default_value(1.0f);
  mid.add_input<decl::Float>("中云底高度"_ustr).default_value(4.0f);
  mid.add_input<decl::Float>("中云厚度"_ustr).default_value(1.5f);

  auto &low = cloud_types.add_panel("低云族"_ustr).default_closed(false);
  low.add_input<decl::Float>("启用低云"_ustr).default_value(true);
  low.add_input<decl::Float>("低云类型"_ustr).default_value(2.0f);
  low.add_input<decl::Float>("低云底高度"_ustr).default_value(0.5f);
  low.add_input<decl::Float>("低云厚度"_ustr).default_value(1.5f);

  auto &vert = cloud_types.add_panel("直展云族"_ustr).default_closed(false);
  vert.add_input<decl::Float>("启用直展云"_ustr).default_value(false);
  vert.add_input<decl::Float>("直展云类型"_ustr).default_value(0.0f);
  vert.add_input<decl::Float>("直展云底高度"_ustr).default_value(0.5f);
  vert.add_input<decl::Float>("直展云顶高度"_ustr).default_value(8.0f);

  auto &special = cloud_types.add_panel("特殊云族"_ustr).default_closed(false);
  special.add_input<decl::Float>("启用特殊云"_ustr).default_value(false);
  special.add_input<decl::Float>("特殊云类型"_ustr).default_value(3.0f);
  special.add_input<decl::Float>("特殊云底高度"_ustr).default_value(1.5f);
  special.add_input<decl::Float>("特殊云厚度"_ustr).default_value(3.5f);

  auto &cloud_adv = cloud.add_panel("高级"_ustr).default_closed(false);
  cloud_adv.add_input<decl::Color>("云反照率"_ustr).default_value(ColorGeometry4f(0.5f, 0.6f, 0.7f, 1.0f));
  cloud_adv.add_input<decl::Color>("环境云光"_ustr).default_value(ColorGeometry4f(0.2f, 0.4f, 0.8f, 1.0f));
  cloud_adv.add_input<decl::Float>("环境光强度"_ustr).default_value(4.0f);
  cloud_adv.add_input<decl::Float>("前向相位"_ustr).default_value(0.5f);
  cloud_adv.add_input<decl::Float>("后向相位"_ustr).default_value(-0.5f);
  cloud_adv.add_input<decl::Float>("双相位混合"_ustr).default_value(0.5f);
  cloud_adv.add_input<decl::Float>("Powder 强度"_ustr).default_value(0.68f);
  cloud_adv.add_input<decl::Float>("多次散射消光"_ustr).default_value(0.505f);
  cloud_adv.add_input<decl::Float>("多次散射强度"_ustr).default_value(1.08f);

  auto &weather = b.add_panel("天气"_ustr).default_closed(false);
  weather.add_input<decl::Float>("降雨强度"_ustr).default_value(0.527f);
  weather.add_input<decl::Float>("降雪强度"_ustr).default_value(0.5f);
  weather.add_input<decl::Float>("闪电频率"_ustr).default_value(0.854f);

  auto &sim = b.add_panel("模拟"_ustr).default_closed(false);
  sim.add_input<decl::Float>("最大步进距离"_ustr).default_value(50.0f);
  sim.add_input<decl::Float>("最远起步距离"_ustr).default_value(350.0f);
  sim.add_input<decl::Float>("视线步数"_ustr).default_value(84);
  sim.add_input<decl::Float>("光线基础步长"_ustr).default_value(0.025f);
  sim.add_input<decl::Float>("光线步长倍率"_ustr).default_value(1.5f);
  sim.add_input<decl::Float>("光线步数"_ustr).default_value(12);

  b.add_output<decl::Color>("着色"_ustr);
  b.add_output<decl::Float>("Cloud Alpha"_ustr);
  b.add_output<decl::Float>("Cloud Density"_ustr);
  b.add_output<decl::Float>("Cloud Transmittance"_ustr);
}

static void node_shader_buts_esky(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "weather_state", ui::ITEM_R_SPLIT_EMPTY_NAME, "", ICON_NONE);
}

static void node_shader_init_esky(bNodeTree * /*ntree*/, bNode *node)
{
  node->custom1 = 0; /* CLEAR */
}

static int node_shader_gpu_esky(GPUMaterial *mat,
                                bNode *node,
                                bNodeExecData * /*execdata*/,
                                GPUNodeStack *in,
                                GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_LIGHTPROBE_ACCESS);
  float weather = float(node->custom1);
  return GPU_stack_link(mat, node, "node_esky", in, out, GPU_constant(&weather));
}

}  // namespace nodes::node_shader_esky_cc

void register_node_type_sh_esky()
{
  namespace file_ns = nodes::node_shader_esky_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeESky"_ustr, SH_NODE_ESKY);
  ntype.ui_name = "ESky";
  ntype.ui_description = "Procedural sky, atmosphere, sun, clouds, weather and night sky (EEVEE only)";
  ntype.enum_name_legacy = "ESKY";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_shader_buts_esky;
  ntype.initfunc = file_ns::node_shader_init_esky;
  ntype.add_ui_poll = world_shader_nodes_poll;
  ntype.gpu_fn = file_ns::node_shader_gpu_esky;

  bke::node_register_type(ntype);
}

}  // namespace blender
