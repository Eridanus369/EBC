# A.2 B6.6 任务 3 / 4 / 5 落地审计

- 审计日期：2026-11-05
- 审计范围：`/home/eridanusepsilon/Code/EBC/EBC5.3`
- 审计方式：只读静态取证 + 复用 19:17–19:20 本轮交叉验收日志；**未重跑 harness、未编译、未删除任何文件、未改任何源码**
- 原始证据：`/tmp/a2_task345_audit.log`（符号 → 文件:行号）、`/tmp/a2_harness_source.log`（harness 源码取证）、`/tmp/a2_results.log`（断言数与历史性能）

## 结论

**任务 3 / 4 / 5 均已落地，没有 missing 项；但验证层面有三处缺口，均不改变"存在"的判定。**

| 任务 | 实现 | 验证 | 综合判定 |
| --- | --- | --- | --- |
| 3 LogicGate + 调度器 trigger 快照 | 完整 | 部分 | **full（验证 partial）**：6 个 op 全部注册，但 E2E 只覆盖 4 个；性能只有人工打印，没有 ±5% 自动断言 |
| 4 WorldPositionLegacy | 完整 | 部分 | **full（验证 partial）**：反投影实现正确，深度三态 E2E 齐全，但 E2E 走 mock adapter，真实引擎路径未端到端执行 |
| 5 TouchLegacy | 完整 | 部分 | **approximate（语义覆盖完整，精度近似）**：E2E 三场景齐全，但 `OverlapQuery` 按设计是球体近似，且 E2E 走 mock adapter。**限定语**：这里的「完整」只指 **legacy 语义覆盖完整**（文档承诺的输入/输出/触发都实现了），与 325 口径的 `approximate` **不矛盾**——后者指**精度**：`OverlapQuery` 用世界位置球心 + `max(|scale.x|,|scale.y|,|scale.z|)` 半径，与形状/旋向无关（`KX_GameAdaptersEngine.cpp:1034-1088`）。分档**以 CAP 为准**，记 `approximate`。 |

一句话：**不是"没做"，而是"做了但测得不全"**。以下逐项给出证据与缺口，不代为补做。

---

## 任务 3：LogicGate + 调度器 trigger 快照

### 存在证据

| 内容 | 证据 |
| --- | --- |
| 快照栈 | `KX_ExecutionContext.cpp:132` `PushTriggerSnapshot`、`:137` `PopTriggerSnapshot`、`:144` `TriggerSnapshot` |
| 调度器集成 | `KX_ExecutionContext.cpp:265-290`：节点 `NeedsTriggerSnapshot()` 时先锁存队列里同节点的后续条目，按 `plan_->trigger_input_offsets` 构造 `std::vector<bool> bits`，`PushTriggerSnapshot(bits)` → `Execute` → `PopTriggerSnapshot()` |
| 节点接口 | `KX_FunctionNode.cpp:1432-1435` `NeedsTriggerSnapshot()` 返回 `spec_->needs_trigger_snapshot`；声明见 `KX_FunctionNode.h:106,123` |
| 注册宏 | `KX_FunctionNode.cpp:1245-1248` `TRIGGER_SNAPSHOT_NODE`，末尾 `true` 即 `needs_trigger_snapshot` |
| 节点注册 | `KX_FunctionNode.cpp:1355` `TRIGGER_SNAPSHOT_NODE("LogicGate", kLogicGateInputs, kLogicGateOutputs, FuncLogicGate)` |
| 端口契约 | `KX_FunctionNode.cpp:1003-1010`：输入 `A`/`B` 均为 `TP`（trigger），输出 `Result(Bool)` / `OnTrue` / `OnFalse` |
| 实现 | `KX_FunctionNode.cpp:1013-1044`：读取 `ctx.TriggerSnapshot(0/1)`，对 op 字符串转小写后分派 |
| 编辑器注册 | `scripts/startup/bge_gamenodes/editor/nodes/native_batch.py:75-85` 六个 op 枚举；`:327-330` 表格行 `('LogicGate', ..., [('A','Condition'),('B','Condition')], [('Result','Bool'),('OnTrue','Condition'),('OnFalse','Condition')], ...)` |

