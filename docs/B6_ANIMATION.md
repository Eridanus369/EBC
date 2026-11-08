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
| 编辑器表 | `scripts/startup/bge_gamenodes/editor/nodes/native_b6.py` | `NATIVE_B6` 24-115 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 232 |

## 2. 节点与端口

| 节点 | 输入 | 输出 | config |
|---|---|---|---|
| PlayActionByLayer | OnEnter, Object, Action(String), Layer(Int), Blend(Float) | OnDone | 无 |
| BlendActions | OnEnter, Object, ActionA(String), ActionB(String), Factor(Float) | OnDone | 无 |
| SetBoneTransform | OnEnter, Object, Bone(String), Position(Vector), Rotation(Vector) | OnDone | `mode`（Override 默认 / Additive / Multiplicative 预留） |
| ClearBoneOverride | OnEnter, Object, Bone(String) | OnDone | 无 |
| SetBoneWeight | OnEnter, Object, Bone(String), Weight(Float) | OnDone | 无 |
| PlayBoneAction | OnEnter, Object, Bone(String), Action(String) | OnDone | 无 |
| SetAnimationSpeed | OnEnter, Object, Layer(Int), Speed(Float) | OnDone | 无 |
| AddAnimationEvent | OnEnter, Object, Action(String), Frame(Int) | OnEvent | `event_name` |
| GetAnimationFrame | Object, Layer(Int) | Frame(Float), Normalized(Float) | 无（value node） |

## 3. 真实现与受引擎限制的占位

| 能力 | 状态 | 说明 |
|---|---|---|
| 分层播放 | **真实现** | `KX_GameObject::PlayAction(name, start, end, layer, priority, blendin, ...)`；帧范围由场景逻辑管理器按动作名查 `bAction::frame_start/frame_end`，区间缺失时失败并诊断 |
| 动作混合 | **真实现（固定 layer 0/1）** | `KX_GameAdaptersEngine.cpp` 传的是真实 layer 权重：A 放 layer 0（该层权重被引擎强制为 `-1`、不参与混合），B 放 layer 1（权重 `1-factor`）。引擎侧 `BL_Action.cpp` 调 `BlendInPose(m_blendpose, m_layer_weight, m_blendmode)`，`BL_ArmatureObject::GameBlendPose` 对 loc/scale 线性加权、对四元数插值，结果为 `(1-factor)*A + factor*B`。**不是近似**；受限于只支持两个动作、固定占用 layer 0/1（会覆盖该层已有动作） |
| 骨骼变换 | **真实现（动作播放中不被覆盖）** | 对象必须是 `OBJ_ARMATURE`；`BL_ArmatureObject::GetPose` → `BKE_pose_channel_find_name` → 写 `loc`/`rotmode=ROT_MODE_XYZ`/`eul` → `ApplyPoseImmediate()`。并登记到每骨骼覆盖表 `m_poseOverrides`，由 `BL_Action::UpdateArmatureAnimation()` 在 `ProcessArmatureBlending()` 之后、`ApplyPose()` 之前调用 `ApplyPoseOverrides()` 重放——**动作播放中手动姿态存活，不再整帧丢失**（§10.1，真实引擎证据） |
| 骨骼权重 | **引擎不支持** | 顶点组权重没有运行时 setter。`SetBoneWeight` 返回 false 并写 stderr「bone weight is not exposed by the engine」 |
| 骨骼级动作 | **引擎不支持** | `BL_ActionManager` 只能整对象播放动作。`PlayBoneAction` 返回 false 并写 stderr「per bone action playback is not exposed by the engine」 |
| 动作速度 | **真实现** | `SetActionSpeed`（B3 适配器方法，按 layer 设 `BL_Action::m_speed`） |
| 独立时间缩放 | **引擎不支持** | `BL_Action` 只有 `m_speed`，没有与速度正交的时间缩放字段；`SetActionTimeScale` 节点显式失败并写 `[SetActionTimeScale] BL_Action exposes playback speed only (m_speed); no independent time scale`，见 `DEFERRED_FOREVER.md` |
| 动画帧 | **真实现** | `GetActionFrame(layer)`；`Normalized` 由当前动作的 `frame_start/frame_end` 计算并 clamp 到 0..1，取不到当前动作时返回 0 |
| 归一化时间 | **真实现** | `GetActionNormalizedTime` 走 `BL_ActionManager::GetActionFrameAtTime()`；真实引擎实测单调递增、循环回绕归零（§10.3） |
| 骨骼姿态读回 | **真实现（只读）** | `GetBoneTransform` → `BL_ArmatureObject::GetPoseChannelTransform()`，返回渲染同帧的 `loc` 与 XYZ euler；用于运行时证明 A-1 的覆盖层生效 |
| 动画事件 | **近似实现（节点侧轮询，已绑定真实层/渲染帧）** | 有状态节点：按 `Action` 名称解析实际 layer，用「当前引擎帧时间」预测本帧将写入的帧，检测正向跨越后触发 `OnEvent`；循环回绕后重新 armed，每循环触发一次。仍非引擎回调；同名动作多层时绑定层号最小者，负 speed 倒放不产生反向事件（见 §9.4） |

