# 动画域重打分（B6.9-A 收口）

> 本文件是动画域在**新口径**下的唯一打分依据。所有数字只在本文件登记，**不再往
> `NUMBER_CONSISTENCY_AUDIT.md` 附录 B 追加编号**（该附录由 K 线单点重写）。

## 新口径（原文）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：

| 子类 | 定义 | 计入分母 | 计入分子 |
|---|---|---|---|
| **full** | 实现完成 + Headless/Mock E2E 通过 + 无已知限制 | 是 | 1.0 |
| **approximate** | 实现完成 + Headless E2E 通过 + 有明确限制（文档写清） | 是 | 0.5 |
| **unsupported** | 引擎层不支持（附 `路径:行号` 可核证据） | 否（进 FOREVER） | — |
| **pending-device** | 实现完成 + Mock E2E 通过 + 需真实设备验证 | 是 | 1.0（真机待验单列） |

`pending-device` 必须同时满足：① 节点有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless
验证的是具体设备/驱动依赖并给出 API 位置。**不是「我们没测」。**

### 本轮重分类口径（2026-11-06，照抄上级原文）

- `D = full + approximate`；`unsupported` 与 `pending-device` **不进分母**；
- `approximate` 只表示「节点实现本身不完整」；**限制来自引擎能力时记 `full`**，限制写 `KNOWN_LIMITATIONS`；
- `pending-device` 剔除分母需同时满足：① 有 Mock/Headless E2E 通过日志；② 有可核理由说明为何 headless 验不了。

> 本口径取代上方旧表「pending-device 计入分母并计 1.0」的写法。动画域没有 `pending-device`，
> 因此分母仍为 8。

## 结论（并排两个数）

| 口径 | 分子/分母 | 动画域 |
|---|---|---|
| 旧严格（上一轮报法，unsupported 计入分母） | 7 / 11 | **63.6%** |
| 新严格（上一轮，unsupported 剔除分母、approximate 不计分子） | 7 / 8 | **87.5%** |
| 新加权（上一轮，unsupported 剔除分母、approximate 0.5） | 7.5 / 8 | **93.75%** |
| **本轮重分类后严格**（`AddAnimationEvent` → `full`，D = full + approximate = 8） | 8 / 8 | **100%** |
| **本轮重分类后加权**（同上，D = 8） | 8 / 8 | **100%** |

- 11 个动画节点：`full` 8、`approximate` 0、`unsupported` 3、`pending-device` 0（本轮重分类后）。
- 3 个 `unsupported` 进 FOREVER、不进分母 → 新分母 8。
- 本域没有 `pending-device`：8 个可计分节点全部有 Headless/Mock 通过记录；其中 3 个还额外有真实
  `blenderplayer` 证据（比 Mock 更强）。
- 旧 63.6% 与新加权 93.75% 的差全部来自口径（把 3 个 FOREVER 移出分母 + 近似按 0.5 计），
  **没有新增实现**。
- 本轮 `AddAnimationEvent` 由 `approximate` 重分类为 `full`：偏差来自引擎没有帧事件通道
  （GraphTick `KX_KetsjiEngine.cpp:591` 早于 `UpdateAnimations` `:752`），节点侧预测是正确实现；
  限制保留在 `KNOWN_LIMITATIONS` #24。**没有新增实现**。

## 逐节点表

节点清单来自运行时注册表（`KX_FunctionNodesB6.cpp` 的 `kB6Specs`）。下表的
`KX_FunctionNodesB6.cpp` 行号为 **2026-11-06 15:45 快照**（该文件有并行改动，
以 `B6_SPEC(...)` 的节点名定位为准）。