### op 覆盖

C++ 实现与编辑器枚举均为 **6 个 op**：`and`（默认）、`or`、`xor`、`not`、`nand`、`nor`（`KX_FunctionNode.cpp:1019-1039`；`native_batch.py:75-85`）。"四个 op（and/or/xor/not，含 nand/nor）"的说法可解释为 4 个基础 + 2 个组合，实际注册齐全。

### E2E 断言

- `tests/b661_logicgate.cpp:265-290` 单测 **6 条**（A-only ×2、AB ×2、跨帧 ×2）；`:301-318` 矩阵 **4 op × 4 组合 = 16 条**。
- 合计 **22 条断言，全部 PASS**：`/tmp/b661_cross_run_b661_logicgate.log` 尾行 `RESULT assertions=22 failures=0`。
- **缺口**：`nand` / `nor` 在 harness 中无任何用例（`grep -n "nand\|nor" tests/b661_logicgate.cpp` 无命中）。

### 性能回归

- `tests/b661_perf.cpp:3-4`：图只包含**不得**进入触发快照的节点（`OnUpdate → Branch`、`Delay`、`Sink`）。
- `:94` `kFramesPerRound = 1000`，20 轮；`:137` 只 `printf` 出 `best/avg`。
- **缺口**：没有任何自动阈值断言（`grep assert/Check/0.05` 无命中）。
- 历史人工对比在 ±5% 内：`304.070→308.879`、`301.696→308.499`、`307.697→306.395`（`/tmp/b661_perf_compare.log`）。本轮交叉运行 `477.806 µs/1000 帧` 因链接对象集合不同，**不可比**。
- 2026-11-05 复核（详见下方「性能值复核」）：`312.376` 的同一二进制（md5 `a0200…`）按同协议 `frames=1000 × rounds=20 × best` 在快段复跑 10 次为 `310.142–320.772`、中位 `313.027`，与 `312.376` 差 `+0.21%`，**可复现**；`327.385` 来自另一链接集（md5 `be2ed…`，中位 `328.466`），属代码布局差异，不能当作 `312.376` 的回归。

### 旧 IR 契约

需要重新导出，且已写入文档：

- `docs/B6_LEGACY_COMPAT.md:6` 升级须知标题；`:12` 旧 IR 不能直接复用、会在编译期被拒绝；`:16` 具体错误 `Output port count mismatch for 'LogicGate': IR declares 1, registered signature has 3`；`:21-22` 处理办法为重新导出；`:48` 再次强调 LogicGate 是唯一例外。

### 缺口与工作量

| 缺口 | 补法 | 估算 |
| --- | --- | --- |
| `nand`/`nor` 无 E2E | 在 `tests/b661_logicgate.cpp:301-318` 矩阵扩 2 op × 4 组合 = 8 条 | < 0.5 天（只重编 harness） |
| 无 ±5% 自动断言 | 在 perf harness 内加基线/阈值比较；须在 clean rebuild 后的统一二进制上跑 | 0.5 天 + 等 clean rebuild |

---

## 任务 4：WorldPositionLegacy

### 存在证据

| 内容 | 证据 |
| --- | --- |
| 反投影实现 | `KX_GameAdaptersEngine.cpp:635-686` |
| 相机逆矩阵 | `:656` `GetWorldToCamera()`；`:660` `world_to_camera.inverse()` 写入 `model_inverse` |
| 投影矩阵 | `:657` `GetProjectionMatrix()`；`:661` 写入 `projection_value` |
| 反投影 | `:669-672` 对近裁 `z=0`、远裁 `z=1` 各调一次 `GPU_matrix_unproject_3fv` |
| 方向 | `:675` `normalize(far_world - near_point)` |
| 深度乘 | `:680-683` `eye + lateral + direction * depth` |
| 节点 | `KX_FunctionNodesB6LegacyQuery.cpp:58-67` 端口（`OnEnter/ScreenX/ScreenY/Depth → OnDone/WorldPos`）；`:69-84` `FuncWorldPositionLegacy`；`:81` 调 `RayCastFromScreenAnyDepth`；`:183-186` 注册 |
| E2E | `tests/b661_query_e2e.cpp:390-477` |

