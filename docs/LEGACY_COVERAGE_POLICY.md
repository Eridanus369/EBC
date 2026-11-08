# LEGACY_COVERAGE_POLICY — 覆盖率口径与可达性政策（Phase D.2）

本文件定义「legacy 覆盖率」的所有分母、算式与不可达原因。任何报告数字前必须先指明
分母；**不同分母之间不可相加、不可换算**。

## 0. 结论速览

| 口径 | 分母 | 分子 | 当前值 | 状态 |
|---|---|---|---|---|
| legacy 全量 | 290 | 69 | **23.8%** | 已落地，可复算 |
| 用户可感知 | 233 | 69 | **29.6%** | 已落地，可复算 |
| semantic | 360 | 146 | **40.6%** | 已落地，可复算（本轮 audit） |
| 已注册节点（严格） | 286 | 252 | **88.1%** | 已落地，见 `CAPABILITY_WEIGHT_AUDIT.md` |

**290 的 70% 目标在「端口契约严格 + 不做引擎层改造」的约束下不可达**：结构性不可迁的
75 条 + 剩余 89 条“基础域”全部迁完，上限也只有 **158/290 = 54.5%**（§3 给出算式与类别）。

## 1. 四个口径的算式

### 1.1 legacy 全量 290

```
290 = 已迁移 69 + 未迁移 221
```

- 分子 69：`native_audit.py` 输出的 `migrated_direct=38 + migrated_translated=30 + migrated_rewritten=1`。
- 分母 221：`docs/deferred_matrix.json` 的 `total_deferred`。
- 证据：`docs/B6_LEGACY_COMPAT.md` §1（`69/290 = 23.8%`）、`deferred_matrix.json` 字段 `total_deferred=221`。
- 含义：旧 uplogic API 的**类行数**，一行 = 一个 legacy 节点类；包含永远不会迁的 57 条。

### 1.2 用户可感知 233

```
233 = 290 − 57
```

- 57 = `docs/DEFERRED_FOREVER.md` 机器区间内的 FOREVER 行（渲染内部件 40 + 废弃 17）。
- 证据：`docs/B6_LEGACY_COMPAT.md:175`「233 = 290 − 57，未做分母修饰」。
- 含义：把“明确不做、用户也基本感知不到”的行剔除后，剩下的**可评估集合**。
- 注意：233 只扣 57 条 FOREVER，**不扣**新增的 8 条 FOREVER（#8 Recast 已实现移出；它们不在机器区间，见该文件 §6）。

### 1.3 semantic 360

```
semantic_total = legacy_semantics(去重旧语义) + native_only_semantics(新增原生语义)
semantic_pct   = (migrated_semantics + native_only_semantics) / semantic_total
```

- 算式实现在 `scripts/startup/bge_gamenodes/generator/native_audit.py:217-256`（`_semantic_coverage`）
  与 `:338-341`（百分比计算）；去重键由 `_normalise_semantics`（`:200-215`）生成。
- 本轮实测（`/tmp/d2_audit.log`）：
  `SEMANTIC COVERAGE covered=146 total=360 pct=40.6% legacy_semantics=283 native_only_new=77 deduped=43`；
  即 `360 = 283 + 77`、`40.6% = 146 / 360`（`40.5556%` 四舍五入到一位）。
- `docs/B5_REMAP.md:26`（`130/360 = 36.1%`、`:77` `132/360 = 36.7%`）是同一分母的历史快照，
  只是当时 `covered` 更少（130/132），不能与本轮的 146 混用。
- 含义：**语义集合**（旧语义 ∪ 仅存在于新原生的语义），不是 legacy 行集合的子集，
  也不能用 290 加减得到。

### 1.4 已注册节点 286

```
286 = 14 个内建节点 + 270 个表驱动节点 + 2 个补充注册
```

- 证据：`docs/CAPABILITY_WEIGHT_AUDIT.md` §1（`14 + 270 + 2 = 286`）。
- 分档：full 252 / approximate 22 / **mock-verified 8** / **unsupported 4**（`252 + 22 + 8 + 4 = 286`）；
  严格 `252/286 = 88.1%`、加权 `267/286 = 93.4%`、覆盖 `282/286 = 98.6%`
  （同一文件 §0 的主口径）。旧文写的「其余 12」把 8 条 `mock-verified` 吞进了未分档部分，
  会让人误以为它们「不支持」；旧 92.0%/95.8% 是 mock 证据补齐前的数，两处均已作废。
- 含义：**运行时注册的 `type_id` 数量**，是新节点体系自身的规模。

