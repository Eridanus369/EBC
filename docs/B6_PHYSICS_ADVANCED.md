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
| Engine 实现 | `KX_PhysicsAdvancedAdapter.cpp` | 类 531、CreateVehicle 535、VehicleControl 719、AddConstraint 742、RemoveConstraints 935、AddJoint 961、SetRestitution 1030、SetGravity 1075、BuildNavMesh 1085、QueryPath 1111、RaycastBatch 1140、SetCollisionGroup 1190、工厂 1246 |
| 上下文注入 | `KX_ExecutionContext.h/.cpp` | `SetAdapters` 末尾参数、`PhysicsAdvanced()` getter |
| 装配 | `KX_GameAdapters.h/.cpp`、`KX_GameAdaptersEngine.cpp` | `KX_AdapterSet::physics_advanced`；Engine 侧 2493 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 232-233、头 243 |
| 编辑器表 | `scripts/startup/bge_gamenodes/editor/nodes/native_b6.py` | `NATIVE_B6` 24-115、注册 117 |

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
- 轮子的连接点/悬挂方向/轮轴由底盘世界变换换算到局部空间；半径取轮子控制器半径。
- **P-3.5 更新（2026-11-06）**：悬挂参数已可逐轮配置。`suspension` 配置为分号分隔的轮组串，每组 `刚度,阻尼,压缩[,静长]`（静长缺省 0.2），按轮序写入 `PHY_IVehicle::SetSuspensionStiffness/Damping/Compression/RestLength`；单组时广播到全部轮子。数值为负、静长非正、或组数与 `wheel_names` 声明的轮数不符时在创建前失败；走 Template 派生轮数时组数不匹配只能在建车后发现（引擎无删除车辆接口，车辆保持默认悬挂，节点仍报错）。`wheel_names` 仍只决定「哪些物体是轮子」。
- `VehicleControl` 的归一化：`Steering` 原样传给每个轮子；`Throttle` 乘 2000 作为引擎力；`Brake` 乘 100 作为制动力。两者都按 0..1 归一化输入，超出 1 会被引擎饱和。

### 3.2 约束与关节（AddConstraint / RemoveConstraint / AddJoint）

- 类型映射：`point`→`PHY_POINT2POINT`、`hinge`→`PHY_LINEHINGE`、`cone`→`PHY_CONE_TWIST`、`generic`→`PHY_GENERIC_6DOF`、**`slider`→`PHY_SLIDER_CONSTRAINT`（B.4 新增的原生分派）**。
- `slider` 已改为 Bullet 原生 `btSliderConstraint`：自由轴为 A 局部 +Y，其余线性轴与全部旋转被约束自身锁死；`params[0]`/`params[1]` 直接映射 `setLowerLinLimit`/`setUpperLinLimit`。引擎侧 `CcdConstraint::SetParam(6, target_velocity, max_force)` 已接线性马达，但节点参数表当前未暴露马达端口。
- `params[6]` 由节点 config 的逗号浮点串解析，按约束类型传给 `PHY_IConstraint::SetParam`；非法串直接失败并诊断。
- 枢轴默认取两个物体世界位置的中点、换算到 A 的局部空间；`AddJoint` 的 `Pivot`/`Axis` 端口按世界空间输入，再换算到 A 局部建 `PHY_LINEHINGE`。
- **P-3.5 更新（2026-11-06）**：`AddConstraint` 新增可选 config `pivot` / `axis`（`"x,y,z"`，世界坐标/世界轴）。配置后由适配器换算到 A 局部再用 `PHY_IPhysicsEnvironment::CreateConstraint` 建约束；空值保持原默认（中点 / +Y）。轴退化（长度平方 < 1e-10）直接失败并诊断。
- `RemoveConstraint` 按物体清理该适配器创建的约束 id；引擎侧没有按物体反查约束的接口，适配器内部维护 id/owner 列表。

### 3.3 物理属性（Mass / Friction / Restitution / Damping）

- Mass/Friction/Damping 直接调用 `PHY_IPhysicsController` 的 setter，对象为空或控制器为空时失败并诊断。
- **Restitution 支持运行时修改**（C.1.1）：`PHY_IPhysicsController` 新增窄接口 `bool SetRestitution(float)`（声明在接口末尾，保证既有虚函数槽位不变），`CcdPhysicsController` 的实现直接对**现有** `btRigidBody` 调 `setRestitution()` 并同步 `m_cci.m_restitution`。**不 remove/re-add 刚体**，因此接触点、约束、速度都保留。
  - 取值范围：适配器限制 `[0, 1]`，越界即失败并诊断；
  - 生效时机：Bullet 在**新建流形接触点**时用两个刚体回弹值的乘积算 `m_combinedRestitution`。新碰撞一定用新值；持续接触的旧点保留旧值直到刷新（有界滞后，不会重建刚体）；
  - 物理挂起（`IsPhysicsSuspended()`）或物体没有刚体时返回 false，节点写 stderr。

