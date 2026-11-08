# PROJECT_STATUS — D.2 主文档

- 日期：2026-11-05（初稿）/ **2026-11-07（K2 收口定稿）**
- 性质：**唯一权威状态文档**；只读汇总 + 只写本文档；未改源码、未跑构建、未 commit
- 写作原则：**能定的都定下来，依赖未完成项的地方标 `PENDING`，不凑数、不猜数**
- 本文所有数字都能指回 docs 或源码；口径冲突处按被引用文档的最新结论为准，并在正文注明

## 0.0 口径（最终定稿，2026-11-06）

**分档定义**（每个节点只归一类）：

| 档 | 计分 | 含义 |
|---|---|---|
| `full` | 1.0 | 节点实现完整；限制若来自引擎能力，不影响本档，另记 `KNOWN_LIMITATIONS` |
| `approximate` | 0.5 | **节点实现本身不完整**（不是引擎能力受限） |
| `unsupported` | 不进分母 | 明确不做，进 `DEFERRED_FOREVER` |
| `pending-device` | 不进分母 | 仅缺真机/设备验证，且须同时满足：① 有 Mock/Headless E2E 通过日志；② 有可核理由说明为何 headless 验不了 |

**公式**：`D = full + approximate`；严格 `= full / D`；加权 `= (full + 0.5·approximate) / D`；
覆盖 `= (full + approximate + pending-device) / (D + U + P)`（U = unsupported，P = pending-device；
衡量的是对全部登记节点的覆盖，与成熟度口径不同）。

**恒等式**：加权要等于 100%，唯一解是 `A = 0`，此时严格 `= F / D` 也必然 100%。
「加权 100% + 严格 85–90%」不可能同时成立——冻结时按实际算出的两个数报，不为对齐某个预期去调分母。

**三条既定裁定**（冻结时照此写，不放宽）：

1. `StringRegexMatch` / `StringRegexReplace` 记 `full`（2026-11-06 正则双修后）：两节点共用
   `IsSafeRegexPattern`（`KX_FunctionNodesB6String.cpp:146-300`），在 `std::regex` 构造前拒绝
   「组体整体无界重复」与「组体可匹配空串」（`(a+)+`、`(a*)*`、`(.*)*`、`(a?)+`），并新增
   `Rejected(Bool)` 输出区分「拒绝」与「不匹配」；`(\d+,)+`、`(ab?)+`、`(a|aa)+` 放行。
   剩余限制为长度上限（源 4096 / 模式 512）与交替型回溯**无墙钟超时**（`(a|aa)+$` 40 字符可达 29.7 秒），
   逐条登记在 `KNOWN_LIMITATIONS.md` #30，**不扣实现分**。
2. `LoadSceneAsync` 可升 `full`，但文档必须保留一句原文：「该节点**不异步**——加载在调用帧内完成，
   进度是反馈而非流式」。
3. 音频四项、`AddAnimationEvent`、场景 push/pop 三项、`TransitionScene`、
   `SetSceneTimeLevel` 的全局时标、AES 两项升 `full`，限制进 `KNOWN_LIMITATIONS`。

> 本节只写口径，不含数值。14 域三档表（严格 / 加权 / 覆盖）在 K 冻结时按重分类线交付的
> `*_RESCORE.md` 汇总表搬入，本文不改分档、只搬运；分子分母以实算为准。

## 0.0a K2 原生化 / K3 收口回归权威结果（2026-11-07，最终定稿）

> 本节取代本文其他小节里的旧分母/旧断言数（286 / 321 / 957 等）。口径见 §0.0，数字以本节为准。

**唯一权威与 RUN_ID 口径**：**本文是全仓库唯一权威状态文档**。其他文档中写死的
`RUN_ID=20261106-224926` 是**插件模式基线值**，按冻结令不再改；其断言、批次、节点数、
`native_nodes`、`checked_pairs` 与 K2 **全部 0 差异**。引用时以本文的
`RUN_ID=20261107-112207` 为准，不必（也不要）为此批量改其它文档。

**适用范围（必须随数字一起引用）**：

- `pending-device` 15 与 `unsupported` 7 已剔出严格/加权分母；覆盖口径才含 P；
- 三个数是 **C++ 实现正确率**，**不等于「用户默认体验」**；
- **E2E 入口覆盖 ≠ 用户点 P 的路径**：绝大多数 harness 直连 `KX_FunctionNode`/`KX_CompiledGraph`，
  不经过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口；
- B7.5 后 Python 逻辑执行器已删除、`nl_` 组件只警告并跳过——**回退路径已消失**，但这不等于
  用户默认路径已被 E2E 覆盖；「建树 → 点 P → 运行时调度」的缺口靠 `REAL_DEVICE_VERIFICATION.md`
  的实机验证补。

**树与冻结指纹**：B7.1–B7.5 已落地；`git status --porcelain -- source scripts tests CMakeLists.txt`
开跑/结束均为 **691 条**、`228800d468791426ac3566792d1aa2870b9f02f98017e4f7c1dd5fa6d2fa48a8`，
两次全量清单 `diff` **0 行**（无漂移、无需归因）。

**构建**：`build_linux` 从零 configure（01:30 段被主机 01:48 关机打断，10:50 在同一棵树上续跑，未删目录）。
`NINJA_EXIT=0`，**2845/2845**，`error:`=0、`FAILED`=0，续跑段 `10:50:38`→`11:16:22`；
`ninja install` `INSTALL_EXIT=0`（startup 脚本装入 `bin/5.3/scripts`）。三产物：

| 产物 | sha256 | bytes |
|---|---|---:|
| `bin/blender` | `8c7cbf5347693ccaeafc3c578a935101ab0e57ed90f7a633f6a346a981b4ab2f` | 233,041,568 |
| `bin/blenderplayer` | `39dcd4e133e9b986559ca5b69831ea726c734a31d610210cb563d2e03b37d2ec` | 261,890,720 |
| `lib/libge_logic_bricks.a` | `8bcf8d2d1b55d0e560efb9adb09b40373301533ff61c2c59c078f879510067ed` | 6,715,752 |

> **哈希口径**：`blender` / `blenderplayer` 在 K2 构建（11:16）之后又被 `ninja install`（11:20）重链过一次。
> 原因是 `buildinfo.h` 的生成边是无输入 `CUSTOM_COMMAND`，每次 ninja 调用都会重写 `BUILD_TIME`
> （UTC `03:20:19` = 本地 11:20:19），并触发 4 个 `buildinfo.c.o` 重编与三个可执行文件重链。
> 两次产物的字节数完全相同，只有内嵌时间字符串不同；源码指纹 `228800d4…`(691) 全程未变。
> 上表采用**当前工作区、也是九项验收与 E2E 实际使用的那一份**：`blender` 11:20:29、`blenderplayer` 11:20:38、
> `libge_logic_bricks.a` 11:15:52；K2 构建当下的 `9a4ba077…` / `0238a60a…` 已被 install 覆盖，不再作为交付哈希。
>
> **可复现口径（2026-11-07，K2 结论）**：**可复现的是**源码冻结指纹（691 条 / `228800d4…`）、三产物**字节数**、
> `buildinfo.h` 的 `BUILD_DATE`/`BUILD_TIME`、以及 E2E 的 `RUN_ID=20261107-112207` 与 `TOTAL=1456`；
> **不可复现的是**三产物 sha256——`buildinfo.h` 生成边是无输入 `CUSTOM_COMMAND`，每次 ninja 调用都会重写时间戳，
> 触发 `buildinfo.c.o` 重编与 `blender` / `blenderplayer` 重链，即使源码一字未改，哈希也会变。sha256 只对
> 「未被再次链接的那一次产物」有效。旧文档中「三产物 sha256 为可复现不变量」的说法**已作废**，以本节口径为准。
> K2 阶段交付二进制哈希**以上表三行为准**；本轮（K3）交付哈希见下方 K3 构建段。

**K3 收口回归构建口径（2026-11-07，本轮交付）**：本轮对 `build_linux` 执行 `rm -rf` 后从零 configure。
首轮 `-j6` 到 `6689/8513` 时**主机重启**（非构建报错，`/tmp` 锁随重启清理）；重启后恢复构建在
`source/blender/render/hydra/engine.cc` 失败——捆绑 USD 25.02 的 `TF_PY_ALLOW_THREADS_IN_SCOPE()`
展开为非限定名，与 `engine.cc` 不兼容，**与本轮载具 / Player 音频 / Print / Debug Trace / 端口标签改动无关**。
按 A1 已验证的 clean 配置补 `-DWITH_HYDRA=OFF -DWITH_USD=OFF` 重配后，末次 `NINJA_EXIT=0`、
**2674/2674**、`error:`=0、`FAILED`=0、`ELAPSED=1787s`；`ninja install` `INSTALL_RC=0`
（`bin/5.3/scripts`、`bin/5.3/python` 已装）。
因此**「与 K2 同配置」的前提不成立**：K2 构建未显式关闭 Hydra/USD，本轮为关闭状态。Hydra 是渲染代理、
USD 是场景交换格式，**均不被 GameNodes 图运行时使用**；`TypeCount()=327`、三契约、48 批 E2E 与
`libge_logic_bricks.a` 的行为均不受影响，**对 GameNodes 功能无影响**。
本轮三产物（install 后实测）：

| 产物 | sha256 | bytes |
|---|---|---:|
| `bin/blender` | `0568b532ead891627dda5b168fe4cdc99b3e76fac5369353f8224c0cc5caa395` | 233,544,424 |
| `bin/blenderplayer` | `97a7cf80c747e7b963656d63e464105c4c5ed5e6922be931b9fed67fae6e0b79` | 262,475,496 |
| `lib/libge_logic_bricks.a` | `1c42d445615b86da1eacdf08ffd6a48727e37fe93a058070482bc11c070207a6` | 6,743,498 |

> **sha256 口径不变（不可复现）**：install 前后两二进制字节数不变、哈希改变（install 前 `d56963d1…` / `ef837f95…`），
> 原因同上述 `buildinfo.h` 无输入 `CUSTOM_COMMAND`。K3 的交付哈希只对「install 后未被再次链接的那一次」有效；
> 可复现的是源码指纹、三产物**字节数**、`buildinfo` 字段与 E2E 的 `RUN_ID=20261107-221756`。

**校验（各跑一次）**：`port_contract native_nodes=244 catalog_nodes=68 errors=0 warnings=4`；
`enum_consistency checked_pairs=125 inconsistencies=0`；`gen_metadata --check` → `up to date (327 nodes)`；
`TypeCount()` 实测 **327**（常量物化修复新增 `ConstString` / `ConstVector` 两个 IR 内部辅助节点；旧对象缓存 325 作为对照）。

**E2E（K2）**：`RUN_ID=20261107-112207`，**46 批**，`TOTAL assertions=1456 failures=0 build_failures=0 crashes=0`，
`E2E_EXIT=0`；逐批 `.build.rc` 46/46 为 0、`error:` 命中 0；builtin 4 行整组例外（`b5_build.rc=0`、日志无 `error:`）。

**E2E（K3 收口回归）**：`RUN_ID=20261107-221756`，**48 批**，`TOTAL assertions=1481 failures=1 build_failures=0 crashes=0`
（`E2E_EXIT=1`）；逐批 `.build.rc` 48/48 为 0、`error:` 命中 0。唯一失败 `b667_defaults` 是**陈旧期望**
（`tests/b667_default_values.cpp:174` 原断言空槽输出 `"None"`，而本轮 `FuncPrint` 已改为 `<Print: Value input unset>`）；
已按 `KNOWN_LIMITATIONS.md` #32 更新该期望，单批重跑 `RUN_ID=20261107-222555` → `b667_defaults 6 / 0`、
`TOTAL assertions=6 failures=0`；其余 47 批未重跑。