### 1.5 补充：另外两个容易混入的数字

| 数字 | 含义 | 来源 |
|---|---|---|
| 226 | 编辑器里可见的节点条目数 | `CAPABILITY_WEIGHT_AUDIT.md:62` |
| 223 | `port_contract.py` native 表里的节点数 | 同上 |

**这四个数（226 / 223 / 286 / 290）互不相等，均不可互换。**

## 2. 290 与 286 是不同集合（重点）

| 维度 | 290 | 286 |
|---|---|---|
| 计数对象 | 旧 uplogic API 的**类行**（历史接口清单） | 新运行时注册的 **`type_id`** |
| 是否包含已删除接口 | 是（含 VR/FMod/子网络等废弃类） | 否 |
| 是否按语义去重 | 否 | 否 |
| 与对方的交集 | 仅 69 条迁移映射构成“一个 legacy 行 ↔ 一个原生节点”的对应 | 同左 |
| 可否相加 | **不可** | **不可** |
| 可否换算 | **不可**（`290 − 69` 得到的 221 是 legacy 行，不是原生节点） | **不可** |

一条 legacy 行甚至可能对应多个原生节点（如 `LogicNodeApplyTransform` → `SetPosition/SetRotation/ApplyImpulse`），
一个原生节点也可能吸收多条 legacy 语义（如 `Touch` 同时覆盖 `NLActionMousePickNode` 等）。
因此“290 里迁了 69 条”与“286 里 252 条 full”描述的是两套完全不同的东西，**禁止并列求和或求平均**。

## 3. 为什么 290 全量的 70% 不可达

### 3.1 算式

70% × 290 = **203** 条。当前 69 条，差 **134** 条；而可评估集合只有 233 条，
其中未迁 **164** 条，也就是说 **164 条里要迁掉 134 条（81.7%）** 才能达标。

### 3.2 164 条的结构分解（可复算）

```bash
python3 - <<'EOF'
import json, pathlib, collections
ex = set(l.strip() for l in pathlib.Path('docs/DEFERRED_FOREVER.md').read_text()
         .split('<!-- excluded:start -->')[1].split('<!-- excluded:end -->')[0].splitlines() if l.strip())
rows = [r for r in json.load(open('docs/deferred_matrix.json'))['rows'] if r['bl_idname'] not in ex]
def cat(r):
    i, c, d = r['bl_idname'], r['nl_class'], r['domain']
    if d == 'UI': return 'ui'
    if d == '网络': return 'net'
    if d == '物理高级': return 'phys'
    if d == '文件': return 'file'
    if any(k in i or k in c for k in ('Sound','List','Dict','Variable')): return 'pyobj'
    return 'basic'
print(collections.Counter(cat(r) for r in rows))   # basic 89 / pyobj 28 / phys 16 / file 15 / ui 13 / net 3
EOF
```

结果：`basic 89 / pyobj 28 / phys 16 / file 15 / ui 13 / net 3`，合计 164。
其中 `basic` 才是「理论上可以靠新增 `*Legacy` 类型补齐端口」的部分。

### 3.3 下限与上限

- 若 75 条结构类别（pyobj 28 + phys 16 + file 15 + ui 13 + net 3）**一条都不迁**，
  只迁完 89 条 `basic`：`(69 + 89) / 290 = 158/290 = **54.5%**`。
- 要达到 70%（203 条），在 89 条 `basic` 全迁的前提下，仍需从 75 条结构类别里再迁 **45 条**，
  而这 45 条每一类都需要引擎层新能力（UI 数据模型、Bullet 约束面、Python 对象引用、整场景存档），
  不是“补端口”能解决的。所以 70% **不可达**。

### 3.4 六类具体不可迁移类别（每类给真实类名）