结论：**实现方式与要求一致**（相机逆矩阵 + `GPU_matrix_unproject_3fv` + 方向归一化 + 深度乘）。

### E2E 三态

- 正常深度 `10`：`:450` 打印，`:468` 断言"相机 (0,0,5) 朝 -Z，深度 10 → (0,0,-5)"。
- 深度 `0`：`:454` 打印，`:471` 断言"返回相机位置 (0,0,5)"。
- 深度为负 `-2`：`:458` 打印，`:473` 断言"落在相机后方 (0,0,7)"。
- 另有 `:463` "每个 trigger 都查询 adapter"、`:475` "每个分支 OnDone 都触发"。
- 成本文件：`/tmp/b661_cross_run_b661_query_e2e.log` 尾行 `RESULT failures=0`（该文件含 WorldPosition + Touch，共 41 条）。

### 缺口

`tests/b661_query_e2e.cpp:306-317` 的 mock adapter 直接返回 `camera_position + camera_forward * depth`，**没有执行真实的 `GPU_matrix_unproject_3fv` / 相机逆矩阵代码**。即：节点连线、深度传递、输出槽位已被验证；引擎数学路径只有静态代码证据。

### 缺口与工作量

| 缺口 | 补法 | 估算 |
| --- | --- | --- |
| 真实反投影路径未端到端执行 | 增加一个绑定真实 `KX_Scene`/`KX_Camera` 的 E2E（或 bpy 侧相机用例），核对已知屏幕点的世界坐标 | 1–2 天 |

---

## 任务 5：TouchLegacy

### 存在证据

| 内容 | 证据 |
| --- | --- |
| 节点端口 | `KX_FunctionNodesB6LegacyQuery.cpp:90-101`（`OnEnter/ScreenX/ScreenY → OnDone/Target/Objects/Point/Normal`） |
| 实现 | `:128-170` `FuncTouchLegacy`；`:135` `overlap_threshold` 默认 `0.1`；`:151` `RayCastFromScreen`；`:155` `OverlapQuery`；`:163-169` 输出 Target/List/Point/Normal/OnDone |
| 注册 | `:187-190` |
| 重叠查询 | `KX_GameAdaptersEngine.cpp:1034-1088` |
| 球近似 | `:1023-1033` 注释明确："physics world 没有点重叠入口……把每个对象当作以世界位置为中心、最大世界缩放分量为半径的球体，这是对碰撞体的近似"；`:1065-1067` 半径计算；`:1070` `distance > threshold + radius` 跳过 |
| 命中优先 | `:1042-1052` 命中对象先以精确射线点/法线回调；`:1076-1082` 其余对象取球面最近点 |

结论：**`OverlapQuery` 是球近似，不是真实碰撞体/网格重叠检测**，且这一点在源码注释中明确声明，不是实现缺陷，而是设计取舍。若产品语义要求"真重叠"，需另立设计变更。

### E2E 三场景

`tests/b661_query_e2e.cpp:479-593`：

- 命中单对象：`:572` `RunTouchCase("single target", true, a, {})`，`:573-576` 断言 Target=A、Point 为 A 表面 `(0,0,4.5)`。
- 空白：`:580` `RunTouchCase("empty space", false, nullptr, {})`，`:582` 断言 Objects 列表为空；`:563` 说明空场景也触发 OnDone。
- 重叠对象：`:589` `RunTouchCase("overlapping objects", true, a, {{b, 0.02f}})`，`:590` 断言列表含 2 个对象，`:592` 断言顺序为射线命中对象在前，`:593` 断言 Target 仍为 A。

### 缺口

- harness 使用可编程 mock adapter（`:322-380`），**真实场景对象遍历 + 球半径计算路径未执行**。
- `Point/Normal` 在空白场景下的取值（当前实现为 `(0,0,0)`）没有被专门断言。

### 缺口与工作量

| 缺口 | 补法 | 估算 |
| --- | --- | --- |
| 真实球体重叠路径未端到端执行 | 真实场景放 2 个 `KX_GameObject`（不同缩放），核对命中集合与顺序 | 1 天 |
| 若需真碰撞体重叠（非球近似） | 接入物理世界 overlap 查询，替换球近似；属语义变更 | 2–3 天 |

