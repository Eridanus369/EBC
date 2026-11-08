# 流程 / 数学 / 字符串三域重打分（B6.9 3.2 / 3.3 后）

> 本文件只覆盖 `B6.9-F`（流程控制）、`B6.9-M`（数学/向量）、`B6.9-S`（字符串/列表）三域。
> 数字不并入 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B；该附录由 K 线单点重写。

## 0. 新口径（原文）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：

| 子类 | 定义 | 计入分母 | 计入分子 |
|---|---|---|---|
| **full** | 实现完成 + Headless/Mock E2E 通过 + 无已知限制 | 是 | 1.0 |
| **approximate** | 实现完成 + Headless E2E 通过 + 有明确限制（文档写清） | 是 | 0.5 |
| **unsupported** | 引擎层不支持（有可核 `路径:行号` 证据） | 否（进 FOREVER） | — |
| **pending-device** | 实现完成 + Mock E2E 通过 + 需真实设备验证 | 是 | 1.0（代码侧）+ 真机待验单列 |

`pending-device` 三条同时满足：① 节点有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless 验证的是具体设备/驱动依赖并给出 API 位置。

### 本轮重分类口径（2026-11-06，照抄上级原文）

- `D = full + approximate`；`unsupported` 与 `pending-device` **不进分母**；
- `approximate` 只表示「节点实现本身不完整」；**限制来自引擎能力时记 `full`**，限制写 `KNOWN_LIMITATIONS`；
- `pending-device` 剔除分母需同时满足：① 有 Mock/Headless E2E 通过日志；② 有可核理由说明为何 headless 验不了。

> 本文件三域没有 `pending-device` 与 `unsupported`。2026-11-06 两轮正则补丁：首版「组内含量词就拒」过粗，
> 误杀 `(\d+,)+` / `(ab?)+`，**该判定与当时使用的增量账分母一并作废，本文不再引用其任何百分比**（见 §4.2 沿革）；
> 第二轮改为「组体整体无界重复 或 可匹配空串」并补 `Rejected` 输出端口，两条正则重新升 `full`，三域回到域内 100%。
> 流程 `While` 与三个列表节点也在本轮升 `full`。全局口径见 §4.2：`N=327 / F=301 / A=4 / U=7 / P=15 / D=305`。

每个域并排给 **严格口径 A**（仅 `full` 计 1）与 **新口径 B**（`full` 1.0 / `approximate` 0.5 / `unsupported` 不进分母）。

## 1. 三域结论

| 域 | 分母口径 | 严格 A | 新口径 B | 覆盖率 | 与上一轮相比 |
|---|---|---:|---:|---:|---|
| 流程控制 | B6.9-F **5 节点** | **100.0%**（5/5） | **100.0%**（5/5） | 100% | 本轮 `While` 按「文档化调度语义」升 `full`，80.0→100.0 |
| 流程控制 | **8 条验收项**（上轮同口径） | **100.0%**（8/8） | **100.0%**（8/8） | 100% | 本轮第 8 项 `While` 条件语言升 `full`，87.5→100.0 |
| 数学/向量 | 值计算全 **53 节点** | **100%**（53/53） | **100%** | 100% | 上轮 42/42 100%，本轮 +11 节点仍 100% |
| 数学/向量 | 本轮 **21 条能力项** | **100%**（21/21） | **100%** | 100% | 100% → 100% |
| 字符串/列表 | 本轮 **7 条能力项** | **100.0%**（7/7） | **100.0%**（7/7） | 100% | 两项正则升 `full`（守卫收紧 + `Rejected` 端口），首版判定曾作废回滚（增量账已作废），双修后回到域内 100.0（7/7） |
| 字符串/列表 | 全 **37 节点** | **100.0%**（37/37） | **100.0%**（37/37） | 100% | 34 节点 85.3/92.6 → 37 节点 86.5/93.2 → 94.6/97.3 → 首版 100.0 → 中间态（增量账已作废，不再引用）→ 双修后 **域内** 100.0（37/37） |

**口径差异说明**

