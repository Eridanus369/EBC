# 音频 / 传感器 / 场景 可行性排查（2026-11-06）

本文只回答「能不能低成本补、补不掉写准」，不改任何既有总数，不改附录 B 编号。
分类口径仍按 `AUDIO_SENSOR_SCENE_RESCORE.md` 顶部的 full / approximate / pending-device / unsupported。

## 0. 结论速览

| 条目 | 判定 | 本轮动作 | 若要做：做法 / 工期 | 关键证据 |
|---|---|---|---|---|
| `PlaySoundPlaylist` | **full**（2026-11-06 改判；容量上限已补 stderr 诊断） | 已修随机跨轮边界并补断言 | 0（已做）；行为缺口已消除 | `KX_FunctionNodesB6Audio.cpp:179-180`（16 轨/48 字节）；`tests/b69_e2e.cpp` **48/0**（K 冻结 `RUN_ID=20261106-224926`）；见 `KNOWN_LIMITATIONS.md` #29 |
| `AudioZone` | **保持 approximate** | 无 | 无原生 zone API，见 §1.2 | `PlaybackCategory.h:74-104` 只有分组音量，没有区域几何 |
| `SetAudioBusVolume` | **可修，待拍板** | 无 | 接 `PlaybackManager`/`PlaybackCategory` + 播放节点 Bus 字段，2~3 天 | `PlaybackManager.h:65/72/79/87/122`；`KX_GameAdaptersEngine.cpp:2701/2728/2746-2747` |
| `SetAudioAttenuation` | **保持 approximate** | 无 | 无 per-handle distance model；不做 | `I3DHandle.h:121-178`；`I3DDevice.h:139` |
| `SetAudioDoppler` | **保持 approximate** | 无 | 无 per-handle doppler；不做 | `I3DDevice.h:127`；`I3DHandle.h` 无 doppler |
| `RadarExtended` | **保持 approximate** | 无 | 可修但属物理接口改造（shape cast），2~3 天 | `KX_GameAdaptersEngine.cpp:610`；`KX_FunctionNodesB6Sensor.cpp:96-129` |
| `AreaSensor` | **保持 approximate** | 无 | 同上 | `KX_FunctionNodesB6Sensor.cpp:271-300`；`B6_SENSOR.md:70` |
| `UnmountChildScene` | 节点 **approximate**；子场景释放 **FOREVER** | 无 | 释放需重新启用 `RemoveScene`，2~4 天 + 渲染资源审计 | `KX_KetsjiEngine.cpp:1341-1356`、`:1362-1374` |
| `MountChildScene` | **保持 approximate**；共存是拍板项 | 只写 §3.2 说明，不动手 | 实验性共存 3~5 天；安全共存+卸载 1~2 周 | `KX_KetsjiEngine.cpp:1106-1122`；`:896`；`:816` |
| `LoadSceneAsync` | **保持 approximate（非流式）** | 无 | 引擎无真实场景加载进度；真实进度需引擎回调，3~5 天 | `KX_KetsjiEngine.cpp:592`；场景转换同步 |
| `SetSceneTimeScale` | **可修，待拍板**（引擎级） | 无 | 加 `KX_Scene` 时标 + tick 按场景乘 dt，2~3 天 | `KX_KetsjiEngine.cpp:140/477/1464/1469`；`KX_Scene.h` 无时标 |
| 多活跃场景 | **拍板项，不动手** | 只写 §3.4 | 见 §3.4；风险高 | `KX_KetsjiEngine.cpp:1106-1109`、`:816`、`:292/342/758/1109/1616` |

---

## 1. 音频

### 1.1 `PlaySoundPlaylist`：为什么仍是 approximate，已修什么

