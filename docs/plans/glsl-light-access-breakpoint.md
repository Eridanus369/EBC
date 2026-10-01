# GLSL Light Access 断点 (2026-10-01)

## 已完成
- 9f27bce Step 1: GPU 引用对象 + light shader 参数 API (GPU_material.hh/cc)
- df448e4 Step 2: shader helper (303行精简版) + glslfn 检测/flag + CMakeLists

## 工作区未提交
- gpu_shader_material_glsl_light_access.glsl 已换成 NPR 完整版(600行)
- eevee_defines.hh 加了 LIGHT_SHADER_TEX_SLOT=20 / INDEX_BUF=5 / UNIFORM_BUF=13
- eevee_light_shared.hh LightData 加了字段 -> **编译失败 Misaligned**

## 卡点: LightData 布局不兼容
EBC 旧布局: float4 power_factor; float shape_power; float point_power; uint resource_id; enum LightFlag flags; ...
NPR 新布局: float4 power; ...  bool32_t shadow_jitter; int lightgroup_id; bool32_t visible_camera; uint shader_parameter_uid; uint2 shadow_set_membership; float shadow_map_scale; ...

## 两条路
A(统一布局): 改 EBC LightData 为 NPR 布局 + 改 host + 改现有 4 个 shader 用旧字段. ~300-500 行, 10+ 文件, 风险中高.
B(适配 EBC): 保持 EBC 布局, 只加 lightgroup_id/shader_parameter_uid/shadow_set_membership/shadow_map_scale + 重写 600 行 shader 用 power_factor/shape_power/point_power. ~700 行, 风险低.
**建议 B**

## 剩余
- Step 3: EEVEE 绑定 light_buf/shadow + define MAT_GLSL_LIGHT_ACCESS
- Step 4: Light Shader 节点树子系统 (~2000 行, 完整版才需)
- Step 5: glslfn 消费 light_shader_parameter_*

## NPR 素材重新 dump (在 ~/Code/EBC/bb-yi-blender)
for f in \
  source/blender/draw/engines/eevee/shaders/eevee_light_data.bsl.hh \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_common_lib.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_frag.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_front_frag.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_bake_frag.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_surfel_comp.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_uniform_comp.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_light_shader_volume_comp.glsl \
  source/blender/draw/engines/eevee/shaders/eevee_bake_light_shader_surface_frag.glsl \
  source/blender/gpu/shaders/material/gpu_shader_material_glsl_light_access.glsl \
  source/blender/gpu/shaders/material/gpu_shader_material_light_shader_info.glsl \
  source/blender/gpu/shaders/material/gpu_shader_material_light_shader_output.glsl \
  source/blender/nodes/shader/nodes/node_shader_eevee_light_shader_info.cc \
  source/blender/nodes/shader/nodes/node_shader_eevee_light_shader_output.cc \
  source/blender/draw/engines/eevee/eevee_light_shared.hh \
  source/blender/draw/engines/eevee/eevee_light.cc \
  source/blender/draw/engines/eevee/eevee_light.hh \
  source/blender/draw/engines/eevee/eevee_pipeline.cc \
  source/blender/draw/engines/eevee/eevee_material.cc \
  source/blender/draw/engines/eevee/eevee_shader.cc \
  ; do mkdir -p /tmp/npr_ls; out="/tmp/npr_ls/$(basename $f)"; git --no-pager show HEAD:$f > "$out" 2>/dev/null && echo "OK $(wc -l < $out) $out" || echo "FAIL $f"; done

## 环境
- 编译: cd ~/Code/EBC/EBC5.3/build_linux && ninja -j6
- CMakeLists 改动会覆盖 build.ninja: ./build_files/npr-merge/apply-build-ninja-hacks.sh
- 别 ninja -j16
