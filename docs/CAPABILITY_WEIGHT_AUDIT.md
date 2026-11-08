# 能力域加权核算（D.2 前置材料）

- 日期：2026-11-05（第 11 节为证据补齐记录，第 12 节为验证层级说明；8 项中 7 项按 `mock-verified` 改判，`ActionDone` 记 `full`、证据等级 M）
- 性质：**只读核算 + 只写文档**；未改任何源码、未跑构建、未动别人的文档
- 分母口径：**已注册节点数 286**（14 个内建 + 272 个表驱动），**不是 legacy 290**
- **K 冻结口径（2026-11-06，`RUN_ID=20261106-224926`）**：运行时实测 `KX_NodeRegistry::TypeCount() = 325`；全量三口径 **严格 98.68%（299/303）、加权 99.34%（301/303）、覆盖 97.85%（318/325）**（`F=299 / A=4 / U=7 / P=15`、`D=303`，闭合 `299+4+7+15=325`）。本文件 §0/§4 的百分比分母仍是旧审计的 286，**只作 14 域权重快照，不得当 325 口径引用**；对外三个数一律用冻结值（见 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B.5）。
- 原始日志：`/tmp/capability_audit.log`；证据补齐日志：`/tmp/audio_evidence.log`
- **数值口径（2026-11-06 勘误）**：**325 注册表**（`TypeCount()` 实测），本文件不引用上一轮的增量账分母。本次修正 `ActionDone` 与传感器 `approximate` 名单；字符串两项正则已于同日双修后升 `full`，但 §4 的「字符串列表 34 / 29 / 5」与 §4.1 的两条字符串记录仍是 B6.9 前的 286 快照，**不得引用**——字符串域现行口径为 37/37 全 `full`（`B6_STRING_LIST.md` §3、`FLOW_MATH_STRING_RESCORE.md` §4.2）。§5.1/§5.2 与 §13 中引用旧值（88.1% / 93.4% / 252 / 8 项）的段落未同步重算，一律以 §0 与 §4 的新值为准。

## 0. 摘要：当前全域三口径（Mock 证据不计严格）

按「已注册节点数」为分母、`unclassified` 不计分子的保守算法。
新增 `mock-verified` 档后，**主口径**（Mock 证据不算严格）与**上界**（假设其余 7 项 Mock 真机全通过）分开列：

| 口径 | 主口径算式 | 主口径（D.2 用这组） | 上界（其余 7 项真机确认后） |
|---|---|---|---:|
| **严格可用率** | `full / 286` | **88.5%**（253 / 286） | 90.9%（260 / 286） |
| **加权可用率** | `(full + 0.5*mock + 0.5*approx) / 286` | **93.5%**（267.5 / 286） | 94.8%（271 / 286） |
| **覆盖率** | `(full + mock + approx) / 286` | **98.6%**（282 / 286） | 98.6%（282 / 286） |

若换成「14 个能力域等权平均」的口径：主口径严格 **81.5%** / 加权 **89.3%** / 覆盖 **97.1%**；
上界 **86.3% / 91.7% / 97.1%**（域等权未随 `ActionDone` 勘误重算）。两种口径都可比、都可复核，但**不能混用**（见第 7 节）。

> 关键澄清：`ActionDone` 已按实现口径计入 `full`（证据等级 **M**，证据覆盖率另列，不扣实现正确率）；
> 其余 7 项 Mock 证据**不提升严格口径**，只把加权从 92.3% 抬到 93.5%、
> 覆盖从 96.2% 抬到 98.6%。D.2 的主数字必须用主口径那一列。
>
> **2026-11-05 更新（见 §13）**：本节 88.5% / 93.5% / 98.6% 是「实现成立」口径，
> 只用于**内部进度跟踪**；A/B 类实测后，**对外主口径用 §13 的 63.3%**
> （5 条原「未覆盖」已全部补齐、20 条断言全 PASS，实算即 63.3%）；
> 58.4% / 56.6% 仅作含重复扣减的保守下界。88.5% 作为口径保留，不删除。

## 1. 分母取证：286 是怎么来的

| 项 | 数量 | 来源 |
|---|---|---|
| 内建节点 | 14 | `KX_RuntimeNodes.cpp` 201、207-229 |
| 表驱动节点（数组内） | 270 | 18 个 `k*Specs[]`（见下表） |
| 表驱动特例 | 2 | `Formula`、`StringFormat`，`KX_FunctionNodesB2.cpp` 1619-1639（`ports_from_ir`，不占数组） |
| **运行时注册合计** | **286** | 14 + 270 + 2 |
| 重复 `type_id` | 0 | 脚本对 270 条做 flat/unique 比对，均等 |

18 个 spec 数组的实际位置（计数均由脚本逐条匹配宏调用得到）：

| 文件 | 数组 | 行 | 条数 |
|---|---|---|---|
| `KX_FunctionNode.cpp` | `kSpecs[]` | 1294 | 67 |
| `KX_FunctionNodesB2.cpp` | `kB2Specs[]` | 1519 | 30 |
| `KX_FunctionNodesB3.cpp` | `kB3Specs[]` | 716 | 21 |
| `KX_FunctionNodesB4.cpp` | `kB4Specs[]` | 714 | 9 |
| `KX_FunctionNodesB4File.cpp` | `kB4FileSpecs[]` | 873 | 8 |
| `KX_FunctionNodesB5.cpp` | `kB5Specs[]` | 863 | 5 |
| `KX_FunctionNodesB6.cpp` | `kB6Specs[]` | 603 | 22 |
| `KX_FunctionNodesB6Audio.cpp` | `kB6AudioSpecs[]` | 170 | 5 |
| `KX_FunctionNodesB6File.cpp` | `kB6FileSpecs[]` | 397 | 5 |
| `KX_FunctionNodesB6Input.cpp` | `kB6InputSpecs[]` | 304 | 8 |
| `KX_FunctionNodesB6Legacy.cpp` | `kB6LegacySpecs[]` | 650 | 14 |
| `KX_FunctionNodesB6LegacyQuery.cpp` | `kB6LegacyQuerySpecs[]` | 182 | 2 |
| `KX_FunctionNodesB6Net.cpp` | `kB6NetSpecs[]` | 462 | 11 |
| `KX_FunctionNodesB6Scene.cpp` | `kB6SceneSpecs[]` | 433 | 5 |
| `KX_FunctionNodesB6Sensor.cpp` | `kB6SensorSpecs[]` | 436 | 5 |
| `KX_FunctionNodesB6String.cpp` | `kB6StringSpecs[]` | 884 | 14 |
| `KX_FunctionNodesB6UI.cpp` | `kB6UISpecs[]` | 953 | 6 |
| `KX_FunctionNodesExtra.cpp` | `kExtraSpecs[]` | 729 | 33 |

编辑器侧对照（只用于说明「可见 ≠ 已注册」，不作为分母）：

- `native_b2/b3/b4/b4b/b5/b6/b6b/b6c/b6file/b6ui/batch` 共 224 条，唯一 223 条
  （`Collision` 在 `native_batch` 与 `native_b6c` 重复一次）；
- `port_contract.py` 输出 `native_nodes=223 catalog_nodes=68 errors=0 warnings=4`（B6.5a 后快照；K 冻结 `RUN_ID=20261106-224926` 后为 `native_nodes=244`）；
- `native_catalog.py` 68 条映射里有 3 条指向内建节点（`OnInit`/`OnUpdate`/`Delay`）；
- 因此编辑器可见唯一节点 = **226**。

> **口径警告**：286（运行时已注册）、226（编辑器可见）、223（`port_contract` 的 native 表）、290（legacy 全量）是四个不同的东西。本文所有百分比的分母都是 **286**，除非明确标注「域等权」或「legacy」。

**分档口径定义**（四档：`full` / `mock-verified` / `approximate` / `unsupported`，
外加未取证时的 `unclassified`）见第 3 节；三口径对 `mock-verified` 的计分规则是：
严格计 0、加权计 0.5、覆盖计 1。`mock-verified` 单列一档的原因是
「行为链在 Mock 层被断言」与「真机/真实引擎路径已执行」是两件事，不能混入 `full`。

## 2. 各域节点清单（286 个全量）

> 说明：`SetGravity` 由 B6.1 复用 core 节点，本审计把它记入「物理（基础）」，避免跨域重复计数；
> `B6_PHYSICS_ADVANCED.md` 的 15 节点批次口径 = 本表物理高级 14 + 该 `SetGravity`，两者分档结论一致。

