# B6.1 动画高级（8 节点）

日期：2026-11-05
范围：分层动作、动作混合、骨骼变换/权重/骨骼动作、动作速度、动画事件、动画帧查询
运行时：`KX_FunctionNodesB6.cpp`（与物理节点共用 `kB6Specs`）
适配器：在既有 `KX_AnimationAdapter` 上**追加**方法（不重排既有虚函数）

## 1. 接入位置

| 项 | 文件 | 位置 |
|---|---|---|
| 节点执行函数 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp` | PlayActionByLayer 338、BlendActions 354、SetBoneTransform 372、SetBoneWeight 392、PlayBoneAction 409、SetAnimationSpeed 428、AddAnimationEvent 450、GetAnimationFrame 490 |
| 事件状态与 Tick | 同上 | `B6AnimEventState` 442、`TickAddAnimationEvent` 469 |
| 适配器接口（追加） | `KX_GameAdapters.h` | `PlayActionByLayer` 835、`BlendActions`、`SetBoneTransform` 859、`SetBoneWeight`、`PlayBoneAction`、`GetActionNormalized` 897 |
| Engine 实现（追加） | `KX_GameAdaptersEngine.cpp` | PlayActionByLayer 1606、BlendActions 1635、SetBoneTransform 1659、SetBoneWeight 1699、PlayBoneAction 1711、GetActionNormalized 1725、`ActionRange` 1749 |
| Mock 实现（追加） | `KX_GameAdapters.h` | PlayActionByLayer 1026、SetBoneTransform 1054、SetBoneWeight 1069、PlayBoneAction 1084、GetActionNormalized 1099 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6.py` | `NATIVE_B6` 24-115 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 232 |

## 2. 节点与端口

| 节点 | 输入 | 输出 | config |
|---|---|---|---|
| PlayActionByLayer | OnEnter, Object, Action(String), Layer(Int), Blend(Float) | OnDone | 无 |
| BlendActions | OnEnter, Object, ActionA(String), ActionB(String), Factor(Float) | OnDone | 无 |
| SetBoneTransform | OnEnter, Object, Bone(String), Position(Vector), Rotation(Vector) | OnDone | 无 |
| SetBoneWeight | OnEnter, Object, Bone(String), Weight(Float) | OnDone | 无 |
| PlayBoneAction | OnEnter, Object, Bone(String), Action(String) | OnDone | 无 |
| SetAnimationSpeed | OnEnter, Object, Layer(Int), Speed(Float) | OnDone | 无 |
| AddAnimationEvent | OnEnter, Object, Action(String), Frame(Int) | OnEvent | `event_name` |
| GetAnimationFrame | Object, Layer(Int) | Frame(Float), Normalized(Float) | 无（value node） |

## 3. 真实现与受引擎限制的占位

| 能力 | 状态 | 说明 |
|---|---|---|
| 分层播放 | **真实现** | `KX_GameObject::PlayAction(name, start, end, layer, priority, blendin, ...)`；帧范围由场景逻辑管理器按动作名查 `bAction::frame_start/frame_end`，区间缺失时失败并诊断 |
| 动作混合 | **真实现（固定 layer 0/1）** | `KX_GameAdaptersEngine.cpp` 1655-1656 传的是真实 layer 权重：A 放 layer 0（权重 `1-factor`）、B 放 layer 1（权重 `factor`）。引擎侧 `BL_Action.cpp` 604-605 调 `BlendInPose(m_blendpose, m_layer_weight, m_blendmode)`，`BL_ArmatureObject::GameBlendPose` 对 loc/scale 线性加权、对四元数插值，结果为 `(1-factor)*A + factor*B`。**不是近似**；受限于只支持两个动作、固定占用 layer 0/1（会覆盖该层已有动作） |
| 骨骼变换 | **真实现（姿态延迟到下一次骨架更新）** | 对象必须是 `OBJ_ARMATURE`；`BL_ArmatureObject::GetPose` → `BKE_pose_channel_find_name` → 写 `loc`/`rotmode=ROT_MODE_XYZ`/`eul` → `ApplyPose()`。`ApplyPose` 受 `m_lastapplyframe != m_lastframe` 守卫（`BL_ArmatureObject.cpp` 409-423），同一帧内不会重算 `BKE_pose_where_is`，因此**当帧渲染不变，需等下一次骨架更新** |
| 骨骼权重 | **引擎不支持** | 顶点组权重没有运行时 setter。`SetBoneWeight` 返回 false 并写 stderr「bone weight is not exposed by the engine」 |
| 骨骼级动作 | **引擎不支持** | `BL_ActionManager` 只能整对象播放动作。`PlayBoneAction` 返回 false 并写 stderr「per bone action playback is not exposed by the engine」 |
| 动作速度 | **真实现** | `SetActionSpeed`（既有适配器方法） |
| 动画帧 | **真实现** | `GetActionFrame(layer)`；`Normalized` 由当前动作的 `frame_start/frame_end` 计算并 clamp 到 0..1，取不到当前动作时返回 0 |
| 动画事件 | **近似实现（节点侧轮询）** | 有状态节点：每帧读取当前帧，检测「上一帧 → 当前帧」是否跨过目标帧；跨越时触发 `OnEvent` 一次。它不是引擎事件通道：固定读 layer 0，`Action` 输入只参与 armed 判定、不绑定具体动作；触发一次后解除 armed，不支持重复/循环触发 |