- 流程的 5 节点口径与上轮 8 验收项口径**都保留**：节点口径把 `ConstInt`（纯辅助节点）算进分母，8 项口径把“浮点条件”“任意端口”“并行同帧”单列为验收项。两者不是同一个分母。
- 字符串的 7 项与 37 节点**都保留**：7 项只算本轮 S-1 + 遗留两项；37 节点是 `CAPABILITY_WEIGHT_AUDIT.md` §2.5 的 34 节点加上本轮新增 `ListReverse` / `ListUnique` / `ListIndexOf`。
- 数学 53 节点 = 旧 42 + 本轮 11（`Formula` 只是加函数，不算新节点）。

## 2. 流程控制（B6.9-F）

### 2.1 逐节点

| 节点 | 分档 | 证据 | 限制 / 说明 |
|---|---|---|---|
| `ForEach` | full | `tests/b69_f_flow.cpp`：嵌套组合点 `10,1,2,3,20,1,2,3`，断言 `lines[4]=="20"`、`lines[5]=="1"`；同批 36 asserts 全过 | 无 |
| `Sequence` | full | serial 顺序 `30 40 50`；parallel 三分支同帧（`FrameTime` 计数器三行相同） | 输出固定 4 槽，由 spec 决定 |
| `Switch` | full | selector 0..3 各只触发对应 `CaseN`，越界只触发 `Default` | 分支固定 `Case0..Case3` + `Default` |
| `ConstInt` | full | `ConstInt` 为 `Switch` / `ListSlice` E2E 提供整数源；`b69_f_flow`、`b69_s_list` 均通过 | 辅助值节点 |
| `While` | **full**（本轮升级） | `i<2.5` → 3 次；`i<Threshold`（4.5）→ 5 次；`i*Step<Threshold`（2.5/7.5）→ 3 次；`i<Missing` → 0 次 + stderr 诊断；`i>=0, max=5` → 5 次 + stderr 诊断 | **限制（文档化调度语义）**：调度器在同一帧同步展开循环，所有输入端口值在 `OnEnter` 时快照，循环体内不能逐次改变条件变量；见 `KNOWN_LIMITATIONS` #28 与 `KX_FunctionNode.cpp:1530-1609` |

流程节点分母 = 5，`full` 5 + `approximate` 0：
严格 A = 5/5 = **100.0%**；加权 B = 5/5 = **100.0%**；覆盖 5/5。

### 2.2 8 条验收项（与上轮同口径）

| # | 验收项 | 分档 | 证据 |
|---|---|---|---|
| 1 | 嵌套 ForEach 内层重置 | full | `b69_f_flow` 组合点断言 |
| 2 | While 次数 | full | `i<10` → 10 次 + OnDone |
| 3 | While 限流诊断 | full | `max_iterations=5` → 5 次 + stderr |
| 4 | Switch 4 分支互斥 | full | selector 0..3 各只出一行 |
| 5 | Switch Default | full | selector 9 → 只出 `99` |
| 6 | Sequence serial 顺序 | full | `30 40 50` |
| 7 | Sequence parallel 扇出 | full | `30 40 50` + 同帧计数器 |
| 8 | While 条件语言 | **full**（本轮升级） | 浮点 + 任意数值端口已支持；端口值在 `OnEnter` 快照、循环体不能逐次更新，属**已写明的调度语义**（`KNOWN_LIMITATIONS` #28），不是实现缺失 |

严格 A = 8/8 = **100.0%**；加权 B = 8/8 = **100.0%**；覆盖 8/8。

### 2.3 3.2 实现要点

- `While` 输入端口：`OnEnter`、`Condition(Bool)`、`Threshold(Float)`、`Step(Float)`。
- 条件求值器现在接受十进制常量与任意输入端口名（大小写不敏感），端口按 `KX_FuncPort::name` 解析；未知标识符 → 0 次迭代 + 一次 stderr 诊断，不再静默。
- `Sequence` 串行走 pending 队列、并行一次 Raise 全部输出；E2E 用 `FrameTime.Frame` 证明并行三分支同帧。
- `KX_ExecutionContext.cpp` 未改动。

## 3. 数学 / 向量（B6.9-M）

### 3.1 逐节点

