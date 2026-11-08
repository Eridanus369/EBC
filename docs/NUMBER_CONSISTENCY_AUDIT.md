# NUMBER_CONSISTENCY_AUDIT — 全仓数字一致性交叉核对

- 日期：2026-11-05
- 性质：**只读审计**。未改任何既有文档、未改源码、未跑构建、未 commit/push、未碰 `build_linux`
- 范围：12 份文档（见 §1）
- 取证日志：`/tmp/number_audit.log`
- 本文档本身是本轮唯一新增文件；所有「建议改法」都只是**建议**，由各文档所有者执行

## 0. 结论（先看这里）

- 共抽出 **120 条量化断言记录**（同一指标在多份文档重复出现的已合并为一行，并在「位置」列列出全部出处）。
- 发现 **15 处不一致**：**P0 4 条**、**P1 11 条**。
- 另有 **6 组「都对但口径不同」**，不是错误，但必须补区分措辞（见 §4）。

**最严重的三条：**

1. **P0-1｜`PROJECT_STATUS.md` §1 把「82–84%」说成可用 14 域等权口径复现。** 事实是该口径的实测点是 **81.5%（主口径）/ 86.3%（上界）**，82–84 不是任何实测点；`CAPABILITY_WEIGHT_AUDIT.md` §7 已明说它只是「两端之间的插值印象」。这是对外能力陈述，必须改。
2. **P0-2｜`KNOWN_LIMITATIONS.md` §0.2 用「动画 4/8 = 50.0%」代表整个动画能力域。** 4/8 是 **B6.1 动画高级子集**；当前**动画域是 11 节点、10/11 = 90.9%**。两个分母混在同一行「按能力域」下，会把动画能力对外说低 40 个百分点。
3. **P0-3｜`B6_LEGACY_COMPAT.md` §6 仍把 `native_nodes=196`、`checked_pairs=71` 写成「本轮输出」。** 当时权威值是 **223 / 108**（`A2_TASK345_STATUS.md` §215–§216），196/71 只是 B6.5a 当时的快照，且没有时间戳。223 正是四个不可互换分母之一，引用错会连带算错覆盖率。**K 冻结（`RUN_ID=20261106-224926`）后为 `native_nodes=244` / `checked_pairs=125`（见 B.5）。**

**特别检查结论（性能节）：**

- `PROJECT_STATUS.md` §9 **没有**写入 `2624.4 / 2525.2 / 8458.1` 等 A.4 绝对数字，符合「PENDING 不引用基线数字」的要求。
- 全仓**没有**任何一处把「3.5% 天花板」和「3.9% 内联变慢」写成同一件事；但 §9 的「实测天花板约 3.5%」缺图/构建限定，存在被外部读成「JIT 普遍有 3.5% 收益」的风险（P0-4）。
- 两者方向相反、图不同、构建不同：3.5% 来自 `build_linux` 上 100 个 `ConstFloat` 最轻图（1127.9→1088.5 ns/帧，**变快**）；3.9% 来自 `build_clean_verify` 的 A.4 100 节点混合链（2624.4 vs 2525.2 ns/帧，**开启内联反而变慢**）。按 `BENCHMARK_BASELINE.md` §3 规则 2，二者**不可跨构建比较**。

## 1. 扫描范围与取证方式

| # | 文档 | 读取方式 |
|---|---|---|
| 1 | `PROJECT_STATUS.md` | 全文 |
| 2 | `CAPABILITY_WEIGHT_AUDIT.md` | §0–§12（含逐域表、复算公式、风险清单） |
| 3 | `KNOWN_LIMITATIONS.md` | §0–§6 |
| 4 | `DEFERRED_FOREVER.md` | §0/§1/§2/§5 |
| 5 | `LEGACY_COVERAGE_POLICY.md` | §0–§5 |
| 6 | `BENCHMARK_BASELINE.md` | 全文 |
| 7 | `B6_PHYSICS_ADVANCED.md` | §6/§7 |
| 8 | `B6_ANIMATION.md` | §6/§8 |
| 9 | `A1_BUILD_AUDIT.md` | §0–§12（含 17 个 harness 表） |
| 10 | `A2_TASK345_STATUS.md` | §断言表、§验证补齐结果 |
| 11 | `REAL_DEVICE_VERIFICATION.md` | 全文 |
| 12 | `B6_LEGACY_COMPAT.md` | §1/§4/§6/§8/§10 |

复核方法：文档数字 × 各自算式 × `/tmp` 原始日志三方对齐。日志计数规则统一为**扣除尾行汇总**（`PASS total failures=0` / `RESULT` 行不计入断言数）。

## 2. 量化断言全表

### 2.1 分母与规模（19 条）

| # | 指标名 | 值 | 位置 | 依据 |
|---|---|---|---:|
| 1 | 运行时已注册节点 | **286** | `PROJECT_STATUS` §1；`CAPABILITY_WEIGHT_AUDIT` §1/§4 合计；`KNOWN_LIMITATIONS` §0.1；`LEGACY_COVERAGE_POLICY` §1.4 | 14 内建 + 270 表驱动 + 2 特例 |
| 2 | 286 的分解 | **14 / 270 / 2** | `PROJECT_STATUS` §1；`CAPABILITY_WEIGHT_AUDIT` §1 | `KX_RuntimeNodes.cpp:201-250` |
| 3 | 编辑器可见节点 | **226** | `PROJECT_STATUS` §1；`CAPABILITY_WEIGHT_AUDIT` §1；`LEGACY_COVERAGE_POLICY` §1.5 | 编辑器 catalog 计数 |
| 4 | `port_contract` native 表节点（B6.5a 后、K 冻结前快照） | **223**（K 冻结后 **244**） | `PROJECT_STATUS` §1；`CAPABILITY_WEIGHT_AUDIT` §1；`LEGACY_COVERAGE_POLICY` §1.5；`A2_TASK345_STATUS` §215 | `port_contract.py` 输出 |
| 5 | `port_contract` native 表节点（B6.5a 快照） | **196** | `B6_LEGACY_COMPAT` §6 | `/tmp/b65a_port.log` |
| 6 | 枚举一致 `checked_pairs`（B6.5a 后、K 冻结前快照） | **108**（K 冻结后 **125**） | `A2_TASK345_STATUS` §216 | `enum_consistency.py` 输出 |
| 7 | 枚举一致 `checked_pairs`（B6.5a 快照） | **71** | `B6_LEGACY_COMPAT` §6 | B6.5a 本轮 stdout |
| 8 | legacy 全量 | **290** | `LEGACY_COVERAGE_POLICY` §0；`PROJECT_STATUS` §2；`KNOWN_LIMITATIONS` §0.2；`DEFERRED_FOREVER` §5 | `deferred_matrix.json` |
| 9 | 用户可感知 | **233** | 同上 + `DEFERRED_FOREVER` §5 | `290 − 57` |
| 10 | semantic | **360** | `LEGACY_COVERAGE_POLICY` §0/§5；`PROJECT_STATUS` §2；`KNOWN_LIMITATIONS` §0.2 | `283 + 77` |
| 11 | legacy 已迁移 | **69** | `LEGACY_COVERAGE_POLICY` §0；`B6_LEGACY_COMPAT` §1/§5 | `55 + 14` |
| 12 | legacy 起点基线 | **55 / 290 = 19.0%** | `B6_LEGACY_COMPAT` §1 | 委派给定，本轮复核 |
| 13 | FOREVER 基线 | **57** | `DEFERRED_FOREVER` §0；`KNOWN_LIMITATIONS` §0.1；`LEGACY_COVERAGE_POLICY` §1.2 | 渲染内件 40 + 废弃/VR/FMod 17 |
| 14 | FOREVER 本轮新增 | **8**（6 组） | `DEFERRED_FOREVER` §2；`KNOWN_LIMITATIONS` §0.1 | 逐条评估（#8 已实现移出） |
| 15 | FOREVER 合计 | **65** | `DEFERRED_FOREVER` §0；`PROJECT_STATUS` §7；`KNOWN_LIMITATIONS` §0.1 | `57 + 8` |
| 16 | 已知限制条数 | **24** | `KNOWN_LIMITATIONS` §0.3；`PROJECT_STATUS` §6 | 逐条编号 |
| 17 | 限制状态分布 | **20 active / 4 mitigated / 0 removed** | `KNOWN_LIMITATIONS` §0.3；`PROJECT_STATUS` §6 | 逐条统计 |
| 18 | 疑似 mock-only | **90（A 41 + B 49）** | `CAPABILITY_WEIGHT_AUDIT` §12.2；`PROJECT_STATUS` §4 | A：7+4+5+14+5+6；B：9+7+9+8+16 |
| 19 | 网络「跨机未验」另计 | **11** | `CAPABILITY_WEIGHT_AUDIT` §12.2；`PROJECT_STATUS` §4 | 网络域节点数，不计入 90 |

### 2.2 能力域核算（32 条）

**逐域（`CAPABILITY_WEIGHT_AUDIT` §4 表）**

| # | 指标名 | 值（n / full / mock / approx / unsup） | 位置 | 依据 |
|---|---|---|---:|---|
| 20 | 值计算 | 42 / 42 / 0 / 0 / 0 → 100.0% | CAP §4:172 | 纯进程内 |
| 21 | 动画 | 11 / 10 / 1 / 0 / 0 → 90.9 / 95.5 / 100 | CAP §4:173 | `ActionDone` 为 mock |
| 22 | 音频 | 12 / 1 / 7 / 4 / 0 → 8.3 / 54.2 / 100 | CAP §4:174 | `B6_AUDIO` §7/§9 |
| 23 | 输入 | 23 / 22 / 0 / 0 / 1 → 95.7 | CAP §4:175 | 引擎无触摸 |
| 24 | 字符串列表 | 34 / 29 / 0 / 5 / 0 → 85.3 / 92.6 / 100 | CAP §4:176 | `B6_STRING_LIST` §7 |
| 25 | 网络 | 11 / 11 / 0 / 0 / 0 → 100 | CAP §4:177 | LAN 为环境边界 |
| 26 | 传感器 | 8 / 6 / 0 / 2 / 0 → 75.0 / 87.5 / 100 | CAP §4:178 | AABB |
| 27 | 场景 | 9 / 8 / 0 / 0 / 1 → 88.9 | CAP §4:179 | RemoveScene 禁用 |
| 28 | UI | 15 / 13 / 0 / 2 / 0 → 86.7 / 93.3 / 100 | CAP §4:180 | `B6_UI_ADVANCED` |
| 29 | 文件 | 13 / 11 / 0 / 2 / 0 → 84.6 / 92.3 / 100 | CAP §4:181 | AES 未接入 |
| 30 | 物理（基础） | 16 / 16 / 0 / 0 / 0 → 100 | CAP §4:182 | core/builtin/Extra |
| 31 | 物理高级 | 14 / 11 / 0 / 3 / 0 → 78.6 / 89.3 / 100 | CAP §4:183 | 15 节点去掉复用 `SetGravity` |
| 32 | 动画高级 | 8 / 4 / 0 / 2 / 2 → 50.0 / 62.5 / 75.0 | CAP §4:184；`B6_ANIMATION` §6 | `B6_ANIMATION` §6 |
| 33 | 其他 | 70 / 68 / 0 / 2 / 0 → 97.1 / 98.6 / 100 | CAP §4:185 | `B6_LEGACY_COMPAT` §7–§8 |

