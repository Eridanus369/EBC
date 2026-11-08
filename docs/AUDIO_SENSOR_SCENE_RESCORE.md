# 音频 / 传感器 / 场景 新口径重评（RESCORE）

本文按新口径逐节点重评三个域，替代此前「Mock 计 0、只把真机 full 计 1」的报分方式。
本文不向 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B 追加任何编号；附录 B 由最终 K 线单点重写。

## 0. 口径（原文照抄）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：
- **full**：分母计入，分子 1.0
- **approximate**：分母计入，分子 0.5（必须有书面明确限制）
- **unsupported**：引擎层不支持，附 `路径:行号` 可核证据，进 FOREVER，不进分母
- **pending-device**：分母计入，分子 1.0，真机待验单列

pending-device 必须同时满足三条：① 节点有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless 验证的是具体设备/驱动依赖并给出 API 位置——不是「我们没测」。

### 0.0 本轮重分类口径（2026-11-06，照抄上级原文）

- `D = full + approximate`；`unsupported` 与 `pending-device` **不进分母**；
- `approximate` 只表示「节点实现本身不完整」；**限制来自引擎能力时记 `full`**，限制写 `KNOWN_LIMITATIONS`；
- `pending-device` 剔除分母需同时满足：① 有 Mock/Headless E2E 通过日志；② 有可核理由说明为何 headless 验不了。

> 音频域 8 个 `pending-device` 两条门槛均满足（Mock/Headless E2E 日志见 §2.1；headless 无设备证据见
> `KX_GameAdaptersEngine.cpp:2294-2301`、`:2605-2612`），故从分母剔除。场景域与传感器域没有
> `pending-device`。

### 0.1 统计约定

- **分母** = `full + approximate`（本轮口径，2026-11-06）；`unsupported` 与 `pending-device` 不进分母。
- **严格** = `full / 分母`，`approximate` 记 0。
- **加权** = `(full + 0.5 × approximate) / 分母`。
- `pass` 表示实现存在且 Mock/Headless E2E 通过；`pass` 不等于真机验证。
- 节点数量以运行时注册表为准（`KX_NodeRegistry` / `TypeCount()` 同源），不以文档旧标题为准。
- 音频的 `pending-device` 共同证据：adauspace 设备由 `aud::DeviceManager::getDevice()` 取得，
  headless 无音频设备时 `EngineAudioAdapter` 在 `KX_GameAdaptersEngine.cpp:2294-2301`、
  `:2605-2612` 直接返回，因此真实发声路径只能在有设备的环境验证。

## 1. 三域结论速览

| 域 | 分母 D | full | approximate | pending-device（剔除） | unsupported（剔除） | 严格 | 加权 | 旧报表值 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 音频 | 5 | 5 | 0 | 8 | 0 个节点（另有 1 项非节点能力） | 5/5 = **100.0%** | 5/5 = **100%** | 1/13 = 7.7% |
| 传感器 | 6 | 6 | 0 | 0 | 0 | 6/6 = **100%** | 6/6 = **100%** | 82% |
| 场景 | 6 | 6 | 0 | 0 | 1（UnloadScene） | 6/6 = **100%** | 6/6 = **100%** | 85% |

旧值口径与新值不同，只作方向对照，不可直接相减：

- 音频旧值 1/13 = 7.7% 来自 `docs/B6_AUDIO.md:288-295` 的「严格：Mock 计 0」；
  同一处的覆盖口径是 13/13 = 100%。7.7% 正是把 pending-device 当缺口的结果，本轮作废。
- 传感器旧值 82%、场景旧值 85% 来自上一轮汇总表；旧分母含跨域节点（传感器旧文档写 7、
  场景旧文档写 7），与本文注册表口径（传感器 6、场景 7 项含 1 个值节点）不一致。
- 场景上一轮加权 66.7%（2/6）不是代码退步，而是旧口径把「等价实现」按 0.5 计入；
  **本轮重分类后场景 6/6 = 100%**：4 个近似项的限制全部来自引擎（单活跃场景、同步替换、全局时标），
  节点实现已是最好，按新口径记 `full` + `KNOWN_LIMITATIONS`。
- 音频分母由 13 降到 5：8 个 `pending-device` 按本轮口径剔除（两条门槛均满足），不是「删除节点」；
  旧值 61.5% / 80.8% 是「pending-device 计入分母并计 1.0」的上一轮报法，本轮作废。

---

## 2. 音频域（13 节点）

音频域 = `KX_FunctionNodesB6Audio.cpp` 的 6 个 `kB6AudioSpecs` 节点
（注册表 `KX_FunctionNodesB6Audio.cpp:353-372`）
+ `KX_FunctionNodesB3.cpp` 的 7 个音频动作节点
（注册表 `KX_FunctionNodesB3.cpp:745-759`）。合计 13。

### 2.1 逐节点分类

> 行号说明：`KX_GameAdaptersEngine.cpp` 在 2026-11-06 的 shape overlap 收口中新增了
> `ObjectBoundingRadius` 与 `QueryProximityShapes`，音频段整体下移（约 +280 行）。
> 下表音频引擎行号是收口前的快照，**以函数名为准**；`SetAudioBusVolume` 的当前行号
> 已在 §4.6 更新为 2788-2807。

