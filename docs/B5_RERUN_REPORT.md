# B5 补跑报告（B6.6 任务 1）

日期：2026-11-05
项目：`/home/eridanusepsilon/Code/EBC/EBC5.3`
范围：P1 / P2 / P3 / P3.5 四个最底层批次，行为回归验证（不逐字节比对）
结论：**4/4 PASS，未发现引擎行为退化**；本次仅新增测试与文档，未修改任何 gameengine 节点源码。

## 1. 结论速览

| 批次 | 断言数 | 失败 | 关键行为 | 重建 vs 历史 |
|---|---|---|---|---|
| P1 runtime | 76 | 0 | FloatMath 7 运算、VectorMath 7 运算、Branch、Delay 0.5s、Raycast、E2E 4×2→8 第 3 帧触发一次 | 一致 |
| P2 asset | 40 | 0 | 42 写入→下一帧读回→×2=84；共享资产两读者 refcount=3；卸载持有中资产仍可读；归零清空 | 一致 |
| P3 pipeline | 23 | 0 | 导出 IR（7 节点/6 连线）→编译→3 帧 Sink=3；priority 排序稳定；3 次重载 refcount 1→2→1 | 一致 |
| P3.5 input/physics | 53 | 0 | 键盘/鼠标边沿、Collision 四态、SetPosition/SetLinearVelocity 4 帧、Near 边界含等号 | 一致 |

合计 192 条断言，0 失败。一键日志：`/tmp/b661_b5_rerun.log`（构建日志 `/tmp/b661_b5_rerun_build.log`）。

## 2. 交付物

```
tests/
├── run_b5_rerun.sh                 # 一键构建 + 导出 + 跑 4 个 harness
└── b5_rerun/
    ├── b5_harness.h                # 共用断言、IR 构建、plan/offset/sink 读取辅助
    ├── b5_stubs.cpp                # 未参与本批的节点族注册符号桩 + Raycast 默认实现
    ├── Makefile                    # 独立可执行文件构建（链接未修改的引擎源码）
    ├── p1_harness.cpp              # P1
    ├── p2_harness.cpp              # P2
    ├── p3_harness.cpp              # P3
    ├── p3_input_harness.cpp        # P3.5
    ├── p3_export.py                # 真实 ir_export.py + mock bpy 数据，产出 P3 manifest
    └── out/p3_manifest.json        # 导出产物（可重新生成）
docs/B5_RERUN_REPORT.md             # 本报告
```

复跑：

```bash
cd /home/eridanusepsilon/Code/EBC/EBC5.3
./tests/run_b5_rerun.sh            # 退出码 0 表示 4/4 PASS
```

## 3. 基线说明（必须首先声明）

任务指定的 `docs/P1_RUNTIME.md`、`docs/P2_ASSET.md`、`docs/P3_PIPELINE.md`、`docs/P3.5_INPUT.md` **在仓库工作区、`git log --all` 历史、`/tmp` 中均不存在**；B5 时期的 `/tmp` harness 也已丢失。因此本次不存在可逐条比对的“历史断言”。

“重建 vs 历史一致”的判定基线改为：

1. 节点源码中的显式契约（`KX_RuntimeNodes.cpp`、`KX_FunctionNode.cpp` 的签名、运算分支、触发条件）；
2. 编译/执行链源码中的固定帧序（`BeginFrame → TickStatefulNodes → ExecuteValueNodes → PropagateTriggers → EndFrame`）；
3. 既有审计证据（B5 审计条目数不变、源文件未改）作为历史侧旁证。

因此本报告的“一致”指**行为契约一致**，不是历史日志的逐字节回放。这是本任务最大的残留风险，已在第 6 节如实列出。

## 4. 重建过程

### 4.1 构建方式

`build_linux/bin/blender` 当前缺失，源码树处于不可链接的中间态（`ninja blender` 在 `KX_FunctionNodesB6.cpp`、`KX_GameAdaptersEngine.cpp`、`KX_KetsjiEngine.cpp` 处失败）。受“不改 gameengine 节点源码”约束，无法修复整树构建。改用独立编译：

- 直接编译 `source/gameengine/GameLogic/GameNodes/` 下 29 个运行期/编译期源文件（清单见 [Makefile](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b5_rerun/Makefile)）；
- 链接参数 `-Wl,--unresolved-symbols=ignore-all`，只补齐未参与本批的 `Register*FunctionGameNodes` 符号桩；
- 测试侧提供 `KX_ExecutionContext::Raycast`（头文件注释本就约定由测试提供）。

