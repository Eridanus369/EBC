# sampler3D (3D LUT strip) 调试交接 — 给下一个 AI

## 目标
让 GLSL Function 节点的 sampler3D 参数能从 ImageToClosure 节点（texture_type =
TEXTURE_3D_LUT_STRIP）读取 3D 纹理并正确采样。

## 当前状态：**未完成**
- GL 后端：shader 链接成功，采样返回 0（黑），退出时 SIGSEGV。
- Vulkan 后端：`vkCreateImageView` 在 libnvidia-glcore 里崩（3D view 创建）。

## 现象
- 中心像素 (0,0,0)；UV 改成 (0.5,0.5,0.5) 仍黑（已排除边界问题）。
- 崩溃栈：`GPU_uniformbuf_free` ← `GPU_material_free_single` ← `RE_engine_free`
  （析构期的内存损坏，非渲染期）。

## 已逐项验证 **正确**（不要再查这些）
1. **数据**：`lut_buffer[0..7] = 1.0 0.0 0.0 1.0 ...`（红）。strip 重排逻辑正确
   （4x2 图 → 2x2x2 LUT）。
2. **shader 声明**：dump 出的 GLSL 是
   `layout(binding = 0) uniform sampler3D samp0;`
   调用点 `texture(samp0, uvw)` 存在。
3. **GL target 映射**：`gl_texture.hh:to_gl_target(GPU_TEXTURE_3D) → GL_TEXTURE_3D`
   正确。
4. **sampler 对象**：`gl_texture.cc:683-685` 已设 `WRAP_S / WRAP_T / WRAP_R`，
   3D 第三轴有设置。
5. **绑定调用发生**：debug 打印显示 `[BIND-TEX] name=samp0 binding=0 tex=0x...`
   至少出现一次。draw_pass 走的是 3D 分支，tex 非空。
6. **UBO slot**：`GPU_NODE_TREE_UBO_SLOT = 0`，与 sampler `binding=0` 撞号，
   但 sampler2D 同样撞号且工作，故**不是根因**。
7. **UV 边界**：0.5 仍黑，排除。
8. **代码对比**：我们的
   - `image_gpu_texture_3d_lut_strip_create`
   - `image_gpu_3d_lut_strip_texture_ptr`
   - `GPU_image_3d_lut_strip`
   - `BKE_image_get_gpu_material_3d_lut_texture`
   与 NPR (bb-yi-blender) **逐行一致**。

## 已排除的怀疑
- 不是数据错
- 不是 shader 类型错
- 不是 GL target 映射错
- 不是 sampler 对象参数错
- 不是绑定调用缺失
- 不是 UBO/sampler 撞 binding
- 不是 UV 边界
- 不是我们的 3D LUT 创建代码与 NPR 有差异

## 唯一剩下的差异（未验证）
**NPR 的 `ImageGPUTextures::texture` 是 `gpu::Texture **`（双指针，延迟绑定），
EBC 我们改成了 `gpu::Texture *`（单指针，立即绑定）。**

NPR 走 **deferred texture loading** 两阶段：
- material 编译期：注册 `gpu::Texture **` 占位（`bind_texture(name, gpu::Texture **)`）
- draw 期：`Manager::load_deferred_textures` 在 GPU 上下文里上传并回填

EBC 我们的实现（当前代码）是 material_set 期**同步创建+立即绑定**
（`bind_texture(name, gpu::Texture *)`）。这可能是时序问题——CPU 端纹理对象有效，
但 GL 上下文/上传时机不对，导致采样 0 + 析构崩。

## 下一步（按优先级）
1. **确认 `bind_texture(name, gpu::Texture *)` 立即路径对 3D 是否有缺陷**
   看 `draw_pass.hh:1294` 附近，以及 `GLStateManager::texture_bind(unit)`。
   对比它和 2D 绑定的实际差异。
2. **移植 NPR 的 deferred 机制**（如果 1 确认问题）
   涉及文件：
   - `source/blender/blenkernel/BKE_image_gpu.hh`：`ImageGPUTextures::texture`
     改回 `gpu::Texture **`
   - `source/blender/blenkernel/intern/image_gpu.cc`：3D LUT 的
     `image_get_gpu_material_3d_lut_texture` 改为写 `**slot`（我当前版已近），
     但 `BKE_image_acquire_gpu_material_3d_lut_texture` 返回的 struct 要双指针
   - `source/blender/draw/intern/draw_pass.hh`：2D/3D 绑定分支改为
     NPR 的 `*gputex.texture == nullptr` 两阶段写法
   - `source/blender/draw/intern/draw_manager.cc`：加
     `add_texture_deferred` / `load_deferred_textures`
   - **警告**：改 `ImageGPUTextures` 会打破所有现有调用点（~10 处，含已工作的
     sampler2D、EEVEE、workbench）。sampler2D 当前能工作，改完可能一起崩。