**与插件模式基线的差异**：K2 断言 1456→1456、批次 46→46、`TypeCount()` 325→325、元数据 325→325、
`native_nodes` 244→244、`checked_pairs` 125→125，全 0；K3 断言 1456→1481（+`c6_vehicle_wheel_guard` 19、
+`b667_defaults` 6）、批次 46→48、`TypeCount()` 325→327、元数据 325→327（+2 IR 内部辅助节点
`ConstString` / `ConstVector`，由常量物化修复引入）、`native_nodes` 244→244、`checked_pairs` 125→125。

**六量与三式（当前树，2026-11-07）**：`N=327`；`F=301 / A=4 / U=7 / P=15`，`D=305`；
`301+4+7+15=327` 闭合成立；严格 **98.69%**（`301/305`）、加权 **99.34%**（`303/305`）、覆盖 **97.86%**（`320/327`）。
历史冻结快照 `RUN_ID=20261106-224926`：`N=325 / F=299 / A=4 / U=7 / P=15 / D=303`（98.68% / 99.34% / 97.85%）。

**九项原生化验收（实测）**：

| # | 检查项 | 结果 |
|---|---|---|
| 1 | `preferences.addons.keys()` 无 `bge_gamenodes` / `bge_netlogic` | PASS（插件列表 10 项，两者均无） |
| 2 | `addon_utils.modules(refresh=True)` 无两者 | PASS（69 个模块，两者均无） |
| 3 | 启动 stderr 0 行、`EXIT=0` | PASS（三次 `--factory-startup --background`，stderr 均 0 行、EXIT 0） |
| 4 | `preferences.game_nodes` 存在且字段可读 | PASS（25 个字段：`uplogic_version` / `use_node_debug` / `palette_*` 等） |
| 5 | `USERPREF_PT_game_gamenodes` 面板已注册 | PASS（`hasattr(bpy.types, ...)` = True） |
| 6 | 节点树下拉含 `('BGELogicTree', 'Game Nodes')` | PASS（`node_groups.new(..., 'BGELogicTree')` 成功，`bl_idname=BGELogicTree`、`bl_label=Game Nodes`；`bpy.types` 属性名不含 `BGELogicTree` 只因该类以 Python 类名注册，不影响下拉） |
| 7 | `bpy.ops.logic.*` 19 个 | PASS（19，逐名一致） |
| 8 | 启动日志无 `Add-on not loaded: "bge_netlogic"` | PASS（0 命中） |
| 9 | `use_native_graph_runtime` 全仓 0 命中 | PASS（`scripts/ source/ tests/`，0） |

**禁用符号**：`grep -rn 'space_logic\|SCA_\|LOGIC_EDITOR\|bge_netlogic' tests/`（仅源码类型）→ **0 命中**。

**非 MANIFEST 证据**：`tests/b73_component_skip.cpp` 未登记进 MANIFEST，按约定单独构建（`error:`=0，rc=0），
但直跑 `SIGSEGV`（rc=139，崩溃在 `Py_Initialize`/`KX_GameObject` 初始化段，未打印任何断言），
**不作为通过证据、不计入六量**；B7.3 的组件跳过逻辑另有源码路径复核。

## 0. 一句话定位与当前阶段

**这个项目是什么**：把 legacy uplogic 图（原 `bge_netlogic` 包，现模块 `bge_gamenodes`、产品名 **Game Nodes**）的旧逻辑节点迁移到引擎内的
**GameNode 原生节点系统**（`KX_LogicNodeRuntime` + 表驱动 `KX_FunctionSpec`，
注册入口 `KX_RuntimeNodes.cpp:201-250`），让逻辑图不再依赖 Python 运行时。

**形态（B7.5）**：包在 `scripts/startup/bge_gamenodes`，**随引擎启动加载、不可禁用**，不在 `Edit > Preferences > Add-ons` 里；`AddonPreferences` 已删除，偏好为 `bpy.context.preferences.game_nodes`（面板 `USERPREF_PT_game_gamenodes`，**Preferences → Game**）；「原生运行时」开关已删除，manifest 导出恒执行；Python 逻辑执行器（`tree_code_generator` 等 6 文件）已删除，旧 `.blend` 的 `nl_` 组件运行时只警告并跳过（每对象一次，带对象名与树名），不自动转换。节点树 `bl_idname` 仍为 `BGELogicTree`（旧文件兼容），显示名 **Game Nodes**。已知残留：`NodeSocketLogicNodeGroup*` 类名保留（`.blend` 类型键）、`ops/tempCodeRunnerFile.py` 是误跟踪的不可解析草稿、105 处 `bl_idname` 仍为 `LogicNode*`。`bge_bricknodes` 仍是独立 addon，不在本轮范围。

**当前阶段**：B6.6 收口中；B6.7 尾巴已修大部分；**B6.8 收口进行中**。

> `PENDING-1`：B6.8 尚无可引用的独立验收文档（仓库内检索无 `B6.8` 条目），
> 因此本文只记录阶段事实，不给出 B6.8 完成度数字。

## 1. 能力域加权（分母 286，不是 290）