### 3.4 导航（BuildNavMesh / QueryPath）

- 引擎内已有 `KX_NavMeshObject`（Detour 静态导航网格），`BuildNavMesh()` 触发构建，`FindPath(origin, dest, float*, maxLen)` 用世界坐标查询；**没有** A* 退化路径。
- **P-1（2026-11-06）：运行时 Recast 烘焙已接上**，`agent_radius` / `agent_height` 现在真正决定可行走面（旧的「已接收但未生效」结论作废，原因见 §7.3 的更新注）：
  - `KX_NavMeshBuilder.h`（header-only，`namespace KX_NavMeshBuilder`）直接用 `extern/recastnavigation/recast-capi.h` 跑完整烘焙链：heightfield → `recast_markWalkableTriangles`（`max_slope`）→ `recast_rasterizeTriangles` → `recast_filterLowHangingWalkableObstacles` / `recast_filterLedgeSpans` / `recast_filterWalkableLowHeightSpans`（`agent_height`）→ compact → `recast_erodeWalkableArea`（`agent_radius`）→ regions → contours → `recast_buildPolyMesh` → `recast_buildPolyMeshDetail`。
  - `recast_buildPolyMeshDetail` 的 detail 索引是 32 位，`dtCreateNavMeshData` 要 16 位布局；转换时显式检查 `>0xffff` 并报 `detail mesh exceeds the 16-bit Detour layout`，不静默截断。
  - 入口 `KX_NavMeshObject::BuildNavMeshRuntime(agent_radius, agent_height)`（`KX_NavMeshObject.cpp`，**不依赖 `bContext`/`ReportList`**）：从对象自己的 RAS 网格取顶点（`m_sharedvertex_map` → `getOrigIndex()`），按 Y-up 送入烘焙；`dtCreateNavMeshData` 后 `new dtStatNavMesh` + `init(data, size, true)` 接管内存，失败则释放缓冲并返回 false。
  - 坐标：几何按 Y-up（x/z 水平）烘焙，`KX_NavMeshObject` 存的是 Blender Z-up，故烘焙前做 Y/Z 交换，`FindPath` 侧反向。
  - `KX_PhysicsAdvancedAdapter::BuildNavMesh` 改调运行时入口；失败时 `error = "runtime navmesh bake failed"`，走 `OnFail` 并写 stderr。
  - 旧的预烘焙资产路径（`KX_NavMeshObject::BuildNavMesh()` 读 `.recast_data` / RAS）保留，运行时入口是新增，不改旧资产行为。
  - 验证层级：`tests/c6_navmesh_bake.cpp` 是**烘焙管线层**（`Bake` → `dtCreateNavMeshData` → `dtStatNavMesh`），不是「节点 → 真实引擎」层；后者需要可驱动的游戏场景，headless 下没有。