| 类别 | 行数 | 真实类名（示例） | 为什么不可迁（节点图无法表达的部分） |
|---|---|---|---|
| ① 已废弃执行模型 | 8（属 57 FOREVER） | `NLActionInstallSubNetwork`（ULInstallSubNetwork）、`NLActionExecuteNetwork`（ULExecuteSubNetwork）、`NLStartLogicNetworkActionNode`（ULStartSubNetwork） | 5.3 已无子网络/逻辑树执行对象，迁移目标实体不存在；重实现等于重写被删除的子系统 |
| ② VR / FMOD 专属插件 | 9（属 57 FOREVER） | `NLGetVRControllerValues`（ULGetVRControllerValues）、`NLGetVRHeadsetValues`（ULGetVRHeadsetValues）、`LogicNodeFModStartEvent`（FModStartEventNode） | 构建未链接 VR device 与 FMOD，没有可调用的设备/库；接口层就缺失 |
| ③ 依赖 Python 对象生命周期 | 28 | `LogicNodeStartSound`（StartSoundNode）、`NLActionStopSound`（ULStopSound）、`NLActionListVariables`（ULListVariables）、`NLSetDictKeyValue`（ULSetDictKey） | legacy 端口返回/接收 Python 对象（`nl_type=29`）；`KX_RuntimeValue` 只有原语与对象指针，没有“带引用计数的 Python 值”语义，也无法在节点图里表达生命周期 |
| ④ 渲染后处理 / filter / evaluated node tree | 40（属 57 FOREVER） | `NLAddFilter`（ULAddFilter）、`NLSetGeometryNodeValue`（ULSetNodeSocket）、`NLSetMaterialNodeValue`（ULSetMatNodeSocket） | 操作的是 GPU filter 栈与 evaluated 节点树，需 depsgraph 重评估与数据块所有权；节点层没有稳定句柄，强接会把帧率与渲染后端耦合进逻辑图 |
| ⑤ UI 控件树 | 13 | `LogicNodeCreateUIButton`（ULCreateUIButton）、`LogicNodeCreateUICanvas`（ULCreateUICanvas）、`LogicNodeSetUIWidgetAttr`（ULSetUIWidgetAttr） | 依赖 UI 数据模型（控件树/布局），且是**编辑器控件**而非游戏对象；节点值语义无法承载控件句柄与布局状态 |
| ⑥ 整场景状态 / 网络 socket | 18 | `NLActionSaveGame`（ULSaveGame）、`NLActionSaveVariables`（ULSaveVariableDict）、`LogicNodeLocalServer`（ULLocalServer）、`LogicNodeSendNetworkMessage`（ULSendNetworkMessage） | 存档是**整场景快照**（不是节点值）；网络是 socket 句柄 + 消息对象，属进程资源，`nl_type` 无对应标签（`SOCKET_TYPE_TO_TAG` 无 29 即此因） |
| ⑦ 物理高级（载具/约束/导航） | 16 | `NLActionAddPhysicsConstraint`（ULAddPhysicsConstraint）、`NLCreateVehicleFromParent`（ULCreateVehicle）、`NLActionNavigate`（ULMoveToWithNavmesh） | Bullet 的约束/载具面未在 `KX_PhysicsAdapter` 暴露；导航是 Recast/Detour 专题（B6.9，3~5 天），不是端口映射 |

> 行数说明：① ② ④ 属于 57 FOREVER；③ ⑤ ⑥ ⑦ 属于 164 gap。类别按
> `domain` + `bl_idname` 关键词划分（脚本见 3.2），同一行只归一类，无重叠。

### 3.5 现实可达率

`basic` 89 条也不是“全都能迁”：B6.5a 对 30 条高频候选的实测成功率是 **16/30 ≈ 53%**
（`B6_LEGACY_COMPAT.md` §4）。按同一成功率外推，89 条约可迁 47 条：
`(69 + 47) / 290 ≈ 116/290 = 40%`，与 semantic 口径 40.6% 同一量级。
**这就是“290 全量的 70% 不可达”的量化结论，不是悲观修辞。**

## 4. 高 / 中 / 低频的定义与对应分母

### 4.1 判定依据

频率来自 `deferred_matrix.json` 的 `frequency` 字段，全部为**离线推定**，
依据写在每行的 `frequency_basis`，`confidence` 标注置信度
（`classification_rules.frequency`：「离线推定，依据官方示例与社区教程中的常见度；无法联网复核」）。

| 档位 | 判定依据（`frequency_basis` 原文摘录） | 行数（221 中） |
|---|---|---|
| 高 | 「运动/属性/碰撞/条件/计时/对象查询是 UPBGE 入门模板（FPS、平台跳跃、第三人称）的必备节点」；「Move To/Rotate To/Follow/Mouse Look 在运动与摄像机控制教程中高频」；「射线/拾取类在瞄准、点击、AI 视线教程中高频」 | 21（高\|易 14 + 高\|中 7） |
| 中 | 「声音开始/停止/音量节点在官方音频示例中常用」；「UI 控件属性读写/移动属教程后半段的进阶用法」；「载具/物理约束/导航网格/角色物理属专题示例，普通教程很少使用」 | 121（中\|易 83 + 中\|中 36 + 中\|难 2） |
| 低 | 「材质/几何节点树与曲线接口属高级渲染专题，仅在材质动画与后处理示例出现」；「存档/读档/变量持久化是进阶教程主题，入门模板不涉及」 | 62（低\|难 28 + 低\|中 24 + 低\|易 10） |
| 极低 | 「旧接口或专属插件路径，当前 5.x 社区教程几乎不出现」 | 17（与「废弃」17 行同一组） |