### 4.2 各 harness 的用例来源与结构

**P1**（[p1_harness.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b5_rerun/p1_harness.cpp)）

- 直接单元：`KX_Node_FloatMath`（Add/Sub/Mul/Div/Min/Max/Pow，除零→0 且 `Warning()` 置位）、`KX_Node_VectorMath`（Add/Sub/Mul/Dot/Cross/Normalize/Length，含零向量归一化）、`KX_Node_Branch`（真/假/无输入触发）、`KX_Node_Delay`（0.5s，dt=0.25：触发帧不完成、0.25s 不完成、0.5s 完成、只触发一次）、`KX_Node_Raycast`（命中/未命中、origin/direction/max_dist 透传、HitPoint/HitNormal）。
- E2E：`OnInit → Branch(cond = 4×2) → Delay(0.5s) → Sink`，`FloatMath → Sink.Value`。断言 8 节点、1 个 tick 节点；第 1/2 帧无触发、第 3 帧恰好 1 次、值 8、之后不重复。

**P2**（[p2_harness.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b5_rerun/p2_harness.cpp)）

- 往返：`OnInit → AssetSet(hp=42)`，poll 模式 `AssetGet → FloatMath(×2) → Branch ← Delay(0.25s)`，`Branch.OnTrue → Sink`。第 1 帧写入 42，第 2 帧读回 42×2=84，Sink 恰好 1 次；编译后 refcount=3（注册表 + 两个节点），销毁实例回到 1，`Unregister` 后 `Get()==nullptr`、`AssetCount()==0`。
- 共享：两个 poll `AssetGet` 同资产，refcount=3，两读者都读到 7；持有中 `Unregister` 后 refcount=2 且仍可读。
- 隔离：两个不同资产，只有被绑定者增引用；`Clear()` 后注册表为空。

**P3**（[p3_harness.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b5_rerun/p3_harness.cpp) + [p3_export.py](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b5_rerun/p3_export.py)）

- 编辑器导出：不修改地加载真实 `scripts/startup/bge_gamenodes/generator/ir_export.py` 的 `export_scene_manifest`，用 mock bpy 数据构造 `B5E2EGraph`（OnUpdate / Keyboard / Branch / Sink / MouseWheel / AssetSet，4 个挂载对象 RunnerA(10)、RunnerB(0)、RunnerC(5)、RunnerD(0)）。
- 导出结果：1 个 graph、4 个 mount、无 warning；7 个节点、6 条连线（`AssetSet.Value` 未连线时 `materialize_constants` 自动补 `ConstFloat 0.0`，故比手写图多 1 节点/1 连线）。
- 编译执行：导出 IR → 编译 → 同 IR 二次编译，`runtime_data_size`/`port_offsets`/`execution_order` 完全一致；无输入设备时 Keyboard=false、OnUpdate 每帧触发，3 帧 Sink=3，Sink 携带 MouseWheel 值 0。
- priority：`stable_sort`（priority 升序，同值按对象名）得到 `RunnerB, RunnerD, RunnerC, RunnerA`；重复解析 manifest 3 次排序不变。
- refcount：注册 `SceneAsset`（1）→ 编译实例（2）→ 销毁（1）→ 3 次“重建/销毁”循环每次 2→1 → `Unregister` 后 0。

**P3.5**（[p3_input_harness.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/b5_rerun/p3_input_harness.cpp)）

- Keyboard：Press 仅首帧、Hold 持续、Release 仅释放帧；MouseButton 同理；MousePosition/MouseDelta/MouseWheel 数值透传。
- Collision：重叠→OnEnter（且仅一次）、持续→OnStay、分离→OnLeave、再次重叠→OnEnter；ContactNormal 主轴 |y|=1、ImpactForce=2（相对速度 (0,-2,0)）。
- Transform：`OnUpdate → SetPosition((1,2,3))`、`SetLinearVelocity((4,5,6))` 各写 4 帧，值逐帧正确。
- Near：距离 3/5/5.5 对 Range=5 → true/true/false，验证源码 `distance <= range` 的闭区间语义。

## 5. Mock 注入与 headless 限制