- 参数越界（<=0）或没有导航网格时仍会失败并写 stderr。
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
| E2E 约束类型（B.4） | point / hinge / cone / slider / generic 五种约束行为 **10/10 PASS**（当次；K 冻结后 `c5` 为 **16/0**） | `/tmp/c5_constraint_types.log`（`RESULT assertions=10 failures=0`） |
| 回归（本轮） | b61 节点图 E2E **26/0**（当次；K 冻结后 **44/0**）；restitution 运行时改值仍可测 | `/tmp/c1_physics.log`、`.e2e_out/run_20261106-095501` |
| E2E QueryPath | 3 个 waypoint 非空 | 同上 |
| E2E RaycastBatch | 3 条射线转发、Hits/Points 输出、单条与批量的命中物体一致 | 同上 |
| 回归 | B2/B3/B4/P4 Rebuild 全过 | `/tmp/b61_reg_b2b3.log`、`/tmp/b61_reg_b4.log`、`/tmp/b61_reg_p4.log` |
| E2E 约束类型（P-2 补证） | point / hinge / cone / slider / generic **14/14 PASS**；新增 point 自由旋转、hinge 限位内自由旋转、cone 平动锁死、6DOF 其余线性轴锁死 | `.e2e_out/run_20261106-131608/c5_constraint_types.log`（`TOTAL assertions=14 failures=0`） |
| E2E 导航烘焙（P-1） | `agent_radius` 0.3 vs 0.6：可行走面积 76.65 vs 61.17，0.8 m 门洞前者可穿（6 poly）、后者封死；`agent_height` 1.0 vs 2.0：单岛可达 vs 裂成 2 岛不可达；**12/0** | `.e2e_out/run_20261106-131628/c6_navmesh_bake.log`；`tests/c6_navmesh_bake.cpp` |
| 复核（P-3 / P-4） | restitution 行为断言仍绿（峰值 0.5280 / 4.2083 / 4.2083）；b61 **44/0**（含四轮顺序、去空格、空配置回退；K 冻结 `RUN_ID=20261106-224926`） | `.e2e_out/run_20261106-131618`、`run_20261106-131620` |
| E2E 约束自定义 frame（P-3.5） | hinge 绕世界 X 轴、枢轴 (0.5,0,0)，限位 [0,0.5]，自旋 2 rad/s：最高角 0.5±0.04、枢轴漂移 0.0±0.02（frame 未生效则角会跑飞）；**16/0** | `.e2e_out/run_20261106-155855/c5_constraint_types.log`；`tests/c5_constraint_types.cpp` |
| E2E 载具悬挂与约束 frame 转发（P-3.5） | 四轮悬挂按轮序记录 16 个浮点（索引 0..3/12/15 数值正确）；非法 `"3,0.8"` 组 → `create_vehicle_calls == 0`；`pivot/axis` 原样转发；body 图（AddJoint/Mass/Friction/Restitution/Damping/CollisionGroup/BuildNavMesh）**44/0** | `.e2e_out/run_20261106-155855/b61.log`；`tests/b61_e2e.cpp` |
| E2E 重力节点（c8） | `OnInit → SetGravity` 挂 `GravityPhysics` mock：`(3,-2,7)` 回读一致、`(±1000,0)` 接受（逐分量判定，范数约 1414 仍接受）、`(1001,0,0)` 拒绝且保持上一值、OnDone 仍触发；NaN / inf / `1e30` 被拒绝（`set_calls=0`、重力不变）并打印诊断；**25/0** | `.e2e_out/run_20261106-162700/c8_gravity.log`；`tests/c8_gravity_e2e.cpp` |

E2E 使用 Mock 适配器驱动真实节点图，验证的是节点调度、端口连线、配置解析与适配器契约；Engine 侧的同一路径由本节 3.1-3.6 的代码引用与限制说明覆盖。

`c6_navmesh_bake` 的验证层级是**烘焙管线层**（Recast bake → `dtCreateNavMeshData` → `dtStatNavMesh`），断言直接看多边形数、面积与可达性；它**不覆盖**「节点图 → 真实引擎场景」那一层，后者需要可驱动的游戏场景，headless 里没有。

## 6. 状态分档（A.3）

分档定义：`full` = 真实现且行为符合端口语义；`approximate` = 能用但语义近似（写明近似方式）；`unsupported` = 明确返回失败（写明缺失的引擎能力）。逐项证据见 `/tmp/a3_b61_audit.log`。

| 节点 | 状态 | 近似方式 / 缺失能力 | 证据 |
|---|---|---|---|
| CreateVehicle | full | 逐轮悬挂参数可配置（`suspension`：`刚度,阻尼,压缩[,静长]`，分号分组）；`wheel_names` 决定轮组；down=底盘 -Z、axle=底盘 +X 为既定局部约定 | `KX_PhysicsAdvancedAdapter.cpp` 535-705（逐轮 658-704）；`KX_FunctionNodesB6.cpp:140`；`.e2e_out/run_20261106-155855` |
| VehicleControl | full | throttle*2000 / brake*100 为固定映射常量，端口未定义单位 | 同上 719-740 |
| AddConstraint | full | `slider` 为原生 `btSliderConstraint`；枢轴/铰轴可用 `pivot` / `axis` 指定（世界坐标/轴，空值回退中点/+Y） | 同上 742-960（frame 839）；`CcdPhysicsEnvironment.cpp` 1949-2007；`.e2e_out/run_20261106-155855` |
| RemoveConstraint | full | 仅覆盖适配器自己登记的约束（引擎无 object→constraint 反查） | 同上 935-960 |
| AddJoint | full | 仅线铰接，无马达/限位端口 | 同上 961-1009 |
| SetPhysicsMass | full | — | 同上 1010-1019 |
| SetPhysicsFriction | full | — | 同上 1020-1029 |
| SetPhysicsRestitution | full | 运行时写现有 `btRigidBody`（不重建刚体）；持续接触的旧流形点保留旧合并值直到刷新 | 同上 1030-1054；`CcdPhysicsController.cpp` 1399-1415 |
| SetLinearDamping | full | — | 同上 1055-1064 |
| SetAngularDamping | full | — | 同上 1065-1074 |
| SetGravity | full | 非法输入策略：NaN / inf / 任一分量绝对值 > 1000 **拒绝**（逐分量独立判定，不做范数合成；不写引擎、stderr 诊断、节点仍触发 OnDone） | `KX_FunctionNode.cpp:451-474`；`KX_GameAdaptersEngine.cpp:1206-1210`；`.e2e_out/run_20261106-162700/c8_gravity.log` |
| BuildNavMesh | full | 运行时 Recast 烘焙：`agent_radius` / `agent_height` 直接进入 erode 与 low-height span 过滤，改变可行走面；旧 `.recast_data` 资产路径保留 | `KX_NavMeshBuilder.h`；`KX_NavMeshObject.cpp` `BuildNavMeshRuntime`；同上 1085-1110；`tests/c6_navmesh_bake.cpp` |
| QueryPath | full | 单次最多 64 航点（Detour 缓冲上限）；空路径仍走 OnSuccess | 同上 1111-1139 |
| RaycastBatch | full | 逐条复用 `KX_RayCast::RayTest`；零方向射线跳过 | 同上 1140-1189 |
| SetCollisionGroup | full | 16 位范围校验 | 同上 1190-1208 |