---

## 17 个 harness 的断言数与最后结果

计数已扣除汇总行（如 `PASS total failures=0`、`B6.6 harness: ...`）。除 b61/b62 外为本轮 19:17–19:20 交叉运行结果；b61/b62 已按扩展后的 harness 更新（见行内注），旧数字作废。

| # | harness | 断言数 | 最后结果 | 日志 |
| --- | --- | --- | --- | --- |
| 1 | `reg_b2_b3` | 15 | PASS，failures=0 | `/tmp/b661_cross_run_reg_b2_b3.log` |
| 2 | `reg_b4_e2e` | 46 | PASS，failures=0 | `/tmp/b661_cross_run_reg_b4_e2e.log` |
| 3 | `p41_rebuild` | 23 | PASS，failures=0 | `/tmp/b661_cross_run_p41_rebuild.log` |
| 4 | `p42_hot_reload` | 9 | PASS，failures=0 | `/tmp/b661_cross_run_p42_hot_reload.log` |
| 5 | `p43_trace` | 9 | PASS，failures=0 | `/tmp/b661_cross_run_p43_trace.log` |
| 6 | `p44_stability` | 36 | PASS，failures=0 | `/tmp/b661_cross_run_p44_stability.log` |
| 7 | `b61_e2e` | 26（旧 23 + `wheel_names` 3） | PASS，failures=0 | `/tmp/a1_clean_run_b61_e2e.log` |
| 8 | `b62_e2e` | 68（旧 51 + v4 音频 17） | PASS，failures=0 | `/tmp/a1_clean_run_b62_e2e.log` |
| 9 | `b63_nodes_e2e` | 36 | PASS，failures=0 | `/tmp/b661_cross_run_b63_nodes_e2e.log` |
| 10 | `b63_network_e2e` | 17 | PASS，failures=0 | `/tmp/b661_cross_run_b63_network_e2e.log` |
| 11 | `b64_e2e` | 61（62 − 1） | PASS，failures=0 | `/tmp/b661_cross_run_b64_e2e.log` |
| 12 | `b65a_e2e` | 29（30 − 1） | PASS，failures=0 | `/tmp/b661_cross_run_b65a_e2e.log` |
| 13 | `b661_e2e` | 19 | PASS，failures=0 | `/tmp/b661_cross_run_b661_e2e.log` |
| 14 | `b661_logicgate` | 22 | PASS，failures=0 | `/tmp/b661_cross_run_b661_logicgate.log` |
| 15 | `b661_query_e2e` | 41 | PASS，failures=0 | `/tmp/b661_cross_run_b661_query_e2e.log` |
| 16 | `b661_bridge` | 6（7 − 1） | PASS，failures=0 | `/tmp/b661_cross_run_b661_bridge.log` |
| 17 | `b661_perf` | 0（仅打印 `PERF`） | 无断言（按设计不输出 PASS/FAIL 或 `RESULT assertions=`，不计入任何断言合计） | `/tmp/b661_cross_run_b661_perf.log` |
| 附 | `b45_gui`（bpy 脚本，非 C++ harness） | — | 日志 `DONE` 后正常退出 | `/tmp/b661_cross_run_b45_gui.log` |

---

## 无法确认

1. **真实引擎反投影/重叠路径的运行时行为**：缺一个绑定真实 `KX_Scene`/`KX_Camera`/`KX_GameObject` 的 E2E；现有 harness 全部使用 mock adapter。
2. **绝对性能值的机器依赖性**：`b661_perf` 只输出绝对 `best/avg µs`，本机 `scaling_governor=powersave`、逐核频率在 1.1–4.5 GHz 波动，同一二进制同协议会落进 `310–321`（快段）或 `446–486`（慢段）两段（约 `1.54×`）。`312.376` 可在快段复现（10 次中位 `313.027`）；但跨二进制（`477.806`、`327.385`）仍不能与它直接比较。正式性能判据应只断言同进程同二进制内的比值（`tests/verify_gap/b661_perf_v.cpp` 的 `ratio_*`）。
3. **其他 harness 未重跑**：除 2026-11-05 的 `b661_perf` 专项复核外，本轮未重跑其他 harness；表中断言数来自源码静态计数与既有日志。