| 节点 | 分档 | 证据（`tests/b69_m_math.cpp`，43 asserts） |
|---|---|---|
| `Formula` | full | 10 个函数：sin/cos/tan/sqrt/abs/floor/ceil/round/min/max；`sqrt(-1)=0`、`1/0=0`、`tan(π/2)` 有限大值 |
| `VectorLerp` | full | t=0/1 精确端点、t=0.5 中点 |
| `VectorSlerp` | full | t=0/1 精确等于 A/B、t=0.5 单位方向 |
| `VectorRotate` | full | 90° 绕 Z、零轴 no-op |
| `VectorClamp` | full | 逐分量 min/max |
| `VectorNormalizeSafe` | full | `(3,4,0)→(0.6,0.8,0)`；零向量→零向量 |
| `VectorAngleSigned` | full | 轴 +Z / -Z 符号相反；零轴退化为无符号角 |
| `Lerp` | full | t=0/1/0.5 |
| `SmoothStep` | full | t=-1/0.5/1/2，端点夹紧 |
| `Step` | full | edge=1：0.999→0、1→1、2→1 |
| `PingPong` | full | 0.5→0.5、1.5→0.5、2→0、length=0→0 |
| `Repeat` | full | 2.5→0.5、-0.25→0.75、length=0→0 |

值计算域全量（旧 42 + 本轮 11）：**53 节点全部 `full`**。
严格 A = **100%**（53/53）；新口径 B = **100%**；覆盖 100%。
本轮能力项 21（Formula 10 + Vector 6 + Math 5）：严格 **100%**（21/21），新 **100%**。

## 4. 字符串 / 列表（B6.9-S）

### 4.1 本轮 7 条能力项

| 能力项 | 分档 | 证据 |
|---|---|---|
| `ListReverse` | full | `b62`：反转 3 元素、空列表 |
| `ListUnique` | full | `b62`：保序去重 `[1,2,1,3]→[1,2,3]` |
| `ListIndexOf` | full | `b62`：值语义命中 `2→1`、未命中 `-1` |
| `ListSlice` | full | `b69_s_list`：负索引 6 个切片 case + 编译断言 |
| `StringRegexReplace` | **full**（本轮升级） | `b62`：替换全部匹配 `a1b2→a#b#`、无匹配原样、源 >4096 字节拒绝；`(a+)+` 被拒时原样返回且 `Rejected=true`；新增 `Rejected` 端口（索引 1），`(\d+,)+` / `(ab?)+` 不再误杀 |
| `StringSplit` 多分隔符 | full | `b62`：`"a,b;c"` 用 `,;` → `a b c` |
| `StringRegexMatch` 捕获组 | **full**（本轮升级） | `b62`：`((a)(b))` 嵌套组顺序、可选组空串、未匹配空列表、源 >4096 字节拒绝；`(a+)+b` / `(a*)*` / `a**` 被拒不阻塞，`(a?)+` 因可匹配空串被拒；新增 `Rejected` 端口（索引 2），`(\d+,)+` / `(ab?)+` 放行 |

7 项分母：严格 A = 7/7 = **100.0%**；加权 B = (7 + 0.5×0)/7 = 7/7 = **100.0%**；覆盖 7/7。
**数值已冻结（K 实测，`RUN_ID=20261106-224926`；46 批 / 1456 断言 / 0 失败）。**

### 4.2 全域名册

`CAPABILITY_WEIGHT_AUDIT.md` §2.5 的 34 节点：原 29 `full` + 5 `approximate`
（`StringRegexMatch`、`StringRegexReplace`、`ListSort`、`ListFilter`、`ListMap`）。
本轮新增 3 个 `full` 节点（`ListReverse`、`ListUnique`、`ListIndexOf`），`ListSlice` 已在旧 34 内。
**第二轮评估后 `ListSort` / `ListFilter` / `ListMap` 全部升 `full`**：三者的偏差都是
`B6_STRING_LIST.md` §7 限制 2/3/4 已写明的文档化语义（数值元素语义、`key` 只读直接属性、
4096 静默截断），不是实现缺失。

37 节点：**37 full + 0 approximate**（两条正则本轮升 `full`）。
严格 A = 37/37 = **100.0%**；加权 B = (37 + 0.5×0)/37 = 37/37 = **100.0%**；覆盖 100%。