| # | 节点 | 分类 | 依据（路径:行号 / 日志行） | 限制说明 |
|---|---|---|---|---|
| 1 | PlayActionByLayer | full | `KX_FunctionNodesB6.cpp:857`；`tests/verify_gap/evidence/b61.log:29` `PASS PlayActionByLayer layer/blend_in (0/0)` | 无 |
| 2 | BlendActions | full | `KX_FunctionNodesB6.cpp:859`；`evidence/b61.log:30`；真实引擎 `evidence/blend_0.00.log`/`blend_0.50.log`/`blend_1.00.log` → `REG-A2-01/02/03` | 只支持两个动作、固定占用 layer 0/1；`factor` 方向已修：`(1-factor)·A + factor·B` |
| 3 | SetBoneTransform | full | `KX_FunctionNodesB6.cpp:861`；`evidence/b61.log:31`；真实引擎 `evidence/lock.log` vs `evidence/action.log`（152 帧 rotX 恒 0.000，对照动作 0→1.000） | 仅 `OBJ_ARMATURE`；覆盖保持到下一次显式设置（无自动清除） |
| 4 | SetBoneWeight | unsupported | `KX_FunctionNodesB6.cpp:863`；失败路径 `evidence/b61.log:5`；动作入口无 bone 参数 `BL_ActionManager.h:63`；GPU 缓存只 hash dvert 指针 `draw_armature_skinning.cc:984-985` | FOREVER（`DEFERRED_FOREVER.md` §2 #6） |
| 5 | PlayBoneAction | unsupported | `KX_FunctionNodesB6.cpp:865`；失败路径 `evidence/b61.log:6`；`BL_ActionManager.h:63` 无 bone 参数；`BL_Action.cpp:596` 调 `obj->ApplyAction(...)`；`BL_ArmatureObject.cpp:549-553` 用 `animsys_evaluate_action` 整动作评估，无路径过滤 | FOREVER（`DEFERRED_FOREVER.md` §2 #7），详见 §3.4 说明 |
| 6 | SetAnimationSpeed | full | `KX_FunctionNodesB6.cpp:867`；`evidence/b61.log:32` `PASS SetAnimationSpeed 1.5` | 无 |
| 7 | AddAnimationEvent | **full**（本轮重分类） | `KX_FunctionNodesB6.cpp:869-871`（spec）、`:606-654`（`TickAddAnimationEvent`）、`:800-803`（端口）；`evidence/b61.log:40-42`（未到帧不触发 / 到帧触发 / 循环重臂）；依据判定见 §3.5 | 限制来自引擎：无帧事件通道，GraphTick（`KX_KetsjiEngine.cpp:591`）早于 `UpdateAnimations`（`:752`），引擎侧事件**最早下一个 GraphTick 可见**；节点只能预测本次渲染期将写入的帧；同名多层取最小层（`BL_ActionManager.cpp:199-207`）；倒放无事件（`:632-651`）。进 `KNOWN_LIMITATIONS` #24 |
| 8 | GetAnimationFrame | full | `KX_FunctionNodesB6.cpp:872`；`evidence/b61.log:33-34`（Frame 42 / Normalized 0.5） | 取不到当前动作返回 0（已定义） |
| 9 | SetActionTimeScale | unsupported | `KX_FunctionNodesB6.cpp:875`；失败路径 `evidence/b61.log:35` + `:4` 显式理由；`BL_Action` 只有 `m_speed`、`BL_ActionManager.h:102` 只有 `SetActionSpeed` | FOREVER（`DEFERRED_FOREVER.md` §3 2026-11-06 条） |
| 10 | GetActionNormalizedTime | full | `KX_FunctionNodesB6.cpp:877`；`evidence/b61.log:36`；真实引擎 `evidence/speed_1.00.log`/`speed_2.00.log`（单调、循环归零、倍速帧位 ×2） | 无 |
| 11 | GetBoneTransform | full | `KX_FunctionNodesB6.cpp:880`；`evidence/b61.log:37`；真实引擎 `evidence/lock.log`（姿态读回） | 只读；仅 `OBJ_ARMATURE` |

## 三类合计

| 分类 | 数量 | 进分母 |
|---|---|---|
| full | 8 | 是 |
| approximate | 0 | 是 |
| unsupported | 3 | 否（FOREVER） |
| pending-device | 0 | — |
| **合计** | **11** | 分母 D = 8 |

## 3.4 第三类 `unsupported` 查清

上一轮报的 3 个 `unsupported` 逐一点名：

1. **`SetBoneWeight`** —— 骨骼权重。
   证据：`BL_ActionManager::PlayAction(...)` 无 bone 参数（`BL_ActionManager.h:63`）；GPU 静态蒙皮
   缓存 key 只含 `mesh_orig->deform_verts().data()` 指针（`draw_armature_skinning.cc:984-985`），
   原地改 `MDeformVert::weight` 不会让缓存失效。→ FOREVER。
2. **`SetActionTimeScale`** —— 独立时间缩放。
   证据：`BL_Action` 只有 `m_speed`（速度与帧推进同一个量），无正交时间缩放字段；
   `BL_ActionManager` 只有 `SetActionSpeed`（`BL_ActionManager.h:102`）。→ FOREVER。
