# 物理域重打分（B6.9-P，2026-11-06）

## 0. 口径（本轮统一，原文引用）

> 代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：
> - **full**：分母计入，分子 1.0
> - **approximate**：分母计入，分子 0.5（必须有书面明确限制）
> - **unsupported**：引擎层不支持，附 `路径:行号` 可核证据，进 FOREVER，不进分母
> - **pending-device**：分母计入，分子 1.0，真机待验单列

| 子类 | 定义 | 计入分母 | 计入分子 |
|---|---|---|---|
| **full** | 实现完成 + Headless/Mock E2E 通过 + 无已知限制 | 是 | 1.0 |
| **approximate** | 实现完成 + Headless E2E 通过 + 有明确书面限制 | 是 | 0.5 |
| **unsupported** | 引擎层不支持（附 `路径:行号` 可核证据） | 否（进 FOREVER） | — |
| **pending-device** | 实现完成 + Mock E2E 通过 + 需真实设备验证 | 是 | 1.0（代码侧，真机单列） |

`pending-device` 必须同时满足：① 节点有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless 验证的是具体设备/驱动依赖并给出 API 位置。

## 1. 打分方法与分母

- 本域节点数：**15**（`BuildNavMesh`、`QueryPath`、`VehicleControl` 等 B6.9 物理节点）。
- `unsupported`：**0**（没有引擎层不支持的节点，故分母仍是 15）。
- `pending-device`：**0**（这批节点全部可在 headless 下验证，不依赖真机设备/驱动）。
- 每条分类的证据列给出**代码路径:行号**与**实跑日志**；断言数一律取 `run_all_e2e.sh` 的 `assertions=`。
- 验证层级单独标注：**节点→适配器层**（Mock）或**引擎/管线层**（Bullet / Recast bake）。两者都算 Headless E2E，但不混称「节点→真实引擎场景」。

## 2. 逐节点分类表

| # | 节点 | 分类 | 依据（路径:行号 / 日志行） | 限制说明 |
|---|---|---|---|---|
| 1 | CreateVehicle | **full** | 节点 `KX_FunctionNodesB6.cpp:140`；引擎 `KX_PhysicsAdvancedAdapter.cpp:535-705`；逐轮悬挂 `:658-704`；`b61` 44/0（`tests/.e2e_out/run_20261106-155855/b61.log`） | 无。`suspension` 配置按轮给 `刚度,阻尼,压缩,静长`；down/axle 采用底盘局部约定（`-Z` / `+X`），属既定语义 |
| 2 | VehicleControl | **full** | `KX_PhysicsAdvancedAdapter.cpp:719-740`；`b61` 44/0 | 无。steering/throttle/brake 直接映射（满油门 2000 N 为既定标度） |
| 3 | AddConstraint | **full** | 节点 `KX_FunctionNodesB6.cpp:194-262`；引擎 `KX_PhysicsAdvancedAdapter.cpp:742-960`；`c5` 16/0（`run_20261106-155855`）；`b61` 44/0 | 无。`pivot` / `axis` 配置（世界坐标/轴）已接入，空值回退中点/+Y |
| 4 | RemoveConstraint | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:935`；`b61` 帧 1 断言（44/0） | 边界（非缺口）：只移除本适配器登记的约束；引擎无反向索引 |
| 5 | AddJoint | **full** | 节点 `KX_FunctionNodesB6.cpp:263`；引擎 `KX_PhysicsAdvancedAdapter.cpp:961`；`b61` 44/0（body 图） | 无。节点契约是铰接；限位/马达走 AddConstraint |
| 6 | SetPhysicsMass | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1010`；`b61` 44/0（body 图，mass=0.5） | 无 |
| 7 | SetPhysicsFriction | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1020`；`b61` 44/0（friction=1.0） | 无 |
| 8 | SetPhysicsRestitution | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1030`；`b61` 44/0（restitution=0.25）；行为断言 `c1` 4 项（`run_20261106-131618`） | 无。已有接触流形沿用旧组合值，属 Bullet 语义，非节点缺口 |
| 9 | SetLinearDamping | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1055`；`b61` 44/0（0.3） | 无 |
| 10 | SetAngularDamping | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1065`；`b61` 44/0（0.4） | 无 |
| 11 | SetGravity | **full** | 节点 `KX_FunctionNode.cpp:451-474`（走 `ctx.Physics()`）；引擎 `KX_GameAdaptersEngine.cpp:1206-1210`；节点级 E2E `tests/c8_gravity_e2e.cpp` 25/0（`tests/.e2e_out/run_20261106-162700/c8_gravity.log`） | 无能力限制。非法输入策略见 §5：NaN / inf / 任一分量绝对值 > 1000 一律**拒绝**（逐分量独立判定，不做范数合成；不写引擎、stderr 诊断、节点仍触发 OnDone） |
| 12 | BuildNavMesh | **full** | 节点 `KX_FunctionNodesB6.cpp:333`；烘焙管线 `KX_NavMeshBuilder.h`；`c6` 12/0（`run_20261106-131628`）；`b61` 44/0（agent 0.3/1.0 契约） | 无。验证层级：**烘焙管线层 + 节点契约层**；节点→真实场景层未覆盖（headless 无场景可驱动） |
| 13 | QueryPath | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1111`；`b61` 44/0（3 点） | 输出最多 64 点（固定缓冲，文档化，不改变可达性语义） |
| 14 | RaycastBatch | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1140`；`b61` 44/0（3 条射线/距离 10） | 零方向射线跳过（文档化输入约定） |
| 15 | SetCollisionGroup | **full** | 引擎 `KX_PhysicsAdvancedAdapter.cpp:1190`；`b61` 44/0（group=2/mask=0） | 无 |

