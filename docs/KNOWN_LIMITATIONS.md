# KNOWN_LIMITATIONS — 已知限制清单（Phase D.2）

日期：2026-11-05
范围：已实现能力中**仍然存在**的不完整语义、精度近似、性能天花板与平台边界。
性质：纯文档。本文件不修改源码，所有证据均来自本轮审计与既有实现文档；状态判定见 §0。

## 0. 怎么用这份清单

本清单回答的是「**已经做出来的能力，哪里还不完整**」，不回答「哪些还没做」。
后者唯一出处是 [DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)（57 条基线 + 本轮新增 8 条）。
一份报告里如果同时引用两者，必须分开写「未实现（FOREVER）」与「已实现但有边界（本清单）」。

### 0.1 三种口径不能混

| 口径 | 分母 | 当前主数字 | 说明什么 | 出处 |
|---|---|---|---|---|
| 能力域加权（286 域表快照，**不是 327 口径**） | **286**（`TypeCount() = 327`；286 是旧 14 域权重表分母） | 严格 `252/286 = 88.1%`；加权 `267/286 = 93.4%`；覆盖 `282/286 = 98.6%`。**仅作域权重快照，不得引用为全局口径**；327 口径三值为 98.69% / 99.34% / 97.86% | 新节点体系自身的完成度（域权重视角） | [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §0 |
| 节点可用率（能力域） | 该能力域节点数 | 动画域（**11 节点**）：`10 full / 1 mock-verified` → 严格 `10/11 = 90.9%`（加权 95.5%、覆盖 100.0%） | 某个**能力域**里有多少节点是 full | [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §2.2/§5.1 |
| 节点可用率（批次子集） | 该批次节点数 | B6.1 动画高级子集（**8 节点**）：`4 full / 2 approximate / 2 unsupported` → 严格 `4/8 = 50.0%`；B6.1 全批 23 节点：`16/23 = 69.6%` | 某个**批次子集**里有多少节点是 full，**不能代表能力域** | [B6_ANIMATION.md](B6_ANIMATION.md) §6 |
| legacy 兼容度 | **290 / 233 / 360** | `69/290 = 23.8%`；`69/233 = 29.6%`；semantic `146/360 = 40.6%` | 旧 uplogic 图迁移了多少 | [LEGACY_COVERAGE_POLICY.md](LEGACY_COVERAGE_POLICY.md) §0 |

**子集分档 ≠ 能力域分档**：`4/8 = 50.0%` 只描述 B6.1 动画高级的 **8 个节点**，不是动画能力域的整体；动画域是 **11 个节点、`10/11 = 90.9%`**。把子集数字当成域成绩会低报 **40.9 个百分点**——引用前必须确认分母对应的是「批次子集」还是「能力域」。

1. **每写一个百分比，先写分母。** 286、290、233、360 不得相加、换算、求平均。
2. **`full` 不等于「没有限制」。** 例如 `BlendActions` 判 `full`，但仍固定占用 layer 0/1、只支持两个动作（#18）。
3. **`active` 不自动扣分。** 它表示限制仍在；是否影响验收要看该限制是否落在报告的评估范围内。
4. **`mitigated` 必须给出缓解证据，`removed` 必须给出「限制已不存在」证据。** 两者都不许凭印象填写。
5. 本清单的条目**不能**塞进 FOREVER 的 57 条机器区间，也不能用来修饰 233 分母。

### 0.2 状态定义

| 状态 | 含义 | 判定要求 |
|---|---|---|
| `active` | 限制仍在，用户可复现 | 给出源码 file:line 或复现路径 |
| `mitigated` | 限制的**根因仍在**，但已用替代路径、明确失败或边界守卫把伤害限制住 | 必须同时给出「根因未消除」与「缓解生效」两类证据 |
| `removed` | 限制已不存在 | 必须给出移除证据（代码/文档/回归）；证明不了就不要写 |

### 0.3 总览（31 条：active 26 / mitigated 4 / removed 1；2026-11-07 快照，新增 #31、#32，#19 改判 `removed`；#26 为重分类说明、不占表行）

| # | 限制 | 状态 |
|---|---|---|
| 1 | 音频 bus/zone 是等价实现 | `active` |
| 2 | UDP 通道无重传/无拥塞控制/不保序 | `active` |
| 3 | UnloadScene 四态语义 | `mitigated` |
| 4 | GraphCompiler 输出白名单只到 Collision | `active` |
| 5 | LoadSceneAsync 进度是反馈而非真实加载 | `active` |
| 6 | JIT 只做图内联，实测天花板约 3.5% | `active` |
| 7 | SetSceneTimeScale 只能全局 | `active` |
| 8 | Collision 接触点是 AABB 近似 | `active` |
| 9 | std::regex 不防灾难性回溯 | `active` |
| 10 | AES 不可用，XOR 只防窥不防攻击 | `active` |
| 11 | 屏幕栈进程级共享 | `active` |
| 12 | audaspace 设备级限制 | `active` |
| 13 | StringLength/StringPad 字节版与字符版差异 | `mitigated` |
| 14 | LogicGate 旧 IR 必须重导出 | `mitigated` |
| 15 | Collision 输出触发槽不可回读 | `active` |
| 16 | Collision 1 帧采样延迟 | `active` |
| 17 | SetBoneTransform 延迟更新 | `active` |
| 18 | BlendActions 固定占用 layer 0/1、只支持两个动作 | `active` |
| 19 | 导航无 Recast，agent 参数不生效（**已消解，2026-11-07 源码复核**，见 §19） | `removed` |
| 20 | 载具悬挂参数固定，悬挂长度写死 0.2 | `active`（子项「轮组命名约定」已 `removed`） |
| 21 | Touch 是球近似 | `active` |
| 22 | 32 位句柄上限 | `mitigated` |
| 23 | Polar 布局在 design_size 缩放下命中不准 | `active` |
| 24 | AddAnimationEvent 是节点侧逐帧轮询 | `active` |
| 25 | 场景父子不共存：Mount/UnmountChildScene 为 push/pop 等价 | `active` |
| 27 | 列表节点语义：`ListSort` 的 `Key` 只读直接属性；`ListFilter`/`ListMap` 只接受数值元素、4096 静默截断 | `active` |
| 28 | While 的条件与输入端口在 OnEnter 快照，循环体不能逐次改变条件 | `active` |
| 29 | `PlaySoundPlaylist` 固定容量：最多 16 轨、单轨名字 47 字节 | `active` |
| 30 | 正则拒绝嵌套量词与可匹配空串的组体；其余模式无墙钟超时 | `active` |
| 31 | 资源在 Player 模式下未注册（音频/贴图/字体等外部资源，**严重**） | `active` |
| 32 | Print 空槽不再输出 `None`，改为 `<Print: Value input unset>`（行为变更） | `active` |

---

## 1. 音频 bus / zone 是等价实现，不是原生总线

- **现象**：`SetAudioBusVolume` 改的是「同名句柄组」的音量；`AudioZone` 只是在 listener 位置循环播放一条声音。没有独立混音/路由层，没有多 bus 叠加，也没有区域空间衰减；新句柄一律默认归 `"master"`，编辑器没有 bus 归属入口。
- **影响范围**：B6.2 的 `SetAudioBusVolume`、`AudioZone`，以及所有把「bus」当成独立混音通道的图。
- **原因**：audaspace 无命名 bus API：`respec::Mixer` 是格式/采样率重采样器，`SoftwareDevice` 全设备只有一个 `m_mixer`，`sequence::Superpose` 是二元叠加。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2313-L2331)（`bus_gain_` + 逐句柄重算，注释写明 equivalent implementation）、[B6_AUDIO.md](B6_AUDIO.md) §4.5（89-106）、§7 第 1/5 条（163/167）。
- **状态**：`active`

## 2. UDP 通道无重传、无拥塞控制、不保序

- **现象**：`SendUnreliable` 发出的包可能丢失、重复、乱序；节点不会重发，也不会等待确认。只保证「已经到达的包内容完整且通过来源校验」。
- **影响范围**：`SendUnreliable` / 网络域 11 节点中走不可靠通道的部分；跨公网或弱网同步。
- **原因**：独立 `O_NONBLOCK` UDP socket，协议帧头没有序号/重传/拥塞窗口；LAN 直连是设计前提。
- **证据**：[KX_NetworkAdapterEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_NetworkAdapterEngine.cpp#L642-L679)（发送路径与失败诊断）、[B6_NETWORK.md](B6_NETWORK.md) 第 60 行与 §9 第 3 条（108）。
- **状态**：`active`

## 3. UnloadScene 四态语义：能诚实失败，但不能真的卸载

- **现象**：`UnloadScene` 对活跃场景返回 `SceneActive`、对非活跃场景返回 `Unsupported`，两种情况都发 `OnFail` 并写 stderr；场景不会消失。旧版本「发 `OnDone` 但什么都没卸载」的假成功已不存在。
- **影响范围**：场景域 `UnloadScene`；任何依赖运行时释放场景的流程。
- **原因**：`KX_KetsjiEngine::RemoveScene` 在 EEVEE 集成期被显式禁用（带注释的提前 `return`）。释放链路完整，但渲染层没有 GPU 资源注销钩子，放开属不可控风险。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1906-L1938)（四态判定）、[KX_FunctionNodesB6Scene.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Scene.cpp#L195-L252)（端口与失败分支）、[B6_SCENE_ADVANCED.md](B6_SCENE_ADVANCED.md) §4.2/§4.3（71-88）。
- **状态**：`mitigated`
  缓解点：假成功分支已删除，四态 + `reason` + stderr + E2E 断言（`unload unsupported: engine RemoveScene disabled`）。
  未变部分：底层 `RemoveScene` 仍被禁用，所以不是 `removed`。

## 4. GraphCompiler 白名单只到 Collision

- **现象**：旧 IR 少声明**尾部输出端口**时，只有签名打开 `allow_fewer_outputs` 的节点能编译；当前只有 `Collision` 打开。其它节点（尤其后来追加过端口的）旧图会在编译期报端口数量不匹配。
- **影响范围**：所有非 `Collision` 节点的旧 IR 兼容；后续给节点追加输出端口时的升级路径。
- **原因**：编译器对输入/输出端口数做严格比对，兼容开关是每签名一个布尔值，目前只给 `Collision` 打开。
- **证据**：[KX_GraphCompiler.cpp](../source/gameengine/GameLogic/GameNodes/KX_GraphCompiler.cpp#L276-L292)（注释明确「(Collision today)」）、[B6_SENSOR.md](B6_SENSOR.md) §7（71-73）。
- **状态**：`active`

## 5. LoadSceneAsync 的进度是反馈，不是真实加载进度

- **现象**：`OnProgress` 从 0 到 1 只是 0.5 秒的计时反馈；这段时间里旧场景仍在正常运行，新场景对象不可用。到 1.0 才执行一次同步替换并点亮 `OnLoaded`。
- **影响范围**：场景域 `LoadSceneAsync`；依赖「进度 50% 时新场景已部分可用」的图。
- **原因**：`KX_KetsjiEngine::LoadScene` 是单帧同步替换，没有增量资产流式加载接口。
- **证据**：[KX_FunctionNodesB6Scene.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Scene.cpp#L122-L182)（`progress += delta_time / 0.5`，到 1.0 才替换）、[B6_SCENE_ADVANCED.md](B6_SCENE_ADVANCED.md) §3（31-41）。
- **状态**：`active`

## 6. JIT 只做图内联，实测天花板约 3.5%（40% 需直线特化）

- **现象**：开启内联后，100 个 `ConstFloat` 的最轻图从 1127.9 降到 1088.5 ns/帧，仅 **3.49%**；多一层虚调用时最坏约 **-1%**。验收项「帧时间降低 40%」为 FAIL。
- **影响范围**：JIT / `KX_InlinePass` 的性能收益；对帧时间敏感的大图。
- **原因**：内联只消除节点间调度与函数调用，没有把叶子节点的偏移读写展开成直线代码；要达到 40% 必须做直线特化（execution specialization）。
- **证据**：`/tmp/b662_jit.log`（`BENCH ... reduction=3.49%`、`FAIL benchmark reduces frame time by at least 40%`、以及「要达成 40% 必须真正特化执行」的结论文本）、[KX_InlinePass.h](../source/gameengine/GameLogic/GameNodes/KX_InlinePass.h)。
- **状态**：`active`

## 7. SetSceneTimeScale 只能全局

- **现象**：节点接收 `SceneName`，但只做存在性校验；实际调用的是引擎唯一的全局 `SetTimeScale`。给 A 场景变速会同时影响 B 场景。
- **影响范围**：场景域 `SetSceneTimeScale`；多场景独立暂停/慢放的用法。
- **原因**：`KX_KetsjiEngine` 只暴露一个全局时间缩放。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1955-L1966)（注释写明「only exposes one global time scale」）、[B6_SCENE_ADVANCED.md](B6_SCENE_ADVANCED.md) §6（98-103）。
- **状态**：`active`

## 8. Collision 接触点是 AABB 近似

- **现象**：`ContactPoint` 是两颗 AABB 重叠盒的中心，`ContactNormal` 是穿透最浅的那条轴，`ImpactForce` 是相对速度在该轴的投影。旋转网格、凸多面体、斜坡会被包围盒放大，接触点可能落在可见网格之外。
- **影响范围**：`Collision` 的 3 个扩展输出端口；贴花、精确法线、斜面滑动物理。
- **原因**：节点是 bounds-based，不是 Bullet manifold；接触代理由 AABB 推导。
- **证据**：[KX_FunctionNode.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNode.cpp#L904-L937)（注释写明「contact proxy, not a Bullet manifold」）、[B6_SENSOR.md](B6_SENSOR.md) §7（66-74）。
- **状态**：`active`

## 9. std::regex 不防灾难性回溯

- **现象**：病态模式（如 `(a|aa)+$` 这类「交替 + 量词」）可能让 `StringRegexMatch` / `StringRegexReplace` 长时间占用 CPU，表现为掉帧甚至假死。节点不做超时或步数限制；嵌套量词与可匹配空串的组体已在编译前由 #30 拒绝，其余形状仍会放行。被拒时 `Rejected=true`，与普通不匹配可区分（#30）。
- **影响范围**：字符串域两个正则节点；对用户输入直接套复杂正则的图。
- **原因**：使用 C++11 `std::regex`（ECMAScript 语法），没有超时机制，也未引入 PCRE/RE2。
- **证据**：[B6_STRING_LIST.md](B6_STRING_LIST.md) §3「正则限制」（66-73）、[KX_FunctionNodesB6String.cpp:146-300](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp#L146-L300)（`IsSafeRegexPattern` 的形状白名单）、`:476-505` / `:549-570`（非法 Pattern 只捕获 `std::regex_error` 并置 `Rejected=true`）。
- **状态**：`active`

## 10. AES 状态：仅 XOR，XOR 只防窥不防攻击

- **现象**：`algorithm=aes` 打印 `AES 待接入 (no BLI AES entry point); refusing to fall back to XOR` 并走失败路径，不触发 `OnDone`；只有 `xor` + Base64 可用。XOR 已知明文可恢复密钥流、无完整性校验。
- **影响范围**：文件域 `EncryptData` / `DecryptData`；需要真实加密的存档/通信。
- **原因**：`BLI_` 无加密入口，仓库未链接 OpenSSL；引入 OpenSSL 评估为 5 天且带来体积与 GPL 组合的许可评审。
- **证据**：[KX_FunctionNodesB6File.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6File.cpp#L266-L292)、[B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §2/§3（83-116）、`native_b6file.py:9-15 / 30-40`（枚举标签 `AES (not available)` + tooltip）。
- **状态**：`active`（不会静默把 xor 当 aes 执行，这一点是缓解，但 AES 仍不可用）

## 11. 屏幕栈是进程级共享状态

- **现象**：多个图、多个对象共用同一份 `UIScreenStack`；一个图 `pop` 会影响另一个图的界面。热重载不清空也不迁移该栈。
- **影响范围**：UI 域 `UIScreenStack`；多图/多场景同时使用 UI 栈的项目。
- **原因**：`B6ScreenStackShared()` 返回函数内 `static` 状态，生命周期是进程而非图实例。
- **证据**：[KX_FunctionNodesB6UI.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6UI.cpp#L790-L798)、[B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §UIScreenStack 与 §限制（78-80、95-97）。
- **状态**：`active`

## 12. audaspace 设备级限制

- **现象**：无 3D 设备（`WITH_AUDASPACE` 关闭、无 `I3DDevice`、headless/无声卡）时，`PlaySound3D` 退化为普通 2D 播放，位置不生效，只有 stderr 诊断；`SetAudioDoppler` 是设备级单值，跨对象严格隔离做不到；`SetAudioAttenuation` 的 `Model` 也只能设备级。
- **影响范围**：3D 音频 4 节点；无声卡/工具链环境；多对象同时要求不同多普勒的场景。
- **原因**：audaspace 的 3D 能力挂在 device 上，device 不提供可用的 3D 子设备时节点层无法补偿。
- **证据**：[B6_AUDIO.md](B6_AUDIO.md) 第 87 行与 §7 第 2/3/6 条（164-168）、[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1937-L1975)（doppler 的对象局部近似）。
- **状态**：`active`

## 13. StringLength / StringPad 的字节版与字符版差异

- **现象**：`StringLength("中文")` 返回 **6** 不是 2；`StringPad` 的 `Length` 与 `PadChar` 按字节处理。用 `StringSplit`/`StringTrim` 处理多字节 UTF-8 分隔符会按字节拆开。
- **影响范围**：B2 `StringLength`、B6 `StringPad`、`StringSplit`/`StringJoin`/`StringTrim`；中文/日文/emoji 文本。
- **原因**：历史节点按 UTF-8 字节串实现，改语义会破坏既有工程图，因此保持字节口径。
- **证据**：[B6_STRING_LIST.md](B6_STRING_LIST.md) §0（7-17）、[KX_FunctionNodesB6String.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp#L121-L293)（新增 `StringLengthChars` / `StringPadChars` 按码点）。
- **状态**：`mitigated`
  缓解点：新增 `StringLengthChars` / `StringPadChars` 两个字符口径节点（UTF-8 解码失败按 1 字符容错），中文/emoji 场景有正确入口。
  未变部分：字节版节点仍按字节，且这是刻意保留的兼容行为。

## 14. LogicGate 旧 IR 必须重导出（含真实报错文本）

- **现象**：升级前按旧端口导出的图不能直接复用，编译期报：

  ```
  Output port count mismatch for 'LogicGate': IR declares 1, registered signature has 3
  ```

  不是运行时静默错，也不会跑出一半再失败。
- **影响范围**：所有包含 `LogicGate` 且使用旧 IR 的工程。
- **原因**：端口从 `A/B(Bool) → Value(Bool)` 改为 `A/B(trigger) → Result/OnTrue/OnFalse`；`TRIGGER_SNAPSHOT_NODE` 的 `allow_fewer_outputs=false`。
- **证据**：[B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §0（6-22）、[KX_GraphCompiler.cpp](../source/gameengine/GameLogic/GameNodes/KX_GraphCompiler.cpp#L276-L292)（端口数严格比对）。
- **状态**：`mitigated`
  缓解点：升级即失败 + 报错文本可读 + 文档给出「打开旧图并重新导出」的处理步骤。
  未变部分：旧 IR 仍然不能直接编译，必须重导出。

## 15. Collision 输出触发槽不可回读

- **现象**：直接读 `Collision` 自身的输出触发槽始终为 `false`，但下游节点能正常收到 `OnEnter` / `OnStay` / `OnLeave`。
- **影响范围**：`Collision` 的自环/调试；依赖「读槽位」而不是「下游投递」的测试与逻辑。
- **原因**：调度器对「有触发输出、无触发输入」的起始节点先 `ClearNodeOutputTriggers`，再用空 `Execute` 跑一遍；节点自身槽位被清空，但 Tick 阶段产生的下游投递已在队列里。
- **证据**：[KX_ExecutionContext.cpp](../source/gameengine/GameLogic/GameNodes/KX_ExecutionContext.cpp#L189-L263)（tick 前清槽 + 传播时清槽）、[B5_RERUN_REPORT.md](B5_RERUN_REPORT.md) §6.2 第 3 条与 §6.3 第 1 条（116、124）。
- **状态**：`active`

## 16. Collision 1 帧采样延迟

- **现象**：对象刚装配/刚换目标的第一帧不会触发；`Object` 输入读到的是上一帧的值。连续状态迁移不受影响，但「刚生效那一帧」会漏。
- **影响范围**：`Collision` 的起始帧；靠首帧触发启动的流程。
- **原因**：有状态 tick 在值节点之前执行，所以 tick 阶段拿到的 `Object` 是上一帧采样结果。
- **证据**：[B5_RERUN_REPORT.md](B5_RERUN_REPORT.md) §6.2 第 4 条与 §6.3 第 2 条（117、125）。
- **状态**：`active`

## 17. SetBoneTransform：无动作时当帧生效，有动作时仍被同帧动作更新覆盖

- **现象**：姿态数据写入后会立即调用 `ApplyPoseImmediate()` 重算骨架并标记 depsgraph，因此**该骨架没有 active action 时，当前帧画面即可看到新姿态**。但图 tick 仍在 `KX_KetsjiEngine::Render()` 的 `UpdateAnimations()` 之前（[KX_KetsjiEngine.cpp:591](../source/gameengine/Ketsji/KX_KetsjiEngine.cpp#L591)、[:752](../source/gameengine/Ketsji/KX_KetsjiEngine.cpp#L752)）；若该骨骼同时被播放中的动作驱动，同一帧稍后的动作更新会重新写入 fcurve 并覆盖本次变换。该通道在动作播放期间可能整帧丢失，而不是延迟一帧。
- **影响范围**：动画域 `SetBoneTransform`；同帧 IK/挂点/相机跟随；动作播放中的外部骨骼覆盖。
- **原因**：`ApplyPoseImmediate()` 已绕过 `m_lastapplyframe` 守卫并触发 `BKE_pose_where_is` + `ID_RECALC_TRANSFORM`，但不修改 `m_lastapplyframe`（否则同帧动作更新会跳过骨架评估）；真正的“后置姿态覆盖层”需要改 `BL_ActionManager` 的混合管线。
- **证据**：[BL_ArmatureObject.cpp:425-445](../source/gameengine/Converter/BL_ArmatureObject.cpp#L425-L445)、[KX_GameAdaptersEngine.cpp:1663-1701](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1663-L1701)、[B6_ANIMATION.md](B6_ANIMATION.md) §9。
- **状态**：`resolved`（B6.9-A，2026-11-06）。后置姿态覆盖层已接在混合管线之后：`BL_ArmatureObject::SetPoseOverride()` 登记每骨骼覆盖，`BL_Action::UpdateArmatureAnimation()` 在 `ProcessArmatureBlending()` 之后、`ApplyPose()` 之前调用 `ApplyPoseOverrides()` 重放。真实引擎证据：同一 `Spin` 动作下，`action` 对照运行 rotX 漂移 0→1.000，`lock` 运行 152 帧 rotX 恒 0.000；两边动作归一化都推进到 0.993。见 [B6_ANIMATION.md](B6_ANIMATION.md) §10.1。
  - 旧文字中「`BL_ActionManager` 未提供通道级锁定」的结论不再成立：不需要 API 级锁定，覆盖层本身在混合之后重放即可；`BL_ActionManager` 仍只到 layer 级。

## 18. BlendActions 判 full，但固定占用 layer 0/1、只支持两个动作

- **现象**：只能混合 A/B 两个动作，A 固定写 layer 0、B 固定写 layer 1，会覆盖这两层已有动作；`factor` 必须在 [0,1]，越界直接报错。
- **影响范围**：`BlendActions`；需要三层以上混合，或需要保留 layer 0/1 既有动作的图。
- **原因**：引擎层用两个真实 layer 权重实现 `(1-factor)A + factorB`，混合本身不是近似；限制来自接口只暴露两个动作与两个固定层。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1635-L1657)、[B6_ANIMATION.md](B6_ANIMATION.md) §3 与 §7.2（38、81、120-124）。
- **状态**：`active`（能力判 `full`，限制是接口范围，不是精度近似）
- **B6.9-A 补充（2026-11-06）**：实测发现此前权重方向与文档相反（实际是 `factor·A + (1-factor)·B`，factor=0 得到 B）。实现已改为 layer 1 权重 `1-factor`，真实引擎 `factor=0 / 0.5 / 1` 对应骨骼 rotX `0.0 / 0.5 / 1.0`，中间值严格位于两端之间；`check_anim.py blend` 3 条断言全过。文档与实现现在一致，**不是近似**。

## 19. 导航无 Recast，agent_radius / agent_height 不生效（**已消解：2026-11-07 源码复核**）

- **现象（2026-11-07 前）**：`BuildNavMesh` 接收并校验 `agent_radius` / `agent_height`，但改这两个值不会改变可行走区域；场景新增/移动障碍后，重建仍是同一份静态 navmesh，不会按障碍重烘焙。
- **影响范围**：`BuildNavMesh` / `QueryPath`；动态障碍、不同体型 agent。
- **原因（已消解）**：原判断「引擎只编译 Detour 查询、没有 Recast 光栅化」不成立。运行时入口 `KX_NavMeshObject::BuildNavMeshRuntime`（`source/gameengine/Ketsji/KX_NavMeshObject.cpp:1015-1109`）调用 `KX_NavMeshBuilder::Bake`（`source/gameengine/Ketsji/KX_NavMeshBuilder.h:87-277`），含 `recast_rasterizeTriangles`(:150)、`recast_buildCompactHeightfield`(:159)、`recast_buildRegions`(:176)、`recast_buildContours`(:184)、`recast_buildPolyMesh`(:196)、`recast_buildPolyMeshDetail`(:209)，再由 `dtCreateNavMeshData`(:284-305) 打包；`agent_radius`/`agent_height` 进入 `walkableRadius`/`walkableHeight`(:113-115)，真实生效。
- **证据**：[KX_NavMeshObject.cpp:1015-1109](../source/gameengine/Ketsji/KX_NavMeshObject.cpp#L1015-L1109)、[KX_NavMeshBuilder.h:87-277](../source/gameengine/Ketsji/KX_NavMeshBuilder.h#L87-L277)、[B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.4 / §5。
- **状态**：`removed`（2026-11-07 源码复核）

## 20. 载具：轮组命名已可配置，但悬挂参数固定、悬挂长度写死 0.2

- **现象**：`wheel_names` 可以指定任意物体作为轮组（C.1.2 起），但逐轮悬挂参数仍不可配：悬挂长度固定 `0.2`，连接点/悬挂方向/轮轴由底盘世界变换换算得到；`wheel_names` 只解决「哪些物体是轮子」。
- **影响范围**：`CreateVehicle` / `VehicleControl`；需要不同悬挂软硬/行程的载具。
- **原因**：引擎侧复用 `btRaycastVehicle` 封装，节点层没有逐轮悬挂参数入口。
- **证据**：[B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.1（50-55，`wheel_names` 优先 + 「悬挂长度固定 0.2」）。
- **状态**：`active`
  子项「轮组只能按 Template 命名约定」已 `removed`：`wheel_names` 非空时按逗号分隔名逐个解析，找不到会指出具体名字并整体失败（同上 51-52）。

## 21. TouchLegacy / OverlapQuery 是球近似（含半径/阈值语义）

- **现象**：`TouchLegacy` / `OverlapQuery` 把每个对象当成以世界位置为球心、半径 `max(|scale.x|, |scale.y|, |scale.z|)` 的球；只当 `distance <= threshold + radius` 时计入（`threshold` 默认 `0.1`）。命中与网格形状、旋向无关，相机等无碰撞体对象也会被遍历。命中对象始终第一个回调，并带精确射线点/法线。B3 原生 `Touch` 不受本条约束：它走 `RayCastFromScreen`（`KX_FunctionNodesB3.cpp:626-634`），输出真实命中点/法线/距离。
- **影响范围**：`TouchLegacy` / 拾取范围；需要真实碰撞体或网格重叠的交互。
- **原因**：物理世界没有「点重叠」入口，`KX_PhysicsAdapter` 也没有同步 `contactTest`，节点只能退化为射线点附近的球查询。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1034-L1088)、[B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §8（192-209）。
- **状态**：`active`

## 22. 32 位句柄上限（超过即拒绝，不截断）

- **现象**：音频 `Handle` 端口是 `KX_VALUE_INT`（32 位 `int`），`uint64_t` 句柄超过 `INT32_MAX` 时无法表达。实现在分配端把 id 限制在 `1..INT32_MAX` 循环复用，节点边界对越界值判失败（`Handle=0`、`OnStarted`/`Found=false`）并回收已创建的声源，而不是静默截断或停错声音。
- **影响范围**：`StartSound` / `PlaySoundByName` / `StopSound` 及一切音频句柄；极端高并发播放。
- **原因**：`KX_RuntimeValue.data.i` 是 `int`，值池没有 int64 类型；扩 int64 要改值池、`port_contract` 标签与 `enum_consistency`。
- **证据**：[KX_FunctionNodesB3.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB3.cpp#L352-L377)（边界守卫）、同文件 `:391-413 / :497-518`（越界回收）、[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L2421-L2468)（分配上限 + 跳过在用 id）、[B6_AUDIO.md](B6_AUDIO.md) §8（170-183）、[SOUND_BRIDGE_EVAL.md](SOUND_BRIDGE_EVAL.md) §5。
- **状态**：`mitigated`
  缓解点：分配上限 + 边界拒绝 + 越界回收，截断与「停错声音」已不可能发生；10 条 E2E 断言（`/tmp/b5_sound.log`，69 PASS / 0 FAIL）。
  未变部分：端口仍是 32 位，没有 int64 值类型（评估后不做，见 FOREVER §2 第 9 条）。

## 23. Polar 布局在 design_size 缩放下的命中仍不准

- **现象**：canvas 设置 `design_size` 后，Polar 子元素显示正确，但点击命中点与显示位置对不上（HitTest 历史上使用未缩放坐标）。
- **影响范围**：Polar 布局 + 点击/拖动；带设计分辨率缩放的 UI 画布。
- **原因**：`KX_UILayoutPolarElements` 只把环上坐标写回子元素 `position`，命中测试整体仍走未缩放坐标，`design_size` 缩放未纳入 HitTest。
- **证据**：[KX_UIElement.h](../source/gameengine/GameLogic/GameNodes/KX_UIElement.h#L605)（布局写回）、同文件 `:165 / :248-250`（`design_size`）、[B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §Polar 命中修正（141-155，明确「未处理 canvas `design_size` 缩放下的命中」）。
- **状态**：`active`

## 24. AddAnimationEvent：已绑定动作层并对齐渲染帧，但仍为节点侧轮询

- **现象**：节点现在按 `Action` 名称解析 `BL_ActionManager` 的实际 layer，并用“当前引擎帧时间”预测本帧 `UpdateAnimations()` 将写入的帧（不是上一帧的陈旧帧）；循环回绕后会重新 armed，因此同一事件每循环触发一次。`OnEvent` 仍在图 tick 的触发阶段产生，不是引擎动画回调；同名动作同时存在于多层时绑定层号最小的一层；反向播放（负 speed）只按正向跨越检测，不触发反向事件。
- **影响范围**：动画域 `AddAnimationEvent`；多层同名动作、倒放事件。
- **原因**：`BL_ActionManager` 没有事件通道，节点仍需状态机轮询；本轮新增的 `FindLayerByActionName()` / `GetActionFrameAtTime()` 只提供只读的层与帧预测。
- **证据**：[KX_FunctionNodesB6.cpp:444-476](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L444-L476)（状态与绑定）、[:508-566](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L508-L566)（tick 跨越/回绕）、[:710](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L710)（注册）、[b61_e2e.cpp:731-770](../tests/b61_e2e.cpp#L731-L770)（E2E 断言）、[B6_ANIMATION.md](B6_ANIMATION.md) §9。
- **状态**：`active`（较原条目已改善：真实 layer 绑定、渲染帧对齐、循环重复触发）
- **本轮核对（2026-11-06）**：当前源行号为 [KX_FunctionNodesB6.cpp:606-654](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L606-L654)（tick）与 `:800-803`（端口）、`:869-871`（注册），E2E 为 [b61_e2e.cpp:971-985](../tests/b61_e2e.cpp#L971-L985)，日志 `tests/verify_gap/evidence/b61.log:40-42`。上方旧行号仅作历史保留。

## 25. 场景父子不共存：MountChildScene / UnmountChildScene 是 push/pop 等价

- **现象**：`MountChildScene` 加载子场景后，父场景变为非活跃；`UnmountChildScene` 只是重新加载父场景返回，子场景**从未被释放**。父子场景不能同时更新、同时渲染，也没有场景栈深度。
- **影响范围**：场景域 `MountChildScene` / `UnmountChildScene`；依赖父子场景共存（如 UI 场景叠在关卡场景上仍各自 tick）的图。
- **原因**：引擎只保留一个活跃场景——`KX_KetsjiEngine::LoadScene` 走 `ReplaceScene`，而 `RemoveScene` 在 EEVEE 集成期被显式禁用（带注释的提前 `return`）。没有「多活跃场景」或「场景栈」语义，节点只能实现 push/pop。
- **证据**：[KX_FunctionNodesB6Scene.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Scene.cpp#L258-L264)（文件注释写明单活跃场景与 push/pop 等价）、同文件 `:309-339`（Mount）、`:341-369`（Unmount，只 `LoadScene(parent)` 不释放子场景）、[KX_KetsjiEngine.cpp](../source/gameengine/Ketsji/KX_KetsjiEngine.cpp#L1341-L1348)（`RemoveScene` 禁用）、`tests/b69_e2e.cpp:686` / `:724-762`（E2E）。
- **状态**：`active`（节点判 `full`：限制来自引擎，节点已做到最好）

## 26. 本轮重分类（approximate → full）带出的限制，按域归类（2026-11-06 追加）

以下条目是 2026-11-06 重分类的落点，**不重排、不合并**上方既有编号；“限制条目”列为落点编号。
重分类只改文档分类，不改源码、不改断言总数。

| 域 | 节点 | 原分类 | 现分类 | 限制条目 | 可核依据 |
|---|---|---|---|---|---|
| 动画 | `AddAnimationEvent` | approximate | full | #24 | 引擎无帧事件通道：GraphTick（`KX_KetsjiEngine.cpp:591`）早于 `UpdateAnimations`（`:752`），引擎侧事件**最早下一个 GraphTick 可见**，节点侧预测是正确实现 |
| 音频 | `SetAudioAttenuation` | approximate | full | #12 | `I3DHandle.h:38-229` 无 `setDistanceModel`，模型只在 `I3DDevice.h:139`（设备级） |
| 音频 | `SetAudioDoppler` | approximate | full | #12 | 只有 `I3DDevice::setDopplerFactor`（`I3DDevice.h:127`），`I3DHandle` 无多普勒 API |
| 音频 | `AudioZone` | approximate | full | #1 | audaspace 无 zone API；节点是 listener 位置循环环境音的等价实现（`KX_GameAdaptersEngine.cpp:2764-2786`） |
| 音频 | `SetAudioBusVolume` | approximate | full | #1 | audaspace 无 bus 概念；节点是「命名 gain 组」等价实现（`KX_GameAdaptersEngine.cpp:2788-2807`） |
| 场景 | `LoadSceneAsync` | approximate | full | #5 | 引擎同步 `ReplaceScene`。该节点**不异步**——加载在调用帧内完成，进度是反馈而非流式 |
| 场景 | `MountChildScene` | approximate | full | #25 | 单活跃场景，push 等价、父子不共存 |
| 场景 | `UnmountChildScene` | approximate | full | #25 | pop 等价，返回父场景但不释放子场景 |
| 场景 | `TransitionScene` | full | full | — | UI canvas 淡入淡出遮住原子的 `ReplaceScene`，不构成缺陷 |
| 场景 | `SetSceneTimeScale` | approximate | full | #7 | 引擎只有一个全局 `SetTimeScale` |
| 文件 | `EncryptData` | approximate | full | #10 | XOR 仅防窥；AES 因密码学设计缺口（缺认证）进 FOREVER（`DEFERRED_FOREVER.md` §3 #3） |
| 文件 | `DecryptData` | approximate | full | #10 | 同上 |
| 字符串 | `StringRegexMatch` | approximate | **full** | #30 | 嵌套量词/可匹配空串组体在编译前被拒，`Rejected` 输出可区分拒绝与不匹配；其余模式仅受长度限制，无墙钟超时（`(a\|aa)+$` 这类仍可能慢） |
| 字符串 | `StringRegexReplace` | approximate | **full** | #30 | 同上，与 Match 共用同一份 `IsSafeRegexPattern`；输出 `Result` + `Rejected` |

**加权能否等于 100%**：本表所涉节点内 `approximate` 为 0（全局 327 口径 `A=4`，见下）。正则两项（`StringRegexMatch` / `StringRegexReplace`）
已补「嵌套量词 + 可匹配空串组体」拒绝与 `Rejected` 端口（见 #30）并改判 `full`；音频 `PlaySoundPlaylist` 两处静默截断
已补 stderr 诊断、改判 `full`，剩余容量边界登记为 #29。`ListSort` / `ListFilter` / `ListMap` 与流程 `While`
已在第二轮升 `full`（见 #27 / #28）。
**全局三口径（327 注册表，`TypeCount()` 实测；当前树 2026-11-07）**：

- `N = 327`；`F = 301` / `A = 4` / `U = 7` / `P = 15`；恒等式 `301 + 4 + 7 + 15 = 327`。
- `D = F + A = 301 + 4 = 305`。
- 严格 A = `F / D` = `301 / 305` = **98.69%**。
- 加权 B = `(F + 0.5A) / D` = `(301 + 0.5×4) / 305` = `303 / 305` = **99.34%**。
- 覆盖 C = `(F + A + P) / (D + U + P)` = `(301 + 4 + 15) / (305 + 7 + 15)` = `320 / 327` = **97.86%**。
- 历史冻结快照 `RUN_ID=20261106-224926`：`N=325 / F=299 / A=4 / U=7 / P=15 / D=303`（98.68% / 99.34% / 97.85%）；差异为新增 `ConstString` / `ConstVector` 两个 IR 内部辅助节点（与 `ConstInt` 先例一致计 `F`）。
- **适用域**：`unsupported` 7 与 `pending-device` 15 已剔出严格/加权分母（覆盖口径含 P、不含 U），不代表「所有节点都好」。这里统计的是 **C++ 实现正确率**。**已无 Python 回退路径**：B7.5 删除 Python 逻辑执行器与「原生运行时」开关后，`nl_` 组件只警告并跳过。**但 E2E 入口覆盖 ≠ 用户点 P 的路径**：多数 harness 直连 `KX_FunctionNode`/`KX_CompiledGraph`，绕过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口。因此用户默认体验（建树 → 点 P → 运行时调度）的证据缺口仍需实机验证补齐，不能声称已被 E2E 覆盖。

**增量账作废**：上一轮使用的增量账分母（只含本轮动过判定的节点，含其回滚态）已作废，本文不引用它的任何百分比；以 327 全注册表口径为准（判定沿革见 [FLOW_MATH_STRING_RESCORE.md](FLOW_MATH_STRING_RESCORE.md) §4.2）。

## 27. 列表节点语义：`ListSort` 的 `Key` 只读直接属性；`ListFilter`/`ListMap` 只接受数值元素

- **现象**：
  - `ListSort`：`Key` 取元素上的一个直接属性名（数值或字符串）；写 `a.b`、`x[0]` 等嵌套路径取不到值，`KeyOf` 退化为按元素自身值排序，不报错。
  - `ListFilter` / `ListMap`：表达式变量只有 `x`（元素 `AsFloat()`）；字符串/对象/列表元素按 0 处理；结果超过 4096 时静默停止。
- **影响范围**：`ListSort`（对象列表按子字段排序）、`ListFilter` / `ListMap`（字符串列表筛选/映射、超长列表）。
- **原因**：`KeyOf` 只做一次直接属性查找，没有路径解析器；表达式解析器只实现数值子集（`|| && == != <= >= < > + - * / %`、一元 `- !`、括号、常量、`x`），没有字符串比较/函数/三目；列表池统一上限 4096。
- **文档依据**：[B6_STRING_LIST.md](B6_STRING_LIST.md#L151-L156) §7 限制 2（数值语义）、限制 3（`key` 直读）、限制 4（4096 静默截断）均已写明。
- **证据**：[KX_FunctionNodesB6String.cpp:486-517](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp#L486-L517)（`KeyOf`）、`:519-558`（排序）、`:757-786`（Filter）、`:796-826`（Map）、`:563-747`（表达式解析）；[KX_RuntimeValue.h:204-217](../source/gameengine/GameLogic/GameNodes/KX_RuntimeValue.h#L204-L217)（非数值 → 0）。
- **状态**：`active`（三个节点判 `full`：偏差是文档化语义，不是实现缺失）

## 28. While 的条件与输入端口在 OnEnter 快照，循环体不能逐次改变条件

- **现象**：`While` 在 `OnEnter` 内用内部计数器 `i` 一次性求出总迭代数；`Threshold` / `Step` 等输入端口值在同一时刻快照。循环体后来对 `i` / `n` 的修改**不改变迭代次数**。所有迭代在同一逻辑帧内同步展开。
- **影响范围**：流程控制 `While`；按命令式直觉在循环体内递增/修改条件的图。
- **原因**：调度器把循环体作为 pending 序列在同帧展开，条件求值发生在任何循环体节点执行之前；改成逐次重读条件需要把循环体变成跨帧状态机/协程。
- **证据**：[KX_FunctionNode.cpp:1530-1609](../source/gameengine/GameLogic/GameNodes/KX_FunctionNode.cpp#L1530-L1609)（条件求值循环）、`:1544-1569`（端口快照）、`:1611-1626`（按迭代派发）；[KX_ExecutionContext.cpp:308-318](../source/gameengine/GameLogic/GameNodes/KX_ExecutionContext.cpp#L308-L318)（每个迭代的子树跑完才推进下一个）。
- **状态**：`active`（节点判 `full`：条件在帧首快照且文档已写明）

## 29. `PlaySoundPlaylist` 固定容量：最多 16 轨、单轨名字 47 字节

- **现象**：一次传入超过 16 轨时，只保留前 16 轨，其余在 `OnEnter` 丢弃——但会发一次 stderr 诊断
  `[PlaySoundPlaylist] N tracks requested, only 16 kept, M dropped`；单轨名字超过 47 字节时截断到 47 字节，
  首个越界轨发一次 `[PlaySoundPlaylist] track i name longer than 47 bytes, truncated`。
  两条诊断各每次 `OnEnter` 至多一条；16 轨以内且名字 ≤47 字节时零 stderr。
- **影响范围**：音频 `PlaySoundPlaylist`；需要 >16 首的顺序/循环/随机播放，或名字 ≥48 字节的音源。
- **原因**：状态块 `KX_PlaylistState` 是 raw memory（不跑构造函数），只能放定长 `char names[16][48]` 与 `int order[16]`；
  改成长列表要换状态块模型并重算尺寸预算。这是既定设计选择，不是遗漏。
- **证据**：[KX_FunctionNodesB6Audio.cpp:179-180](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Audio.cpp#L179-L180)（常量）、
  `:277-303`（两条诊断）；[b69_e2e.cpp:573-597](../tests/b69_e2e.cpp#L573-L597)（16 轨零 stderr）、
  `:599-640`（17 轨诊断 + 前 16 轨照常播放）、`:642-675`（名字截断）。
- **状态**：`active`（节点判 `full`：上限是设计限制，静默缺诊断已消除）

## 30. 正则拒绝嵌套量词和可匹配空串的组体；其余模式无墙钟超时

- **现象**：`StringRegexMatch` / `StringRegexReplace` 共用 `IsSafeRegexPattern`（`KX_FunctionNodesB6String.cpp:146-300`），
  在构造 `std::regex` 前单趟 `O(n)` 扫描 + 括号深度栈判定。量词修饰的分组在**两个条件任一成立**时被拒：
  - 组体**整体就是一个无界重复**（末尾无界量词覆盖整个组体）：`(a+)+`、`(a*)*`、`(.*)*`、`(a+){2,}`、跨层传播的 `((a+))+`；
  - 组体**可匹配空串**：`(a?)+`、`(a*)+`、`(b?)*`。
  连续量词（`a**`、`a++`、`a?*`、`a*+`）仍直接拒绝。放行的常见写法包括
  `(\d+,)+`、`(ab?)+`、`(ab)+`、`(a|b)*`、`(a|aa)+`、`[+*?]`、`\+`、`a+?`、`a+?`、`^abc$`、`(?:ab)+`。
  命中时每个实例只写一次 `[Regex] unsafe pattern rejected: nested quantifier`，**不编译正则**。
- **`Rejected` 输出端口（2026-11-06）**：拒绝现在与「真不匹配」可区分。
  - `StringRegexMatch` 输出 `Matched` / `Groups` / `Rejected`（索引 0/1/2）；
  - `StringRegexReplace` 输出 `Result` / `Rejected`（索引 0/1）。
  - 语义：拒绝（守卫命中、源串超长、非法 Pattern）→ `Rejected=true` + `Matched=false` / `Result` 原样返回；
    正常不匹配 → `Rejected=false` + `Matched=false`；正常匹配 → `Rejected=false` + `Matched=true`。
  - stderr 那行仍是补充信息：该行**每个节点实例只打一次**（`state->warned`，Match `:468-473` / Replace `:541-546`），
    跨帧保留、只有重建图实例才重置，且与「输入超长 / 非法 Pattern」诊断共用同一个 `warned` 标志。
- **保守拒绝**：判定只看「整体无界重复」与「可匹配空串」，所以
  **`(a+b*)+`、`(aa+)+` 这类「组体含内部量词但结尾不是纯重复」的模式会被放行**（见 `KNOWN_LIMITATIONS.md` #9）。
  反过来，`(a+)?`、`(a?)*` 仍会被拒——有意保守，代价是部分合法模式不可用。
- **影响范围**：字符串域两个正则节点；对不可信输入直接套复杂正则的图。
- **限制原文**：正则节点拒绝嵌套量词和可匹配空串的组体，其余模式受源串 ≤4096、模式 ≤512 限制。(a|aa)+ 这类交替项前缀重叠的模式不受保护，40 字符输入可达 29.7 秒，44 字符超 45 秒。std::regex 无墙钟超时，属引擎层限制。
- **交替型回溯爆炸不在保护范围**：`(a|aa)+$`、`(a|a?)+$` 这类「交替 + 量词」会被放行，**无墙钟超时**；
  账本线实测 40 字符可达 29.7 秒、44 字符超 45 秒（本补丁不改引擎能力）。
- **原因**：`std::regex` 没有超时或步数限制，判定器只是编译前的形状白名单，不是 ReDoS 免疫。
- **证据**：[KX_FunctionNodesB6String.cpp:67-100](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp#L67-L100)（`IsBraceQuantifier`）、
  `:106-144`（栈帧与原子登记）、`:146-300`（`IsSafeRegexPattern`）、`:468-475`（Match 拒绝分支）、`:541-548`（Replace 拒绝分支）、
  `:442-446` / `:507-519`（端口数组与 `static_assert`）；[b62_e2e.cpp:1316-1406](../tests/b62_e2e.cpp#L1316-L1406)
  （10 条验收断言，含 `(\d+,)+`、`(ab?)+`、`(a?)+`、拒绝后下一帧、`(a|aa)+$` 22 字符 <100ms）。
- **状态**：`active`（节点判 `full`：嵌套量词与空串组体已拒绝，剩余的是引擎层无超时，已写明不承诺线性时间）

## 31. 资源在 Player 模式下未注册

- **严重度**：严重
- **现象**：同一 `.blend` 在编辑器内按 `P` 有声音（说明音频数据块确实存在）；改用独立 `blenderplayer` 运行同一文件时报 `not found`，资源取不到。节点侧对应 `[PlaySound3D] unknown sound` 的失败路径（`KX_FunctionNodesB6Audio.cpp:169-171`）。
- **影响范围**：音频、贴图、字体等**一切依赖外部资源的节点**；编辑器内正常，独立 player 下缺失。
- **触发条件**：仅独立 `blenderplayer <file>.blend`；编辑器按 `P` 正常，不触发。
- **现象已确认；根因未确认**，候选：
  1. `BL_SceneConverter` 在 Player 模式下没有把 Sound 数据块注册进 `maggie->sounds`（名称去 `SO` 前缀的注册逻辑见 `source/gameengine/Converter/BL_Converter.cpp:214-218`；Player 侧的具体缺口尚未定位到行，**不作已证实结论**）；
  2. 注册名与节点里填的名字不一致（`SO` 前缀、扩展名、大小写、空格）；
  3. 资源未打包，或相对路径在 player 的启动 cwd 下失效。
- **最小自查判据**：同一 `.blend` 用编辑器 `P` 与 `blenderplayer <file>.blend` 各跑一次；对比 stderr 打印的 `not found` 资源名，并核对 `Outliner → Blender File → Sounds` 的 ID 名（去掉 `SO` 前缀）。若「编辑器出声、player 报 not found」，即命中本项。
- **与 327 口径的关系**：**不改变 327 口径的六量与三个百分比**。理由：编辑器内按 `P` 正常，说明节点实现本身无缺失；问题在运行时资源加载器层，不是节点语义缺失，因此不进 `approximate`，也不进 `unsupported` 或 FOREVER。
- **状态**：`active`（严重；用户决定留作核心遗留问题，不再纠缠）

## 32. Print 空槽不再输出 `None`，改为 `<Print: Value input unset>`

- **严重度**：轻微（行为变更，非缺陷）
- **现象**：`Print` 的 String `Value` 端口未连线、且 IR 中没有对应 `ConstString` / `ConstInt` / `ConstVector` 常量时，旧实现把「未设置」当字面量 `None` 打印（与用户真正想打印的字符串 `"None"` 无法区分）；本轮修复后改为打印 `<Print: Value input unset>`。
- **源码位置**：`source/gameengine/GameLogic/GameNodes/KX_FunctionNode.cpp`（`FuncPrint` NONE 槽诊断分支；注释明确「Never report that as the literal string None」）。
- **回归证据**：`tests/b667_default_values.cpp:174-177`（`TestPrintWithoutConstant`）期望由 `"None"` 改为 `<Print: Value input unset>`；单批重跑 `RUN_ID=20261107-222555` → `b667_defaults 6 / 0`，`TOTAL assertions=6 failures=0`。
- **影响范围**：仅「端口未连线 / 无常量」的 Print 输出文本；端口有真实常量或连线时行为不变。
- **与 327 口径的关系**：不改变六量（`N=327 / F=301 / A=4 / U=7 / P=15`）。
- **状态**：`active`（有意保留的新诊断行为；旧 `None` 输出不再出现）

---

## 附：核对方式

| 条目 | 可复跑证据 |
|---|---|
| 1 / 12 | `grep -n "SetAudioBus\|bus_gain_\|I3DDevice" source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp`；[B6_AUDIO.md](B6_AUDIO.md) §4.5 |
| 2 | [B6_NETWORK.md](B6_NETWORK.md) §9；`KX_NetworkAdapterEngine.cpp:642-679` |
| 3 / 5 / 7 | [B6_SCENE_ADVANCED.md](B6_SCENE_ADVANCED.md) §3/§4/§6；`/tmp/b63_nodes_e2e.log` |
| 6 | `/tmp/b662_jit.log`（`reduction=3.49%`、`FAIL ... 40%`） |
| 8 / 15 / 16 | [B5_RERUN_REPORT.md](B5_RERUN_REPORT.md) §6.2/§6.3；`KX_FunctionNode.cpp:904-937` |
| 9 / 13 / 30 | [B6_STRING_LIST.md](B6_STRING_LIST.md) §0/§3/§7；`KX_FunctionNodesB6String.cpp:146-300`；`tests/b62_e2e.cpp:1316-1406` |
| 10 | [B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §2/§3 |
| 11 / 23 | [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §UIScreenStack/§Polar；`/tmp/c7_ui.log` |
| 14 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §0 |
| 17 / 18 / 24 | [B6_ANIMATION.md](B6_ANIMATION.md) §3/§4/§7.2/§7.3；`/tmp/b61_e2e.log` |
| 19 / 20 | [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.1/§3.4 |
| 21 / 22 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §8；[B6_AUDIO.md](B6_AUDIO.md) §8；`/tmp/b5_sound.log` |
