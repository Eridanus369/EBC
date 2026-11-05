# B6.2 音频高级（5 节点）

日期：2026-11-05
范围：`SetAudioAttenuation`、`SetAudioDoppler`、`AudioZone`、`SetAudioBusVolume`、`PlaySound3D`
运行时：`KX_FunctionNodesB6Audio.cpp`（表驱动，`kB6AudioSpecs`）

## 1. 接入位置

| 层 | 文件 | 位置 |
|---|---|---|
| 运行时实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Audio.cpp` | 新建；节点实现 33-166，spec 表 170-185，注册 214-224 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6AudioFunctionGameNodes()` 245 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 追加调用 242 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | `GameNodes/KX_FunctionNodesB6Audio.cpp` 198 |
| 适配器接口 | `.../KX_GameAdapters.h` | `KX_AudioAdapter` 追加 5 个虚函数（1145-1230）；归属登记 978-1060 与 `PlaySoundByNameFor` / `PlaySound3DFor` 1203-1225 |
| Mock 记录 | `.../KX_GameAdapters.h` / `.cpp` | 记录结构 1284-1308；`HandleSettings` 1296-1308；实现 545-615 / 1022-1094 |
| 引擎实现 | `.../KX_GameAdaptersEngine.cpp` | 1835-2100 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6b.py` | `NATIVE_B6B` 中 5 条 |
| 调色板 | `.../ui/node_palette.py` | 5 条音频条目 |

## 2. 节点与端口

全部是 ACT（`OnEnter` 触发、`OnDone` 回执）：

| 节点 | 输入 | 输出 |
|---|---|---|
| SetAudioAttenuation | OnEnter, Object, Model(Int), RefDist(Float), MaxDist(Float) | OnDone |
| SetAudioDoppler | OnEnter, Object, Factor(Float) | OnDone |
| AudioZone | OnEnter, ZoneName, Sound, Volume | OnDone |
| SetAudioBusVolume | OnEnter, Bus, Volume | OnDone |
| PlaySound3D | OnEnter, Sound, Position(Vector), Volume, Pitch | OnDone |

参数校验：`Model` 只接受 0..5；`RefDist > MaxDist` 写 stderr 诊断；`Doppler` 负值钳到 0；空 `Bus` / 空 `ZoneName` / 空 `Sound` 写诊断。所有分支都发 `OnDone`。

## 3. 适配器 API

```cpp
virtual void SetAttenuation(KX_GameObject *object, int model,
                            float reference_distance, float maximum_distance);
virtual void SetDopplerFactor(KX_GameObject *object, float factor);
virtual uint64_t SetAudioZone(const std::string &zone_name,
                              const std::string &sound, float volume);
virtual void SetAudioBus(const std::string &bus, float volume);
virtual uint64_t PlaySound3D(const std::string &sound, const float position[3],
                             float volume, float pitch);

/* 对象归属登记（B6.2 收尾补充） */
virtual uint64_t PlaySoundByNameFor(KX_GameObject *object, const std::string &name,
                                    bool loop, float volume, float pitch);
virtual uint64_t PlaySound3DFor(KX_GameObject *object, const std::string &sound,
                                const float position[3], float volume, float pitch);