**合计与派生**

| # | 指标名 | 值 | 位置 | 依据 |
|---|---|---|---:|---|
| 34 | 合计分档 | **252 full / 8 mock / 22 approx / 4 unsup / 0 unclass** | CAP §4:186；`LEGACY_COVERAGE_POLICY` §1.4 | 逐域求和 |
| 35 | 节点严格 | **252/286 = 88.1%** | CAP §0；KNOWN §0.1；LCP §1.4；PROJECT §1 | 同上 |
| 36 | 节点加权 | **267/286 = 93.4%** | 同上 | `252 + 4 + 11` |
| 37 | 节点覆盖 | **282/286 = 98.6%** | 同上 | `252 + 8 + 22` |
| 38 | 上界严格 | **260/286 = 90.9%** | CAP §0:20；PROJECT §1 | `252 + 8` |
| 39 | 上界加权 | **271/286 = 94.8%** | 同上 | `267 + 4` |
| 40 | 上界覆盖 | **282/286 = 98.6%** | 同上 | 不变 |
| 41 | 14 域等权·严格 | **81.5%（上界 86.3%）** | CAP §0:19–20；§5.2:279；PROJECT §1 | `1141.1/14` |
| 42 | 14 域等权·加权 | **89.3%（上界 91.7%）** | CAP §0:19–20；§5.2:290 | `1250.3/14` |
| 43 | 14 域等权·覆盖 | **97.1%（上界 97.1%）** | 同上 | `1359.6/14` |
| 44 | S2（再修 4 项 unsupported） | **264/286 = 92.3%**；加权 **275/286 = 96.2%**；覆盖 100% | CAP §6:335–339 | `260+4`、`271+4` |
| 45 | S2 域等权严格 | **89.2%** | CAP §6:339 | `86.3 + 1.8 + 0.7 + 0.4` |
| 46 | 风险下界（A 类 41 全改判） | **211/286 = 73.8%** | CAP §12.3；PROJECT §4 | `252 − 41` |
| 47 | 风险加权 | **246.5/286 = 86.2%** | 同上 | `267 − 20.5` |
| 48 | 风险覆盖 | **282/286 = 98.6%** | 同上 | 不变 |
| 49 | 「82–84%」 | **非实测点** | CAP §7:349–359；PROJECT §1:36 | 两端插值印象 |
| 50 | 音频上界 | **8/12 = 66.7%** | CAP §6:316 | 7 项真机确认 |
| 51 | 音频历史 | **25.0% / 41.7%**（旧） | CAP §11:490 | 改判前 |

### 2.3 B6.1 物理 / 动画 23 节点（11 条）

| # | 指标名 | 值 | 位置 | 依据 |
|---|---|---|---:|---|
| 52 | 物理节点数 | **15** | `B6_PHYSICS_ADVANCED` §6:139；PROJECT §3 | 逐项表 |
| 53 | 物理分档 | **12 full / 3 approx / 0 unsup** | 同 §6:139；PROJECT §3 | C.1.1 后 `SetPhysicsRestitution` 转 full |
| 54 | 物理三口径 | **80.0% / 90.0% / 100%** | 同 §6:145–147 | `12/15`、`13.5/15`、`15/15` |
| 55 | 动画节点数 | **8** | `B6_ANIMATION` §6:1；PROJECT §3 | B6.1 高级子集 |
| 56 | 动画分档 | **4 full / 2 approx / 2 unsup** | 同 §6；PROJECT §3 | 逐项表 |
| 57 | 动画三口径 | **50.0% / 62.5% / 75.0%** | 同 §6:95–97 | `4/8`、`5/8`、`6/8` |
| 58 | 23 节点合计 | **23 = 15 + 8** | PROJECT §3；`B6_ANIMATION` §6:93 | 含复用的 `SetGravity` |
| 59 | 23 节点分档 | **16 / 5 / 2** | 同 | `12+4`、`3+2`、`0+2` |
| 60 | 23 节点三口径 | **69.6% / 80.4% / 91.3%** | 同；KNOWN §0.2:18 | `16/23`、`18.5/23`、`21/23` |
| 61 | SetBoneTransform 备选口径 | **5/8 = 62.5%；5.5/8 = 68.8%** | `B6_ANIMATION` §6:101 | 用户允许的 full-with-caveat；本审计取保守 `approximate` |
| 62 | 旧「动画 85%」 | **已废弃** | `B6_ANIMATION` §6:101 | 任一公式均无法复现 |

### 2.4 Legacy 口径派生（12 条）

| # | 指标名 | 值 | 位置 | 依据 |
|---|---|---|---:|---|
| 63 | legacy 全量比例 | **69/290 = 23.8%** | LCP §0:11；PROJECT §2；KNOWN §0.2；B6_LEGACY §1 | 可复算 |
| 64 | 可感知比例 | **69/233 = 29.6%** | 同上 | 可复算 |
| 65 | semantic 比例 | **146/360 = 40.6%**（40.5556） | LCP §0:12/§5:202；PROJECT §2；KNOWN §0.2 | `native_audit.py` |
| 66 | 70% 理论上限 | **158/290 = 54.5%** | LCP §3；PROJECT §2 | `(69+89)/290` |
| 67 | 70% 目标条数 | **203** | LCP §3:98 | `0.70 × 290` |
| 68 | 距目标缺口 | **134 条** | LCP §3:98 | `203 − 69` |
| 69 | 可评估集合 | **233** | 同上 | 290 − 57 |
| 70 | 基础类可迁 | **89** | LCP §3.2 | 分解 `28+16+15+13+3=75` + 14 已迁 |
| 71 | 结构性缺口行 | **164** | LCP §3；B6_LEGACY §6:136 | `gap_rows` |
| 72 | 结构性子集 | **75**（pyobj 28 / phys 16 / file 15 / ui 13 / net 3） | LCP §3.2 | 求和校验通过 |
| 73 | 30 候选迁移率 | **16/30 = 53.3%** | LCP §3.5；B6_LEGACY §4 | 14 新 + 2 旧 |
| 74 | 外推收益 | **+47 条 = 116/290 ≈ 40.0%** | LCP §3.5:151 | `89 × 53% ≈ 47` |

### 2.5 验证层级 / 真机（10 条）

| # | 指标名 | 值 | 位置 | 依据 |
|---|---|---|---:|---|
| 75 | WorldPosition 反投影误差 | **1.679e-6** | `B6_LEGACY_COMPAT` §8:194；`A2_TASK345_STATUS` §185；PROJECT §5 | `blenderplayer` 真实相机 + 真实 `GPU_matrix_unproject_3fv`，容差 1e-4 |
| 76 | depth 0 误差 | **3.353e-8** | `A2_TASK345_STATUS` §185 | 同上 |
| 77 | 真机验收项 | **8 项：1 verified / 7 pending** | `REAL_DEVICE_VERIFICATION` §0:13–22；PROJECT §8 | 唯一 verified = 绿框 |
| 78 | 真机未修复问题 | **2 条**（`-11` 退出、截图为窗口内截图） | 同 §未修复问题:204 | 仅记录 |
| 79 | 房间/重连 E2E | **32 PASS / 0 FAIL** | PROJECT §5；日志重跑 | `/tmp/c32_rooms_e2e` 实测 32 条 PASS |
| 80 | UI C3/C.7 E2E | **19 PASS** | PROJECT §5 | `/tmp/a1_clean_run_b661_e2e.log`（polar 4/8 槽、slider 1/2/0、rich text 3 条） |
| 81 | B6.5a 行为 E2E | **29 条断言 + 汇总** | `B6_LEGACY_COMPAT` §6:135 | `/tmp/b65a_e2e.log`（30 PASS 行含汇总） |
| 82 | LogicGate 真值表 | **16 条**（4 op × 4 组合） | `B6_LEGACY_COMPAT` §4:72/§8:144 | 子集，不是全部 LogicGate 断言 |
| 83 | 音频断言 | **10 条** | `KNOWN_LIMITATIONS` #22 | `/tmp/b5_sound.log` 中 69 PASS 的子集 |
| 84 | 音频 E2E v4 | **69 PASS / 0 FAIL** | `B6_LEGACY_COMPAT` §10.1:240；`A1_BUILD_AUDIT` §11:202 | 含尾行汇总；断言 = 68 |

### 2.6 构建 / 性能（19 条）

| # | 指标名 | 值 | 位置 | 依据 |
|---|---|---|---:|---|
| 85 | clean 构建步骤 | **8442**，exit 0 | `A1_BUILD_AUDIT` §10 | `/tmp/a1_clean_build.log` |
| 86 | 旧 `bin/blender` 大小 | **232,729,384 B** | `A1_BUILD_AUDIT` §0/§1 | `build_linux` |
| 87 | 旧 `bin/blenderplayer` 大小 | **261,541,680 B** | `A1_BUILD_AUDIT` §1 | `build_linux` |
| 88 | clean `blender` 大小 | **232,781,176 B** | `A1_BUILD_AUDIT` §10 | `build_clean_verify` |
| 89 | clean `blenderplayer` 大小 | **261,597,568 B** | `A1_BUILD_AUDIT` §10 | `build_clean_verify` |
| 90 | clean 构建耗时 | **20:05:23 → 21:03:59 = 58m36s（-j4）** | `A1_BUILD_AUDIT` §10 | 日志时间戳 |
| 91 | A.4 native 中位 | **2624.4 ns/帧** | `BENCHMARK_BASELINE` §0:15 | 3 进程 × 21 轮 × 1000 帧 |
| 92 | A.4 inline_off 中位 | **2525.2 ns/帧** | 同 §0:16 | 同上 |
| 93 | A.4 python 中位 | **8458.1 ns/帧** | 同 §0:17 | `blender -b --python` |
| 94 | 内联开启相对关闭 | **慢 3.9%** | 同 §0:25 | `(2624.4−2525.2)/2525.2 = 3.93%` |
| 95 | Python 相对原生 | **3.2×** | 同 §0:26 | `8458.1/2624.4 = 3.223` |
| 96 | 基准规模 | **3 × 21 轮 / 1000 帧 / 100 节点** | 同 §0/§1 | 固定图 |
| 97 | JIT 天花板 | **约 3.5%**（实测 3.49%） | KNOWN #6:110；PROJECT §9:159/§10:173 | `/tmp/b662_jit.log`，`build_linux` |
| 98 | JIT 轻量图绝对变化 | **1127.9 → 1088.5 ns/帧** | KNOWN #6:110 | `(1127.9−1088.5)/1127.9 = 3.49%` |
| 99 | b661_perf（clean） | **312.376 µs/1000 帧**（原写 313.959，无落盘日志，已作废） | `A1_BUILD_AUDIT` §11:212；`/tmp/a1_clean_run_b661_perf.log:1` | clean 链接集 |
| 100 | b661_perf（旧） | **477.806 µs/1000 帧；历史 ~305 µs** | `A2_TASK345_STATUS` §173 | 不同链接集，不可比 |
| 101 | 性能断言容差 | **±5%**；`ratio_gate_vs_plain=1.0254`、`ratio_mixed_vs_plain=1.8618` | `A2_TASK345_STATUS` §184 | 临时基线，A.4 后需替换 |
| 102 | inspector 面板审计 | **284（§4）vs 287（§11.1/当前日志）** | `A1_BUILD_AUDIT` §4:81/§11.1:269 | `/tmp/b45_inspector.log` 当前 287 |
| 103 | palette 面板审计 | **150** | `A1_BUILD_AUDIT` §4:81/§11.1:269 | `/tmp/b45_palette.log` |

