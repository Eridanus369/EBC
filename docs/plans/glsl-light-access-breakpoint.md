# GLSL Light Access — 状态 (2026-10-01 收盘)

## 结论: 停用
触发路径已从 node_shader_glsl_function.cc 移除。infra 全部保留（休眠状态）。

## 原因
EBC 的 `LightData` 是旧布局：
  float4 power_factor; float shape_power; float point_power;
NPR helper 期望：
  float4 power;

差异导致 `glsl_light_get()` 返回值不可靠（count 能读，光参数算不出）。
强行适配 shader 只在部分场景工作（t_light4 count 非零，t_light5 count 归零，
两个程序行为矛盾）。

## 保留的 infra（休眠，勿删）
- `source/blender/gpu/GPU_material.hh` / `.cc`:
  GPU_material_referenced_object_ensure / GPU_material_light_shader_parameter_ensure
  GPULightShaderParameterRequest / GPUReferencedObject
  GPU_material_dependency_source_get
- `source/blender/gpu/shaders/material/gpu_shader_material_glsl_light_access.glsl`
  （600 行，已注册 CMakeLists，含 EBC 布局适配的 glsl_light_power_get）
- `source/blender/draw/engines/eevee/eevee_defines.hh`:
  LIGHT_SHADER_*_SLOT / WORLD_SUNLIGHT_BUF_SLOT
- `source/blender/draw/engines/eevee/eevee_light_shared.hh`:
  LightData 扩展字段 (lightgroup_id / visible_camera / shader_parameter_uid /
  shadow_set_membership / shadow_map_scale + pad)
- `source/blender/draw/engines/eevee/shaders/eevee_light_data.bsl.hh`:
  LightShaderEvalData / LightShaderSurfelEvalData / sunlight_buf
- `source/blender/draw/engines/eevee/shaders/eevee_light_lib.bsl.hh`:
  light_influence_cutoff
- `source/blender/draw/engines/eevee/eevee_shader.cc`:
  generated source 依赖内联 + MAT_GLSL_LIGHT_ACCESS define 前置

## 未来重做（要跑通 light access）
**方案 B（推荐）**：统一 LightData 布局为 NPR 版
1. 改 `eevee_light_shared.hh` LightData 为 NPR 布局（power[4]，去掉
   power_factor / shape_power / point_power）
2. 改 host 端填 light_buf（`eevee_light.cc`）用 power[4]
3. 改现有 4 个用旧字段的 shader：
   - `eevee_light_lib.bsl.hh` L281-282
   - `eevee_nodetree_lib.bsl.hh` L917 / 972 / 975 / 1124-1127
   - `eevee_light_iter.bsl.hh` L198
   - `eevee_lightprobe_sphere_bake.bsl.hh` L530-536
4. 重新启用 `node_shader_glsl_function.cc` 的触发
5. 重新启用 `eevee_shader.cc` 的 GLSL_LIGHT_ACCESS → use_lighting_nodes

工作量: ~500 行 diff, 10+ 文件, 2~3 天

**方案 A（不推荐）**：继续适配 EBC 布局
在 shader 里维护两套 power 语义的映射表。已在 `glsl_light_power_get`
尝试过，无法覆盖所有场合。

## 复现命令（当前停用状态）
无（停用后普通 GLSL Function 不受影响）

## 回归基线
- sampler2D: `/tmp/t_samp.py` → rgb=(1.000,0.000,0.000)
- sampler3D: `/tmp/t_samp3d_val.py` → center (255,0,0)
- @glsl_meta: `/tmp/t_meta.py`
