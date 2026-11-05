# PROJECT_STATUS — D.2 主文档

- 日期：2026-11-05
- 性质：**只读汇总 + 只写本文档**；未改源码、未跑构建、未 commit
- 写作原则：**能定的都定下来，依赖未完成项的地方标 `PENDING`，不凑数、不猜数**
- 本文所有数字都能指回 docs 或源码；口径冲突处按被引用文档的最新结论为准，并在正文注明

## 0. 一句话定位与当前阶段

**这个项目是什么**：把 bge_netlogic 的旧逻辑节点（legacy uplogic 图）迁移到引擎内的
**GameNode 原生节点系统**（`KX_LogicNodeRuntime` + 表驱动 `KX_FunctionSpec`，
注册入口 `KX_RuntimeNodes.cpp:201-250`），让逻辑图不再依赖 Python 运行时。

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

- 物理剩余 3 个 `approximate`：载具悬挂参数（`wheel_names` 已可配，悬挂长度仍固定）、
  `AddConstraint` 的 slider 映射、`BuildNavMesh` 的 agent 尺寸（无 Recast）。
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
| 场景查询 | `WorldPositionLegacy` 真实反投影（真实 `KX_Camera` + `GPU_matrix_unproject_3fv`） | 误差 **1.679e-6**（`max_err={:.3e}` 打印值，3 位有效数字；`/tmp/verify_gap/worldpos_real_final.log:32`；测量前提 **1920×1080，与场景 16:9 一致**，改 4:3 会得另一组值）：[B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) `:193-196`；实现 `KX_GameAdaptersEngine.cpp:635-686` |
| 输入/触摸 | `TouchLegacy` 球近似（世界位置球心 + 最大缩放半径，非真实碰撞体重叠） | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) `:187-192`；`KX_GameAdaptersEngine.cpp:699-753` |
| UI | C3/C.7 三项：Polar 布局命中、圆形旋钮（`DrawCircleApprox` 近似）、富文本行内颜色 | `/tmp/a1_clean_run_b661_e2e.log`（19 PASS，含 polar 4/8 槽、slider 1/2/0 次、rich text 3 条）；[B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §C.7 |
| UI | 多字体（`BLF_load_unique` 独立槽位） | [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §C.7.1 |
| UI | 基础竖排（非排版级） | [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §C.7.2 |
| 网络 | UDP v2 协议（v2 握手换端口、payload 上限 65507-16） | [B6_NETWORK.md](B6_NETWORK.md) §4；`/tmp/c31_udp_e2e`（`failures=0`，corrupt=0/duplicate=0） |
| 网络 | 房间列表 + 重连（无 host 300 ms 明确失败、host 恢复后 attempts=1） | [B6_NETWORK.md](B6_NETWORK.md) §8；`/tmp/c32_rooms_e2e`（**32 PASS**） |
| 物理 | `SetPhysicsRestitution` 运行时 setter（不重建刚体） | `KX_PhysicsAdvancedAdapter.cpp:841-864`；`CcdPhysicsController.cpp:1399-1415` |
| 物理 | 载具 `wheel_names`（逗号分隔，空则回退 Template） | `KX_PhysicsAdvancedAdapter.cpp:495-616、671-711` |
| 物理 | 导航无 Recast 已文档化（`agent_radius`/`agent_height` 接收未生效） | [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) `:134` |
| 文件 | 存档 zlib 压缩（`SaveGameBinary` 的 `Compress`，version=2） | [B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §压缩（`:44-60`） |
| 音频 | `StartSound` 句柄 32 位截断守卫（超 `INT32_MAX` 立即 `StopSound`、`Handle=0`） | `KX_FunctionNodesB3.cpp:361-377`、`:391-413`；`/tmp/audio_evidence.log`（Mock 断言） |
| 真机 | 绿框（green box）真机验证 | [REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md)（唯一 `verified`） |
| 全量 E2E | **27 批套件 / 850 条断言 / 1 失败**（唯一失败仍是 `b662_jit` 的 40% 阈值） | 算式 **`822 − 46(b4) − 36(p44) + 55 + 35 + 20 = 850`**；不含新增 `b66_uncovered` 的 26 批为 **830**。三处变化：`reg_b4_e2e` 46→**55**（新增 10 条 JSON 值断言）、`p44_stability` 36→**35**（手工汇总句不计断言）、新增 `b66_uncovered` **20 条**（`ProximitySensor` 10 / `JoystickAxis` 5 / `JoystickButton` 4 + 1 编译检查）。原始 `822` 见 `/tmp/d1_clean_rerun.log:33` |

> 注意：C3 的「圆形旋钮」是 `DrawCircleApprox` 近似（不是真圆角矩形控件）；
> 多字体与竖排都有明确「不做」清单（字形覆盖检测、粗斜体、排版级竖排）。

**上表的验证层级（不要混读）**：

| 证据类型 | 条目 | 说明 |
|---|---|---|
| 真实引擎/真机 | 绿框、`WorldPosition` 反投影 | `blenderplayer` 真实相机/真实 `unproject`（**1.679e-6**，`/tmp/verify_gap/worldpos_real_final.log:32`；前提 1920×1080 与场景 aspect 一致）；绿框为真机 `verified` |
| 本机真实进程（非跨机） | UDP v2、房间列表/重连 | 本机双进程 UDP + TCP，**不是跨机网络验证** |
| headless 渲染调用层 | C3 三项、多字体、竖排 | 断言的是布局坐标/绘制调用/文本分段，**不是真机显示效果** |
| Mock 适配器层 | `StartSound` 截断守卫、8 项 `mock-verified` | 只证明节点语义与参数读取，真机 `pending` |
| 实现 + 文档 | zlib 存档、restitution、`wheel_names`、导航无 Recast | 有源码行号与文档，未逐项跑真机 E2E |

## 6. 已知限制（前 5 条）

完整清单见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)（24 条：active 20 / mitigated 4 / removed 0）。

1. 音频 bus/zone 是等价实现，不是 audaspace 原生总线（#1）。
2. UDP 通道无重传、无拥塞控制、不保序（#2）。
3. `UnloadScene` 四态语义为 `mitigated`，根因仍在（#3）。
4. `GraphCompiler` 输出白名单只到 `Collision`（#4）。
5. `LoadSceneAsync` 进度是反馈值，不是真实加载进度（#5）。

## 7. 明确不做（66 条）

完整清单见 [DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)（57 条基线 + 本轮新增 9 条 = **66**）。
工期口径：易 2~3 天、中 4~6 天、难 8~12 天；废弃域记「不可排期」。

| 代表条目 | 理由 | 若真要做 |
|---|---|---:|
| `StartSound`/`StopSound`（legacy） | Python 侧不暴露 `AUD_Handle`，桥接路径 A 不成立 | 5 天，与 StopSound 同批 |
| AES（`EncryptData`/`DecryptData` 的 `algorithm=aes`） | 无加密入口、未链接加密库 | 5 天（体积+许可证+交叉编译） |
| 混音总线（真 audaspace bus） | audaspace 无命名 bus 路由 | 8~10 天，收益为零 |
| Touch 真重叠（接触对象集合） | `KX_PhysicsAdapter` 无同步 `contactTest` | 6~8 天 |
| `SetBoneWeight` | `KX_GameObject` 无顶点组 API | 10~15 天 |
| Recast 光栅化 | 只编译 Detour，无 Recast 网格光栅化 | 3~5 天（B6.9） |

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
8. 导航只有 Detour 查询，**没有 Recast**：`agent_radius`/`agent_height` 接收但不生效。
9. legacy `StartSound`/`StopSound` **永久不迁**（`AUD_Handle` 不暴露，Sound 桥路径 A 不成立）。
10. 真机当前只有**绿框 verified**，其余 pending；**性能基线已由 D.3 统一（BENCHMARK_FINAL.md），
    更早的历史数字一律不可引用**；引用任何性能数字必须带「进程数 × 每进程轮数 × 统计量」协议标签。
11. **断言计数的真实病根是「测试产出接口没定死」**：同一件事有 `RESULT assertions=N` /
    `INFO assertions=N` / `PASS` 行 / `PASS total failures=` / 手工 `PASS` 汇总句**五种表达**，
    导致「到底多 1 还是少 1」查了三轮、牵动四份文档。规则定为：**只认 `assertions=N` 声明**，
    并要求所有 harness 统一输出 `RESULT assertions=N failures=M`；**手工汇总句不计断言**。
    本轮 850 即按此规则复算（`822 − 46 − 36 + 55 + 35 + 20`，见 §5）。

## 11. 引用边界

- 本文只引用、不改写：
  [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)、[DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)、
  [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md)、[REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md)、
  [B6_NETWORK.md](B6_NETWORK.md)、[B6_UI_ADVANCED.md](B6_UI_ADVANCED.md)。
- 能力口径的唯一依据是 [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md)；
  本文与它冲突时，以审计文档为准并回改本文。
- §9 性能节已由 D.3 填入（[BENCHMARK_FINAL.md](BENCHMARK_FINAL.md)），PENDING 解除；
  **当前唯一 PENDING** 为 §0 的 **B6.8 独立验收文档**（另一条线正在产出
  `B6.8_ACCEPTANCE.md`，落地后本条亦消除）。其余条目均已有可指认证据。