分母取证：14 个内建节点 + 270 个表驱动节点 + 2 个表驱动特例（`Formula`、`StringFormat`）
= **286 个运行时已注册节点**。编辑器可见节点是 226，`port_contract` 的 native 表是 223，
legacy 全量是 290——**这四个数不可互换**。详见
[CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §1。

| 口径 | 主口径（D.2 采用） | 上界（8 项 Mock 转真机后） |
|---|---:|---:|
| 14 域等权 · 严格 | **81.5%** | 86.3% |
| 节点加权 · 严格 | **88.1%**（252/286） | 90.9%（260/286） |
| 节点加权 · 加权 | **93.4%**（267/286） | 94.8%（271/286） |
| 节点加权 · 覆盖 | **98.6%**（282/286） | 98.6%（282/286） |

- 8 项 B3 音频/动画节点是 `mock-verified`（Mock 层已断言、真机未执行），
  严格计 0、加权计 0.5、覆盖计 1。逐项见
  [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §4.3、§11。
- **「82–84%」不是任何实测口径的复现值**：14 域等权为 **81.5%**（主口径）/ **86.3%**（上界），
  节点严格为 **88.1%** / **90.9%**。
- **88.1% 只是「实现成立」口径**：其中 **85 条仅有 Mock 证据**，
  按证据层级**实算**后严格为 **63.3%**（原 5 条「未覆盖」已全部补齐、20 条断言全 PASS），见 §4。
  更保守的 **58.4% / 56.6%** 含 14 条 `approximate` 的重复扣减，**仅作下界、不是实测值**。
  **对外主口径用 63.3%；不要只引用 88.1%。**
- **分母写错（用 290）会得到另一套数**：286 与 290 既不可相加，也不可换算。
- **327 口径（当前树，2026-11-07；历史冻结快照 `325` 见 §0.0a）**：`N=327`、`F=301 / A=4 / U=7 / P=15`、`D=305`；严格 **98.69%**（`301/305`）、加权 **99.34%**（`303/305`）、覆盖 **97.86%**（`320/327`）。本节的 286 域权重表**不是**该口径，**不得混算**。

## 2. Legacy 兼容度三口径（不可相加、不可换算）

依据 [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md) §0：

| 口径 | 分母 | 已迁移 | 比例 | 可复算 |
|---|---:|---:|---:|---|
| legacy 全量 | **290** | 69 | **23.8%** | 是 |
| 用户可感知 | **233** | 69 | **29.6%** | 是 |
| semantic | **360** | 146 | **40.6%** | 是（`native_audit.py`，LCP §5） |

- 三条口径回答的是三个不同问题，**不能相加、不能平均、不能互相换算**；
  写报告时先写分母（286 / 290 / 233 / 360 各自独立）。
- **290 全量的 70% 目标理论上限只有 54.5%**（`(69+89)/290`，LCP §3）：
  不是“再补几个端口”能达到的，70% 在“端口契约严格 + 不做引擎层改造”约束下不可达。
- 关于 40.6% 的标注：任务书旧版要求标「用户给定值，待补算式」，但
  [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md) §5 已改为**可复算的 146/360**，
  并明确作废旧标注。本文按最新结论写，不沿用旧标注。

## 3. B6.1 物理 / 动画分档（23 节点）

依据 [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §6 与
[B6_ANIMATION.md](B6_ANIMATION.md) §6：

| 组 | 节点数 | full | approximate | unsupported |
|---|---:|---:|---:|---:|
| 物理 | 15 | **12** | 3 | **0**（C.1.1 后已无 unsupported） |
| 动画 | 8 | 4 | 2 | 2 |
| **合计** | **23** | **16** | **5** | **2** |

合计三口径：严格 **16/23 = 69.6%**、加权 **18.5/23 = 80.4%**、覆盖 **21/23 = 91.3%**。

> 本表的物理行是 P-1 / P-3.5 / c8 之前的快照；`BuildNavMesh` 已由运行时 Recast 烘焙升为 `full`，
> 物理侧最新分档为 `15 full / 0 approximate / 0 unsupported`（[B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §6）。

- 物理侧原「剩余 3 个 `approximate`」中的 `BuildNavMesh` agent 尺寸一项已由 P-1 接上
  （`KX_NavMeshObject::BuildNavMeshRuntime`，完整 Recast → Detour 烘焙，判 `full`）。
- 动画 2 个 `unsupported`：`SetBoneWeight`、`PlayBoneAction`（需引擎层改造，2 天内不可完成）。

## 4. 验证层级声明（必读）

**88.1% 是「有实现」的下界，不是「真机可交付」的下界。** 分档本身只回答实现是否成立，
没有系统区分证据来自 Mock / 真实引擎路径 / 真机。前一轮只把已明确只有 Mock 证据的 8 项
改判 `mock-verified`，其余 278 项沿用旧口径；本轮把 B 类 49 条逐条查到驱动实现层
（[VERIFICATION_LEVEL_AUDIT.md](VERIFICATION_LEVEL_AUDIT.md)）：

- **B 类 49 条实测：44 mock-driven / 0 engine-driven / 5 未覆盖**（89.8% / 0% / 10.2%；
  这 5 条现已全部补齐，20 条断言全 PASS）。
- **原 5 条「未覆盖」已全部补齐**：`JoystickAxis`、`JoystickButton`、`ProximitySensor` 原先只有注册、
  无任何 type_id 引用；`JSONStringify`、`JSONParse` 原先只执行、无 `Check` 读输出。
  补真断言后 **20 条全 PASS**（`/tmp/b66_uncovered_assertions.log`），因此**不再有「剔除未覆盖」档**，
  实算即 63.3%。
- 另有网络 11 条属「真实引擎路径已验、跨机未验」，不在 49 条内。

| 口径 | 算式 | 结果 | 定位 |
|---|---|---:|---|
| 当前主口径严格（实现成立） | `252 / 286` | **88.1%** | 内部参考 |
| 证据层级**实算**（只扣真正从 full 跌落的 71 条） | `(252 − 71) / 286 = 181 / 286` | **63.3%** | **对外主口径** |
| 保守下界（85 条一律从 full 扣，含 14 条 `approximate` 重复扣减） | `(252 − 85) / 286 = 167 / 286` | 58.4% | **下界，非实测** |
| 下界再减 5 条（历史更低下界） | `(167 − 5) / 286 = 162 / 286` | 56.6% | 下界，非实测 |

- 旧的 **73.8%** 只是「仅 A 类 41 条改判」的历史情景，已被上述实算值 **63.3%** 取代；
  `0 engine-driven` 说明 B 类不会把数字拉回 88.1%。
- **实算值已回写** [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §13：
  §0 的 88.1% 保留为「实现成立」口径（内部进度跟踪），**对外主口径用 63.3%**。
- **结论：88.1% 是「有实现」的下界；85 条只有 Mock 证据，
  因此「真机可交付」口径明显更低（**63.3%**），不能拿 88.1% 当交付承诺。**
  对外写能力时，必须同时给出口径、分母与 `VERIFICATION_LEVEL_AUDIT.md` 的逐条清单；
  **58.4% / 56.6% 只能以「更保守下界（含重复扣减）」出现，不得当实测值。**

## 5. 本轮交付清单（按域，带证据）

| 域 | 交付项 | 证据 |
|---|---|---|
| 逻辑/值计算 | `LogicGate` 六个 op 全部注册（`and/or/xor/not/nand/nor`）+ 调度器 trigger 快照 | `KX_FunctionNode.cpp:1355`；`native_batch.py:75-85`；断言 22→30：`/tmp/verify_gap/b661_logicgate_v.log`（`RESULT assertions=30 failures=0`） |
| 逻辑/值计算 | `LogicGate` 端口契约升级（1 出→3 出，旧 IR 编译期拒绝） | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §0（`:6-22`） |
| 场景查询 | `WorldPositionLegacy` 真实反投影（真实 `KX_Camera` + `GPU_matrix_unproject_3fv`） | 误差 **1.679e-6**（`max_err={:.3e}` 打印值，3 位有效数字；`/tmp/verify_gap/worldpos_real_final.log:32`；测量前提 **1920×1080，与场景 16:9 一致**，改 4:3 会得另一组值）：[B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) `:208-211`；实现 `KX_GameAdaptersEngine.cpp:635-686` |
| 输入/触摸 | `TouchLegacy` 球近似（世界位置球心 + 最大缩放半径，非真实碰撞体重叠；分档 `approximate`） | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) `:200-209`；`KX_GameAdaptersEngine.cpp:1034-1088` |
| UI | C3/C.7 三项：Polar 布局命中、圆形旋钮（`DrawCircleApprox` 近似）、富文本行内颜色 | `/tmp/a1_clean_run_b661_e2e.log`（19 PASS，含 polar 4/8 槽、slider 1/2/0 次、rich text 3 条）；[B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §C.7 |
| UI | 多字体（`BLF_load_unique` 独立槽位） | [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §C.7.1 |
| UI | 基础竖排（非排版级） | [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §C.7.2 |
| 网络 | UDP v2 协议（v2 握手换端口、payload 上限 65507-16） | [B6_NETWORK.md](B6_NETWORK.md) §4；`/tmp/c31_udp_e2e`（`failures=0`，corrupt=0/duplicate=0） |
| 网络 | 房间列表 + 重连（无 host 300 ms 明确失败、host 恢复后 attempts=1） | [B6_NETWORK.md](B6_NETWORK.md) §8；`/tmp/c32_rooms_e2e`（**32 PASS**） |
| 物理 | `SetPhysicsRestitution` 运行时 setter（不重建刚体） | `KX_PhysicsAdvancedAdapter.cpp:841-864`；`CcdPhysicsController.cpp:1399-1415` |
| 物理 | 载具 `wheel_names`（逗号分隔，空则回退 Template） | `KX_PhysicsAdvancedAdapter.cpp:495-616、671-711` |
| 物理 | 导航运行时 Recast 烘焙（`agent_radius`/`agent_height` 生效） | [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.4 / §5 `c6_navmesh_bake` |
| 文件 | 存档 zlib 压缩（`SaveGameBinary` 的 `Compress`，version=2） | [B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §压缩（`:44-60`） |
| 音频 | `StartSound` 句柄 32 位截断守卫（超 `INT32_MAX` 立即 `StopSound`、`Handle=0`） | `KX_FunctionNodesB3.cpp:361-377`、`:391-413`；`/tmp/audio_evidence.log`（Mock 断言） |
| 真机 | 绿框（green box）真机验证 | [REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md)（唯一 `verified`） |
| 全量 E2E | **46 批 / 1456 条断言 / 0 失败 / 0 崩溃**（K2 定稿 `RUN_ID=20261107-112207`；逐批 `.build.rc` 46/46 全 0；42 份 harness 构建日志 `error:` 0 份；无「证据不足」批次） | 逐批 `assertions=` 求和 = **1456**；插件模式基线 851/26 批（J 块）与 957/29 行（`RUN_ID=20261106-101536`）均已被覆盖 |

> 注意：C3 的「圆形旋钮」是 `DrawCircleApprox` 近似（不是真圆角矩形控件）；
> 多字体与竖排都有明确「不做」清单（字形覆盖检测、粗斜体、排版级竖排）。

**上表的验证层级（不要混读）**：

| 证据类型 | 条目 | 说明 |
|---|---|---|
| 真实引擎/真机 | 绿框、`WorldPosition` 反投影 | `blenderplayer` 真实相机/真实 `unproject`（**1.679e-6**，`/tmp/verify_gap/worldpos_real_final.log:32`；前提 1920×1080 与场景 aspect 一致）；绿框为真机 `verified` |
| 本机真实进程（非跨机） | UDP v2、房间列表/重连 | 本机双进程 UDP + TCP，**不是跨机网络验证** |
| headless 渲染调用层 | C3 三项、多字体、竖排 | 断言的是布局坐标/绘制调用/文本分段，**不是真机显示效果** |
| Mock 适配器层 | `StartSound` 截断守卫、8 项 `mock-verified` | 只证明节点语义与参数读取，真机 `pending` |
| 实现 + 文档 | zlib 存档、restitution、`wheel_names`、导航运行时 Recast 烘焙 | 有源码行号与文档，未逐项跑真机 E2E |

## 6. 已知限制（前 5 条）

完整清单见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)（30 条：active 25 / mitigated 4 / removed 1）。

1. 音频 bus/zone 是等价实现，不是 audaspace 原生总线（#1）。
2. UDP 通道无重传、无拥塞控制、不保序（#2）。
3. `UnloadScene` 四态语义为 `mitigated`，根因仍在（#3）。
4. `GraphCompiler` 输出白名单只到 `Collision`（#4）。
5. `LoadSceneAsync` 进度是反馈值，不是真实加载进度（#5）。

> **2026-11-07 新增严重项：资源在 Player 模式下未注册**（[KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) #31，`active`）。
> 归类：**引擎/平台层**（运行时资源加载器），不是节点实现层——编辑器内按 P 正常，节点实现无缺失，
> 因此**不改变 327 口径的六量与三个百分比，也不进 `approximate` 口径**。
> 现象已确认；根因未确认（候选：`BL_SceneConverter` 未注册 Sound 数据块 / 注册名不一致 / 资源未打包或相对路径在 player 下失效）。

### 6.1 已隐藏的「可见但不可执行」legacy 菜单行（2026-11-07，用户批准）

**判定规则（机器判定，无手写名单）**：在已注册节点类中，`native_only == False`
且 `runtime_type_id == ''` 的类，其 `node.add_node` 菜单行由
`ui/node_menu.py::insertNode` 直接跳过。**只跳过菜单行**：类仍注册、`bl_idname` 未改，
旧 `.blend` 中已存在的节点照常加载；导出时仍由 `ir_export.add_node` 跳过并给出警告。

**隐藏后实测计数**（`build_linux/bin/blender --factory-startup -b`，601 个节点类）：

| 指标 | 数值 | 说明 |
|---|---:|---|
| 一级菜单项 | **23** | 无独立 `Native Nodes` 顶层 |
| 菜单节点行（含全部子菜单） | **302** | 隐藏前 532；本规则隐藏 **230 行 / 176 类** |
| legacy 可见 | **41** | 全部有 `runtime_type_id`；与预估 41 一致 |
| legacy 可见但无 runtime | **0** | 本轮清零 |
| native 可见 | **254 / 254** | 无缺失 |
| 重复标签组 | **0** | 与隐藏前一致，仍为 0 |

各一级分类可见节点行数（`direct` 为菜单直属行，`subtree` 含其全部子菜单）：

| 一级菜单 | direct | subtree |
|---|---:|---:|
| `LN_MT_events_menu` | 14 | 15 |
| `LN_MT_game_menu` | 2 | 2 |
| `LN_MT_input_menu` | 8 | 28 |
| `LN_MT_values_menu` | 56 | 62 |
| `LN_MT_animation_menu` | 10 | 10 |
| `LN_MT_lights_menu` | 0 | 0 |
| `LN_MT_nodes_menu` | 0 | 0 |
| `LN_MT_objects_menu` | 28 | 28 |
| `LN_MT_scene_menu` | 29 | 32 |
| `LN_MT_sound_menu` | 0 | 0 |
| `LN_MT_logic_menu` | 4 | 4 |
| `LN_MT_math_menu` | 18 | 18 |
| `LN_MT_physics_menu` | 31 | 36 |
| `LN_MT_python_menu` | 0 | 0 |
| `LN_MT_time_menu` | 6 | 6 |
| `LN_MT_data_menu` | 0 | 0 |
| `LN_MT_file_menu` | 18 | 18 |
| `LN_MT_network_menu` | 17 | 17 |
| `LN_MT_portal_menu` | 0 | 0 |
| `LN_MT_render_menu` | 5 | 5 |
| `LN_MT_ui_menu` | 15 | 15 |
| `LN_MT_layout_menu` | 2 | 2 |
| `LN_MT_utility_menu` | 4 | 4 |

> `direct=0` 不等于分类为空：`Lights`/`Sound`/`Python`/`Data`/`Nodes` 的可见行全在子菜单或已全部隐藏；
> `Portal` 的「Portal In」由 `insertPortal` 绘制，不是 `node.add_node` 行，故不计入上表。

可核对清单（176 类；按 `bl_idname` 排序。「—」表示当前没有对应的 native 编辑器节点）：

| # | 类名 | `bl_idname` | 隐藏原因 | 替代项 |
|---|---|---|---|---|
| 1 | `GameNodeAddUIWidget` | `LogicNodeAddUIWidget` | legacy 且 `runtime_type_id` 为空（原菜单标签「Add Widget」） | —（无同名 native 替代） |
| 2 | `GameNodeApplyTransform` | `LogicNodeApplyTransform` | legacy 且 `runtime_type_id` 为空（原菜单标签「Apply Transform」） | —（无同名 native 替代） |
| 3 | `GameNodeCreateUIPath` | `LogicNodeCreateUIPath` | legacy 且 `runtime_type_id` 为空（原菜单标签「Create Path」） | —（无同名 native 替代） |
| 4 | `GameNodeCurveInterpolation` | `LogicNodeCurveInterpolation` | legacy 且 `runtime_type_id` 为空（原菜单标签「Curve Interpolation」） | —（无同名 native 替代） |
| 5 | `GameNodeDictGetKeys` | `LogicNodeDictGetKeys` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Dictionary Keys」） | —（无同名 native 替代） |
| 6 | `GameNodeDistributeCurvePoints` | `LogicNodeDistributeCurvePoints` | legacy 且 `runtime_type_id` 为空（原菜单标签「Distribute Points on Curve」） | —（无同名 native 替代） |
| 7 | `GameNodeDraw` | `LogicNodeDraw` | legacy 且 `runtime_type_id` 为空（原菜单标签「Draw」） | —（无同名 native 替代） |
| 8 | `GameNodeEvaluateCurve` | `LogicNodeEvaluateCurve` | legacy 且 `runtime_type_id` 为空（原菜单标签「Evaluate Curve」） | —（无同名 native 替代） |
| 9 | `GameNodeGetCollisionBitMask` | `LogicNodeGetCollisionBitMask` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Collision Group」） | —（无同名 native 替代） |
| 10 | `GameNodeGetFont` | `LogicNodeGetFont` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Font」） | —（无同名 native 替代） |
| 11 | `GameNodeGetLogicTreeProperty` | `LogicNodeGetLogicTreeProperty` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Tree Property」） | —（无同名 native 替代） |
| 12 | `GameNodeGetMasterFolder` | `LogicNodeGetMasterFolder` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Master Folder」） | —（无同名 native 替代） |
| 13 | `GameNodeGetRigBoneAttribute` | `LogicNodeGetRigBoneAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Bone Attribute」） | —（无同名 native 替代） |
| 14 | `GameNodeJoinPath` | `LogicNodeJoinPath` | legacy 且 `runtime_type_id` 为空（原菜单标签「Join Path」） | —（无同名 native 替代） |
| 15 | `GameNodeListFromItems` | `LogicNodeListFromItems` | legacy 且 `runtime_type_id` 为空（原菜单标签「List From Items」） | —（无同名 native 替代） |
| 16 | `GameNodeLocalClient` | `LogicNodeLocalClient` | legacy 且 `runtime_type_id` 为空（原菜单标签「LAN Client」） | —（无同名 native 替代） |
| 17 | `GameNodeLocalServer` | `LogicNodeLocalServer` | legacy 且 `runtime_type_id` 为空（原菜单标签「LAN Server」） | —（无同名 native 替代） |
| 18 | `GameNodeLogicGateList` | `LogicNodeLogicGateList` | legacy 且 `runtime_type_id` 为空（原菜单标签「Gate List」） | —（无同名 native 替代） |
| 19 | `GameNodeLoop` | `LogicNodeLoop` | legacy 且 `runtime_type_id` 为空（原菜单标签「Loop」） | —（无同名 native 替代） |
| 20 | `GameNodeListLoop` | `LogicNodeLoopFromList` | legacy 且 `runtime_type_id` 为空（原菜单标签「Loop from List」） | —（无同名 native 替代） |
| 21 | `GameNodeRaycast` | `LogicNodeRaycast` | legacy 且 `runtime_type_id` 为空（原菜单标签「Raycast」） | —（无同名 native 替代） |
| 22 | `GameNodeRebuildData` | `LogicNodeRebuildData` | legacy 且 `runtime_type_id` 为空（原菜单标签「Rebuild Data」） | —（无同名 native 替代） |
| 23 | `GameNodeResizeVector` | `LogicNodeResizeVector` | legacy 且 `runtime_type_id` 为空（原菜单标签「Resize Vector」） | —（无同名 native 替代） |
| 24 | `GameNodeRotateByPoint` | `LogicNodeRotateByPoint` | legacy 且 `runtime_type_id` 为空（原菜单标签「Vector Rotate」） | —（无同名 native 替代） |
| 25 | `GameNodeSerializeData` | `LogicNodeSerializeData` | legacy 且 `runtime_type_id` 为空（原菜单标签「Serialize Data」） | —（无同名 native 替代） |
| 26 | `GameNodeSetConstraintAttribute` | `LogicNodeSetConstraintAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Constraint Attribute」） | —（无同名 native 替代） |
| 27 | `GameNodeSetCustomCursor` | `LogicNodeSetCustomCursor` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Custom Cursor」） | —（无同名 native 替代） |
| 28 | `GameNodeSetLogicTreeProperty` | `LogicNodeSetLogicTreeProperty` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Tree Property」） | —（无同名 native 替代） |
| 29 | `GameNodeSetRigBoneAttribute` | `LogicNodeSetRigBoneAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Bone Attribute」） | —（无同名 native 替代） |
| 30 | `GameNodeSpawnPool` | `LogicNodeSpawnPool` | legacy 且 `runtime_type_id` 为空（原菜单标签「Spawn Pool」） | —（无同名 native 替代） |
| 31 | `GameNodeStringOperation` | `LogicNodeStringOperation` | legacy 且 `runtime_type_id` 为空（原菜单标签「String Operation」） | —（无同名 native 替代） |
| 32 | `GameNodeToggleLogicTreeProperty` | `LogicNodeToggleLogicTreeProperty` | legacy 且 `runtime_type_id` 为空（原菜单标签「Toggle Tree Property」） | —（无同名 native 替代） |
| 33 | `GameNodeTweenValue` | `LogicNodeTweenValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Tween Value」） | —（无同名 native 替代） |
| 34 | `GameNodeVector` | `LogicNodeVector` | legacy 且 `runtime_type_id` 为空（原菜单标签「Vector」） | —（无同名 native 替代） |
| 35 | `GameNodeVectorMath` | `LogicNodeVectorMath` | legacy 且 `runtime_type_id` 为空（原菜单标签「Vector Math」） | —（无同名 native 替代） |
| 36 | `GameNodeAddPhysicsConstraint` | `NLActionAddPhysicsConstraint` | legacy 且 `runtime_type_id` 为空（原菜单标签「Add Physics Constraint」） | —（无同名 native 替代） |
| 37 | `GameNodeClearVariables` | `NLActionClearVariables` | legacy 且 `runtime_type_id` 为空（原菜单标签「Clear Variables」） | —（无同名 native 替代） |
| 38 | `GameNodeSendEvent` | `NLActionCreateMessage` | legacy 且 `runtime_type_id` 为空（原菜单标签「Send Event」） | —（无同名 native 替代） |
| 39 | `GameNodeRunLogicTree` | `NLActionExecuteNetwork` | legacy 且 `runtime_type_id` 为空（原菜单标签「Run Logic Tree」） | —（无同名 native 替代） |
| 40 | `GameNodeFollowPath` | `NLActionFollowPath` | legacy 且 `runtime_type_id` 为空（原菜单标签「Follow Path」） | —（无同名 native 替代） |
| 41 | `GameNodeCharacterInfo` | `NLActionGetCharacterInfo` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Physics Info」） | —（无同名 native 替代） |
| 42 | `GameNodeAddLogicTree` | `NLActionInstallSubNetwork` | legacy 且 `runtime_type_id` 为空（原菜单标签「Add Logic Tree to Object」） | —（无同名 native 替代） |
| 43 | `GameNodeListVariables` | `NLActionListVariables` | legacy 且 `runtime_type_id` 为空（原菜单标签「List Saved Variables」） | —（无同名 native 替代） |
| 44 | `GameNodeVariableLoad` | `NLActionLoadVariable` | legacy 且 `runtime_type_id` 为空（原菜单标签「Load Variable」） | —（无同名 native 替代） |
| 45 | `GameNodeVariablesLoad` | `NLActionLoadVariables` | legacy 且 `runtime_type_id` 为空（原菜单标签「Load Variable Dict」） | —（无同名 native 替代） |
| 46 | `GameNodeMoveToNavmesh` | `NLActionNavigate` | legacy 且 `runtime_type_id` 为空（原菜单标签「Move To with Navmesh」） | —（无同名 native 替代） |
| 47 | `GameNodePauseSound` | `NLActionPauseSound` | legacy 且 `runtime_type_id` 为空（原菜单标签「Pause Sound」） | —（无同名 native 替代） |
| 48 | `GameNodePlayAnimation` | `NLActionPlayActionNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Play Animation」） | —（无同名 native 替代） |
| 49 | `GameNodeRemovePhysicsConstraint` | `NLActionRemovePhysicsConstraint` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Constraint」） | `GameNodeRemoveConstraint` |
| 50 | `GameNodeRemoveVariable` | `NLActionRemoveVariable` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Variable」） | —（无同名 native 替代） |
| 51 | `GameNodeReplaceMesh` | `NLActionReplaceMesh` | legacy 且 `runtime_type_id` 为空（原菜单标签「Replace Mesh」） | —（无同名 native 替代） |
| 52 | `GameNodeResumeSound` | `NLActionResumeSound` | legacy 且 `runtime_type_id` 为空（原菜单标签「Resume Sound」） | —（无同名 native 替代） |
| 53 | `GameNodeSaveVariable` | `NLActionSaveVariable` | legacy 且 `runtime_type_id` 为空（原菜单标签「Save Variable」） | —（无同名 native 替代） |
| 54 | `GameNodeSaveVariableDict` | `NLActionSaveVariables` | legacy 且 `runtime_type_id` 为空（原菜单标签「Save Variable Dict」） | —（无同名 native 替代） |
| 55 | `GameNodeSetCollectionVisibility` | `NLActionSetCollectionVisibility` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Collection Visibility」） | —（无同名 native 替代） |
| 56 | `GameNodeSetGlobalValue` | `NLActionSetGlobalValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Global Property」） | —（无同名 native 替代） |
| 57 | `GameNodeSetPhysics` | `NLActionSetPhysicsNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Physics」） | —（无同名 native 替代） |
| 58 | `GameNodeJumpToFile` | `NLActionStartGame` | legacy 且 `runtime_type_id` 为空（原菜单标签「Load Blender File」） | —（无同名 native 替代） |
| 59 | `GameNodeStopAllSounds` | `NLActionStopAllSounds` | legacy 且 `runtime_type_id` 为空（原菜单标签「Stop All Sounds」） | —（无同名 native 替代） |
| 60 | `GameNodeActiveCamera` | `NLActiveCameraParameterNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Active Camera」） | —（无同名 native 替代） |
| 61 | `GameNodeAddFilter` | `NLAddFilter` | legacy 且 `runtime_type_id` 为空（原菜单标签「Add Filter」） | —（无同名 native 替代） |
| 62 | `GameNodeListAppend` | `NLAppendListItem` | legacy 且 `runtime_type_id` 为空（原菜单标签「Append」） | —（无同名 native 替代） |
| 63 | `GameNodeLogicTreeStatus` | `NLConditionLogitNetworkStatusNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Logic Tree Status」） | —（无同名 native 替代） |
| 64 | `GameNodeMouseOver` | `NLConditionMouseTargetingNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Mouse Over」） | —（无同名 native 替代） |
| 65 | `GameNodeNotNone` | `NLConditionNotNoneNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Not None」） | —（无同名 native 替代） |
| 66 | `GameNodeOnValueChangedTo` | `NLConditionValueTriggerNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「On Value Changed To」） | —（无同名 native 替代） |
| 67 | `GameNodeControllerStatus` | `NLControllerStatus` | legacy 且 `runtime_type_id` 为空（原菜单标签「Controller Status」） | —（无同名 native 替代） |
| 68 | `GameNodeCopyProperty` | `NLCopyPropertyFromObject` | legacy 且 `runtime_type_id` 为空（原菜单标签「Copy Property From Object」） | —（无同名 native 替代） |
| 69 | `GameNodeVehicleCreate` | `NLCreateVehicleFromParent` | legacy 且 `runtime_type_id` 为空（原菜单标签「Create New Vehicle」） | —（无同名 native 替代） |
| 70 | `GameNodeListDuplicate` | `NLDuplicateList` | legacy 且 `runtime_type_id` 为空（原菜单标签「Duplicate」） | —（无同名 native 替代） |
| 71 | `GameNodeListExtend` | `NLExtendList` | legacy 且 `runtime_type_id` 为空（原菜单标签「Extend」） | —（无同名 native 替代） |
| 72 | `GameNodeHasProperty` | `NLGameObjectHasPropertyParameterNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Object Has Property」） | —（无同名 native 替代） |
| 73 | `GameNodeGamepadActive` | `NLGamepadActive` | legacy 且 `runtime_type_id` 为空（原菜单标签「Gamepad Active」） | —（无同名 native 替代） |
| 74 | `GameNodeGamepadLook` | `NLGamepadLook` | legacy 且 `runtime_type_id` 为空（原菜单标签「Gamepad Look」） | —（无同名 native 替代） |
| 75 | `GameNodeGamepadSticks` | `NLGamepadSticksCondition` | legacy 且 `runtime_type_id` 为空（原菜单标签「Gamepad Sticks」） | —（无同名 native 替代） |
| 76 | `GameNodeGamepadVibration` | `NLGamepadVibration` | legacy 且 `runtime_type_id` 为空（原菜单标签「Gamepad Vibrate」） | —（无同名 native 替代） |
| 77 | `GameNodeGetActuatorValue` | `NLGetActuatorValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Actuator Value」） | —（无同名 native 替代） |
| 78 | `GameNodeGetGetCollection` | `NLGetCollectionNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Collection」） | —（无同名 native 替代） |
| 79 | `GameNodeGetCollectionObjectNames` | `NLGetCollectionObjectNamesNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Object Names」） | —（无同名 native 替代） |
| 80 | `GameNodeGetCollectionObjects` | `NLGetCollectionObjectsNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Objects」） | —（无同名 native 替代） |
| 81 | `GameNodeGetCurvePoints` | `NLGetCurvePoints` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Curve Points」） | —（无同名 native 替代） |
| 82 | `GameNodeDictGetKey` | `NLGetDictKeyNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Dictionary Key」） | —（无同名 native 替代） |
| 83 | `GameNodeGetGeoNodeAttr` | `NLGetGeometryNodeAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Node Value」） | —（无同名 native 替代） |
| 84 | `GameNodeGetGeoNodeSocket` | `NLGetGeometryNodeValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Socket Value」） | —（无同名 native 替代） |
| 85 | `GameNodeGetImage` | `NLGetImage` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Image」） | —（无同名 native 替代） |
| 86 | `GameNodeGetLightColor` | `NLGetLightColorAction` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Light Color」） | —（无同名 native 替代） |
| 87 | `GameNodeGetLightEnergy` | `NLGetLightEnergy` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Light Power」） | —（无同名 native 替代） |
| 88 | `GameNodeListGetIndex` | `NLGetListIndexNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get List Index」） | —（无同名 native 替代） |
| 89 | `GameNodeGetMatNode` | `NLGetMaterialNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Node」） | —（无同名 native 替代） |
| 90 | `GameNodeGetMaterialNodeAttr` | `NLGetMaterialNodeAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Node Value」） | —（无同名 native 替代） |
| 91 | `GameNodeGetMaterialSocket` | `NLGetMaterialNodeValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Socket Value」） | —（无同名 native 替代） |
| 92 | `GameNodeGetGroupSocket` | `NLGetNodeGroupNodeValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Socket Value」） | —（无同名 native 替代） |
| 93 | `GameNodeGroupNodeAttr` | `NLGetNodeTreeNodeAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Node Value」） | —（无同名 native 替代） |
| 94 | `GameNodeGetObjectID` | `NLGetObjectDataName` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Object ID」） | —（无同名 native 替代） |
| 95 | `GameNodeGetObjectVertices` | `NLGetObjectVertices` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Vertices」） | —（无同名 native 替代） |
| 96 | `GameNodeListGetRandom` | `NLGetRandomListIndex` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Random List Item」） | —（无同名 native 替代） |
| 97 | `GameNodeGetScene` | `NLGetScene` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Scene」） | —（无同名 native 替代） |
| 98 | `GameNodeSensorPositive` | `NLGetSensorNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Sensor Positive」） | —（无同名 native 替代） |
| 99 | `GameNodeGetSound` | `NLGetSound` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Sound」） | —（无同名 native 替代） |
| 100 | `GameNodeVRController` | `NLGetVRControllerValues` | legacy 且 `runtime_type_id` 为空（原菜单标签「VR Controller」） | —（无同名 native 替代） |
| 101 | `GameNodeVRHeadset` | `NLGetVRHeadsetValues` | legacy 且 `runtime_type_id` 为空（原菜单标签「VR Headset」） | —（无同名 native 替代） |
| 102 | `GameNodeDictEmpty` | `NLInitEmptyDict` | legacy 且 `runtime_type_id` 为空（原菜单标签「New Dictionary」） | —（无同名 native 替代） |
| 103 | `GameNodeListEmpty` | `NLInitEmptyList` | legacy 且 `runtime_type_id` 为空（原菜单标签「New List」） | —（无同名 native 替代） |
| 104 | `GameNodeDictNew` | `NLInitNewDict` | legacy 且 `runtime_type_id` 为空（原菜单标签「Dictionary From Item」） | —（无同名 native 替代） |
| 105 | `GameNodeInStream` | `NLKeyLoggerAction` | legacy 且 `runtime_type_id` 为空（原菜单标签「Key Logger」） | —（无同名 native 替代） |
| 106 | `GameNodeKeyboardActive` | `NLKeyboardActive` | legacy 且 `runtime_type_id` 为空（原菜单标签「Keyboard Active」） | —（无同名 native 替代） |
| 107 | `GameNodeLoadFileContent` | `NLLoadFileContent` | legacy 且 `runtime_type_id` 为空（原菜单标签「Load File Content」） | —（无同名 native 替代） |
| 108 | `GameNodeLightMakeUnique` | `NLMakeUniqueLight` | legacy 且 `runtime_type_id` 为空（原菜单标签「Make Light Unique」） | —（无同名 native 替代） |
| 109 | `GameNodeGetObjectAttr` | `NLObjectAttributeParameterNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Attribute」） | —（无同名 native 替代） |
| 110 | `GameNodeAnimationStatus` | `NLParameterActionStatus` | legacy 且 `runtime_type_id` 为空（原菜单标签「Animation Status」） | —（无同名 native 替代） |
| 111 | `GameNodeGetAxisVector` | `NLParameterAxisVector` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Axis Vector」） | —（无同名 native 替代） |
| 112 | `GameNodeEuler` | `NLParameterEulerSimpleNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Euler」） | —（无同名 native 替代） |
| 113 | `GameNodeXYZtoMatrix` | `NLParameterEulerToMatrixNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「XYZ To Matrix」） | —（无同名 native 替代） |
| 114 | `GameNodeFilePath` | `NLParameterFileValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「File Path」） | —（无同名 native 替代） |
| 115 | `GameNodeChildByIndex` | `NLParameterFindChildByIndexNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Child By Index」） | —（无同名 native 替代） |
| 116 | `GameNodeChildByName` | `NLParameterFindChildByNameNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Child By Name」） | —（无同名 native 替代） |
| 117 | `GameNodeFormattedString` | `NLParameterFormattedString` | legacy 且 `runtime_type_id` 为空（原菜单标签「Formatted String」） | —（无同名 native 替代） |
| 118 | `GameNodePythonInstanceAttr` | `NLParameterGetAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Instance Attribute」） | —（无同名 native 替代） |
| 119 | `GameNodeGetGlobalValue` | `NLParameterGetGlobalValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Global Property」） | —（无同名 native 替代） |
| 120 | `GameNodeKeyCode` | `NLParameterKeyboardKeyCode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Key Code」） | —（无同名 native 替代） |
| 121 | `GameNodeMatrixToXYZ` | `NLParameterMatrixToEulerNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Matrix To XYZ」） | —（无同名 native 替代） |
| 122 | `GameNodeRunPython` | `NLParameterPythonModuleFunction` | legacy 且 `runtime_type_id` 为空（原菜单标签「Run Python Code」） | —（无同名 native 替代） |
| 123 | `GameNodeColorRGBA` | `NLParameterRGBANode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Color RGBA」） | —（无同名 native 替代） |
| 124 | `GameNodeColorRGB` | `NLParameterRGBNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Color RGB」） | —（无同名 native 替代） |
| 125 | `GameNodeScreenPosition` | `NLParameterScreenPosition` | legacy 且 `runtime_type_id` 为空（原菜单标签「World To Screen」） | —（无同名 native 替代） |
| 126 | `GameNodeSetPythonInstanceAttr` | `NLParameterSetAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Object Attribute」） | —（无同名 native 替代） |
| 127 | `GameNodeLogicBranch` | `NLParameterSwitchValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Branch」） | `GameNodeBranch` |
| 128 | `GameNodeTypecast` | `NLParameterTypeCast` | legacy 且 `runtime_type_id` 为空（原菜单标签「Typecast Value」） | —（无同名 native 替代） |
| 129 | `GameNodeCombineXY` | `NLParameterVector2SimpleNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Combine XY」） | —（无同名 native 替代） |
| 130 | `GameNodeSeparateXY` | `NLParameterVector2SplitNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Separate XY」） | —（无同名 native 替代） |
| 131 | `GameNodeCombineXYZW` | `NLParameterVector4SimpleNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Combine XYZW」） | —（无同名 native 替代） |
| 132 | `GameNodePlaySequence` | `NLPlayMaterialSequence` | legacy 且 `runtime_type_id` 为空（原菜单标签「Play Sequence」） | —（无同名 native 替代） |
| 133 | `GameNodePlaySpeaker` | `NLPlaySpeaker` | legacy 且 `runtime_type_id` 为空（原菜单标签「Start Speaker」） | —（无同名 native 替代） |
| 134 | `GameNodeRangedThreshold` | `NLRangedThresholdNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Ranged Threshold」） | —（无同名 native 替代） |
| 135 | `GameNodeRemoveFilter` | `NLRemoveFilter` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Filter」） | —（无同名 native 替代） |
| 136 | `GameNodeListRemoveIndex` | `NLRemoveListIndex` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Index」） | —（无同名 native 替代） |
| 137 | `GameNodeListRemoveValue` | `NLRemoveListValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Value」） | —（无同名 native 替代） |
| 138 | `GameNodeRemoveOverlayCollection` | `NLRemoveOverlayCollection` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Overlay Collection」） | —（无同名 native 替代） |
| 139 | `GameNodeSensorValue` | `NLSensorValueNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Get Sensor Value」） | —（无同名 native 替代） |
| 140 | `GameNodeCharacterSetMaxJumps` | `NLSetActionCharacterJump` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Max Jumps」） | —（无同名 native 替代） |
| 141 | `GameNodeSetActuatorValue` | `NLSetActuatorValueNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Actuator Value」） | —（无同名 native 替代） |
| 142 | `GameNodeSetBoneConstraintAttr` | `NLSetBoneConstraintAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Attribute」） | —（无同名 native 替代） |
| 143 | `GameNodeSetBoneConstraintInfluence` | `NLSetBoneConstraintInfluence` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Influence」） | —（无同名 native 替代） |
| 144 | `GameNodeSetBoneConstraintTarget` | `NLSetBoneConstraintTarget` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Target」） | —（无同名 native 替代） |
| 145 | `GameNodeSetCurvePoints` | `NLSetCurvePoints` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Curve Points」） | —（无同名 native 替代） |
| 146 | `GameNodeDictionaryRemoveKey` | `NLSetDictDelKey` | legacy 且 `runtime_type_id` 为空（原菜单标签「Remove Dictionary Key」） | —（无同名 native 替代） |
| 147 | `GameNodeDictionarySetKey` | `NLSetDictKeyValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Dictionary Key」） | —（无同名 native 替代） |
| 148 | `GameNodeSetFilterState` | `NLSetFilterState` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Filter State」） | —（无同名 native 替代） |
| 149 | `GameNodeSetGeometryNodeProperty` | `NLSetGeometryNodeAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Node Value」） | —（无同名 native 替代） |
| 150 | `GameNodeSetGeometrySocket` | `NLSetGeometryNodeValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Socket」） | —（无同名 native 替代） |
| 151 | `GameNodeSetLightColor` | `NLSetLightColorAction` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Light Color」） | —（无同名 native 替代） |
| 152 | `GameNodeSetLightEnergy` | `NLSetLightEnergyAction` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Light Power」） | —（无同名 native 替代） |
| 153 | `GameNodeSetLightShadow` | `NLSetLightShadowAction` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Light Shadow」） | —（无同名 native 替代） |
| 154 | `GameNodeListSetIndex` | `NLSetListIndex` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set List Index」） | —（无同名 native 替代） |
| 155 | `GameNodeSetMaterialNodeProperty` | `NLSetMaterialNodeAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Node Value」） | —（无同名 native 替代） |
| 156 | `GameNodeSetMaterialSocket` | `NLSetMaterialNodeValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Socket」） | —（无同名 native 替代） |
| 157 | `GameNodeSetNodeGroupNodeProperty` | `NLSetNodeTreeNodeAttribute` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Node Value」） | —（无同名 native 替代） |
| 158 | `GameNodeSetNodeGroupSocket` | `NLSetNodeTreeNodeValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Socket」） | —（无同名 native 替代） |
| 159 | `GameNodeSetObjectAttr` | `NLSetObjectAttributeActionNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Attribute」） | —（无同名 native 替代） |
| 160 | `GameNodeSetOverlayCollection` | `NLSetOverlayCollection` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Overlay Collection」） | —（无同名 native 替代） |
| 161 | `GameNodeShowProfile` | `NLSetProfile` | legacy 且 `runtime_type_id` 为空（原菜单标签「Show Profile」） | —（无同名 native 替代） |
| 162 | `GameNodeSetScene` | `NLSetScene` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Scene」） | —（无同名 native 替代） |
| 163 | `GameNodeSetSensorValue` | `NLSetSensorValueNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Sensor Value」） | —（无同名 native 替代） |
| 164 | `GameNodeShowFramerate` | `NLShowFramerate` | legacy 且 `runtime_type_id` 为空（原菜单标签「Show Framerate」） | —（无同名 native 替代） |
| 165 | `GameNodeSlowFollow` | `NLSlowFollow` | legacy 且 `runtime_type_id` 为空（原菜单标签「Slow Follow」） | —（无同名 native 替代） |
| 166 | `GameNodeStartLogicTree` | `NLStartLogicNetworkActionNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Start Logic Tree」） | —（无同名 native 替代） |
| 167 | `GameNodeStopLogicTree` | `NLStopLogicNetworkActionNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Stop Logic Tree」） | —（无同名 native 替代） |
| 168 | `GameNodeStoreValue` | `NLStoreValue` | legacy 且 `runtime_type_id` 为空（原菜单标签「Store Value」） | —（无同名 native 替代） |
| 169 | `GameNodeToggleFilter` | `NLToggleFilter` | legacy 且 `runtime_type_id` 为空（原菜单标签「Toggle Filter」） | —（无同名 native 替代） |
| 170 | `GameNodeToggleProperty` | `NLToggleGameObjectGamePropertyActionNode` | legacy 且 `runtime_type_id` 为空（原菜单标签「Toggle Object Property」） | —（无同名 native 替代） |
| 171 | `GameNodeValueSwitchList` | `NLValueSwitchList` | legacy 且 `runtime_type_id` 为空（原菜单标签「Value Switch List」） | —（无同名 native 替代） |
| 172 | `GameNodeValueSwitchCompare` | `NLValueSwitchListCompare` | legacy 且 `runtime_type_id` 为空（原菜单标签「Value Switch List Compare」） | —（无同名 native 替代） |
| 173 | `GameNodeVehicleBrake` | `NLVehicleApplyBraking` | legacy 且 `runtime_type_id` 为空（原菜单标签「Brake」） | —（无同名 native 替代） |
| 174 | `GameNodeVehicleAccelerate` | `NLVehicleApplyEngineForce` | legacy 且 `runtime_type_id` 为空（原菜单标签「Accelerate」） | —（无同名 native 替代） |
| 175 | `GameNodeVehicleSteer` | `NLVehicleApplySteering` | legacy 且 `runtime_type_id` 为空（原菜单标签「Steer」） | —（无同名 native 替代） |
| 176 | `LoigcNodeVehicleSetAttributes` | `NLVehicleSetAttributes` | legacy 且 `runtime_type_id` 为空（原菜单标签「Set Vehicle Attributes」） | —（无同名 native 替代） |


### 6.2 旧部署 `ir_export` 只物化 Float/Bool 的受影响面（2026-11-07）

旧部署副本的 `materialize_constants` 只识别 Bool/Float；因此任何「输入端口不连线、靠控件默认值填值」
的 String / Int / Vector 节点，导出的 IR 都会缺少对应常量（Print 的 `ConstString{"E"}` 丢失即此原因）。
机器枚举结果：**153 个节点类型 / 255 个输入端口**（String 126 / Int 75 / Vector 54）。

源码与部署副本现均已同步的 `_CONSTANT_NODE_TYPES` 覆盖 Bool / Float / Int / String / Vector 五类；
Print 已用 `blenderplayer` 实跑拿到 `actual=E expected=E`。下列节点走同一物化路径，**无需再逐项修改**。

| # | 类名 | `bl_idname` | `runtime_type_id` | 受影响输入端口（标签） |
|---|---|---|---|---|
| 1 | `GameNodeObjectByName` | `LogicNodeObjectByName` | `ObjectByNameLegacy` | Name（String） |
| 2 | `GameNodeSimpleValue` | `LogicNodeSimpleValue` | `SimpleValueLegacy` | String（String） |
| 3 | `GameNodePrint` | `NLActionPrint` | `Print` | Value（String） |
| 4 | `GameNodeSendObjectMessage` | `NLActionSendMessage` | `SendMessage` | Subject（String）, Body（String） |
| 5 | `GameNodeModifyProperty` | `NLAddToGameObjectGamePropertyActionNode` | `ModifyPropertyLegacy` | Property（String） |
| 6 | `GameNodeEvaluateProperty` | `NLObjectPropertyOperator` | `EvaluatePropertyLegacy` | Property（String） |
| 7 | `GameNodeReceiveEvent` | `NLParameterReceiveMessage` | `MessageReceivedLegacy` | Subject（String） |
| 8 | `GameNodeSetMaterial` | `NLSetMaterial` | `SetMaterialLegacy` | Material（String） |
| 9 | `GameNodeActionDone` | `GameNodeActionDone` | `ActionDone` | Layer（Int） |
| 10 | `GameNodeAddAnimationEvent` | `GameNodeAddAnimationEvent` | `AddAnimationEvent` | Action（String）, Frame（Int） |
| 11 | `GameNodeAddJoint` | `GameNodeAddJoint` | `AddJoint` | Pivot（Vector）, Axis（Vector） |
| 12 | `GameNodeAddObject` | `GameNodeAddObject` | `AddObject` | Name（String） |
| 13 | `GameNodeAlignAxisToVector` | `GameNodeAlignAxisToVector` | `AlignAxisToVector` | Axis（Vector）, Target（Vector） |
| 14 | `GameNodeApplyForce` | `GameNodeApplyForce` | `ApplyForce` | Force（Vector）, Point（Vector） |
| 15 | `GameNodeApplyImpulse` | `GameNodeApplyImpulse` | `ApplyImpulse` | Impulse（Vector）, Point（Vector） |
| 16 | `GameNodeApplyTorque` | `GameNodeApplyTorque` | `ApplyTorque` | Torque（Vector） |
| 17 | `GameNodeAreaSensor` | `GameNodeAreaSensor` | `AreaSensor` | AreaName（String） |
| 18 | `GameNodeAudioZone` | `GameNodeAudioZone` | `AudioZone` | ZoneName（String）, Sound（String） |
| 19 | `GameNodeBlendActions` | `GameNodeBlendActions` | `BlendActions` | ActionA（String）, ActionB（String） |
| 20 | `GameNodeBroadcastMessage` | `GameNodeBroadcastMessage` | `BroadcastMessage` | Data（String） |
| 21 | `GameNodeClampValue` | `GameNodeClampValue` | `ClampValue` | Range（Vector） |
| 22 | `GameNodeColorFromVector` | `GameNodeColorFromVector` | `ColorFromVector` | Vector（Vector） |
| 23 | `GameNodeCreateDirectory` | `GameNodeCreateDirectory` | `CreateDirectory` | Path（String） |
| 24 | `GameNodeCreateListView` | `GameNodeCreateListView` | `CreateListView` | Parent（Int）, Pos（Vector）, Size（Vector） |
| 25 | `GameNodeCreateScrollView` | `GameNodeCreateScrollView` | `CreateScrollView` | Parent（Int）, Pos（Vector）, Size（Vector） |
| 26 | `GameNodeCreateTabView` | `GameNodeCreateTabView` | `CreateTabView` | Parent（Int）, Pos（Vector）, Size（Vector） |
| 27 | `GameNodeCreateUIButton` | `GameNodeCreateUIButton` | `CreateUIButton` | Parent（Int）, （String）, （String）, Border Width（Int）, Text（String）, Text Position（Vector）, （String）, Font Size（Int） |
| 28 | `GameNodeCreateUIImage` | `GameNodeCreateUIImage` | `CreateUIImage` | Parent（Int）, （String）, （String）, （String） |
| 29 | `GameNodeCreateUILabel` | `GameNodeCreateUILabel` | `CreateUILabel` | Parent（Int）, （String）, Text（String）, （String）, Font Size（Int）, Shadow Offset（Vector） |
| 30 | `GameNodeCreateUILayout` | `GameNodeCreateUILayout` | `CreateUILayout` | Parent（Int）, （Vector）, （Vector）, Border Width（Int）, Spacing（Int）, Rows（Int）, Columns（Int） |
| 31 | `GameNodeCreateUISliderWidget` | `GameNodeCreateUISliderWidget` | `CreateUISliderWidget` | Parent（Int）, （Vector）, （Vector）, Bar Texture（String）, Border Width（Int）, Knob Texture（String）, Steps（Int） |
| 32 | `GameNodeCreateVehicle` | `GameNodeCreateVehicle` | `CreateVehicle` | Template（String） |
| 33 | `GameNodeDecryptData` | `GameNodeDecryptData` | `DecryptData` | Encrypted（String）, Key（String） |
| 34 | `GameNodeDeleteFile` | `GameNodeDeleteFile` | `DeleteFile` | Path（String） |
| 35 | `GameNodeDiscoverRooms` | `GameNodeDiscoverRooms` | `DiscoverRooms` | Port（Int）, TimeoutMs（Int） |
| 36 | `GameNodeEncryptData` | `GameNodeEncryptData` | `EncryptData` | Data（String）, Key（String） |
| 37 | `GameNodeFileExists` | `GameNodeFileExists` | `FileExists` | Path（String） |
| 38 | `GameNodeFileReadChunk` | `GameNodeFileReadChunk` | `FileReadChunk` | Path（String）, Offset（Int） |
| 39 | `GameNodeFileReadLines` | `GameNodeFileReadLines` | `FileReadLines` | Path（String） |
| 40 | `GameNodeFileWriteChunk` | `GameNodeFileWriteChunk` | `FileWriteChunk` | Path（String）, Content（String） |
| 41 | `GameNodeGetActionFrame` | `GameNodeGetActionFrame` | `GetActionFrame` | Layer（Int） |
| 42 | `GameNodeGetAnimationFrame` | `GameNodeGetAnimationFrame` | `GetAnimationFrame` | Layer（Int） |
| 43 | `GameNodeGetInputValue` | `GameNodeGetInputValue` | `GetInputValue` | ActionName（String） |
| 44 | `GameNodeGetPlayerState` | `GameNodeGetPlayerState` | `GetPlayerState` | PlayerId（Int） |
| 45 | `GameNodeGetRemoteProperty` | `GameNodeGetRemoteProperty` | `GetRemoteProperty` | PlayerId（Int）, Key（String） |
| 46 | `GameNodeGetUIWidgetAttr` | `GameNodeGetUIWidgetAttr` | `GetUIWidgetAttr` | Widget（Int） |
| 47 | `GameNodeHostRoom` | `GameNodeHostRoom` | `HostRoom` | Port（Int）, MaxPlayers（Int） |
| 48 | `GameNodeInputMap` | `GameNodeInputMap` | `InputMap` | ActionName（String）, Binding（String） |
| 49 | `GameNodeJoinRoom` | `GameNodeJoinRoom` | `JoinRoom` | Host（String）, Port（Int） |
| 50 | `GameNodeLineOfSight` | `GameNodeLineOfSight` | `LineOfSight` | IgnoreMask（Int） |
| 51 | `GameNodeListDirectory` | `GameNodeListDirectory` | `ListDirectory` | Path（String） |
| 52 | `GameNodeListFilter` | `GameNodeListFilter` | `ListFilter` | Predicate（String） |
| 53 | `GameNodeListGet` | `GameNodeListGet` | `ListGet` | Index（Int） |
| 54 | `GameNodeListMap` | `GameNodeListMap` | `ListMap` | Expression（String） |
| 55 | `GameNodeListRemove` | `GameNodeListRemove` | `ListRemove` | Index（Int） |
| 56 | `GameNodeListRooms` | `GameNodeListRooms` | `ListRooms` | Host（String）, Port（Int） |
| 57 | `GameNodeListSet` | `GameNodeListSet` | `ListSet` | Index（Int） |
| 58 | `GameNodeListSlice` | `GameNodeListSlice` | `ListSlice` | Start（Int）, End（Int） |
| 59 | `GameNodeLoadGame` | `GameNodeLoadGame` | `LoadGame` | Slot（String） |
| 60 | `GameNodeLoadScene` | `GameNodeLoadScene` | `LoadScene` | Scene（String） |
| 61 | `GameNodeLoadSceneAsync` | `GameNodeLoadSceneAsync` | `LoadSceneAsync` | SceneName（String） |
| 62 | `GameNodeMessageReceived` | `GameNodeMessageReceived` | `MessageReceived` | Subject（String） |
| 63 | `GameNodeMountChildScene` | `GameNodeMountChildScene` | `MountChildScene` | ParentScene（String）, ChildScene（String） |
| 64 | `GameNodeMoveTo` | `GameNodeMoveTo` | `MoveTo` | Target（Vector） |
| 65 | `GameNodeMoveWidget` | `GameNodeMoveWidget` | `MoveWidget` | Widget（Int） |
| 66 | `GameNodeObjectPropertyOperator` | `GameNodeObjectPropertyOperator` | `ObjectPropertyOperator` | Property（String）, Op（String） |
| 67 | `GameNodeOnRPC` | `GameNodeOnRPC` | `OnRPC` | Subject（String） |
| 68 | `GameNodePlayAction` | `GameNodePlayAction` | `PlayAction` | Action（String）, Layer（Int） |
| 69 | `GameNodePlayActionByLayer` | `GameNodePlayActionByLayer` | `PlayActionByLayer` | Action（String）, Layer（Int） |
| 70 | `GameNodePlayActionByName` | `GameNodePlayActionByName` | `PlayActionByName` | ObjectName（String）, Action（String）, Layer（Int） |
| 71 | `GameNodePlayBoneAction` | `GameNodePlayBoneAction` | `PlayBoneAction` | Bone（String）, Action（String） |
| 72 | `GameNodePlaySound3D` | `GameNodePlaySound3D` | `PlaySound3D` | Sound（String）, Position（Vector） |
| 73 | `GameNodePlaySoundByName` | `GameNodePlaySoundByName` | `PlaySoundByName` | Name（String） |
| 74 | `GameNodePlaySoundPlaylist` | `GameNodePlaySoundPlaylist` | `PlaySoundPlaylist` | Mode（Int）, Seed（Int） |
| 75 | `GameNodePrint` | `GameNodePrint` | `Print` | Value（String） |
| 76 | `GameNodeQueryPath` | `GameNodeQueryPath` | `QueryPath` | From（Vector）, To（Vector） |
| 77 | `GameNodeRadar` | `GameNodeRadar` | `Radar` | Axis（Vector）, Property（String） |
| 78 | `GameNodeRaycastAll` | `GameNodeRaycastAll` | `RaycastAll` | Direction（Vector） |
| 79 | `GameNodeRaycastSensor` | `GameNodeRaycastSensor` | `RaycastSensor` | Direction（Vector） |
| 80 | `GameNodeReadFile` | `GameNodeReadFile` | `ReadFile` | Path（String） |
| 81 | `GameNodeRefreshRooms` | `GameNodeRefreshRooms` | `RefreshRooms` | Host（String）, Port（Int） |
| 82 | `GameNodeRemoveDirectory` | `GameNodeRemoveDirectory` | `RemoveDirectory` | Path（String） |
| 83 | `GameNodeRotateTo` | `GameNodeRotateTo` | `RotateTo` | Target（Vector） |
| 84 | `GameNodeSaveGame` | `GameNodeSaveGame` | `SaveGame` | Slot（String） |
| 85 | `GameNodeSaveGameBinary` | `GameNodeSaveGameBinary` | `SaveGameBinary` | Slot（String） |
| 86 | `GameNodeSendDelayedMessage` | `GameNodeSendDelayedMessage` | `SendDelayedMessage` | Subject（String）, Payload（String） |
| 87 | `GameNodeSendMessage` | `GameNodeSendMessage` | `SendMessage` | Subject（String）, Body（String） |
| 88 | `GameNodeSendNetworkMessage` | `GameNodeSendNetworkMessage` | `SendNetworkMessage` | TargetId（Int）, Data（String） |
| 89 | `GameNodeSendRPC` | `GameNodeSendRPC` | `SendRPC` | Subject（String）, Payload（String） |
| 90 | `GameNodeSet3DListener` | `GameNodeSet3DListener` | `Set3DListener` | Position（Vector）, Orientation（Vector） |
| 91 | `GameNodeSetActionFrame` | `GameNodeSetActionFrame` | `SetActionFrame` | Layer（Int） |
| 92 | `GameNodeSetActionSpeed` | `GameNodeSetActionSpeed` | `SetActionSpeed` | Layer（Int） |
| 93 | `GameNodeSetAnimationSpeed` | `GameNodeSetAnimationSpeed` | `SetAnimationSpeed` | Layer（Int） |
| 94 | `GameNodeSetAudioAttenuation` | `GameNodeSetAudioAttenuation` | `SetAudioAttenuation` | Model（Int） |
| 95 | `GameNodeSetAudioBusVolume` | `GameNodeSetAudioBusVolume` | `SetAudioBusVolume` | Bus（String） |
| 96 | `GameNodeSetBoneTransform` | `GameNodeSetBoneTransform` | `SetBoneTransform` | Bone（String）, Position（Vector）, Rotation（Vector） |
| 97 | `GameNodeSetBoneWeight` | `GameNodeSetBoneWeight` | `SetBoneWeight` | Bone（String） |
| 98 | `GameNodeSetCollisionGroup` | `GameNodeSetCollisionGroup` | `SetCollisionGroup` | Group（Int）, Mask（Int） |
| 99 | `GameNodeSetCollisionMask` | `GameNodeSetCollisionMask` | `SetCollisionMask` | Mask（Int） |
| 100 | `GameNodeSetFontText` | `GameNodeSetFontText` | `SetFontText` | Text（String） |
| 101 | `GameNodeSetGravity` | `GameNodeSetGravity` | `SetGravity` | Gravity（Vector） |
| 102 | `GameNodeSetLayerWeight` | `GameNodeSetLayerWeight` | `SetLayerWeight` | Layer（Int） |
| 103 | `GameNodeSetLinearVelocity` | `GameNodeSetLinearVelocity` | `SetLinearVelocity` | Velocity（Vector） |
| 104 | `GameNodeSetMaterial` | `GameNodeSetMaterial` | `SetMaterial` | Material（String） |
| 105 | `GameNodeSetMousePosition` | `GameNodeSetMousePosition` | `SetMousePosition` | Position（Vector） |
| 106 | `GameNodeSetPosition` | `GameNodeSetPosition` | `SetPosition` | Value（Vector） |
| 107 | `GameNodeSetResolution` | `GameNodeSetResolution` | `SetResolution` | Width（Int）, Height（Int） |
| 108 | `GameNodeSetRotation` | `GameNodeSetRotation` | `SetRotation` | Value（Vector） |
| 109 | `GameNodeSetScale` | `GameNodeSetScale` | `SetScale` | Value（Vector） |
| 110 | `GameNodeSetSceneTimeScale` | `GameNodeSetSceneTimeScale` | `SetSceneTimeScale` | SceneName（String） |
| 111 | `GameNodeSetSoundPitch` | `GameNodeSetSoundPitch` | `SetSoundPitch` | Handle（Int） |
| 112 | `GameNodeSetSoundVolume` | `GameNodeSetSoundVolume` | `SetSoundVolume` | Handle（Int） |
| 113 | `GameNodeSetUIWidgetAttr` | `GameNodeSetUIWidgetAttr` | `SetUIWidgetAttr` | Widget（Int） |
| 114 | `GameNodeSoundFinished` | `GameNodeSoundFinished` | `SoundFinished` | Handle（Int） |
| 115 | `GameNodeStartSound` | `GameNodeStartSound` | `StartSound` | File（String） |
| 116 | `GameNodeStopAction` | `GameNodeStopAction` | `StopAction` | Layer（Int） |
| 117 | `GameNodeStopSound` | `GameNodeStopSound` | `StopSound` | Handle（Int） |
| 118 | `GameNodeStringCompare` | `GameNodeStringCompare` | `StringCompare` | A（String）, B（String） |
| 119 | `GameNodeStringConcat` | `GameNodeStringConcat` | `StringConcat` | A（String）, B（String） |
| 120 | `GameNodeStringFind` | `GameNodeStringFind` | `StringFind` | Haystack（String）, Needle（String） |
| 121 | `GameNodeStringJoin` | `GameNodeStringJoin` | `StringJoin` | Separator（String） |
| 122 | `GameNodeStringLength` | `GameNodeStringLength` | `StringLength` | Text（String） |
| 123 | `GameNodeStringLengthChars` | `GameNodeStringLengthChars` | `StringLengthChars` | Source（String） |
| 124 | `GameNodeStringPad` | `GameNodeStringPad` | `StringPad` | Source（String）, Length（Int）, PadChar（String）, Side（Int） |
| 125 | `GameNodeStringPadChars` | `GameNodeStringPadChars` | `StringPadChars` | Source（String）, Length（Int）, PadChar（String）, Side（Int） |
| 126 | `GameNodeStringRegexMatch` | `GameNodeStringRegexMatch` | `StringRegexMatch` | Source（String）, Pattern（String） |
| 127 | `GameNodeStringRegexReplace` | `GameNodeStringRegexReplace` | `StringRegexReplace` | Source（String）, Pattern（String）, Replacement（String） |
| 128 | `GameNodeStringReplace` | `GameNodeStringReplace` | `StringReplace` | Text（String）, Find（String）, Replace（String） |
| 129 | `GameNodeStringSplit` | `GameNodeStringSplit` | `StringSplit` | Source（String）, Separator（String） |
| 130 | `GameNodeStringSubstr` | `GameNodeStringSubstr` | `StringSubstr` | Source（String）, Start（Int）, Length（Int） |
| 131 | `GameNodeStringToLower` | `GameNodeStringToLower` | `StringToLower` | Source（String） |
| 132 | `GameNodeStringToNumber` | `GameNodeStringToNumber` | `StringToNumber` | Source（String） |
| 133 | `GameNodeStringToUpper` | `GameNodeStringToUpper` | `StringToUpper` | Source（String） |
| 134 | `GameNodeStringTrim` | `GameNodeStringTrim` | `StringTrim` | Source（String）, Mode（Int） |
| 135 | `GameNodeSyncPlayerState` | `GameNodeSyncPlayerState` | `SyncPlayerState` | Position（Vector）, Rotation（Vector）, Animation（Int） |
| 136 | `GameNodeSyncProperty` | `GameNodeSyncProperty` | `SyncProperty` | Key（String） |
| 137 | `GameNodeTouch` | `GameNodeTouch` | `Touch` | Button（Int） |
| 138 | `GameNodeTouchGet` | `GameNodeTouchGet` | `TouchGet` | Index（Int） |
| 139 | `GameNodeTransitionScene` | `GameNodeTransitionScene` | `TransitionScene` | FromScene（String）, ToScene（String） |
| 140 | `GameNodeTranslate` | `GameNodeTranslate` | `Translate` | Value（Vector） |
| 141 | `GameNodeUIAnimation` | `GameNodeUIAnimation` | `UIAnimation` | Widget（Int）, Property（String） |
| 142 | `GameNodeUIScreenAnchor` | `GameNodeUIScreenAnchor` | `UIScreenAnchor` | Widget（Int） |
| 143 | `GameNodeUIScreenStack` | `GameNodeUIScreenStack` | `UIScreenStack` | ScreenName（String） |
| 144 | `GameNodeUnloadScene` | `GameNodeUnloadScene` | `UnloadScene` | SceneName（String） |
| 145 | `GameNodeUnmountChildScene` | `GameNodeUnmountChildScene` | `UnmountChildScene` | ParentScene（String）, ChildScene（String） |
| 146 | `GameNodeVectorAngle` | `GameNodeVectorAngle` | `VectorAngle` | A（Vector）, B（Vector） |
| 147 | `GameNodeVectorDistance` | `GameNodeVectorDistance` | `VectorDistance` | A（Vector）, B（Vector） |
| 148 | `GameNodeVectorProject` | `GameNodeVectorProject` | `VectorProject` | A（Vector）, B（Vector） |
| 149 | `GameNodeVectorReflect` | `GameNodeVectorReflect` | `VectorReflect` | A（Vector）, B（Vector） |
| 150 | `GameNodeVectorRotateToward` | `GameNodeVectorRotateToward` | `VectorRotateToward` | From（Vector）, To（Vector） |
| 151 | `GameNodeVehicleControl` | `GameNodeVehicleControl` | `VehicleControl` | Vehicle（Int） |
| 152 | `GameNodeVibrateGamepad` | `GameNodeVibrateGamepad` | `VibrateGamepad` | Pad（Int） |
| 153 | `GameNodeWriteFile` | `GameNodeWriteFile` | `WriteFile` | Path（String）, Content（String） |

## 7. 明确不做（65 条）

完整清单见 [DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)（57 条基线 + 本轮新增 8 条 = **65**；
Recast `#8` 已由 `KX_NavMeshObject::BuildNavMeshRuntime` 实现并移出，见该文件 §2/§3）。
工期口径：易 2~3 天、中 4~6 天、难 8~12 天；废弃域记「不可排期」。

| 代表条目 | 理由 | 若真要做 |
|---|---|---:|
| `StartSound`/`StopSound`（legacy） | Python 侧不暴露 `AUD_Handle`，桥接路径 A 不成立 | 5 天，与 StopSound 同批 |
| AES（`EncryptData`/`DecryptData` 的 `algorithm=aes`） | 无加密入口、未链接加密库 | 5 天（体积+许可证+交叉编译） |
| 混音总线（真 audaspace bus） | audaspace 无命名 bus 路由 | 8~10 天，收益为零 |
| Touch 真重叠（接触对象集合） | `KX_PhysicsAdapter` 无同步 `contactTest` | 6~8 天 |
| `SetBoneWeight` | `KX_GameObject` 无顶点组 API | 10~15 天 |

> 原第 6 行「Recast 光栅化」已移出：P-1 起 `BuildNavMesh` 走 `KX_NavMeshObject::BuildNavMeshRuntime`
> （完整 Recast → Detour 烘焙，判 `full`），不再列为「不做」。

## 8. 真实设备清单

依据 [REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md)：

- **verified：只有绿框（green box）**；
- 其余项目全部 **pending**（该文档由另一条线维护，本文只引用结论，不改写）。

## 9. 性能

**已填（D.3 最终值，2026-11-05）**。基准统一在 `build_clean_verify` 的
`9e15798a649a-clean_verify-3f074ee0eb1e` 上（100 节点混合链、1000 帧/轮，
**协议：5 进程 × 每进程 21 轮，取各进程中位数的中位**）：

| 路径 | ns / frame | ns / node |
|---|---:|---:|
| `direct-call`（control，无图，**不可与图路径比**） | 200.7 | 2.01 |
| `inline-off` | 2619.2 | 25.68 |
| `inline-on` | 2584.0 | 25.33 |

- **同协议配对**（vs A.4 重跑 `2577.9 / 2606.9`，同 5×21 协议）：**+0.5% / +0.2%**，
  几乎重合，**判定不退化**。
- **跨协议仅参考**（vs A.4 基线 `2624.4 / 2525.2`，3×21）：+3.7% / −1.5%，仍 <5%，
  但**不得与同协议值并列下结论**。
- **协议标签是硬要求（比数字本身重要）**：每个性能数字必须带
  「进程数 × 每进程轮数 × 统计量」（如 `5×21 / 中位数的中位`）。
  **只有协议完全相同的数字允许直接比较**；跨协议绝对数一律不进结论，只能作溯源。
  本轮的问题正是把 1×5 单次值与 5×21 中位值并列。
- 细节、口径映射与可重复命令：[BENCHMARK_FINAL.md](BENCHMARK_FINAL.md)；
  A.4 基线：[BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md)。
- 在统一二进制与固定构建产物之前，**更早的历史数字不可引用**——链接对象集合与编译参数
  不同（实测仅 `-fPIC` 一项就会带来约 20% 的绝对值差异），历史测量之间不可比。

内联收益（**按「量不到稳定收益」表述，不要写成「内联有 1–2% 收益」**）：

- **D.3 同协议区间：−3.9% ~ +2.08%**。三个诊断配置 A/B/C 分别为 **+1.34% / +1.78% / +2.08%**
  （正向），A.4 基线方向为 **−3.93%**（反向）；幅度全在 **±4%** 内，且**方向与 A.4 基线相反**，
  因此只能判「**量不到稳定收益**」。
- `build_linux` 上 100 个 `ConstFloat` 最轻图的 **3.49%**（1127.9→1088.5 ns/帧）是**另一个构建下的单图值**，
  按 [BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md) §3 规则 2 **不可跨构建比较**，
  不能与本节 −3.9% ~ +2.08% 混用，也不能读成「开内联普遍快 3.5%」。
- **40% 目标实测不可达**：要达到约 40% 需要**直线特化（linear specialization）**，
  不是再给 JIT 加优化 pass。不能用「JIT 已上线」暗示性能已解决。

## 10. 必记事实（11 条）

1. 能力域分母是 **286**（已注册节点），不是 legacy **290**；两套数不可换算。
2. 能力域主口径：14 域等权严格 **81.5%**；节点严格 **88.1%** / 加权 **93.4%** / 覆盖 **98.6%**。
3. 8 项音频/动画节点只到 `mock-verified`；真机确认后的上界是 **86.3% / 90.9% / 94.8% / 98.6%**。
4. **90 条 mock-only 已实测确认**（A 类 41 / B 类 49）：B 类 49 条中 **44 mock-driven /
   0 engine-driven / 5 未覆盖**，这 5 条现均已补齐、**20 条断言全 PASS**。只扣真正从 full
   跌落的 71 条，严格实算 `(252−71)/286 = 63.3%`，即**对外主口径 63.3%**（不再有「剔除未覆盖」档）。
   `(252−85)/286 = 58.4%` 与 `162/286 = 56.6%` 含 14 条 `approximate` 重复扣减，
   **仅作更保守下界、非实测**。历史情景 **73.8%** 已被取代，仅作阶段记录。
   所以 **88.1% 不是交付承诺**。说明见 [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §13.3。
5. legacy 三口径 **23.8% / 29.6% / 40.6%** 不可相加、不可换算；**290 全量的 70% 上限只有 54.5%**。
6. B6.1 的 23 节点是 **16/5/2**，即 **69.6% / 80.4% / 91.3%**；C.1.1 后物理已无 `unsupported`。
7. JIT 只做图内联；D.3 **同协议（5 进程 × 21 轮 × 1000 帧）**实测收益区间
   **−3.9% ~ +2.08%**（诊断 A/B/C 为 +1.34% / +1.78% / +2.08%；A.4 基线 −3.93% 反向），
   全在 **±4%** 内、方向与 A.4 相反，只能判「**量不到稳定收益**」。
   `build_linux` 最轻图 3.49% 属另一构建的单图值，**不可跨构建混用**，也不等于「普遍快 3.5%」。
   要 **40%** 必须做直线特化，仅靠 JIT 不可能。
8. 导航：Detour 查询 + 运行时 Recast 烘焙（P-1）已接上，`agent_radius`/`agent_height` 真实生效；
   但验证层级是烘焙管线层（`tests/c6_navmesh_bake.cpp`），「节点 → 真实引擎场景」仍需实机/场景 harness。
9. legacy `StartSound`/`StopSound` **永久不迁**（`AUD_Handle` 不暴露，Sound 桥路径 A 不成立）。
10. 真机当前只有**绿框 verified**，其余 pending；**性能基线已由 D.3 统一（BENCHMARK_FINAL.md），
    更早的历史数字一律不可引用**；引用任何性能数字必须带「进程数 × 每进程轮数 × 统计量」协议标签。
11. **断言计数的真实病根是「测试产出接口没定死」**：同一件事有 `RESULT assertions=N` /
    `INFO assertions=N` / `PASS` 行 / `PASS total failures=` / 手工 `PASS` 汇总句**五种表达**，
    导致「到底多 1 还是少 1」查了三轮、牵动四份文档。规则定为：**只认 `assertions=N` 声明**，
    并要求所有 harness 统一输出 `RESULT assertions=N failures=M`；**手工汇总句不计断言**。
    J 块 851 即按此规则复算（`822 − 46 − 36 + 55 + 35 + 20 − 17 + 18`，见 §5）；**K2 定稿全量为 1456 / 46 批**（`RUN_ID=20261107-112207`，见 `NUMBER_CONSISTENCY_AUDIT.md` B.5）。

## 11. 引用边界

- 本文只引用、不改写：
  [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)、[DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)、
  [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md)、[REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md)、
  [B6_NETWORK.md](B6_NETWORK.md)、[B6_UI_ADVANCED.md](B6_UI_ADVANCED.md)。
- 能力**分档规则与算法**的唯一依据是 [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md)；
  **对外引用的最终数值以本文 §0.0a 的 K2 六量/三式为准**；若两者冲突，先按 CAP 的规则重算，再回改本文。
- §9 性能节已由 D.3 填入（[BENCHMARK_FINAL.md](BENCHMARK_FINAL.md)），PENDING 解除；
  B6.8 独立验收文档已落地（[B6.8_ACCEPTANCE.md](B6.8_ACCEPTANCE.md)），**本文已无 PENDING 项**。

## 12. 文档索引（按用途）

| 用途 | 文档 |
|---|---|
| 唯一权威状态 | **本文（`PROJECT_STATUS.md`）** |
| 编号登记与六量 | [NUMBER_CONSISTENCY_AUDIT.md](NUMBER_CONSISTENCY_AUDIT.md)（附录 B.5 为 K2 权威表） |
| 能力分档与三口径 | [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md)、[KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) |
| 域内重评分 | [FLOW_MATH_STRING_RESCORE.md](FLOW_MATH_STRING_RESCORE.md)、[AUDIO_SENSOR_SCENE_RESCORE.md](AUDIO_SENSOR_SCENE_RESCORE.md)、[INPUT_RESCORE.md](INPUT_RESCORE.md)、[ANIMATION_RESCORE.md](ANIMATION_RESCORE.md)、[FILE_MSG_RESCORE.md](FILE_MSG_RESCORE.md)、[UI_RESCORE.md](UI_RESCORE.md) |
| 构建与回归 | [A1_BUILD_AUDIT.md](A1_BUILD_AUDIT.md)、[D1_REGRESSION.md](D1_REGRESSION.md)、[B6.8_ACCEPTANCE.md](B6.8_ACCEPTANCE.md) |
| 证据与溯源 | [EVIDENCE_PROVENANCE_AUDIT.md](EVIDENCE_PROVENANCE_AUDIT.md)、[VERIFICATION_LEVEL_AUDIT.md](VERIFICATION_LEVEL_AUDIT.md) |
| 不做的项 / legacy 口径 | [DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)、[LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md) |
| 真机验证 | [REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md) |
| 引擎侧取证 | [GAME_ENVIRONMENT_AUDIT.md](GAME_ENVIRONMENT_AUDIT.md) |