## 验证补齐结果（2026-11-05）

本节只回答 A.2 审计中任务 3/4/5 的 partial 项。所有新产物在 `/tmp/verify_gap/`，
未改动原 harness `tests/b661_logicgate.cpp`、`tests/b661_query_e2e.cpp`、`tests/b661_perf.cpp`。

| partial 项 | 状态 | 证据 | 说明 |
| --- | --- | --- | --- |
| LogicGate 的 `nand`/`nor` 无 E2E 用例 | **closed** | `tests/verify_gap/b661_logicgate_v.cpp`、`/tmp/verify_gap/b661_logicgate_v.log` | 补 `nand`/`nor` × 4 组输入，断言数 22 → 30，`RESULT assertions=30 failures=0`。期望值：`nand` = A∧B 取反（只给 A 或只给 B 时为 1，AB 为 0）；`nor` = A∨B 取反（A/B/AB 均为 0）；两者在 A、B 都未触发时不执行，`OnDone` 不落。 |
| 性能脚本只有 printf、无 ±5% 断言 | **closed（临时基线）** | `tests/verify_gap/b661_perf_v.cpp`、`/tmp/verify_gap/perf_baseline_v.txt`、`/tmp/verify_gap/b661_perf_v_first.log`、`/tmp/verify_gap/b661_perf_v_second.log` | 同进程测三张图：`plain`（仅非快照节点）、`gate`（仅 LogicGate，快照路径）、`mixed`（LogicGate 与非快照节点同图）。只断言比值（绝对耗时不跨链接集可比）：`ratio_gate_vs_plain=1.0254`、`ratio_mixed_vs_plain=1.8618`；第二次运行 `0.9950 / 1.8595`，两条 ±5% 断言 PASS（`RESULT assertions=3 failures=0`）。用伪造基线 `ratio_gate_vs_plain=2.0` 验证过 FAIL 路径（退出码 1）。**该基线是临时值，等 A.4 正式基线产出后必须替换**；基线文件里已注明。 |
| WorldPosition 的真实 `GPU_matrix_unproject_3fv` 路径从未执行 | **closed（真实引擎路径已跑通）** | `tests/verify_gap/make_worldpos.py`、`tests/verify_gap/check_worldpos.py`、`/tmp/verify_gap/worldpos_real.blend`、`/tmp/verify_gap/worldpos_real_final.log:32`（`blenderplayer` 重跑，2026-11-05 21:47 CST） | 以 `blenderplayer` 跑真实引擎：真实 `KX_Scene` + `KX_Camera` + canvas + 真实 `GPU_matrix_unproject_3fv`。相机 `(0,0,5)`、50mm/36mm、16:9；探针 `(0,0)`（ndc −1/−1）depth 0 与 depth 10。手算 `(−3.327339, −1.871628, −4.242609)`，实测 `(−3.327340, −1.871630, −4.242610)`，最大误差 `1.679e-6`（**精度 3 位有效数字，源自 `check_worldpos.py` 的 `max_err={:.3e}` 打印**，见 `worldpos_real_final.log:32`）；depth 0 实测 `(0,0,5)`，误差 `3.353e-8`。`RESULT assertions=2 failures=0`（`worldpos_real_final.log:34`，容差 1e-4）。 |
| Touch 的球近似路径未由真实 adapter 执行；无真碰撞体重叠 | **still-partial** | 见下方说明与 `docs/B6_LEGACY_COMPAT.md` §8 新增条目 | 未接真重叠，也未在真实引擎里跑球体遍历。原因与结论见下。 |

### 真实路径的三个运行前提（WorldPosition）

1. **必须显式设置活动相机**：standalone `blenderplayer` 下 `BL_DataConversion.cpp:1425-1432` 只在
   `rv3d->persp == RV3D_CAMOB` 时才把 `scene.camera` 设为活动相机；否则引擎走
   `KX_KetsjiEngine::PostProcessScene` 的 `__default__cam__` 回退（位置取自 view3d 的 `viewinv`）。
   图里用 `ObjectByName("Camera") → SetActiveCamera` 才能在真实路径上用到自己的相机。
   第一次实验没设活动相机，depth 0 返回的是 `__default__cam__` 的位置 `(15.03,-6.70,8.20)`，
   不是相机位置；这与“反投影错误”不同，属于相机未生效。