**判定沿革（2026-11-06）**：首版守卫把「组内含量词」一律拒绝，误杀 `(\d+,)+`、`(ab?)+`；当时的判定及所用增量账分母一并作废，本文不引用该账任何百分比。第二轮改为「组体整体无界重复 或 可匹配空串」并补 `Rejected` 端口后，两条正则重新判 `full`；依据是 §4.3 的 10 条验收断言（`b62` 135/0），不是沿用上一轮结论。

**字符串域内 100%**：37 个节点全部 `full`（37/37）；严格 A = 37/37 = **100.0%**；加权 B = (37 + 0.5×0)/37 = **100.0%**；覆盖 100%。这是**字符串域内 100%**，不是全局 100%。

**全局口径（327 注册表；`TypeCount()` 实测；当前树 2026-11-07）**：

- `N = 327`；`F = 301`、`A = 4`、`U = 7`、`P = 15`；恒等式 `301 + 4 + 7 + 15 = 327`。
- `D = F + A = 301 + 4 = 305`。
- 严格 A = `F / D` = `301 / 305` = **98.69%**。
- 加权 B = `(F + 0.5A) / D` = `(301 + 0.5×4) / 305` = `303 / 305` = **99.34%**。
- 覆盖 C = `(F + A + P) / (D + U + P)` = `(301 + 4 + 15) / (305 + 7 + 15)` = `320 / 327` = **97.86%**。
- 历史冻结快照 `RUN_ID=20261106-224926`：`N=325 / F=299 / A=4 / U=7 / P=15 / D=303`（98.68% / 99.34% / 97.85%）；差异为新增 `ConstString` / `ConstVector` 两个 IR 内部辅助节点（与 `ConstInt` 先例一致计 `F`）。
- **适用域**：`unsupported` 7 与 `pending-device` 15 已剔出严格/加权分母（覆盖口径含 P、不含 U），不代表「所有节点都好」；这三个数是 **C++ 实现正确率**。**已无 Python 回退路径**：B7.5 删除 Python 逻辑执行器与「原生运行时」开关后，`nl_` 组件只警告并跳过。**但 E2E 入口覆盖 ≠ 用户点 P 的路径**：多数 harness 直连 `KX_FunctionNode`/`KX_CompiledGraph`，绕过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口；用户默认体验（建树 → 点 P → 运行时调度）的证据缺口仍需实机验证补齐。

### 4.3 3.3 正则保护（本轮升 `full`）

- `Source` > **4096** 字节或 `Pattern` > **512** 字节 → 拒绝并置 `Rejected=true`：
  - `StringRegexMatch` → `Matched=false` + 空 `Groups`；
  - `StringRegexReplace` → 原样返回 `Source`；
  - 每个实例只写一次 `[StringRegexMatch] input too long ...` / `[StringRegexReplace] input too long ...`。
- **守卫收紧（2026-11-06 第二轮）**：两节点共用同一份 `IsSafeRegexPattern`（`KX_FunctionNodesB6String.cpp:146-300`），
  在 `std::regex` 构造前单趟 `O(n)` 扫描 + 括号深度栈判定，不借用 `std::regex` 自身。量词修饰的分组在**两个条件任一成立**时被拒：
  1. 组体**整体就是一个无界重复**（`+` / `*` / `{n,}` 覆盖整个组体），含跨层传播：`(a+)+`、`(a*)*`、`(.*)*`、`(a+){2,}`、`((a+))+`；
  2. 组体**可匹配空串**：`(a?)+`、`(a*)+`、`(b?)*`。

  连续量词（`a**`、`a++`、`a?*`、`a*+`）仍直接拒绝。命中时写一次
  `[Regex] unsafe pattern rejected: nested quantifier`，**不编译正则**。
  懒量化后缀（`a+?` / `a*?` / `a??`）算单个量词。放行 `(\d+,)+`、`(ab?)+`、`(ab)+`、`(a|b)*`、`(a|aa)+`、`[+*?]`、`\+`、`^abc$`、`(?:ab)+`。
