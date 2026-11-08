# SOUND_BRIDGE_EVAL — StartSound / StopSound 的 Python 句柄桥评估

日期：2026-11-05
工程：`/home/eridanusepsilon/Code/EBC/EBC5.3`
日志：`/tmp/b5_sound.log`（构建 + 69 条 E2E 断言 + 引擎适配器目标文件编译）
前置报告：`/tmp/b661_sound_bridge_eval.md`（B6.6 首轮评估，本文件是对它的补充与收口）

## 0. 结论

| 问题 | 结论 |
|---|---|
| Python 侧音频句柄的真实表示 | **`SCA_SoundActuator` 的 Python 实例**。`bge.logic` 只导出 `KX_SOUNDACT_*` 枚举常量；不存在 `KX_SoundActuator` / `KX_3DSound` 类，也不存在 `bge.logic.playSound()`。内部 `AUD_Handle m_handle`（`std::shared_ptr<aud::IHandle>`）**不暴露给 Python**，没有 `handle` 属性 |
| 路径 A（`KX_SoundActuator` 稳定 ID） | **≥5 天，且前提不成立**（节点层拿不到 `SCA_SoundActuator`，legacy 输出也不是它） |
| 路径 B（`KX_AudioAdapter` 映射表） | **4~5 天，取整报 5 天**（C++ 桥 0 天已存在；成本全在 legacy 端口契约） |
| 是否重开 | **不重开**，维持 FOREVER；理由从「未评估 / 无语义 Int 桥」改为「桥接需 5 天，性价比不足」 |
| 32 位截断缺陷 | **已修**（节点边界守卫 + 引擎分配上限 + 4 条新 E2E） |

## 1. Python 侧句柄的真实表示（证据）