### 2.7 E2E 断言数（17 个 harness；下表为当时审计快照）

> **状态同步（2026-11-06，K 冻结）**：本节表格保留当时审计现场，不作覆盖。**K 冻结（`RUN_ID=20261106-224926`）：46 批、`TOTAL assertions=1456` / 0 失败 / 0 崩溃**。历史口径：`RUN_ID=20261106-101536` 为 957/29 行、J 块为 851/26 批，均已被冻结值覆盖。权威来源 `A1_BUILD_AUDIT.md` §11 与 `NUMBER_CONSISTENCY_AUDIT.md` B.5。

计数规则：**扣除尾行汇总**。A1 表头写「断言（PASS+FAIL）」，但实际把 `PASS total failures=0` 计入；A2 明确「PASS 行 − 1 汇总」。

| # | harness | A1 §11 | A2 §155–164 | 日志实测（扣汇总） | 判定 |
|---|---|---|---:|---:|---|
| 104 | `reg_b2_b3` | 15 | 15 | 15 | 一致 |
| 105 | `reg_b4_e2e` | 46 | 46 | 46 | 一致 |
| 106 | `p41_rebuild` | 23 | 23 | 23 | 一致 |
| 107 | `p42_hot_reload` | 9 | 9 | 9 | 一致 |
| 108 | `p43_trace` | 9 | 9 | 9 | 一致 |
| 109 | `p44_stability` | 36 | 36 | 36 | 一致 |
| 110 | `b61_e2e` | **27** | **23** | **26** | **不一致**：A1 +1；A2 旧版（少 3 条 `wheel_names`） |
| 111 | `b62_e2e` | **69** | **51** | **68** | **不一致**：A1 +1；A2 旧版（少 17 条 v4 音频） |
| 112 | `b63_nodes_e2e` | 36 | 36 | 36 | 一致 |
| 113 | `b63_network_e2e` | 17 | 17 | 17 | 一致 |
| 114 | `b64_e2e` | **62** | **61** | **61** | **不一致**：A1 +1 |
| 115 | `b65a_e2e` | **30** | **29** | **29** | **不一致**：A1 +1 |
| 116 | `b661_e2e` | 19 | 19 | 19 | 一致 |
| 117 | `b661_logicgate` | 22 | 22 | 22（补测后 **30**） | 时间线差异，非错误 |
| 118 | `b661_query_e2e` | 41 | 41 | 41 | 一致 |
| 119 | `b661_bridge` | **7** | **6** | **6** | **不一致**：A1 +1 |
| 120 | `b45_gui`（bpy） | 2 截图 + 审计 | 2 截图 + 审计 | 同 | 一致 |

> 任务书列出的 `33 / 20 / 55` 三个断言数在 12 份文档中**均无命中**，未计入本表；如需核对，应指明出处文档。

## 3. 冲突清单

> **修复状态（2026-11-05 追加）**：P0-1（`PROJECT_STATUS` §1 的 82–84% 措辞）与 P1-8（同文档 §11 的 PENDING 前提）已在 `PROJECT_STATUS.md` 修复，**§9 按所有者要求未动**。其余 P0/P1 仍待各自文档所有者处理；本清单保留原始判定，供复核时对照。

### 3.1 P0（影响对外结论，4 条）

#### P0-1｜`PROJECT_STATUS.md` §1 的「82–84% 可复现」说法错误

- **位置**：`PROJECT_STATUS.md:36`
- **现文**：「**「82–84%」只有 14 域等权口径可复现**：主口径 81.5%、上界 86.3%，82–84% 是这两端之间的旧区间取值……」
- **哪份对**：`CAPABILITY_WEIGHT_AUDIT.md` §7:359 —— 「82–84% 不是实测点，只是两端之间的插值印象，D.2 若只给一个数应用 81.5%」。
- **错在哪**：14 域等权口径的实测复现值是 **81.5%** 与 **86.3%**，没有任何一个是 82–84%。「可复现」与「插值印象」自相矛盾。
- **建议改法（改这一句）**：改为
  > **「82–84%」不是任何实测口径的复现值**：14 域等权主口径 81.5%、上界 86.3%，82–84% 只是两端之间的旧区间印象（见审计 §7）；节点加权从未落在 82–84%。

#### P0-2｜`KNOWN_LIMITATIONS.md` §0.2 用 8 节点子集代表动画能力域

- **位置**：`KNOWN_LIMITATIONS.md:18`
- **现文**：「节点可用率（按能力域）| 该域节点数 | 例：动画 `4/8 = 50.0%`；B6 23 节点 `16/23 = 69.6%`」
- **哪份对**：`CAPABILITY_WEIGHT_AUDIT.md` §4:173 与 `B6_ANIMATION.md` §8:163 —— **动画域是 11 节点，`10/11 = 90.9%`**；`4/8 = 50.0%` 是 **动画高级（B6.1）8 节点子集**。
- **错在哪**：把子集比例挂在「按能力域」标题下。对外读成「动画能力只有一半可用」，与动画域 90.9% 相差 40.9 个百分点。
- **建议改法（拆成两句/两行）**：
  > 例：**动画域（11 节点）** `10/11 = 90.9%`；**动画高级子集（B6.1，8 节点）** `4/8 = 50.0%`；B6 23 节点 `16/23 = 69.6%`。
  并在同格加注「两个动画分母不可互换」。

#### P0-3｜`B6_LEGACY_COMPAT.md` §6 的 196 / 71 与当前 223 / 108 冲突

- **位置**：`B6_LEGACY_COMPAT.md:132-133`
- **现文**：「端口契约 | `native_nodes=196 catalog_nodes=68 errors=0 warnings=4`（……）| `/tmp/b65a_port.log`（本轮输出见汇报）」；「枚举一致性 | `checked_pairs=71 inconsistencies=0`」
- **哪份对**：`A2_TASK345_STATUS.md` §215–§216 的 **223 / 108**；`LEGACY_COVERAGE_POLICY.md` §1.5 也把 **223** 列为四个不可互换分母之一；当前 `port_contract.py` 与 `enum_consistency.py` 输出为 223 / 108。
- **错在哪**：196 / 71 是 B6.5a 当轮快照，但原文写「本轮输出」且无时间戳，读者会把它当现状。223 是分母类指标，引用错会连带把端口契约覆盖率算错。
- **建议改法（改这两行）**：
  > 端口契约 | `native_nodes=**196** catalog_nodes=68 errors=0 warnings=4`（**B6.5a 当时快照；当前为 223，见 `A2_TASK345_STATUS.md` §215**）
  > 枚举一致性 | `checked_pairs=**71** inconsistencies=0`（**B6.5a 当时快照；当前为 108，见 §216**）

#### P0-4｜性能「3.5% 天花板」与「3.9% 内联变慢」缺限定语

- **位置**：`PROJECT_STATUS.md:159` vs `BENCHMARK_BASELINE.md:25`
- **现文**：「JIT 只做图内联，实测天花板约 3.5%」 / 「本 100 节点链上，内联开启比关闭慢约 3.9%」
- **判定**：**两处都没有写错，也没有互相混写**；但 §9 的「天花板」缺图与构建限定，而 A.4 恰恰给出了同一开关的**负收益**，外部极易把 3.5% 当成「JIT 普遍收益」。
- **两份的准确口径**：
  - 3.5%：`build_linux`，**100 个 `ConstFloat` 的最轻图**，`1127.9 → 1088.5 ns/帧`，**变快**；
  - 3.9%：`build_clean_verify`，**A.4 的 100 节点混合链**（96 `FloatMath` + 2 const + OnUpdate + Sink），**开启内联反而变慢**。
  - 按 `BENCHMARK_BASELINE.md` §3 规则 2，**不可跨构建/跨图比较**。
- **建议改法（只给 §9 加一句限定，不写 A.4 绝对数字）**：
  > JIT 只做图内联；**3.5% 是 100 个 `ConstFloat` 最轻图（`build_linux`）的收益上限，不代表所有图**。在 A.4 的 100 节点混合链上，同一内联开关开启比关闭慢约 3.9%（仅为方向性对照，绝对值以 `BENCHMARK_BASELINE.md` 为准，本节的最终数字仍等 D.3）。
- **禁止**：不要把 `2624.4 / 2525.2 / 8458.1` 搬进 §9；它们属于 A.4 基线，D.3 还要重跑最终版。

### 3.2 P1（措辞/计数规则，11 条）

#### P1-1｜`A1_BUILD_AUDIT.md` §11 的断言数把汇总行计入，5 项各多 1

- **位置**：`A1_BUILD_AUDIT.md:201/202/205/206/210`
- **现文**：b61 `27`、b62 `69`、b64 `62`、b65a `30`、bridge `7`；表头「断言（PASS+FAIL）」。
- **哪份对**：`A2_TASK345_STATUS.md` §155–164 的计数规则（扣除 `PASS total failures=0`）；日志实测扣汇总后为 **26 / 68 / 61 / 29 / 6**。
- **错在哪**：`PASS total failures=0` 是汇总行，不是断言。A1 的 17 项中有 5 项因此各多 1；其余 12 项因尾行是 `RESULT` 或 `INFO`，未受影响。
- **建议改法**：把表头改为「断言（**已扣除汇总行**）」，并把 5 个值改为 `26 / 68 / 61 / 29 / 6`；若坚持保留 PASS 行数，则表头写「PASS 行数（含汇总）」，并在 §11 开头注明两种计数不可混用。

#### P1-2｜`A2_TASK345_STATUS.md` 的 b61=23、b62=51 是旧 harness 版本

- **位置**：`A2_TASK345_STATUS.md:155-156`
- **现文**：「b61_e2e | 23（24 PASS 行 − 1 汇总）」「b62_e2e | 51（52 − 1）」
- **哪份对**：计数方法对，但**版本旧**。当前 clean 日志为 b61 **26**（新增 3 条 `wheel_names`：空值回退 + 显式四轮 + 顺序/去空格）、b62 **68**（v4 音频新增 17 条）。
- **错在哪**：同一 harness 名在同一交付周期出现两套断言数，且 A2 没标「旧版」。
- **建议改法**：两格改为
  > `26（旧版 23；本轮新增 wheel_names 3 条）` / `68（旧版 51；v4 新增 17 条）`，
  并加脚注「A1 §11 为 clean 重链后的当前版本」。

#### P1-3｜`A2_TASK345_STATUS.md` §43–44 的分解加不出 22

- **位置**：`A2_TASK345_STATUS.md:43-44`
- **现文**：「单测 3 条；矩阵 4 op × 4 = 16 条」→「合计 22 条断言」。
- **哪份对**：日志 `/tmp/a1_clean_run_b661_logicgate.log` 实为 **6 条 unit + 16 条 matrix = 22**；补测后为 30。
- **错在哪**：`3 + 16 = 19 ≠ 22`；分解已过期（clean 日志里 unit 是 6 条）。
- **建议改法**：改为「单测 **6** 条（`unit builds …` 6 行）+ 矩阵 4 op × 4 = 16 条 = **22** 条；补 `nand`/`nor` 后 30 条」。

