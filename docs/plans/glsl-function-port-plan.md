# GLSL Function 移植计划（NPR → EBC 5.3）

> 状态：**规划阶段**，非代码实施。
> 目标读者：下一个接手 NPR 移植的 AI / 工程师 / 未来的自己。
> 源文件：`~/Code/EBC/bb-yi-blender/source/blender/nodes/shader/nodes/node_shader_glsl_function.cc`（10992 行）

---

## 0. 结论摘要

**GLSL Function 不是"节点"，是"运行时 GLSL 编译器 + 节点图生成器"**。

- 源码规模：10992 行（单文件），外加 600 行 `glsl_light_access.glsl`、726 行 `script_expression.cc`
- 开发历史：NPR 团队跨越 5 个月（2026-04 → 2026-09），十几个 commit 迭代
- 依赖深度：**跨 GPU 编译系统、EEVEE material shader、节点 RNA、编辑器 UI**

**移植估时**：单人 2~4 周（无中间踩坑），且必须先完成 5.3 侧底层 API 补全（1 周）。**总 3~5 周**。

---

## 1. 源文件模块拆解

`node_shader_glsl_function.cc` 内部逻辑大致分 8 块。**建议阅读顺序 = 移植顺序**。

### 1.1 数据结构（估算 300~500 行）
- `NodeShaderGLSLFunction`（DNA struct，见 `DNA_node_types.h`）
  - `source_mode`（Internal / External）
  - `parse_status`（Dirty / Ready / Error）
  - `flags`（Code Mode / Edit Function）
  - source code 文本缓冲
  - define 值列表（`NodeShaderGLSLDefineValue`：bool / int）
  - 函数签名缓存（输入 / 输出 socket 描述）
- 相关 enum：`NodeGLSLFunctionSourceMode` / `ParseStatus` / `Flag` / `DefineType`

**移植动作**：
- 复制 `NodeShaderGLSLFunction` + `NodeShaderGLSLDefineValue` 到 `DNA_node_types.h`
- 复制 5 个 enum
- **注意**：`DNA_DEFINE_CXX_METHODS` 需要保证 struct 无虚函数

### 1.2 GLSL 源码解析器（估算 2000~3500 行，最大单块）
**这是整个系统的心脏。** 功能：
- 词法分析 GLSL：识别 `type name(args)` 函数定义
- 类型系统：`float` / `vec3` / `sampler2D` / `mat4` 等
- 参数解析：in / out / inout 关键字，默认值
- `@glsl_meta v1` 注释块解析（声明 UI 元数据：label、range、default、hide_value、int_choice...）
- 函数签名 → 节点输入/输出 socket 描述

**依赖 5.3**：
- `BLI_string_ref` / `BLI_string_utils.hh`（都在）
- `NOD_socket_declarations.hh`（socket 声明系统，需检查 5.3 是否兼容）

**风险点**：
- NPR 解析器是否用了 5.3 不存在的数据结构？
- `@glsl_meta` 是 NPR 发明的语法，纯字符串解析，**无外部依赖**

**建议**：**这块独立搬运**，先写独立单测（脱离 Blender 跑）验证。

### 1.3 GLSL → GPU 节点图生成（估算 3000~4000 行，第二大块）
功能：
- 把解析出的 GLSL 函数体转成 `GPUNodeLink` 图（Blender 的 shader 节点系统）
- 生成对应的 GPU shader 源码（`gpu_shader_material_glsl_function.glsl` 里的模板）
- 处理 sub-function 拆分（`GPU_material_split_sub_function`）
- 处理闭包回调（Closure 系统）
- 处理 sampler / texture 参数

**依赖 5.3**：
- `GPU_material_split_sub_function` — **签名不同**（NPR 版本多一个 deps 参数），需要适配或改 5.3
- `GPU_stack_link_custom` — 需确认 5.3 有
- `GPU_material_generated_source_add` — **NPR 新增**，5.3 无 → **必须移植**
- `GPU_material_closure_uv_source_push/pop` — **NPR 新增**（Closure 系统）→ 若不做 Closure 可跳过

**这是最深的部分**，与 5.3 的 GPU 编译管线正面冲突。

### 1.4 GPU 函数注册（估算 200~400 行）
- `node_shader_gpu_glsl_function()` 入口
- 处理 `in[]` / `out[]` GPU stack 的映射
- 编译失败时回退