**统计（15 个物理节点，SetGravity mock 后更新）**：`full` 15 / `approximate` 0 / `unsupported` 0 / `pending-device` 0。逐节点证据与限制见 `docs/PHYSICS_RESCORE.md`。

折算口径（严格与加权并排，避免只报高的）：

| 口径 | 公式 | 物理结果 |
|---|---|---|
| A 严格（pending-device 不计分） | full=1，其他=0 | 15/15 = **100%**（旧口径 13/15 = 86.7%） |
| B 加权（新口径） | full=1、approximate=0.5、pending-device=1.0、unsupported 不进分母 | 15/15 = **100%** |
| C 节点覆盖 | full+approximate 计 1 | 15/15 = **100%** |

物理侧已没有 `unsupported`：之前唯一的一项（`SetPhysicsRestitution`）在 C.1.1 接上了运行时 setter。P-3.5 之后 `CreateVehicle`（逐轮悬挂）与 `AddConstraint`（`pivot`/`axis` 可指定）由 `approximate` 升为 `full`，各有行为断言（见 §5 的 `-155855`）。最后一项 `SetGravity` 的验证缺口由 `tests/c8_gravity_e2e.cpp` 的 `GravityPhysics` mock 补上（20/0，见 §5），严格与新口径同时到 15/15。该 mock **仅存在于测试 harness**，`source/` 无引用；运行时仍走 `KX_EnginePhysicsAdapter::SetGravity`。`BuildNavMesh` 在 P-1 之后为 `full`（agent 尺寸进入真实烘焙链路，证据见 §5 的 c6 行）。这里的分母是**物理域 15 个节点**；全量统计口径已由 K 冻结（`N=325`、`F=299/A=4/U=7/P=15`、`RUN_ID=20261106-224926`），见 `docs/B6_ANIMATION.md` 第 6 节末尾的合计表。

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
- **复核结论（2026-11-06）**：Recast **不是没编译**——`extern/recastnavigation` 的 Recast/Detour 源码与 `recast-capi` 都在，`.o` 已进 `build_linux`，`Ketsji/CMakeLists.txt:191` 已链接。缺的是**运行时烘焙入口**：唯一的 bake 实现在编辑器 `mesh_navmesh.cc` 的 `static buildNavMesh()`（:135），依赖 `bContext`/`ReportList` 并 `createRepresentation()`（:302），玩家进程无法调用；运行时 `KX_NavMeshObject::BuildNavMesh()`（:775）只消费 `.recast_data`（:613）或 RAS 多边形（:720），不做光栅化。
- 文档见 3.4 节：`agent_radius` / `agent_height` **已接收但未生效**，无 agent 尺寸、无动态障碍重烘焙。
- 节点行为不变：参数合法时成功（值只记录 + stderr 告警），参数越界或无 navmesh 时失败并诊断。没有把「不生效」伪装成「已应用」。
- **P-1 更新（2026-11-06，追加说明，不改本节历史结论）**：上述「不接 Recast / 已接收未生效」是 C 块当时的结论。P-1 已补上运行时烘焙入口（`KX_NavMeshBuilder.h` + `KX_NavMeshObject::BuildNavMeshRuntime`），`agent_radius` / `agent_height` 现在进入 `recast_erodeWalkableArea` 与 `recast_filterWalkableLowHeightSpans`，节点不再只记录参数；证据见 §3.4 / §5 的 `c6_navmesh_bake` 行（验证层级：烘焙管线层）。