void RegisterSoundOwner(KX_GameObject *object, uint64_t handle);
void ReleaseSoundOwner(uint64_t handle);
virtual void ForgetObject(KX_GameObject *object);
std::vector<uint64_t> HandlesFor(KX_GameObject *object) const;
KX_GameObject *OwnerOf(uint64_t handle) const;
void ClearSoundOwners();
```

归属表内部是两张互相索引的容器（`object_handles_ : object -> handles`、`handle_owner_ : handle -> object`），
`KX_GameAdapters.h` 978-1060。key 是**借用指针**，对象销毁前必须调用 `ForgetObject`；
`ForgetObject` 只按指针值 erase，不 dereference，因此对已析构对象也安全（悬空 key 会被直接移除，不会再被查询命中）。

## 4. audaspace 映射与等价实现

audaspace 有 3D 设备/句柄，但**没有原生 bus（总线）与 zone（音频区域）概念**。因此：

| 节点 | 引擎侧实现 | 是否原生 |
|---|---|---|
| SetAudioAttenuation | **object 为空（全局）**：`I3DDevice::setDistanceModel(model + 1)`（节点 0..5 对应 audaspace 1..6）并更新所有现存 3D 句柄的 `setDistanceReference` / `setDistanceMaximum` / `setAttenuation(1.0)`；**object 非空（对象局部）**：只更新 `HandlesFor(object)` 返回的句柄，并把设置记在 `object_attenuation_[object]`，之后该对象新起的 3D 句柄沿用（1859-1935，`PlaySound3DFor` 2069-2100） | 原生 3D 距离模型 |
| SetAudioDoppler | **object 为空（全局）**：`I3DDevice::setDopplerFactor(clamp(factor, >=0))`；**object 非空**：存 `object_doppler_[object]`，仅当该对象拥有全部已登记句柄时才写设备，否则只存不写并警告一次（1937-1975）。audaspace 的 doppler 是设备级单值，无法真正按句柄隔离 | 设备级原生，对象局部为近似 |
| AudioZone | **等价实现**：按名字注册一条 listener 处的循环环境音（`PlaySoundByName(sound, loop=true, volume)`），同名 zone 再次进入会先 `StopSound` 旧句柄再重新注册（1985-2007） | **非原生**：不是 audaspace zone |
| SetAudioBusVolume | **等价实现**：`bus_gain_[bus]` 命名增益组；对归属该 bus 的句柄重新计算 `setVolume(base_volume * gain)`，增益钳在 0..1（2009-2028） | **非原生**：不是 audaspace bus，`handle_bus_` 目前默认全部归 `"master"` |
| PlaySound3D | `PlaySoundByName(sound, loop=false, volume, pitch)` 后转 `I3DHandle`：`setRelative(false)`、`setLocation(position)`，并套用最近一次**全局** `SetAudioAttenuation` 的参考/最大距离（2030-2066） | 原生 3D 播放 |

### object 语义：空 = 全局，非空 = 对象局部

- `object == nullptr`：沿用 B6.2 之前的全局行为，影响 3D 设备和**所有**已登记句柄。
- `object != nullptr`：只影响该对象自己的句柄（`HandlesFor(object)`），其他对象完全不变。
- 归属登记：`PlaySound3D` / `PlaySoundByName` 节点通过 `ctx.Owner()`（图所属对象）调用 `PlaySound3DFor` / `PlaySoundByNameFor`，
  创建句柄后 `RegisterSoundOwner(object, handle)` 写入 `object_handles_` 与 `handle_owner_`。
- 注销路径：`StopSound` → `ReleaseSoundOwner(handle)`；句柄销毁（`Reset()` / `ClearSoundOwners()`）清空双向表；
  对象销毁由 `EngineSceneAdapter::RemoveObject` 调 `audio_->ForgetObject(object)`（623）后在 `DelayedRemoveObject` 之前完成，
  `ForgetObject` 同时清 `object_attenuation_` / `object_doppler_` 与两张归属表。表 key 只是指针值，erase 不解引用，
  所以对象析构后的悬空 key 会被移除，不会继续被查询命中。

无 3D 支持时（`WITH_AUDASPACE` 关闭或设备不暴露 `I3DDevice`）写 stderr 诊断并安全返回，不崩溃；`PlaySound3D` 返回有效句柄但位置不生效。

## 4.5 audaspace 是否有真正的 mixer / bus（C.5.1 调研）

结论：**没有命名 bus / mix 总线 API**。`SetAudioBusVolume` 是节点层的等价实现，不能称为“原生总线”。

证据（看类型与 API，不只看名字）：

| 候选 | 实际语义 | 位置 |
|---|---|---|
| `respec::Mixer` | “mix audiosignals with same channel count and sample rate and convert it to a specific output format” —— 输出格式/重采样混音器，不接收名字或分组 | `extern/audaspace/include/respec/Mixer.h:36-37` |
| `SoftwareDevice` | 整个设备只持有**一个** `m_mixer`，逐句柄 `mix(buf, ..., sound->m_volume)` 叠加，无路由层 | `extern/audaspace/src/devices/SoftwareDevice.cpp:714 / 765 / 790` |
| `sequence::Superpose` | “This sound mixes two other factories” —— 二元叠加，无名字、无独立增益总线 | `extern/audaspace/include/sequence/Superpose.h:30` |
| `Bus` / `bus` 符号 | 0 命中 | `grep -rn "Bus|bus" extern/audaspace/include` |

现有等价实现（`KX_GameAdaptersEngine.cpp`）：

- `bus_gain_[bus]` 命名增益表 + `handle_bus_[handle]` 归属表，新句柄默认 `"master"`（2304 / 2340 / 2438 / 2456-2457）。
- `SetAudioBus` 只写增益表并对该 bus 下所有句柄重算 `setVolume(base_volume * gain)`，增益钳在 0..1（2309-2330）。
- 因此语义是“**按名字的增益组**”，播放时与改增益时各应用一次；没有独立的混音/路由/总线树。

## 5. Mock 侧记录

`KX_MockAudioAdapter` 追加：

- `attenuation_requests`（object/model/reference_distance/maximum_distance）
- `doppler_requests`（object/factor）
- `zone_requests`（zone/sound/volume）
- `bus_requests`（bus/volume）
- `spatial_requests`（sound/position[3]/volume/pitch）
- 计数器 `attenuation_calls` / `doppler_calls` / `zone_calls` / `bus_calls` / `spatial_calls`

`Reset()` 会同时清空以上字段。`SetAudioZone` 在 mock 中按 `PlaySoundByName(sound, loop=true, volume, 1.0)` 记录，`PlaySound3D` 按 `PlaySoundByName(sound, loop=false, volume, pitch)` 记录，便于断言参数。

按句柄/按对象语义还需要可断言的最终状态，因此追加：

- `struct HandleSettings { int model; float reference_distance; float maximum_distance; float doppler; float attenuation; float volume; }`
- `std::map<uint64_t, HandleSettings> handle_settings`：每个句柄一份（创建时拷贝当前 `global_settings`，`volume` 取创建音量）
- `HandleSettings global_settings`：`object == nullptr` 时写入的全局值
- `const HandleSettings &SettingsOf(uint64_t handle) const`：查不到时返回 `global_settings`（KX_GameAdapters.cpp 1022-1027）

`SetAttenuation` / `SetDopplerFactor` 在 mock 中遵循同一套语义：空 object 改全局并同步所有句柄，非空只改 `HandlesFor(object)` 的句柄（1029-1094）。
同理，`RegisterSoundOwner` / `ReleaseSoundOwner` / `ForgetObject` 在基类里实现，mock 与引擎实现共用。

## 6. 验证

- 编译：`ninja -C build_linux -j6` exit=0（日志 `/tmp/b62_build2.log`，44 行，仅既有 `-Woverloaded-virtual` 警告）。
- E2E：`/tmp/b62_e2e` 51 条断言 + 汇总行，`PASS` 52 行、`FAIL` 0 行、`total failures=0`（`/tmp/b62_e2e.log`）。
- 端口契约：`PORT CONTRACT native_nodes=185 catalog_nodes=54 errors=0 warnings=4`（`/tmp/b62_port_contract2.log`）。
- 枚举一致性：`ENUM CONFIG CONSISTENCY checked_pairs=63 inconsistencies=0`（`/tmp/b62_enum2.log`）。
- 注册：`native_b6b.py` 27 行与 C++ 27 条 spec 完全一致 `MISSING [] EXTRA []`。

音频相关断言：

| 断言 | 结果 |
|---|---|
| SetAudioAttenuation 到达适配器，ref=1.0 / max=10.0 | PASS |
| SetAudioDoppler factor=2.0 | PASS |
| AudioZone zone=`forest` sound=`beep` | PASS |
| SetAudioBusVolume bus=`master` volume=0.5 | PASS |
| PlaySound3D 到达适配器，位置 (1,2,3) | PASS |
| PlaySound3D volume=0.8 / pitch=1.2 | PASS |
| PlaySound3D attributes the handle to the graph owner | PASS |
| object A attenuation applied（ref=2.0 / max=5.0） | PASS |
| **object B attenuation and volume unchanged**（改 A 前后 B 的 ref/max/volume 一致） | PASS |
| object A doppler applied（3.0） | PASS |
| **object B doppler unchanged**（改 A 前后 B 的 doppler 一致） | PASS |
| ForgetObject drops A ownership（`HandlesFor(A)` 为空、`OwnerOf(handle_a)` 为空） | PASS |
| ForgetObject keeps B ownership（`OwnerOf(handle_b) == object_b`） | PASS |

隔离断言的做法：A/B 各自 `PlaySound3DFor` 建句柄，修改 A 之前**先快照 B 的 ref/max/doppler/volume**，改 A 之后与快照比较。
之所以改用快照而不是断言 B 等于默认值，是因为同一张图里先前执行过 object 为空的全局 `SetAudioAttenuation` / `SetAudioDoppler`，
B 的句柄按设计继承了全局默认（ref=1 / max=10 / doppler=2）；断言改成「相对快照不变」才真正验证隔离，属于语义变化后的合理调整。

## 7. 限制

1. bus / zone 是等价实现，不是 audaspace 原生概念（见 §4）。
2. `SetAudioAttenuation` 的 `Model` 仍是设备级：非空 object 时只隔离 ref/max/attenuation，模型本身无法按对象区分（首次遇到写一次警告）。
3. `SetAudioDoppler` 在 audaspace 里是设备级单值：非空 object 只有在「该对象拥有全部已登记句柄」时才真正生效，否则只记录不应用并警告一次；需要严格隔离时应避免同时存在多对象的 3D 句柄。
4. 归属依赖 `ctx.Owner()`：节点 spec 没有独立 Object 端口，句柄归属的是图所属对象；跨图共享句柄的场景需要后续补端口。
5. 无 bus 归属编辑界面：新句柄默认归 `"master"`；其他分组需要后续提供映射入口。
6. 无 3D 设备时只报诊断，`PlaySound3D` 退化为普通 2D 播放。

## 8. 句柄宽度与 32 位端口（B.5 收口）

原生 `StartSound` / `PlaySoundByName` 的 `Handle` 端口是 `KX_VALUE_INT`，而适配器句柄是 `uint64_t`。
旧代码把它 `static_cast<int>`，句柄 ≥2^31 时截断/变负，`StopSound` 可能停掉另一条音。现分三层修掉：

| 层 | 位置 | 行为 |
|---|---|---|
| 节点边界 | `KX_FunctionNodesB3.cpp:352-377` | `B3SoundHandleInt()`：0 或 >INT32_MAX 判失败并写 stderr，不再静默截断 |
| 节点动作 | `KX_FunctionNodesB3.cpp:391-413 / 497-518` | 越界时立即 `StopSound` 回收，`Handle` 输出 0，`OnStarted`/`Found` 为 false |
| 引擎分配 | `KX_GameAdaptersEngine.cpp:2419-2438` | `AllocateHandle()` 把 id 限制在 1..INT32_MAX 循环复用，跳过在用 id |

不做 Int64 值类型：`KX_RuntimeValue.data.i` 是 `int`，扩展要改值池、`port_contract` 标签与
`enum_consistency`，属共享文件改造（0.5~1 天），而“分配上限 + 边界守卫”已让截断不可能发生。
legacy `StartSound` / `StopSound` 的迁移评估见 `docs/SOUND_BRIDGE_EVAL.md`。

## 9. B3 原生音频 7 项的证据补齐（Mock 层）

背景：加权核算把 `StartSound`、`StopSound`、`SetSoundVolume`、`SetSoundPitch`、
`SoundFinished`、`PlaySoundByName`、`Set3DListener` 记为 `unclassified`（无逐项行为证据）。
本轮用 Mock 适配器补齐「节点 → 适配器调用 → 状态迁移 → 触发回调」全链路，
8 项（含动画 `ActionDone`）分档由 `unclassified` 改为 **`mock-verified`**。
**没有判 `full`**：Mock 通过不等于真机通过。

| 节点 | 实现位置 | Mock 层已断言 | 真机待验 | 分档 |
|---|---|---|---|---|
| StartSound | `KX_FunctionNodesB3.cpp:391-413` | `PlaySound` 调用 1 次；file/loop/volume/pitch 四个参数原样到达；句柄=1；`OnStarted` 一次 | 真实声卡输出、Loop 实际循环 | **mock-verified** |
| StopSound | `:430-437` | `StopSound` 调用 1 次；Mock 声源 playing→false；`OnDone` 一次 | 真实声源停止/声道释放 | **mock-verified** |
| SetSoundVolume | `:439-446` | `SetVolume` 一次；Mock 读回 0.6（由 1.0 改为 0.6） | 真实音量听感变化 | **mock-verified** |
| SetSoundPitch | `:448-455` | `SetPitch` 一次；Mock 读回 1.5（由 1.0 改为 1.5） | 真实音高听感变化 | **mock-verified** |
| SoundFinished | `:466-482` | 未完成帧不触发；完成后触发 1 次；后续帧不再触发 | 真实播放结束事件是否同帧可观测 | **mock-verified** |
| PlaySoundByName | `:497-518` | 名字 `explosion` 原样转发；句柄=1；`Found=true`；`OnDone` 一次 | name→asset 真实解析是否命中 | **mock-verified** |
| Set3DListener | `:527-538` | 位置 (1,2,3)、朝向 (0,1,0) 到达适配器；`OnDone` 一次 | audaspace 真实 3D 衰减/方位 | **mock-verified** |

证据载体：`tests/b3_audio_e2e.cpp`（31 条断言）、`/tmp/b3_audio_e2e_run.log`（failures=0）、
`/tmp/audio_evidence.log`。运行方式：
`tests/run_all_e2e.sh --only b3_audio`
（独立 TU 编译，不走 ninja，不动 `build_linux`）。

**证据等级与边界**：以上是 **Mock 适配器层证据**，不是真机听感。以下仍为
`pending real device`，不得用 Mock 结果冒充真机通过：

- 真实声卡输出、Loop 实际循环、Volume/Pitch 实际听感；
- 真实资源名解析（`PlaySoundByName` 的 name→asset 查找）；
- 真实 audaspace 的 3D 衰减/方位（`Set3DListener` 只证明参数到达适配器）；
- 无 3D 设备时 `PlaySound3D` 退化为 2D 的既有行为（见 §7 第 6 条）。

音频域口径由此变化：12 节点中 `full` 仍为 1、`mock-verified` 0→7、`approximate` 4、
`unclassified` 7→0。

- 严格（主口径，Mock 计 0）：**8.3% 不变**；
- 加权（`mock-verified` 计 0.5）：25.0% → **54.2%**；
- 覆盖（`mock-verified` 计 1）：41.7% → **100%**。

只有 7 项真机验证通过、`mock-verified` → `full` 后，严格才会升到 66.7%、加权到 83.3%。
当前状态**不得**表述为「音频已实现/已验证」。