### 1.5 RNA 层（估算 500~800 行）
在 `rna_nodetree.cc` 里：
- `def_sh_glsl_function()` — RNA 属性定义（source_code、display_mode、define 列表、...）
- `rna_ShaderNodeGLSLFunction_source_code_get/set/update`
- `rna_ShaderNodeGLSLFunction_display_mode_get/set`
- `rna_ShaderNodeGLSLFunction_tag_dirty`
- `rna_ShaderNodeGLSLFunction_script_set`（关联外部文本）
- `rna_ShaderNodeGLSLFunction_clear_legacy_edit_state`
- 辅助函数：`rna_ShaderNodeGLSLDefineValue_choice_items()`

**移植动作**：
- 复制这些函数到 5.3 的 `rna_nodetree.cc`
- **注意**：5.3 RNA 生命周期 API 可能有差异（`RNA_def_property_*` 系列）

### 1.6 Python 端集成（估算 300~600 行，在 `scripts/`）
- GLSL 函数列表下拉刷新
- 参数折叠面板
- 文件外部引用打包
- 定义面板（Define 编辑 UI）

**移植动作**：
- 检查 `scripts/nodes/` 或 `scripts/startup/bl_ui/` 里 NPR 是否加了 glsl_function 专属文件
- 通常是 `node_add_menu_shader.py` + `space_node.py` 的 diff

### 1.7 编辑器 UI（估算 500~1000 行）
- Node/Code 双模式切换
- 源码编辑器（在节点内嵌文本编辑）
- 草稿提交/丢弃逻辑
- 共享源码原子刷新

**依赖**：
- 5.3 的 `editors/space_node/` 与 5.2 差异可能很大
- 需要看 NPR 对 `space_node_draw.cc` 等文件的 diff

### 1.8 GLSL Light Access 子系统（`glsl_light_access.glsl` 600 行）
- `glsl_light_get()` helper（在 GLSL 源码里调用）
- 读取灯光 SSBO / 纹理
- 与 Light Shader Output 联动

**依赖 5.3**：
- `eevee::LightRenderData` resource（5.3 有，但 material shader 里没绑定）
- `light_shader_tx` / `light_shader_uniform_buf`（NPR 新增）

**可延后**：先搬"无灯光访问"的 GLSL Function 核心，Light Access 作为第二阶段。

---

## 2. 5.3 侧需要补的底层（前置条件）

### 2.1 GPU 材质编译 API 补全
| NPR API | 5.3 现状 | 补全方案 |
|---|---|---|
| `GPU_material_split_sub_function(mat, type, link, deps)` | 5.3 版本无 `deps` 参数 | 加默认参数 `deps = {}` 或新签名重载 |
| `GPU_material_generated_source_add(mat, filename, deps, source)` | **不存在** | 从 NPR 移植整个函数 + 其数据结构 |
| `GPU_material_split_sub_function` 中 deps 用于建立编译依赖 | — | 需理解 NPR 的 `generated_sources` 结构 |
| `GPU_stack_link_custom` | 5.3 有 | 检查参数兼容 |

**实现位置**：`source/blender/gpu/intern/gpu_material.cc` + `GPU_material.hh`

**难度**：中等（改 200~400 行，但需要理解 NPR 的编译依赖图机制）

### 2.2 EEVEE material shader 资源扩展
| NPR resource | 5.3 material shader | 说明 |
|---|---|---|
| `draw::View` | ✅ 已有 | — |
| `eevee::HiZ` | ✅ 已有 | — |
| `eevee::Sampling` | ✅ 已有 | — |
| `eevee::Uniform` | ✅ 已有 | — |
| `eevee::LightRenderData` | ❌ | GLSL Light Access 需要 |
| `eevee::LightprobeRenderData` | ❌ | World Environment 需要 |
| `FilterMaterial*` | ❌ | Filter Graph 需要 |

**补全方案**：在 `eevee_nodetree_infos.hh` + `eevee_nodetree_lib.bsl.hh` 里追加 resource_table 声明 + 在 `eevee_shader.cc` 的 material shader info 里 `additional_info(...)`。

**难度**：中等（每加一个 resource 需 30~60 分钟）

### 2.3 材质 flag 补全
在 `GPU_material.hh` 的 `eGPUMaterialFlag` 里加：
- `GPU_MATFLAG_GLSL_LIGHT_ACCESS`
- `GPU_MATFLAG_NPR`（若需要）
- 已有的 `GPU_MATFLAG_RAYCAST` 可用

