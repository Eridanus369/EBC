# GLSL Function 移植 — 2026-10-01 会话交接

## 一句话现状
GLSL Function 完成 ~80%。可用：动态 socket / @glsl_meta / @glsl_defines /
多函数选择 / sampler2D 端到端。未完成：sampler3D（Vulkan 崩）、@glsl_closure、
Light Access、Script Expression。

## 分支与提交
分支 npr-merge-phase1，最新 b54fe7f。

今日提交（新→旧）：
- b54fe7f sampler3D: 保留 infra，禁用绑定（cleanup 崩）
- b367a7a sampler3D infra（Vulkan 崩，禁用）
- 54f4f89 GPU/BKE: 3D LUT strip 基础设施
- cd1e5cb GLSLFunction Stage C: sampler2D via ImageToClosure
- a7c037a RNA: ShaderNodeImageToClosure
- cf73896 Stage C prep: ImageToClosure 节点 + enums + DNA
- 43565c7 GLSLFunction Stage 4a: @glsl_defines
- 5f80e1b DNA: NodeShaderGLSLFunction.define_values
- 014db4a GLSLFunction T4: int items / show_label / panel_name
- 4eb5601 GLSLFunction T4-lite: @glsl_meta + function_name

## 已验证可用（Python + EEVEE 渲染）
1. 动态 socket：float/int/bool/vec2/vec3/vec4 输入输出，按源码函数签名生成。
2. @glsl_meta（块注释）：default / min / max / hide_value / subtype /
   description / label / items（int 枚举）/ panel_name。见 /tmp/t_meta.py /tmp/t_items.py。
3. 多顶层函数：g.function_name = "name" 切换。见 /tmp/t_multi.py。
4. @glsl_defines：bool/int，生成 #define，支持 #ifdef 条件编译。见 /tmp/t_define.py。
5. sampler2D：ImageToClosure 节点 -> GPU_image -> 渲染验证。
   2x2 图像红/绿/蓝/白，sample_tex(sampler2D, vec2) 返回首像素红。见 /tmp/t_samp.py。

## 关键坑（按严重程度）

### 1. decl::Closure 输入 socket 不占 GPU slot
node_declare 里 b.add_input<decl::Closure>() 的 socket，在 gpu_fn 的
GPUNodeStack *in 里对应项 type == GPU_NONE（无 slot）。绑定 sampler 时
不能用 in[i].type != GPU_NONE 做过滤，否则全被跳过。
正确写法见 node_shader_glsl_function.cc gpu_fn 里 sampler 绑定段。

### 2. 自定义节点的生成源必须内联，不能走 include
GPU_material_generated_source_add 注册的源，其 dependencies 只能指向
静态 shader 库（g_sources），指向另一个运行时生成源会在
gpu_shader_dependency.cc 查找失败。
解决：在 eevee_shader.cc 的 fragment shader 生成前，把生成源内容
直接拼进 frag_gen 顶部（已实现）。

### 3. node->dependency_name 是 char[128]
GPUNode.dependency_name 是定长数组，codegen 用
node->use_static_function 分支：
- true -> gpu_material_library_use_function(used_libraries, node->name)
- false -> 自定义节点，跳过 library 查找（生成源已内联）
见 gpu_codegen.cc node_serialize。

### 4. RNA 属性 pointer_sdna 必须放在 sdna_from(storage) 之前
NodeShaderGLSLFunction.id（Text 引用）要指向 bNode::id。
若放在 RNA_def_struct_sdna_from(srna, "NodeShaderGLSLFunction", "storage") 之后，
会去找 storage->id -> 编译报错 "identifier not found"。

### 5. cmake regen 覆盖 build.ninja
改任意 CMakeLists.txt（如加新节点文件）会触发 cmake regen，覆盖
build.ninja，丢失 X11/OIIO/OpenEXR/libtiff hack。
恢复：./build_files/npr-merge/apply-build-ninja-hacks.sh（幂等）。
本次新增 node_shader_image_to_closure.cc 已触发过一次。

### 6. shader hash 必须混入生成源内容
gpu_codegen.cc 里 hash_ 计算（BLI_hash_mm2a_end 前）要加生成源
filename + content。否则多材质共享 shader 变体会撞哈希（不同 GLSL 源码
渲染出同一结果）。已实现。

### 7. 3D LUT strip（sampler3D）Vulkan/GL 都崩
- 纹理创建成功（image_gpu_texture_3d_lut_strip_create 跑通）。
- 崩在 material cleanup：GPU_uniformbuf_free via RE_engine_free
  -> Depsgraph::clear_id_nodes。
- 已禁用绑定（draw_pass.hh + node_shader_glsl_function.cc），
  infra 全部保留。需排查 Vulkan image view 生命周期。

