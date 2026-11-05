# B6.1 物理高级（15 节点）

日期：2026-11-05
范围：载具、约束/关节、物理属性、重力、导航网格、路径查询、批量射线、碰撞组
运行时：`KX_FunctionNodesB6.cpp`（表驱动，`kB6Specs` 22 条 + 复用 `SetGravity`）
适配器：`KX_PhysicsAdvancedAdapter.h/.cpp`（接口 + Engine 实现 + Mock/Null）

## 1. 接入位置

| 项 | 文件 | 位置 |
|---|---|---|
| 节点执行函数 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp` | CreateVehicle 88、QueryPath 251、RaycastBatch 278、表 597-674、注册 676 |
| 注册声明 | `KX_FunctionNode.h` | `RegisterB6AdvancedFunctionGameNodes()` 276 |
| 启动注册 | `KX_RuntimeNodes.cpp` | 250 |
| 物理适配器接口 | `KX_PhysicsAdvancedAdapter.h` | 抽象类、Mock、Null/Engine 工厂声明 |
| Null/Mock | `KX_PhysicsAdvancedAdapter.cpp` | Null 65、Mock CreateVehicle 136、Mock QueryPath 342、Mock RaycastBatch 360 |
| Engine 实现 | `KX_PhysicsAdvancedAdapter.cpp` | 类 491、CreateVehicle 495、VehicleControl 618、AddConstraint 641、RemoveConstraints 746、AddJoint 772、SetRestitution 841、SetGravity 877、BuildNavMesh 896、QueryPath 919、RaycastBatch 948、SetCollisionGroup 998、工厂 1054 |
| 上下文注入 | `KX_ExecutionContext.h/.cpp` | `SetAdapters` 末尾参数、`PhysicsAdvanced()` getter |
| 装配 | `KX_GameAdapters.h/.cpp`、`KX_GameAdaptersEngine.cpp` | `KX_AdapterSet::physics_advanced`；Engine 侧 2493 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 232-233、头 243 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6.py` | `NATIVE_B6` 24-115、注册 117 |

## 2. 节点与端口

| 节点 | 输入 | 输出 | config |
|---|---|---|---|
| CreateVehicle | OnEnter, Chassis(Object), Template(String) | OnSuccess, OnFail, Vehicle(Int) | `wheel_names`（逗号分隔，空则走 Template 命名约定） |
| VehicleControl | OnEnter, Vehicle(Int), Steering(Float), Throttle(Float), Brake(Float) | OnDone | 无 |
| AddConstraint | OnEnter, ObjectA, ObjectB | OnDone | `type`(enum point/hinge/slider/cone/generic)、`params`(6 个逗号浮点) |
| RemoveConstraint | OnEnter, Object(Object) | OnDone | 无 |
| AddJoint | OnEnter, ObjectA, ObjectB, Pivot(Vector), Axis(Vector) | OnDone | 无 |
| SetPhysicsMass | OnEnter, Object, Mass(Float) | OnDone | 无 |
| SetPhysicsFriction | OnEnter, Object, Friction(Float) | OnDone | 无 |
| SetPhysicsRestitution | OnEnter, Object, Restitution(Float) | OnDone | 无 |
| SetLinearDamping | OnEnter, Object, Damping(Float) | OnDone | 无 |
| SetAngularDamping | OnEnter, Object, Damping(Float) | OnDone | 无 |
| SetGravity | OnEnter, Gravity(Vector) | OnDone | 无（复用已有节点） |
| BuildNavMesh | OnEnter | OnSuccess, OnFail | `agent_radius`、`agent_height` |
| QueryPath | OnEnter, From(Vector), To(Vector) | OnSuccess, OnFail, Waypoints(List) | 无 |
| RaycastBatch | OnEnter, Origins(List), Dirs(List), Distance(Float) | Hits(List), Points(List) | 无 |
| SetCollisionGroup | OnEnter, Object, Group(Int), Mask(Int) | OnDone | 无 |

`SetGravity` 已由 `KX_FunctionNode.cpp` 的 `kSpecs` 与 `native_batch.py` 提供，端口完全一致，B6.1 不重复注册（避免 `GameNodeSetGravity` 的 `bl_idname` 冲突）。因此验收口径为 22 个新节点 + 1 个复用节点 = 23。

## 3. 实现要点与真实限制

### 3.1 载具（CreateVehicle / VehicleControl）

- 复用引擎已有的 `PHY_IPhysicsEnvironment::CreateVehicle` + `PHY_IVehicle`（`btRaycastVehicle` 封装），没有第二套车辆物理。
- 轮组解析有两条路，`wheel_names` 优先：
  - **`wheel_names` 非空**：逗号分隔的物体名，按顺序逐个解析（会去掉首尾空白）。任何一个名字在场景里找不到、或该物体没有物理控制器，整次创建失败，stderr 写明**具体是哪个名字**，不会静默少轮；
  - **`wheel_names` 为空**：回退旧的 `Template` 命名约定——按名字找轮组物体，再递归收集其有物理控制器且为动态的子物体。旧图行为不变。