2. **首帧投影矩阵未初始化**：`KX_Camera` 默认构造的 `m_projection_matrix` 不是零/单位阵，
   第一帧 `hasValidProjectionMatrix()` 为假（渲染路径尚未写入）。首帧观测值是无意义值；
   从第二帧起稳定。用例用 `Delay(0.3s)` 起测，规避首帧。
3. **窗口尺寸必须与场景一致（1920×1080）**：本次重跑验证，`-w 800 600`（4:3）会改变 NDC 映射，
   depth 10 得到另一组值 `(-3.28292, -2.46219, -4.11922)`，`max_err` 远超 1e-4；
   `-w 1920 1080` 或省略 `-w` 可稳定复现 `1.679e-6`。
   重跑日志头已记录启动命令与 `worldpos_real.blend` 的 sha256（`worldpos_real_final.log:1-4`）。

结论：headless 环境**不需要 GPU 矩阵上下文**（`GPU_matrix_unproject_3fv` 是纯 CPU 数学），
但**投影矩阵由渲染路径每帧写入**，所以必须在引擎跑起来、进入第二帧之后调用，不能只用 mock。

### Touch 为什么仍为 partial

- `PHY_IPhysicsEnvironment` / `CcdPhysicsEnvironment` 侧存在 `btGhostObject` 与
  `getOverlappingPairCache()`，但只用于 **Collision 触发器的内部配对**（`CcdPhysicsEnvironment.cpp:394-397`、
  `:1430` 附近的 trigger 配对遍历），没有暴露给适配器的同步 “点/球重叠” 查询。
  `KX_PhysicsAdapter` 只有 `Raycast`、速度/冲量/力/扭矩、WorldBounds 等，没有 `contactTest` 类入口。
- 若把 `OverlapQuery` 换成 Bullet 真重叠，需要新增物理适配器接口，并把语义从
  “射线命中点附近的对象（含无碰撞体对象）” 改成 “本帧与某刚体接触的刚体”，
  这是设计变更而非验证补齐。因此当前保留球近似，并在 `docs/B6_LEGACY_COMPAT.md` §8 写明限制。

### 契约脚本（本次未改节点契约，仅记录当前值）

- `port_contract.py`：`native_nodes=223 catalog_nodes=68 errors=0 warnings=4`（4 条均为历史命名差异，索引对齐）。
- `enum_consistency.py`：`checked_pairs=108 inconsistencies=0`。
- 日志：`/tmp/verify_gap/port_contract.log`、`/tmp/verify_gap/enum_consistency.log`。

### 性能值复核（2026-11-05，b661_perf）

**协议标签**：`frames=1000 × rounds=20 × best(min)`。源码：`tests/b661_perf.cpp:94-95`
（`kFramesPerRound=1000`、`kRounds=20`）、`:115-118`（200 帧预热）、`:120-143`
（每轮计时、`best=min`、打印 `PERF`）。构建：`tests/a1_clean_harness.sh:23-31`
的 `-O1 -std=c++20 -march=x86-64-v2` 与 `:55-58` 的链接命令，对象来自
`build_clean_verify/lib/libge_logic_bricks.a`。

复现方法：直接运行产出 `312.376` 的同一二进制 `/tmp/a1_clean_b661_perf`
（md5 `a0200513869049a7e5970213ceb55111`）；另外从 clean 归档重新解包对象、
用同一条链接命令重建，重建产物 md5 **逐字节相同**（`/tmp/c4_recheck_b661_perf`）。
原始证据：`/tmp/c4_perf_recheck.log`。