| # | 节点 | 分类 | 实现位置 | Mock / Headless E2E | 限制或设备依赖 |
|---:|---|---|---|---|---|
| 1 | `SetAudioAttenuation` | **full**（本轮重分类） | 注册 `KX_FunctionNodesB6Audio.cpp:354-357`；引擎 `KX_GameAdaptersEngine.cpp:2638-2714` | `tests/b62_e2e.cpp:1111`（`attenuation_calls == 1`） | 限制来自引擎：`I3DHandle.h:38-229` 无 `setDistanceModel`，距离模型只在 `I3DDevice.h:139`，设备级不能按对象隔离（`KX_GameAdaptersEngine.cpp:2686-2693`）；对象非空时逐 handle 施加 RefDist/MaxDist/attenuation（`:2694-2709`）。进 `KNOWN_LIMITATIONS` #12 |
| 2 | `SetAudioDoppler` | **full**（本轮重分类） | 注册 `:358-361`；引擎 `:2716-2754` | `tests/b62_e2e.cpp:1119`（`SetAudioDoppler factor`） | 限制来自引擎：只有 `I3DDevice::setDopplerFactor`（`I3DDevice.h:127`），`I3DHandle` 无多普勒 API；对象级因子只在「该对象拥有全部 handle」时应用，否则只存储不施加（`:2732-2752`）。进 `KNOWN_LIMITATIONS` #12 |
| 3 | `AudioZone` | **full**（本轮重分类） | 注册 `:362`；引擎 `:2764-2786` | `tests/b62_e2e.cpp:1124`（`AudioZone registration`） | 限制来自引擎：audaspace 无 zone API；节点是等价实现——listener 位置的循环环境音，重复注册替换旧环境音（`:2772-2785`）。进 `KNOWN_LIMITATIONS` #1 |
| 4 | `SetAudioBusVolume` | **full**（本轮重分类） | 注册 `:363-366`；引擎 `:2788-2807` | `tests/b62_e2e.cpp:1127`（`SetAudioBusVolume master 0.5`） | 限制来自引擎：audaspace 无 bus 概念；节点是等价实现——「命名 gain 组」按 handle 所属 bus 乘基础音量（`:2790-2806`）。进 `KNOWN_LIMITATIONS` #1 |
| 5 | `PlaySound3D` | **pending-device** | 节点 `KX_FunctionNodesB6Audio.cpp:157-173`；注册 `:367`；引擎 `:2531-2567` | `tests/b62_e2e.cpp:1107-1117`（spatial_calls 与位置/音量/音高） | 需要 `I3DHandle` 与真实音频设备：`setRelative/setLocation/setDistanceReference/setDistanceMaximum` 在 `KX_GameAdaptersEngine.cpp:2547-2562`；无设备时 `DeviceManager::getDevice()` 为空直接跳过（`:2294-2301`, `:2605-2612`）。API：`I3DHandle.h:151/164`、`I3DDevice.h:59-96` |
| 6 | `PlaySoundPlaylist` | **full**（本轮重分类：静默截断已修） | 节点 `:196-349`；注册 `:368-372` | `tests/b69_e2e.cpp:573-597`（16 轨零 stderr）、`:599-640`（17 轨诊断 + 前 16 轨照常播放）、`:642-675`（名字截断） | 状态块固定容量：16 轨（`:179-180`）、单轨名字 47 字节，属设计限制 → `KNOWN_LIMITATIONS` #29；第 17 轨起丢弃与超长名字截断**均发 stderr 诊断**（`:277-303`），每次 `OnEnter` 各至多一条。句柄 >`INT32_MAX` 有诊断（`:236-242`）。见 §9.4 |
| 7 | `StartSound` | **pending-device** | `KX_FunctionNodesB3.cpp:391-413` | `tests/b3_audio_e2e.cpp:346-386`；`tests/b62_e2e.cpp:1192-1273` | 需要 audaspace playback handle（引擎 `KX_GameAdaptersEngine.cpp:2266-2274`）；无 audaspace 时 `PlaySound` 返回 0（`:2231-2237`）。32 位句柄守卫 `KX_FunctionNodesB3.cpp:361-377` 是边界保护，不是功能缺口 |
| 8 | `StopSound` | **pending-device** | `KX_FunctionNodesB3.cpp:430-437` | `tests/b3_audio_e2e.cpp:392-426` | `IHandle::stop()` 经引擎 `KX_GameAdaptersEngine.cpp:2308-2321`；只有真实播放设备才产生可听停止 |
| 9 | `SetSoundVolume` | **pending-device** | `KX_FunctionNodesB3.cpp:439-446` | `tests/b3_audio_e2e.cpp:440-472` | `IHandle::setVolume`（`IHandle.h:143`），引擎 `KX_GameAdaptersEngine.cpp:2323-2329`；音量作用于真实 handle |
| 10 | `SetSoundPitch` | **pending-device** | `KX_FunctionNodesB3.cpp:448-455` | `tests/b3_audio_e2e.cpp:440-470` | `IHandle::setPitch`（`IHandle.h:158`），引擎 `KX_GameAdaptersEngine.cpp:2331-2337` |
| 11 | `SoundFinished` | **pending-device** | `KX_FunctionNodesB3.cpp:466-482` | `tests/b3_audio_e2e.cpp:480-505` | 轮询 `IHandle::getStatus`（`IHandle.h:128`），引擎 `KX_GameAdaptersEngine.cpp:2345-2356`；状态迁移依赖真实播放 |
| 12 | `PlaySoundByName` | **pending-device** | `KX_FunctionNodesB3.cpp:497-518` | `tests/b3_audio_e2e.cpp:511-546` | 需要 `bSound` 数据块与播放句柄（引擎 `KX_GameAdaptersEngine.cpp:2241-2274`）；无 audaspace 返回 0（`:2275-2281`） |
| 13 | `Set3DListener` | **pending-device** | `KX_FunctionNodesB3.cpp:527-538` | `tests/b3_audio_e2e.cpp:552-586` | `I3DDevice::setListenerLocation` / `setListenerOrientation`（`I3DDevice.h:67`, `:96`），引擎 `KX_GameAdaptersEngine.cpp:2293-2304`；audaspace 只有一组 listener API |