### 4.2 三档对应的报告分母（项目约定）

| 档位 | 报告分母 | 理由 | 当前数字（分子/分母） |
|---|---|---|---|
| 高 | **290（全量）** | 高频项是“用户最可能立刻感知”的部分，报告时用最保守的全量分母，避免任何修饰 | `69/290 = 23.8%` |
| 中 | **233（可感知）** | 剔除永不移植的 57 条后，中频项才有比较意义 | `69/233 = 29.6%` |
| 低 / 极低 | **360（semantic）** | 低/极低行多数属结构性不可迁，逐行算 290 没有意义，改用“语义是否已存在”汇总 | `146/360 = 40.6%`（本轮 audit 可复算，见 §5） |

> 注意：这张表是**报告口径映射**，不是“三档各自的迁移率”。三档的分子目前都是同一个 69
> （整体迁移数），分母不同才产生三个百分比。若要按档位单独出分子/分母，需要重跑
> `native_audit.py` 并按 `frequency` 分组，本轮没有做。

### 4.3 三个数字的分母与分子（必须逐字引用）

```
23.8% = 69 / 290      （legacy 全量，第一层口径）
29.6% = 69 / 233      （用户可感知，第四层口径，233 = 290 − 57）
40.6% = 146 / 360     （semantic 口径，本轮 audit 实测）
```

## 5. semantic 40.6%：本轮 audit 实测，可复算

- **状态：可复算。** 复跑命令：

  ```bash
  build_linux/bin/blender -b --factory-startup --python-expr \
    "import sys; sys.path.append('scripts/startup'); \
     from bge_gamenodes.generator.native_audit import main; main()"
  ```

  本轮输出（`/tmp/d2_audit.log`）：

  ```
  SEMANTIC COVERAGE covered=146 total=360 pct=40.6% legacy_semantics=283 native_only_new=77 deduped=43
  ```

- 算式：`total = 283 + 77 = 360`；`covered = migrated_semantics + native_only_new(77) = 146`，
  反推 `migrated_semantics = 146 − 77 = 69`；`146 / 360 = 40.5556%`，输出保留一位小数即 **40.6%**。
- 与旧说法的差异：此前本文曾标注为「用户给定值（算式未给）」，理由是仓库内没有该次运行日志；
  本轮重跑后已有可复算结果，**旧说法作废**。`docs/B5_REMAP.md:26` 的 `130/360 = 36.1%`
  只是同一分母更早的快照，不能与本轮 146 混用。
- 注意：40.6% 的分母是**语义集合**（360），不是 290/233；360 会随新增原生语义变化，
  引用时必须同时写 `legacy_semantics` / `native_only_new` 两个数。

## 6. 复算清单

| 检查 | 命令/位置 | 期望 |
|---|---|---|
| 290 | `deferred_matrix.json.total_deferred` + `B6_LEGACY_COMPAT.md` §1 | `69 + 221 = 290` |
| 233 | `B6_LEGACY_COMPAT.md:175` | `290 − 57 = 233` |
| 57 FOREVER 完整、无已迁入 | `DEFERRED_FOREVER.md` §0 校验脚本 | `57 []` |
| 286 | `CAPABILITY_WEIGHT_AUDIT.md` §1 | `14 + 270 + 2 = 286` |
| 164 分解 | 本文 §3.2 脚本 | `basic 89 / pyobj 28 / phys 16 / file 15 / ui 13 / net 3` |
| semantic 40.6% | 本文 §5 复跑命令 → `/tmp/d2_audit.log` | `146/360`、`legacy_semantics=283`、`native_only=77` |

## 7. 使用规则（给写报告的人）

1. 先写分母，再写百分比；未写分母的数字一律视为无效。
2. `290`、`233`、`286`、`360`、`226`、`223` 不得相加、不得换算、不得求平均。
3. 任何“XX% 已覆盖”若不能在本文件 §1/§6 找到算式，必须标注「不可复算」并补跑 audit；
   本文件现有的 23.8% / 29.6% / 40.6% 均已可复算。
4. 引用 FOREVER 条目时引用 `DEFERRED_FOREVER.md`，不要重新解释分母（该文件 §6 是唯一口径说明）。