### 2.1 值计算（42）
core 11：`Compare`、`LogicGate`、`LogicNot`、`Clamp`、`MapRange`、`Interpolate`、`RandomFloat`、`Threshold`、`ColorConst`、`ColorFromVector`、`VectorFromColor`
builtin 4：`ConstFloat`、`ConstBool`、`FloatMath`、`VectorMath`
B2 16：`RandomValue`、`ClampValue`、`Wrap`、`Round`、`Sign`、`VectorReflect`、`VectorProject`、`VectorRotateToward`、`VectorAngle`、`VectorDistance`、`ColorMix`、`ColorLerp`、`ColorToRGBA`、`RGBAToColor`、`HSVToRGB`、`RGBToHSV`
Extra 10：`Vector3`、`VectorSplit3`、`ValueInvert`、`LimitRange`、`MapRangeNode`、`InterpolateValue`、`ThresholdNode`、`ValueSwitch`、`TimeFactor`、`WithinRangeNode`
special 1：`Formula`

### 2.2 动画（11）
B3 8：`PlayAction`、`PlayActionByName`、`StopAction`、`SetActionFrame`、`SetActionSpeed`、`SetLayerWeight`、`GetActionFrame`、`ActionDone`
B5 3：`RotateTo`、`MoveTo`、`AlignAxisToVector`

### 2.3 音频（12）
B3 7：`StartSound`、`StopSound`、`SetSoundVolume`、`SetSoundPitch`、`SoundFinished`、`PlaySoundByName`、`Set3DListener`
B6 5：`SetAudioAttenuation`、`SetAudioDoppler`、`AudioZone`、`SetAudioBusVolume`、`PlaySound3D`

### 2.4 输入（23）
core 9：`Keyboard`、`MouseButton`、`MousePosition`、`MouseDelta`、`MouseWheel`、`JoystickAxis`、`JoystickButton`、`SetMousePosition`、`SetCursorVisible`
B6 8：`GamepadButton`、`GamepadAxis`、`GamepadTrigger`、`TouchInput`、`KeyCombo`、`InputMap`、`GetInputValue`、`VibrateGamepad`
Extra 5：`MouseData`、`KeyPressed`、`MousePressed`、`MouseMoved`、`SetMousePositionLegacy`
B5 1：`MouseLook`

### 2.5 字符串列表（34）
core 5：`StringConst`、`StringConcat`、`StringLength`、`StringReplace`、`ListLength`
B2 14：`StringCompare`、`StringFind`、`StringToUpper`、`StringToLower`、`StringSubstr`、`StringToNumber`、`NumberToString`、`ListAdd`、`ListRemove`、`ListSet`、`ListGet`、`ListContains`、`ListClear`、`ForEach`
B6 14：`StringSplit`、`StringJoin`、`StringRegexMatch`、`StringRegexReplace`、`StringTrim`、`StringPad`、`StringLengthChars`、`StringPadChars`、`ListSort`、`ListFilter`、`ListMap`、`ListFind`、`ListSlice`、`ListConcat`
special 1：`StringFormat`

### 2.6 网络（14）
`HostRoom`、`JoinRoom`、`LeaveRoom`、`OnNetworkEvent`、`SendNetworkMessage`、`BroadcastMessage`、`SyncProperty`、`GetRemoteProperty`、`ListRooms`、`RefreshRooms`、`Reconnect`、`DiscoverRooms`（N-1 广播发现）、`SyncPlayerState`、`GetPlayerState`（N-2 20 Hz 位姿）

### 2.7 传感器（8）
B6 5：`RadarExtended`、`LineOfSight`、`AreaSensor`、`RaycastSensor`、`ProximitySensor`
core 1：`Collision`
B3 2：`Touch`、`Radar`

### 2.8 场景（9）
core 1：`LoadScene`
B6 5：`LoadSceneAsync`、`UnloadScene`、`TransitionScene`、`GetActiveScenes`、`SetSceneTimeScale`
B3 3：`SuspendScene`、`ResumeScene`、`AddObject`

### 2.9 UI（15）
B4 9：`CreateUICanvas`、`CreateUILayout`、`CreateUILabel`、`CreateUIImage`、`CreateUIButton`、`CreateUISliderWidget`、`MoveWidget`、`SetUIWidgetAttr`、`GetUIWidgetAttr`
B6 6：`CreateScrollView`、`CreateListView`、`CreateTabView`、`UIAnimation`、`UIScreenAnchor`、`UIScreenStack`

### 2.10 文件（13）
B4File 8：`SaveGame`、`LoadGame`、`ReadFile`、`WriteFile`、`FileExists`、`DeleteFile`、`JSONStringify`、`JSONParse`
B6 5：`SaveGameBinary`、`SaveGameSlots`、`EncryptData`、`DecryptData`、`FileReadLines`

### 2.11 物理（基础）（16）
core 10：`SetLinearVelocity`、`ApplyImpulse`、`ApplyForce`、`ApplyTorque`、`SetDynamics`、`SetRigidBody`、`SetCollisionMask`、`SetGravity`、`GetGravity`、`GetVelocity`
builtin 1：`Raycast`
Extra 5：`CharacterJump`、`CharacterGravity`、`CharacterVelocity`、`CharacterWalkDir`、`CharacterJumpSpeed`

### 2.12 物理高级（14）
`CreateVehicle`、`VehicleControl`、`AddConstraint`、`RemoveConstraint`、`AddJoint`、`SetPhysicsMass`、`SetPhysicsFriction`、`SetPhysicsRestitution`、`SetLinearDamping`、`SetAngularDamping`、`BuildNavMesh`、`QueryPath`、`RaycastBatch`、`SetCollisionGroup`

### 2.13 动画高级（8）
`PlayActionByLayer`、`BlendActions`、`SetBoneTransform`、`SetBoneWeight`、`PlayBoneAction`、`SetAnimationSpeed`、`AddAnimationEvent`、`GetAnimationFrame`

### 2.14 其他（70）
core 30：`SetResolution`、`SetFullScreen`、`Print`、`QuitGame`、`RestartGame`、`SetTimeScale`、`SetActiveCamera`、`RemoveObject`、`SetVisibility`、`SetParent`、`ClearParent`、`SendMessage`、`SetFontText`、`SetPosition`、`SetRotation`、`SetScale`、`Translate`、`Timer`、`Once`、`Sequence`、`FrameTime`、`WorldPosition`、`WorldRotation`、`WorldScale`、`GetParent`、`ObjectByName`、`GetOwner`、`IsNone`、`Distance`、`WithinRange`
builtin 9：`OnInit`、`OnUpdate`、`Sink`、`Branch`、`Delay`、`AssetGet`、`AssetSet`、`ObjectGet`、`ObjectSet`
Extra 13：`GetTimeScale`、`GetResolution`、`GetFullscreen`、`GetVsync`、`SetCameraFov`、`SetCameraOrthoScale`、`SetVSync`、`NextFrame`、`GetPropertyNode`、`SetPropertyLegacy`、`ValueChanged`、`SetVisibilityLegacy`、`SetParentLegacy`
B5 1：`SetMaterial`
B3 1：`MessageReceived`
legacy 16：`TimerLegacy`、`QuitGameLegacy`、`ClearParentLegacy`、`MessageReceivedLegacy`、`ObjectByNameLegacy`、`GetObjectLegacy`、`TimeDataLegacy`、`SimpleValueLegacy`、`SetMaterialLegacy`、`PulsifyLegacy`、`BarrierLegacy`、`EvaluatePropertyLegacy`、`ModifyPropertyLegacy`、`AlignAxisToVectorLegacy`、`WorldPositionLegacy`、`TouchLegacy`

合计：42+11+12+23+34+11+8+9+15+13+16+14+8+70 = **286**。

## 3. 分档规则与证据等级

| 分档 | 判据 | 严格 A | 加权 B | 覆盖 C |
|---|---|---|---:|---:|---:|
| `full` | 有真实引擎路径/真机 E2E 证据，或实现为纯进程内确定性逻辑、无引擎能力缺口 | 1 | 1 | 1 |
| **`mock-verified`**（新增） | **行为链在 Mock 适配器层已被断言（调用 → 状态迁移 → 回调），但真实引擎/真机路径未执行** | 0 | 0.5 | 1 |
| `approximate` | 能用，但文档写明了语义近似（固定常量、AABB/球近似、设备级共享、子集语法等） | 0 | 0.5 | 1 |
| `unsupported` | 文档写明引擎能力缺失，节点明确返回失败并写 stderr | 0 | 0 | 0 |
| `unclassified` | 只有注册与实现，缺逐项行为证据；**不猜**，三口径一律计 0 | 0 | 0 | 0 |

`mock-verified` 的硬规则：**Mock 通过 ≠ 真机通过**。凡是证据只有 Mock 适配器、
没有真实引擎适配器或真机观察的节点，一律不得判 `full`；
真机部分必须逐项写 `pending real device`。

证据等级（正文里不改变分档，只影响可信度）：