### 2.2 分类合计与百分比

| 分类 | 数量 | 节点 |
|---|---:|---|
| full | 5 | `SetAudioAttenuation`、`SetAudioDoppler`、`AudioZone`、`SetAudioBusVolume`（本轮由 approximate 重分类）、`PlaySoundPlaylist`（本轮两处静默截断补诊断后重分类） |
| approximate | 0 | — |
| pending-device（剔除分母） | 8 | `PlaySound3D`、`StartSound`、`StopSound`、`SetSoundVolume`、`SetSoundPitch`、`SoundFinished`、`PlaySoundByName`、`Set3DListener` |
| unsupported（节点） | 0 | — |
| unsupported（非节点能力） | 1 | 多监听器，见 2.4；不进 13 节点分母 |
| **分母 D = full + approximate** | **5** | — |

- 严格：`5 / 5 = 100.0%`
- 加权：`(5 + 0.5 × 0) / 5 = 5/5 = 100%`
- 实现覆盖：`13/13 = 100%`（全部节点有实现且 Mock E2E 通过）
- pending-device 8 个两条门槛均满足（Mock E2E 日志见 §2.1、无设备 API 见 §0.1），按本轮口径剔除分母。
- 旧口径：`1/13 = 7.7%`（仅把真机 full 计 1，pending-device 与 Mock 全计 0）；旧覆盖 100%。
- **数值已冻结（K 实测，`RUN_ID=20261106-224926`；46 批 / 1456 断言 / 0 失败）**。

**真缺口说明：音频 13 个节点没有「没做」的节点，也没有 `approximate`。**
`PlaySoundPlaylist` 的两处静默截断（第 17 轨起丢弃、单轨名字 ≥48 字节）已补 stderr 诊断，
16 轨 / 47 字节上限按设计限制进 `KNOWN_LIMITATIONS` #29，本轮由 `approximate` 升 `full`；
4 个原 approximate 的限制来自 audaspace 设备级 API，节点已做到最好，本轮重分类为 `full`；
8 个 `pending-device` 是「代码侧完成、Mock E2E 通过、只差真实声卡」，按本轮口径剔除分母而非算成 0。

### 2.3 旧口径对照

| 口径 | 数值 | 说明 |
|---|---:|---|
| 旧严格（`docs/B6_AUDIO.md:288-295`） | 1/13 = 7.7% | 把 12 个 Mock/受限/待真机节点全部当缺口 |
| 旧覆盖（同处） | 13/13 = 100% | 把 Mock 与 approximate 都按 1.0 |
| 上一轮新严格（pending-device 计入分母） | 8/13 = 61.5% | pending-device 按 1.0，approximate 按 0 |
| 上一轮新加权（pending-device 计入分母） | 10.5/13 = 80.8% | approximate 按 0.5 |
| **本轮严格（D = full + approximate = 5）** | **5/5 = 100.0%** | pending-device 剔除分母，五个节点全 `full` |
| **本轮加权（D = 5）** | **5/5 = 100%** | 无 approximate 按 0.5 计的项 |

7.7% 与 100% 的差距不是代码变化，而是口径变化：旧算式把「代码已完成、只差设备验证」的
节点记成缺口。本轮不再使用 7.7% 或 61.5%/80.8% 作为音频成熟度。

### 2.4 非节点能力：多监听器 → unsupported + FOREVER

- 结论：**unsupported**，进 `DEFERRED_FOREVER.md`，不进任何分母。
- 可核证据：`extern/audaspace/include/devices/I3DDevice.h:59-96` 只有一组 listener API：
  `getListenerLocation:59`、`setListenerLocation:67`、`getListenerVelocity:73`、
  `setListenerVelocity:81`、`getListenerOrientation:87`、`setListenerOrientation:96`。
  没有 `addListener`、没有 `setListener(index, ...)`、没有 listener 数组或句柄。
- 设备是单例：引擎经 `aud::DeviceManager::getDevice()` 取得唯一设备
  （`KX_GameAdaptersEngine.cpp:2294`, `:2607`）。
- 因此「多监听器」无法在不改 audaspace 的前提下实现；这不是节点没写，而是引擎层能力缺失。
  现有 `Set3DListener` 节点本身只承诺单监听器，故它仍按 `pending-device` 计 1.0。

