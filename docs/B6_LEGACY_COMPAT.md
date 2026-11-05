# B6.5a Legacy 兼容攻坚

本文件记录 B6.5a 的实测结论、落点、逐条判定与真实未完成项。所有数字均来自本轮实际运行，
日志路径写在每节末尾；未达成项不做美化。

## 0. 升级须知（B6.6 LogicGate 端口契约变更）

**`LogicGate` 是本轮唯一一处对既有原生节点端口契约的修改；其余既有原生节点均确认未改端口契约。**

- 变更内容：`LogicGate` 已从值节点改为**触发节点**——旧端口为 `A(Bool) / B(Bool) → Value(Bool)`，
  新端口为 `A(trigger) / B(trigger) → Result(Bool) / OnTrue(trigger) / OnFalse(trigger)`。
- 影响：升级前按旧端口导出的旧 IR **不能直接复用**，会在**编译期**被端口契约校验拒绝，
  **不是运行时静默出错**。症状（输入数同为 2，通常命中输出数校验）：

  ```
  Output port count mismatch for 'LogicGate': IR declares 1, registered signature has 3
  ```

  校验落点：`KX_GraphCompiler.cpp` 对每个节点的输入/输出端口数做严格比对；
  `TRIGGER_SNAPSHOT_NODE` 的 `allow_fewer_outputs=false`，因此不接受只声明旧输出的前缀图。
- 处理办法：用编辑器打开旧图，**重新导出** IR（LogicGate 改用 A/B 触发输入与
  Result/OnTrue/OnFalse 输出），再编译运行。旧图资源可保留，但重新导出前无法在新运行时编译。

## 1. 结论与实测数字

| 口径 | 分子/分母 | 百分比 | 日志 |
|---|---|---|---|
| 290 全量（第一层口径） | 69 / 290 | 23.8% | `/tmp/b65a_legacy.log` |
| 用户可感知（第四层口径） | 69 / 233 | 29.6% | `/tmp/b65a_legacy.log` |
| 起点基线（B5.1 结束） | 55 / 290 | 19.0% | 委派给定，本轮复核一致 |

- 本轮新增迁移 **14 条**：55 → 69。
- 第四层排除 **57 条**（渲染 40 + 废弃 17；`NLSetMaterial` 已迁移，不在排除区间）。
- 目标 ≥70% **未达成**：`PERCEIVED TARGET missed=70.0% gap_rows=164`。差距逐条原因见第 7 节，
  分母未作任何缩小。
- 审计三层明细：`migrated_direct=38 migrated_translated=30 migrated_rewritten=1 deferred=221`。

## 2. 落点与策略（14 条 Legacy 为什么新建类型而非改原生签名）

B5.1 的教训是“能映射”必须由契约证明，而不是由名字推测。本轮对所有候选逐条读取
`port_contract._legacy_sockets()` 与 C++ `KX_FunctionSpec` 端口表后，采用如下策略：

- Legacy 端口与任一原生节点不完全一致（触发器数量、输出种类、端口顺序、类型、config 化输入
  任一不同）时，**新建 `*Legacy` 运行时类型**，端口逐字复制 legacy，catalog 只做 `bl_idname →
  runtime_type_id` 绑定。
- 因此，**除 `LogicGate`（见第 0 节）外，本轮没有修改任何既有原生节点的端口契约**。旧 IR
  （原本指向原生 `Timer` / `Collision` 等）声明不变即可继续编译；新增能力只对新 `*Legacy`
  类型生效。`LogicGate` 是唯一例外：已改为触发契约，旧 IR 必须重新导出后才能编译。
  这也避开了 `KX_GraphCompiler.cpp` 白名单正在并行收口的冲突（该文件本轮未触碰）。
- 调度器约束：`ReadInput` 对缺失端口执行 `assert(index < input_count_)`，所以 catalog 不允许
  “只映射子集”；端口数量/类型/顺序必须全等。

新增文件与注册点：