#### P1-4｜`LEGACY_COVERAGE_POLICY.md` §1.4 的「其余 12」吞掉了 `mock-verified`

- **位置**：`LEGACY_COVERAGE_POLICY.md:66`
- **现文**：「分档：full 252 / approximate 22 / 其余 12；严格 252/286 = 88.1%……」
- **哪份对**：`CAPABILITY_WEIGHT_AUDIT.md` §4:186/§3:152 —— 12 = **8 `mock-verified` + 4 `unsupported`**。
- **错在哪**：算术没错（252+22+12=286，加权 267、覆盖 282 也都能对上），但四档制里「其余」会把 8 条 Mock 证据藏起来，与「Mock 通过 ≠ 真机通过」的口径相冲突。
- **建议改法**：改为「full 252 / `mock-verified` **8** / approximate 22 / unsupported **4** / unclassified 0」。

#### P1-5｜`A1_BUILD_AUDIT.md` 自身 284 与 287 不统一

- **位置**：`A1_BUILD_AUDIT.md:81`（284） vs `:269`（287）
- **现文**：§4 引 `/tmp/b45_inspector.log` 为 `checked=284`；§11.1 同一日志写 `checked=287`。
- **哪份对**：当前 `/tmp/b45_inspector.log` 为 **287**；287 与 clean 构建后的节点数一致。
- **错在哪**：284 是 19:19 首轮的旧值，日志已被 clean 轮覆盖；§4 没有标时间。
- **建议改法**：§4 改为「**19:19 首轮 `checked=284`；clean 重跑（日志当前值）`checked=287`**」，或直接统一引 287 并注明覆盖。

#### P1-6｜`B6_LEGACY_COMPAT.md` 的 29 条与 A1 的 30 条

- **位置**：`B6_LEGACY_COMPAT.md:135` vs `A1_BUILD_AUDIT.md:206`
- **现文**：「行为 E2E | 29 条断言 + 汇总 `PASS total failures=0`」 / A1 表「30」。
- **哪份对**：**29 对**（30 PASS 行 − 1 汇总）。
- **错在哪**：A1 多算 1（与 P1-1 同源）；B6 的写法本身正确但容易被误读为「29 + 1 = 30 条断言」。
- **建议改法**：B6 保留 29，但把「+ 汇总」改为「（30 PASS 行含 1 条汇总，**断言 29**）」；A1 按 P1-1 改为 29。

#### P1-7｜三个性能数字（312.376 / 477.806 / ~305）缺少「不可比」标注

- **位置**：`A1_BUILD_AUDIT.md:212` vs `A2_TASK345_STATUS.md:173`
- **现文**：A1 「`best_us_per_1000_frames=312.376`」（原写 `313.959`，全库日志零命中，已订正）；
  A2 「`477.806 µs/1000 帧`，历史 `~305 µs`，因链接对象集合不同不可比」。
- **哪份对**：**三个都对**，但属于不同链接集/构建；A2 已写「不可比」，A1 没写。
- **错在哪**：A1 的读者可能把 312.376 与 A.4 的 `2624.4 ns/帧` 直接比较——两者是**不同图**（b661_perf 约 4 节点，A.4 是 100 节点），差约 8 倍。
- **建议改法**：A1 §11 附注改为
  > `312.376 µs/1000 帧`（= `312.376 ns/帧`，clean 链接集，**小图**；原 `313.959` 无落盘日志，已作废）；与 A.2 的 `477.806`、历史 `~305` 属不同链接集，与 A.4 的 `2624.4 ns/帧` 属不同图，**均不可横向比较**。

#### P1-8｜`PROJECT_STATUS.md` §9 的 PENDING 前提已过期

- **位置**：`PROJECT_STATUS.md:153`
- **现文**：「`PENDING`：等 **A.1 clean rebuild + A.4 基线 + D.3 最终基准**填入。」
- **哪份对**：`A1_BUILD_AUDIT.md` §8/§10 与 `BENCHMARK_BASELINE.md` 已成文 —— A.1 clean rebuild **已完成（exit=0，8442 步）**，A.4 基线 **已建立**。
- **错在哪**：把两个已完成项写成前置阻塞，会让对外状态显示为「性能完全没开始」。
- **建议改法**：改为
  > `PENDING`：**只等 D.3 最终基准**；A.1 clean rebuild（exit=0，8442 步）与 A.4 基线（`9e15798a649a-clean_verify-3f074ee0eb1e`）已完成，但 A.4 是中间基线，D.3 还要在同一构建上重跑最终版，故本节仍不引用其绝对数字。

#### P1-9｜「动画」一词同时指 11 节点域与 8 节点子集

- **位置**：`B6_ANIMATION.md:163` / `CAPABILITY_WEIGHT_AUDIT.md:173-184`
- **现文**：B6 §8「动画域口径由此变化（**11 节点**……）」；CAP §4 同时有「动画 11」与「动画高级 8」两行。
- **哪份对**：两处都对，分母不同。
- **错在哪**：`B6_ANIMATION.md` 标题就是「B6.1 动画高级（8 节点）」，§8 突然换到 11 节点域，读者容易把 90.9% 与 §6 的 50.0% 当成同一分母下的矛盾。
- **建议改法**：§8 首句改为「**基础动画域（11 节点，不含 B6.1 高级 8 节点）**口径由此变化……」，并补一句「§6 的 8 节点子集与本节 11 节点域不可互换；两者在 CAP §4 是两个独立行」。

#### P1-10｜物理 15 与能力域 14 的跨文档互指缺失

- **位置**：`B6_PHYSICS_ADVANCED.md:139` vs `CAPABILITY_WEIGHT_AUDIT.md:183`
- **现文**：B6 「15 个物理节点，12/3/0 → 80.0%」；CAP 「物理高级 14，11/3/0 → 78.6%」。
- **哪份对**：两处都对。CAP 已注明「15 节点 12/3/0，去掉复用的 `SetGravity`」；B6 没写反向映射。
- **错在哪**：同一批节点出现 80.0% 与 78.6%，无交叉引用时会被判成矛盾。
- **建议改法**：B6 §6 统计行后加一句
  > 能力域核算把复用的 `SetGravity` 归入「物理（基础）」，故 CAP §4 的「物理高级」分母为 **14 → 11/14 = 78.6%**，与本表 12/15 不矛盾。

#### P1-11｜「69 PASS」与「69 条断言」的计数规则未定义

- **位置**：`B6_LEGACY_COMPAT.md:240`；`A1_BUILD_AUDIT.md:202`
- **现文**：B6 「`/tmp/b62_e2e_v4` **69 PASS / 0 FAIL**」；A1 表「b62_e2e | **69** | … 断言（PASS+FAIL）」。
- **哪份对**：69 是 **PASS 行数（含尾行汇总）**；真实断言为 **68**。
- **错在哪**：同一数字在 B6 是「PASS 行数」，在 A1 被当作「断言数」，读者会重复计数。
- **建议改法**：B6 改为「**68 条断言（69 PASS 行含 1 条汇总）/ 0 FAIL**」；A1 按 P1-1 统一。

## 4. 已确认一致清单

以下数字在多份文档/日志中**完全一致**，可直接作为对外引用：

| 组 | 指标 | 一致值 | 交叉位置 |
|---|---|---|---|
| A | legacy 全量 / 可感知 / semantic | 290 / 233 / 360 | LCP、PROJECT、KNOWN、FOREVER |
| B | legacy 三比例 | 23.8% / 29.6% / 40.6% | LCP、PROJECT、KNOWN、B6_LEGACY |
| C | legacy 已迁 / 起点 / FOREVER | 69 / 55 / 57 / 8 / 65 | LCP、B6_LEGACY、FOREVER、KNOWN、PROJECT |
| D | 70% 上限链 | 203 / 134 / 158 / 54.5% | LCP（§3 算式逐步可复算） |
| E | 286 分档合计 | 252 / 8 / 22 / 4 / 0 | CAP、LCP |
| F | 节点三口径 | 88.1% / 93.4% / 98.6% | CAP、LCP、KNOWN、PROJECT |
| G | 上界三口径 | 90.9% / 94.8% / 98.6% | CAP、PROJECT |
| H | 14 域等权主/上界 | 81.5% / 86.3% | CAP、PROJECT |
| I | 风险下界 | 73.8% / 86.2% / 98.6% | CAP、PROJECT |
| J | mock-only 拆分 | 90 = 41 + 49；网络另 11 | CAP、PROJECT |
| K | KNOWN 总数 | 24 = 20 + 4 + 0 | KNOWN、PROJECT |
| L | B6.1 23 节点 | 16 / 5 / 2 → 69.6% / 80.4% / 91.3% | PROJECT、B6_ANIMATION、KNOWN |
| M | 物理 15 | 12 / 3 / 0 → 80.0% / 90.0% / 100% | B6_PHYSICS、PROJECT |
| N | 动画高级 8 | 4 / 2 / 2 → 50.0% / 62.5% / 75.0% | B6_ANIMATION、PROJECT、CAP |
| O | 动画域 11 | 10 / 1 / 0 / 0 / 0 → 90.9% / 95.5% / 100% | CAP、B6_ANIMATION §8 |
| P | 音频 12 | 1 / 7 / 4 / 0 / 0 → 8.3% / 54.2% / 100% | CAP（引 `B6_AUDIO` §7/§9） |
| Q | WorldPosition 误差 | 1.679e-6（depth 0：3.353e-8） | B6_LEGACY、A2、PROJECT |
| R | 真机状态 | 8 项 = 1 verified + 7 pending；另有 2 条未修复记录 | REAL_DEVICE、PROJECT |
| S | 房间/重连 E2E | 32 PASS / 0 FAIL | PROJECT、日志重跑 |
| T | UI C3/C.7 E2E | 19 PASS | PROJECT、`a1_clean_run_b661_e2e.log` |
| U | clean 构建 | 8442 步 / exit 0 / 58m36s / `9e15798a649a` | A1、BENCHMARK |
| V | A.4 内部算术 | 3.9%（相对 2525.2）、3.2×、中位数三值 | BENCHMARK |
| W | JIT 轻量图 | 3.49% ≈ 3.5%（1127.9→1088.5） | KNOWN、PROJECT |
| X | b661_perf clean | 312.376 µs/1000 帧（原 313.959 无落盘日志，作废） | A1、`/tmp/a1_clean_run_b661_perf.log` |
| Y | LogicGate 时间线 | 22 → 30（补 `nand`/`nor` 8 条） | A1、A2 §183 |
| Z | 其余 12 个 harness | 15/46/23/9/9/36/36/17/19/41 等 | A1、A2、日志 |

## 5. 「都对但口径不同」的 6 组（不判冲突，但要补措辞）

