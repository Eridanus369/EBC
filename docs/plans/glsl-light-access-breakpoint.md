# GLSL Light Access 断点 (2026-10-01 收盘 v2)

## 一句话状态
Light Access **触发已启用**，shader 编译链接通过，但 `glsl_light_count()` 
返回 0（t_light6 结果 = 红色）。SSBO 绑定通路正常（早期 t_light4 能读到
`light_cull_buf.items_count` 非零）。

## 最新提交
分支 npr-merge-phase1, 最近: 41e6d89 + 未提交改动

## 未提交改动
- source/blender/nodes/shader/nodes/node_shader_glsl_function.cc
  → light access 触发**已启用**（deps.append("gpu_shader_material_glsl_light_access.glsl")）
- source/blender/gpu/shaders/material/gpu_shader_material_glsl_light_access.glsl
  → glsl_light_friendly_power 改成直接返回 glsl_light_power_get
  → glsl_light_power_get 适配 EBC 布局 (power_factor * shape_power/point_power)
- source/blender/draw/engines/eevee/eevee_shader.cc
  → use_lighting_nodes 也由 GPU_MATFLAG_GLSL_LIGHT_ACCESS 触发
  → inlined 依赖源 + MAT_GLSL_LIGHT_ACCESS define 前置

## 当前卡点
t_light6 测试 (`/tmp/t_light6.py`) 返回红 = `glsl_light_count() == 0`。

`glsl_light_count` 内部循环检查（`glsl_light_loop_accept`）：
1. `light_index < light_cull_buf.visible_count`（本地光）
2. `light_index < light_cull_buf.items_count`（全局光）
每个光的 filter：
- `!glsl_light_index_matches_locality(light_index, is_local)` → 跳过
- `glsl_light_is_zero(light.color)` → 跳过
- `light_linking_affects_receiver(light_set_membership, receiver_light_set)` → 跳过
- `max(power[DIFFUSE], power[SPECULAR]) >= LIGHT_ATTENUATION_THRESHOLD`

## 最后一步测试（未跑完）
`/tmp/t_l7.py` — 逐条件诊断：
- 红  → visible_count == 0
- 绿  → local_lights_len == 0
- 蓝  → light_buf[0].color == 0
- 黄  → power < LIGHT_ATTENUATION_THRESHOLD
- 白  → 全部通过（那 count 不该是 0，bug 在别处）

**开机后第一步**: 跑 t_l7，看颜色

## 关键差异 (EBC vs NPR LightData)
EBC (旧):
  float4 power_factor;   // diff/spec/transmission/volume 权重
  float shape_power;     // base_power * shape_radiance_get()
  float point_power;     // base_power * point_radiance_get()
  uint resource_id;
  enum LightFlag flags;

NPR (新):
  float4 power;          // 全部预乘: diff_fac * shape_power * vis
  ... (无 resource_id, flags)
  bool32_t shadow_jitter;
  int lightgroup_id;
  bool32_t visible_camera;
  uint shader_parameter_uid;
  uint2 shadow_set_membership;

数学等价: EBC `color * power_factor[t] * shape_power` == NPR `(base_power*color) * power[t]`
EBC 把 base_power 放在 shape_power/point_power 里，color 是纯光照颜色。
NPR 把 base_power 放进 color。

## 从 NPR dump 素材
mkdir -p /tmp/npr_ls && cd ~/Code/EBC/bb-yi-blender &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/eevee_light_shared.hh > /tmp/npr_ls/eevee_light_shared.hh &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/eevee_light.cc > /tmp/npr_ls/eevee_light.cc &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/eevee_light.hh > /tmp/npr_ls/eevee_light.hh &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/shaders/eevee_light_lib.bsl.hh > /tmp/npr_ls/eevee_light_lib.bsl.hh &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/shaders/eevee_light_iter.bsl.hh > /tmp/npr_ls/eevee_light_iter.bsl.hh &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/shaders/eevee_nodetree_lib.bsl.hh > /tmp/npr_ls/eevee_nodetree_lib.bsl.hh &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/shaders/eevee_lightprobe_sphere_bake.bsl.hh > /tmp/npr_ls/eevee_lightprobe_sphere_bake.bsl.hh &&
git --no-pager show HEAD:source/blender/draw/engines/eevee/shaders/eevee_light_data.bsl.hh > /tmp/npr_ls/eevee_light_data.bsl.hh &&
git --no-pager show HEAD:source/blender/gpu/shaders/material/gpu_shader_material_glsl_light_access.glsl > /tmp/npr_ls/glsl_light_access.glsl

## 剩余工作
- **Light Access 完整版**:
  - 定位 count=0 的真实原因（t_l7 诊断）
  - 可能路径：修 `light_linking_affects_receiver` 语义 / 调 `light_set_membership`
  - Light Shader 参数子系统 (~2000 行，可选，NPR 特有)
- **@glsl_closure**: ~1400 行，依赖 Light Access
- **Script Expression**: ~300 行，独立

## 当前进度
- GLSL Function 核心: ✅ 100% (socket/meta/define/sampler2D/sampler3D)
- Light Access: ~70% (通路通，count=0 未定位)
- 综合: ~80%

## 环境提醒
- 编译: cd ~/Code/EBC/EBC5.3/build_linux && ninja -j6
- 若 cmake regen 覆盖 build.ninja: ./build_files/npr-merge/apply-build-ninja-hacks.sh
- 别 ninja -j16 (OOM)
- LD_LIBRARY_PATH 见 docs/plans/glsl-function-session-2026-10-01.md

## 测试脚本
- /tmp/t_light6.py: 红=count 0, 绿=invalid, 蓝=color 0
- /tmp/t_l7.py: 五色诊断（下一步跑）
- /tmp/t_samp.py: sampler2D 基线 (rgb=1,0,0)