## 4. AddAnimationEvent 的状态机

`B6AnimEventState` 保存 `object`、`event_name`、`action`、`frame`、`layer`、`previous_frame`、
`armed`、`action_bound`（字符串用固定 char 数组，避免原始状态块触发 `std::string` 构造）：

1. `OnEnter` 记录对象与参数，`armed = object && action`；能查到动作层时按「当前预测帧」初始化
   `previous_frame`（恰在事件帧进入可触发一次，已越过则不追溯）；
2. 每帧 Tick 用 `GetActionLayer(object, action)` 解析真实层，再用
   `GetActionFrameAtCurrentEngineTime()` 预测本帧将写入的帧；
3. 若 `armed` 且 `previous_frame < frame && current >= frame`，触发 `OnEvent` 一次并解除 `armed`；
4. 若 `current < previous_frame`（循环/重启回绕），重新 `armed`；回绕点恰在事件帧时立即触发；
5. 对象/动作无效时不触发、不挂起，写入诊断。

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
| E2E 动画事件 | 帧 0/2/4 不触发；帧 5 触发 1 次；回绕后帧 5 再触发，累计 2 次 | `tests/b61_e2e.cpp:731-770`，日志 `/tmp/c_block_b61.log` |
| 引擎不支持证据 | `SetBoneWeight` / `PlayBoneAction` 各写一条明确 stderr，节点仍到达适配器一次 | 同上 |

E2E 用 Mock 动画适配器驱动真实节点图，验证节点调度、端口连线与适配器契约；骨骼真实现的引擎路径见第 3 节代码引用，限制按「真实现/层近似/不支持」如实标注。

## 6. 状态分档（A.3）

分档定义：`full` = 真实现且行为符合端口语义；`approximate` = 能用但语义近似（写明近似方式）；`unsupported` = 明确返回失败（写明缺失的引擎能力）。逐项证据见 `/tmp/a3_b61_audit.log`。