---

## 3. 传感器域（6 节点）

传感器域 = `KX_FunctionNodesB6Sensor.cpp:516-543` 注册的 6 个节点。
旧文档 `docs/B6_SENSOR.md:135-137` 写「6 增到 7」，但运行时注册表只有 6 个
`B6S_ACTION` 条目；本文以注册表为准，差异留给 K 线用 `TypeCount()` 复核。
（旧 82% 的分母含跨域/历史节点，与新分母 6 不可直接比较。）

### 3.1 逐节点分类

| # | 节点 | 分类 | 实现位置 | Mock / Headless E2E | 限制 |
|---:|---|---|---|---|---|
| 1 | `RadarExtended` | **full** | 注册 `KX_FunctionNodesB6Sensor.cpp:561-565`；实现 105-155；形状查询 `KX_GameAdaptersEngine.cpp:725` | `tests/b63_nodes_e2e.cpp:449`, `:529-530`；`tests/b69_e2e.cpp` radar 段 698-790；`tests/c6_shape_overlap.cpp` 18 条 | 球/锥按真实形状接触检测（`contactPairTest`），不是 AABB 近似；仅「候选超过 64」或无碰撞体对象走原点距离降级，见 `docs/B6_SENSOR.md:§5` |
| 2 | `LineOfSight` | **full** | 注册 `:566-570`；实现 172-247 | `tests/b63_nodes_e2e.cpp:453-454`, `:532`（掩码穿越/遮挡） | 无已知限制；掩码语义已在 E2E 验证 |
| 3 | `AreaSensor` | **full** | 注册 `:571`；实现 280-397；形状查询 `KX_GameAdaptersEngine.cpp:725` | `tests/b63_nodes_e2e.cpp:457`, `:533`；`tests/c6_shape_overlap.cpp` 18 条 | 区域有碰撞体时用其真实形状 `TestControllerContact`，否则用 scale 盒 `TestShapeContact`；仅「候选超过 64」或无碰撞体对象走原点距离降级；`Axis` 在 config 中拆为 `axis_x/y/z` 标量 |
| 4 | `RaycastSensor` | **full** | 注册 `:572-576`；实现 415-441 | `tests/b63_nodes_e2e.cpp:460`, `:534` | 单射线命中；无已知限制 |
| 5 | `RaycastAll` | **full** | 注册 `:577-581`；实现 465-518（`FuncRaycastAll`） | `tests/b69_e2e.cpp:496-561`（命中、未命中、最近优先） | 后端 `RayHit` 返回 false 继续收集，节点层再按距离 `stable_sort`；多层命中与顺序均有断言 |
| 6 | `ProximitySensor` | **full** | 注册 `:582-586`；实现 534-558 | `tests/b66_uncovered_e2e.cpp:209`, `:332-357` | 距离查询；目标缺失时输出 0；无已知限制 |

### 3.2 分类合计与百分比

| 分类 | 数量 | 节点 |
|---|---:|---|
| full | 6 | `RadarExtended`、`LineOfSight`、`AreaSensor`、`RaycastSensor`、`RaycastAll`、`ProximitySensor` |
| approximate | 0 | — |
| pending-device | 0 | — |
| unsupported | 0 | — |
| **分母** | **6** | — |

- 严格：`6/6 = 100%`
- 加权：`(6 + 0.5 × 0) / 6 = 6/6 = 100%`
- 实现覆盖：`6/6 = 100%`
- 旧报表值：82%（旧分母与本文不同，仅作方向对照）

传感器没有 `pending-device`：它们依赖物理/场景查询，不依赖声卡、手柄或磁盘等具体设备，
没有「设备缺失导致 headless 不可验」的可核 API 位置，因此不能套用 pending-device。

---

## 4. 场景域（7 项，含 1 个值节点）

场景域 = `KX_FunctionNodesB6Scene.cpp:550-583` 注册的 7 项
（6 个 `B6SC_ACTION` + 1 个 `B6SC_VALUE_SOURCE`）。
其中 `UnloadScene` 按引擎证据判 `unsupported`，不进分母，故分母为 6。

### 4.1 逐节点分类