- 找不到底盘、底盘非动态、或轮组为空时返回失败并写 stderr，不会留下半成品车辆（引擎没有公开的「删除车辆」接口，所以校验全部在创建之前完成）。
- 轮子的连接点/悬挂方向/轮轴由底盘世界变换换算到局部空间；悬挂长度固定 0.2，半径取轮子控制器半径。`wheel_names` 只解决「哪些物体是轮子」，不解决逐轮悬挂参数（仍属近似）。
- `VehicleControl` 的归一化：`Steering` 原样传给每个轮子；`Throttle` 乘 2000 作为引擎力；`Brake` 乘 100 作为制动力。两者都按 0..1 归一化输入，超出 1 会被引擎饱和。

### 3.2 约束与关节（AddConstraint / RemoveConstraint / AddJoint）

- 类型映射：`point`→`PHY_POINT2POINT`、`hinge`→`PHY_LINEHINGE`、`cone`→`PHY_CONE_TWIST`、`generic`→`PHY_GENERIC_6DOF`。
- **`slider` 没有原生约束**：用 `PHY_GENERIC_6DOF` 并把非滑动轴锁死来近似，是近似实现，不是真正的 slider 限制。
- `params[6]` 由节点 config 的逗号浮点串解析，按约束类型传给 `PHY_IConstraint::SetParam`；非法串直接失败并诊断。
- 枢轴取两个物体世界位置的中点并换算到 A 的局部空间；`AddJoint` 的 `Pivot`/`Axis` 端口按世界空间输入，再换算到 A 局部建 `PHY_LINEHINGE`。
- `RemoveConstraint` 按物体清理该适配器创建的约束 id；引擎侧没有按物体反查约束的接口，适配器内部维护 id/owner 列表。

### 3.3 物理属性（Mass / Friction / Restitution / Damping）

- Mass/Friction/Damping 直接调用 `PHY_IPhysicsController` 的 setter，对象为空或控制器为空时失败并诊断。
- **Restitution 支持运行时修改**（C.1.1）：`PHY_IPhysicsController` 新增窄接口 `bool SetRestitution(float)`（声明在接口末尾，保证既有虚函数槽位不变），`CcdPhysicsController` 的实现直接对**现有** `btRigidBody` 调 `setRestitution()` 并同步 `m_cci.m_restitution`。**不 remove/re-add 刚体**，因此接触点、约束、速度都保留。
  - 取值范围：适配器限制 `[0, 1]`，越界即失败并诊断；
  - 生效时机：Bullet 在**新建流形接触点**时用两个刚体回弹值的乘积算 `m_combinedRestitution`。新碰撞一定用新值；持续接触的旧点保留旧值直到刷新（有界滞后，不会重建刚体）；
  - 物理挂起（`IsPhysicsSuspended()`）或物体没有刚体时返回 false，节点写 stderr。

### 3.4 导航（BuildNavMesh / QueryPath）

- 引擎内已有 `KX_NavMeshObject`（Detour 静态导航网格），`BuildNavMesh()` 触发构建，`FindPath(origin, dest, float*, maxLen)` 用世界坐标查询；**没有** A* 退化路径。
- **已知限制（面向使用者）**：`BuildNavMesh` 的 `agent_radius` / `agent_height` 当前**已接收但未生效**。原因不是节点没传，而是引擎只编译了 Detour 的导航网格查询、没有 Recast 的网格光栅化：没有 agent 尺寸、没有动态障碍重烘焙，导航网格完全来自场景里已有的 navmesh 数据。
  - 因此：改 agent 尺寸不会改变可行走区域；场景里新增/移动障碍物后，`BuildNavMesh` 只会重建同一份静态数据，不会根据障碍物重新烘焙网格。
  - 参数越界（<=0）或没有导航网格时仍会失败并写 stderr；参数合法时节点成功，但这两个值只被记录和告警。
- `QueryPath` 最多取 64 个点；成功但无路径时返回空列表（`OnSuccess` + 空 `Waypoints`），找不到导航网格或查询失败才走 `OnFail`。

### 3.5 批量射线（RaycastBatch）

- 引擎实现逐条调用 `KX_RayCast::RayTest(environment, origin, target, callback)`，与单条射线节点**完全同一条代码路径**，`ignore` 为图宿主对象。
- `Origins`/`Dirs` 是扁平 xyz 三元组列表，配对数为两者 `size/3` 的较小值；`Distance` 为每条射线的最大距离。
- 输出 `Hits`（命中的物体列表）与 `Points`（命中点 Vector 列表），长度与命中数一致；列表通过 `ctx.Pool().AllocList` 分配，与既有 List 语义一致。