| 节点 | 状态 | 近似方式 / 缺失能力 | 证据 |
|---|---|---|---|
| PlayActionByLayer | full | layer 0 在引擎侧被强制 `layer_weight=-1`（首层不混合、直接应用），符合"播放到指定层"语义 | `KX_GameAdaptersEngine.cpp` 1606-1632；`BL_ActionManager.cpp` 128-130 |
| BlendActions | full | 真实 layer 权重混合，结果 `(1-factor)*A + factor*B`；限于两个动作、固定 layer 0/1 | `KX_GameAdaptersEngine.cpp` 1635-1657；`BL_Action.cpp` 604-605；`BL_ArmatureObject.cpp` 59-104 |
| SetBoneTransform | full | 动作播放中生效：`SetPoseOverride` 登记每骨骼覆盖，`BL_Action::UpdateArmatureAnimation()` 在 `ProcessArmatureBlending()` 之后、`ApplyPose()` 之前重放。真实引擎 `lock` 运行 152 帧 rotX 恒 0，同动作 `action` 运行 rotX 0→1，证明不被覆盖 | §10.1；`BL_ArmatureObject.cpp` `SetPoseOverride/ApplyPoseOverrides`；`BL_Action.cpp` `UpdateArmatureAnimation` |
| SetBoneWeight | unsupported | 引擎无运行时顶点组权重 setter（`source/gameengine` 全量无 `vertex_group`/`VertexWeight` 匹配） | `KX_GameAdaptersEngine.cpp` 1704-1715 |
| PlayBoneAction | unsupported | `BL_ActionManager` 无 per-bone 逻辑，`BL_ArmatureObject` 无骨骼级动作接口 | `KX_GameAdaptersEngine.cpp` 1716-1730；`BL_ActionManager.cpp` 全文无 `bone` |
| SetAnimationSpeed / SetActionSpeed | full | `SetAnimationSpeed` 在 B6、`SetActionSpeed` 在 B3；均落到 `BL_ActionManager::SetActionSpeed(layer, speed)` | `KX_FunctionNodesB6.cpp` 428-440；`KX_FunctionNodesB3.cpp`；`BL_ActionManager.h` 102 |
| SetActionTimeScale | unsupported | `BL_Action` 只有 `m_speed`，无独立时间缩放字段；节点显式失败并回报理由 | §10.3；`DEFERRED_FOREVER.md` |
| AddAnimationEvent | approximate | 节点侧轮询（非引擎回调）；已按动作名绑定真实 layer、用当前引擎时间预测渲染帧、循环回绕重新 armed；多层同名/倒放仍有限制 | `KX_FunctionNodesB6.cpp` 444-477、479-506、508-566 |
| GetAnimationFrame | full | 取不到当前动作时返回 0（已定义默认值） | `KX_FunctionNodesB6.cpp` 490-501；`KX_GameAdaptersEngine.cpp` 1725-1745 |
| GetActionNormalizedTime | full | 真实引擎：单调递增，循环回绕归零；倍速下同一 tick 帧位 ×2 | §10.3；`check_anim.py speed` |
| GetBoneTransform | full | 只读姿态读回（`loc` + XYZ euler）；本轮为 A-1 运行时证据新增 | §10.1；`BL_ArmatureObject::GetPoseChannelTransform` |

**统计（11 个动画节点，8 个原有 + 3 个本轮新增）**：`full` 7 / `approximate` 1 / `unsupported` 3。
（`SetActionSpeed` 属 B3 既有能力，作为速度语义复用，不重复计入本分母。）

折算口径（三种并列，避免单一数字不可复现）：

| 口径 | 公式 | 动画结果（11 节点） | 原 23 节点合计（物理+动画旧分母） |
|---|---|---|---|
| A 严格真可用 | full=1，其他=0 | 7/11 = **63.6%** | 19/26（新分母，含 3 新节点）= **73.1%**；旧 23 分母下为 16/23 = 69.6% |
| B 加权可用 | full=1、approximate=0.5、unsupported=0 | 7.5/11 = **68.2%** | 21/26 = **80.8%**；旧分母 18.5/23 = 80.4% |
| C 节点覆盖 | full+approximate 计 1 | 8/11 = **72.7%** | 23/26 = **88.5%**；旧分母 21/23 = 91.3% |

合计按 C.1.1 之后的物理分档（物理 `full` 12 / `approximate` 3 / `unsupported` 0，见 `docs/B6_PHYSICS_ADVANCED.md` 第 6 节）与动画新分档（`full` 7 / `approximate` 1 / `unsupported` 3）相加；分母 26 与旧 23 的差是 B6.9-A 的 3 个新节点。**域内合计：`full 19 / approximate 4 / unsupported 3`（分母 23）**；全局六量已由 K 冻结（`N=325`、`RUN_ID=20261106-224926`），本表口径不变。