| # | 指标 | 口径 A | 口径 B | 建议措辞 |
|---|---|---|---|---|
| 1 | 动画可用率 | 动画高级 8 节点：4/8 = 50.0% | 动画域 11 节点：10/11 = 90.9% | 两个数必须同时带括号标注「**B6.1 高级子集**」与「**动画全域**」，且声明不可互换 |
| 2 | 物理可用率 | B6.1 15 节点（含 `SetGravity`）：12/15 = 80.0% | CAP 高级 14 节点（`SetGravity` 归基础）：11/14 = 78.6% | 写「同一批节点，分母差 1 个复用节点；CAP 见 §4:183」 |
| 3 | 加权可用率 | 节点加权 267/286 = 93.4% | 14 域等权加权 89.3% | 必须写「**节点加权**」或「**域等权**」；禁止只写「加权 9x%」 |
| 4 | 内联收益 | 轻量图 **+3.49%**（变快） | A.4 混合链 **−3.9%**（变慢） | 必须写「图 + 构建」；禁止把 3.5% 与 3.9% 互换或求平均 |
| 5 | E2E 断言数 | PASS 行数（含汇总） | 断言数（扣汇总） | 统一用后者；若用前者必须写「PASS 行数，含 1 条汇总」 |
| 6 | semantic 40.6% | 实测 `146/360` | LCP §3.5 外推 `116/290 ≈ 40.0%` | 两个数必须分开写：一个实测、一个外推，禁止写成「40.6% 也证明外推成立」 |

## 6. 特别检查：性能节（§9）与 A.4 基线边界

1. **边界遵守情况**：`PROJECT_STATUS.md` §9 全文**未出现** `2624.4 / 2525.2 / 8458.1 / 3.9%`，符合「PENDING 不引用 A.4 绝对数字」。
2. **「3.5% 天花板」来源链**：`PROJECT_STATUS` §9:159 → `KNOWN_LIMITATIONS` #6:110 → `/tmp/b662_jit.log`（`BENCH ... reduction=3.49%`）→ `build_linux`、100 个 `ConstFloat` 最轻图。**这条链是通的**。
3. **「3.9% 内联变慢」来源链**：`BENCHMARK_BASELINE` §0:25 → `/tmp/a4_benchmark_baseline.log` → `build_clean_verify`、A.4 100 节点混合链。**这条链也是通的**。
4. **是否混淆**：全仓**无一处**把 3.5% 与 3.9% 写成同一个结论；但 §9 缺「图/构建」限定（P0-4），且两份文档都未互相交叉引用。
5. **建议的唯一补充**：在 §9 加一句「3.5% 只代表最轻图；A.4 混合链上同一开关为 −3.9%，仅方向性对照，绝对值等 D.3」。**不要**把 A.4 的绝对数字搬进 §9。
6. **D.3 前置条件**：按 `BENCHMARK_BASELINE.md` §3 规则 2，只要 HEAD、`libge_logic_bricks.a` sha256、configure 参数或编译器变化，就必须重建基线；D.3 应复用 `9e15798a649a-clean_verify-3f074ee0eb1e`，否则新数字与 A.4 不可比。

## 7. 取证命令与局限

- 断言数复核：对 `/tmp/a1_clean_run_*.log` 统计 `^PASS` 行并扣除 `^PASS total failures=0`；对 `RESULT` 尾行的 harness 直接用 PASS 行数。
- 版本差异复核：`diff` b61/b62 的 cross 与 clean 日志，确认新增 `wheel_names` 3 条与 v4 音频 17 条。
- 端口/枚举复核：`/tmp/b65a_port.log`（196）、`/tmp/verify_gap/port_contract.log`（223）、`/tmp/verify_gap/enum_consistency.log`（108）。
- 网络复核：重跑 `/tmp/c32_rooms_e2e`，32 条 PASS、`RESULT failures=0`。
- 真机复核：`REAL_DEVICE_VERIFICATION.md` 8 行状态表逐行统计。
- **局限**：本审计只做数字与算式对齐，不改判任何节点分档；`A1` 与 `A2` 的 harness 版本差异依据日志 diff 推断，未逐行审阅两个 `.cpp` 的全部差异；`A2` 的 477.806 只知「链接集不同」，未复现该链接集。

## 附录 A：已解决记录（2026-11-05）

> 本节只记录「已修」，**不改 §2–§7 的历史判定**。原判定保留在原处，供追溯当时的依据链。

| 原编号 | 原问题 | 处理结果 |
|---|---|---|
| P0-1 | 「82–84% 只有 14 域等权口径可复现」措辞不实 | `PROJECT_STATUS.md` §1 已改为「不是任何实测口径的复现值；14 域等权 81.5% / 86.3%，节点严格 88.1% / 90.9%」 |
| P0-4 | §9 的 3.5% 天花板缺「图/构建」限定 | `PROJECT_STATUS.md` §9/§10 已补「最轻图 3.49%」「与 A.4 的 3.93% 方向相反、不可跨构建比较」 |
| P1-7 | 三个性能数字缺「不可比」标注；且 313.959 无落盘日志 | `A1_BUILD_AUDIT.md` 已订正为 **312.376**（`/tmp/a1_clean_run_b661_perf.log:1`）；本文 §2 第 99 行、§4 X 行、P1-7 条目同步订正，原值注明作废 |
| P1-8 | `PROJECT_STATUS.md` §11 的 PENDING 前提过期 | 已改为「§9 由 D.3 填入（`BENCHMARK_FINAL.md`），唯一 PENDING 为 B6.8 独立验收文档」 |
| — | 73.8% 仍是「风险区间」措辞，与 B 类实测不符 | `PROJECT_STATUS.md` §1/§4/§10 已按实测改为 **实算 63.3%（`(252−71)/286`）**；`CAPABILITY_WEIGHT_AUDIT.md` 新增 §13「证据层级口径」。**58.4% / 56.6% 降为「含 14 条 `approximate` 重复扣减的保守下界」**，73.8% 保留为已取代的历史情景。**原 5 条「未覆盖」已全部补齐（20 条断言全 PASS，`/tmp/b66_uncovered_assertions.log`），故不存在「再剔除未覆盖」这一档** |
| — | 「实算 + 剔除未覆盖」这一档已作废 | 5 条（`JoystickAxis`、`JoystickButton`、`ProximitySensor`、`JSONStringify`、`JSONParse`）补真断言后 20 条全 PASS，**没有「未覆盖」可剔，该档等于 63.3%**；保留会让读者误以为仍有未覆盖项，故三档表删除该档，原档数值不再引用 |
| — | `B6_PHYSICS_ADVANCED.md:169` 的 `0.5279` 无日志来源 | 已改为 **0.5280**，并绑定 `/tmp/c1_physics.log:54` |
| — | `1.679e-6` 一度无可落盘证据 | 已绑定 `/tmp/verify_gap/worldpos_real_final.log:32`（`max_err=1.679e-06`），并补测量前提「1920×1080，与场景 aspect 一致」 |

**口径结论（2026-11-05 最终）**：`CAPABILITY_WEIGHT_AUDIT.md` §13.3 指出——§12.2 的 90 条名单里有
14 条本来就是 `approximate`，「85 条从 full 扣减」会重复扣 14 条。因此**对外主口径取可复算的
63.3%（`181/286`）**；**原 5 条「未覆盖」已全部补齐（20 条断言全 PASS），实算即 63.3%，不存在
「再剔除未覆盖」这一档（该档已作废，数值不再引用）**；**58.4% / 56.6% 仅作含重复扣减的
保守下界，不得当实测值呈现**。

## 附录 B：唯一编号登记表（K3 收口，`RUN_ID=20261107-221756`）

> **定位**：本附录是**附录 B 的唯一登记处**，本轮起由单一写者维护；**其它线不得再自行追加编号**。
> **编号规则**：`B-1` → `B-40` 连续，一条一行，不允许跨批次各自编号；同号重复已在上轮合并（见 B.2），合并项不再单独计数。派生类另用 `D-1`～`D-3`（见 B.4），不占 B 编号、不单独计数总量。
> **两类权威值**（断言类 **1481 / 48 批**（含 1 条陈旧期望失败，见 B.5）；节点类 `TypeCount()` **327**，历史冻结快照 `325` 见 B.0；逐条见 B.5）：
> - **断言类**：最终值一律以 `tests/run_all_e2e.sh` 逐批 `assertions=` 为准（禁用 `grep -c '^PASS'`）；
> - **节点类**：最终值一律以运行时 `KX_NodeRegistry::TypeCount()` 为准。
> **历史快照依据（已被 B.5 覆盖）**：clean rebuild（HEAD `325d3071`，`build_linux` 从零重建，`NINJA_EXIT=0`，8442 步）
> + 全量 E2E `TOTAL assertions=957 failures=0 build_failures=0 crashes=0`（29 行 = 4 `builtin` + 24 `harness` + 1 `script`；
> 不含 `b66_uncovered` = 28 行 / 937），日志 `/tmp/k_full_regression_20261106-101536.log`。
> **时点**：快照 E2E 跑于 12:58；13:21 MANIFEST 增至 36 行（4/29/3）；B-9 起各线自测晚于快照，只作线索。
> **构建补充证据（不含任何断言）**：2026-11-06 14:19–14:56 另一次 clean rebuild（`RUN_ID=20261106-141851`，
> `NINJA_EXIT=0`，8442/8442，日志 `/tmp/k_full_regression_20261106-141851.log`）仅证明「无 shim 树可干净编过」，
> **未跑 E2E**，不作为任何权威值来源。

### B.0 当前口径（注册表 `TypeCount()` 全量口径）

**六个量**（当前树实测，2026-11-07；`gdb` 与 `gen_metadata` 双侧 327）：

```text
F = full（实现完整）                 = 301
A = approximate（实现本身不完整）     = 4（Radar / Collision / EvaluatePropertyLegacy / TouchLegacy）
U = unsupported（明确不做）          = 7（触摸注入 ×3 / 骨骼权重 / 动作时标 / 场景卸载）
P = pending-device（仅缺设备验证）    = 15（音频 8 + 输入 7）
D = F + A（进分母的已定集合）         = 305
N = KX_NodeRegistry::TypeCount()     = 327
```

**历史冻结快照（保留一行，不参与当前算式）**：`RUN_ID=20261106-224926`（2026-11-06）：`N=325`、`F=299 / A=4 / U=7 / P=15`、`D=303`（严格 98.68% / 加权 99.34% / 覆盖 97.85%）。差异原因：常量物化修复新增两个 **IR 内部辅助节点** `ConstString`、`ConstVector`（无 `bl_idname`、不进 Add 菜单，只由 `ir_export` 注入），`TypeCount()` +2、二者均计 `F`，故 `F` 299→301、`D` 303→305、`N` 325→327。**为什么计 F 而不是剔除**：与 `ConstInt` 先例一致（纯辅助值节点计 `F`）；剔除等于为了分母好看而把真实注册类型排除在口径外，违反 `N = TypeCount()` 的冻结门槛。

**三个算式与结果**（每个都必须能从各域 `*_RESCORE.md` 的逐节点表加和复算）：

- 严格 `= F / D = 301 / 305 = 98.69%`
- 加权 `= (F + 0.5·A) / D = (301 + 0.5×4) / 305 = 303 / 305 = 99.34%`
- 覆盖 `= (F + A + P) / (D + U + P) = (301 + 4 + 15) / (305 + 7 + 15) = 320 / 327 = 97.86%`

**`U` 的定义边界**：`U` 只放**真正的 unsupported 清单**（7 条）。327 个类型已逐名分类，不再把「尚未分类的类型」计入 `U`；旧口径的未分类残差已作废，报覆盖必须用这 7 条真实清单。