### 3.6 碰撞组（SetCollisionGroup）

- 直接调用 `SetCollisionGroup` / `SetCollisionMask`，范围 0..65535，越界即失败并诊断。

## 4. 失败路径与诊断

每个节点都有明确的三类失败路径，并写 stderr（前缀为节点 id）：

- 对象为空 / 不是物理对象 / 不是动态物体；
- 适配器返回 false（Engine 侧把具体原因写进 `error`）；
- 参数非法（约束类型未知、params 无法解析、数值越界、导航网格不存在）。

失败时执行 `OnFail`（有该输出的节点）或只写诊断并不触发 `OnDone`，不存在「静默成功」或永久挂起。

## 5. 验收证据

| 项 | 结果 | 证据 |
|---|---|---|
| 构建 | exit=0 | `/tmp/b61_build.log` |
| port_contract | errors=0 | `python3 .../port_contract.py` 输出 `errors=0` |
| enum_consistency | inconsistencies=0 | `checked_pairs` 含 `native_b6.py` ↔ `kB6Specs` |
| 注册 | 23/23，`MISSING []`，IR 警告 0 | `/tmp/b61_register.log` |
| E2E 载具 | 创建 handle、控制参数全对 | `/tmp/b61_e2e.log` |
| E2E 约束 | 创建（hinge, params[0]=1, owners A/B）+ 第二帧移除后清空 | 同上 |
| E2E QueryPath | 3 个 waypoint 非空 | 同上 |
| E2E RaycastBatch | 3 条射线转发、Hits/Points 输出、单条与批量的命中物体一致 | 同上 |
| 回归 | B2/B3/B4/P4 Rebuild 全过 | `/tmp/b61_reg_b2b3.log`、`/tmp/b61_reg_b4.log`、`/tmp/b61_reg_p4.log` |

E2E 使用 Mock 适配器驱动真实节点图，验证的是节点调度、端口连线、配置解析与适配器契约；Engine 侧的同一路径由本节 3.1-3.6 的代码引用与限制说明覆盖。

## 6. 状态分档（A.3）

分档定义：`full` = 真实现且行为符合端口语义；`approximate` = 能用但语义近似（写明近似方式）；`unsupported` = 明确返回失败（写明缺失的引擎能力）。逐项证据见 `/tmp/a3_b61_audit.log`。

| 节点 | 状态 | 近似方式 / 缺失能力 | 证据 |
|---|---|---|---|
| CreateVehicle | approximate | 轮组已可配置（`wheel_names`，逗号分隔，空则回退 Template）；但悬挂长度固定 0.2、down=底盘 -Z、axle=底盘 +X，仍不可逐轮配置 | `KX_PhysicsAdvancedAdapter.cpp` 495-616、671-711 |
| VehicleControl | full | throttle*2000 / brake*100 为固定映射常量，端口未定义单位 | 同上 618-639 |
| AddConstraint | approximate | `slider` 映射 `PHY_GENERIC_6DOF` 并锁死其余轴；枢轴固定取中点、铰轴固定 A 局部 +Y，不可指定 | 同上 641-744 |
| RemoveConstraint | full | 仅覆盖适配器自己登记的约束（引擎无 object→constraint 反查） | 同上 746-770 |
| AddJoint | full | 仅线铰接，无马达/限位端口 | 同上 772-819 |
| SetPhysicsMass | full | — | 同上 821-829 |
| SetPhysicsFriction | full | — | 同上 831-839 |
| SetPhysicsRestitution | full | 运行时写现有 `btRigidBody`（不重建刚体）；持续接触的旧流形点保留旧合并值直到刷新 | 同上 841-864；`CcdPhysicsController.cpp` 1399-1415 |
| SetLinearDamping | full | — | 同上 866-874 |
| SetAngularDamping | full | — | 同上 876-884 |
| SetGravity | full | 场景级全局重力（复用 `KX_FunctionNode.cpp` 的既有节点） | `KX_FunctionNode.cpp` 405-417、1308 |
| BuildNavMesh | approximate | 可重建 Detour 静态网格，但 `agent_radius`/`agent_height` **已接收未生效**（无 Recast 光栅化、无动态障碍重烘焙） | `KX_PhysicsAdvancedAdapter.cpp` 896-917 |
| QueryPath | full | 单次最多 64 航点（Detour 缓冲上限）；空路径仍走 OnSuccess | 同上 919-946 |
| RaycastBatch | full | 逐条复用 `KX_RayCast::RayTest`；零方向射线跳过 | 同上 948-996 |
| SetCollisionGroup | full | 16 位范围校验 | 同上 998-1018 |

