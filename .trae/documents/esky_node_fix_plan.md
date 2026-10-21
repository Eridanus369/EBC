# ESky 原生节点修复 + 天气算法改进 + 屏幕雨水 实施计划

## Context（背景）

旧版 `esky.glsl`（GLSL Function 节点，5 张外部纹理）正在被重构为 EEVEE 原生节点 `ShaderNodeESky`（SH_NODE_ESKY=920），云改为程序化 FBM。当前卡在：

1. **GPU 编译失败**：C++ 81 个输入 socket 与 BSL `node_esky` 签名从第 72 位起错位——"天气状态"是 C++ Float socket，BSL 却在参数末尾放 `int weather_state`；"视线步数/光线步数"C++ 是 `decl::Float`，BSL 声明为 `int`；函数体还残留 `int ray_steps = int(ray_steps_f)`（未定义标识符+重定义）。结果：`undeclared identifier` / `no matching overloaded function`，渲染纯黑。
2. **天气枚举未落地**：RNA `def_sh_esky` 已把 custom1 注册为枚举（晴/雨/雷暴/雪），但节点没有下拉菜单、没有 init、gpu_fn 也没传值。
3. **天气算法无效**：天气状态只叠加屏幕贴片，不影响天空与云；雨/雪用穹圭角坐标 UV（方位角回绕、与屏幕无关）；雨幕 hash 非网格化，基本看不见；程序化云还有致命 bug：`fc_cloud_fbm(..., int(ndetail+0.5))` 把"噪声缩放值 0.417"当八度音数 → 0 个八度 → 云基础形状恒为 0。
4. **新需求**：下雨时可选"雨水在屏幕上划过"的镜头雨效果。

已核实的事实：

