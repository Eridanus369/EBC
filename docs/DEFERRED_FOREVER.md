# DEFERRED_FOREVER — 明确不做清单（Phase D.2 终稿）

本文件是 `generator/native_audit.py` 第四层「用户可感知兼容度」的唯一排除依据。
审计脚本只读取文末 `<!-- excluded:start/end -->` 机器区间：只有出现在该区间的 `bl_idname`
且确实是 legacy 行（出现在 `deferred_matrix.json` 的待处理集合）时，才从分母扣除。
**已被 B6.6 / C 阶段实现的条目绝不留在该区间**，见 §3。

## 0. 统计与复核

| 项 | 数量 | 说明 |
|---|---|---|
| 原基线 | 57 | 渲染内部件 40 + 废弃/VR/FMod 17 |
| 复核后仍保留 | **57** | 逐条 `grep <bl_idname> native_catalog.py` 命中 0；57 行仍在 `deferred_matrix.json` 的 `渲染`/`废弃` 域 |
| 移出 | **0** | B6.6（LogicGate/WorldPosition/Touch）、C 阶段（字符串/音频/文件）均未改动这 57 条 |
| 新增 FOREVER | **9**（7 组） | §2；其中 StartSound/StopSound、SetBoneWeight/PlayBoneAction 为成对项 |
| 合计 | **66** | 57 + 9 |

复核方法（可重跑，直接调用审计脚本自己的解析函数）：

```bash
python3 - <<'EOF'
import sys; sys.path.insert(0, 'scripts/addons_core')
from bge_netlogic.generator.native_audit import _forever_ids
ids = sorted(_forever_ids())
print(len(ids), ids)          # 期望 57 条
assert 'NLSetMaterial' not in ids and 'LogicNodeStartSound' not in ids
EOF
```

> 维护提示：解析器取**第一次出现**的区间起点标记，因此本文件正文里不要再写出与标记
> 完全相同的字符串（除 §5 的机器区间本身）——那会让解析起点错位。

**证据缩写**：`matrix 渲染/低/难` = `deferred_matrix.json` 中该行的 `domain/frequency/cost`；
所有基线行同时满足「[native_catalog.py](../scripts/addons_core/bge_netlogic/editor/nodes/native_catalog.py) 0 命中」。

**工期口径**（估算，非承诺）：`cost=易` → 2~3 天，`cost=中` → 4~6 天，`cost=难` → 8~12 天；
`废弃` 域记「不可排期」——依赖的接口在 5.3 已不存在，重实现等于重写已删除的子系统。

## 1. 基线 57 条（逐条）

### 1.1 渲染内部件（40 条）

这些节点操作的是 GPU 后处理、材质/几何节点树、光源与曲线 datablock 的内部句柄，
属渲染管线内部实现，不是游戏逻辑语义；原生节点层不暴露这些 API，
移植还会把帧率与图形后端耦合进逻辑图（本类整体不为 290 全量口径贡献可用率）。

**状态：40 条全部 `FOREVER（基线）`**；下表只列理由、证据与工期。