**统计（15 个物理节点，C.1.1 后更新）**：`full` 12 / `approximate` 3 / `unsupported` 0。

折算口径（三种并列，避免单一数字不可复现）：

| 口径 | 公式 | 物理结果 |
|---|---|---|
| A 严格真可用 | full=1，其他=0 | 12/15 = **80.0%** |
| B 加权可用 | full=1、approximate=0.5、unsupported=0 | (12+1.5)/15 = **90.0%** |
| C 节点覆盖 | full+approximate 计 1 | 15/15 = **100%** |

物理侧已没有 `unsupported`：之前唯一的一项（`SetPhysicsRestitution`）在 C.1.1 接上了运行时 setter。剩余 3 个 `approximate` 都是"能用但语义受限"（轮组悬挂参数、slider 近似、导航 agent 尺寸），且全部有明确诊断，没有静默降级。全量 23 节点的统计口径见 `docs/B6_ANIMATION.md` 第 6 节末尾的合计表。

## 7. C.1 物理尾巴的实施与验证

### 7.1 C.1.1 restitution 运行时 setter（已接上）

判定结论：**能安全改，不需要重建刚体**。

- `PHY_IPhysicsController` 增加窄接口 `bool SetRestitution(float)`，默认返回 false；**声明放在接口末尾**，既有虚函数槽位不变，增量构建不会把旧调用点派发错位（`Physics/Common/PHY_IPhysicsController.h`）。
- `CcdPhysicsController::SetRestitution`（`Physics/Bullet/CcdPhysicsController.cpp` 1399-1415）：对现有 `btRigidBody` 直接 `setRestitution(clamped)`，并同步 `m_cci.m_restitution`（副本/形状重建时保持同值）。
  - 不 remove/re-add：接触点、约束、速度全部保留；
  - 值域 `[0,1]`（适配器与控制器各夹一次）；
  - Bullet 的合并回弹是**两个刚体回弹值的乘积**，因此「地面 0 + 球 0.9」仍然是 0——这是 Bullet 语义，不是节点 bug；要让球弹起来，地面也必须是 1.0（或非零）。文档在此明确，避免使用者误判。
- 生效边界：只在**新建流形接触点**时读取合并值。新的撞击立即使用新值；已经在持续接触的旧点保留旧合并值直到该点被刷新/重建（有界滞后，但不是"不生效"）。
- 失败路径：无刚体、物理挂起（`IsPhysicsSuspended()`）、值越界 → 返回 false + 明确 stderr。

数值验证（`tests/c1_restitution.cpp`，链 `build_linux/lib/libextern_bullet.a`，同一落体、同一地面，120Hz、400 步；
落盘日志 `/tmp/c1_physics.log:54`：`dead peak=0.5280 bouncy peak=4.2083 runtime peak=4.2083`。
原稿写的 `0.5279` 在全库日志中无命中，已按实测改为 `0.5280`）：

| 场景 | 触地后最高点 |
|---|---|
| 球 restitution=0.0 | 0.5280 |
| 球 restitution=0.9（创建时设定） | 4.2083 |
| 球 restitution=0.0，**飞行中**第 20 步改 0.9 | 4.2083（与建好即为 0.9 一致，误差 < 0.15） |

断言全部为数值比较，失败返回非 0：`PASS C.1.1 restitution runtime change is measurable and in place`。

### 7.2 C.1.2 轮组命名可配置（已接上，旧图兼容）

- 节点新增 config `wheel_names`（逗号分隔）。非空时按顺序解析；空时回退旧的 `Template` 命名约定，旧图行为不变。
- 引擎按配置解析（`KX_PhysicsAdvancedAdapter.cpp` 495-616）：名字找不到 → `wheel object '<name>' from wheel_names was not found in the scene`；找到但没有物理控制器 → 同样指名报错；**整次创建失败，不返回少轮的车辆**。
- 空格容错：`"wheelFL_A, wheelFR_A,wheelRL_A ,wheelRR_A"` 解析为 4 个名字。
- E2E（Mock 驱动真实节点图）：显式四名 → `vehicle_wheels.size() == 4` 且顺序/去空格正确；空配置 → `vehicle_wheels.empty()`，仍走 `Template` 路径。两者均通过。

### 7.3 C.1.3 导航退化路径（走路径 B：只写清限制）

- 不接 Recast（属 B6.9 的 3-5 天工作量）。
- 文档见 3.4 节：`agent_radius` / `agent_height` **已接收但未生效**；只有 Detour 查询、没有 Recast 光栅化，因此无 agent 尺寸、无动态障碍重烘焙。
- 节点行为不变：参数合法时成功（值只记录 + stderr 告警），参数越界或无 navmesh 时失败并诊断。没有把「不生效」伪装成「已应用」。
