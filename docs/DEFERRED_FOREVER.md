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
| 移出（新增项） | **1** | #8 Recast 光栅化：P-1 已由 `KX_NavMeshObject::BuildNavMeshRuntime` 实现（源码核实判 `full`，见 §2 #8 后复核注），从新增项移出 |
| 新增 FOREVER | **8**（6 组） | §2；其中 StartSound/StopSound、SetBoneWeight/PlayBoneAction 为成对项；#8 已移出 |
| 合计 | **65** | 57 + 8 |

复核方法（可重跑，直接调用审计脚本自己的解析函数）：

```bash
python3 - <<'EOF'
import sys; sys.path.insert(0, 'scripts/startup')
from bge_gamenodes.generator.native_audit import _forever_ids
ids = sorted(_forever_ids())
print(len(ids), ids)          # 期望 57 条
assert 'NLSetMaterial' not in ids and 'LogicNodeStartSound' not in ids
EOF
```

> 维护提示：解析器取**第一次出现**的区间起点标记，因此本文件正文里不要再写出与标记
> 完全相同的字符串（除 §5 的机器区间本身）——那会让解析起点错位。

**证据缩写**：`matrix 渲染/低/难` = `deferred_matrix.json` 中该行的 `domain/frequency/cost`；
所有基线行同时满足「[native_catalog.py](../scripts/startup/bge_gamenodes/editor/nodes/native_catalog.py) 0 命中」。

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

## 2. 本轮新增 FOREVER（8 条 / 6 组；#8 Recast 已实现并移出）