- **保守拒绝**：只看「整体无界重复」与「可匹配空串」；`(a+)?`、`(a?)*` 仍会被拒，而
  `(a+b*)+`、`(aa+)+` 这类「组体含内部量词但结尾不是纯重复」会被放行——这是有意的保守/取舍，
  代价是部分形态不在保护范围（见 `KNOWN_LIMITATIONS.md` #30 / #9）。
- **`Rejected` 端口（2026-11-06）**：拒绝与「真不匹配」现在可区分。
  `StringRegexMatch` 输出 `Matched`(0)/`Groups`(1)/`Rejected`(2)；`StringRegexReplace` 输出 `Result`(0)/`Rejected`(1)。
  拒绝（守卫命中、超长、非法 Pattern）→ `Rejected=true` + `Matched=false` / `Result` 原样；
  正常不匹配 → `Rejected=false` + `Matched=false`；正常匹配 → `Rejected=false` + `Matched=true`。
  stderr 仍每实例只打一次（`state->warned`），跨帧保留、重建图实例才重置，且与长度/非法 Pattern 诊断共用同一标志。
- **不在保护范围**：`(a|aa)+$`、`(a|a?)+$` 这类「交替项前缀重叠 + 量词」会被放行，**无墙钟超时**，
  只受模式 512 / 源串 4096 的长度限制；账本线实测 40 字符可达 **29.7 秒**、44 字符 **超 45 秒**。
  `IsSafeRegexPattern` 不是 ReDoS 免疫，属引擎层限制。
- 常量位置：`KX_FunctionNodesB6String.cpp:54-55`；长度拒绝分支 `:462-467`（Match）/ `:535-540`（Replace）；
  守卫拒绝分支 `:468-475`（Match）/ `:541-548`（Replace）；端口数组与 `static_assert` `:442-446` / `:507-519`。
- **证据**：`b62` 第二轮新增 10 条验收断言（`tests/b62_e2e.cpp:1316-1406`，`run_20261106-215519`：**135/0**）：
  `(\d+,)+` 配 `1,2,3,` → `Matched=true` / `Rejected=false`；
  `(ab?)+` 配 `abab` → `Matched=true` / `Rejected=false`；
  `(a+)+b`、`(a*)*`、`(.*)*`、`(a?)+` → `Rejected=true` / `Matched=false`；
  `abc` 配 `xyz` → 两者皆 false，`abc` 配 `abc` → `Matched=true` / `Rejected=false`；
  同图拒绝后第 2 帧 `[a-z]+` 仍编译、仍匹配；
  `(a|aa)+$` 配 22 字符（21 个 `a` + `b`）→ 放行、<100ms，**只证明短输入能跑完，不证明长输入不会卡**。
- **判定**：两处真缺口已关闭（误杀常见写法 + 拒绝与不匹配无法区分），两条正则由 `approximate` 升 `full`；
  剩余限制为书面登记的 `KNOWN_LIMITATIONS.md` #30（长度上限、`(a+b*)+` 这类形态放行、交替回溯无超时），**不进 FOREVER**。
  **数值已冻结（K 实测，`RUN_ID=20261106-224926`；46 批 / 1456 断言 / 0 失败）。**

## 5. 证据与命令

- 聚焦批次：`E2E_JOBS=4 tests/run_all_e2e.sh --only 'b62|b69_f|b69_m|b69_s'` → `run_20261106-154145`
  - `b62 101/0`、`b69_f_flow 36/0`、`b69_m_math 43/0`、`b69_s_list 7/0`、`b62` 之外同批 `b69_file 19/0`、`b69_msg 20/0`（其他线在跑）。
- 本批新增断言：流程 **+11**（25→36）；字符串 **+2**（79→81，其余 `b62` 增量为其他线）；数学不变 43。
- 本轮（2026-11-06 正则补丁第一版）单独重跑 `b62`：`tests/.e2e_out/run_20261106-211618/b62.log` → **125/0**
  （新增 23 条正则断言，`tests/b62_e2e.cpp:1224-1310`；生产 TU 增量编译 `NINJA_RC=0`、`error:` 0）。