3. **`PlayBoneAction`** —— 就是此前没点名的那一个。
   证据链：
   - 动作入口只有整对象：`BL_ActionManager::PlayAction(const std::string&, float, float, short layer, short priority, float blendin, short play_mode, float layer_weight, short ipo_flags, float playback_speed, short blend_mode)`（`BL_ActionManager.h:63`），**没有 bone / channel / path 过滤参数**；
   - `BL_ActionManager` 全文无 `bone`/`channel`/`fcurve` 命中（仅 `BL_ActionManager.cpp:193` 注释里的 SGNode 同步）；
   - 实际求值：`BL_Action::UpdateArmatureAnimation()` → `obj->ApplyAction(m_action, ...)`（`BL_Action.cpp:596`）；
   - `BL_ArmatureObject::ApplyAction()` 用 `animsys_evaluate_action(&ptrrna, action, slot_handle, &evalCtx, false)`（`BL_ArmatureObject.cpp:549-553`），该 API 评估整个 action，**没有按骨骼路径裁剪的入口**；
   - 节点侧 `PlayBoneAction` 当前显式失败并回报 `[PlayBoneAction] per bone action playback is not exposed by the engine`（`evidence/b61.log:6`）。

**能不能补**：不能做成真正的引擎能力。可行的只有「保存其余通道 → 整动作求值 → 还原其余通道」的
旁路，这会绕过 `BlendInPose` 的 blend-in 快照、破坏约束与父子通道的当帧求值，属于伪造语义，
不采纳。→ 保持 FOREVER（`DEFERRED_FOREVER.md` §2 #7）。
（作者侧可用「只给单骨骼打键的 action + `PlayActionByLayer`」达到近似效果，但那是内容工作流，
不是引擎的 per-bone API。）

## 3.5 `AddAnimationEvent`：approximate → full（引擎无帧事件通道，节点侧预测是正确实现）

**结论：重分类为 `full`，限制进 `KNOWN_LIMITATIONS` #24。** 它的限制不是节点实现缺陷，
而是**引擎没有帧事件通道**：GraphTick 与 `UpdateAnimations` 的先后顺序固定
（`KX_KetsjiEngine.cpp:591` 在 `:752` 之前），节点只能用
`GetActionFrameAtCurrentEngineTime()` 预测本次渲染期动作更新将写入的帧。三问裁定：

1. 实现是否正确？是。3 条 E2E（`b61.log:40-42`）覆盖未到帧 0 次 / 到帧 1 次 / 循环回绕后 2 次；
2. 偏差来自哪？引擎调度顺序（逻辑 tick 早于渲染期动作更新），不是节点逻辑；
3. 能否改代码消除？不能。改成引擎回调，事件最早只能在**下一个** GraphTick 被节点看到，
   等于把「同帧即时」换成「晚一帧」——改引擎也只能二选一。

以下证据支撑上述裁定：

### 1. 钉死点：真正被写入的帧只在 `BL_Action::Update()` 内部成立

- 逻辑图 tick 早于渲染期动作更新：`KX_KetsjiEngine.cpp:591`（`NextFrame()` 内
  `scene->GraphTick(...)`）→ `:752`（`Render()` 调 `UpdateAnimations(scene)`）→
  `:940-956`（`KX_KetsjiEngine::UpdateAnimations`）。GraphTick 时动作还没算。
- 帧号唯一权威位置是 `BL_Action::Update()`（`BL_Action.cpp:448-480`）里的
  `UpdateActionTiming`（`:487-496`）→ `SetLocalTime`（`:338-346`）与
  `HandleFrameWrapping`（`:498-527`）。
- 因此节点只能调 `BL_Action::GetFrameAtTime()`（`BL_Action.cpp:267-299`）外推；它的输入是
  上一帧快照 `m_prevUpdate`/`m_localframe` 与 `m_starttime`/`m_speed`。
  「预测 == 实际写入」的前提是：本渲染帧恰好执行一次 Update，且两者之间时序状态不变。

### 2. 这个前提在现有引擎里可被公开节点打破

- **动画限帧**：`RESTRICT_ANIMATION` 门控（`KX_KetsjiEngine.cpp:943-952`，由
  `LA_Launcher.cpp:171` 打开）时，部分逻辑帧不执行动作更新——逻辑已前进，画面没前进。
- **一个渲染帧多个逻辑子步**：`KX_KetsjiEngine.cpp:501-605` 每个子步都 GraphTick，
  动作更新只在随后的 `Render()` 发生一次。
- **同一 tick 内先改时序再读事件**：`SetActionFrame` / `SetActionSpeed`
  （`BL_ActionManager.cpp:70-76`、`:93-99`；节点 `KX_FunctionNodesB3.cpp:156-163`、`:172-179`）
  会把 `m_calc_localtime` 置 false，下一帧 Update 走 `ResetStartTime`
  （`BL_Action.cpp:348-355`），预测基准作废；循环回绕还会重写 `m_starttime`（`:512-516`）。