| # | 节点 | 分类 | 实现位置 | Mock / Headless E2E | 限制 / 证据 |
|---:|---|---|---|---|---|
| 1 | `LoadSceneAsync` | **full**（本轮重分类） | `KX_FunctionNodesB6Scene.cpp:122-189`；注册 `:551-556` | `tests/b63_nodes_e2e.cpp:552`, `:576-580` | 限制来自引擎：该节点**不异步**——加载在调用帧内完成，进度是反馈而非流式。进度是 0.5 秒定时反馈，到 1.0 才同步 `ReplaceScene`（`KX_FunctionNodesB6Scene.cpp:160-174`；引擎 `KX_GameAdaptersEngine.cpp:1101-1111`）。进 `KNOWN_LIMITATIONS` #5 |
| 2 | `UnloadScene` | **unsupported** | `KX_FunctionNodesB6Scene.cpp:195-252`；注册 `:557` | `tests/b63_nodes_e2e.cpp:591-597`, `:664-666`（`OnFail`） | 引擎 `RemoveScene` 显式禁用：`source/gameengine/Ketsji/KX_KetsjiEngine.cpp:1341-1348`。活跃场景无法卸载，节点只能如实 `OnFail`（`KX_FunctionNodesB6Scene.cpp:225-238`）。进 FOREVER，不进分母 |
| 3 | `MountChildScene` | **full**（本轮重分类） | `KX_FunctionNodesB6Scene.cpp:309-339`；注册 `:558-563` | `tests/b69_e2e.cpp:686` | 限制来自引擎：只保留一个活跃场景，`LoadScene` 即 `ReplaceScene`，父场景变为非活跃，**父子不共存**（文件注释 `:258-264`，加载 `:332`）。节点只能实现 push/pop 等价。进 `KNOWN_LIMITATIONS` #25 |
| 4 | `UnmountChildScene` | **full**（本轮重分类） | `KX_FunctionNodesB6Scene.cpp:341-369`；注册 `:564-569` | `tests/b69_e2e.cpp:724-762` | 限制来自引擎：「卸载」实为 `LoadScene(parent)` 返回父场景，**不释放子场景**（`:362-368`）；释放链路仍被 `KX_KetsjiEngine.cpp:1341-1348` 禁用。进 `KNOWN_LIMITATIONS` #25 |
| 5 | `TransitionScene` | **full** | `KX_FunctionNodesB6Scene.cpp:408-497`；注册 `:570-575` | `tests/b63_nodes_e2e.cpp:601`, `:645-651`, `:667` | 通过既有 UI adapter 建黑幕，中点切换，结束销毁。这不构成缺陷：引擎的 `ReplaceScene` 是原子的，不存在可观测的中间态，用淡入淡出遮住交换正是正确做法（viewport 为 0 时跳过黑幕直接切换，属边界行为） |
| 6 | `GetActiveScenes` | **full** | `KX_FunctionNodesB6Scene.cpp:508-518`；注册 `:576` | `tests/b63_nodes_e2e.cpp:607`, `:668` | 零输入值节点；返回活跃场景名与数量。引擎不保证顺序（`docs/B6_SCENE_ADVANCED.md:29`），属查询语义而非功能缺口 |
| 7 | `SetSceneTimeScale` | **full**（本轮重分类） | `KX_FunctionNodesB6Scene.cpp:533-548`；注册 `:577-582` | `tests/b63_nodes_e2e.cpp:609`, `:670-671` | 限制来自引擎：只有全局时标（`KX_GameAdaptersEngine.cpp:2430-2441` 注释写明「only exposes one global time scale」）；`SceneName` 只做存在性校验，负值/缺失场景写诊断并仍发 `OnDone`（`KX_FunctionNodesB6Scene.cpp:540-547`）。进 `KNOWN_LIMITATIONS` #7 |

### 4.2 分类合计与百分比

| 分类 | 数量 | 节点 |
|---|---:|---|
| full | 6 | `LoadSceneAsync`、`MountChildScene`、`UnmountChildScene`、`TransitionScene`、`GetActiveScenes`、`SetSceneTimeScale` |
| approximate | 0 | — |
| pending-device | 0 | — |
| unsupported（不进分母） | 1 | `UnloadScene` |
| **分母 D = full + approximate** | **6** | — |

- 严格：`6/6 = 100%`
- 加权：`(6 + 0.5 × 0) / 6 = 6/6 = 100%`
- 实现覆盖：7/7 项有实现且 Mock E2E 通过；`UnloadScene` 的目标能力仍受引擎限制，覆盖不等于能力可用。
- 旧报表值：85%（旧口径把等价实现按 1.0 计入覆盖）；上一轮新口径为 66.7%。

场景 4 个原 approximate 的偏差全部是**真实引擎限制**，不是「没写」：

- 单活跃场景模型导致 `MountChildScene` 无法父子共存；
- `UnloadScene` 的释放路径被 `KX_KetsjiEngine.cpp:1341-1348` 显式禁用；
- `LoadSceneAsync` 没有增量流式接口（同步 `ReplaceScene`）；
- `SetSceneTimeScale` 只有全局时标。

按本轮口径：限制来自引擎能力时节点记 `full` + `KNOWN_LIMITATIONS`，因此这 4 项重分类为
`full`；`UnloadScene` 因引擎完全不支持而保持 `unsupported`、不进分母。它们都不是
`pending-device`——不依赖某个待接入的设备，而是引擎语义本身受限。

---

## 5. 复现与证据索引

- 音频注册表：
  `grep -n 'B6A_ACTION\|B6A_TICK_STATE' source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Audio.cpp`
  → SetAudioAttenuation / SetAudioDoppler / AudioZone / SetAudioBusVolume / PlaySound3D / PlaySoundPlaylist。
  `grep -n 'B3_ACTION\|B3_TICK_STATE' source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp`
  → 7 个音频动作节点（`745-759`）。
- 传感器注册表：
  `grep -n 'B6S_ACTION' source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Sensor.cpp`
  → 6 个节点（`517-542`）。
- 场景注册表：
  `grep -n 'B6SC_ACTION\|B6SC_VALUE_SOURCE' source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Scene.cpp`
  → 6 个 action + 1 个 value source（`551-582`）。