- **为什么不是 full**：节点有书面容量上限——16 轨、名字 48 字节，超出截断
  （[KX_FunctionNodesB6Audio.cpp:179-180](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Audio.cpp#L179-L180)）；
  句柄经 32 位端口，超过 `INT32_MAX` 输出 0（同文件 `:236-242`）。
  这些是固定的实现边界，不是「随机/循环行为没做」。
- **行为缺口已修**：随机模式跨轮时，上一轮最后一首可能成为下一轮第一首。
  现在在重洗后强制首曲不等于上一轮末曲（`KX_FunctionNodesB6Audio.cpp` 的
  `FuncPlaySoundPlaylistTick` 边界分支；`state->count > 1` 时交换 `order[0]/order[1]`）。
- **新增断言（3 条，已通过）**：
  1. 循环模式 7 次请求恰好 2 次 `OnDone`（每轮边界一次）；
  2. 2 轨随机、seed=256、3 次请求的序列为 `two, one, two`，跨轮不重复末曲；
  3. 原有顺序/循环/固定种子/排列断言保持通过。
- **验证**：`tests/run_all_e2e.sh --only '^b69$'` → `b69 38 0`（当次）；
  日志 `tests/.e2e_out/run_20261106-160220/b69.log`（ALL PASS）。K 冻结 `RUN_ID=20261106-224926` 后为 **48/0**（见 §4 与 `NUMBER_CONSISTENCY_AUDIT.md` B.5）。
- **判定**：按 rubric 的字面「有书面明确限制 → approximate」，保持 approximate。
  若团队把「最多 16 轨」视为产品容量边界、不算语义近似，可改 full；但本轮不改总数。
  行为层面已经补到可断言，剩下的只是容量。

### 1.2 `AudioZone`：audaspace 到底有没有 per-zone API

- 结论：**没有**。
- audaspace 的 `PlaybackCategory` 是「按名字分组 + 组音量」
  （[PlaybackCategory.h:74-104](../extern/audaspace/include/fx/PlaybackCategory.h#L74-L104)），
  没有区域几何、进入/离开事件、空间衰减区、优先级/遮挡。
- 现实现是「listener 位置的循环环境音」，重复注册替换旧环境音
  （[KX_GameAdaptersEngine.cpp:2486-2508](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2486-L2508)）。
  这是最接近的可听行为，但不是 zone。
- **判定**：保持 approximate，限制原文写准：
  > AudioZone 不提供区域几何/进入离开/遮挡；它把命名环境音挂在 listener 位置循环播放，
  > 以名字去重，后注册者替换先注册者。位置随 listener 变化，没有区域边界。
- 不接 `PlaybackCategory` 的原因：接了也只是给这个环境音加一个组音量，不会变成 zone，
  反而多一层音量耦合。

### 1.3 `SetAudioBusVolume`：`PlaybackManager` / `PlaybackCategory` 存在，但没接入

**查证结果：audaspace 确实有命名分类（category）API，且已编译。**

- `PlaybackManager(std::shared_ptr<IDevice>)`（`PlaybackManager.h:65`）
- `addCategory(float volume)`（`:79`）/ `addCategory(std::shared_ptr<PlaybackCategory>)`（`:72`）
- `play(std::shared_ptr<ISound>, unsigned int catKey)`（`:87`）
- `setVolume(float, unsigned int catKey)`（`:122`）
- `clean()` / `clean(catKey)`（`:137` / `:147`）
- C 绑定也在（`extern/audaspace/CMakeLists.txt:385` 一带）。

**为什么现在还不能「接上就 full」：**

1. 引擎没有接：`grep -rn 'PlaybackManager|PlaybackCategory' source/` 0 命中；
   当前播放走 `DeviceManager::getDevice()->play()`。
2. 播放节点没有 bus 归属端口。`StartHandle` 把每个句柄都写死为 `"master"`
   （[KX_GameAdaptersEngine.cpp:2701](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2701)、`:2728`），
   `SetAudioBus` 只对已有句柄按 `handle_bus_` 乘增益（`:2574-2585`）；
   没有任何路径能把一个声音放进 `"sfx"` 之类的具名 bus。
   所以现实现的具名 bus 是**注册了但无成员**，只有 `master` 实际生效。
3. `AUD_Sound` 是 `std::shared_ptr<aud::ISound>`
   （[BKE_sound_types.hh:25](../source/blender/blenkernel/BKE_sound_types.hh#L25)），
   `StartHandle` 已经拿到这个指针，接 `PlaybackManager::play` 技术上可行；
   但需要同时处理句柄所有权、`clean()`、停止/释放，以及与 3D handle 的叠加。

**可修方案（需拍板）：**

- 给 `StartSound` / `PlaySoundByName` / `PlaySound3D` / `PlaySoundPlaylist` 增加 Bus 字段
  （默认 `"master"`，编辑器 metadata 同步）；
- `EngineAudioAdapter` 持有一个 `PlaybackManager`，每个 bus 一个 category；
  `StartHandle(sound, bus, ...)` 改为 `manager->play(sound, key)`；
- `SetAudioBus` 改为 `manager->setVolume(gain, key)`；
- 断言：mock 层记录 `bus` 与 `category_gain`；`master` 默认；未知 bus 为 no-op；
  引擎层在无设备环境只能验句柄/分类创建，真实音听仍单列 pending-device。

- **工期**：2~3 天（含 4 个节点字段、编辑器表、mock 断言）。
- **风险**：中。`PlaybackManager` 与直接 `DeviceManager` 播放混用会重复播放/泄漏句柄；
  `clean()` 时机错误会丢正在播放的声音。`KX_GameAdaptersEngine.cpp:2294-2612` 可能被输入线
  同时改，必须改前重读、只动音频段。
- **判定**：当前保持 approximate；如果拍板要做，按上面方案升 full（真实 bus 路由）。

### 1.4 `SetAudioAttenuation` / `SetAudioDoppler`：handle 级 API 不存在

- `I3DHandle.h` 只有距离参数与衰减：
  `setVolumeMaximum:121`、`setVolumeMinimum:134`、`setDistanceMaximum:151`、
  `setDistanceReference:164`、`setAttenuation:178`；
  **没有 `setDistanceModel`，也没有 `setDopplerFactor`**。
- 这两个都在设备级：
  `I3DDevice::setDopplerFactor`（`I3DDevice.h:127`）、
  `setDistanceModel`（`I3DDevice.h:139`）。
- 现实现的对象级限制是真实且已写明的：
  - attenuation 的对象级 `Model` 不能按对象隔离，只逐 handle 施加 RefDist/MaxDist
    （`KX_GameAdaptersEngine.cpp:2408-2415`、`:2428-2430`）；
  - doppler 的对象级因子只在「该对象拥有全部 handle」时应用，否则只存不施加
    （`:2456-2466`）。
- 能否用「每帧算距离再 `setVolume`」模拟 per-handle distance model？
  技术上可以，但会与设备自身的距离衰减重复计算，并且要自己维护 listener/源位置与速度；
  不是低成本修正，且会让真实设备与 mock 行为分叉。**不做**。
- **判定**：保持 approximate，限制原文与上面一致。

---

## 2. 传感器

### 2.1 现状不是「AABB」，而是「对象原点 + 球/锥」

- `RadarExtended` 调 `ctx.SceneQuery().QueryProximity(...)`
  （[KX_FunctionNodesB6Sensor.cpp:96-129](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Sensor.cpp#L96-L129)），
  传 `cone_angle_cos`，按距离/角度过滤属性。
- `AreaSensor` 先用自己的半尺寸算包围球半径
  （`sqrt(half_x²+half_y²+half_z²) * 1.05`，同文件 `:271-300`），再调 `QueryProximity`。
- `QueryProximity` 的实现
  （[KX_GameAdaptersEngine.cpp:610](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L610)）
  是逐对象**世界原点**到 `origin` 的距离 + 锥角点积；不读对象包围体、不读网格、不做形状重叠。
- 因此精确的限制是：
  > 球/锥查询以对象世界原点为准；Area 的半径是本节点用半尺寸估的包围球。
  > 对象原点在区域外但几何体压进区域时会漏检；不做网格级/形状级重叠，也不做遮挡。
  这比旧文档的「AABB 近似」更准确；旧措辞见 `docs/B6_SENSOR.md:70`，本文不改旧文档原文。

### 2.2 有没有 shape cast / contactTest 可用

- `KX_PhysicsAdapter` 只有 `Raycast` / `RaycastAll`（本轮新增后者），没有 shape overlap。
- `KX_SceneAdapter::OverlapQuery`（`KX_GameAdapters.h:387`）是「屏幕射线点选后的一个点/小阈值」，
  不是球/锥/胶囊。
- `grep -rn 'contactTest|convexSweepTest' source/gameengine` 无命中；Bullet 的
  `btCollisionWorld::contactTest` / `convexSweepTest` 没有被引擎暴露。
- **可修方案**：新增 `KX_PhysicsAdapter::OverlapShape(shape, pose, mask, results)`，
  在 `CcdPhysicsEnvironment` 里对 `btCollisionWorld` 做 `contactTest`（球/胶囊用临时
  `btSphereShape`/`btCapsuleShape`，锥用近似凸包）；节点改用该接口。
- **工期**：2~3 天（接口 + Bullet 实现 + headless Bullet 测试；Bullet 不需要显示设备）。
- **风险**：中。碰撞过滤/去重/性能语义要与现有 Area/Radar 对齐；会动物理接口。
- **判定**：当前保持 approximate；无「拿来的」shape cast API，不属于低成本修正。

---

## 3. 场景

### 3.1 `UnmountChildScene`：返回父场景可做，释放子场景做不了

- 现实现是 `LoadScene(parent)` 返回父场景，子场景仍留在 `m_scenes`，**不释放**
  （`KX_FunctionNodesB6Scene.cpp:341-369`）。
- 引擎的 `RemoveScene` 是**硬禁用**：
  `KX_KetsjiEngine.cpp:1341-1356` 在不满足条件时直接 `return`，不会把场景名排进删除队列；
  `RemoveScheduledScenes` 的删除体（`:1362-1374`）本身可用，但永远收不到条目。
- 所以「引用计数/延迟释放」在节点层无解：节点没有可调用的删除入口；
  就算在节点里记引用计数，也没有 API 把计数归零变成实际释放。
- 真要修：重新启用 `RemoveScene`（或新增专用 `RemoveAddedScene`），
  并审计 EEVEE 的 GPU viewport/资源注销；日志注释说明该路径在 EEVEE 集成期被临时关闭。
- **工期**：2~4 天 + 渲染资源审计；**风险高**（悬空 viewport/GPU 资源）。
- **判定**：节点保持 approximate（返回父场景确实工作）；**子场景释放记 FOREVER**，
  理由是可核的引擎禁用路径，不是「我们没做」。

### 3.2 `MountChildScene` / 多活跃场景：不是「重写单活跃模型」，但风险仍在

- 引擎的 `m_scenes` 是场景列表，`AddScene` 存在并明确支持并行场景：
  注释写「able to switch between scenes and have several scenes running in parallel」
  （`KX_KetsjiEngine.cpp:1106-1109`），`AddScene` 在 `:1122`。
- 当前节点走的是 `EngineSceneAdapter::LoadScene` → `engine->ReplaceScene(...)`
  （`KX_GameAdaptersEngine.cpp:896`），所以是**节点/adapter 选择替换**，不是引擎只能有一个场景。
- 但「共存」不是把 `ReplaceScene` 换成 `AddScene` 就完事，风险点：
  1. **front scene 假设**：引擎多处用 `m_scenes->GetFront()` 决定 viewport/相机/逻辑
     （`KX_KetsjiEngine.cpp:292/342/758/816/1109/1616`），多场景时谁当 front、如何合成要重新定义；
  2. **输入/UI 重复**：多个场景共享输入设备与 canvas，按键/触摸/UI 可能被两个场景各消费一次；
  3. **卸载**：子场景无法释放（见 §3.1），共存会放大泄漏；
  4. **渲染顺序/相机**：每个场景有自己的相机，合成顺序、深度、后处理需要明确；
  5. **物理/音频**：每个场景自己的物理世界没问题，但音频 listener 只有一个，
     两个场景的 3D 声音会共享同一 listener。
- **改什么**：
  - `KX_SceneControlAdapter` 增加 `AddScene(name)`（`KX_KetsjiEngine::CreateScene` + `AddScene`）；
  - `MountChildScene` 改为 AddScene，并维护父/子栈；
  - `UnmountChildScene` 需要移除子场景（卡在 §3.1）；
  - 明确 front scene 规则与输入/UI 路由。
- **范围/工期**：实验性 additive mount 3~5 天；安全共存 + 卸载 1~2 周。
- **判定**：按指令**不动手**；这是拍板项，不是本轮可修的低成本项。

### 3.3 `LoadSceneAsync`：没有真实加载进度

- 场景加载走同步 `ConvertScene`；`KX_LibLoadStatus` 的进度只覆盖 **lib 加载**
  （`KX_KetsjiEngine.cpp:592` 的 `FinalizeAsyncLoads`），不是场景资产流式进度。
- 现节点的 progress 是节点侧 0.5 秒定时反馈（`KX_FunctionNodesB6Scene.cpp:171-174`），
  到点整体 `ReplaceScene`。
- **判定**：保持 approximate，限制原文写准：
  > 非流式。progress 是节点侧的定时反馈，不是引擎真实加载百分比；
  > 引擎在进度到 1 时一次性 ReplaceScene，没有增量上传/分帧实例化。
- 若要做真实进度：需要给 `KX_BlenderSceneConverter`/场景转换加进度回调，3~5 天，属引擎级。

### 3.4 `SetSceneTimeScale`：只有全局时标

- 引擎只有 `m_timescale`（`KX_KetsjiEngine.cpp:140` 初始化、`:477` 参与 `framestep`、
  `:1464/1469` get/set）；`KX_Scene` 没有 time scale 字段。
- 现节点对命名场景缩放会拒绝并写诊断（`KX_FunctionNodesB6Scene.cpp:540-545`），仍发 `OnDone`。
- **可修方案**（引擎级）：给 `KX_Scene` 加 `m_time_scale`，`EngineNextFrame` 的逐场景
  `RenderLogic` 用 `dt * scene_scale`；物理/动画/音频分别确认是否用同一个 dt。
- **工期**：2~3 天；**风险**：中高（物理步进、音频时钟、动画时钟可能分叉）。
- **判定**：当前保持 approximate；拍板后再动引擎，不在本轮做。

---

## 4. 本轮实际改动与验证

- 改了 `KX_FunctionNodesB6Audio.cpp`：随机播放列表跨轮不重复末曲。
- 改了 `tests/b69_e2e.cpp`：`RunPlaylist` 支持自定义曲目表与回传 `OnDone` 计数；
  新增 3 条断言（循环边界 `OnDone`、随机跨轮不重复、原断言保持）。
- 验证：`tests/run_all_e2e.sh --only '^b69$'` → **38 asserts / 0 failures**（当次；
  `tests/.e2e_out/run_20261106-160220/b69.log`）。K 冻结 `RUN_ID=20261106-224926` 后为 **48/0**。
  首次编译因 `AddString` 只接 `const char *` 失败，已改为 `sounds.c_str()` 后重跑；
  失败那次脚本复用了旧二进制，未把错误计入 failure，这点需注意。
- 未改 `KX_GameAdaptersEngine.cpp:2294-2612`；未改附录 B 编号；未改任何总数；
  未动 `build_linux`；未 commit/push；未用 shim / `HARNESS_DROP_TU` / `/tmp` 补丁头文件。
- 本轮没有跑 `ninja`：E2E harness 直接编译 TU，不经过 `build_linux`。
  若后续要跑，命令一律 `flock /tmp/build_linux.lock ninja -C build_linux`。

### 4.1 发现：`run_all_e2e.sh` 可能用旧二进制「假绿」（未改共享脚本）

- `build_one()` 在后台执行，返回码没有被收集；构建失败判定只看
  `[ -x "$BINDIR/$name" ]`（`tests/run_all_e2e.sh:153-205`）。
- 只要上一次运行留下了同名二进制，本次编译失败仍会 `build_failures=0`，
  并直接运行旧二进制。
- 本轮首次复现：`b69_e2e.cpp` 因 `AddString` 只接 `const char *` 编译失败，
  脚本仍报 `35 asserts / 0 failures`，而 `b69.build.log` 里明确有 error；
  改为 `sounds.c_str()` 后才真正得到 `38 asserts / 0 failures`。
- 未改共享脚本（遵守只追加纪律）。建议修法二选一：
  1. 构建前 `rm -f "$out"`，失败后没有可执行文件，`build_failures` 自然会计数；
  2. 把 `build_one` 的返回码写进数组，最终与 `-x` 检查取并集。
- 在修好之前，任何 E2E 报数都必须同时检查 `$OUTDIR/<name>.build.log` 是否含 `error:`。

## 5. 待拍板清单（按收益/成本排序）

1. **音频真 bus**（2~3 天，中风险）：`PlaybackManager` 已存在，缺的是 Bus 归属字段与接线。
2. **传感器 shape overlap**（2~3 天，中风险）：需新增物理 shape cast 接口，headless Bullet 可测。
3. **子场景释放**（2~4 天 + 渲染审计，高风险）：重新启用 `RemoveScene`，先解决 EEVEE 资源注销。
4. **按场景时标**（2~3 天，中高风险）：引擎级，先确认物理/音频时钟语义。
5. **多活跃场景共存**（3~5 天实验 / 1~2 周安全，高风险）：需先定 front scene、输入/UI 路由、卸载。