| 事实 | 位置 |
|---|---|
| Python 类型注册为 `SCA_SoundActuator` | [KX_PythonInitTypes.cpp:262](../source/gameengine/Ketsji/KX_PythonInitTypes.cpp#L262) |
| 类声明、成员 `AUD_Handle m_handle` | [SCA_SoundActuator.h:53-61](../source/gameengine/GameLogic/SCA_SoundActuator.h#L53-L61) |
| Python 方法只有 `startSound` / `pauseSound` / `stopSound` | [SCA_SoundActuator.h:109-111](../source/gameengine/GameLogic/SCA_SoundActuator.h#L109-L111) |
| 播放时 `m_handle = device->play(sound, false)`（handle 只在 C++ 侧） | [SCA_SoundActuator.cpp:155](../source/gameengine/GameLogic/SCA_SoundActuator.cpp#L155) |
| Python 属性表：`is3D` / 7 个 3D 参数 / `sound` / `time` / `volume` / `pitch` / `preload` / `mode`，**无 handle** | [SCA_SoundActuator.cpp:376-413](../source/gameengine/GameLogic/SCA_SoundActuator.cpp#L376-L413) |
| `bge.logic` 只有 `KX_SOUNDACT_*` 常量，没有 play/stop 函数 | [KX_PythonInit.cpp:1402-1410](../source/gameengine/Ketsji/KX_PythonInit.cpp#L1402-L1410) |
| 全仓 `playSound` 命中 0 | `grep -rn "playSound" source/ scripts/` |

因此「把 Python 对象转成整数 ID」在 5.3 里没有对象可转：用户能拿到的只有 actuator 实例，
而 legacy 节点 `Sound` 端口要的又是旧 uplogic `StartSoundNode` 的返回对象（该类源码在 5.3 中已不存在）。

## 2. 路径 A：`KX_SoundActuator` 稳定 ID（评估后不做，≥5 天）

草案：

```cpp
class SCA_SoundActuator {
  uint64_t GetStableId() const;   // (generation << 32) | slot，或 (this 指针 + 生成计数) 哈希
};
// KX_AudioAdapter 侧维护 unordered_map<uint64_t, SCA_SoundActuator*>，节点只认这个 ID。
```

要解决的四件事与卡点：

1. **节点层拿不到 `SCA_SoundActuator`**：节点上下文只有 `ctx.Owner()`（`KX_GameObject*`）和适配器，
   从对象到「某个挂在它逻辑砖上的 actuator 实例」没有稳定寻址路径。
2. **legacy 输出类型对不上**：legacy `Sound` 端口是 `NodeSocketLogicPython`（`nl_type=29`），
   期望旧 uplogic 的 Python 对象；`GetStableId` 产出的是整数，类型契约仍不成立。
3. **句柄回收 / 复用**：actuator 销毁后 ID 悬垂，必须引入 generation；还要保证 Python 侧拿到的是同一个 ID
   （当前 actuator 的 Python 包装层没有任何 ID 暴露点）。
4. **场景卸载后的悬垂**：actuator 随场景销毁，节点侧 ID 需要 `ForgetObject` 级别的清理钩子。

结论：即便补齐以上四点，路径 A 也只是「另一个形态的路径 B」，端口类型问题一分没解，
工期 ≥5 天，**评估后不做**。

## 3. 路径 B：`KX_AudioAdapter` 映射表（已存在，4~5 天只差兼容层）

现状（本轮之前就已可用，`tests/b661_bridge.cpp` 已验证往返）：

- `PlaySoundByName` 返回 `uint64_t`，引擎用 `std::map<uint64_t, std::shared_ptr<aud::IHandle>> handles_` 存句柄：
  [KX_GameAdapters.h:1299-1325](../source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h#L1299-L1325)、
  [KX_GameAdaptersEngine.cpp:2459-2468](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2459-L2468)。
- `StopSound/SetVolume/SetPitch/IsPlaying/IsFinished` 全部按 `uint64_t` 查表。
- 对象销毁清理钩子已经存在：`ForgetObject` → [KX_GameAdapters.h:1257-1271](../source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h#L1257-L1271)；
  句柄与对象双向表 `object_handles_` / `handle_owner_` 在 1461-1463。

要补的两块：

| 工作项 | 估算 | 说明 |
|---|---|---|
| `StartSoundLegacy` / `StopSoundLegacy` 端口与 spec（含 editor / catalog / port_contract 接入） | 0.5 天 | legacy 是 20 入/3 出、2 入/1 出；按 B6.5a 策略新建 Legacy 类型 |
| 20 个输入的语义映射（cone / seek / lowpass / 结束回调；occlusion / reverb 只能近似 + 诊断） | 1.5~2 天 | audaspace 有 cone 与 seek，无 occlusion/reverb 原生能力 |
| `Sound` 端口 `nl_type=29` 的类型策略 | 0.5~1 天 | `port_contract.py` 的 `SOCKET_TYPE_TO_TAG` 无 29，需要评审「Python 句柄 ≡ Int」 |
| 值宽度（见 §5） | 0.5 天 | 已由本轮截断修复覆盖，不需要新增 Int64 值类型 |
| E2E（启动→停止→再启动、停止后句柄失效、对象销毁后停止） | 0.5~1 天 | 需要 mock 侧句柄回收断言 |

合计 **4~5 天**，取整报 **N = 5 天**。

## 4. 决策记录

- 决策规则：任一标题 ≤2 天落地才重开。
- 路径 A ≥5 天且前提不成立；路径 B 4~5 天。两条都超过阈值。
- **决策：维持 FOREVER，不实现 legacy `StartSound` / `StopSound`。**
- 理由（写入 `DEFERRED_FOREVER.md` §5 与 `B6_LEGACY_COMPAT.md` §10）：
  「Python 句柄桥需 5 天（20 端口语义 + Python socket 类型策略），性价比不足」。
- 具体卡点：① legacy 20 入/3 出；② `Sound` 是 `nl_type=29`，原生值模型无 Python 类型；
  ③ occlusion / reverb / timescale 在 `KX_AudioAdapter` 无对应能力。
- 原生 `StartSound` / `StopSound` 已覆盖新图需求，本轮修复不影响它们的行为（见 §5）。

## 5. 32 位截断缺陷修复（已修）

缺陷：`FuncStartSound` 把 `uint64_t` 句柄 `static_cast<int>` 后写进 `KX_VALUE_INT`，
句柄 ≥2^31 时会截断或变负；之后 `StopSound` 用这个坏值去查表，可能停掉**另一条**正在播放的音。

修了三层：

| 层 | 文件 / 行 | 内容 |
|---|---|---|
| 节点边界 | [KX_FunctionNodesB3.cpp:352-377](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp#L352-L377) | 新增 `kMaxNodeSoundHandle = 0x7FFFFFFF` 与 `B3SoundHandleInt()`：0 或 >INT32_MAX 一律判失败并写 stderr 诊断，绝不静默截断 |
| StartSound / PlaySoundByName | [KX_FunctionNodesB3.cpp:391-413](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp#L391-L413)、[497-518](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp#L497-L518) | 越界句柄立即 `StopSound` 回收（不留“播着但拿不到”的泄漏），`Handle` 输出 0、`OnStarted`/`Found` 为 false |
| 引擎分配 | [KX_GameAdaptersEngine.cpp:2419-2438](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2419-L2438) | 新增 `AllocateHandle()`：id 限制在 1..INT32_MAX 循环复用，跳过仍在 `handles_` 中的 id；返回 0 时 `StartHandle` 放弃播放 |

注意：**没有**新增 Int64 值类型。`KX_RuntimeValue::data.i` 仍是 `int`（[KX_RuntimeValue.h:100](../source/gameengine/GameLogic/GameNodes/KX_RuntimeValue.h#L100)），
`OutInt/InInt` 仍是 32 位（[KX_FunctionNode.h:145](../source/gameengine/GameLogic/GameNodes/KX_FunctionNode.h#L145) / [176](../source/gameengine/GameLogic/GameNodes/KX_FunctionNode.h#L176)）。
新增值类型要动 `KX_RuntimeValue`、值池、`port_contract` 标签表、`enum_consistency` 与全部节点 switch，
估算 0.5~1 天且属于共享文件改造；改为“分配上限 + 边界守卫”后，截断在语义上已经不可能发生。

## 6. 验证

| 项 | 结果 | 日志 |
|---|---|---|
| E2E 构建（含 B3 源码重编） | exit=0 | `/tmp/b5_sound.log` 第 1-3 行 |
| E2E 运行 | **69 PASS / 0 FAIL / total failures=0** | `/tmp/b5_sound.log` |
| 引擎适配器目标文件编译 | `KX_GameAdaptersEngine.cpp -c` exit=0 | `/tmp/b5_sound.log` 末段，`/tmp/b5_engine.o` 217936 B |

本轮新增 4 组 / 10 条断言（`tests/b62_e2e.cpp`，段 “sound handle E2E”）：

| 断言 | 结果 |
|---|---|
| StartSound 小句柄往返（1 → 1） | PASS |
| StartSound OnStarted 触发 | PASS |
| **INT32_MAX 句柄不被截断（2147483647 → 2147483647）** | PASS |
| INT32_MAX 仍能起播 | PASS |
| **>INT32_MAX 句柄被拒绝（输出 0）** | PASS |
| 拒绝时不触发 OnStarted | PASS |
| 拒绝时句柄被 StopSound 回收（stop_calls=1） | PASS |
| 被拒绝的句柄不再处于 playing | PASS |
| StartSound → StopSound 各到达适配器一次 | PASS |
| StartSound 输出的句柄正好停掉它启动的音 | PASS |

越界场景同时验证了诊断输出：`GameNode: sound handle 2147483648 exceeds the 32-bit node port range`。

未运行全量 `ninja -C build_linux -j6`：构建目录被 JIT 线占用，按要求只做目标文件级验证。

## 7. 本轮改动清单

1. `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp`：句柄收窄守卫 + 两个输出点。
2. `source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp`：`AllocateHandle()` 与分配上限。
3. `docs/SOUND_BRIDGE_EVAL.md`（本文件）。
4. `docs/DEFERRED_FOREVER.md` §5、`docs/B6_LEGACY_COMPAT.md` §10：追加本轮复核与修复说明。
5. `docs/B6_AUDIO.md` §8：追加句柄宽度说明。
6. `tests/b62_e2e.cpp`（已入库）、`/tmp/b5_sound.log`、`tests/b5_engine_check.sh`（临时验证物）。

未做：`git commit/push`（按要求）；未改 7 个未跟踪 `_lib.glsl`；未改编译器文件
（`KX_CompiledGraph*` / `KX_InlinePass*` / `KX_GraphCompiler*`）；未改 `KX_GameAdapters.h/.cpp`
的物理/网络/UI 段。

## 复核记录（2026-11-06）

本轮性质：核实 + 定稿，不重做、不改生产源码。**结论：保持 FOREVER，不重开；N = 5 天。**

### 1. 七条首版事实复核（全部仍成立）

| 首版事实 | 复核结果 | 证据（2026-11-06 重查） |
|---|---|---|
| Python 侧只有 `SCA_SoundActuator`，内部 `AUD_Handle` 不暴露 | 仍成立 | [SCA_SoundActuator.h:53,58](../source/gameengine/GameLogic/SCA_SoundActuator.h#L53-L58)；Python 方法仅 `:109-111`；全仓 `grep playSound` 0 命中 |
| `bge.logic` 没有 play/stop 函数 | 仍成立 | `KX_PythonInit.cpp` 仅导出 `KX_SOUNDACT_*`；`grep playSound source/ scripts/` 仍为 0 |
| 路径 B 的 C++ 句柄桥已存在（`uint64_t` + 句柄表） | 仍成立 | [KX_GameAdapters.h:1315](../source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h#L1315)、`:1339-1362`、`:1461-1463`、`:1257-1271` |
| 32 位截断守卫与引擎分配上限已修 | 仍成立 | [KX_FunctionNodesB3.cpp:352-377](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp#L352-L377)；[KX_GameAdaptersEngine.cpp:2419-2438](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2419-L2438) |
| `Sound` 端口 `nl_type=29` 无原生值类型 | 仍成立 | [port_contract.py:88-101](../scripts/startup/bge_gamenodes/generator/port_contract.py#L88-L101) 的 `SOCKET_TYPE_TO_TAG` 无 29，出现 29 即计 error |
| legacy `StartSound` 20 入/3 出 | 仍成立 | [B6_LEGACY_COMPAT.md:81](B6_LEGACY_COMPAT.md#L81)；`deferred_matrix.json:36-40` 的 `LogicNodeStartSound / StartSoundNode` 条目 |
| 原生 `StartSound` / `StopSound` 已覆盖新图 | 仍成立 | [KX_FunctionNodesB3.cpp:745-746](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp#L745-L746) |

路径 A 的四个卡点（节点层拿不到 actuator、`nl_type=29` 类型对不上、ID 回收/generation、场景卸载清理）
也全部未变：即使做出稳定 ID，也只是“换形态的路径 B”。**路径 A ≥5 天且前提不成立，不做。**

### 2. N = 5 天的构成（路径 B，唯一可落地路径）

| 工作项 | 天数 | 依据 |
|---|---|---|
| `StartSoundLegacy` / `StopSoundLegacy` 端口与 spec + editor/catalog/port_contract 接入 | 0.5 | legacy 20 入/3 出、2 入/1 出；按 B6.5a 的 Legacy 类型策略新增端口表 |
| 20 个输入的语义映射 | 1.5~2 | audaspace 有 cone（`extern/audaspace/include/sequence/SequenceEntry.h:287-317`）与 seek；**occlusion / reverb 在 audaspace 与 `KX_GameAdapters.h` 均 0 命中**，只能近似 + 诊断 |
| `Sound` 端口 `nl_type=29` 类型策略 | 0.5~1 | `port_contract.py:88-101` 无 29；需评审「Python 句柄 ≡ Int」的标签口径 |
| 值宽度（32 位句柄） | 0 | 首轮已修：分配上限 + 边界守卫，`KX_RuntimeValue` 不需要 Int64 |
| E2E（启动 → 停止 → 再启动、停止后句柄失效、对象销毁后停止） | 0.5~1 | 需要 mock 侧句柄回收/失效断言 |
| **合计** | **4~5 → 报 5** | 决策规则是“任一标题 ≤2 天落地才重开”，5 天为阈值的 2.5 倍 |

### 3. 判决边界

- 决策规则不变：**任一实现路径 ≤2 天落地才重开**。路径 A ≥5 天且前提不成立，路径 B 4~5 天，
  两条都超过阈值 → **维持 FOREVER**。
- 定稿理由：**「桥接需 5 天（20 端口语义 + Python socket 类型策略），性价比不足」**。
- 若产品坚持 occlusion / reverb 的**真实**语义（而非近似），audaspace 无原生能力需另做后端，
  路径 B 再加约 2 天 → **N = 7 天，判决只会更坚定地保持 FOREVER**。

### 4. 复核证据入口

- A.2 状态日志（WorldPosition / Touch，独占创建 + `chmod 444`）：`/tmp/a2_status_20261106-094325.log`。
- A.3 本轮审查依据即上表所引源码/契约文件与 `grep playSound`、`grep occlusion|reverb` 的结果；
  本轮未运行 `ninja`，未改源码、`tests/`、构建目录与 `_lib.glsl`，未 commit/push。
- 本复核取代了 2026-11-06 09:44 的临时时间戳副本（已删除），本文件为唯一真相源。