| # | 节点/能力 | 状态 | 一句话理由 | 证据 | 若真要做的工期 |
|---|---|---|---|---|---|
| 1 | `StartSound`（legacy `LogicNodeStartSound`） | FOREVER | Python 侧只有 `SCA_SoundActuator` 实例、内部 `AUD_Handle` 不暴露，legacy `Sound` 端口是 `nl_type=29` 的 Python socket，桥接需 5 天 | [SOUND_BRIDGE_EVAL.md](SOUND_BRIDGE_EVAL.md) §0/§2；[SCA_SoundActuator.cpp:376-413](../source/gameengine/GameLogic/SCA_SoundActuator.cpp#L376-L413) | 5 天（路径 A 前提不成立；路径 B 成本在 20 入/3 出与 `nl_type=29`） |
| 2 | `StopSound`（legacy `NLActionStopSound`） | FOREVER | 句柄生产者 `StartSound` 未迁移，单迁 StopSound 无合法句柄来源 | 同上 §0；[matrix](deferred_matrix.json) 行 `NLActionStopSound` | 与 1 同批，不单独排期 |
| 3 | AES（`EncryptData`/`DecryptData` 的 `algorithm=aes`） | FOREVER（**查过并否掉**） | **仓库内已有实现**（FFmpeg `libavutil` 的 `av_aes_*`），拒绝理由是**密码学**而非依赖：裸分组密码（仅 ECB/CBC/CTR）缺完整性保护，现有 CRC32 不是认证（可被重算），直接暴露会制造“已加密”的虚假安全感。**成本不在依赖，而在密码学设计**；OpenSSL 方案另需 4~6 天并需 GPLv3 分发评审 | [B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §加密 §1/§2/§3；`lib/ffmpeg/lib/libavutil.a`（`nm` 可见 `av_aes_*` / `av_sha_*` / `av_hmac_*`） | 对价（不做的比较基准）：`av_aes` CBC + `av_hmac` SHA-256 约 **3~5 天**，且仍需解决密钥管理、nonce 复用、侧信道 |
| 4 | 混音总线（`SetAudioBusVolume`） | FOREVER（作为“原生总线”） | audaspace 无命名 bus/mixer 路由（`Mixer` 是格式重采样器、`Superpose` 是二元叠加），现实现只是按名字注册的增益组 | [B6_AUDIO.md](B6_AUDIO.md) §4.5（91-106） | 真总线需改 audaspace 内核，约 8~10 天，收益为零 |
| 5 | Touch 真重叠（`Collision` 的 overlap 对象列表） | FOREVER | `KX_PhysicsAdapter` 没有同步 `contactTest` 入口，逐帧接触流拿不到“重叠对象集合”，属设计变更 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §3；`grep contactTest source/gameengine` 0 命中 | 6~8 天（新增同步接触查询 + 对象列表语义） |
| 6 | `SetBoneWeight` | FOREVER | `KX_GameObject` 无顶点组 API，`source/gameengine` 全量无 `vertex_group/VertexWeight` setter | [B6_ANIMATION.md](B6_ANIMATION.md) §7.1（105-109） | 10~15 天（mesh deform layer 写入 + 蒙皮重算） |
| 7 | `PlayBoneAction` | FOREVER | `BL_ActionManager` 全文无骨骼分支，`BL_ArmatureObject` 无骨骼级动作接口 | [B6_ANIMATION.md](B6_ANIMATION.md) §7.1（110） | 8~12 天（FCurve 路径过滤 + pose 混合） |
| 8 | Recast 光栅化（`BuildNavMesh` 的 `agent_radius`/`agent_height`） | **已实现 → 移出 FOREVER**（2026-11-07 源码复核判 `full`；不计入 §0 的 65） | P-1 运行时入口 `KX_NavMeshObject::BuildNavMeshRuntime`（`KX_NavMeshObject.cpp:1015-1109`）走完整 Recast 链路：`markWalkableTriangles` / `rasterizeTriangles` / 三个 filter / `buildCompactHeightfield` / `erodeWalkableArea` / `buildRegions` / `buildContours` / `buildPolyMesh` / `buildPolyMeshDetail`（`KX_NavMeshBuilder.h:148-210`），再由 `dtCreateNavMeshData`（`:284-305`）打包给 `dtStatNavMesh`；`agent_radius`/`agent_height` 进入 `walkableRadius`/`walkableHeight`（`:113-115`） | [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.4 / §5 `c6_navmesh_bake` | —（已交付） |
| 9 | `KX_RuntimeValue` 的 int64 句柄 | FOREVER（评估后不做） | 新增 Int64 值类型要改值池、`port_contract` 标签与 `enum_consistency`（共享文件），而分配上限 + 边界守卫已让截断不可能发生 | [SOUND_BRIDGE_EVAL.md](SOUND_BRIDGE_EVAL.md) §5；[KX_RuntimeValue.h:100](../source/gameengine/GameLogic/GameNodes/KX_RuntimeValue.h#L100) | 0.5~1 天 |

> **2026-11-06 复核（可行性排查，追加说明，不改 #4 原文与原总数）**：audaspace 确有 `PlaybackManager` / `PlaybackCategory`（`extern/audaspace/include/fx/PlaybackManager.h:65/72/79/87/122`、`PlaybackCategory.h:74-104`），提供命名分类与 per-category 音量；但 Blender 声桥与游戏引擎都未接入（`grep -rn 'PlaybackManager|PlaybackCategory' source/` 0 命中），且播放节点没有 bus 归属端口（`KX_GameAdaptersEngine.cpp:2728` 全部写死 `master`）。因此 #4 的 FOREVER 仅指「原生无限层级总线树/完整 mixer 路由」仍不可做；**per-category 分组音量本身不是 FOREVER**，属可修（需扩展播放节点 Bus 字段，2~3 天），详见 [FEASIBILITY_AUDIT.md](FEASIBILITY_AUDIT.md) §1.3。

> **2026-11-07 源码复核（收口 #8，取代 11-06 的 B.1 / P-1 两条追加说明）**：以源码为准，`#8` 判 **`full`** 并从 FOREVER 移出；运行时入口存在且走完整 Recast 链路，不是「只编译 Detour」。
> - 入口 `KX_NavMeshObject::BuildNavMeshRuntime(agent_radius, agent_height)`（`source/gameengine/Ketsji/KX_NavMeshObject.cpp:1015-1109`）：取 `GetMesh(0)` 的多边形（`:1021`）、按 Y-up 三角化（`:1026-1064`），失败即返回 false。
> - Recast 能力（`KX_NavMeshBuilder::Bake`，`source/gameengine/Ketsji/KX_NavMeshBuilder.h:87-277`）：`recast_calcBounds`(:111)、`recast_calcGridSize`(:125)、`recast_createHeightfield`(:133)、`recast_markWalkableTriangles`(:148)、**`recast_rasterizeTriangles`(:150)**、`recast_filterLowHangingWalkableObstacles`(:153)、`recast_filterLedgeSpans`(:154)、`recast_filterWalkableLowHeightSpans`(:155)、**`recast_buildCompactHeightfield`(:159)**、`recast_erodeWalkableArea`(:170)、`recast_buildDistanceField`(:175)、**`recast_buildRegions`(:176)**、**`recast_buildContours`(:184)**、**`recast_buildPolyMesh`(:196)**、**`recast_buildPolyMeshDetail`(:209)**。
> - Detour 打包：`BuildStatNavMeshData` 调 `dtCreateNavMeshData`（`:284-305`），`dtStatNavMesh::init(data, dataSize, true)` 接管内存（`KX_NavMeshObject.cpp:1094-1107`）。
> - 参数生效：`agent_radius`/`agent_height` 进入 `walkableRadius`/`walkableHeight`（`KX_NavMeshBuilder.h:113-115`），由节点 config 传入（`KX_FunctionNodesB6.cpp:339-340`）；节点端口契约只有 `OnEnter` → `OnSuccess`/`OnFail`（`:758-759`、`B6_SPEC` `:849-850`）。
> - 明确没有（均不在节点契约内，故不构成 FOREVER 理由）：offmesh links、tile cache、crowd / 动态障碍、多网格（只取 `GetMesh(0)`）；`grep -rn 'dtTileCache\|dtNavMeshQuery\|dtCrowd\|dtOffMeshConnection' source/gameengine` **0 命中**。
> - 判定与计数：`full`；§0 的「新增 9 / 合计 66」按 1 条移出 → **新增 8 / 合计 65**（`57 + 8`）。机器 id `FOREVER_IDS=57` 不变（`NUMBER_CONSISTENCY_AUDIT.md` B.5），`233 = 290 − 57` 与 semantic 360 不变。

> **2026-11-06 复核（P-1，已被上条 11-07 源码复核取代）**：原注提出的「已实现 → 各减 1 → 65」结论成立；原「表格合计已冻结」作废，§0 现为新增 8 / 合计 65。

> **2026-11-06 复核（C 块，追加说明，不改 #6/#7 原文）**
> - `SetBoneWeight` 仍属 FOREVER：`KX_GameObject`/`BL_ActionManager`/`BL_ArmatureObject` 均无按骨骼设权入口；Blender 侧 `Mesh::deform_verts_for_write()` 可写，但 GPU 蒙皮静态缓存只 hash dvert 指针（`draw_armature_skinning.cc:984-985`），原地改权重不失效缓存，需同时补权重写入与缓存重算，见 [B6_ANIMATION.md](B6_ANIMATION.md) §9.1。
> - `PlayBoneAction` 仍属 FOREVER：`BL_ActionManager::UpdateArmatureAnimation()` 最终走整对象 `obj->ApplyAction()`（`BL_Action.cpp:581-596`），无骨骼过滤/FCurve 路径掩码，见 [B6_ANIMATION.md](B6_ANIMATION.md) §9.1。

> **2026-11-06 复核（B6.9-A，追加说明，不改 #6/#7 原文）**
> - `SetBoneWeight`（#6）可核化证据：动作入口 `BL_ActionManager::PlayAction(const std::string &name, float start, float end, short layer, short priority, float blendin, short play_mode, float layer_weight, short ipo_flags, float playback_speed, short blend_mode)`（`BL_ActionManager.h:63`）**没有 bone 参数**，只有 `layer`；`BL_ActionManager::SetLayerWeight()` 与 `SetActionSpeed()`（`BL_ActionManager.h:102/107`）同样只到 layer 级；`BL_ArmatureObject` 本轮新增的 `GetPoseChannelTransform()` 是只读，没有写入顶点权重的 setter。GPU 静态蒙皮缓存 key 只含 `mesh_orig->deform_verts().data()` 指针（`source/blender/draw/intern/gpu_modifiers/draw_armature_skinning.cc:984-985`），原地改 `MDeformVert::weight` 不改变该指针，缓存不会失效。**不进 95% 分母**。
> - 新增 FOREVER 候选：`SetActionTimeScale`（独立时间缩放）。`BL_Action` 只有 `m_speed`（`BL_Action.cpp` 的 `SetSpeed()`，同时驱动帧推进），没有与速度正交的时间缩放字段；`BL_ActionManager` 只有 `SetActionSpeed(short layer, float speed)`（`BL_ActionManager.h:102`）。因此节点只做显式失败并回报理由，不伪造时间缩放。**不进 95% 分母；§0 合计见前（机器 id `57`，见 `NUMBER_CONSISTENCY_AUDIT.md` B.5；`65 = 57 + 8` 正文项，#8 已移出）**。

## 3. 已实现、明确不在本清单的条目（移出 1 条，但需防重复计入）

| 条目 | 现状 | 文档 |
|---|---|---|
| `BuildNavMesh`（#8 Recast 光栅化） | 已由 `KX_NavMeshObject::BuildNavMeshRuntime` 实现（P-1）：完整 Recast → Detour 烘焙，`agent_radius`/`agent_height` 真实生效，判 `full` | §2 的 2026-11-07 源码复核；[B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.4 |
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

## 4.1 环境受限：频率无关硬件计数（J 块，非节点能力）

- 目标：判定内联的真实方向（而不只是「不显著变慢」）。
- 做不到：本机 `perf_event_paranoid=4`、`CapEff` 全零、无 valgrind，取不到频率无关的指令/周期计数；纯墙钟在 5% 噪声内无法判方向。
- 保留入口：`tests/a4_hwcount.sh` 保留，环境解锁（PMU 权限或 valgrind 任一可用）后可直接跑。
- 不假装：J.1 判据只用同窗成对墙钟比值（`median(off/native) ≤ 1.05`），不据此宣称「内联显著变快/变慢」。

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

1. **本清单是「明确不做」，不是「还没做」。** 清单内的 65 条均为已评估、有结论的
   不做项；它们与 `deferred_matrix.json` 里的普通 `deferred` 行不是同一概念。
2. **本清单不吃进任何可用率分母。** 只有机器区间内的 57 条会从
   「用户可感知 233」口径中扣除（`233 = 290 − 57`）；新增的 8 条 FOREVER **不在机器区间**，
   不改变 233 这个分母，也不改变 290 全量口径。
3. 可用率数字的三套分母与算法见 [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md)：
   legacy 全量 **290**、用户可感知 **233**、semantic **360**、已注册节点 **286**。
   **290 与 286 是不同集合，不可相加、不可换算。**
4. 本文件不含“以后可能做”的条目；需要排期的项一律进 `docs/deferred_matrix.json`
   或对应专题文档（B6.9 等），不进这里。

## 7. UI 文本能力边界（非 legacy 节点，不进机器区间）

以下不是 `bl_idname`，不参与第 5 节机器区间与任何可用率分母，只记录“明确不做”的
能力边界，避免用近似实现含糊带过：

> 本节物理位置在 §5 机器区间的结束标记之后，而 `native_audit.py` 的
> `_forever_ids()` 只切 §5 的起始标记到结束标记之间的文本；本节也没有任何
> `NL*` / `LogicNode*` 形式的 `bl_idname`。因此机器读到的 FOREVER 集合仍是
> **57** 条，`233 = 290 − 57` 与 semantic **360** 均不变。实测：
> `FOREVER_IDS 57 / EXCLUDED 57 / PERCEIVED_TOTAL 233 / SEMANTIC_TOTAL 360`。
>
> 本节 6 条（展开为 8 项能力：粗体/斜体/下划线、描边、渐变、per-run 字体族、
> 字形覆盖、竖排+富文本）是**能力边界说明**，不是 FOREVER 节点条目，
> 不计入 §0 的合计 **65**；§0 的 65 仍是 57 个机器区间 id + 8 个正文 FOREVER
> 节点项。即使把这 6 条物理移进机器区间，正则也只认 `NL*` / `LogicNode*`
> `bl_idname`，不会新增任何 id。

1. **富文本粗体 / 斜体 / 下划线**：BLF 没有按 run 切换字形的 API；用几何偏移或描边
   冒充属于近似实现，明确不做，标记按普通文本原样输出。
2. **per-run 字体族切换**：富文本目前只做 color + size；跨字体 run 的基线/行高规则
   需要排版引擎，不做。字体切换只在 label 级（G.1）成立。
3. **`{size:N}` 的排版级行高**：只改字形大小，行高仍由元素 `font_size` /
   `line_height` 决定；按 run 重排行高不做。
4. **竖排 + 富文本标记**：竖排按 UTF-8 字符逐行断行，不剥离行内标记；两者的组合
   不支持，需先关闭竖排再使用富文本。
5. **字形覆盖检测**：某字体缺 CJK 字形时交给 BLF 自身回退链，不做覆盖检查与告警。
6. **UI 旋钮的真机外观截图（A.1.2）**：UI canvas 只在 BGE 播放器内绘制，
   headless 无 UI framebuffer；16 段与等半径已由 `b661` 数值断言覆盖，
   视觉外观留 `pending real device`。

真实 display 上的多字体字形渲染与富文本视觉仍未验证，状态保持
`pending real device`，见 [REAL_DEVICE_VERIFICATION.md](REAL_DEVICE_VERIFICATION.md)。

## B6.9 新增 FOREVER 项（2026-11-06）

1. **多监听器 3D 音频（Au-1）**：FOREVER，按**单监听器**交付。
   `extern/audaspace/include/devices/I3DDevice.h` 只有一组
   `get/setListenerLocation` / `get/setListenerVelocity` / `get/setListenerOrientation`，
   没有按索引新增/切换 listener 的 API；设备实现也只保存一份听者状态。
   这属于 audaspace 的模型限制，不是桥接工期问题。
2. **场景共存层级 / 子场景释放（Sc-1）**：FOREVER。
   `KX_KetsjiEngine::LoadScene` 调用 `ReplaceScene`，任一时刻只有一个活动场景；
   `RemoveScene` 在 `KX_KetsjiEngine.cpp:1341-1356` 显式禁用，子场景释放只能返回
   `KX_SceneUnloadResult::Unsupported`。本轮只交付诚实的「单活跃场景栈」
   （`MountChildScene` / `UnmountChildScene`），不伪造共存或假成功。
3. **真流式场景加载（Sc-2）**：FOREVER。`LoadSceneAsync` 的 `Progress` 是分帧
   **反馈**；加载在末尾单帧整场景替换，引擎没有 chunk/stream 入口。
   **进度不是流式**，不得按流式宣传。
4. **RPC 远端点对点寻址（Ms-1）**：**明确不做**（不是工期问题）。`SendRPC` 在连网时
   一律 `BroadcastReliable` 广播，`Target` 只影响本地回环；没有 peer 路由表、没有
   请求-响应关联 id / 超时 / 重传 / 去重。信封 `EBCRPC1` 只有
   `magic + subject_len + payload_len`，没有 sequence，重复包会重复触发。
   需要定向语义时由应用层自己在 payload 里带目标并过滤。
5. **延迟消息跨场景/存档持久化（Ms-2）**：**明确不做**。挂起队列归场景消息适配器
   所有，`EngineMessageAdapter` 析构即丢弃并打印
   `GameNode: message bus: N delayed message(s) dropped on scene unload`；
   不做序列化、不迁移到新场景、不补发，也没有 per-message 取消 API。
6. **流式文件的无锁并发写（Fi-1）**：**明确不做**。`FileWriteChunk` 按调度顺序
   逐片交给适配器，`append=false` 每次触发首片 truncate、其余 append；
   不提供文件锁、跨进程互斥或写入回滚，多节点写同一路径的结果由图作者自行串行化。
7. **目录操作的符号链接逃逸检测（Fi-2）**：**明确不做**。`KX_FileAdapterDirectoryPathIsSafe`
   拦截绝对路径、`..`、`.`、`\`、`:`、控制字符与空组件，根目录不可删；但 blend 目录
   内已存在的符号链接会被引擎后端跟随（`fs::directory_iterator` /
   `create_directories` 的标准行为），本节点层不额外拦截。
8. **真机触摸 / 压力 / 多点手势（I-2）**：**节点侧已在 mock 层交付，真机不可用**。
   `EngineInputAdapter::GetTouch` 固定返回全 0 / `down=false`，`GetTouchCount()` 返回 0
   （`source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp:473-480` / `:482-485`），
   基类 `GetTouchPressure()` 也返回 0——Ketsji/Device 层没有触摸设备接口。
   移动端接入需要新增设备层，不是节点工期问题；`TouchInput` / `TouchGesture` 的真实
   多点、压力与手势行为保持 `pending real device`，本轮 E2E 只证明 mock 语义。
   **2026-11-06 输入域重打分复核**：本项改判为 `unsupported`——引擎层不存在触摸设备接口
   （`source/gameengine/Device/` 无 touch 类，唯一命中 `DEV_JoystickDefines.h:40` 是手柄
   `TOUCHPAD` 按钮常量；`KX_GameAdaptersEngine.cpp:473-485` 硬编码空值），不是「有接口但
   headless 没设备」。因此 `TouchInput` / `TouchGet` / `TouchGesture` 进 FOREVER、不进代码侧
   分母；原「pending real device」表述作废。详见 `docs/INPUT_RESCORE.md` §3.7。

## B6.9-F 收口登记：场景三件 + 音频 bus（2026-11-06，位于 §5 机器区间之外）

本节是**能力边界登记**，不是 legacy 节点条目；不含任何 `bl_idname` 形式的条目名，
因此 `native_audit.py` 的 `_forever_ids()` 仍只读 §5 区间，集合保持 **57** 条，
`233 = 290 − 57` 与 semantic **360** 均不变。§0 的合计也不因本节改变。

### F.1 场景三件（明确不做）

| # | 能力 | 一句话可核理由 |
|---:|---|---|
| 1 | **按场景时标（per-scene time scale）** | `KX_Scene` 没有独立时间缩放字段，只有引擎全局 `m_timescale`（`source/gameengine/Ketsji/KX_KetsjiEngine.cpp:140/477/1464/1469`），要实现得改 6 处 `GetFront()` 单场景假设 + 输入/UI 路由，风险高于收益（+0.42pp）。 |
| 2 | **子场景释放（sub-scene release）** | `RemoveScene` 在 `KX_KetsjiEngine.cpp:1341-1356` 被显式禁用（EEVEE 集成期留下的禁用路径），GPU 资源注销没有钩子，重启用不可控。 |
| 3 | **多活跃场景共存（concurrent active scenes）** | 单 listener 音频、单 active camera、UI 单 canvas 等 6 处单场景假设，改动面覆盖引擎核心，且与 §2 #2 的单活跃场景栈交付口径冲突。 |

### F.2 音频 bus（保持 `approximate`，不接 `PlaybackManager`）

> audaspace 无 bus 概念（`Mixer` 只是格式/重采样混音器，`extern/audaspace/include/respec/Mixer.h:36-37`；
> `SoftwareDevice` 全设备单 `m_mixer`，`extern/audaspace/include/devices/SoftwareDevice.h:230`），
> 当前「命名 gain 组」是等价实现，**不是原生总线**。总线树/父子增益/solo/mute 全部不支持。
>
> （原文写作 `respec::Mixer`，那是头文件目录名；类本身在 `aud` 命名空间下，即 `aud::Mixer`。）

- 档位定案：`SetAudioBusVolume` 保持 **`approximate`**，不是 `full`，也不算 FOREVER 节点项。
- `PlaybackManager` / `PlaybackCategory` 只有命名分类与 per-category 音量，且播放节点没有 bus 归属端口
  （`KX_GameAdaptersEngine.cpp:2783/2819/2942` 全部写死 `master`）；接完仍是 `approximate`，档位不变。
- 因此不为了 **+0.23pp** 改代码。详见 [B6_AUDIO.md](B6_AUDIO.md) §4.6 与 §2 #4 的复核说明。