- **A 级**：真实引擎路径或真机 E2E，或 B6 各域文档有逐项限制/分档表；
- **M 级（Mock）**：Mock 适配器层 E2E，只能证明节点语义与参数读取，不能证明真实引擎行为；
- **B 级**：核心/查找表/Extra 节点为纯进程内实现，按代码审阅计 `full`，无逐项 E2E；
- 本次没有对 286 个节点逐项重跑真机 E2E，这一点在 D.2 必须如实披露（见第 12 节）。

## 4. 逐域分档结果

| 域 | n | full | mock | approx | unsup | unclass | A 严格 | B 加权 | C 覆盖 | 主要依据 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 值计算 | 42 | 42 | 0 | 0 | 0 | 0 | 100.0% | 100.0% | 100.0% | 纯进程内；`KX_FunctionNodesB2.cpp` 1519、`KX_FunctionNode.cpp` 1294、`KX_FunctionNodesExtra.cpp` 729 |
| 动画 | 11 | 11 | **0** | 0 | 0 | 0 | 100.0% | 100.0% | 100.0% | B3 716、B5 863；`ActionDone` 记 full（证据等级 M，第 11 节） |
| 音频 | 12 | 1 | **7** | 4 | 0 | 0 | 8.3% | 54.2% | 100.0% | `docs/B6_AUDIO.md` §7 + §9；B3 音频 7 项为 Mock 证据（第 11 节） |
| 输入 | 23 | 22 | 0 | 0 | 1 | 0 | 95.7% | 95.7% | 95.7% | `docs/B6_INPUT.md` §7（引擎无触摸） |
| 字符串列表 | 34 | 29 | 0 | 5 | 0 | 0 | 85.3% | 92.6% | 100.0% | `docs/B6_STRING_LIST.md` §7 |
| 网络 | 11 | 11 | 0 | 0 | 0 | 0 | 100.0% | 100.0% | 100.0% | `docs/B6_NETWORK.md` §9（LAN 为环境边界，不是端口降级） |
| 传感器 | 8 | 6 | 0 | 2 | 0 | 0 | 75.0% | 87.5% | 100.0% | `docs/B6_SENSOR.md` §7（AABB）；`approximate` = `Radar` + `Collision`；「B6 传感器批 6/6」≠ 本域 100% |
| 场景 | 9 | 8 | 0 | 0 | 1 | 0 | 88.9% | 88.9% | 88.9% | `docs/B6_SCENE_ADVANCED.md` §4（RemoveScene 禁用） |
| UI | 15 | 13 | 0 | 2 | 0 | 0 | 86.7% | 93.3% | 100.0% | `docs/B6_UI_ADVANCED.md` 限制段 |
| 文件 | 13 | 11 | 0 | 2 | 0 | 0 | 84.6% | 92.3% | 100.0% | `docs/B6_FILE_ADVANCED.md` 限制段（AES 未接入） |
| 物理（基础） | 16 | 16 | 0 | 0 | 0 | 0 | 100.0% | 100.0% | 100.0% | core 1294、builtin Raycast、Extra 729 |
| 物理高级 | 14 | 11 | 0 | 3 | 0 | 0 | 78.6% | 89.3% | 100.0% | `docs/B6_PHYSICS_ADVANCED.md` §6（15 节点 12/3/0，去掉复用的 SetGravity） |
| 动画高级 | 8 | 4 | 0 | 2 | 2 | 0 | 50.0% | 62.5% | 75.0% | `docs/B6_ANIMATION.md` §6 |
| 其他 | 70 | 68 | 0 | 2 | 0 | 0 | 97.1% | 98.6% | 100.0% | `docs/B6_LEGACY_COMPAT.md` §7-§8；core/builtin/Extra 代码审阅 |
| **合计（主口径）** | **286** | **253** | **7** | **22** | **4** | **0** | **88.5%** | **93.5%** | **98.6%** | — |
| 合计（上界：其余 7 项真机确认） | 286 | 260 | 0 | 22 | 4 | 0 | 90.9% | 94.8% | 98.6% | — |

### 4.1 `approximate` 的 22 项（逐项，286 快照；字符串两项已升 `full`）

| 域 | 节点 | 近似方式 | 证据 |
|---|---|---|---|
| 音频 | SetAudioAttenuation | 对象级只隔离 ref/max/attenuation，model 仍是设备级 | `B6_AUDIO.md` §7.2 |
| 音频 | SetAudioDoppler | audaspace 是设备级单值；对象非全句柄时只记录不应用 | §7.3 |
| 音频 | AudioZone | bus/zone 是等价实现，非 audaspace 原生概念 | §7.1 |
| 音频 | SetAudioBusVolume | 同上，等价实现 | §7.1 |
| 字符串 | ~~StringRegexMatch / StringRegexReplace~~（**已升 `full`，本行勿引用**） | 286 快照时记：`std::regex` 无超时；ECMAScript 子集，无命名组。现行：`Rejected` 端口 + 编前守卫拒绝嵌套量词/空匹配组体，余长度上限与交替回溯（#30） | `FLOW_MATH_STRING_RESCORE.md` §4.3 |
| 字符串 | ListSort / ListFilter / ListMap | 表达式子集；超 `kMaxListElements` 静默截断 | §7.2、§7.4 |
| 传感器 | Radar | 只按对象**世界位置距离**判半径，不判形状/朝向（`KX_GameAdaptersEngine.cpp:686-690`）；`B6_SENSOR.md` §3 已写明是扇形/球形 `QueryProximity` | `B6_SENSOR.md` §3、`KX_FunctionNodesB3.cpp:660-690` |
| 传感器 | Collision | 接触点/法线取 AABB 重叠盒近似（`KX_FunctionNode.cpp:929-957` `BoundsOverlap`） | `B6_SENSOR.md` §7 |
| UI | CreateListView | `OnItemClick` 只在点击帧有效 | `B6_UI_ADVANCED.md` 限制 |
| UI | UIScreenStack | 进程级共享栈，多图共用、热重载不迁移 | 同上 |
| 文件 | EncryptData / DecryptData | 仅 XOR；`algorithm=aes` 明确失败 | `B6_FILE_ADVANCED.md` 限制 |
| 物理高级 | CreateVehicle | 悬挂长度固定 0.2、down/axle 固定；`wheel_names` 只解决轮组选择 | `B6_PHYSICS_ADVANCED.md` §6 |
| 物理高级 | AddConstraint | `slider` 用 `PHY_GENERIC_6DOF` 锁其余轴；枢轴固定取中点 | 同上 |
| 物理高级 | BuildNavMesh | **已消解（2026-11-07 源码复核）**：运行时 Recast 烘焙，`agent_radius/height` 真实生效（`KX_NavMeshObject.cpp:1015-1109` / `KX_NavMeshBuilder.h:87-277`） | 同上 §3.4 |
| 动画高级 | SetBoneTransform | `ApplyPose` 受 `m_lastapplyframe != m_lastframe` 守卫，当帧不刷新 | `B6_ANIMATION.md` §6 |
| 动画高级 | AddAnimationEvent | 节点侧逐帧轮询，非引擎事件；固定 layer 0；单次 | 同上 |
| 其他 | EvaluatePropertyLegacy | `mode=1`（内部属性）运行时直接返回，只支持 `mode=0` | `B6_LEGACY_COMPAT.md` §8 |
| 其他 | TouchLegacy | 重叠集合是球近似，不是真实碰撞体/网格 | 同上 §8 |

> 勘误（2026-11-06，正则双修）：`StringRegexMatch` / `StringRegexReplace` 已升 `full`——`Rejected(Bool)` 端口区分拒绝与不匹配，`IsSafeRegexPattern` 在编译前拒绝嵌套量词与可匹配空串组体；剩余限制（长度上限、交替回溯无超时）登记 `KNOWN_LIMITATIONS.md` #30。故本表与 §4「字符串列表 34 / 29 / 5」的字符串行是 B6.9 前的 286 快照，现行字符串域为 **37/37 全 `full`**（`B6_STRING_LIST.md` §3、`FLOW_MATH_STRING_RESCORE.md` §4.2），**不得引用本表字符串行**。
>
> 勘误（2026-11-06）：`AreaSensor` 已按真实形状重叠升 `full`（`AUDIO_SENSOR_SCENE_RESCORE.md:273-279`），故本表传感器 `approximate` = `Radar` + `Collision`。`AUDIO_SENSOR_SCENE_RESCORE` 的「6/6」只指 **B6 传感器批**（`RadarExtended`/`LineOfSight`/`AreaSensor`/`RaycastSensor`/`RaycastAll`/`ProximitySensor`），不含 core `Collision` 与 B3 `Touch`/`Radar`；该批 6/6 不得写成「传感器域 100%」。

### 4.2 `unsupported` 的 4 项