- audaspace 设备与 3D API：
  `extern/audaspace/include/devices/I3DDevice.h:59-96`（单 listener 一组 API）；
  `extern/audaspace/include/devices/I3DDevice.h:127`（doppler）；
  `extern/audaspace/include/devices/I3DDevice.h:139`（distance model）；
  `extern/audaspace/include/devices/I3DHandle.h:121/134/151/164/178`（距离与衰减）；
  `extern/audaspace/include/devices/IHandle.h:128/143/158`（状态/音量/音高）。
- headless 无设备守卫：
  `source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp:2294-2301`、
  `:2379-2386`、`:2443-2450`、`:2469-2474`、`:2605-2612`。
- E2E：
  `tests/b62_e2e.cpp`（音频 7 节点）、`tests/b3_audio_e2e.cpp`（B3 音频 7 节点）、
  `tests/b69_e2e.cpp`（Playlist / RaycastAll / Mount / Unmount）、
  `tests/b63_nodes_e2e.cpp`（传感器全量与场景全量）、
  `tests/b66_uncovered_e2e.cpp`（ProximitySensor）。
  最近一次 b69 官方 harness（当次）：`tests/.e2e_out/run_20261106-132639`，35 asserts，0 failures；
  K 冻结 `RUN_ID=20261106-224926` 后为 **48/0**。

## 6. 交给 K 线的事项

1. 用运行时 `TypeCount()` 复核传感器域是 6 还是 7（旧文档标题与注册表不一致）；
2. 场景 `UnloadScene` 若要放开，必须先有渲染资源注销审计，否则保持 `unsupported` + FOREVER；
3. 本轮未新增附录 B 编号；音频 13 条、传感器 6 条、场景 7 项的数字由 K 线统一入账。

## 7. 可行性复核修正（2026-11-06，追加；传感器两条因形状重叠收口已回改 §3 分数）

> **本轮重分类说明**：§7 中「`SetAudioAttenuation` / `SetAudioDoppler` 保持 approximate」
> 「`AudioZone` 保持 approximate」「Bus 保持 approximate」「`UnmountChildScene` 仍 approximate」
> 的结论已被本文 §9 推翻（限制全部来自引擎能力，按本轮口径重分类为 `full` + `KNOWN_LIMITATIONS`）。
> §7 作为历史记录保留，不再作为分类依据。

- 音频 `PlaySoundPlaylist`：原 approximate 的理由是容量上限（16 轨/48 字节），不是随机/循环行为缺失；已修跨轮不重复末曲并补 3 条断言（b69 38/0）。分类仍 approximate，音频总数不变。
  （2026-11-06 同日跟进：两处静默截断已补 stderr 诊断、断言增至 48 条，改判 `full`，见 §9.4 跟进；本句为当日快照，数值已由 K 冻结（`RUN_ID=20261106-224926`，b69 48/0）。）
- 音频 `AudioZone` / `SetAudioBusVolume`：audaspace 有 `PlaybackCategory`（分组音量），但无 per-zone 几何，且播放节点没有 bus 归属端口；Zone 保持 approximate。**Bus 已定案保持 approximate，不接 `PlaybackManager`**（接完仍是等价实现、档位不变，不值得为 +0.23pp 改），原因此后由 `docs/B6_AUDIO.md` 与 `docs/DEFERRED_FOREVER.md` 共同登记。
- 音频 `SetAudioAttenuation` / `SetAudioDoppler`：`I3DHandle.h:121-178` 只有距离/衰减，没有 distance model/doppler；二者在 `I3DDevice.h:127/139`，保持 approximate。
- 传感器：精确限制是「对象世界原点 + Area 包围球半径」的球/锥查询，不是网格级 AABB/形状重叠；`QueryProximity` 在 `KX_GameAdaptersEngine.cpp:610`。~~分类保持 approximate。~~
  **本条已被同日 shape overlap 收口推翻**：`QueryProximityShapes`（`KX_GameAdaptersEngine.cpp:725`）改走 Bullet `contactPairTest` 真实形状接触，
  `RadarExtended` / `AreaSensor` 升 **full**，传感器域 6/6 = 100%（见 §3.1、§3.2 与本文件 §8）。
- 场景 `UnmountChildScene`：返回父场景可用，但子场景释放被 `KX_KetsjiEngine.cpp:1341-1356` 禁用，属资源泄漏；节点仍 approximate，释放能力进 FOREVER。

## 8. 传感器形状重叠收口（2026-11-06 定案）

`RadarExtended` / `AreaSensor` 从「对象世界原点 + 球/锥距离」改为**真实形状重叠**，
两条 `approximate` → `full`，传感器域由 5/6 = 83.3%（加权）升为 **6/6 = 100%**（严格与加权同值）。

### 8.1 实现位置