### 7.4 B.4 约束类型原生分派（slider）

- `PHY_DynamicTypes.h` 新增 `PHY_SLIDER_CONSTRAINT = 13`；`CcdPhysicsEnvironment::CreateConstraint` 用原生 `btSliderConstraint` 构造（自由轴 = frame A 的 X 轴，即 A 局部 +Y；其余 5 个自由度由约束自身锁死）；`CcdConstraint::SetParam(0, lo, hi)` 映射线性下限/上限，`SetParam(6, target_velocity, max_force)` 映射线性马达。
- 适配器 `slider` 直接映射 `PHY_SLIDER_CONSTRAINT`，不再走 `PHY_GENERIC_6DOF` 的"锁死其余轴"近似。
- 行为 E2E：`/tmp/c5_constraint_types.log`，point / hinge / cone / slider / generic **10/10 PASS**（当次；K 冻结后 `c5` 为 **16/0**），含 slider 线性限位 `|y| = 0.5`、旋转锁死、线性马达 `0.992 m`（目标 1.0 m）。
- **P-3.5 更新**：枢轴与轴已可通过 `AddConstraint` 的 `pivot` / `axis` config 指定（见 §3.2）；本节原「仍取中点 / 固定 +Y」的结论仅指默认值。节点参数表仍未暴露马达端口。
- 验证层级：上述 E2E 在 Bullet 构造参数层（与 `CreateConstraint` 完全相同的 frame/limit 构造）；引擎侧的类型分派由编译通过 + 代码路径确认，`tests/` 没有可创建真实物理场景的 harness，因此没有“节点 → 真实引擎”的物理 E2E。
- 顺带确认的既有行为（非本轮引入）：`btHingeConstraint::setLimit(-0.5, 0.5)` 会把负角度归一化成 `2π-0.5`，区间塌缩、限位失效；E2E 改用 `[0, 0.5]` 才能量到上限 `0.5067`。节点传负角度限位时同样受此影响。
- **用负下限会静默失效**：`btHingeConstraint::setLimit` 不报错、不返回失败，也不写任何诊断——它只是把负值换算成 `2π-|low|`，于是下限大于上限，限位区间塌缩成空，铰接点表现为「完全不限位」。排查时没有任何异常信号，只有实际角度跑飞才能发现；因此 `hinge` 的 `params` 下限应传非负值，若需要对称摆幅请用 `[0, 2*swing]` 之后在应用层夹紧角度。

### 7.5 P-3.5 两项近似的补齐（2026-11-06）

两项都在引擎已有能力内完成，没有新增物理实现，只补了"参数从哪来"：

- **轮组悬挂逐轮配置**：`PHY_IVehicle` 本来就有逐轮 setter（`SetSuspensionStiffness` / `SetSuspensionDamping` / `SetSuspensionCompression`），缺的是静长——本轮在接口末尾追加 `SetSuspensionRestLength`（`PHY_IVehicle.h`，不改变既有虚函数槽位），`WrapperVehicle` 在 `CcdPhysicsEnvironment.cpp` 写 `btWheelInfo::m_suspensionRestLength1`。适配器 `CreateVehicleWithSuspension`（`KX_PhysicsAdvancedAdapter.cpp:658`）先校验数值与 `wheel_names` 声明的组数，再建车并逐轮写入；Template 派生轮数的组数不匹配无法在建车前判定，故只能报错并保留默认悬挂（引擎无删除车辆接口）。
- **约束枢轴/铰轴可指定**：`PHY_IPhysicsEnvironment::CreateConstraint` 一直接收 A 局部 pivot/axis，适配器此前只是自己取中点/+Y。本轮新增 `AddConstraintWithFrame`（`:826`）：把世界 pivot/axis 用 `A^-1 * (world - Apos)` / `A^-1 * world_axis` 换算到 A 局部，其余分派与 `SetParam` 逻辑与 `AddConstraint` 完全一致；轴退化（`length2() < 1e-10`）在创建前失败。
- **E2E**：`b61` 44/0（`.e2e_out/run_20261106-155855/b61.log`）覆盖逐轮转发顺序/数值、非法悬挂组不创建车、`pivot/axis` 转发；`c5` 16/0（`.e2e_out/run_20261106-155855/c5_constraint_types.log`）用"绕世界 X 轴自旋到 0.5 rad 上限"证明 frame 真被引擎采用。
- 两项均无 `unsupported` 分支，故不新增 FOREVER 条目。