### 8. ImageToClosure 是 sampler 的桥接节点
sampler2D 输入必须从 ShaderNodeImageToClosure 节点链接。该节点：
- 无输入 socket，输出 decl::Closure。
- 内部 id 指向 Image（RNA "image" 属性）。
- storage 有 texture_type / texture_size_mode / interpolation / extension /
  texture_width/height/depth。

## 剩余工作精确入口

### A. 修 sampler3D（1~2 天）
入口：source/blender/gpu/vulkan/vk_texture.cc 的 VKTexture::image_view_get
（或 VKImageView 构造），看 3D texture view 创建是否漏了字段。
复现：/tmp/t_samp3d.py（需先在 draw_pass.hh 重新启用 3D 分支）。
修复后在 draw_pass.hh + node_shader_glsl_function.cc 里恢复 3D 绑定。

### B. @glsl_closure（需先建 Light Access 数据层，3~5 天）
NPR 参考函数（/tmp/npr_glslfn.cc）：
- parse_glsl_closure_block（~L2899）
- extract_glsl_closures（~L3008）
- build_glsl_closure_callbacks（~L?）
- GLSLRawClosureMeta struct（~L443）

EBC 需新建（全部缺失）：
- GPUMaterial 加 light_shader_parameters vector
- GPULightShaderParameterRequest struct
- GPU_light_shader_parameter_key / GPU_material_light_shader_parameter_ensure
- GPUMaterialClosureCallbackInput struct
- GPU_material_closure_callback_input_frame_push/pop/find/error_set/get
- EEVEE 侧参数上传 + shader 侧 GLSLLight / glsl_light_get()
参考 NPR 文件：source/blender/gpu/GPU_material.hh（~L83, L362, L602），
gpu_material.cc（~L671）。

### C. Stage 6 Script Expression（1~2 周）
Node-Code 双模式 UI + 会话状态（edit_source 等 DNA 字段已存在但未用）。
参考 NPR node_declare 的 code_mode 分支（~L9778）。

## 测试脚本（/tmp/）
- glslfn_value.py — 4 签名真值（c_red/c_green/c_half/c_gray）
- t_meta.py — @glsl_meta
- t_items.py — int items 枚举
- t_define.py — @glsl_defines
- t_multi.py — 多函数切换
- t_samp.py — sampler2D 端到端
- t_samp3d.py — sampler3D（当前崩）

## 编译 / 运行

编译：

    cd ~/Code/EBC/EBC5.3/build_linux && ninja -j6 2>&1 | tail -5

链接失败（X11 .a / libtiff）：

    cd ~/Code/EBC/EBC5.3 && ./build_files/npr-merge/apply-build-ninja-hacks.sh && cd build_linux && ninja -j6

运行 blender（每次 export）：

    export LD_LIBRARY_PATH=$HOME/Code/EBC/EBC5.3/build_linux/bin/lib:$HOME/Code/EBC/EBC5.3/lib/imath/lib:$HOME/Code/EBC/EBC5.3/lib/openexr/lib:$HOME/Code/EBC/EBC5.3/lib/openimageio/lib:$HOME/Code/EBC/EBC5.3/lib/openvdb/lib:$HOME/Code/EBC/EBC5.3/lib/tbb/lib:$HOME/Code/EBC/EBC5.3/lib/materialx/lib:$HOME/Code/EBC/EBC5.3/lib/usd/lib:$HOME/Code/EBC/EBC5.3/lib/opencolorio/lib:$HOME/Code/EBC/EBC5.3/lib/shaderc/lib:$HOME/Code/EBC/EBC5.3/lib/vulkan/lib:$HOME/Code/EBC/EBC5.3/lib/dpcpp/lib:$HOME/Code/EBC/EBC5.3/lib/openimagedenoise/lib:$HOME/Code/EBC/EBC5.3/lib/opensubdiv/lib:$HOME/Code/EBC/EBC5.3/lib/embree/lib:$HOME/Code/EBC/EBC5.3/lib/openpgl/lib:/usr/local/lib
    ./build_linux/bin/blender --background --factory-startup --python /tmp/t_samp.py 2>&1 | tail -20

## 给下一个 AI 的开场建议
1. 先跑 /tmp/t_samp.py 确认 sampler2D 仍工作（回归基线）。
2. 若做 sampler3D：从 vk_texture.cc 的 3D image view 入手，别再从 node 侧试。
3. 若做 closure/light：先建 GPUMaterial 数据层，别急着搬解析代码。
4. 别用 git grep（NPR 是 partial clone，会卡死）。用
   git --no-pager show HEAD:<path> > /tmp/xxx 一次 dump。
5. 别 ninja -j16（14GB RAM 会卡死）。用 -j6。