## 4. AddAnimationEvent 的状态机

`B6AnimEventState` 保存 `object`、`event_name`、`frame`、`previous_frame`、`armed`：

1. `OnEnter` 时记录对象与参数，置 `armed=true`，`previous_frame` 取当前帧；
2. 每帧 Tick 读取当前帧 `current`；
3. 若 `armed` 且 `previous_frame < frame && current >= frame`（仅正向跨越，代码 477-479），触发 `OnEvent` 一次并解除 `armed`；
4. 对象或动作无效时不触发、不挂起，写入诊断。

`GetAnimationFrame` 是 value node（`is_value_node=true`），每帧刷新，不需要触发输入。

## 5. 验收证据

| 项 | 结果 | 证据 |
|---|---|---|
| 构建 | exit=0 | `/tmp/b61_build.log` |
| port_contract | errors=0（含 `native_b6.py` ↔ `kB6Specs`） | `errors=0` |
| enum_consistency | inconsistencies=0（`event_name` 等 config 键已读取） | `checked_pairs` 含 B6 表 |
| 注册 | 23/23，`MISSING []` | `/tmp/b61_register.log` |
| E2E 分层播放 | layer=2、blend_in=0.3 | `/tmp/b61_e2e.log` |
| E2E 混合 | A=`run`、B=`walk`、factor=0.4 | 同上 |
| E2E 骨骼变换 | bone=`spine`、位置/旋转与输入一致 | 同上 |
| E2E 动作速度 | speed=1.5 | 同上 |
| E2E 帧查询 | Frame=42、Normalized=0.5 | 同上 |
| 引擎不支持证据 | `SetBoneWeight` / `PlayBoneAction` 各写一条明确 stderr，节点仍到达适配器一次 | 同上 |

E2E 用 Mock 动画适配器驱动真实节点图，验证节点调度、端口连线与适配器契约；骨骼真实现的引擎路径见第 3 节代码引用，限制按「真实现/层近似/不支持」如实标注。

## 6. 状态分档（A.3）

分档定义：`full` = 真实现且行为符合端口语义；`approximate` = 能用但语义近似（写明近似方式）；`unsupported` = 明确返回失败（写明缺失的引擎能力）。逐项证据见 `/tmp/a3_b61_audit.log`。

| 节点 | 状态 | 近似方式 / 缺失能力 | 证据 |
|---|---|---|---|
| PlayActionByLayer | full | layer 0 在引擎侧被强制 `layer_weight=-1`（首层不混合、直接应用），符合"播放到指定层"语义 | `KX_GameAdaptersEngine.cpp` 1606-1632；`BL_ActionManager.cpp` 128-130 |
| BlendActions | full | 真实 layer 权重混合，结果 `(1-factor)*A + factor*B`；限于两个动作、固定 layer 0/1 | `KX_GameAdaptersEngine.cpp` 1635-1657；`BL_Action.cpp` 604-605；`BL_ArmatureObject.cpp` 59-104 |
| SetBoneTransform | approximate | 姿态写入成功，但 `ApplyPose` 受 `m_lastapplyframe != m_lastframe` 守卫，当帧渲染不变，需等下一次骨架更新 | `KX_GameAdaptersEngine.cpp` 1659-1697；`BL_ArmatureObject.cpp` 409-423 |
| SetBoneWeight | unsupported | 引擎无运行时顶点组权重 setter（`source/gameengine` 全量无 `vertex_group`/`VertexWeight` 匹配） | `KX_GameAdaptersEngine.cpp` 1699-1709 |
| PlayBoneAction | unsupported | `BL_ActionManager` 无 per-bone 逻辑，`BL_ArmatureObject` 无骨骼级动作接口 | `KX_GameAdaptersEngine.cpp` 1711-1723；`BL_ActionManager.cpp` 全文无 `bone` |
| SetAnimationSpeed | full | — | `KX_FunctionNodesB6.cpp` 428-440；`BL_ActionManager.h` 102 |
| AddAnimationEvent | approximate | 节点侧逐帧轮询（非引擎事件）；固定 layer 0；单次触发后解除 armed | `KX_FunctionNodesB6.cpp` 450-467、469-488 |
| GetAnimationFrame | full | 取不到当前动作时返回 0（已定义默认值） | `KX_FunctionNodesB6.cpp` 490-501；`KX_GameAdaptersEngine.cpp` 1725-1745 |