| # | 节点（legacy 类） | 一句话理由 | 证据 | 工期 |
|---|---|---|---|---|
| 1 | `NLAddFilter`（ULAddFilter） | 运行时插入 GPU 2D filter 栈，节点层没有 filter 句柄 | matrix 渲染/低/难 | 8~12 天 |
| 2 | `LogicNodeDraw`（DrawNode） | 调试绘制（drawLine/drawBox）属调试面，且与帧率/后端耦合 | matrix 渲染/低/难 | 8~12 天 |
| 3 | `NLMakeUniqueLight`（ULMakeUniqueLight） | 运行时复制 light datablock，ownership 与 depsgraph 不受节点控制 | matrix 渲染/低/中 | 4~6 天 |
| 4 | `NLPlayMaterialSequence`（ULPaySequence） | 材质动画序列需要 image sequence 与材质动画调度接口 | matrix 渲染/低/难 | 8~12 天 |
| 5 | `NLRemoveFilter`（ULRemoveFilter） | 同 1，filter 栈无节点可见句柄 | matrix 渲染/低/难 | 8~12 天 |
| 6 | `NLRemoveOverlayCollection`（ULRemoveOverlayCollection） | overlay 集合属 view layer，不是游戏场景对象 | matrix 渲染/低/中 | 4~6 天 |
| 7 | `NLActionReplaceMesh`（ULReplaceMesh） | 运行时换 mesh 会触发 depsgraph 重评估，无安全入口 | matrix 渲染/中/中 | 4~6 天 |
| 8 | `NLActionSetCollectionVisibility`（ULSetCollectionVisibility） | view layer 集合可见性不在 KX 场景模型里 | matrix 渲染/中/中 | 4~6 天 |
| 9 | `NLSetCurvePoints`（ULSetCurvePoints） | evaluated curve 的顶点数据没有运行时写入口 | matrix 渲染/低/中 | 4~6 天 |
| 10 | `NLSetExposureAction`（ULSetExposure） | 全局 color management 参数，作用于整帧而非对象 | matrix 渲染/低/中 | 4~6 天 |
| 11 | `NLSetFilterState`（ULSetFilterState） | 同 1，filter 栈 | matrix 渲染/低/中 | 4~6 天 |
| 12 | `NLSetGammaAction`（ULSetGamma） | 同 10，全局 color management | matrix 渲染/低/易 | 2~3 天 |
| 13 | `NLSetGeometryNodeAttribute`（ULSetNodeValue） | 需在运行时写 evaluated geometry node tree 的属性 | matrix 渲染/低/难 | 8~12 天 |
| 14 | `NLSetGeometryNodeValue`（ULSetNodeSocket） | 同上，写 node socket 需要 depsgraph 重评估 | matrix 渲染/低/难 | 8~12 天 |
| 15 | `NLSetLightColorAction`（ULSetLightColor） | `KX_LightObject` 无运行时颜色 setter，需改渲染线 | matrix 渲染/中/易 | 2~3 天 |
| 16 | `NLSetLightEnergyAction`（ULSetLightEnergy） | 同上，无能量 setter | matrix 渲染/中/易 | 2~3 天 |
| 17 | `NLSetLightShadowAction`（ULSetLightShadow） | 光源阴影参数属渲染管线，无节点入口 | matrix 渲染/低/中 | 4~6 天 |
| 18 | `NLSetMaterialNodeAttribute`（ULSetMatNodeValue） | 运行时改材质节点树，归属与重评估都不受节点控制 | matrix 渲染/低/难 | 8~12 天 |
| 19 | `NLSetMaterialNodeValue`（ULSetMatNodeSocket） | 同上，写材质 socket | matrix 渲染/低/难 | 8~12 天 |
| 20 | `NLSetNodeTreeNodeAttribute`（ULSetNodeValue） | 通用 node tree 写入，无所有权语义 | matrix 渲染/低/难 | 8~12 天 |
| 21 | `NLSetNodeTreeNodeValue`（ULSetNodeSocket） | 同 20 | matrix 渲染/低/难 | 8~12 天 |
| 22 | `NLSetOverlayCollection`（ULSetOverlayCollection） | overlay 集合属 view layer | matrix 渲染/低/中 | 4~6 天 |
| 23 | `NLShowFramerate`（ULShowFramerate） | 帧率叠层是调试显示，不产出游戏逻辑值，收益为零 | matrix 渲染/低/中 | 4~6 天 |
| 24 | `NLSetProfile`（ULSetProfile） | profiler 开关只服务编辑器诊断 | matrix 渲染/低/中 | 4~6 天 |
| 25 | `NLToggleFilter`（ULToggleFilter） | 同 1，filter 栈 | matrix 渲染/低/中 | 4~6 天 |
| 26 | `LogicNodeDistributeCurvePoints`（DistributeCurvePointsNode） | 曲线重采样依赖 evaluated curve | matrix 渲染/低/中 | 4~6 天 |
| 27 | `LogicNodeCurveInterpolation`（CurveInterpolationNode） | 曲线插值依赖 evaluated curve | matrix 渲染/低/中 | 4~6 天 |
| 28 | `NLGetCurvePoints`（ULGetCurvePoints） | 读 evaluated curve，节点层无法取得该数据块 | matrix 渲染/低/易 | 2~3 天 |
| 29 | `LogicNodeGetFont`（ULGetFont） | 字体 datablock 属编辑器资源，游戏运行时没有稳定句柄 | matrix 渲染/低/易 | 2~3 天 |
| 30 | `NLGetGeometryNodeAttribute`（ULGetNodeAttribute） | 读 evaluated geometry node 属性 | matrix 渲染/低/难 | 8~12 天 |
| 31 | `NLGetGeometryNodeValue`（ULGetNodeSocket） | 读 node socket，需重评估 | matrix 渲染/低/难 | 8~12 天 |
| 32 | `NLGetNodeTreeNodeAttribute`（ULGetNodeAttribute） | 读 node tree 属性，无所有权语义 | matrix 渲染/低/难 | 8~12 天 |
| 33 | `NLGetNodeGroupNodeValue`（ULGetNodeSocket） | 读 node group socket，跨 group 无稳定路径 | matrix 渲染/低/难 | 8~12 天 |
| 34 | `NLGetImage`（ULGetImage） | image datablock 属编辑器资源 | matrix 渲染/低/易 | 2~3 天 |
| 35 | `NLGetLightColorAction`（ULGetLightColor） | 读光源颜色，仍属渲染线 | matrix 渲染/中/易 | 2~3 天 |
| 36 | `NLGetLightEnergy`（ULGetLightEnergy） | 读光源能量，仍属渲染线 | matrix 渲染/中/易 | 2~3 天 |
| 37 | `NLGetMaterialNode`（ULGetMaterialNode） | 读材质节点，需 evaluated 数据 | matrix 渲染/中/易 | 2~3 天 |
| 38 | `NLGetMaterialNodeAttribute`（ULGetMaterialAttribute） | 读材质节点属性 | matrix 渲染/低/难 | 8~12 天 |
| 39 | `NLGetMaterialNodeValue`（ULGetMaterialSocket） | 读材质 socket | matrix 渲染/低/难 | 8~12 天 |
| 40 | `LogicNodeEvaluateCurve`（EvaluateCurveNode） | 曲线求值依赖 evaluated curve | matrix 渲染/低/中 | 4~6 天 |