| 域 | 节点 | 缺失能力 / 行为 | 证据 |
|---|---|---|---|
| 输入 | TouchInput | Ketsji 无触摸设备，`GetTouch/GetTouchCount` 恒空 | `B6_INPUT.md` §7.1 |
| 场景 | UnloadScene | 引擎 `RemoveScene` 被显式禁用，节点恒走 `OnFail`（诚实失败） | `B6_SCENE_ADVANCED.md` §4 |
| 动画高级 | SetBoneWeight | 无运行时顶点组权重 setter | `B6_ANIMATION.md` §3 |
| 动画高级 | PlayBoneAction | `BL_ActionManager` 无 per-bone 动作接口 | 同上 |

### 4.3 原 `unclassified` 8 项：7 项改判 `mock-verified`，`ActionDone` 记 `full`（证据等级 M）

| 域 | 节点 | Mock 层已验什么 | 真机待验什么 | 分档 |
|---|---|---|---|---|
| 动画 | ActionDone | 未完成帧不触发；完成后触发 1 次；第 3 帧不重复 | 真实 `BL_ActionManager` 完成回调的时序 | **full**（证据等级 **M**；证据覆盖率另列，不扣实现正确率） |
| 音频 | StartSound | `PlaySound` 一次；file/loop/volume/pitch 到达；句柄=1；`OnStarted` 一次 | 真实声卡输出、Loop 实际循环、Volume/Pitch 听感 | **mock-verified** |
| 音频 | StopSound | `StopSound` 一次；playing→false；`OnDone` 一次 | 真实声源停止、声道释放 | **mock-verified** |
| 音频 | SetSoundVolume | `SetVolume` 一次；读回 0.6 | 真实音量听感变化 | **mock-verified** |
| 音频 | SetSoundPitch | `SetPitch` 一次；读回 1.5 | 真实音高听感变化 | **mock-verified** |
| 音频 | SoundFinished | 未完成不触发；完成后触发 1 次且不重复 | 真实播放结束事件是否在同一帧可观测 | **mock-verified** |
| 音频 | PlaySoundByName | 名字 `explosion` 转发；句柄=1；`Found=true`；`OnDone` 一次 | name→asset 真实解析是否命中 | **mock-verified** |
| 音频 | Set3DListener | 位置 (1,2,3)、朝向 (0,1,0) 到达 | audaspace 下真实 3D 衰减/方位变化 | **mock-verified** |

证据载体：`tests/b3_audio_e2e.cpp`（31 条断言，0 失败）、`/tmp/b3_audio_e2e_run.log`、
`/tmp/audio_evidence.log`；域文档回填在 `docs/B6_AUDIO.md` §9 与 `docs/B6_ANIMATION.md` §8。

**边界**：以上是 **Mock 适配器层证据**，不是真实听感/真实资源解析。按第 3 节的硬规则，
Mock 通过不能判 `full`，因此这 8 项统一判 `mock-verified`：严格口径计 0、加权计 0.5、覆盖计 1。
真机部分全部标 `pending real device`，不得用 Mock 结果冒充真机通过。

## 5. 三种口径的算式（全域 + 域内）

### 5.1 节点加权（分母 286）

主口径（Mock 证据不算严格，`mock-verified` 只在加权/覆盖里计分）：

```
A 严格   = full / 286
         = 252 / 286
         = 0.8811  → 88.1%

B 加权   = (full + 0.5*mock + 0.5*approximate) / 286
         = (252 + 4 + 11) / 286
         = 267 / 286
         = 0.9336  → 93.4%

C 覆盖   = (full + mock + approximate) / 286
         = (252 + 8 + 22) / 286
         = 282 / 286
         = 0.9860  → 98.6%
```

上界（假设 8 项 Mock 证据全部被真机确认，`mock-verified` → `full`）：

```
A = 260/286 = 90.9%   B = 271/286 = 94.8%   C = 282/286 = 98.6%
```

注意：这 8 项无论判 `mock-verified` 还是 `full`，**覆盖口径都是 98.6%**
（两种档位在覆盖口径里都计 1）；差异只体现在严格（88.1% vs 90.9%）和加权（93.4% vs 94.8%）。

### 5.2 域等权（14 域各 1/14）

把第 4 节每一列的百分比直接算术平均：

```
A 域等权 = (100.0 + 90.9 + 8.3 + 95.7 + 85.3 + 100.0 + 75.0
           + 88.9 + 86.7 + 84.6 + 100.0 + 78.6 + 50.0 + 97.1) / 14
         = 1141.1 / 14 = 81.5%

B 域等权 = (100.0 + 95.5 + 54.2 + 95.7 + 92.6 + 100.0 + 87.5
           + 88.9 + 93.3 + 92.3 + 100.0 + 89.3 + 62.5 + 98.6) / 14
         = 1250.3 / 14 = 89.3%

C 域等权 = (100.0 + 100.0 + 100.0 + 95.7 + 100.0 + 100.0 + 100.0
           + 88.9 + 100.0 + 100.0 + 100.0 + 100.0 + 75.0 + 100.0) / 14
         = 1359.6 / 14 = 97.1%
```

上界（8 项真机确认）：**86.3% / 91.7% / 97.1%**。

注意主口径的域等权严格（81.5%）与「8 项未取证时」的历史下界数值相同——
这不是巧合：`mock-verified` 在严格口径里计 0，所以 Mock 证据对严格率没有任何提升。

### 5.3 与 B6.1「23 节点」批次口径的关系

`docs/B6_ANIMATION.md` 第 6 节合计表写的是 B6.1 批次口径：

```
物理 15（12 full / 3 approx / 0 unsup）+ 动画 8（4 / 2 / 2）= 23 节点
A = 16/23 = 69.6%
B = 18.5/23 = 80.4%
C = 21/23 = 91.3%
```

这是**批次验收口径**，不是全域能力口径。两者只在「同为三公式」这一点上可比，
**分母与节点集合完全不同**，不能拿 80.4% 当全域加权，也不能把 93.4% 当 B6.1 成绩。

## 6. 最拖后腿的域与提升空间

按严格口径排序（域等权时每域权重相同，所以低分域影响更大）：

| 排名 | 域 | 严格（主口径） | 拖后腿原因 | 修好后的严格值 |
|---|---|---:|---|---:|
| 1 | 动画高级 | 50.0% | 2 项引擎能力缺失（骨骼权重、per-bone 动作）+ 2 项近似 | 修 2 项 unsup → 75.0%；再修 2 项 approx → 100% |
| 2 | 音频 | 8.3% | 7 项只有 Mock 证据（严格计 0）+ 4 项设备级/等价实现近似 | 7 项真机确认 → 66.7%；近似也修 → 100% |
| 3 | 传感器 | 75.0% | `AreaSensor`/`Collision` 都是 AABB 近似 | 100% |
| 4 | 物理高级 | 78.6% | `CreateVehicle` 悬挂参数、`AddConstraint` slider、`BuildNavMesh` agent 尺寸 | 100% |
| 5 | 文件 | 84.6% | `EncryptData`/`DecryptData` 只有 XOR，AES 未接入 | 100% |
| 6 | 字符串列表 | 85.3% | 正则无超时、列表表达式子集、静默截断 | 100% |
| 7 | UI | 86.7% | 列表点击仅一帧、屏幕栈进程级共享 | 100% |

按**节点加权**计算，当前扣分总量分三类：

- 4 项 `unsupported` → 严格口径最多 +4 节点 = **+1.4pp**；
- 22 项 `approximate` → 加权口径最多 +11 节点 = **+3.8pp**（严格 +7.7pp 若全部做实）；
- 8 项 `mock-verified` → **严格口径 +8 节点 = +2.8pp、加权 +4 节点 = +1.4pp**，
  前提是全部通过真机；这是「证据层级缺口」，不是能力缺口。

修复场景（节点加权，主口径为起点）：

| 场景 | 严格 | 加权 | 覆盖 | 域等权严格 |
|---|---:|---:|---:|---:|
| 现状（8 项为 `mock-verified`） | 88.1% | 93.4% | 98.6% | 81.5% |
| S1 = 8 项全部真机确认（上界） | 90.9% | 94.8% | 98.6% | 86.3% |
| S2 = S1 + 修 4 项 unsupported | 92.3% | 96.2% | 100% | 89.2%* |
| S3 = 全部修完 | 100% | 100% | 100% | 100% |

\* S2 的域等权严格约 89.2%（S1 的 86.3% + 动画高级 1.8pp + 场景 0.7pp + 输入 0.4pp）。

结论：**全域瓶颈是「证据层级缺口 + 能力缺口」双轨**——
（1）8 项 Mock 证据不能换严格分，严格口径仍是 88.1%；真机验证是最便宜的 +2.8pp；
（2）动画高级 2 项引擎能力缺失（难度最高、收益最小）；
（3）22 项 `approximate` 修一项只涨 0.35pp（严格）或 0.17pp（加权）。
把 Mock 当 full 会把严格率虚抬到 90.9%，D.2 不能采用那个数。

## 7. 给 D.2 的「能力域加权」建议值