3. **替代方案**：抓帧用 RenderDoc 看 draw call 时 GL 状态（是否 sampler3D 的
   uniform 真指向 unit 0，纹理是否真在 GL_TEXTURE_3D target 上）。

## 相关文件 / 关键位置
- `source/blender/nodes/shader/nodes/node_shader_glsl_function.cc`
  - `node_shader_gpu_glsl_function` 里 sampler 绑定段（当前 3D 分支被注释禁用）
- `source/blender/gpu/intern/gpu_node_graph.cc`
  - `GPU_image_3d_lut_strip` (~L901)
  - `gpu_node_graph_add_texture`（多 4 参数：use_3d_lut_strip + w/h/d）
  - `case GPU_NODE_LINK_IMAGE`（3D 也走这个分支，与 2D 共用）
- `source/blender/gpu/intern/gpu_codegen.cc`
  - L274：`info.sampler(slot++, ImageType::Float3D, ...)`（3D LUT 采样器声明）
- `source/blender/gpu/intern/gpu_material.cc`
  - `GPU_material_generated_source_add`
- `source/blender/blenkernel/BKE_image_gpu.hh`
  - `ImageGPUTextures` struct（当前是单指针，NPR 是双指针）
  - `BKE_image_acquire_gpu_material_3d_lut_texture{,_try}`
  - `ImageRuntimeGPUTexture3DLutStrip`
- `source/blender/blenkernel/intern/image_gpu.cc`
  - `image_gpu_3d_lut_strip_dimensions_valid`
  - `image_gpu_3d_lut_strip_texture_ptr`
  - `image_gpu_texture_3d_lut_strip_create`
  - `image_get_gpu_material_3d_lut_texture`
  - `BKE_image_free_gpu_3d_lut_textures`
- `source/blender/draw/intern/draw_pass.hh`
  - `PassBase::material_set`（3D 分支当前被禁用，注释里有）
  - `PassBase::bind_texture(name, gpu::Texture*, ...)`（加过 `binding==-1` 保护，
    这是确凿修复，别删）

## 复现命令

```bash
# 3D 测试脚本（已存在）
cat /tmp/t_samp3d.py 2>/dev/null || echo "TMP 被清，需重建"

# 开启 3D 绑定需要改两处：
#   1. draw_pass.hh 里 material_set 的 use_3d_lut_strip 分支
#   2. node_shader_glsl_function.cc 里 sampler 绑定段的 Sample3D 分支
# 然后编译 + 运行：
cd ~/Code/EBC/EBC5.3/build_linux && ninja -j6
cd ~/Code/EBC/EBC5.3 && export LD_LIBRARY_PATH=...
./build_linux/bin/blender --background --factory-startup --gpu-backend opengl \
  --python /tmp/t_samp3d.py
```

## 环境提示
- cmake regen 会覆盖 build.ninja，丢失 X11/OIIO/OpenEXR/libtiff hack，
  改完 CMakeLists 后跑 `./build_files/npr-merge/apply-build-ninja-hacks.sh`。
- NPR 是 partial clone，用 `git --no-pager show HEAD:<path> > /tmp/xxx` 一次 dump，
  别 `git grep`。
- 编译用 `ninja -j6`（14GB RAM，别用 -j16）。
- GL debug 后端：`--debug-gpu-shader-source-name='*'` 不生效，
  想 dump shader 在 `source/blender/gpu/opengl/gl_shader.cc:1275` 附近打印
  `concat_source`。

## 已知 NPR 侧参考（/tmp/npr_*，若被清需重新 dump）
- `/tmp/npr_glslfn.cc`：node_shader_glsl_function.cc (10992 行)
- `/tmp/npr_image_gpu.cc`：image_gpu.cc（3D LUT 实现 L324-460）
- `/tmp/npr_gng.cc`：gpu_node_graph.cc
- `/tmp/npr_draw_pass.hh`：draw_pass.hh（3D 绑定 L1193-1232）
- `/tmp/npr_bke_image.hh`：BKE_image.hh（ImageGPUTextures L701-704）
