# NPR 5.2 → EBC 5.3 移植指南

## 快速上手

每次 cmake regen（改了任何 CMakeLists.txt 后 ninja 自动触发）都会清掉 build.ninja 里的本地 hack，链接立即失败。恢复：

    ./build_files/npr-merge/apply-build-ninja-hacks.sh

首次跑二进制若崩在 OCIO，跑：

    ./build_files/npr-merge/patch-ocio-config.sh

## 加一个 shader node 的 8 处改动（以 Twirl 为例）

1. source/blender/blenkernel/BKE_node_legacy_types.hh — 加 SH_NODE_XXX 编号
2. source/blender/nodes/shader/node_shader_register.hh — 加声明
3. source/blender/nodes/shader/node_shader_register.cc — 加调用
4. source/blender/nodes/shader/nodes/node_shader_xxx.cc — 从 NPR 5.2 取
5. source/blender/nodes/shader/CMakeLists.txt — 加 nodes/node_shader_xxx.cc
6. source/blender/gpu/shaders/material/gpu_shader_material_xxx.glsl — 从 NPR 5.2 取
7. source/blender/gpu/CMakeLists.txt — 加 shaders/material/gpu_shader_material_xxx.glsl
8. source/blender/makesrna/intern/rna_nodetree.cc — 加 define("ShaderNode", "ShaderNodeXxx");

漏了第 8 步：编译过、链接过、启动 SIGSEGV，崩在 RNA_struct_blender_type_set。

## 节点编号

5.3 已用到 SH_NODE_LIGHT_ITER_INTERNAL_OUTPUT = 725，UPBGE 占用 800。NPR 新增从 726 起递增。

## 已知坑

1. cmake regen 清 hack。触发：改任意 CMakeLists.txt。表现：[0/1] Re-running CMake...。解法：apply-build-ninja-hacks.sh。
2. OCIO 2.4.1 vs config 2.5。EBC 的 lib/opencolorio 是 2.4.1，5.3 config 是 2.5，读不了会崩。临时解：patch-ocio-config.sh。根治：换 lib/opencolorio 到 2.5。
3. .glsl vs .bsl.hh。5.3 双轨兼容，NPR 的 .glsl 直接搬，不要改名。
4. shader node CMake 是手工列。漏加 = undefined reference。
5. ShaderNode RNA 是手工列。漏加 = 启动崩。
6. principled.glsl 不要直接搬。5.3 已 OpenPBR 化。NPR 自己的 Principled NPR 是独立节点，走独立 shader。
7. BLI_utildefines.h 改成 .hh。NPR 老代码要改 include 后缀。
8. scene_time_uniforms 签名变了。5.2 两参，NPR 三参，5.3 两参。

## 编译流程

    cd build_linux
    ninja -j6
    # 链接失败时：
    ../build_files/npr-merge/apply-build-ninja-hacks.sh
    ninja -j6

## 参考来源

- 上游 NPR：https://github.com/bb-yi/blender（main）
- 官方 5.2.2 基线：commit d13f752e3b9
- 本地 clone：~/Code/EBC/bb-yi-blender
- 完整 patch：/tmp/npr-full.patch（724 文件，+125682/-5995）