**闭合校验**：`F + A + U + P = 301 + 4 + 7 + 15 = 327 = N` ✅。若四项再变动，仍以 `F + A + U + P == N` 为冻结门槛。

**作废口径**：上一轮按「本轮动过的节点」做增量账的三量口径（`F` / `A` / `D` 小集合）作废——
`docs/` 无登记、成员名单无法从仓库复现，**不进附录 B、不对外使用、不参与任何算式**。

**适用范围（两条，必须与数字同页出现）**：

1. `unsupported`（7）与 `pending-device`（15）都剔出分子，报告只描述**代码侧已定节点**；
2. 这是 **C++ 实现正确率**。**已无 Python 回退路径**：B7.5 删除 Python 逻辑执行器与 `use_native_graph_runtime` 开关，`nl_` 组件只警告并跳过。**但 E2E 入口覆盖 ≠ 用户点 P 的路径**：多数 harness 直连 `KX_FunctionNode`/`KX_CompiledGraph`，绕过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口；用户默认体验（建树 → 点 P → 运行时调度）的证据缺口仍需实机验证补齐，**不能声称 E2E 已覆盖用户默认路径**。

### B.1 登记表（唯一编号，一条一行）

```text
编号 | 登记值（历史快照，非当前权威） | 冻结值索引（K 最终权威见 B.5）
B-1 | K 块批 1（`RUN_ID=20261106-101536`）· 运行时已注册节点 = **321**（14 内建 + 296 表驱动 + 11 特例；旧 286 有两种写法） | 已冻结（见 B.5）；`KX_NodeRegistry::TypeCount()`（`RegisterBuiltinGameNodes()` 后）；冻结值见 B.5
B-2 | K 块批 1 · `native_metadata` 节点 = **301**（生成字典；`gen_metadata.py --check` 当时报 stale，未收敛） | 已冻结（见 B.5）；`gen_metadata.py` 重生成后的节点计数；冻结值见 B.5
B-3 | K 块批 1 · `b62` 断言 = **77** | 已冻结（见 B.5）；`run_all_e2e.sh` 批 `b62` 的 `assertions=`；冻结值见 B.5
B-4 | K 块批 1 · 全量断言总数 = **957**（29 行；`76+40+23+53+15+55+10+9+23+9+9+35+31+29+77+17+36+61+29+29+41+22+18+20+14+14+32+70+60`；不含 `b66` = 28 行 / 937） | 已冻结（见 B.5）；`run_all_e2e.sh` 的 `TOTAL assertions=`；冻结值见 B.5
B-5 | K 块批 1 · 分档百分比分母 = **321**（`252/321=78.5%`、`267/321=83.2%`、`282/321=87.9%`；分子未重审，只作下界） | 已冻结（见 B.5）；`TypeCount()` 分母 + 各域重打分分子；冻结值见 B.5
B-6 | K 块批 1 · `KNOWN_LIMITATIONS.md` §0.1 分母 286 → **321** | 已冻结（见 B.5）；同上（分母 `TypeCount()`）；冻结值见 B.5
B-7 | UI 线（`/tmp/e2e_a1g/b661.log`）· `b661` 断言 = **29** | 已冻结（见 B.5）；`run_all_e2e.sh` 批 `b661` 的 `assertions=`；冻结值见 B.5
B-8 | UI 线（`/tmp/e2e_c7/c7_ui.log`）· `c7_ui` 断言 = **60** | 已冻结（见 B.5）；批 `c7_ui` 的 `assertions=`；冻结值见 B.5
B-9 | B6.9-F 线（流程控制）· 运行时已注册节点 = **295**（+`While` / `Switch` / `ConstInt`；`Sequence` 原地重写不增数） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-10 | B6.9-F 线（`/tmp/b69_f_flow`）· `b69_f_flow` 断言 = **25** | 已冻结（见 B.5）；批 `b69_f_flow` 的 `assertions=`；冻结值见 B.5
B-11 | B6.9-U 线（`/tmp/e2e_u3/b64.log`）· `b64` 断言 = **78**。**并入（原同号条目）**：运行时已注册节点 = **299（暂计）**（`PlaySoundPlaylist` / `RaycastAll` / `MountChildScene` / `UnmountChildScene`），**已合并自 B6.9-Au/Se/Sc 线 13:16 后自测轮，不再单独计数** | 已冻结（见 B.5）；批 `b64` 的 `assertions=`（主）+ `TypeCount()`（并入项）；冻结值见 B.5
B-12 | P 线 P-2（`.e2e_out/run_20261106-131608`）· `c5_constraint_types` 断言 = **14**。**并入（原同号条目）**：`b69` 三块断言 = **35（暂计）**（音频 12 + 传感器 6 + Radar 4 + 场景 13），**已合并自 B6.9-Au/Se/Sc 线，不再单独计数** | 已冻结（见 B.5）；批 `c5` / `b69*` 的 `assertions=`；冻结值见 B.5
B-13 | P 线 P-1（`.e2e_out/run_20261106-131628`）· `c6_navmesh_bake` 断言 = **12**。**并入（原同号条目）**：新增适配器契约 = **1**（`KX_PhysicsAdapter::RaycastAll`，近→远顺序契约），**已合并自 B6.9-Se 线同轮，不再单独计数** | 已冻结（见 B.5）；批 `c6_navmesh_bake` 的 `assertions=`；冻结值见 B.5
B-14 | 复核轮（`.e2e_out/run_20261106-131620`）· `b61` 断言 = **31**（含 `wheel_names` 四轮顺序 / 去空格 / 空配置回退） | 已冻结（见 B.5）；批 `b61` 的 `assertions=`；冻结值见 B.5
B-15 | P 线 P-1 · `DEFERRED_FOREVER` 条目 = **−1**（#8 Recast 已实现；未改 §0 合计） | 已冻结（见 B.5）；`DEFERRED_FOREVER.md` §0 合计重算（口径外：文档计数）；冻结值见 B.5
B-16 | B6.9-N 线 · 网络域已注册节点 = **14**（+`DiscoverRooms` / `SyncPlayerState` / `GetPlayerState`） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-17 | B6.9-N 线 · 新增网络 E2E 断言 = **28**（`n1_discover_two_proc` 14 + `n2_state_sync_two_proc` 14） | 已冻结（见 B.5）；两批次各自 `assertions=`；冻结值见 B.5
B-18 | B6.9-N 线 · 网络重评分 = 实现 **14/14 = 100%**；真机 / 跨机 **0/14 = 0%** | 已冻结（见 B.5）；`TypeCount()` 分母 + 跨机证据（真机单列）；冻结值见 B.5
B-19 | B6.9-N 线（`.e2e_out/run_20261106-131904`）· 既有网络回归 = **183 断言 / 0 失败**（17+36+14+14+32+70） | 已冻结（见 B.5）；批 `b63*` / `c31*` / `c32*` 的 `assertions=` 之和；冻结值见 B.5
B-20 | B6.9-Fi 线 · 运行时已注册节点 = **+5**（`FileReadChunk` / `FileWriteChunk` / `ListDirectory` / `CreateDirectory` / `RemoveDirectory`；新表 `kB6FileStreamSpecs[]`） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-21 | B6.9-Fi 线（`/tmp/b69_file_e2e.log`）· `b69_file` 断言 = **19** | 已冻结（见 B.5）；批 `b69_file` 的 `assertions=`；冻结值见 B.5
B-22 | B6.9-Ms 线 · 运行时已注册节点 = **+3**（`SendRPC` / `OnRPC` / `SendDelayedMessage`；新表 `kB6MsgSpecs[]`） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-23 | B6.9-Ms 线（`/tmp/b69_msg_e2e.log`）· `b69_msg` 断言 = **20** | 已冻结（见 B.5）；批 `b69_msg` 的 `assertions=`；冻结值见 B.5
B-24 | B6.9-Fi/Ms 线 · 新增适配器契约 = **+9**（File 5 虚函数 + 1 自由函数；Message 2；Network 1） | 已冻结（见 B.5）；`KX_GameAdapters.h` 槽位静态计数（口径外：头文件计数）；冻结值见 B.5
B-25 | B6.9-Fi/Ms 线（`/tmp/e2e_b69_fi_ms`）· 回归面 = **39 断言 / 0 失败**（19+20） | 已冻结（见 B.5）；批 `b69_file` + `b69_msg` 的 `assertions=` 之和；冻结值见 B.5
B-26 | B6.9-Fi/Ms 线 · 编辑器元数据 / 端口 = **321**（`native_nodes=241 errors=0 warnings=4`；`checked_pairs=117 inconsistencies=0`；`checked=321 missing=0 unknown=0`） | 已冻结（见 B.5）；`port_contract.py` / `enum_consistency.py` / `gen_metadata.py` 输出；冻结值见 B.5
B-27 | B6.9-A 线 · 运行时已注册节点 = **322**（`SetActionTimeScale` / `GetActionNormalizedTime` / `GetBoneTransform`） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-28 | B6.9-A 线（`.e2e_out/run_20261106-134937`）· `b61` 断言 = **32** | 已冻结（见 B.5）；批 `b61` 的 `assertions=`；冻结值见 B.5
B-29 | B6.9-A 线（`tests/verify_gap/check_anim.py`）· 真实引擎动画断言 = **9** | 已冻结（见 B.5）；`check_anim.py` 的 PASS 计数（口径外：脚本断言；若并入 MANIFEST 则改取 `assertions=`）；冻结值见 B.5
B-30 | B6.9-A 线 · `DEFERRED_FOREVER` 条目 = **+1 候选**（`SetActionTimeScale`） | 已冻结（见 B.5）；`DEFERRED_FOREVER.md` §0 合计重算；冻结值见 B.5
B-31 | B6.9-A 线（C 块复核）· `BlendActions` 权重方向 = **已修**（layer 1 权重 `factor` → `1-factor`；不增节点 / 断言） | 已冻结（见 B.5）；真实引擎 factor 0 / 0.5 / 1 的 rotX 0.0 / 0.5 / 1.0 断言；冻结值见 B.5
B-32 | B6.9-I 线 · 运行时已注册节点 = **325**（+`GamepadCount` / `TouchGesture` / `TouchGet`；`gen_metadata.py --check` → `up to date (325 nodes)`） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-33 | B6.9-I 线（`tests/.e2e_out/run_20261106-141602`）· `b62` 断言 = **99** | 已冻结（见 B.5）；批 `b62` 的 `assertions=`；冻结值见 B.5
B-34 | B6.9-I 线 · 新增适配器契约 = **+3 虚函数 + 1 语法扩展**（`GetGamepadButtonValue` / `GetGamepadCount` / `GetTouchPressure`；`SetInputMap` 可选 `priority`；绑定可选 `:<deadzone>`） | 已冻结（见 B.5）；`KX_GameAdapters.h` 槽位静态计数；冻结值见 B.5
B-35 | B6.9-I 线 · `DEFERRED_FOREVER` 条目 = **+1**（真机触摸 / 压力 / 多点手势；`EngineInputAdapter::GetTouch` 返回全 0、`GetTouchCount` 返回 0，`KX_GameAdaptersEngine.cpp:425-437`） | 已冻结（见 B.5）；`DEFERRED_FOREVER.md` §0 合计重算；冻结值见 B.5
B-36 | B6.9-I 线 · 输入域三口径（26 节点：23 full / 1 mock / 0 approx / 2 unsup）= 严格 **88.5%**、加权 **90.4%**、覆盖 **92.3%** | 已冻结（见 B.5）；`TypeCount()` 分母 + 输入域重打分；冻结值见 B.5
B-37 | B6.9-M 线（登记早于第 11–13 号，编号后移以保其主条目不变）· 运行时已注册节点 = **306**（6 向量高级 + 5 数学补全；Formula 另增 10 函数，非节点） | 已冻结（见 B.5）；`TypeCount()`；冻结值见 B.5
B-38 | B6.9-M 线（`/tmp/b69_m_math`）· `b69_m_math` 断言 = **43**（Formula 12 + Vector 15 + Math 16） | 已冻结（见 B.5）；批 `b69_m_math` 的 `assertions=`；冻结值见 B.5
B-39 | B6.9-S 线（`/tmp/b69_s_run.log`）· `b69_s_list` 断言 = **7**（`ListSlice` 1 编译 + 6 负索引切片） | 已冻结（见 B.5）；批 `b69_s_list` 的 `assertions=`；冻结值见 B.5
B-40 | B6.9-S 线（`/tmp/b69_s_run.log`）· `b62` 断言 = **79**（`StringRegexReplace` 替换全部匹配 / 无匹配保持源串） | 已冻结（见 B.5）；批 `b62` 的 `assertions=`；冻结值见 B.5
```