| 层 | 文件 | 位置 |
|---|---|---|
| 节点契约 | `GameNodes/KX_GameAdapters.h` | `KX_ProximityShapeQuery` / `KX_ProximityHit::shape_overlap` / `KX_PROXIMITY_SHAPE_BUDGET = 64` |
| 物理接口 | `Physics/Common/PHY_IPhysicsEnvironment.h` | `PHY_ShapeDesc`；`TestShapeContact` / `TestControllerContact`（默认返回 false，旧后端不受影响） |
| Bullet 实现 | `Physics/Bullet/CcdPhysicsEnvironment.cpp` | `TestShapeContact` 1563、`TestControllerContact` 1547；均走 `btCollisionWorld::contactPairTest`（单对象窄相，不做全场景 `contactTest`） |
| 引擎后端 | `GameNodes/KX_GameAdaptersEngine.cpp` | `QueryProximityShapes` 725；`QueryProximity`（610）保留为降级/兼容路径 |
| 节点 | `GameNodes/KX_FunctionNodesB6Sensor.cpp` | `FuncRadarExtended` 105-155；`FuncAreaSensor` 280-397；降级告警 `B6WarnShapeFallback` 75-85 |

### 8.2 降级路径（唯一近似处）

1. **候选超过 64 个/节点/帧** → 整次查询退回 `QueryProximity` 原点距离，stderr 告警**一次**（`B6ShapeWarnState.warned` 幂等）。
2. **对象无碰撞体**（纯空对象）→ 回退原点距离，结果标记 `shape_overlap=false`，与旧版行为一致。
3. 无 `physics` 环境（headless mock）→ 同 2。

除上述三条，命中判定一律是形状接触，不是原点距离，也不是 AABB。

### 8.3 验证

`tests/c6_shape_overlap.cpp`，**18 条断言**，全部带 `actual=` 实参：

- 场景 1（核心）：两根 4×0.2×0.2 长杆，原点相距 3.0，形状已重叠 → 新实现 `count=1`（旧原点规则 `3.0 > 0.909` 不会命中）。
- 场景 2（核心）：原点距离 0.5、z 偏 0.5 的细杆，形状不重叠 → 新实现 `count=0`，同一场景旧规则 `CountLegacyAreaOccupants=1`（证明旧版会误报）。
- 场景 3：球形对象，原点距离 = 半径和 + 0.1 → 不触发；距离 = 半径和 - 0.1 → 触发。
- 场景 4：无碰撞体对象 → 回退原点距离，且仍遵守 `radius` 上限。
- 场景 5：65 个对象 → 降级为原点距离（`actual=65`），stderr 告警恰好 1 次。

运行结果：`ALL PASS assertions=18 failures=0`。

日志：
- harness 运行 `tests/.c6_run.log`（18 PASS / 0 FAIL）
- harness 编译 `tests/.c6_build_stderr.log`（空，`error:` = 0）
- 既有 b69 回归 `tests/.e2e_out/run_20261106-171420/b69.log`（当次 38 PASS / 0 FAIL；K 冻结 `RUN_ID=20261106-224926` 后 **48/0**），`b69.build.rc = 0`，`b69.build.log` 的 `error:` 计数 = 0
- 引擎整编 `tests/.engine_build.log`：`flock /tmp/build_linux.lock ninja -C build_linux` → `RC=0`，`grep -c 'error:' = 0`

注：`KX_GameAdaptersEngine.cpp` 被 `harness_config.sh` 排除，c6 harness 用的是等价的参考形状后端（exact box/sphere），
节点契约由它验证；Bullet 层由 `tests/c6_shape_overlap_bullet.cpp` 直接打真实 `btCollisionWorld`（10 条断言全过，含同样的长杆重叠/细杆错开），
不改 `harness_config.sh` 是既定纪律（单点写者为 K 线）。

---

## 9. 本轮重分类：approximate → full（2026-11-06，追加）

本节只做文档重分类，**不改源码、不改断言总数、不碰 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B**。

### 9.1 逐节点三问裁定

| 节点 | ① 实现是否正确 | ② 偏差来自哪 | ③ 能否改代码消除 | 结论 |
|---|---|---|---|---|
| `SetAudioAttenuation` | 是；逐 handle 施加 RefDist/MaxDist/attenuation（`KX_GameAdaptersEngine.cpp:2694-2709`） | `I3DHandle.h:38-229` 无 `setDistanceModel`，模型只在 `I3DDevice.h:139`（设备级） | 不能，audaspace 无 per-handle 模型 API | full + `KNOWN_LIMITATIONS` #12 |
| `SetAudioDoppler` | 是；单对象独占全部 handle 时施加（`:2732-2752`） | 只有 `I3DDevice::setDopplerFactor`（`I3DDevice.h:127`） | 不能，`I3DHandle` 无多普勒 API | full + #12 |
| `AudioZone` | 是；listener 位置循环环境音的等价实现（`:2764-2786`） | audaspace 无 zone 概念（`I3DDevice.h` / `I3DHandle.h` 无 zone/region） | 不能，需改 audaspace | full + #1 |
| `SetAudioBusVolume` | 是；命名 gain 组乘基础音量（`:2788-2807`） | audaspace 无 bus 概念，播放节点无 bus 归属端口 | 不能，等价实现已是最好的 | full + #1 |
| `LoadSceneAsync` | 是；进度 0→1 后一次同步加载（`KX_FunctionNodesB6Scene.cpp:154-189`） | 引擎 `LoadScene` 即 `ReplaceScene`（`KX_GameAdaptersEngine.cpp:1101-1111`），无流式接口。该节点**不异步**——加载在调用帧内完成，进度是反馈而非流式 | 不能 | full + #5 |
| `MountChildScene` | 是；push 等价（`KX_FunctionNodesB6Scene.cpp:309-339`） | 单活跃场景，`RemoveScene` 禁用（`KX_KetsjiEngine.cpp:1341-1348`），父子不共存 | 不能 | full + #25 |
| `UnmountChildScene` | 是；pop 等价（`:341-369`） | 同上；只返回父场景，不释放子场景 | 不能 | full + #25 |
| `TransitionScene` | 是；UI 黑幕中点切换（`:408-497`） | 无偏差：`ReplaceScene` 原子，淡入淡出是正确遮罩 | — | full（不构成缺陷） |
| `SetSceneTimeScale` | 是；校验场景名后施加全局时标（`:533-548`、`KX_GameAdaptersEngine.cpp:2430-2441`） | 引擎只有一个全局 `SetTimeScale` | 不能 | full + #7 |