**难度**：低（改 10 行）

### 2.4 Poll 函数补全
NPR 有 `object_or_npr_eevee_shader_nodes_poll()`。5.3 只有 `object_eevee_shader_nodes_poll()`。

**当前策略**：直接用 5.3 的 `object_eevee_shader_nodes_poll()`（我们前 14 个节点都这么做）。
**若 GLSL Function 需要 NPR shader tree 类型**，需要先移植 NPR Tree 系统（超出本计划）。

---

## 3. 分阶段移植路线（推荐）

### 阶段 1：数据层（2~3 天）
- [ ] `DNA_node_types.h` 加 `NodeShaderGLSLFunction` + `NodeShaderGLSLDefineValue` struct
- [ ] 加 5 个 enum
- [ ] 加 `SH_NODE_GLSL_FUNCTION = 738`（继 OKLab 737 后）
- [ ] 实现 `node_shader_glsl_function.cc` 的 **declare / init / register 骨架**（不带实际 GPU 逻辑）
- [ ] RNA def 骨架（source_code / display_mode / define 属性）
- [ ] **验证**：GUI 里能创建节点、切模式、编辑源码字符串

### 阶段 2：GLSL 解析器（3~5 天）
- [ ] 从 NPR 抽出解析器逻辑，**独立成模块**（不依赖 Blender 的临时 Python 单测）
- [ ] 单元测试：给定 GLSL 源码 → 输出函数签名 + 元数据
- [ ] 集成到 node 的 `updatefunc`（修改源码时重新解析）
- [ ] **验证**：改源码后节点 socket 列表自动更新

### 阶段 3：GPU 节点图生成（5~7 天，最难）
- [ ] 补全 `GPU_material_generated_source_add` 等底层 API
- [ ] 移植 `node_shader_gpu_glsl_function()`
- [ ] 实现简单的 GLSL 表达式 → GPU 节点图（先只支持 `float func(float x) { return x*2; }`）
- [ ] **验证**：一个最简 GLSL 函数能渲染出正确结果

### 阶段 4：完整功能（5~10 天）
- [ ] 支持 vector / color / sampler 参数
- [ ] 支持 `@glsl_meta v1` 全部元数据
- [ ] 支持 define 编译期开关
- [ ] 支持 Node/Code 双模式 + 源码编辑器
- [ ] 支持外部文件引用 + 打包
- [ ] **验证**：NPR 官方测试用例（`tests/python/npr/test_glsl_function_*.py`）全部通过

### 阶段 5：GLSL Light Access（3~5 天）
- [ ] 补 EEVEE material shader 的 LightRenderData resource
- [ ] 移植 `glsl_light_access.glsl`
- [ ] 实现 `glsl_light_get()` helper
- [ ] **验证**：GLSL 里能读取灯光信息

### 阶段 6（可选）：Script Expression（1~2 天）
- 依赖阶段 1~4 的成果，726 行独立文件，稍作适配

---

## 4. 关键风险点

### 4.1 `GPU_material_generated_source_add` 是整个 GLSL Function 的瓶颈
**这是 NPR 的核心 patch**。若 5.3 的 GPU 编译管线不支持"运行时注入 shader 源码 + 声明依赖"，GLSL Function 无从谈起。

**缓解方案**：先做 spike——写一个最小例（一个"写死"的 GLSL 函数字符串 → 注入 → 编译 → 渲染），验证底层通路可行后再做全功能。

### 4.2 5.3 的 GPU 编译管线与 5.2 差异
`eevee_shader.cc` 在 5.3 里经过 BSL 迁移，很多 NPR 的 hook 点可能位置变了。

**缓解方案**：先做 diff `NPR eevee_shader.cc` vs `5.3 eevee_shader.cc`，看关键函数（`material_create_info` 等）差异。

### 4.3 NPR Tree / Filter Graph 依赖
GLSL Function 在 NPR 里**支持 NPR shader tree 和 Filter Materials 里使用**。这两块 5.3 完全没有。

**缓解方案**：**先只支持标准材质节点树**，NPR Tree / Filter 场景留到这两个系统移植后再补。

### 4.4 编辑器 UI（Node/Code 双模式、源码内嵌编辑）
NPR 对 `space_node` 的改动可能很大。