- 本轮（2026-11-06 正则双修第二版）单独重跑 `b62`：`tests/.e2e_out/run_20261106-215519/b62.log` → **135/0**
  （新增 10 条验收断言，`tests/b62_e2e.cpp:1316-1406`；`NINJA_RC=0`、`error:` 0）。
- `enum_consistency`：125 pairs / 0 inconsistencies（`Sequence.mode` 的编辑器条目保持 `enum` 字符串导出）。
- `port_contract`：errors=0（Match 输出 3 / Replace 输出 2 已同步）。
- `gen_metadata.py --check`：327 节点 up to date（`StringRegexMatch` / `StringRegexReplace` 已带 `Rejected`）。

## 6. 未达 95% 的差距（不硬凑）

| 域 | 新口径 B | 距 95% | 差距性质 |
|---|---:|---:|---|
| 流程（8 项） | 100.0% | 0 | 本轮已关闭（`While` 是文档化调度语义，升 full） |
| 数学 | 100% | 0 | — |
| 字符串（37 节点） | 100.0% | 0 | 本轮已关闭（两条正则收紧守卫 + 补 `Rejected` 端口后升 full；剩余限制见 `KNOWN_LIMITATIONS` #30） |

流程、数学、字符串三域本轮均为 100.0%（新口径 B），严格口径 A 亦为 100.0%。
字符串此前的 2.7pp 缺口（两条正则 `approximate`）已通过「整体无界重复 或 可匹配空串」的守卫收紧关闭；
剩余的长度上限、`(a+b*)+` 这类形态放行、交替回溯无超时属书面登记的限制，不再折算成缺口。
**适用域**：本表三域 95 个节点（流程 5 + 数学 53 + 字符串 37）均为**域内 100%**；字符串的 100% 是「字符串域内 100%」，不是全局 100%。全局 327 口径见 §4.2：`N=327 / F=301 / A=4 / U=7 / P=15 / D=305`，严格 **98.69%**、加权 **99.34%**、覆盖 **97.86%**。这是 **C++ 实现正确率**，不是用户默认体验。**节点六量为当前树（2026-11-07）实测；E2E 断言数 1456 为 `RUN_ID=20261106-224926` 快照，待 P0 修复后重跑再改。**

### 本轮（2026-11-06）重分类复核

- 本文件原不在 12 条 `approximate → full` 重分类名单内；`StringRegexMatch` / `StringRegexReplace`
  在 2026-11-06 的首版正则补丁中补上 `IsSafeRegexPattern`，但首版规则过粗（误杀 `(\d+,)+` / `(ab?)+`），
  **其判定与所用增量账分母已作废，不再引用任何百分比**；第二轮收紧守卫并补 `Rejected` 端口后才由 `approximate` 升 `full`
  （`KNOWN_LIMITATIONS` #30，`b62` 135/0）。
- 3 个列表节点（`ListSort` / `ListFilter` / `ListMap`）在上一轮未重分类；本轮逐项复评后，
  三者的偏差都能指到 `B6_STRING_LIST.md` §7 限制 2/3/4 的**文档化语义**（数值元素语义、
  `key` 只读直接属性、4096 静默截断）→ 全部升 `full`（`KNOWN_LIMITATIONS` #27）。
- 流程 `While` 的偏差是 `OnEnter` 快照的**文档化调度语义** → 升 `full`（`KNOWN_LIMITATIONS` #28）。
- 更新后的百分比：流程 100.0%、数学 100%、字符串 100.0%（严格 A 与加权 B 均为 7/7、37/37）。**数值已冻结（K 实测，`RUN_ID=20261106-224926`；46 批 / 1456 断言 / 0 失败）。**

## 7. 第二轮逐项评估（2026-11-06，只读核查）

### 7.1 `ListSort` / `ListFilter` / `ListMap`