## 3. 合计与两个百分比

| 分类 | 节点数 | 权重 | 分子贡献 |
|---|---|---|---|
| full | 15 | 1.0 | 15.0 |
| approximate | 0 | 0.5 | 0.0 |
| unsupported | 0 | 不进分母 | — |
| pending-device | 0 | 1.0（真机单列） | — |
| **分母** | **15** | | |

- **严格口径**（只算 `full`）：**15 / 15 = 100%**
- **新口径**（`full`=1.0、`approximate`=0.5、`pending-device`=1.0）：**15 / 15 = 100%**
- `SetGravity` 的验证缺口已由 `tests/c8_gravity_e2e.cpp` 补上（20/0），严格与新口径同时到顶。

## 4. 与旧严格口径对照

| 口径 | full | approximate | 严格（full 占比） | 加权 |
|---|---|---|---|---|
| 旧（P-1 前，`B6_PHYSICS_ADVANCED.md` §6） | 13 | 2（CreateVehicle 悬挂逐轮、AddConstraint 枢轴/铰轴） | 13/15 = 86.7% | (13+1)/15 = 93.3% |
| P-3.5 后（SetGravity 未 mock，本轮前半） | 14 | 1（SetGravity 验证缺口） | 14/15 = 93.3% | 14.5/15 = 96.7% |
| 本轮（SetGravity mock 后） | 15 | 0 | **15/15 = 100%** | **15/15 = 100%** |
| 旧 → 本轮差值 | +2 | −2 | **+13.3 pp** | **+6.7 pp** |

旧口径把 `CreateVehicle` / `AddConstraint` 记为近似（各 0.5）；这两项在 P-3.5 已补齐并新增行为断言，转为 `full`。`SetGravity` 的 mock 是本轮最后一项：它补的是**验证手段**，不是产品代码能力——mock 只在 `tests/c8_gravity_e2e.cpp` 内定义并注入，运行时仍由 `KX_EnginePhysicsAdapter` 调用真实 `PHY_IPhysicsEnvironment::SetGravity`（`KX_GameAdaptersEngine.cpp:1206-1210`），mock 不进入产品路径。

## 5. 本轮新增/变更证据

- `tests/c8_gravity_e2e.cpp`（新增，已入 `run_all_e2e.sh` MANIFEST）：`GravityPhysics : KX_PhysicsAdapter` 只实现 `SetGravity` / `GetGravity`，无 per-step 钩子（`KX_PhysicsAdapter` 本身没有 step 虚函数；`SetGravity` 在触发帧写入，不需要）。`OnInit → SetGravity` 图逐值回读：`(3,-2,7)` 精确回读、边界 `(±1000,0)` 接受；`(1001,0,0)` 被拒绝且重力保持上一值 `(3,-2,7)`、OnDone 仍触发 1 次；NaN / inf / `1e30` 均被拒绝（`set_calls=0`，重力保持原值），stderr 原文见日志。25/0，`tests/.e2e_out/run_20261106-162700/c8_gravity.log`。
- **非法输入策略（拒绝，不钳制）**：`KX_FunctionNode.cpp:448-474` 在建 `SetGravity` 前校验，任一分量非有限或绝对值 > 1000 时打印诊断并**不调用**适配器；节点仍触发 OnDone，避免图停摆。**判定是逐分量独立进行的，不做范数合成**：`(1000,-1000,0)` 的范数约 1414，但每个轴都在 ±1000 内，所以按设计**接受**；只有某个轴单独越界才拒绝。理由：重力本来就按轴写入 `btVector3`，轴向独立限幅比范数耦合更可预测，也不会把合法的对角重力误判为非法。阈值 `kMaxGravityComponent = 1000` 是文档化的单轴安全上限（约 100 倍地球重力）。
- **mock 隔离**：`GravityPhysics` 定义在 [c8_gravity_e2e.cpp](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/c8_gravity_e2e.cpp#L68-L83)，仅被该 harness 的 `RunCase` 注入；`source/` 下无任何引用（`grep -rn "GravityPhysics" source/` 零命中）。运行时路径仍是 `KX_EnginePhysicsAdapter::SetGravity`（`KX_GameAdaptersEngine.cpp:1206-1210`）。
- `tests/b61_e2e.cpp`：新增逐轮悬挂（4 轮 × 4 参数顺序/数值）、非法悬挂配置（车未创建）、`pivot/axis` 转发断言，以及 AddJoint / SetMass / SetFriction / SetRestitution / SetLinearDamping / SetAngularDamping / SetCollisionGroup / BuildNavMesh 的 body 图，44/0。
- 引擎代码：`KX_PhysicsAdvancedAdapter.cpp:658`（CreateVehicleWithSuspension）、`:839`（AddConstraintWithFrame）、`CcdPhysicsEnvironment.cpp` `SetSuspensionRestLength`、`PHY_IVehicle.h` 接口新增。

## 6. 真机验证（只列清单，不执行）

物理域本批节点均不依赖真实设备；无 `pending-device` 条目。真机清单为空。