### B.2 撞号与合并清单（本轮扫描结果）

| 类型 | 冲突 | 涉及条目 | 处理 |
|---|---|---|---|
| 同号双登记（不同指标） | 原第 11 号被占两次：`b64` 断言 vs 运行时节点 | 表内第 11 条（`b64`） | `b64` 为主条目，节点值并入其「登记来源」并标「已合并自 B6.9-Au/Se/Sc 线」，不再单独计数 |
| 同号双登记（不同指标） | 原第 12 号被占两次：`c5_constraint_types` vs `b69` 三块 | 表内第 12 条（`c5_constraint_types`） | `c5` 为主条目，`b69` 并入同上 |
| 同号双登记（不同指标） | 原第 13 号被占两次：`c6_navmesh_bake` vs 适配器契约 | 表内第 13 条（`c6_navmesh_bake`） | `c6_navmesh_bake` 为主条目，契约并入同上 |
| 非数字编号 | `B-M-1` / `B-M-2` / `B-S-1` / `B-S-2` 破坏连续数字规则 | B-37 / B-38 / B-39 / B-40 | 重编为连续数字，原前缀作废；登记时间早于第 11 号，编号后移 |
| 同指标多来源 | 运行时节点 **321 / 295 / 299 / 306 / 322 / 325** 六处 | B-1 / B-9 / 表内第 11 条（并入值）/ B-37 / B-27 / B-32 | 以上均为历史值；K2 快照统一为 `TypeCount() = 325`（B.5 历史表），当前树为 **327**（B.0） |
| 同指标多来源 | `b62` 断言 **77 / 79 / 99** 三处 | B-3 / B-40 / B-33 | 时间序；最终只认末次 `assertions=` = **134**（B.5） |
| 同指标多来源 | `b61` 断言 **31 / 32** 两处 | B-14 / B-28 | 同上；最终 `b61 = **44**`（B.5） |
| 同指标多来源 | 节点 / 元数据 **301 / 321 / 322 / 325** | B-2 / B-26 / B-27 / B-32 | `301` 已收敛到 **325**（B.5 历史快照）；当前树为 **327**（B.0） |
| 同指标多来源 | `FOREVER` 合计三条独立增减（−1 / +1 / +1），均「未改 §0 合计」 | B-15 / B-30 / B-35 | K 冻结：`FOREVER_IDS = 57`（B.5）；§0 的 `65 = 57` 机器 id `+ 8` 正文项 |
| 口径冲突 | 输入域分母 26 vs `CAPABILITY_WEIGHT_AUDIT.md` 旧分母 286 | B-36 | 两个分母不可混算，待重审 |
| 快照内不一致 | B-4 的 29 批求和里 `b61=29` / `b62=77` / `b64=61` / `c5=10`，但逐批条目已是 31 / 79 / 78 / 14 | B-4 与 B-14 / B-40 / 原第 11 号 / 原第 12 号 | **957 相对本附录自身条目已陈旧**，已被 **1481（48 批，B.5）**覆盖 |
| 批次未入总数 | B-4 的 29 行不含 `b69_f_flow`(25) / `b69_m_math`(43) / `b69_s_list`(7) / `b69_file`(19) / `b69_msg`(20) / `c6_navmesh_bake`(12) / `n1`+`n2`(28) | B-10 / B-38 / B-39 / B-21 / B-23 / 原第 13 号 / B-17 | 均为新增批次，已计入冻结总数 **1481（48 批）**（B.5） |
| 权威类缺失 | `FOREVER` 计数、适配器契约计数、域百分比不在「断言 / 节点」两类覆盖内 | B-15 / B-24 / B-30 / B-34 / B-35 / B-5 / B-18 / B-36 | 需你裁定第三类（文档计数 / 头文件静态计数 / 域打分） |

### B.3 派生值与口径声明

**派生值声明（已作废）**：B-5 / B-6 的 321 分母下界已被 B.5 冻结值取代——当前树严格 **98.69%**（301/305）、加权 **99.34%**（303/305）、覆盖 **97.86%**（320/327），`N=327`；历史快照 `RUN_ID=20261106-224926` 为 `N=325`（98.68% / 99.34% / 97.85%）。本附录不再给出任何下界百分比。

**不要混淆**：legacy 四口径（290 全量 / 233 可感知 / 360 semantic / 69 已迁移）与 `native_metadata` 的 **327** 无关。快照复核：`docs/deferred_matrix.json` 的 `rows=221`、`migrated_b65a=16`，`221 + 69 = 290` 未变；`233 / 360 / 69` 本轮未重算。`FOREVER_IDS` 仍为 57（§0 的 65 = 57 机器 id + 8 正文项，两套口径别混）。

**最终 K 前置检查（防 shim 污染）**：开跑前 `grep -rn "fadapter[-_]shim\|shim[.]h" tests/ docs/ source/` 必须零命中；`b64` 必须在无 shim 干净树重拿最终值。本轮快照声明：**未使用任何 shim / 仓库外补丁头文件**。

**端口附注**：`ListIndexOf` 的 `Value` 端口为 `Float`，已在 `B6_STRING_LIST.md` §4 补写「绝对值超过 `2^24` 的整数不再精确、相邻大整数可能误判相等」；`Index` 输出为 `Int` 不受影响。

**K 冻结锚点算式：本轮新增批次项（可入权威口径）**：下列两批均在 `build_one()` 修复后自测（构建前先删旧产物，`.build.rc=0`、`.build.log` 无 `error:`），断言数取自 `run_all_e2e.sh` 的 `TOTAL`/逐批汇总，可直接进权威口径：`b67_input_nodes` assertions=49（`tests/b67_input_nodes_e2e.cpp`，harness）、`b68_input_engine` assertions=12（脚本 `tests/b68_input_engine_e2e.sh`，断言源 `tests/b68_input_engine_e2e.cpp` 编译进同一二进制）。二者均产自 `RUN_ID=20261106-162958`。记账形式只增项、不改总数：`1456（46 批，K 冻结 `RUN_ID=20261106-224926`）`，上列 49 / 12 等新增批次均已包含在内；逐项以 K 的 `TOTAL` 行为准（B.5）。其中 `b68_input_engine`（`tests/b68_input_engine_e2e.sh`，`-DKX_INPUT_TEST_HOOKS`）：该批二进制以仓库内 `#ifdef` 钩子构建，虚拟手柄经 `KX_CreateEngineInputAdapterForTest` 注入；被测类为生产 `EngineInputAdapter`（生产对象无该钩子工厂，`nm` 已核）。设备来源为虚拟手柄，故输入节点档位为 `pending-device`。`c6_shape_overlap`（`tests/c6_shape_overlap.cpp`，18 断言）：节点契约层以等价参考后端（精确 box/sphere）验证；`harness_config.sh` 排除 `KX_GameAdaptersEngine.cpp`，故 Bullet `contactPairTest` 路径在本次运行中**仅编译验证 + 代码审查**，未经真实 Bullet 执行验证。**已由 `c6_shape_overlap_bullet` 覆盖真实 Bullet 路径**（10 断言，`contactPairTest` 在栈上探针对象、无 broadphase handle、不检查激活状态、无残留 manifold）。

### B.4 派生类条目（D）— 派生物，由上游类派生，不单独计数总量

```text
编号 | 派生项（上游条目） | 权威值来源（机器可核命令 / 可复算表）
D-1 | FOREVER 计数（上游：B-15 / B-30 / B-35） | **57**（最终 K 实测，`FOREVER_IDS_COUNT=57`）；`native_audit.py` 的 `_forever_ids()`（`native_audit.py:96-108`）区间扫描输出；扫描区间 `docs/DEFERRED_FOREVER.md` L187 起点标记 → L245 结束标记，机器区间 L188–L244；解释器为外部 UPBGE 构建（`/mnt/Eridanus/Blender/upbge-0.53-alpha-linux-x86_64-2026-09-13/build/bin/blender`，Blender 5.3.0 Alpha，hash `71d86278b6a3`），审计逻辑取自本仓库 `scripts/startup`；本仓库 `build_linux/bin/blender` 缺 `scripts/modules` 无法初始化 Python，故不以其为解释器
D-2 | 适配器契约计数（上游：B-24 / B-34、原第 13 号并入项） | `port_contract` 最终输出 **`native_nodes=244` / `errors=0`**（`warnings=4`；`catalog_nodes=68`），同轮 `enum_consistency checked_pairs=125 / inconsistencies=0`（`python3 scripts/startup/bge_gamenodes/generator/port_contract.py`）
D-3 | 域百分比（上游：B-5 / B-18 / B-36 及各域条目） | 各域 `*_RESCORE.md` 逐节点表合计 → **`F=301 / A=4 / U=7 / P=15`，`N=327`**，恒等式 `301+4+7+15=327` 闭合；严格 `301/305=98.69%`、加权 `303/305=99.34%`、覆盖 `320/327=97.86%`（`FLOW_MATH_STRING_RESCORE.md` §4.2、`KNOWN_LIMITATIONS.md` §4）
```

**派生规则**：D-1 / D-2 / D-3 **不占 B 编号**、不计入 `grep -c '^B-'` 的编号数，也**不单独计数总量**；它们只是把 B 组对应上游条目换算成一条可核命令（或可复算表）的输出。

### B.5 最终权威值（K3 收口回归，`RUN_ID=20261107-221756`，2026-11-07）

**K3 结论**：本轮源码修复（载具 `ownsMotionState` / `CcdPhysicsEnvironment` 释放语义 / `CreateVehicle` 准入与真实轮径、
Player 音频归一化查找与路径兜底、`FuncPrint` NONE 槽诊断、Debug Trace `poll`、端口标签）与新增 IR 辅助节点
（`ConstString` / `ConstVector`）后的干净重建全量回归。六量 `N=327 / F=301 / A=4 / U=7 / P=15`
（`301+4+7+15=327` 闭合）；E2E **48 批 / 1481 断言 / 1 失败 / 0 构建失败 / 0 崩溃**，唯一失败为陈旧期望（见下）。