| 二进制 / 运行方式 | 次数 | `best_us`/1000 帧 | 中位 | 说明 |
| --- | --- | --- | --- | --- |
| `a1_clean`（md5 `a0200…`），不绑核 | 24（A 7 + B 7 + D 10） | `310.142–485.972`，明显双峰 | 快段 10 次 `313.027`；慢段 13 次 `482.686` | 同一二进制、同一协议，快/慢两段约 `1.54×`；快段 spread `3.4%`，慢段除 1 次 `446.208`（−7.6%）外为 `478.688–485.972`（`1.5%`） |
| `b661_cross`（md5 `be2ed…`），不绑核 | 7 | `327.625–333.686` | `328.466` | 另一链接集合（二进制体积 `1.36 MB` vs clean `3.24 MB`）；spread `1.85%` |
| `a1_clean`，`taskset -c 2` | 6 | `550.643–553.108` | `552.121` | 绑到慢态核；spread `0.45%` |
| `b661_cross`，`taskset -c 2` | 6 | `577.563–585.238` | `584.067` | 同上；spread `1.33%` |
| `a1_clean`，逐核 0/1/2/3/4/6/8/10/12/14 | 10 | `311.585–319.871` | — | 全部落在快段；此时绑核也不慢，说明慢段是**时间窗口**而非固定核 |

- 每次运行前后记录 `/proc/loadavg`（`1.18–3.67` / 16 核），全量见
  `/tmp/c4_perf_recheck.log`。`loadavg` 与快/慢段**无对应关系**：D 组 loadavg
  `3.67` 时仍 10/10 跑出 `310–321`；判据是同一二进制多次运行的离群幅度。
- 慢段成因：本机 `scaling_governor=powersave`，逐核 `scaling_cur_freq` 现场采样在
  `1.1–4.5 GHz` 间波动（`/sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq`）；
  同一二进制同一协议可整进程落在慢段，属**机器电源/频率状态**，不是轮间噪声。
- **结论**：
  1. `312.376` **可复现**：同一二进制快段 10 次 `310.142–320.772`、中位 `313.027`，
     与 `312.376` 差 `+0.21%`（落在 ±1% 内）。不降级为“历史单次观测”。
  2. 但同一二进制也会整进程落进慢段（中位 `482.686`，约 `1.54×`），所以 `312.376`
     **绑定了“快段”这一机器状态**：引用时必须带协议与电源状态标签，不能当作任意
     时刻可得的绝对值。
  3. `327.385` 是本机另一链接集合的中位 `328.466` 的同类观测（spread `1.85%`）；
     它与 `312.376` 的 `+5.1%` 差异来自**链接对象集/代码布局**，不是同二进制重复
     运行的噪声。按既有“不同链接集不可比”的口径，不能把 `327.385` 当成 `312.376`
     的性能回归。
  4. `312.376` 的权威记载在 `A1_BUILD_AUDIT.md:218,227`（并已注明“clean 链接集，
     与 A2 的 `477.806` / `~305` 不可比”）；本文补记协议、复现数据与机器状态约束。
  5. 绝对 µs 对调频状态敏感，性能回归判据只应使用**同进程同二进制内的比值**
     （`tests/verify_gap/b661_perf_v.cpp` 的 `ratio_gate_vs_plain` /
     `ratio_mixed_vs_plain`），不同二进制之间只报“不可比”。
- **断言口径**：`b661_perf` 只打印 `PERF frames=… rounds=… best_us…`，源码
  `tests/b661_perf.cpp:137-143` 无任何 `PASS/FAIL` 或 `RESULT assertions=`，按设计
  **0 条断言**，不计入全量断言批次（**46 批 / 1456 条**的口径见 `NUMBER_CONSISTENCY_AUDIT.md` B.5；J 块 26 批 / 851 为历史）。

## 约束遵守

- 未修改任何 C++ / Python 源码；
- 未执行 `cmake`、`ninja`、未删除 `build_linux` 任何内容；
- 未 `git commit/push`，未触碰 `_lib.glsl`；
- 本轮唯一写入仓库的文件是本文档。
- 2026-11-05 性能值复核轮同样只写本文档：所有重跑与重链接产物都在 `/tmp`
  （`/tmp/c4_perf_recheck.log`、`/tmp/c4_recheck_b661_perf`），未改源码、未碰 `tests/`、
  未动 `build_linux` 与 `build_clean_verify`。