`SetBoneTransform` 不再按 `approximate` 计：动作播放中的覆盖已由真实引擎断言证明（§10.1），旧口径「动画 85%」仍然无法用上述任一公式复现，已废弃。

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

## 9. C 块（2026-11-06）动画引擎层复核与改造

本轮对 C.1–C.4 重新做了引擎能力勘察；结论以本节为准，§7.3 中「不主动触发骨架更新」的
旧口径已被本节 C.3 取代。

### 9.1 C.1 SetBoneWeight / PlayBoneAction：明确不做（引擎不支持）

勘察过的具体类与方法：

- `KX_GameObject`（[KX_GameObject.h:145](../source/gameengine/Ketsji/KX_GameObject.h#L145)、
  [:292-342](../source/gameengine/Ketsji/KX_GameObject.h#L292-L342)）只有整对象动作 API：
  `GetActionManager()/NoCreate`、`PlayAction`、`GetActionFrame`、`GetActionName`、
  `SetActionFrame`、`SetLayerWeight`、`StopAction`；没有任何 bone/vertex-group 参数。
- `BL_ActionManager`（[BL_ActionManager.h:63-134](../source/gameengine/Ketsji/BL_ActionManager.h#L63-L134)）
  以 `layer -> BL_Action` 组织整动作，`PlayAction` 无骨骼过滤参数；
  [BL_Action.cpp:581-596](../source/gameengine/Ketsji/BL_Action.cpp#L581-L596) 的
  `UpdateArmatureAnimation()` 最终调用整对象 `ApplyAction(m_action, ...)`。
- `BL_ArmatureObject`（[BL_ArmatureObject.h:95-102](../source/gameengine/Converter/BL_ArmatureObject.h#L95-L102)）
  只有 `GetPose/ApplyPose/GameBlendPose/ApplyAction/BlendInPose`，无按骨骼设权重或播放动作的入口。
- 运行时权重写入还受 GPU 蒙皮缓存限制：`source/gameengine` 全量没有
  `vertex_group/VertexGroup` setter；Blender 侧虽可写
  `Mesh::deform_verts_for_write()`，但 GPU 蒙皮静态缓存只把 dvert **指针**
  纳入 hash（[draw_armature_skinning.cc:984-985](../source/blender/draw/intern/gpu_modifiers/draw_armature_skinning.cc#L984-L985)），
  原地改权重不会使缓存失效。要做成可靠接口，需要引擎层同时补「权重写入 + 缓存失效/重算」，
  工期见 `DEFERRED_FOREVER.md` §2 第 6/7 条（10~15 天 / 8~12 天）。

E2E：本轮**无法产出骨骼权重变化断言**，因为没有运行时权重 setter，也没有可触发的真实骨骼
测试资产；`tests/b61_e2e.cpp` 只验证节点仍走适配器并打印引擎不支持诊断，不冒充真机通过。

### 9.2 C.2 BlendActions：做了——引擎本身就是真 layer 混合

`BL_ActionManager` 用 `BL_ActionMap m_layers`（[BL_ActionManager.h:49](../source/gameengine/Ketsji/BL_ActionManager.h#L49)）
保存任意 layer，`Update()` 逐层调用；layer 0 是基础层，layer ≥1 通过
`m_layer_weight` 走 `ProcessArmatureBlending()` → `BlendInPose()`（
[BL_Action.cpp:596-641](../source/gameengine/Ketsji/BL_Action.cpp#L596-L641)）。现有
`BlendActions` 把 A 放 layer 0、B 放 layer 1，得到
`(1-factor)·A + factor·B`，属于真权重混合，不是双 layer 近似。限制仍是接口只暴露两层、
会覆盖 layer 0/1 既有动作（见 `KNOWN_LIMITATIONS.md` §18）。

### 9.3 C.3 SetBoneTransform：做了（带同帧动作覆盖边界）

新增 `BL_ArmatureObject::ApplyPoseImmediate()`（[BL_ArmatureObject.cpp:425-445](../source/gameengine/Converter/BL_ArmatureObject.cpp#L425-L445)），
在写入 pose channel 后立即执行 `BKE_pose_where_is()` 并用 `ID_RECALC_TRANSFORM` 标记
depsgraph；`KX_GameAdaptersEngine.cpp` 的 `SetBoneTransform` 已改用该入口
（[KX_GameAdaptersEngine.cpp:1663-1701](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1663-L1701)）。

- 无 active action 时：当帧可见，不再等下一次骨架更新。
- 有 active action 且同一骨骼被 fcurve 驱动时：图 tick 早于 `Render() -> UpdateAnimations()`
  （[KX_KetsjiEngine.cpp:591](../source/gameengine/Ketsji/KX_KetsjiEngine.cpp#L591)、
  [:752](../source/gameengine/Ketsji/KX_KetsjiEngine.cpp#L752)），同帧动作更新会覆盖本次写入；
  该通道可能整帧丢失，而不是延迟一帧。要保证覆盖，需要在混合管线之后加姿态覆盖层。

本轮只做了编译验证（5 个引擎对象 + B6 节点对象全部通过），未产出真实骨骼场景截图断言；
不再声称「需等待下一次骨架更新」的旧结论，但也不声称「动作播放中当帧一定可见」。

### 9.4 C.4 AddAnimationEvent：做了——绑定真实层 + 对齐渲染帧 + 循环重新武装

- `BL_ActionManager` 追加 `FindLayerByActionName()` / `GetActionFrameAtTime()`；`BL_Action`
  追加只读的 `GetFrameAtTime()`，按上次更新时间预测**当前引擎时间**将写入的帧，
  不改播放状态（[BL_Action.cpp:267-299](../source/gameengine/Ketsji/BL_Action.cpp#L267-L299)、
  [BL_ActionManager.cpp:199-213](../source/gameengine/Ketsji/BL_ActionManager.cpp#L199-L213)）。
- 节点侧 `AddAnimationEvent` 现在按 `Action` 名称解析实际 layer，用预测帧做跨越检测；
  循环回绕时重新 armed，同一事件每循环触发一次；进入时若已越过事件帧不追溯触发，
  恰在事件帧进入则触发一次（[KX_FunctionNodesB6.cpp:444-476](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L444-L476)、
  [:508-566](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L508-L566)）。
- E2E 新增于 `tests/b61_e2e.cpp:731-770`（`BuildEventGraph` 见 `:475-503`）：
  帧 0/2/4 不触发，帧 5 触发 1 次；回绕到 0 后帧 5 再触发，累计 2 次。运行结果
  `PASS b61_e2e`（日志 `/tmp/c_block_b61.log`）。这验证的是适配器/节点层时序，
  不是真实动作播放器的帧事件回调；真实 `BL_Action` 的预测走查仍需真机场景。
- 边界（**保留 `approximate`，不改代码**）：同名动作出现在多层时绑定层号最小的一层
  （`BL_ActionManager.cpp:199-207`）；负 speed 倒放只按正向跨越检测，不产生反向事件
  （`KX_FunctionNodesB6.cpp:632-651`）；仍非引擎回调——GraphTick（`KX_KetsjiEngine.cpp:591`）
  早于 `Render()` 的 `UpdateAnimations()`（`:752`、`:940-956`），节点只能用
  `BL_Action::GetFrameAtTime()`（`BL_Action.cpp:267-299`）外推，`RESTRICT_ANIMATION`
  （`KX_KetsjiEngine.cpp:943-952`）与同 tick 内 `SetActionFrame`（`BL_ActionManager.cpp:70-76`）
  会让预测帧偏离实际写入帧。详见 `docs/ANIMATION_RESCORE.md` §3.5。

### 9.5 状态矩阵

| 项 | 结论 | 档位 | 关键证据 |
|---|---|---|---|
| C.1 SetBoneWeight / PlayBoneAction | 引擎不支持，需引擎层改造 | 明确不做 | 9.1；`DEFERRED_FOREVER.md` §2 #6/#7 |
| C.2 BlendActions | 真 layer 混合已接 | 做了 | 9.2；`BL_ActionManager` 多 layer + `BlendInPose` |
| C.3 SetBoneTransform | 立即重算已接；有 active action 时同帧被覆盖 | 做了（带边界） | 9.3；`ApplyPoseImmediate` |
| C.4 AddAnimationEvent | 真实层绑定 + 渲染帧预测 + 循环重复 | 做了（节点侧） | 9.4；`b61_e2e` PASS |

## 10. B6.9-A（2026-11-06）：动作中通道锁定、混合断言、速度/时间与骨骼权重

| 项 | 结论 | 档位 | 关键证据 |
|---|---|---|---|
| A.1 动作中通道锁定 | 接上了，`SetBoneTransform` 在动作播放中不被覆盖 | full | §10.1；`setOverride`/`ApplyPoseOverrides` + `check_anim.py lock` 3/3 |
| A.2 混合测试补全 | 真 layer 混合；撤掉「近似」；补 factor 0/0.5/1，0.5 严格居中 | full | §10.2；`check_anim.py blend` 3/3；`b61` 44/0 |
| A.3 速度/时间缩放/归一化时间 | `SetActionSpeed` 复用既有；`GetActionNormalizedTime` 新增；`SetActionTimeScale` 无引擎能力 → FOREVER | full / full / unsupported | §10.3；`check_anim.py speed` 3/3 |
| A.4 骨骼权重 FOREVER | 整对象 API（无 bone 参数）+ GPU 缓存只 hash dvert 指针 | FOREVER（不进分母） | §10.4；`DEFERRED_FOREVER.md` §2 #6 |

### 10.1 A-1 后置姿态覆盖层

`SetBoneTransform` 现在做两件事：写 pose channel（`loc`/`eul`）并立即 `ApplyPoseImmediate()`；同时调用新增的 `BL_ArmatureObject::SetPoseOverride(bone, position, rotation)`，把该骨骼的目标值登记进 `m_poseOverrides`。

播放动作时，`BL_Action::UpdateArmatureAnimation()` 的管线变为：

```
ApplyAction → ProcessArmatureBlending → (ApplyPoseOverrides || ApplyPose) → ProcessPipeline → UpdateTimestep
```

覆盖层在**混合之后**重放，所以 fcurve 与 layer 混合都覆盖不了它；`ApplyPoseOverrides()` 成功时设置 `m_lastapplyframe = m_lastframe`，避免同一帧再评估一次。

真实引擎证据（`blenderplayer`，同一 `Spin` 动作 2.5 s，`tests/verify_gap/`）：

| 运行 | 动作归一化 | 骨骼 rotX | 断言 |
|---|---|---|---|
| `action`（只播动作） | 0 → 0.993（69 帧非零） | 70 个不同值，0 → 1.000 | 动作确实驱动该通道 |
| `lock`（每帧 `SetBoneTransform` rot=0） | 0 → 0.993（69 帧非零） | **152 帧仅 1 个值：0.000** | 覆盖在混合后存活 |

复现：`python3 tests/verify_gap/make_anim.py`（Blender 生成 `.blend`）→ `blenderplayer` → `python3 tests/verify_gap/check_anim.py lock action.log lock.log`（3 PASS / 0 FAIL）。

### 10.2 A-2 真混合与方向修正

`BlendActions` 走 `KX_GameObject::PlayAction` 的真实 layer 0/1 + `BL_ArmatureObject::BlendInPose`，是**真加权混合**，不是双 layer 近似。`KNOWN_LIMITATIONS.md` 中「双 layer 权重近似」的表述已由 C.2 移除，本轮复核全文无此标签。

本轮实测还发现权重方向反了：实现传入 `factor` 时实际结果是 `factor·A + (1-factor)·B`（factor=0 得到 B）。已把 layer 1 权重改为 `1-factor`，现在结果与文档一致：`(1-factor)·A + factor·B`。

真实引擎断言（`check_anim.py blend`，3/3 PASS）：

| factor | 期望 | 实测骨骼 rotX |
|---|---|---|
| 0.0 | 全 A | 0.0000 |
| 0.5 | 严格居中 | 0.5000（`min(0,1) < 0.5 < max(0,1)`） |
| 1.0 | 全 B | 1.0000 |

`b61_e2e` 同步为 `44 assertions / 0 failures`（K 冻结 `RUN_ID=20261106-224926`；旧 32 条已被覆盖；`b64` 94/0 无回归）。

### 10.3 A-3 速度、时间缩放、归一化时间

- `SetActionSpeed`：B3 已存在，落 `BL_ActionManager::SetActionSpeed(layer, speed)`，本轮未重复注册。
- `SetActionTimeScale`：新增节点，但引擎无独立时间缩放字段（`BL_Action` 只有 `m_speed`，`BL_ActionManager` 只有 `SetActionSpeed`）→ 显式失败并回报理由，写入 `DEFERRED_FOREVER.md`，不进分母。
- `GetActionNormalizedTime`：新增 value node，走 `GetActionFrameAtTime()`。

真实引擎证据（`check_anim.py speed`，3/3 PASS）：
- 同一 tick 下，speed=2 的归一化位置是 speed=1 的 **2.00 倍**（中位比值 2.000，样本 25 条）。
- 归一化时间单调递增（每段内 `next > prev`），达到 1.0 后回绕到 0.0（日志中多次 `1.000 → 0.000`）。

### 10.4 A-4 骨骼权重 FOREVER（可核）

- `BL_ActionManager::PlayAction(...)` / `SetLayerWeight()` / `SetActionSpeed()` 只到 layer 级，**无 bone 参数**（`BL_ActionManager.h:63/102/107`）；`BL_ArmatureObject` 无写入顶点权重的 setter。
- GPU 静态蒙皮缓存 key 只含 `mesh_orig->deform_verts().data()` 指针（`source/blender/draw/intern/gpu_modifiers/draw_armature_skinning.cc:984-985`），原地改 `MDeformVert::weight` 不改变指针 → 缓存不失效。
- 结论不变：`SetBoneWeight` FOREVER，**不进 95% 分母**。

### 10.5 动画域重评分（按实际证据）

11 个动画节点（8 个原有 + 3 个本轮新增）：`full` 7 / `approximate` 1 / `unsupported` 3。
- A 严格真可用：**7/11 = 63.6%**
- B 加权可用：**7.5/11 = 68.2%**
- C 节点覆盖：**8/11 = 72.7%**

分子分母可逐项核对见 §6。指令中的 72% / 90% 不采用；本轮也不写「动画 95%」。

### 10.6 B6.10 骨骼姿态 additive（方案 B）与清除

`SetBoneTransform` 新增 `mode` 枚举（`KX_PoseBlendMode`），默认 `Override`，旧图行为不变；同时新增 `ClearBoneOverride` 节点，端口 `OnEnter, Object, Bone(String)` / `OnDone`，调用 `KX_GameAdaptersEngine::ClearPoseOverride(obj, bone)` 从 `m_poseOverrides` 删除该骨骼条目。

四条已拍板的语义（实现与注释必须与此一致）：

1. **数学运算用四元数乘，不用欧拉加。** 欧拉加会引入万向锁，override 层存目标四元数/增量四元数，应用时用 `Quaternion::Multiply`（[BL_ArmatureObject.cpp](../source/gameengine/Converter/BL_ArmatureObject.cpp#L575-L600)）。
2. **增量相对「动作管线求值后、override 层应用前」的姿态**，顺序固定为：fcurve 求值 → layer blending → override 层（ADD: `result = current * delta`；OVERRIDE: `result = target`）→ 写回 `pose_channel->quat`。**不是相对上一帧姿态**。覆盖层重放点见 [BL_Action.cpp](../source/gameengine/Ketsji/BL_Action.cpp#L596-L606)。
3. **每帧不累积。** `m_poseOverrides` 只存 `{bone, mode, value}`（用户输入的绝对目标或增量），不存每帧结算后的最终姿态；每帧都以「动作求值结果」为基底重算（基线在 blending 之后、override 之前由 `CapturePoseOverrideBases()` 快照）。因此即使节点只在 `OnInit` 触发一次，后续每帧也不会叠加漂移。
4. **必须能清除。** 不做清除口 = 用户一旦触发过就永远回不到动画；`ClearPoseOverride` 删除条目与基线，并用基线恢复 channel，未知骨骼返回 `false` 且不崩溃。

`Multiplicative = 2` 为 P2 预留：适配器显式拒绝并打印诊断，不会静默当 `Override` 处理。

节点/接口落点：

| 层 | 位置 |
|---|---|
| `KX_PoseBlendMode` 与四条语义注释 | [BL_ArmatureObject.h](../source/gameengine/Converter/BL_ArmatureObject.h#L60-L131) |
| `SetPoseOverride` / `CapturePoseOverrideBases` / `ClearPoseOverride` | [BL_ArmatureObject.h](../source/gameengine/Converter/BL_ArmatureObject.h#L195-L229)、[BL_ArmatureObject.cpp](../source/gameengine/Converter/BL_ArmatureObject.cpp#L470-L600) |
| 适配器 `SetBoneTransform(..., int blend_mode)` / `ClearPoseOverride` | [KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2042-L2113) |
| 节点函数与规格表 | [KX_FunctionNodesB6.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L472-L514)、[#L845-L910](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L845-L910) |
| 编辑器 `mode` 下拉框与节点入口 | [native_b6.py](../scripts/startup/bge_gamenodes/editor/nodes/native_b6.py#L99-L130) |

E2E：`tests/b61_e2e.cpp` 断言数 **44 → 50**（新增 6 条：Override 默认、Additive 透传、60 帧不累积、清除生效、清除不存在骨骼不崩溃、`mode` 读写 round-trip），`b61` 50 PASS / 0 FAIL。编辑器侧同步加了 `ClearBoneOverride`（`native_b6.py`、`node_palette.py`、`node_menu.py`），`native_metadata_gen.py` 由 `generator/gen_metadata.py` 重新生成；静态契约检查（`port_contract.py` 0 error、`enum_consistency.py` 126 对 / 0 不一致）与 C++ 端口表一致。

真实引擎证据（`VIEW3D_OT_game_start` 嵌入式采样，同一 `Spin` 动作把 `Head` 从 0 转到 0.2 弧度为 1..61 帧；`additive` 只在 `OnInit` 触发一次）：

| 运行 | 样本 | 观测 | 结论 |
|---|---|---|---|
| `plain`（只播动作） | 152 | rotX 0.000 → 0.200 | 动作基线 |
| `additive60`（`mode=1`，抵消 `plain`） | 151 | 每帧差值恒为 **0.5000**（帧 1..61，min=max=0.5000） | 叠加正确；**不累积**（不是 60×0.5） |
| `override`（`mode=0`） | 151 | rotX 恒为 **0.500** | 旧行为保持 |
| `additive` + `ClearBoneOverride`（1.0 s） | 60 帧叠加 + 91 帧清除后 | 清除前差值恒 0.500；清除当 tick 仍有 1 帧旧值，下一帧起与 `plain` 误差 **0.000** | 清除后完全回到动作驱动 |