**构建**：`rm -rf build_linux` 后从零 configure。首轮 `-j6` 到 `6689/8513` 时主机重启（非构建报错）；
重启后按 A1 已验证的 clean 配置补 `-DWITH_HYDRA=OFF -DWITH_USD=OFF` 重配（默认开 Hydra 时
`source/blender/render/hydra/engine.cc` 无法编译捆绑 USD 25.02，与本轮改动无关），末次
`NINJA_EXIT=0`，**2674/2674**、`error:`=0、`FAILED`=0、`ELAPSED=1787s`；`ninja install` `INSTALL_RC=0`
（`bin/5.3/scripts`、`bin/5.3/python` 已装）。三产物（install 后实测）：
`bin/blender` `0568b532ead891627dda5b168fe4cdc99b3e76fac5369353f8224c0cc5caa395`（233,544,424 B）、
`bin/blenderplayer` `97a7cf80c747e7b963656d63e464105c4c5ed5e6922be931b9fed67fae6e0b79`（262,475,496 B）、
`lib/libge_logic_bricks.a` `1c42d445615b86da1eacdf08ffd6a48727e37fe93a058070482bc11c070207a6`（6,743,498 B）。
**哈希口径**：与 K2 相同——`buildinfo.h` 为无输入 `CUSTOM_COMMAND`，每次 ninja 调用重写 `BUILD_TIME`；
install 前后两二进制字节数不变、哈希改变（install 前 `d56963d1…` / `ef837f95…`），交付以 install 后这组为准；
sha256 不可复现，可复现的是源码指纹、三产物字节数、`buildinfo` 字段与 E2E 的 `RUN_ID`/`TOTAL`。

**校验**（K3 实测，各一次）：`port_contract native_nodes=244 catalog_nodes=68 errors=0 warnings=4`；
`enum_consistency checked_pairs=125 inconsistencies=0`；`gen_metadata --check` → `up to date (327 nodes)`；
`TypeCount()` **实测 327**（`/var/tmp/k2_typecount.cpp` 经 `tests/harness_config.sh` 链接后直跑，输出 `TYPECOUNT=327`）。

**E2E**：**48 批**，`TOTAL assertions=1481 failures=1 build_failures=0 crashes=0`（`E2E_EXIT=1`）；
逐批 `.build.rc` 48/48 为 0、`error:` 命中 0（自动汇总）；`builtin` 4 行按整组例外
（`b5_build.rc=0`、`b5_build.log` 无 `error:`）。唯一失败 `b667_defaults`（6 断言 / 1 失败）：
该 untracked harness 原断言 `Print` 空槽输出 `"None"`，而本轮 `FuncPrint` 按修复要求输出
`<Print: Value input unset>`（`KX_FunctionNode.cpp` 注释明确不得再报 `"None"`）。
**这是测试期望落后于本轮修复，不是运行时回归。** 已按 `KNOWN_LIMITATIONS.md` #32 把该断言期望改为
`<Print: Value input unset>`，单批重跑 `RUN_ID=20261107-222555` → `b667_defaults 6 / 0`、
`TOTAL assertions=6 failures=0`；其余 47 批未重跑（全量 K3 快照仍记 `1481 / 1`，重跑后该索引为 0）。

**与 K2 快照的差异**（`RUN_ID=20261107-112207` → `20261107-221756`）：

| 指标 | K2 原生化后 | K3 收口回归 | 差异 |
|---|---:|---:|---|
| 断言总数 | 1456 | 1481 | +25（新增批 `c6_vehicle_wheel_guard` 19 + `b667_defaults` 6；后者含 1 陈旧期望失败） |
| 批次数 | 46 | 48 | +2 |
| 注册表节点 `TypeCount()` | 325 | 327 | +2 |
| 元数据节点 | 325 | 327 | +2 |
| `native_nodes` | 244 | 244 | 0 |
| `checked_pairs` | 125 | 125 | 0 |
| 失败/崩溃/构建失败 | 0/0/0 | 1/0/0 | +1（`b667_defaults` 陈旧期望，非运行时回归） |
| 三产物 sha256 | 见 K2 记录 | 见上方 | 全部改变（`ninja install` 重链接所致；**sha256 不可复现**） |

> **历史快照**：K2（`RUN_ID=20261107-112207`）为 `N=325 / F=299 / A=4 / U=7 / P=15`、`D=303`、46 批 / 1456 断言 / 0 失败；
> 插件模式基线（`RUN_ID=20261106-224926`）与其逐项一致。凡 `325 / 299 / 303 / 98.68% / 97.85%` 只作追溯，不得作为当前主口径。

**非 MANIFEST 证据**：`tests/b73_component_skip.cpp`（B7.3 专项）未登记进 MANIFEST，单独构建成功
（`error:`=0），但直跑 `SIGSEGV`（rc=139，`Py_Initialize`/`KX_GameObject` 段），**不作为通过证据**；
组件跳过行为另有源码路径（`KX_GameObject::Update` 内 `nl_` 前缀跳过）复核，未计入六量。

> 本表是**K3 冻结值**（`RUN_ID=20261107-221756`，2026-11-07 实测）；**当前口径（`N=327`）见 B.0**。K2/插件快照的 `325 / 299 / 303 / 98.68% / 97.85%` 只作追溯，不得作为当前主口径。

| 编号 | 冻结快照值（覆盖 B.1 的登记值） | 来源 |
|---|---|---|
| B-1 | 注册表节点 **327**（覆盖 321） | `TypeCount()` |
| B-2 | `native_metadata` **327**（覆盖 301） | `gen_metadata.py --check` |
| B-3 | `b62` **134**（覆盖 77） | 批 `b62` `assertions=` |
| B-4 | 全量 **1481**（覆盖 957；48 批，1 失败见 B.5） | `TOTAL assertions=` |
| B-5 | `D=305`；严格 **98.69%** / 加权 **99.34%** / 覆盖 **97.86%** | `*_RESCORE.md` 合计 + `TypeCount()` |
| B-6 | 分母 **327**（覆盖 286→321） | `TypeCount()` |
| B-7 | `b661` **29**（不变） | 批 `b661` |
| B-8 | `c7_ui` **60**（不变） | 批 `c7_ui` |
| B-9 | 注册表 **327**（覆盖 295 时点值） | `TypeCount()` |
| B-10 | `b69_f_flow` **36**（覆盖 25） | 批 `b69_f_flow` |
| B-11 | `b64` **94**（覆盖 78）；节点并入项以 **327** 为准（覆盖 299） | 批 `b64` + `TypeCount()` |
| B-12 | `c5_constraint_types` **16**（覆盖 14）；并入 `b69` **48**（覆盖 35） | 批 `c5` / `b69` |
| B-13 | `c6_navmesh_bake` **12**（不变）；适配器契约 **1** | 批 `c6_navmesh_bake` |
| B-14 | `b61` **44**（覆盖 31/32） | 批 `b61` |
| B-15 | `FOREVER` **57** | D-1 机器计数 |
| B-16 | 网络域 **14**（域内不变；总注册 **327**） | `TypeCount()` + `NETWORK_RESCORE.md` |
| B-17 | `n1` **14** + `n2` **14** = **28** | 两批 `assertions=` |
| B-18 | 实现 **14/14**；跨机 **0/14**（真机单列） | `NETWORK_RESCORE.md` |
| B-19 | 网络回归 **183**（17+36+14+14+32+70） | 六批 `assertions=` |
| B-20 | 文件域增量并入总量 **327**（覆盖 +5 时点值） | `TypeCount()` |
| B-21 | `b69_file` **19**（不变） | 批 `b69_file` |
| B-22 | 消息域增量并入总量 **327**（覆盖 +3 时点值） | `TypeCount()` |
| B-23 | `b69_msg` **20**（不变） | 批 `b69_msg` |
| B-24 | 适配器契约 **+9**（头文件静态计数，不变） | `KX_GameAdapters.h` |
| B-25 | 回归面 **39**（19+20） | 两批 `assertions=` |
| B-26 | 元数据 **327**；`native_nodes=244`；`checked_pairs=125`（覆盖 321/241/117） | 三条命令 |
| B-27 | 注册表 **327**（覆盖 322） | `TypeCount()` |
| B-28 | `b61` **44**（与 B-14 同指标，取末次） | 批 `b61` |
| B-29 | 脚本断言 **9**（`check_anim.py`，未入 MANIFEST） | 脚本口径 |
| B-30 | `FOREVER` **57** | D-1 机器计数 |
| B-31 | `BlendActions` 权重方向 **已修**（不增断言/节点） | 代码复核 |
| B-32 | 注册表 **327**（不变） | `TypeCount()` |
| B-33 | `b62` **134**（与 B-3/B-40 同指标，取末次） | 批 `b62` |
| B-34 | 契约 **+3 虚函数 + 1 语法扩展**（头文件静态计数，不变） | `KX_GameAdapters.h` |
| B-35 | `FOREVER` **57** | D-1 机器计数 |
| B-36 | 输入域 **26 节点 / D=23**（三口径以 `INPUT_RESCORE.md` 合计为准） | `INPUT_RESCORE.md` |
| B-37 | 注册表 **327**（覆盖 306） | `TypeCount()` |
| B-38 | `b69_m_math` **43**（不变） | 批 `b69_m_math` |
| B-39 | `b69_s_list` **7**（不变） | 批 `b69_s_list` |
| B-40 | `b62` **134**（覆盖 79） | 批 `b62` |

**六量与闭合（当前树，2026-11-07）**：`N=327`（`TypeCount()` 实测）；`F=301 / A=4 / U=7 / P=15`、`D=F+A=305`；
恒等式 `301 + 4 + 7 + 15 = 327` **闭合成立**。历史快照 `RUN_ID=20261106-224926` 为 `325 / 299 / 4 / 7 / 15 / 303`，
差异为新增 `ConstString` / `ConstVector` 两个 IR 内部辅助节点（与 `ConstInt` 先例一致计 `F`）。

**三口径（当前树复算）**：

- 严格 `= F/D = 301/305 = 98.69%`
- 加权 `= (F + 0.5·A)/D = (301+2)/305 = 99.34%`
- 覆盖 `= (F+A+P)/(D+U+P) = (301+4+15)/(305+7+15) = 320/327 = 97.86%`

- **适用范围**：`unsupported` 7 与 `pending-device` 15 剔出分子，只描述代码侧已定节点；
  这是 **C++ 实现正确率**。已无 Python 回退路径（B7.5 删除执行器与开关，`nl_` 组件只警告并跳过）；**但 E2E 入口覆盖 ≠ 用户点 P 的路径**——多数 harness 直连节点类/编译图、绕过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口，用户默认体验仍需实机验证补齐。
- **无「证据不足」批次**（K3 48/48 有 `.build.rc` 且全为 0、`error:` 命中 0；builtin 4 行按整组例外汇总）。
  唯一失败为 `b667_defaults` 的 1 条陈旧期望（见 B.5），不影响批次证据完整性。
- legacy 三口径同轮复核：`290 / 69 / 233 / 360`，`233 = 290 − 57`，semantic `146/360 = 40.6%`（D-1 同一次运行）。
