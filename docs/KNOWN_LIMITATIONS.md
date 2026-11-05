# KNOWN_LIMITATIONS — 已知限制清单（Phase D.2）

日期：2026-11-05
范围：已实现能力中**仍然存在**的不完整语义、精度近似、性能天花板与平台边界。
性质：纯文档。本文件不修改源码，所有证据均来自本轮审计与既有实现文档；状态判定见 §0。

## 0. 怎么用这份清单

本清单回答的是「**已经做出来的能力，哪里还不完整**」，不回答「哪些还没做」。
后者唯一出处是 [DEFERRED_FOREVER.md](DEFERRED_FOREVER.md)（57 条基线 + 本轮新增 9 条）。
一份报告里如果同时引用两者，必须分开写「未实现（FOREVER）」与「已实现但有边界（本清单）」。

### 0.1 三种口径不能混

| 口径 | 分母 | 当前主数字 | 说明什么 | 出处 |
|---|---|---|---|---|
| 能力域加权（已注册节点） | **286** | 严格 `252/286 = 88.1%`；加权 `267/286 = 93.4%`；覆盖 `282/286 = 98.6%` | 新节点体系自身的完成度 | [CAPABILITY_WEIGHT_AUDIT.md](CAPABILITY_WEIGHT_AUDIT.md) §0 |
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

### 0.3 总览（24 条：active 20 / mitigated 4 / removed 0）

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
| 19 | 导航无 Recast，agent 参数不生效 | `active` |
| 20 | 载具悬挂参数固定，悬挂长度写死 0.2 | `active`（子项「轮组命名约定」已 `removed`） |
| 21 | Touch 是球近似 | `active` |
| 22 | 32 位句柄上限 | `mitigated` |
| 23 | Polar 布局在 design_size 缩放下命中不准 | `active` |
| 24 | AddAnimationEvent 是节点侧逐帧轮询 | `active` |

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