D.2 草案写的是「能力域加权 82–84%（不美化）」。8 项按 `mock-verified` 改判后，
D.2 主数字必须用**主口径**（Mock 不算严格），上界只能放括号里。

### 解释 A（14 域等权 + 严格）

- 主口径（现状，8 项 `mock-verified` 计 0）：**81.5%**；
- 上界（8 项全部真机确认）：**86.3%**。

因此「82–84%」正好落在主口径与上界之间，可以作为**区间写法**保留：
「14 域等权、严格口径；主口径 81.5%，上界 86.3%」。
但必须写清楚 82–84% 不是实测点，只是两端之间的插值印象，D.2 若只给一个数应用 **81.5%**。

### 解释 B（全域节点加权，分母 286）

- 主口径：严格 **88.1%**、加权 **93.4%**、覆盖 **98.6%**；
- 上界（8 项真机确认）：严格 **90.9%**、加权 **94.8%**、覆盖 **98.6%**。

这个口径下 **82–84% 仍然偏低（保守），不是美化值**；但主口径的严格率 88.1% 与
「8 项未取证时」相同，说明 Mock 证据没有带来严格分。

### 建议的最终写法

> 能力域加权：**81.5%**（14 域等权、严格口径、主口径；若 8 项真机确认则上界 86.3%）；
> 已注册节点加权：**88.1% 严格 / 93.4% 加权 / 98.6% 覆盖**（分母 286，不含 legacy 290；
> 括号内上界：若 8 项真机确认则 90.9% / 94.8% / 98.6%）。

不得把 90.9% / 94.8% 作为 D.2 主数字——那两组数只有在 8 项真机验证通过后才成立。

> **本节已被 §13 取代（2026-11-05）**：以上 88.1% 的对外写法只保留给内部进度跟踪；
> **对外主口径用 63.3%**（5 条原「未覆盖」已补齐 20 条断言）；58.4% / 56.6% 仅作含重复扣减的保守下界。
> 「82–84% 区间写法」也不再对外使用——它不是任何实测口径的复现值（见 `PROJECT_STATUS.md` §1）。

## 8. Legacy 兼容度与已注册节点可用率（并列表，不可混算）

| 指标 | 分子 / 分母 | 数值 | 来源 |
|---|---|---:|---|
| Legacy 全量兼容 | 69 / 290 | **23.8%** | `docs/B6_LEGACY_COMPAT.md` 第 28 行 |
| Legacy 用户可感知 | 69 / 233 | **29.6%** | 同上第 29 行（233 = 290 − 57 永不移植） |
| Legacy 语义口径 | 待查 | **40.6%**（用户给定） | 全仓 `grep 40.6` 无命中；本审计不推导，标记为待查 |
| 已注册节点严格（主口径） | 252 / 286 | **88.1%** | 本文第 5.1 节（8 项为 `mock-verified`，严格计 0） |
| 已注册节点加权（主口径） | 267 / 286 | **93.4%** | 同上 |
| 已注册节点覆盖（主口径） | 282 / 286 | **98.6%** | 同上 |
| 已注册节点三口径（上界） | 260 / 271 / 282 | **90.9% / 94.8% / 98.6%** | 8 项真机确认后（`mock-verified` → `full`） |

**不可相加、不可换算**，原因：

1. 分子不同：legacy 的分子是「从 290 条 legacy 行里迁移过来的条数」；
   已注册可用率的分子是「286 个新节点里分档为 full 的个数」。
2. 分母不同：290/233 是 legacy 行数；286 是运行时注册的 type_id 数。
3. 方向不同：legacy 指标衡量「旧 API 覆盖了多少」，节点指标衡量「新节点本身能不能用」。
   一个节点可以用，但它可能不对应任何 legacy 行；一个 legacy 行迁移了，也可能只到 approximate。

## 9. 待查清单（D.2 前建议补齐，按性价比排序）

1. **7 项真机验证**（B3 音频 7 项）——**能把主口径严格率从 88.5% 抬到 90.9% 的动作**；`ActionDone` 已按实现口径计入 `full`（证据等级 M），不再靠真机确认抬分。
   当前 7 项为 `mock-verified`，`ActionDone` 记 `full`（证据等级 M）（证据见第 11 节与 `/tmp/audio_evidence.log`），
   Mock 只证明节点语义与参数读取；真实声卡/资源解析/`BL_ActionManager` 时序均 `pending real device`。
2. **Legacy 语义 40.6% 的算式**——doc 内没有落地定义，D.2 若要引用必须先定义分子/分母。
3. **B2 值计算 42 项的逐项 E2E**——当前是 B 级证据（代码审阅），不是 A 级。
4. **B4 UI 9 项、B4File 8 项的逐项分档**——本审计按既有实现计 full，文档里没有逐节点表。
5. **其它域的疑似 Mock-only 项**——见第 12 节，逐项列出、未改档，等对应线核实。
6. 真机输入（`REAL_DEVICE_VERIFICATION.md` 线）——本审计未引用其未完成结论，按“不要改”的约束原样保留。

## 10. 复现与边界

复现命令见 `/tmp/capability_audit.log` 第七节，核心三条：

```bash
python3 scripts/startup/bge_gamenodes/generator/port_contract.py
# PORT CONTRACT native_nodes=223 catalog_nodes=68 errors=0 warnings=4  # B6.5a 后快照；K 冻结后为 native_nodes=244
```

```bash
python3 - <<'PY'
# (full, mock-verified, approximate, unsupported, unclassified)
D={'值计算':(42,0,0,0,0),'动画':(10,1,0,0,0),'音频':(1,7,4,0,0),'输入':(22,0,0,1,0),
   '字符串列表':(29,0,5,0,0),'网络':(11,0,0,0,0),'传感器':(6,0,2,0,0),'场景':(8,0,0,1,0),
   'UI':(13,0,2,0,0),'文件':(11,0,2,0,0),'物理（基础）':(16,0,0,0,0),
   '物理高级':(11,0,3,0,0),'动画高级':(4,0,2,2,0),'其他':(68,0,2,0,0)}
N=sum(sum(v) for v in D.values())
F=sum(v[0] for v in D.values()); M=sum(v[1] for v in D.values())
A=sum(v[2] for v in D.values()); U=sum(v[3] for v in D.values()); C=sum(v[4] for v in D.values())
print(N,F,M,A,U,C)
print('strict  %.2f%%' % (F/N*100))
print('weighted %.2f%%' % ((F+0.5*M+0.5*A)/N*100))
print('coverage %.2f%%' % ((F+M+A)/N*100))
# 主口径 => 286 252 8 22 4 0 / 88.11 / 93.36 / 98.60
# 上界（M 全部真机确认并入 F）=> 90.91 / 94.76 / 98.60
PY
```

```bash
# 8 项证据重跑（独立 TU 编译，不走 ninja，不改 build_linux）
tests/run_all_e2e.sh --only b3_audio
# => 31 条断言，INFO failures=0（退出码 0）
```

边界声明：

- 本轮**未修改任何源码**；未跑 `ninja`；未删除 `build_linux`；
- 未修改 `KX_GameAdapters.h/.cpp` 音频段（只读），未改编译器文件、未改 `_lib.glsl`；
- 未修改 `B6_NETWORK.md`、`B6_UI_ADVANCED.md`、`SOUND_BRIDGE_EVAL.md`、`REAL_DEVICE_VERIFICATION.md`；
- 本审计的分档来自「既有文档限制 + 注册表 + 代码审阅 + 历史回归日志 + 本轮 Mock E2E」，
  没有对 286 个节点逐项重跑 E2E；`unclassified` 已归零，没有用猜测填充；
- **8 项新增证据全部是 Mock 层**，不能代表真机听感/真机动画播放，真机项见第 11 节。

## 11. 证据补齐后的口径变化（2026-11-05）

本节只补证据、不改实现：8 项原 `unclassified` **改判 `mock-verified`**（不是 `full`）。
判定依据是第 3 节的硬规则：行为链只在 Mock 适配器层被断言，真实引擎/真机路径未执行。
完整日志：`/tmp/audio_evidence.log`；重跑：`tests/run_all_e2e.sh --only b3_audio`。

口径说明：`mock-verified` 在严格口径计 0、加权计 0.5、覆盖计 1。
因此本节的「补齐前 → 补齐后」只改加权与覆盖；**严格口径不变**（88.1%）。

### 11.1 逐项证据

实现位置：`source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp`；
适配器只读引用：`KX_GameAdapters.h`（`KX_MockAudioAdapter` 1467-1569、`KX_MockAnimationAdapter` 911-1024）、
`KX_GameAdapters.cpp`（`MockAudio::PlaySoundByName` 560-572、`Set3DListener` 574-582、`IsFinished` 619-623）。