| 文件 | 行 | 内容 |
|---|---|---|
| [KX_FunctionNodesB6Legacy.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Legacy.cpp#L650-L711) | 650–711 | `kB6LegacySpecs[]`，14 条 spec |
| 同上 | 754 | `RegisterB6LegacyFunctionGameNodes()` |
| [KX_RuntimeNodes.cpp](../source/gameengine/GameLogic/GameNodes/KX_RuntimeNodes.cpp#L248) | 248 | 追加注册调用（只追加，未重排） |
| [KX_FunctionNode.h](../source/gameengine/GameLogic/GameNodes/KX_FunctionNode.h#L263) | 263 | 追加声明 |
| [CMakeLists.txt](../source/gameengine/GameLogic/CMakeLists.txt#L201) | 201 | 追加源文件 |
| [native_catalog.py](../scripts/addons_core/bge_netlogic/editor/nodes/native_catalog.py#L443-L557) | 443–557 | 14 条映射 + config value_map |
| [port_contract.py](../scripts/addons_core/bge_netlogic/generator/port_contract.py#L66) | 66 | 新表加入 `CPP_TABLES` |

## 3. 第一层：7 条高价值

| 节点 | 结果 | 落点 / 证据 |
|---|---|---|
| Timer | 迁移 `TimerLegacy` | spec 651；旧端口 `WhenElapsed` 保持（`kLegacyTimerOutputs` 第 81 行）；E2E：`WhenElapsed fires exactly once` |
| QuitGame | 迁移 `QuitGameLegacy` | spec 657；legacy 无输出；E2E：`RequestQuit` |
| ClearParent | 迁移 `ClearParentLegacy` | spec 658；E2E：父链断开、Former Parent 输出、`OnDone` 三条 |
| LogicGate | 迁移 `LogicGate`（B6.6 重开） | B6.6 在调度器层加入 trigger 快照（`NeedsTriggerSnapshot` + `TriggerSnapshot`），`A/B` 为 trigger 输入，输出 `Result(Bool) / OnTrue / OnFalse`；AND/OR/XOR/NOT 真值表 E2E 16 条断言通过 |
| MessageReceived | 迁移 `MessageReceivedLegacy` | spec 662；输出 `OnMessage/Body/Messenger`（`kLegacyMessageOutputs` 第 162 行）；E2E 4 条 |
| WorldPosition | **deferred** | legacy 语义是 Screen To World（`camera/screen_x/screen_y/world_z`），不是对象世界坐标；适配器仅有 `RayCastFromScreen` 表面命中，没有任意深度相机射线方向，强行映射会给出错误坐标 |
| Touch | **deferred** | 对应 `NLConditionCollisionNode`：legacy 输出 `TARGET/OBJECTS/POINT/NORMAL`，原生 `Collision` 仅接触三触发器且无重叠对象列表；`mode: contact|touch` 扩展无法补齐对象列表语义 |

5 条中价值：

| 节点 | 结果 | 原因 |
|---|---|---|
| StartSound | **deferred** | legacy 20 入/3 出，原生 `StartSound` 5 入/2 出；`sound` 是 Python 句柄，无法一一映射 |
| StopSound | **deferred** | `sound` 为 Python 句柄，且句柄生产者 StartSound 未迁移，无可信 Int 桥 |
| IsNone | **deferred** | legacy 输入是通用 `checked_value` + 输出 condition；原生 `IsNone` 是 `Object → Bool`，端口 kind 与执行契约都不同 |
| Keyboard | **deferred** | legacy 只有 condition 输出；原生 `Keyboard` 输出 Bool `Active` 且走 key config |
| ObjectByName | 迁移 `ObjectByNameLegacy` | spec 670；E2E：按名字解析成功 |

结论：中价值 5 条完成 1 条（ObjectByName），其余 4 条按实情 deferred，原因不是“没时间”，而是
端口契约不成立。

## 4. 第二层：30 条高频+易

`deferred_matrix.json` 的 `candidate_verdicts` 记录了 30 条逐条实测判定：

- **16 条 migrated**：Timer、QuitGame、ClearParent、MessageReceived、ObjectByName、GetObject、
  TimeData、SimpleValue、SetMaterial、Pulsify、Barrier、EvaluateProperty、ModifyProperty、
  AlignAxisToVector，外加 B5.1 已完成的 Once、Compare。
- **14 条原判定 deferred**（逐条理由已写入 JSON），其中 LogicGate 已于 B6.6 重开为 migrated：
  - StartSound / StopSound：Python 句柄，无可信 Int 桥；
  - WorldPosition：Screen To World，不是对象坐标；
  - Collision/Touch：缺重叠对象列表与 4 个 legacy 输出；
  - LogicGate：B6.6 调度器 trigger 快照已补齐，改为 migrated（见第 3 节）；
  - Keyboard：condition vs Bool；
  - SwitchValue：legacy 单 condition 入 / 双 condition 出，原生 Branch 契约不同；
  - RotateTo：缺 `rot_axis/front_axis`；
  - MoveTo：缺 `dynamic/distance` 与 `Done+Reached` 双输出；
  - MouseLook：7 入/4 出 vs 5 入/1 出；
  - ApplyTransform：一个 mode 复用三套语义；
  - CreateMessage：legacy 有 `Messenger(from)`，原生 SendMessage 无 From；
  - LoadScene：legacy 5 输出，原生仅 `OnDone`；
  - MousePick：legacy 相机射线含 property/xray/mask 与 4 输出。

## 5. 第四层审计口径（写死在代码里）

`generator/native_audit.py`：

- 第 300 行 `_perceived_coverage()`：
  - 分母 = `report['total'] - len(excluded)`，`excluded` 只取
    `docs/DEFERRED_FOREVER.md` 的 `<!-- excluded:start --> … <!-- excluded:end -->` 区间中
    **同时属于 legacy 待处理集合** 的 `bl_idname`；
  - 分子 = `migrated_direct + translated + rewritten` 三个桶，口径与前三层一致，不另造分类。
- 第 345 行同时打印两套数字：
  - `PERCEIVED COVERAGE migrated=69 total=233 pct=29.6% raw_290=69/290=23.8% excluded=57`
  - 目标未达时打印 `PERCEIVED TARGET missed=70.0% gap_rows=164`。
- `_forever_doc_path()` 使用 `os.path.realpath(__file__)`，避免 build_linux 符号链接导致向上六层
  找不到 `docs/`。

## 6. 验收证据（全部为本轮实测）

| 项 | 结果 | 日志 |
|---|---|---|
| 构建 | `ninja -C build_linux -j2` → `NINJA_EXIT=0`（写入日志） | `/tmp/b65a_build_final.log` |
| 端口契约 | `native_nodes=223 catalog_nodes=68 errors=0 warnings=4`（4 条均为历史“名称不同、索引对齐”提示） | `/tmp/b65a_port_223.log` |
| 枚举一致性 | `checked_pairs=108 inconsistencies=0` | `/tmp/b65a_enum_108.log` |
| 注册/编辑器行数 | `REGISTERED_COUNT=14 EDITOR_ROWS=14 MISSING [] EXTRA []`（section D） | `/tmp/b65a_e2e_final.log` |
| 行为 E2E | 29 条断言 + 汇总 `PASS total failures=0`（section A） | `/tmp/b65a_e2e_final.log` |
| 四层审计 | `69/290=23.8%`、`69/233=29.6%`、`gap_rows=164` | `/tmp/b65a_legacy_final.log` |
| B2/B3 回归 | 14 断言 + `RESULT failures=0`（section B） | `/tmp/b65a_e2e_final.log` |
| B4 回归 | 45 断言 + `RESULT failures=0`（section C） | `/tmp/b65a_e2e_final.log` |
| P4 Rebuild | 旧图重建 + TimeData 重建后继续出值（0.25 ≥ 0.2） | `/tmp/b65a_e2e_final.log` |
| B5 | 未单独重跑；本轮未修改任何 B5 源文件，审计中 B5 条目数不变（见第 8 节限制） | — |

上表端口契约与枚举一致性数字对应 **2026-11-05 21:35 CST / build hash `9e15798a649a`**（当前 `build_linux`；
日志 `stat` mtime 均为 `21:35:29`，内容与表中数字逐字一致）。另可与
`/tmp/verify_gap/port_contract.log`、`/tmp/verify_gap/enum_consistency.log`（20:30:15 CST，同为 223/108）交叉复核。
历史值 `native_nodes=196 / checked_pairs=71` 属 **B6.5a 阶段快照（2026-11-05 18:24 CST 实测，已被 223/108 取代）**，
计算覆盖率时不要再用 196 作分母。

**追溯说明**：原 `/tmp/b65a_build.log`（18:16:56）未把退出码写入日志，原 `/tmp/b65a_e2e.log`（18:00:43）
早于最终增量构建，均不能作为「最终产物上的验收证据」。两者已由 **2026-11-05 21:40–21:41** 的重跑取代：
`/tmp/b65a_build_final.log` 含 `NINJA_EXIT=0`；`/tmp/b65a_e2e_final.log` 的 A/B/C/D 四段来自在 clean 产物上
21:14–21:15 构建的 harness 二进制（只读执行，未写入构建目录）。旧路径保留作痕迹。

E2E 覆盖（每个新节点至少一条行为断言）：

- LogicGate：B6.6 重开，4 种 op × 4 种输入组合（无 / A / B / AB）真值表 E2E 16 条断言通过。
- ClearParent：`clears the parent link`、`reports the former parent`、`OnDone`。
- WorldPosition 无实现；改用 TimeData 的 Rebuild 断言验证状态重建路径。
- Touch/contact 无实现；Collision 的 deferred 理由已写入矩阵。
- MessageReceived：`Body carries the payload`、`Messenger resolves the sender`、每消息一次、
  每帧轮询一次。
- Evaluate/Modify：比较触发、属性值输出、clamp 结果。
- AlignAxis：+X 旋到 +Y 的旋转结果。

向后兼容证据：

- 原生 `Timer` 定义仍在 `KX_FunctionNode.cpp:1307`，`git diff -U0` 对该文件无 `kTimerOutputs`
  相关改动；**B6.5a 本轮**没有给原生节点新增输出端口（B6.6 对 `Collision` 追加 3 个输出、对
  `LogicGate` 的契约变更见第 0 节，不计入本节结论）。
- 旧图重建由 E2E `old graph survives a Rebuild` + `TimeDataLegacy keeps producing values after
  Rebuild` 覆盖（此处指 B6.5a 的 Timer/TimeData 旧图；LogicGate 旧契约图必须重新导出，见第 0 节）。
  P4 的状态迁移白名单未改动（仅 UISlider/Delay/ForEach 及 UI 句柄类）。

## 7. 差距与真实限制（为什么未达 70%）

`gap_rows=164`。主要构成与原因：

1. **渲染内部件 40 条**：GPU 后处理/材质/几何节点树内部句柄，不属于游戏逻辑语义；
   已在 `DEFERRED_FOREVER.md` 排除。
2. **废弃 17 条**：旧 logictree/子网络、FMOD、VR，运行时接口已不存在。
3. **物理高级与运动类**：B6.1 正在并行覆盖；本轮不重复实现，矩阵中对应条目保持未迁移，
   待 B6.1 落地后再更新，避免“矩阵与现实脱节”反向发生。
4. **音频 2 条**：Python 句柄桥缺失（StartSound/StopSound）。
5. **输入 1 条**：Keyboard 的 condition/Bool 契约差异。
6. **其余低频条目**：需求语义不清或依赖不存在的适配器能力。

未做分母修饰：233 = 290 − 57，57 是文档机器区间内真实“永不移植”的 legacy 行。

## 8. 已知限制（如实保留）

- LogicGate 已由 B6.6 的调度器输入 trigger 快照解决同帧观测问题；NOT 定义为对 A 取反，
  B 仅用于触发求值（与 legacy UI 禁用 B 的行为一致）。
- 属性节点 `mode=1`（内部属性）在 `EvaluatePropertyLegacy` 中运行时直接返回，仅支持
  `mode=0` 游戏属性；这是 backend 能力边界，不是端口问题。
- MoveTo/RotateTo/MouseLook/SwitchValue 等需要独立 Legacy 类型，本轮未写，已列 deferred。
- B5 未单独执行回归套件（历史 harness 未保留）；仅以“未修改 B5 源文件 + 审计 B5 条目数不变”
  作为间接证据，属未覆盖风险。
- TouchLegacy 的重叠集合是**球近似**，不是真实碰撞体/网格重叠：`OverlapQuery`
  （`KX_GameAdaptersEngine.cpp:699-753`）以对象世界位置为球心、`max(|world_scale.x|, |world_scale.y|,
  |world_scale.z|)` 为半径；`overlap_threshold` 默认 `0.1`；仅当 `distance <= threshold + radius` 时计入。
  它遍历整条场景对象列表（包括相机等无碰撞体对象），与对象形状、旋向无关，命中的对象始终第一个
  回调且用精确射线点/法线。物理侧虽然有 `btGhostObject` 与 `getOverlappingPairCache()`，但只服务于
  Collision 触发器的内部配对，`KX_PhysicsAdapter` 没有同步 `contactTest` 类入口，因此本节点保持
  “射线命中点附近的对象”语义，而不是“本帧接触的刚体”。要真重叠属于设计变更。
- WorldPosition 的真实反投影链路已在 `blenderplayer` 里端到端验证（真实 `KX_Scene`/`KX_Camera`/
  canvas + 真实 `GPU_matrix_unproject_3fv`，误差 1.679e-6）；但 standalone player 下必须用
  `SetActiveCamera` 显式设活动相机（否则走 `__default__cam__` 回退），且首帧投影矩阵未初始化，
  查询需从第二帧起。

## 9. 本轮未触碰

- `KX_GraphCompiler.cpp`（并行线收口，本轮零改动）。
- 7 个未跟踪 `_lib.glsl`。
- `PROJECT_STATUS.md`（交叉验收统一写）。
- 未执行 `git commit/push`。

## 10. B6.6 复核：StartSound / StopSound 的句柄理由修正

日期：2026-11-05；报告 `/tmp/b661_sound_bridge_eval.md`。

§7 第 4 条（以及 `deferred_matrix.json` 的 `candidate_verdicts`）写的「`sound` 为 Python 句柄，
无可信 Int 桥」**经复核不成立**：

- `bge.logic` 侧音频对象是 `SCA_SoundActuator` 的 Python 实例（`KX_PythonInitTypes.cpp:262`；
  `SCA_SoundActuator.h:53-61` 内部持有 `AUD_Handle`），没有 `KX_SoundActuator` / `KX_3DSound` 类，
  也没有 `bge.logic.playSound()` API；handle 不暴露给 Python。
- 原生层 Int 桥已存在：`KX_AudioAdapter::PlaySoundByName → uint64_t`、引擎 `handles_` 表、
  `StopSound(uint64_t)`；`/tmp/b661_bridge` 已验证「播放→Int 往返→停止」全链路 `total failures=0`。
- 真正阻塞：legacy `StartSound` 20 入/3 出且 `Sound` 是 `NodeSocketLogicPython`（`nl_type=29`），
  原生值模型无 Python 类型；`port_contract.py` 无 29 的 tag 会计 error；20 输入中的 occlusion /
  reverb / timescale / start-end 无适配器能力。

结论不变（保持 FOREVER），但理由改为：**Python 句柄桥需 5 天（含 20 端口语义与 Int 宽度），
性价比不足**。本轮不改 `deferred_matrix.json`，其两条 `candidate_verdicts` 待矩阵维护者按本结论重写。

### 10.1 B.5 补充：两条路径的工期核算与截断修复（2026-11-05）

报告：`docs/SOUND_BRIDGE_EVAL.md`；日志 `/tmp/b5_sound.log`。

- **Python 侧句柄**：`bge.logic` 暴露的是 `SCA_SoundActuator` 的 Python 实例
  （`KX_PythonInitTypes.cpp:262`、`SCA_SoundActuator.h:53-61`），`AUD_Handle m_handle` 不暴露给 Python
  （属性表 `SCA_SoundActuator.cpp:376-413` 无 handle 属性），`bge.logic` 只有 `KX_SOUNDACT_*` 常量
  （`KX_PythonInit.cpp:1402-1410`），全仓无 `playSound`。
- **路径 A**（`KX_SoundActuator` 稳定 ID）：节点层拿不到 actuator，legacy `Sound` 端口仍是 Python 类型，
  ≥5 天且前提不成立，**评估后不做**。
- **路径 B**（`KX_AudioAdapter` 映射表）：C++ 桥 0 天（已存在），成本全在 legacy 20 入/3 出与
  `nl_type=29` 类型策略，合计 4~5 天（报 5 天）。
- **截断缺陷已修**（与 legacy 是否迁移无关，影响已在用的原生 `StartSound`）：
  `KX_FunctionNodesB3.cpp:352-377` 新增 32 位边界守卫，越界句柄立即 `StopSound` 并输出 0；
  `KX_GameAdaptersEngine.cpp:2419-2438` 新增 `AllocateHandle()`，id 限制在 1..INT32_MAX。
  未新增 Int64 值类型（该改造 0.5~1 天且涉及共享文件）。
- E2E：`/tmp/b62_e2e_v4` 69 PASS / 0 FAIL，其中包含「INT32_MAX 不截断」「>INT32_MAX 被拒绝」两组断言。

### 10.2 D.1 工程规则：E2E 构建必须走统一 TU 清单（2026-11-05）

入口脚本：`tests/harness_config.sh`、`tests/run_all_e2e.sh`、`tests/run_stability.sh`、
`tests/run_jit_equivalence.sh`、`tests/run_benchmark.sh`；说明：`tests/README.md`。

- **唯一 TU 清单来自 `build_linux/build.ninja`**（`harness_ninja_tus`），任何 harness
  （E2E / stability / benchmark / `/tmp/*_build.sh`）都不得再手写源文件路径。新增 TU 只改
  `source/gameengine/GameLogic/CMakeLists.txt` 并重新配置，所有脚本自动同步。
- 排除只允许两种：harness 源码里 `void Register*GameNodes() { }` 的**自动 stub 检测**，
  以及 `HARNESS_STATIC_EXCLUSIONS` 里的**显式表**（每行必须写 `TU|原因|owner`）。
- **历史教训（连续两次）**：独立 harness 用 `-Wl,--unresolved-symbols=ignore-all` 链接时，
  漏编 `KX_InlinePass.cpp` 不会导致链接失败，而是把缺失符号解析成 0 地址，运行到
  `KX_CompiledGraph::Compile` 内才段错误（`tests/b63_net_build.sh` 与 C.3.2 节点级 E2E）。
  现在链接后强制 `harness_check_required_symbols`（`nm -C` 检查
  `KX_CompiledGraph::Compile` / `KX_InlinePass::Run` / `CompileGraph(GraphIR const&)` /
  `KX_GraphInstance::Load`），缺失即输出
  `harness_config: undefined required symbol: <sym>` 并以构建失败退出，运行阶段不会启动。
- 自测：`HARNESS_DROP_TU=KX_InlinePass.cpp` 把该 TU 临时剔除，构建门禁必须报
  `undefined required symbol: KX_InlinePass::Run`（`/tmp/d1_scripts.log` 有记录），恢复后
  `check_rc=0`。
- `build_linux` 不 clean、不删除；检测到 ninja 锁时等待重试。`run_stability.sh` 默认完全不
  修改 `build_linux`，ASan 产物只写入 `tests/build_asan/`；只有显式 `--with-ninja` 才做增量构建。
- b5 回归固定 `BGE_GAMENODE_INLINE=0`：其期望的节点计数是未内联的结果，与当前 InlinePass
  的折叠行为不同；这是测试期望问题，不是清单问题，本轮未改编译器或断言。