### 1.2 废弃执行模型 / VR / FMod（17 条）

依赖已移除的 uplogic 子网络、logic tree 属性、FMOD 或 VR 专属插件；
5.3 的图执行计划没有、也不会保留对应运行时接口。

**状态：17 条全部 `FOREVER（基线）`**；下表只列理由、证据与工期。

| # | 节点（legacy 类） | 一句话理由 | 证据 | 工期 |
|---|---|---|---|---|
| 1 | `NLActionInstallSubNetwork`（ULInstallSubNetwork） | 5.3 已无子网络执行对象，安装目标不存在 | matrix 废弃/极低/中 | 不可排期 |
| 2 | `NLActionExecuteNetwork`（ULExecuteSubNetwork） | 同上，无子网络执行器 | matrix 废弃/极低/中 | 不可排期 |
| 3 | `LogicNodeSetLogicTreeProperty`（ULSetLogicTreeProperty） | logic tree 属性模型在 5.3 已移除 | matrix 废弃/极低/中 | 不可排期 |
| 4 | `LogicNodeToggleLogicTreeProperty`（ToggleLogicTreePropertyNode） | 同上 | matrix 废弃/极低/中 | 不可排期 |
| 5 | `NLStartLogicNetworkActionNode`（ULStartSubNetwork） | 无子网络启动入口 | matrix 废弃/极低/中 | 不可排期 |
| 6 | `NLStopLogicNetworkActionNode`（ULStopSubNetwork） | 无子网络停止入口 | matrix 废弃/极低/中 | 不可排期 |
| 7 | `NLConditionLogitNetworkStatusNode`（ULLogicTreeStatus） | 无 logic tree 状态对象 | matrix 废弃/极低/中 | 不可排期 |
| 8 | `LogicNodeGetLogicTreeProperty`（ULGetLogicTreeProperty） | 同上 | matrix 废弃/极低/中 | 不可排期 |
| 9 | `NLGetVRControllerValues`（ULGetVRControllerValues） | 构建未链接 VR device 接口 | matrix 废弃/极低/难 | 不可排期 |
| 10 | `NLGetVRHeadsetValues`（ULGetVRHeadsetValues） | 同上 | matrix 废弃/极低/难 | 不可排期 |
| 11 | `LogicNodeFModLoadBank`（FModLoadBankNode） | 仓库未链接 FMOD 第三方库 | matrix 废弃/极低/难 | 不可排期 |
| 12 | `LogicNodeFModStartEvent`（FModStartEventNode） | 同上 | matrix 废弃/极低/难 | 不可排期 |
| 13 | `LogicNodeFModGetEventAttribute`（FModGetEventAttributeNode） | 同上 | matrix 废弃/极低/难 | 不可排期 |
| 14 | `LogicNodeFModSetEventAttribute`（FModSetEventAttributeNode） | 同上 | matrix 废弃/极低/难 | 不可排期 |
| 15 | `LogicNodeFModGetEventParameter`（FModGetEventParameterNode） | 同上 | matrix 废弃/极低/难 | 不可排期 |
| 16 | `LogicNodeFModSetEventParameter`（FModSetEventParameterNode） | 同上 | matrix 废弃/极低/难 | 不可排期 |
| 17 | `LogicNodeFModModifyEventParameter`（FModModifyEventParameterNode） | 同上 | matrix 废弃/极低/难 | 不可排期 |