### 3. 换成引擎回调只会更晚：最早下一个 GraphTick 才可见

若在 `BL_Action::Update()` 里派发跨越事件，事件最早只能在**下一个** GraphTick 被节点看到
（`KX_KetsjiEngine.cpp:591` 在 `:752` 之前），等于把「同帧即时」换成「晚一帧」。
同帧即时与精确写入不可兼得 → 这是架构取舍，不是缺实现；节点侧预测是唯一能保持同帧即时的手段。

### 4. 节点侧两处限制（可改，但改完不改定性）

- 层号按动作名解析：`FindLayerByActionName` 返回 `std::map` 升序**第一个**匹配层
  （`BL_ActionManager.cpp:199-207`），节点没有 `Layer` 输入
  （`KX_FunctionNodesB6.cpp:800-802`，端口仅 `OnEnter/Object/Action/Frame`）。
- 只检测正向跨越（`KX_FunctionNodesB6.cpp:632-651`）：负 speed 倒放不产生事件；
  `PlayActionByLayer` 固定 `play_mode=0`（`ACT_MODE_PLAY`），到 end 即钳住
  （`BL_Action.cpp:507-511`）。

### 5. 证据强度

`tests/b61_e2e.cpp:971-985` 的 3 条断言（未到帧 0 次 / 到帧 1 次 / 回绕后 2 次）由 **Mock
适配器**驱动真实节点图，属 Headless E2E；日志 `tests/verify_gap/evidence/b61.log:40-42`。
按本轮口径：实现正确 + Mock/Headless E2E 通过，偏差来自引擎能力 → **`full`** + 限制。

**后续若要产品级加固**：可加可选 `Layer` 输入（消除同名多层歧义）与反向跨越检测
（覆盖负 speed）。这两项都是节点层改动，改完后本节点**仍**受第 1~3 条的引擎时序限制；
本轮已判 `full`，两项属可选加固，不为分数改代码。

## 3 条 `BlendActions` 回归断言（本轮新增）

命令（真实引擎，`blenderplayer`）：

```bash
# 生成：make_anim.py -- blend 0.0 / 0.5 / 1.0
python3 tests/verify_gap/check_anim.py blend \
  tests/verify_gap/evidence/blend_0.00.log \
  tests/verify_gap/evidence/blend_0.50.log \
  tests/verify_gap/evidence/blend_1.00.log
```

结果（原文）：

```text
PASS REG-A2-01 factor=0 keeps pose A rotX=0.0000
PASS REG-A2-02 factor=1 keeps pose B rotX=1.0000
PASS REG-A2-03 factor=0.5 is strictly between A and B rotX=0.5000 strictly between 0.0000 and 1.0000
RESULT assertions=3 failures=0
```

- `REG-A2-03` 同时断言 `rotX == 0.5` 且 `min(A,B) < rotX < max(A,B)`，即**严格居中**，不是取其一。
- 日志：`tests/verify_gap/evidence/blend_0.00.log`、`blend_0.50.log`、`blend_1.00.log`。
- `b61` 同步为 `44 assertions / 0 failures`（K 冻结 `RUN_ID=20261106-224926`；旧 `evidence/b61.log` 的 32 条已被覆盖）。

## 数字与日志路径（不进附录 B）

| 数字 | 值 | 日志/来源 |
|---|---|---|
| 动画节点数 | 11 | `KX_FunctionNodesB6.cpp` 的 `kB6Specs` 第 828-882 行（本轮核对） |
| full / approximate / unsupported / pending-device | 8 / 0 / 3 / 0 | 本文件逐节点表（本轮重分类后） |
| 分母 D = full + approximate | 8 | 11 − 3（FOREVER） |
| 旧严格 | 63.6%（7/11） | 上一轮 `B6_ANIMATION.md` §6 |
| 上一轮新严格 / 新加权 | 87.5%（7/8） / 93.75%（7.5/8） | 重分类前 |
| 本轮严格 / 加权 | 100%（8/8） / 100%（8/8） | 本文件，D = 8 |
| A-1 真实引擎断言 | 3 PASS / 0 | `evidence/lock.log` + `evidence/action.log`；`check_anim.py lock` |
| A-2 回归断言 | 3 PASS / 0 | `evidence/blend_*.log`；`check_anim.py blend` |
| A-3 真实引擎断言 | 3 PASS / 0 | `evidence/speed_*.log`；`check_anim.py speed` |
| `b61` | **44 PASS / 0 FAIL** | K 冻结 `RUN_ID=20261106-224926`；旧 `evidence/b61.log` 为 32 |