| 节点 | 实现行 | 语义（实际读参方式） | 断言链（Mock） | 分档 |
|---|---|---|---|---:|
| `StartSound` | 391-413 | `OnEnter`；`File` 空回退 config `file`；Volume/Pitch ≤0 回退 1.0；句柄经 `B3SoundHandleInt`（361-377）压到 32 位，超 `INT32_MAX` 立即 StopSound 且 Handle=0 | `play_calls=1`、`entries[1].filepath="test.wav"`、`loop=true`、`volume=0.7`、`pitch=1.2`、`playing=true`、`Handle=1`、`OnStarted=1` | **mock-verified** |
| `StopSound` | 430-437 | `OnEnter`；Handle 转 uint64 调 `StopSound`；`OnDone` 无条件触发 | `stop_calls=1`、`entries[1].playing=false`、`OnDone=1` | **mock-verified** |
| `SetSoundVolume` | 439-446 | `OnEnter`；`SetVolume(handle, Volume)`；`OnDone` | `volume_calls=1`、`entries[1].volume=0.6`、`OnDone=1` | **mock-verified** |
| `SetSoundPitch` | 448-455 | `OnEnter`；`SetPitch(handle, Pitch)`；`OnDone` | `pitch_calls=1`、`entries[1].pitch=1.5`、`OnDone=1` | **mock-verified** |
| `SoundFinished` | 466-482 | 每帧 Tick；Handle 变化重新武装；`IsFinished` 为真且未触发 → `PendingTrigger` 一次 | 未完成帧不触发；完成帧触发 1 次；下一帧仍为 1 次 | **mock-verified** |
| `PlaySoundByName` | 497-518 | `OnEnter`；`Name` 空回退 config `name`；Handle=0/越界 → `Found=false` 且不触发 `OnDone` | `play_by_name_calls=1`、`by_name_requests[0]="explosion"`、`Handle=1`、`Found=true`、`OnDone=1` | **mock-verified** |
| `Set3DListener` | 527-538 | `OnEnter`；Position 取输入向量，Orientation 缺省 `(0,0,-1)`；调 `Set3DListener`；`OnDone` | `listener_calls=1`、`listener_position=(1,2,3)`、`listener_orientation=(0,1,0)`、`OnDone=1` | **mock-verified** |
| `ActionDone` | 229-255 | 每帧 Tick 查 `GetActionName(object, layer)`；名字变化重新武装；名字为空不触发；`IsActionDone` 为真且未触发 → 触发一次 | action="run"/done=false 第 1 帧不触发；done=true 第 2 帧触发 1 次；第 3 帧仍为 1 次 | **full**（证据等级 **M**） |

合计 31 条断言，`INFO failures=0`。

### 11.2 分档变更

| 域 | 节点 | 变更前 | 变更后 |
|---|---|---|---:|
| 音频 | 7 项（`StartSound`/`StopSound`/`SetSoundVolume`/`SetSoundPitch`/`SoundFinished`/`PlaySoundByName`/`Set3DListener`） | `unclassified` ×7 | `mock-verified` ×7 |
| 动画 | `ActionDone` | `unclassified` ×1 | `full`（证据等级 M）×1 |

**分局变化**（mock = `mock-verified`）：

| 域 | 分档前 (full/mock/approx/unsup/unclass) | 严格 | 加权 | 覆盖 | 分档后 | 严格 | 加权 | 覆盖 |
|---|---|---|---:|---:|---:|---|---:|---:|---:|
| 音频（12） | 1 / 0 / 4 / 0 / 7 | 8.3% | 25.0% | 41.7% | 1 / 7 / 4 / 0 / 0 | **8.3%** | **54.2%** | **100%** |
| 动画（11） | 10 / 0 / 0 / 0 / 1 | 90.9% | 90.9% | 100% | 10 / 1 / 0 / 0 / 0 | **90.9%** | **95.5%** | **100%** |

注：音频与动画的**严格率没有变化**——Mock 证据在严格口径计 0。
变化只发生在加权（音频 25.0%→54.2%，动画 90.9%→95.5%）和覆盖（音频 41.7%→100%）。

### 11.3 全域三口径重算（两套数）

按「已注册节点数 286」为分母，`mock-verified=0.5`（加权）、`approximate=0.5`、`unsupported=0`：

**主口径**（Mock 不算严格；D.2 用这组）：

| 口径 | 补齐前 | 补齐后（主口径） | 算式 |
|---|---:|---:|---|
| **严格**（`full`） | 88.1% | **88.1%** | 252 / 286 |
| **加权**（`full + 0.5*mock + 0.5*approx`） | 92.0% | **93.4%** | 267 / 286 |
| **覆盖**（`full + mock + approx`） | 95.8% | **98.6%** | 282 / 286 |

**上界**（假设 8 项真机全部确认，`mock-verified` → `full`）：

| 口径 | 上界值 | 算式 |
|---|---:|---|
| 严格 | **90.9%** | 260 / 286 |
| 加权 | **94.8%** | 271 / 286 |
| 覆盖 | **98.6%** | 282 / 286 |

14 域等权口径（严格 / 加权 / 覆盖）：

| 口径 | 补齐前 | 补齐后（主口径） | 上界 |
|---|---|---:|---:|
| 14 域等权严格 | 81.5% | **81.5%** | 86.3% |
| 14 域等权加权 | 86.9% | **89.3%** | 91.7% |
| 14 域等权覆盖 | 92.3% | **97.1%** | 97.1% |

`unclassified` 计数：8 → **0**，同时新增 `mock-verified` 8 项。四个 `unsupported`
（动画高级 2 + 场景 1 + 输入 1）未变，22 项 `approximate` 未变。
因此本轮增量全部来自「证据层级提升」（`unclassified` → `mock-verified`），
**没有提升严格口径，也没有掩盖任何能力缺口**。

两套数的正确用法：

- **主口径** 88.1% / 93.4% / 98.6% —— 「实现成立」口径，内部跟踪用这组（对外见 §13）；
- **上界** 90.9% / 94.8% / 98.6% —— 只能以「若 8 项真机确认」的括号形式出现。
  若把上界当主数字，等于用 Mock 冒充真机，违反本条任务的硬规则。

### 11.4 证据边界（重要）

这 8 项是 **Mock 适配器层证据**，不是真实听感/真实播放验证。验证链是
「节点触发 → 适配器调用计数/参数 → 适配器内部状态迁移 → 节点触发回调」，
能证明节点的**语义与参数读取正确**，不能证明以下真机行为：

- 真实声卡输出、`Loop` 实际循环、Volume/Pitch 实际听感；
- `PlaySoundByName` 在真实资源表里按名解析是否命中；
- `Set3DListener` 在真实 audaspace 下的 3D 衰减/方位变化；
- `ActionDone` 在真实 `BL_ActionManager` 完成回调下的时序（当前只证明「触发一次且仅一次」的状态机）。

以上统一标 **`pending real device`**，不得用本节结果冒充真机通过。
本轮核对未发现音频实现缺陷；仅记录一个边界：Mock 的 `IsFinished` 对未知句柄返回 true
（`KX_GameAdapters.cpp` 619-623），真实实现不适用，不改代码。

## 12. 验证层级说明（审计局限）

### 12.1 本审计的分档只回答「实现是否成立」

第 3 节的 `full` / `approximate` / `unsupported` 原本只区分**实现层面**的结论
（能不能用、是否语义近似、是否引擎能力缺失），**没有系统区分证据来自哪里**：
Mock 适配器 / 真实引擎适配器 / 真机。本次新增 `mock-verified` 档，只把**已经明确**
只有 Mock 证据的 8 项拎出来；其余 278 项仍沿用旧口径，**没有做统一的证据来源审计**。

这是本审计的硬局限：

- 历史 E2E（`/tmp/b62_e2e`、`/tmp/b63_nodes_e2e`、`/tmp/b64_e2e`、`/tmp/b65a_e2e`、
  `/tmp/b61_e2e` 等）多数由 Mock 适配器驱动真实节点图，验证的是节点调度、端口连线、
  配置解析与适配器契约，**不是真实引擎/真机行为**；
- 因此下表中的项若按第 3 节的新规则复核，很可能也应判 `mock-verified`，而不是 `full`。
- **本审计不改它们的档**——那些是各自线（B6.1/B6.2/B6.4/B6.5/legacy）的结论，
  本文只列「疑似 mock-only，待核」，供 D.2 与各线对齐。

### 12.2 疑似 Mock-only 项清单（不改档，待核）

**A. 文档已明说 E2E 由 Mock 适配器驱动（41 项）**