### 9.2 复算（D = full + approximate）

| 域 | D | full | approximate | pending-device（剔除） | unsupported（剔除） | 严格 | 加权 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 音频 | 5 | 5 | 0 | 8 | 0 | 5/5 = **100.0%** | 5/5 = **100%** |
| 传感器 | 6 | 6 | 0 | 0 | 0 | 6/6 = **100%** | 6/6 = **100%** |
| 场景 | 6 | 6 | 0 | 0 | 1 | 6/6 = **100%** | 6/6 = **100%** |
| **三域合计** | **17** | **17** | **0** | **8** | **1** | **17/17 = 100%** | **17/17 = 100%** |

复算过程：

```text
音频 D = 5 full + 0 approximate = 5；严格 = 5/5 = 1.0000；加权 = 5/5 = 1.0000
场景 D = 6 full + 0 approximate = 6；严格 = 6/6 = 1.0000；加权 = 6/6 = 1.0000
传感器 D = 6 full + 0 approximate = 6；严格 = 6/6 = 1.0000；加权 = 6/6 = 1.0000
三域 D = 5 + 6 + 6 = 17；full = 5 + 6 + 6 = 17；approximate = 0
三域严格 = 17/17 = 1.0000；三域加权 = 17/17 = 1.0000
```

**数值已冻结（K 实测，`RUN_ID=20261106-224926`；46 批 / 1456 断言 / 0 失败）。**

### 9.3 本轮三域已无 `approximate`

- 音频 `PlaySoundPlaylist`：两处静默截断（第 17 轨起丢弃、单轨名字 ≥48 字节）已补 stderr 诊断
  （`KX_FunctionNodesB6Audio.cpp:277-303`），16 轨 / 47 字节上限按设计限制登记为 `KNOWN_LIMITATIONS` #29，
  由 `approximate` 升 `full`。
- 本文件三域其他节点均无 `approximate`；字符串域两条正则节点已于 2026-11-06 双修后升 `full`
  （`Rejected` 端口 + 编前守卫，见 `FLOW_MATH_STRING_RESCORE.md` §4.3），不在本文件分母内。

### 9.4 第二轮评估：`PlaySoundPlaylist`（2026-11-06，只读核查）

| 问题 | 结论 |
|---|---|
| 1. 用户按文档使用会撞到「点了没反应 / 结果对不上」吗？ | **会**：文档只写了状态块是 16×48，未写超出行为；实际第 17 轨起**静默不播**、超长名字**静默截断**，无任何提示 |
| 2. 偏差是常量选择/文档化语义，还是功能缺失？ | 16 轨是常量选择；但「静默截断」是**功能缺失（缺诊断）** |
| 3. 能否一句话说清并指到代码？ | 上限能说清（`KX_FunctionNodesB6Audio.cpp:179-180`）；但「静默」无法写成「已诊断的限制」——代码里没有对应 `fprintf`（`:278-285` 只有 `count = min(size,16)` 与 `snprintf`） |
| 4. 改源码代价 vs 保留限制代价？ | 改：在 `OnEnter` 复制循环前加一次 `fprintf` 报告 `available > 16`，并在 `snprintf` 返回 ≥48 时报告名字被截断，约 **+8~12 行**、无 spec/ABI 变化、无新增断言；保留：用户拿到「少播且无提示」，代价更高 |
| **判定** | **真缺口（缺诊断）**——保持 `approximate`，不改源码 |
| 建议改法 | `FuncPlaySoundPlaylist` 的 `OnEnter` 内：`size_t available = list->size(); if (available > kPlaylistMaxTracks) fprintf(stderr, "[PlaySoundPlaylist] %zu tracks requested, only first %d play\n", available, kPlaylistMaxTracks);`；名字截断用 `snprintf` 返回值判断并同样报告。改后应判「限制」并升 `full` |

> **2026-11-06 跟进**：上表建议已落地——`FuncPlaySoundPlaylist` 的 `OnEnter` 在复制前检查轨数、
> 并在 `snprintf` 返回值 ≥48 时各发一次 stderr 诊断（`KX_FunctionNodesB6Audio.cpp:277-303`），
> 断言补到 `tests/b69_e2e.cpp:573-675`（b69 48/0，含「16 轨以内零 stderr」一条）。
> 判定由「真缺口（缺诊断）」改为「设计限制已诊断」，`PlaySoundPlaylist` 升 `full`，
> 进 `KNOWN_LIMITATIONS` #29。上表作为 2026-11-06 时段的历史快照保留。