- BSL `[[node]]` 支持：`static` 辅助函数（codegen 会剥离 static）、`out` 参数（生成 `_ref`）、struct 按值传参/返回（同文件 FcCloudAccum/FcInterval 先例）、三元、continue、比较实参、常量上界 for + break。
- 常量入参标准写法：`GPU_stack_link(..., GPU_constant(&float))`，变参常量**按顺序追加在全部 socket 输入之后、outs 之前**（[node_shader_tex_gradient.cc](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/nodes/shader/nodes/node_shader_tex_gradient.cc#L55-L67) 同款；消费逻辑见 [gpu_node_graph.cc:1089-1113](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/gpu/intern/gpu_node_graph.cc#L1089-L1113)）。
- 世界表面 pass = `MAT_PIPE_DEFERRED + MAT_GEOM_WORLD`（[eevee_world.cc:117](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/draw/engines/eevee/eevee_world.cc#L117)），即世界 shader 中 `MAT_DEFERRED` 有定义；`g_data.P = -视线外射方向`。
- 真屏幕 UV 有现成实现：[eevee_nodetree_lib.bsl.hh:1069-1099](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/draw/engines/eevee/shaders/eevee_nodetree_lib.bsl.hh#L1069-L1099) 的 `coordinate_screen(P)`，在 `MAT_GEOM_WORLD` 下用 `interp.P` + `point_view_to_screen` + camera uv_scale/bias（透视/正交均正确）；材质侧 include `gpu_shader_material_interface.bsl.hh` 即可（非 EEVEE 时桩返回 0）。
- 注册链路无需改动：gpu/CMakeLists.txt:759、nodes/CMakeLists.txt:163、register.cc:154、legacy_types:225、rna_nodetree.cc:6781/13939 均已存在。
- 约束：所有开关/整数一律走 Float socket（BSL 内 `>0.5f` / `int()`）；BSL 内 if 全部带花括号；不向函数实参直接传 bool 表达式（用显式三目 `(c)?1.0f:0.0f`）；不动 7 个 untracked `_lib.glsl` 与 `.bak`；不 commit。

## 修改文件（仅 2 个）

### 1. [node_shader_esky.cc](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/nodes/shader/nodes/node_shader_esky.cc)

- **天气面板**：删除 `weather.add_input<decl::Float>("天气状态")`；在面板最前面新增 3 个 Float socket：
  - `雨水屏幕`（default 0，开关）、`屏幕雨强度`（default 0.6，0..1）、`雨痕长度`（default 0.5，0..1）；
  - 其后保持 降雨强度/降雪强度/闪电频率。最终输入 socket 总数 83。
- 新增 `node_shader_buts_esky(ui::Layout&, bContext*, PointerRNA*)`：`layout.prop(ptr, "weather_state", ui::ITEM_R_SPLIT_EMPTY_NAME, "天气状态", ICON_NONE);`（参考 [node_shader_clamp.cc](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/nodes/shader/nodes/node_shader_clamp.cc)；下拉出现在节点顶部、面板上方）。
- 新增 `node_shader_init_esky(bNodeTree*, bNode *node)`：`node->custom1 = 0;`，注册 `ntype.draw_buttons` / `ntype.initfunc`；**不**加 storage。
- gpu_fn：
  ```cpp
  float weather_state = float(node->custom1);
  GPU_material_flag_set(mat, GPU_MATFLAG_LIGHTPROBE_ACCESS);
  return GPU_stack_link(mat, node, "node_esky", in, out, GPU_constant(&weather_state));
  ```
- include 无需新增。

### 2. [gpu_shader_material_esky.bsl.hh](file:///home/eridanusepsilon/Code/EBC/EBC5.3/source/blender/gpu/shaders/material/gpu_shader_material_esky.bsl.hh)

**接口修复（必须先过编译）**

- 顶部新增 `#include "gpu_shader_material_interface.bsl.hh"`。
- `node_esky` 签名与 83 个 socket 严格一一对应（全 float/float3/float4）：
  - 天气段插入 `float rain_screen_on, float screen_rain_strength, float rain_streak_length, float rain_intensity, float snow_intensity, float lightning_freq`；
  - 模拟段用 `float ray_steps_f, float light_step, float light_mult, float light_steps_f`，函数体首行 `int ray_steps = int(ray_steps_f); int light_steps = int(light_steps_f);`（删除现有重名两行）；
  -  outs 前最后一位 `float weather_state`（对应 GPU_constant）；outs 仍为 `out float4 color, out float out_cloud_density? ` 顺序：color/cloud_alpha/cloud_density/cloud_transmittance 不变。
- 所有 bool 形参（*_on）内部一律先比较后用；函数实参位置禁止出现裸比较表达式。

**程序化云修复（解决"看不见云"）**

- `fc_cloud_fbm` 调用不再把缩放当八度：基础形状固定 5 八度 `fc_cloud_fbm((rel+woff)*nscale, 5)`；细节噪声 2~3 八度并按 `detail_scale` 提频。
- 重标定 `fc_cloud_map`：天气覆盖率 FBM 用 `weather_scale*0.0025` 级别频率；基础云经 remap 展宽值域，coverage 直接抬/降阈值（默认 coverage 0.426 + 低云族参数必须能出云）；`rel` 的行星偏移改用传入的 `radius`（fc_cloud_map/fc_light_trans 增加 float radius 形参，调用点同步）。
- 保持高度梯度、detail 侵蚀、powder 等既有光照结构。

**天气算法改进（新增 struct FcFx，按值传入 fc_trace_cloud）**

```glsl
struct FcFx { float storm; float flash; float trace_start; };
```

- 枚举权重（三目显式 0/1）：`w_rain / w_thunder / w_snow`，`precip = rain+thunder`，`overcast = rain*0.85 + thunder*1.0 + snow*0.8`。
- **天空**：overcast 压低天空亮度（雷暴约 -75%）、向灰蓝去色、加强地平线霾；太阳强度与日轮按 `1-overcast*0.9` 衰减（云底光照用同一衰减后的太阳强度）。
- **云层**：在 5 个调用点按族乘覆盖率/密度系数——雨/雪增厚中低云（雨层云/层云感），雷暴显著加密+压暗；`fx.storm` 在 fc_trace_cloud 内按 `(1-h01)` 梯度把云底向蓝灰压暗；`fx.flash` 闪电时给云底 l0/l1 叠青白闪光；补上 `st > fx.trace_start` 跳过。
- **闪电重写** `fc_lightning_flash`：3~9 秒随机间隔的主闪+0.05~0.15s 二次回击，包络 exp 衰减、亮度随机；最终颜色按闪光叠加（雷暴云底强闪），替换现在恒定 sin 贴片。

**屏幕雨/雪（真屏幕 UV）**

- 在既有 `GPU_FRAGMENT_SHADER && (MAT_DEFERRED||MAT_FORWARD||NPR_SHADER)` 守卫内：`float2 screen_uv = coordinate_screen(g_data.P).xy;`；守卫外用穹圭角 UV 兜底（仅用于非片元编译）。
- 替换 `fc_rain_drop/fc_snow_flake`：
  - `fc_rain_streaks(uv, t, intensity, skew)`：2~3 层网格哈希雨丝，竖直下落+风向微斜、运动模糊拖尾、亮度/速度随机；按 `smoothstep(rd.z>-0.02)` 只在天空区叠加（这是"场景中的雨"）。
  - `fc_snow_flakes`：2 层网格软斑，慢落+横摆，仅天空区。
- **新增镜头雨 `fc_lens_rain(uv, t, strength, length)`**（受 `rain_screen_on>0.5` 且下雨时启用，全屏幕包括地面）：
  - 网格水珠层：低频哈希圆形落点、轻微粘滞漂移；
  - 流动雨痕层：拉长水珠沿屏幕下滑（速度/宽度随机、横向微摆），尾部渐细留湿痕；
  - 合成：湿润区域轻微压暗 + 沿痕方向的青白高光边；strength 控密度/不透明度，length 控拖尾长度。
- 天气非雨/雷暴时镜头雨自动为 0。

**收尾**：`曝光` socket 生效（HSV 调色前 `result *= exposure`）；保持最终 HSV Hue/Saturation/Brightness 与非负截断。

## 验证（全部前台运行）

1. 编译：`ninja -j6`（cwd=build_linux），日志落盘 `/home/eridanusepsilon/Code/ebc_build_esky.log`，失败即据日志修到 EXIT=0。
2. 新建 `/tmp/esky_test.py`：程序化创建世界节点树（ShaderNodeESky→Background→World Output），EEVEE 渲染 960×540：
   - 帧 1 晴（默认）：确认非纯黑、蓝天+低云可见、太阳可见；
   - 帧 2 雨 / 帧 3 雷暴（含闪电时刻多个 time 采样）/ 帧 4 雪：天空压暗、中低云增厚、雷暴云底发暗，四种天气 PNG 均值/直方图有可见差异；
   - 帧 5 雨+`雨水屏幕=1`：屏幕上有下滑雨痕（对比开关帧像素差异，确认差异随时间变化）。
   - 运行环境照 memory：仓根 cwd + `LD_LIBRARY_PATH="$(find "$PWD/lib" -maxdepth 2 -type d -name lib | tr '\n' ':')$PWD/build_linux/lib"`，`./build_linux/bin/blender -b --log-level 3 --log eevee ...`，前台。
3. 用 `--debug-gpu-shader-source "*"` 仅在仍有 shader 编译错误时导出 Shaders/*.glsl 定位。

## 已知取舍

- 删除/平移天气区 socket 会使旧存档该节点 72 号以后连线错位；该节点为未发布的自定义新节点，不做版本迁移。
- 镜头雨为全屏后期式贴片（不做背景重采样折射），高光+压暗模拟玻璃水痕，性能开销为 2 层网格循环。