| 项 | 处理 | 原因 |
|---|---|---|
| Blender 编辑器导出 | mock `bpy` 对象模型，**导出逻辑为真实 `ir_export.py`** | `build_linux/bin/blender` 缺失，整树不可链接 |
| 键盘/鼠标 | `MockInputAdapter` 自行由 `previous_*` 计算 Press/Release 边沿 | headless 无窗口设备 |
| 物理/物体查询 | `MockPhysicsAdapter`（AABB、SetLinearVelocity）、`MockObjectAdapter`（位置/速度/SetLocalPosition） | 无 Bullet 场景 |
| 对象指针 | 以 `0x1000/0x2000` 假指针作不透明 token，绝不解引用 | 不构造真实 `KX_GameObject` |
| priority 排序 | 在 manifest + `stable_sort` 契约层验证 | 无法 headless 构造真实 `KX_Scene` 触发 `OnSceneLoad` |

## 6. 差异与解释（区分“行为变化”与“测试用例不精确”）

### 6.1 行为变化

**无。** 所有关键输出值、触发次数、帧数、refcount 终点均与源码契约吻合；未发现引擎行为退化。

### 6.2 测试用例本身不精确（已按源码契约修正，引擎未改动）

| # | 现象 | 根因 | 处理 |
|---|---|---|---|
| 1 | P1 除零用例最初得到 9 而非 0 | 节点未 `Configure({"op":"Div"})`，默认运算是 Add | 显式配置 Div，验证 0 + Warning |
| 2 | P2 往返最初 Sink 不触发、读到 0 | 触发模式 `AssetGet` 接在值链上；值节点在触发过程之前执行，读到上一帧/初值 | 改为 poll 模式 Get + Delay 门控：第 1 帧写、第 2 帧读回 84（符合固定帧序） |
| 3 | P3.5 Collision 直接读输出触发槽始终为 false | `PropagateTriggers` 对“无触发输入但有触发输出”的起始节点先 `ClearNodeOutputTriggers`，再用空 `Execute` 跑一遍；节点自身槽位被清空 | 改为经 Sink 统计下游投递（tick 阶段已入队，下游正常收到）；见 6.3 |
| 4 | Collision 首帧不触发 | 有状态 tick 在值节点之前执行，`Object` 输入来自上一帧，编译后首帧尚未采样 | 用例改为第 2 帧起验证状态迁移 |
| 5 | ContactNormal/ImpactForce 初值不符 | 假 AABB 三轴穿透深度相同，源码取“最小穿透轴”=X，且相对速度与该轴点积为 0 | 调整 mock AABB 为 Y 向主导重叠，按源码公式期望 \|n.y\|=1、impact=2 |
| 6 | Near 恰在边界 5 判 false | 用例按原点计算距离，但 `SetPosition` 每帧把 owner 移到 (1,2,3) | 改为相对 owner 的坐标 (4,2,3)/(6,2,3)/(6.5,2,3)；源码 `<=` 闭区间确认 |
| 7 | P3 refcount 重载基线偏大 | 二次编译的 `rebuilt` 实例未销毁即进入重载循环 | 在重载前销毁，基线恢复 2→1 |

### 6.3 行为观察（当前实现，非本次引入，未改动）

1. **Collision 的触发输出槽在 Tick 后不可直接观测**：节点自身是“有触发输出、无触发输入”的起始节点，`PropagateTriggers` 会先清空其输出触发，再以空 `Execute` 执行；但 Tick 阶段产生的下游投递已在队列中，因此**下游节点能正常收到 OnEnter/OnStay/OnLeave**。这是调度器语义而非本次测试引入的问题，建议后续在节点/调度文档中明确“状态节点的输出触发只保证向下游投递，不保证槽位可回读”。
2. **Collision 存在 1 帧采样延迟**：tick 在值节点之前，`Object` 输入总是上一帧的值。对连续状态迁移无影响，但“刚装配的第一帧”不会触发。

## 7. 约束合规

- 只新增 `tests/` 与本文档；`git status` 中 gameengine 源文件的改动均为此前任务遗留，本次未触碰。
- 未 `git commit` / `git push`。
- 7 个未跟踪 `_lib.glsl` 未改动。
- 发现差异均记录在案，未擅自修改引擎。

## 8. 证据

- 一键日志：`/tmp/b661_b5_rerun.log`（含构建状态、导出状态、逐 harness 输出与“重建 vs 历史一致”标注、`SUMMARY: 4/4 PASS`）
- 构建日志：`/tmp/b661_b5_rerun_build.log`
- 断言统计：P1 76 / P2 40 / P3 23 / P3.5 53，共 192，0 失败。