**统计（8 个动画节点）**：`full` 4 / `approximate` 2 / `unsupported` 2。

折算口径（三种并列，避免单一数字不可复现）：

| 口径 | 公式 | 动画结果 | 23 节点合计 |
|---|---|---|---|
| A 严格真可用 | full=1，其他=0 | 4/8 = **50.0%** | 16/23 = **69.6%** |
| B 加权可用 | full=1、approximate=0.5、unsupported=0 | 5.0/8 = **62.5%** | 18.5/23 = **80.4%** |
| C 节点覆盖 | full+approximate 计 1 | 6/8 = **75.0%** | 21/23 = **91.3%** |

合计按 C.1.1 之后的物理分档（物理 `full` 12 / `approximate` 3 / `unsupported` 0，见 `docs/B6_PHYSICS_ADVANCED.md` 第 6 节）与动画分档（`full` 4 / `approximate` 2 / `unsupported` 2）相加。

若把 `SetBoneTransform` 按"full-with-caveat"计（用户允许的口径），动画口径 A 为 5/8=62.5%、B 为 5.5/8=68.8%；本审计取保守的 `approximate`。旧口径「动画 85%」同样无法用上述任一公式复现，已废弃。

## 7. C.2 动画尾巴三项评估

### 7.1 `SetBoneWeight` / `PlayBoneAction`：需引擎层改造，2 天内不可完成

结论：**不加接口**，两项维持 `unsupported`。理由与缺失能力：

- `SetBoneWeight`：`KX_GameObject` 无骨骼/顶点组 API（`KX_GameObject.h` 仅 `PlayAction` 292、`GetActionFrame` 307、`GetActionManagerNoCreate` 170），`source/gameengine` 全量 grep `vertex_group|VertexWeight|SetVertexGroup` 无匹配。补全需要：① 运行时写入 mesh 顶点组权重的接口（`blender::Mesh` deform layer 层）；② 标记并重算蒙皮（depsgraph 重评估 + 顶点组归一化），③ 再包一层 KX 适配器。这是引擎层改造，不是"加一个方法"。
- `PlayBoneAction`：`BL_ActionManager` 内部无任何骨骼相关分支（全文 grep `bone` 无匹配），`BL_ArmatureObject.h` 只有 `GetPose/ApplyPose/GameBlendPose/ApplyAction/BlendInPose/UpdateTimestep/GetArmatureObject`。补全需要：① 按骨骼名过滤 action 通道（`bActionGroup`/`FCurve` 路径匹配）；② 与 `BL_Action::ProcessArmatureBlending` 合并时不能覆盖其它骨骼通道；③ 引入 bone mask/layer 概念。工作量远超 2 天，且会改到正在改的动画管线。

处理方式：节点保持诚实失败（`KX_GameAdaptersEngine.cpp` 1699-1709、1711-1723 写明确 stderr），没有伪造成功。

### 7.2 `BlendActions`：引擎支持真实 layer 混合，已接上

结论：**不需要引擎改造**，已使用真正的多 layer 权重混合，分档由 `approximate` 上调为 `full`（带固定 layer 0/1 caveat）。

证据链：
1. `BL_ActionManager.h` 66-73 `PlayAction(..., short layer, ..., float layer_weight, ..., short blend_mode)` 与 107 `SetLayerWeight` 说明接口本身支持按层加权；
2. `BL_ActionManager.cpp` 128-130 `if (layer == 0) layer_weight = -1.f;` 只对首层关闭混合，其它层保留权重；
3. `BL_Action.cpp` 558-566 `if (m_layer_weight >= 0) obj->GetPose(&m_blendpose);` → 590-607 `ProcessArmatureBlending` → 604-605 `obj->BlendInPose(m_blendpose, m_layer_weight, m_blendmode)`；
4. `BL_ArmatureObject::GameBlendPose`（`Converter/BL_ArmatureObject.cpp` 59-104）在 `ACT_BLEND_BLEND` 时 `dstweight = 1 - srcweight`，loc/scale 线性加权、旋转四元数插值。