| 域 | 节点 | 依据 |
|---|---|---|
| 动画（B3） | `PlayAction`、`PlayActionByName`、`StopAction`、`SetActionFrame`、`SetActionSpeed`、`SetLayerWeight`、`GetActionFrame`（7） | B3 全套走 `KX_AnimationAdapter`，E2E 用 Mock 动画适配器 |
| 动画高级 | `PlayActionByLayer`、`BlendActions`、`SetAnimationSpeed`、`GetAnimationFrame`（4） | `docs/B6_ANIMATION.md` 第 72 行：「E2E 用 Mock 动画适配器驱动真实节点图」；分档依据是 `KX_GameAdaptersEngine.cpp` + `BL_*` 的**代码引用**，未在真实引擎路径执行 |
| 音频（B6.5） | `SetAudioAttenuation`、`SetAudioDoppler`、`AudioZone`、`SetAudioBusVolume`、`PlaySound3D`（5） | `docs/B6_AUDIO.md` §6 E2E 为 Mock 适配器 |
| 物理高级 | 14 项全部 | `docs/B6_PHYSICS_ADVANCED.md` 第 115 行：「E2E 使用 Mock 适配器驱动真实节点图」 |
| 文件（B6.5） | `SaveGameBinary`、`SaveGameSlots`、`EncryptData`、`DecryptData`、`FileReadLines`（5） | `docs/B6_FILE_ADVANCED.md` 第 128 行：Mock 用 `savebin:<slot>` 键 |
| UI（B6） | `CreateScrollView`、`CreateListView`、`CreateTabView`、`UIAnimation`、`UIScreenAnchor`、`UIScreenStack`（6） | `docs/B6_UI_ADVANCED.md` 第 167 行：真实 BLF 加载路径不在 headless 中断言 |

**B. 文档没有逐项写明驱动方式（49 项，需各线自证）**

| 域 | 节点 | 疑似原因 |
|---|---|---|
| 输入 | `GamepadButton`、`GamepadAxis`、`GamepadTrigger`、`VibrateGamepad`、`KeyCombo`、`InputMap`、`GetInputValue`、`JoystickAxis`、`JoystickButton`（9） | `docs/B6_INPUT.md` §4：headless/mock 可完整注入，真机设备路径未验证 |
| 传感器 | `RadarExtended`、`LineOfSight`、`AreaSensor`、`RaycastSensor`、`ProximitySensor`、`Touch`、`Radar`（7） | 无原生区域触发器（`B6_SENSOR.md` §5），E2E 由场景查询/Mock 驱动 |
| UI（B4） | `CreateUICanvas`、`CreateUILayout`、`CreateUILabel`、`CreateUIImage`、`CreateUIButton`、`CreateUISliderWidget`、`MoveWidget`、`SetUIWidgetAttr`、`GetUIWidgetAttr`（9） | 本审计原本就注明「无逐节点表，按既有实现计 full」 |
| 文件（B4File） | `SaveGame`、`LoadGame`、`ReadFile`、`WriteFile`、`FileExists`、`DeleteFile`、`JSONStringify`、`JSONParse`（8） | Mock/Engine 两套实现（`B6_FILE_ADVANCED.md` 第 19 行），未确认 E2E 走哪套 |
| 其他（legacy） | 16 项 legacy 节点 | `/tmp/b65a_e2e` 的驱动适配器未在文档逐条注明 |

**C. 已有真实引擎路径、但缺真机/跨机（11 项）**

| 域 | 节点 | 说明 |
|---|---|---|
| 网络 | 全部 11 项 | `B6_NETWORK.md` §9 有真实引擎适配器 E2E + 本机双 peer UDP；但没有跨机真实网络验证。**不算 mock-only，算「真实引擎已验、真机环境未验」** |

### 12.3 风险提示

若把上表 A 的 41 项按新规则改判 `mock-verified`（主口径），全域严格率会从 88.1% 跌到约
**73.8%**（211/286），加权约 **86.2%**，覆盖仍 98.6%。这只是**历史情景**，不是本审计结论：
本文不替其它线改档。它的用途是提醒 D.2——**88.1% 是「实现成立」的下界，
不是「真机可交付」的下界**；引用时必须同时给出第 12.2 节清单。

> **已由 §13 取代（2026-11-05）**：B 类 49 条实测为 **44 mock-driven / 0 engine-driven /
> 5 未覆盖**（[VERIFICATION_LEVEL_AUDIT.md](VERIFICATION_LEVEL_AUDIT.md)），A 41 + B 44 一并降档后
> **实算对外主口径为 63.3%**（原 5 条「未覆盖」已全部补齐、20 条断言全 PASS）；
> **58.4% / 56.6% 只作含重复扣减的保守下界**。
> 73.8% 仅记录「当时只敢说是风险」的阶段，不再作为对外数字。

### 12.4 给 `KNOWN_LIMITATIONS` 的候选条目（不由本线写入）

> Mock 音频适配器的 `IsFinished` 对未知句柄返回 true（`KX_GameAdapters.cpp` 619-623）；
> 真实 audaspace 实现不适用。仅为 Mock 行为，不影响引擎路径，但会让「句柄失效后是否已结束」
> 的 Mock 断言产生假阳性。

本线不改 `KNOWN_LIMITATIONS.md`（归另一条线维护），仅在此登记候选。

## 13. 证据层级口径（A/B 类实测后）

> 本节是 §12.3 的实测收口，**不替代 §4 的实现分档，也不删除 88.1%**。
> 证据来源：[VERIFICATION_LEVEL_AUDIT.md](VERIFICATION_LEVEL_AUDIT.md)（B 类 49 条逐条查到驱动实现层）。
> 本节只写核算，不改任何线的分档；**63.3% 是可复算的实算值**（只扣真正从 `full` 跌落的节点），
> **58.4% / 56.6% 是额外保守下界**（把 14 条 `approximate` 也再扣一遍，含重复扣减）。
> **原 5 条「未覆盖」已全部补齐（20 条断言全 PASS），不再有「剔除未覆盖」这一档。**
> 档位本身仍按 §3 的 `full` / `mock-verified` / `approximate` / `unsupported` / `unclassified`。

### 13.1 三档口径（同一张表，按用途分开引用）

| 档 | 分档构成（full / mock / approx / unsup / unclass） | 严格 | 加权 | 覆盖 | 定位 |
|---|---|---:|---:|---:|---|
| **实现成立**（§0 主口径，保留） | 252 / 8 / 22 / 4 / 0 | **88.1%**（252/286） | **93.4%**（267/286） | **98.6%**（282/286） | 内部进度跟踪：只回答「节点有没有写」 |
| **证据层级实算** | 181 / 79 / 22 / 4 / 0 | **63.3%**（181/286） | **80.9%**（231.5/286） | **98.6%**（282/286） | **对外主口径** |
| **重复扣减下界** | 167 / 93 / 22 / 4 / 0 | 58.4%（167/286）/ 56.6%（162/286） | 78.5% / 76.7% | 98.6% / 96.9% | **下界，非实测值**（含 14 条重复扣减） |

一句话定义：**88.1% 回答「有没有实现」；63.3% 是「只把真正从 full 跌落的节点按 Mock 档记账」的可复算实算值；
58.4% / 56.6% 是把 90 条一律从 full 扣掉（含 14 条 `approximate` 重复扣减）的保守下界，
只用于「我们要比这还不乐观」的场合。**

- **对外发布：主口径用 63.3%**。原 5 条「未覆盖」已全部补齐（20 条断言全 PASS），
  因此**没有「剔除未覆盖」档**（再剔只会得到同一个 63.3%）。引用时必须给分母 286 并引
  `VERIFICATION_LEVEL_AUDIT.md`。
- **内部进度跟踪：可用 88.1%**（用于排期、找缺口，不得单独对外）。
- **58.4% / 56.6% 只能以「更保守下界（含 14 条重复扣减）」出现**，不得当实测值或主口径。

### 13.2 加权 / 覆盖的重算过程（可复算）

**实算**（只扣真正从 `full` 跌落的 **71 条** = 85 − 14；那 14 条仍记 `approximate`）：

```
严格 A = (252 − 71) / 286 = 181 / 286 = 0.632867 → 63.3%
加权 B = (181 + 0.5×79 + 0.5×22) / 286 = 231.5 / 286 = 0.809441 → 80.9%
覆盖 C = (181 + 79 + 22) / 286 = 282 / 286 = 0.986014 → 98.6%
```

**原 5 条「未覆盖」已全部补齐**：`JoystickAxis`、`JoystickButton`、`ProximitySensor`、
`JSONStringify`、`JSONParse` 补真断言后 **20 条全 PASS**（`/tmp/b66_uncovered_assertions.log`），
所以不再有「剔除 5 条」这一档，实算即 63.3%。

> 留档（漏掉原因）：这 5 条当初漏掉，是因为「**注册了但全仓无 `type_id` 引用**」——
> `port_contract` 与 `enum_consistency` 都不会报「从未执行」。补齐时还发现
> `docs/B6_SENSOR.md` 的端口表与触发语义写错了：实现是 `Object` + `Distance → OnNear / OnFar /
> DistanceValue`、每帧求值，已由另一条线订正。