- **现象**：病态模式（嵌套量词 + 长输入）可能让 `StringRegexMatch` / `StringRegexReplace` 长时间占用 CPU，表现为掉帧甚至假死。节点不做超时或步数限制。
- **影响范围**：字符串域两个正则节点；对用户输入直接套复杂正则的图。
- **原因**：使用 C++11 `std::regex`（ECMAScript 语法），没有超时机制，也未引入 PCRE/RE2。
- **证据**：[B6_STRING_LIST.md](B6_STRING_LIST.md) §3「正则限制」（63-67）、[KX_FunctionNodesB6String.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp#L191-L247)（非法 Pattern 只捕获 `std::regex_error`）。
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

## 17. SetBoneTransform 延迟更新（当帧不改渲染）

- **现象**：姿态数据写入成功，但当前帧画面不变；要等下一次骨架更新才看到效果。同帧截图或同帧依赖骨骼位置的逻辑会读到旧姿态。
- **影响范围**：动画域 `SetBoneTransform`；同帧 IK/挂点/相机跟随。
- **原因**：`BL_ArmatureObject::ApplyPose()` 受 `m_lastapplyframe != m_lastframe` 守卫，同一帧内不会重算 `BKE_pose_where_is`。
- **证据**：[BL_ArmatureObject.cpp](../source/gameengine/Converter/BL_ArmatureObject.cpp#L409-L423)、[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1663-L1697)、[B6_ANIMATION.md](B6_ANIMATION.md) §3 与 §7.3（39、82、126-139）。
- **状态**：`active`

## 18. BlendActions 判 full，但固定占用 layer 0/1、只支持两个动作

- **现象**：只能混合 A/B 两个动作，A 固定写 layer 0、B 固定写 layer 1，会覆盖这两层已有动作；`factor` 必须在 [0,1]，越界直接报错。
- **影响范围**：`BlendActions`；需要三层以上混合，或需要保留 layer 0/1 既有动作的图。
- **原因**：引擎层用两个真实 layer 权重实现 `(1-factor)A + factorB`，混合本身不是近似；限制来自接口只暴露两个动作与两个固定层。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L1635-L1657)、[B6_ANIMATION.md](B6_ANIMATION.md) §3 与 §7.2（38、81、120-124）。
- **状态**：`active`（能力判 `full`，限制是接口范围，不是精度近似）

## 19. 导航无 Recast，agent_radius / agent_height 不生效

- **现象**：`BuildNavMesh` 接收并校验 `agent_radius` / `agent_height`，但改这两个值不会改变可行走区域；场景新增/移动障碍后，重建仍是同一份静态 navmesh，不会按障碍重烘焙。
- **影响范围**：`BuildNavMesh` / `QueryPath`；动态障碍、不同体型 agent。
- **原因**：引擎只编译了 Detour 的查询路径，没有 Recast 的网格光栅化；没有 agent 尺寸，也没有动态障碍重烘焙。
- **证据**：[B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.4（73-79）、[DEFERRED_FOREVER.md](DEFERRED_FOREVER.md) §2 第 8 条（工期 3~5 天，属 B6.9）。
- **状态**：`active`

## 20. 载具：轮组命名已可配置，但悬挂参数固定、悬挂长度写死 0.2

- **现象**：`wheel_names` 可以指定任意物体作为轮组（C.1.2 起），但逐轮悬挂参数仍不可配：悬挂长度固定 `0.2`，连接点/悬挂方向/轮轴由底盘世界变换换算得到；`wheel_names` 只解决「哪些物体是轮子」。
- **影响范围**：`CreateVehicle` / `VehicleControl`；需要不同悬挂软硬/行程的载具。
- **原因**：引擎侧复用 `btRaycastVehicle` 封装，节点层没有逐轮悬挂参数入口。
- **证据**：[B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.1（50-55，`wheel_names` 优先 + 「悬挂长度固定 0.2」）。
- **状态**：`active`
  子项「轮组只能按 Template 命名约定」已 `removed`：`wheel_names` 非空时按逗号分隔名逐个解析，找不到会指出具体名字并整体失败（同上 51-52）。

## 21. Touch 是球近似（含半径/阈值语义）

- **现象**：`Touch` / overlap 把每个对象当成以世界位置为球心、半径 `max(|scale.x|, |scale.y|, |scale.z|)` 的球；只当 `distance <= threshold + radius` 时计入（`threshold` 默认 `0.1`）。命中与网格形状、旋向无关，相机等无碰撞体对象也会被遍历。命中对象始终第一个回调，并带精确射线点/法线。
- **影响范围**：`TouchLegacy` / 拾取范围；需要真实碰撞体或网格重叠的交互。
- **原因**：物理世界没有「点重叠」入口，`KX_PhysicsAdapter` 也没有同步 `contactTest`，节点只能退化为射线点附近的球查询。
- **证据**：[KX_GameAdaptersEngine.cpp](../source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp#L688-L753)、[B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §8（186-192）。
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

## 24. AddAnimationEvent 是节点侧逐帧轮询、固定 layer 0、触发一次后解除

- **现象**：事件不是引擎回调：节点每帧读当前帧，检测「上一帧 → 当前帧」是否正向跨过目标帧；跨越时触发 `OnEvent` 一次并 `armed=false`。固定读 layer 0；`Action` 输入只参与 armed 判定，不绑定具体动作；不支持循环/重复触发。
- **影响范围**：动画域 `AddAnimationEvent`；非 0 层动作、循环事件、依赖引擎事件时序的用法。
- **原因**：`BL_ActionManager` 没有事件通道，节点只能用状态机近似。
- **证据**：[KX_FunctionNodesB6.cpp](../source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6.cpp#L444-L488)（`armed`、`previous_frame < frame && current >= frame`、触发后解除）、`:645-646`（注册为有状态 tick 节点）、[B6_ANIMATION.md](B6_ANIMATION.md) §3 与 §4（44、46-53、86）。
- **状态**：`active`

---

## 附：核对方式

| 条目 | 可复跑证据 |
|---|---|
| 1 / 12 | `grep -n "SetAudioBus\|bus_gain_\|I3DDevice" source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp`；[B6_AUDIO.md](B6_AUDIO.md) §4.5 |
| 2 | [B6_NETWORK.md](B6_NETWORK.md) §9；`KX_NetworkAdapterEngine.cpp:642-679` |
| 3 / 5 / 7 | [B6_SCENE_ADVANCED.md](B6_SCENE_ADVANCED.md) §3/§4/§6；`/tmp/b63_nodes_e2e.log` |
| 6 | `/tmp/b662_jit.log`（`reduction=3.49%`、`FAIL ... 40%`） |
| 8 / 15 / 16 | [B5_RERUN_REPORT.md](B5_RERUN_REPORT.md) §6.2/§6.3；`KX_FunctionNode.cpp:904-937` |
| 9 / 13 | [B6_STRING_LIST.md](B6_STRING_LIST.md) §0/§3 |
| 10 | [B6_FILE_ADVANCED.md](B6_FILE_ADVANCED.md) §2/§3 |
| 11 / 23 | [B6_UI_ADVANCED.md](B6_UI_ADVANCED.md) §UIScreenStack/§Polar；`/tmp/c7_ui.log` |
| 14 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §0 |
| 17 / 18 / 24 | [B6_ANIMATION.md](B6_ANIMATION.md) §3/§4/§7.2/§7.3；`/tmp/b61_e2e.log` |
| 19 / 20 | [B6_PHYSICS_ADVANCED.md](B6_PHYSICS_ADVANCED.md) §3.1/§3.4 |
| 21 / 22 | [B6_LEGACY_COMPAT.md](B6_LEGACY_COMPAT.md) §8；[B6_AUDIO.md](B6_AUDIO.md) §8；`/tmp/b5_sound.log` |