**缓解方案**：**先不做 Node/Code 双模式**，只做"源码字符串"输入（用普通 text 字段）。UI 打磨留到功能可用后。

---

## 5. 最小可用里程碑（建议先做的 spike）

**目标**：3 天内做出"一个能渲染的 GLSL 函数节点"。

**范围**：
- 只支持 `float func(float x) { return ...; }` 形式
- 无 `@glsl_meta`
- 无 define
- 无 sampler
- 源码通过简单字符串输入
- 无 Node/Code 双模式

**步骤**：
1. 复制 `node_shader_glsl_function.cc` 到 5.3，删掉所有功能，只留 register 骨架
2. 硬编码一个 GLSL 源码字符串
3. 实现 `GPU_material_generated_source_add`（或找到 5.3 替代）
4. 硬编码 socket（1 in float + 1 out float）
5. 在 `eevee_nodetree_frag_lib.glsl` 里调用注入的 sub-function
6. 渲染验证

**如果这一步不通，整个计划要重新评估。**

---

## 6. 参考资源

### 6.1 源文件位置
- **主文件**：`~/Code/EBC/bb-yi-blender/source/blender/nodes/shader/nodes/node_shader_glsl_function.cc`
  - 读取方式：`cd ~/Code/EBC/bb-yi-blender && git --no-pager show HEAD:source/blender/nodes/shader/nodes/node_shader_glsl_function.cc > /tmp/npr_glslfn.cc`
- **RNA**：`/tmp/npr_rna.cc` 里 grep `GLSLFunction`
- **DNA**：`/tmp/npr_dna.h` 里 grep `NodeShaderGLSL`
- **Shader**：`git show HEAD:source/blender/gpu/shaders/material/gpu_shader_material_glsl_light_access.glsl`

### 6.2 NPR 文档
- `~/Code/EBC/bb-yi-blender/docs/glsl-function-node-conversion-guide.md`（84533 字节！**详细程度极高，必读**）
- `~/Code/EBC/bb-yi-blender/docs/plans/2026-04-02-glsl-function-node.md`
- `~/Code/EBC/bb-yi-blender/docs/plans/2026-04-06-glsl-function-sample2d-and-image-to-closure.md`
- `~/Code/EBC/bb-yi-blender/docs/plans/2026-04-15-glsl-function-eevee-light-access.md`
- `~/Code/EBC/bb-yi-blender/docs/plans/2026-04-16-glsl-function-friendly-light-struct.md`

### 6.3 官方测试
- `tests/python/bl_node_glsl_function.py`
- `tests/python/npr/test_glsl_function_*.py`（5 个）
- `tests/python/npr/test_filter_glsl_function_render.py`

### 6.4 关键 commit
NPR 侧的 GLSL Function 开发历史可用 `git log --all --oneline -- source/blender/nodes/shader/nodes/node_shader_glsl_function.cc` 查看（在 `~/Code/EBC/bb-yi-blender`）。

---

## 7. 下一步建议

**如果决定推进**：
1. 先读 `docs/glsl-function-node-conversion-guide.md`（84KB，可能需要 1~2 小时）
2. 做第 5 节的 spike（3 天）
3. 若 spike 通，按第 3 节分阶段推进

**如果暂缓**：
- 本文件保留在此，供未来决策
- 先完成 `PORTING-GUIDE.md` 里列的工程收尾（OCIO 根治、`.bsl.hh` 迁移）

---

## 8. 附：估算时间线（单人全职）

| 阶段 | 内容 | 估时 |
|---|---|---|
| 前置 1 | GPU 材质 API 补全 | 3~5 天 |
| 前置 2 | EEVEE material shader resource 扩展 | 2~3 天 |
| 前置 3 | Spike 验证（最小例） | 3 天 |
| 阶段 1 | 数据层 + 骨架 | 2~3 天 |
| 阶段 2 | GLSL 解析器 | 3~5 天 |
| 阶段 3 | GPU 节点图生成 | 5~7 天 |
| 阶段 4 | 完整功能 | 5~10 天 |
| 阶段 5 | GLSL Light Access | 3~5 天 |
| 阶段 6 | Script Expression | 1~2 天 |
| **合计** | | **约 27~43 天** |

**注意**：这是乐观估计，不含 5.3 EEVEE 管线不兼容导致的重构。**保守估计翻倍 = 2~3 个月**。