**重复扣减下界**（把 14 条 `approximate` 也再扣一遍，算术上刻意多扣，仅供最保守引用）：

```
严格 = (252 − 85) / 286 = 167 / 286 = 58.4%
加权 = 224.5 / 286 = 78.5%
覆盖 = 282 / 286 = 98.6%
再减 5 条的历史更低下界 = 162 / 286 = 56.6%（加权 219.5 / 286 = 76.7%，覆盖 277 / 286 = 96.9%）
```

**14 域等权口径未重算**：逐域重排需要先裁定 §13.3 的 14 条 `approximate` 归属，
本节只更新节点严格 / 加权 / 覆盖，域等权维持 §5.2 的 81.5% / 89.3% / 97.1%（实现成立口径）。

### 13.3 算术校验：85 条里有 14 条本来就是 `approximate`

把 §12.2 的 A/B 名单与 §4 的分档列对照，**有 14 条在 §4 已是 `approximate`**，严格口径本就计 0：

| 域 | 重叠条数 | 节点 |
|---|---:|---|
| 音频 | 4 | `SetAudioAttenuation`、`SetAudioDoppler`、`AudioZone`、`SetAudioBusVolume` |
| 物理高级 | 3 | `CreateVehicle`、`AddConstraint`、`BuildNavMesh` |
| 文件 | 2 | `EncryptData`、`DecryptData` |
| UI | 2 | `CreateListView`、`UIScreenStack` |
| 传感器 | 1 | `AreaSensor` |
| legacy（其他） | 2 | `EvaluatePropertyLegacy`、`TouchLegacy` |

因此「85 条从 full 扣减」会把这 14 条重复扣掉。**可复算的实算值只扣真正是 `full` 的 71 条**
（= 85 − 14，这 14 条留在 `approximate`）：

| 口径 | 严格 | 加权 | 覆盖 | 定位 |
|---|---:|---:|---:|---|
| 证据层级实算（只扣 71 条 full） | **63.3%**（181/286） | **80.9%**（231.5/286） | **98.6%**（282/286） | **对外主口径** |
| 重复扣减下界（85 条一律从 full 扣） | 58.4%（167/286）/ 56.6%（162/286） | 78.5% / 76.7% | 98.6% / 96.9% | **下界，非实测值** |

结论：**对外主口径采用 63.3%（严格）/ 80.9%（加权）/ 98.6%（覆盖）。**
原 5 条「未覆盖」已全部补齐（20 条断言），因此不存在「再剔除 5 条」这一档。
58.4% / 56.6% 只能写成「更保守下界（含 14 条 `approximate` 的重复扣减）」，**不得当实测值呈现**——
一个能被读者按 §4 逐列复算的 63.3%，比一个更保守但分子对不上的 58.4% 可信。

## 14. B6.9-N 网络增量重评分（2026-11-06；已计入 K 冻结口径，`RUN_ID=20261106-224926`）

> 本节只重算网络域，**不改 §0/§4 的 286 分母与 88.1%/63.3%**。K 已冻结（B.5：网络域 14 节点），
> 全域最终值以 `NUMBER_CONSISTENCY_AUDIT.md` B.5 的六量为准；本节不得被当作全域最终值。

### 14.1 分子分母

网络域节点由 **11** 增至 **14**（`DiscoverRooms`、`SyncPlayerState`、`GetPlayerState` 三个已注册节点，均带编辑器目录条目）：

| 口径 | 分子 / 分母 | 百分比 | 依据 |
|---|---|---:|---|
| 实现成立（§4 同一口径） | **14 / 14** | **100.0%** | 三个新节点均为真实引擎实现，无 Mock、无近似、无降级 |
| 证据层级（真实引擎路径） | **14 / 14** | **100.0%** | N-1/N-2 都是真实 UDP + 两个独立进程；非单进程 mock |
| 真机 / 跨机验证 | **0 / 14** | **0.0%** | 所有 E2E 都在同一台 Linux 上；没有第二台机器、没有真实局域网多 host、没有真实 WAN 丢包 |

**引用规则**：对外只能说「网络 14/14 实现且真实引擎本机两进程已验；跨机 0/14 未验」。
**不得**把 100% 写成「LAN 已跨机验证」，也**不得**照抄历史上任何「70%→90%」的估值——
本轮没有产生跨机证据，分数不因新增节点自动升到 90% 以上。

### 14.2 为什么不是 mock

- N-1：host 进程真实 `socket(AF_INET, SOCK_DGRAM)` + `SO_BROADCAST` 广播；client 进程独立 socket 收应答；伪造包用第三方原始 UDP socket 验证「不应答 ROOMINFO」。证据 `n1_discover_two_proc` 14/0。
- N-2：host/client 两个进程各持真实引擎适配器，`SendDatagram`/`recvfrom` 走内核 UDP；人为停 tick 200 ms 制造缺口。证据 `n2_state_sync_two_proc` 14/0。
- 仍属「本机回环/本机 LAN 接口」，所以真机/跨机口径为 0/14，而不是把本机两进程当成跨机。

### 14.3 明确排除项（不进分母）

NAT 穿透、状态回滚、客户端预测已写入 `DEFERRED_FOREVER`，**不计入 14 的分子分母**；N-2 明确不做插值/外推，丢包只保持上一位姿。

## 15. B6.9-Fi / B6.9-Ms 增量重评分（2026-11-06；已计入 K 冻结口径，`RUN_ID=20261106-224926`）

> 与 §14 同一规则：只重算**本轮新增**，不改 §0/§4 的 286 分母，不改任何历史百分比。
> 增量已并入 B.5（文件域 B-20、消息域 B-22）。所有数字给分子/分母，可复算。

### 15.1 文件域（B6.9-Fi）

新增 5 个节点：`FileReadChunk`、`FileWriteChunk`、`ListDirectory`、`CreateDirectory`、
`RemoveDirectory`（`kB6FileStreamSpecs[]`，编辑器目录 5/5 有条目）。文件域总数
`13 → 18`（B4File 8 + B6File 5 + 本轮 5）。

| 口径 | 分子 / 分母 | 百分比 | 依据 |
|---|---|---:|---|
| 实现成立（§4 同一口径） | **5 / 5** | **100.0%** | 5 个节点都有真实运行时实现与适配器实现，无 stub、无近似 |
| 节点语义 E2E | **5 / 5** | **100.0%** | `b69_file` 19/0；覆盖写 3 块逐字节、EOF、offset 越界、chunk_size 0/1048577、建/列/删、非空拒绝、递归删除、3 条穿越拒绝 |
| **真实磁盘后端 E2E** | **0 / 5** | **0.0%** | `b69_file` 用的是 `KX_MockFileAdapter`（内存）；`KX_FileAdapterEngine` 已实现并随树编译，但**没有任何测试驱动真实文件系统**。不得把这 5 个算作"真机验证通过" |

**引用规则**：只能说「文件新增 5 个节点实现完成、Mock 层 E2E 19/0；
真实磁盘后端 0/5 未验」。不得写"文件域 100% 已验"。

### 15.2 消息域（B6.9-Ms）

新增 3 个节点：`SendRPC`、`OnRPC`、`SendDelayedMessage`（`kB6MsgSpecs[]`，编辑器目录 3/3 有条目）。

| 口径 | 分子 / 分母 | 百分比 | 依据 |
|---|---|---:|---|
| 实现成立（§4 同一口径） | **3 / 3** | **100.0%** | 三节点均为真实实现；`match` 复用 `ConfigInt("match", 0) == 1` 枚举，未引入字符串比较 |
| 本地回环 / 延迟调度 E2E | **3 / 3** | **100.0%** | `b69_msg` 20/0：发→收、不匹配不触发、同帧两条触发两次、前缀 vs 精确、0.25s 未到 / 0.5s 恰一次、负 delay 拒绝、卸载丢弃计数 |
| **真实 UDP/跨机 RPC** | **0 / 3** | **0.0%** | RPC 信封（`EBCRPC1`）只在 `b69_msg` 里以注入事件 + Mock 网络适配器验证；**没有两个进程用真实 UDP 跑过 `SendRPC`/`OnRPC`**。§14 的 14/14 是 N 线自己的节点，不能借给本行 |

**引用规则**：只能说「消息新增 3 个节点实现完成、本地/调度 E2E 20/0；
真实 UDP RPC 0/3 未验」。**不得**因为 §14 网络域 14/14 就把消息域写高。

### 15.3 为什么不能照抄旧估值

`82%→92%`、`70%→90%` 这类数字本轮没有对应证据：新增节点里
文件只有 Mock 后端（真实磁盘 0/5）、消息只有 Mock/注入信封（真实 UDP 0/3）。
按 §13.1 的证据层级，这两块应记为
「实现 8/8、Mock 级 E2E 8/8、真实引擎传输/磁盘 0/8」，
**全域百分比不因本轮新增而上升**，等 K 跑真实后端后再重算。