> 备注：`deferred_matrix.json` 中 `frequency=极低` 的 17 行与「废弃」17 行是**同一组**
> （`bl_idname` 交集 17/17，无独立条目），因此不另列第二份清单。

## 2. 本轮新增 FOREVER（9 条 / 7 组）

| # | 节点/能力 | 状态 | 一句话理由 | 证据 | 若真要做的工期 |
|---|---|---|---|---|---|
| 1 | `StartSound`（legacy `LogicNodeStartSound`） | FOREVER | Python 侧只有 `SCA_SoundActuator` 实例、内部 `AUD_Handle` 不暴露，legacy `Sound` 端口是 `nl_type=29` 的 Python socket，桥接需 5 天 | [SOUND_BRIDGE_EVAL.md](SOUND_BRIDGE_EVAL.md) §0/§2；[SCA_SoundActuator.cpp:376-413](../source/gameengine/GameLogic/SCA_SoundActuator.cpp#L376-L413) | 5 天（路径 A 前提不成立；路径 B 成本在 20 入/3 出与 `nl_type=29`） |
| 2 | `StopSound`（legacy `NLActionStopSound`） | FOREVER | 句柄生产者 `StartSound` 未迁移，单迁 StopSound 无合法句柄来源 | 同上 §0；[matrix](deferred_matrix.json) 行 `NLActionStopSound` | 与 1 同批，不单独排期 |
| 3 | AES（`EncryptData`/`DecryptData` 的 `algorithm=aes`） | FOREVER | `BLI_` 无加密入口、仓库未链接加密库；OpenSSL 引入需 5 天且只换来一个可选算法 | [B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §加密（90/105/107/113） | 5 天（依赖体积 + 许可证 + 交叉编译） |
| 4 | 混音总线（`SetAudioBusVolume`） | FOREVER（作为“原生总线”） | audaspace 无命名 bus/mixer 路由（`Mixer` 是格式重采样器、`Superpose` 是二元叠加），现实现只是按名字注册的增益组 | [B6_AUDIO.md](B6_AUDIO.md) §4.5（91-106） | 真总线需改 audaspace 内核，约 8~10 天，收益为零 |
| 5 | Touch 真重叠（`Collision` 的 overlap 对象列表） | FOREVER | `KX_PhysicsAdapter` 没有同步 `contactTest` 入口，逐帧接触流拿不到“重叠对象集合”，属设计变更 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §3；`grep contactTest source/gameengine` 0 命中 | 6~8 天（新增同步接触查询 + 对象列表语义） |
| 6 | `SetBoneWeight` | FOREVER | `KX_GameObject` 无顶点组 API，`source/gameengine` 全量无 `vertex_group/VertexWeight` setter | [B6_ANIMATION.md](B6_ANIMATION.md) §7.1（105-109） | 10~15 天（mesh deform layer 写入 + 蒙皮重算） |
| 7 | `PlayBoneAction` | FOREVER | `BL_ActionManager` 全文无骨骼分支，`BL_ArmatureObject` 无骨骼级动作接口 | [B6_ANIMATION.md](B6_ANIMATION.md) §7.1（110） | 8~12 天（FCurve 路径过滤 + pose 混合） |
| 8 | Recast 光栅化（`BuildNavMesh` 的 `agent_radius`/`agent_height`） | FOREVER（本轮） | 只编译了 Detour 查询，没有 Recast 网格光栅化，agent 尺寸与动态障碍重烘焙无后端 | [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) 76/134/184 | 3~5 天，属 B6.9 排期 |
| 9 | `KX_RuntimeValue` 的 int64 句柄 | FOREVER（评估后不做） | 新增 Int64 值类型要改值池、`port_contract` 标签与 `enum_consistency`（共享文件），而分配上限 + 边界守卫已让截断不可能发生 | [SOUND_BRIDGE_EVAL.md](SOUND_BRIDGE_EVAL.md) §5；[KX_RuntimeValue.h:100](../source/gameengine/GameLogic/GameNodes/KX_RuntimeValue.h#L100) | 0.5~1 天 |

## 3. 已实现、明确不在本清单的条目（移出 0 条，但需防重复计入）

| 条目 | 现状 | 文档 |
|---|---|---|
| `NLSetMaterial` | 已由 `SetMaterialLegacy` 实现（B6.5a），历史分组里曾属“渲染”域，但从未进机器区间 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §2 |
| `NLParameterWorldPosition` | 已由 `WorldPositionLegacy` 实现（B6.6） | 下文 |
| `NLActionMousePickNode` | 已由 `TouchLegacy` 实现（B6.6） | 下文 |
| `LogicGate` | B6.6 改为触发契约（端口变更），不是迁移项 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §0 |
| `StringSplit` / `StringRegexMatch` | C 阶段增强（多分隔符 / Groups 验证），原生节点自身能力 | [B6_STRING_LIST.md](B6_STRING_LIST.md) |

B6.6 的 screen query 节点（`WorldPosition` / `Touch`）**不在**机器区间，只作为 deferred
审计行出现在 `deferred_matrix.json` 的旧快照里；实现后无需从区间删除任何条目：

| Deferred 审计行 | 运行时节点（B6.6） | 端口 |
| --- | --- | --- |
| `NLParameterWorldPosition` | `WorldPositionLegacy` | `OnEnter, ScreenX, ScreenY, Depth / OnDone, WorldPos` |
| `NLActionMousePickNode` | `TouchLegacy` | `OnEnter, ScreenX, ScreenY / OnDone, Target, Objects, Point, Normal` |

- `WorldPositionLegacy` 是纯逆投影（`KX_SceneAdapter::RayCastFromScreenAnyDepth`），无物理射线。
- `TouchLegacy` 先按屏幕像素射线求交，再用 `OverlapQuery` 取命中点邻域对象（`overlap_threshold`
  默认 0.1）；它是**点查询**，与物理 `Collision` 的接触流显式分离，`Collision` 未改。

## 4. StartSound / StopSound 复核链接

完整评估（两条路径、工期、句柄真实表示、截断修复三层）见
[SOUND_BRIDGE_EVAL.md](SOUND_BRIDGE_EVAL.md)，日志 `/tmp/b5_sound.log`。
历史报告 `/tmp/b661_sound_bridge_eval.md` 保留作为首次评估记录。

## 5. 机器可读区间（勿改；审计脚本的唯一输入）

<!-- excluded:start -->
NLActionInstallSubNetwork
NLActionExecuteNetwork
LogicNodeSetLogicTreeProperty
LogicNodeToggleLogicTreeProperty
NLStartLogicNetworkActionNode
NLStopLogicNetworkActionNode
NLConditionLogitNetworkStatusNode
LogicNodeGetLogicTreeProperty
NLGetVRControllerValues
NLGetVRHeadsetValues
LogicNodeFModLoadBank
LogicNodeFModStartEvent
LogicNodeFModGetEventAttribute
LogicNodeFModSetEventAttribute
LogicNodeFModGetEventParameter
LogicNodeFModSetEventParameter
LogicNodeFModModifyEventParameter
NLAddFilter
LogicNodeDraw
NLMakeUniqueLight
NLPlayMaterialSequence
NLRemoveFilter
NLRemoveOverlayCollection
NLActionReplaceMesh
NLActionSetCollectionVisibility
NLSetCurvePoints
NLSetExposureAction
NLSetFilterState
NLSetGammaAction
NLSetGeometryNodeAttribute
NLSetGeometryNodeValue
NLSetLightColorAction
NLSetLightEnergyAction
NLSetLightShadowAction
NLSetMaterialNodeAttribute
NLSetMaterialNodeValue
NLSetNodeTreeNodeAttribute
NLSetNodeTreeNodeValue
NLSetOverlayCollection
NLShowFramerate
NLSetProfile
NLToggleFilter
LogicNodeDistributeCurvePoints
LogicNodeCurveInterpolation
NLGetCurvePoints
LogicNodeGetFont
NLGetGeometryNodeAttribute
NLGetGeometryNodeValue
NLGetNodeTreeNodeAttribute
NLGetNodeGroupNodeValue
NLGetImage
NLGetLightColorAction
NLGetLightEnergy
NLGetMaterialNode
NLGetMaterialNodeAttribute
NLGetMaterialNodeValue
LogicNodeEvaluateCurve
<!-- excluded:end -->

## 6. 口径声明

1. **本清单是「明确不做」，不是「还没做」。** 清单内的 66 条均为已评估、有结论的
   不做项；它们与 `deferred_matrix.json` 里的普通 `deferred` 行不是同一概念。
2. **本清单不吃进任何可用率分母。** 只有机器区间内的 57 条会从
   「用户可感知 233」口径中扣除（`233 = 290 − 57`）；新增的 9 条 FOREVER **不在机器区间**，
   不改变 233 这个分母，也不改变 290 全量口径。
3. 可用率数字的三套分母与算法见 [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md)：
   legacy 全量 **290**、用户可感知 **233**、semantic **360**、已注册节点 **286**。
   **290 与 286 是不同集合，不可相加、不可换算。**
4. 本文件不含“以后可能做”的条目；需要排期的项一律进 `docs/deferred_matrix.json`
   或对应专题文档（B6.9 等），不进这里。