| 节点 | 实际支持的语法 | 拒绝/截断的输入 | 诊断 | 用户会不会撞到 | 判定 | 代码位置 |
|---|---|---|---|---|---|---|
| `ListSort` | 无表达式；`Key` 为可选属性名，直接读元素属性（数值或字符串），`Descending` 布尔；稳定排序；字符串键可直接比较 | 嵌套属性路径（`a.b`）、列表索引（`x[0]`）不支持；无数值/长度截断（`CopyIntoPool` 只把 reserve 截到 4096，`assign` 不丢元素） | 属性不可读（非对象/无该属性）时 `KeyOf` 退化为按元素自身值排序（`:508-516`），不报错；该边界已写在 `B6_STRING_LIST.md` §7 限制 3 | 不会（按文档使用对象 + 直接属性时）；只有嵌套路径取不到，属边界 | **限制** → 升 `full` | `KX_FunctionNodesB6String.cpp:519-558`（排序）、`:486-517`（`KeyOf`）；`docs/B6_STRING_LIST.md:151-156` |
| `ListFilter` | 谓词为数值表达式：变量只有 `x`（元素 `AsFloat()`）；运算符 `|| && == != <= >= < > + - * / %`、一元 `- !`、括号、十进制常量 | 字符串字面量/字符串比较、函数调用、属性访问、三目；源 >4096 或结果 >4096 时在 `result.size() < 4096` 处停止 | 表达式语法错误有 stderr（每实例一次）；非数值元素按 `AsFloat()` 规则处理（字符串/对象/列表 → 0），**该语义已写在 §7 限制 2**；4096 截断已写在 §7 限制 4 | **不会**（按文档使用：元素必须是数值语义）；用字符串列表属违反文档契约，不是节点漏实现 | **限制** → 升 `full` | `KX_FunctionNodesB6String.cpp:757-786`（求值/4096 上限）、`:563-747`（表达式解析）、`KX_RuntimeValue.h:204-217`；`docs/B6_STRING_LIST.md:151-156` |
| `ListMap` | 同 `ListFilter`：变量只有 `x`，同一套算术/比较/逻辑运算符与括号 | 同上；结果 >4096 时静默停止（`:821`） | 表达式错误有 stderr 并清空结果（`:806-809`）；非数值元素按 `AsFloat()` → 0，§7 限制 2 已写明；4096 截断见 §7 限制 4 | **不会**（同上：文档要求数值语义） | **限制** → 升 `full` | `KX_FunctionNodesB6String.cpp:796-826`；`KX_RuntimeValue.h:204-217`；`docs/B6_STRING_LIST.md:151-156` |

说明 1：`a > b && c` 这一写法在本节点不成立，因为节点模型只有 `x` 一个变量；对应的常用布尔组合
`x > 5 && x < 10` 是**支持**的。

说明 2：字符串比较不可用，但这不是静默误导——`B6_STRING_LIST.md` §7 限制 2 明确写了
「Predicate/Expression 只支持 §5 子集，元素必须是数值语义（`AsFloat`）；对象/列表元素按 0 处理」，
用户按文档使用不会撞到。若产品上需要字符串筛选，属**新功能请求**，不是缺陷修复。

### 7.2 `While`

| 问题 | 结论 | 依据 |
|---|---|---|
| 条件每帧读一次还是每次循环重读？ | **进入时一次性求值**：`OnEnter` 内用内部计数器 `i` 循环求条件，得到总迭代数后才开始派发循环体 | `KX_FunctionNode.cpp:1530-1609`（条件求值循环）、`:1571-1577`（`i` 自增）、`:1611-1626`（按迭代派发） |
| 输入端口是否逐次更新？ | 否：`Threshold` / `Step` 等端口值在 `OnEnter` 时快照 | `KX_FunctionNode.cpp:1544-1569` |
| 并行分支同帧内是否读到同一个 `i`？ | 否：每个迭代的完整下游子树在推进下一个迭代前执行完，各分支看到各自的 `Index` | `KX_ExecutionContext.cpp:308-318`（注释写明 “Run the whole downstream subtree of one element before advancing”） |
| 用户写 `While(i < n)` 且循环体修改 `i`，行为可预测吗？ | **可预测但不命令式**：迭代次数已由 `OnEnter` 时的快照端口 + 内部 `i` 决定，循环体对 `i`/`n` 的修改不改变次数 | 同上；文档 `docs/FLOW_MATH_STRING_RESCORE.md` §3.1（`While` 行）已写明 |
| 判定 | **限制** → 升 `full`（`KNOWN_LIMITATIONS` #28） | 条件在帧首快照且文档已写明 |