因此 B6 的实现（`KX_GameAdaptersEngine.cpp` 1655-1656）是 `(1-factor)A + factorB` 的真混合。仅有的 caveat 是**固定占用 layer 0/1**、只支持两个动作、会覆盖这两个层已有动作。原注释中的 "Layer based approximation" 与实现不符，已改为准确描述（仅注释，行为未变）。

### 7.3 `SetBoneTransform` 延迟：本轮不主动触发骨架更新

结论：**保留"延迟到下一次骨架更新"的语义，不改引擎代码**。文档已写明：本节点只保证姿态数据写入成功，**当前帧不改变渲染**。

延迟根因：`BL_ArmatureObject::ApplyPose()`（`Converter/BL_ArmatureObject.cpp` 409-423）判断 `m_lastapplyframe != m_lastframe` 才调用 `BKE_pose_where_is(depsgraph, scene, m_objArma)`，同一帧内第二次调用直接返回；`UpdateTimestep`（462-473）只改 `pose->ctime`，也不重算。

若要"在姿态写入后立刻生效"，需要做的事与成本：

1. 绕过 `ApplyPose` 守卫直接调 `BKE_pose_where_is`（或临时重置 `m_lastapplyframe`），并把骨架标脏：
   `scene->AppendToIdsToUpdate(&ob->id, ID_RECALC_TRANSFORM, false)`（`KX_Scene.h` 380，`BL_Action::ProcessPipeline` 已有同样用法）；
2. 适配器需要拿到 `KX_Scene`/depsgraph 句柄——当前动画适配器只持有 `KX_GameObject` 与 `BL_ActionManager`，要先扩接口与注入点；
3. 风险：同帧内与 `BL_Action::Update` 的 `ApplyAction/ProcessArmatureBlending/ApplyPose` 重复评估，可能覆盖本节点的写入或反过来被动作覆盖；还会改变 `m_lastapplyframe` 记账，影响下一帧的混合基准。要稳定需在引擎动画管线里加"外部姿态覆盖"层，属于引擎层改造。

评估成本：最小可行版本约 0.5-1 天，但必须在引擎侧改 `BL_ArmatureObject` 并回归全部动作/混合用例；与本轮"不写引擎代码、不跑全量 ninja"的约束冲突，故不在本轮实施，仅记录方案与风险。

## 8. B3 `ActionDone` 的证据补齐（Mock 层）

背景：加权核算把 B3 的 `ActionDone` 记为 `unclassified`（无逐项行为证据）。本轮补上
「动作未完成 → 不触发；完成 → 触发一次；继续 Tick → 不重复触发」的证据链。

- 实现位置：`KX_FunctionNodesB3.cpp:229-255`（`FuncActionDoneTick`）
- 语义：每帧读 `Animation().GetActionName(object, layer)`；名字变化时重新武装 `fired`；
  `GetActionName` 为空不触发；`IsActionDone(object, layer, name)` 为真且未触发时
  `PendingTrigger(OnDone)` 一次。
- 证据（Mock 动画适配器，`KX_GameAdapters.h:911-1024`）：
  1. `action="run"`、`done=false` 的第 1 帧：`OnDone` 计数 0；
  2. 置 `done=true` 后的第 2 帧：`OnDone` 计数 1；
  3. 第 3 帧仍为 1（不重复触发）。
- 载体：`tests/b3_audio_e2e.cpp` 的 `TestActionDone`，`/tmp/b3_audio_e2e_run.log`（failures=0）。
  重跑：`tests/run_all_e2e.sh --only b3_audio`。
- 分档：`unclassified` → **`mock-verified`**（不是 `full`）。

**证据等级与边界**：这是 **Mock 适配器层证据**，证明的是节点状态机与触发语义，
不是真实 `BL_ActionManager` 的完成回调时序。真实动作播完是否稳定地在同一帧被
`IsActionDone` 观测到，仍属 `pending real device`；不得用 Mock 结果冒充真机通过，
也不得表述为「动画完成信号已验证」。

动画域口径由此变化（11 节点：`full` 10、`mock-verified` 0→1、`unclassified` 1→0）：

- 严格（主口径，Mock 计 0）：**90.9% 不变**；
- 加权（`mock-verified` 计 0.5）：90.9% → **95.5%**；
- 覆盖：**100% 不变**。

只有真机确认后 `mock-verified` → `full`，严格/加权才会到 100%。全域（分母 286）
的三个口径见 `docs/CAPABILITY_WEIGHT_AUDIT.md` 第 0、5、11 节。
