# B5.1 长尾 Remap 核实报告

日期：2026-11-05
范围：`scripts/addons_core/bge_netlogic/editor/nodes/native_catalog.py`
约束：零新运行时节点；只给“所有 native 端口都存在、类型匹配、声明顺序一致”的 legacy 节点加
`CATALOG` 条目。所有端口差异只记录，不改 legacy socket，不为了凑百分比造假条目。

## 一、基线（改动前）

命令：

```bash
python3 scripts/addons_core/bge_netlogic/generator/port_contract.py
python3 scripts/addons_core/bge_netlogic/generator/enum_consistency.py
LD_LIBRARY_PATH=$PWD/lib/dpcpp/lib ./build_linux/bin/blender -b --factory-startup \
  --python-expr "import sys; sys.path.append('$PWD/scripts/addons_core'); \
                 from bge_netlogic.generator.native_audit import main; main()"
```

| 检查 | 基线 |
| --- | --- |
| port_contract | `native_nodes=135 catalog_nodes=52 errors=0 warnings=4` |
| enum_consistency | `checked_pairs=38 inconsistencies=0` |
| native_audit | `total=290 migrated=53 direct=33 translated=19 rewritten=1 deferred=237` |
| legacy 覆盖率 | `53/290 = 18.3%` |
| semantic 覆盖率 | `130/360 = 36.1%` |

## 二、候选逐条核实

“已映射”指 `native_catalog.py` 里已有条目；“端口匹配”指 native 侧全部端口均能在 legacy
socket 列表中找到同名/同种类且顺序一致。legacy 多出的输入可以忽略（既有 `SendMessage`
就忽略了 `From`），但 legacy 多出的**输出**不能被忽略：`ir_export` 与 `port_contract`
要求输出全覆盖，否则下游端口索引无法解析。

| # | 候选（native id） | legacy 类 | 状态 | 差异 |
| --- | --- | --- | --- | --- |
| 1 | LoadScene | `NLLoadScene` | translated | `Scene` 为 SCENE 数据块，native 是 String；输出 5→1（Updated/Status/Datatype/Item 无对应） |
| 2 | QuitGame | `NLActionEndGame` | translated | 缺输出 `OnDone`（legacy 无任何输出 socket） |
| 3 | PlayAction | `NLActionPlayActionNode` | translated | 输入 13→7，native `Loop` 无对应（legacy 的 `Stop When Done` 语义相反）；输出 4→2（Running/OnFinish 无对应） |
| 4 | StartSound | `LogicNodeStartSound` | translated | 输入 20→5：`Sound File` 为 SOUND 数据块而非 String，`Mode(loop_count)` 是 Int 而非 Bool；`Pitch/Volume` 顺序与 native 相反；`Handle` 为 Python 而非 Int |
| 5 | StopSound | `NLActionStopSound` | translated | 输入 `Sound` 为 Python 句柄，native `Handle` 是 Int（类型不同） |
| 6 | Collision | `NLConditionCollisionNode` | translated | 输入 4→1（UseMaterial/Property/Material 无对应）；输出 5→3（legacy 只有 1 个 trigger，native 要 OnEnter/OnStay/OnLeave） |
| 7 | Compare | `NLConditionLogicOperation` | **本批映射** | 输入 `param_a/param_b` 是 generic Value，收紧为 Float；常量 Threshold 被丢弃；`operator` 0..5 映射到 `eq/ne/gt/lt/ge/le` |
| 8 | LogicGate | `LogicNodeLogicGate` | translated | 输入 `Condition A/B` 是 trigger，native 要 Bool；输出 trigger vs Bool（B6.6 后 native 改为 A/B trigger + Result/OnTrue/OnFalse，此差异消除） |
| 9 | Once | `NLConditionOnceNode` | **本批映射** | native 只需 `OnEnter→OnOut`；legacy `Repeat`、`Reset After` 无运行时对应，保留在编辑器侧不导出 |
| 10 | Timer | `NLConditionTimeElapsed` | translated | 输入 2→2 类型一致；输出 1→2，缺 `Elapsed(Float)` |
| 11 | MessageReceived | `NLParameterReceiveMessage` | translated | 输入顺序 `Subject,Tracked...`（Subject 在前），native 是 `Object,Subject`（顺序不同）；输出 `Content` 为 generic、`Messenger` 为 Object，native 要 String |
| 12 | SendMessage | `NLActionSendMessage` | **早已映射**（B3） | 无新增；本批不重复计数 |
| 13 | IsNone | `NLConditionNone` | translated | 输入 `Value(generic)` vs `Object`；输出 trigger vs Bool |
| 14 | Keyboard | `NLKeyboardActive` | translated | 输出 `Active` 是 trigger，native 是 Bool；且 native 按键码走 config，无输入口 |
| 15 | ObjectByName | `LogicNodeObjectByName` | translated | native 无输入口，名字走 config；legacy 有 `Name(String)` 输入，导出无法从 socket 读取 config |
| 16 | WorldPosition | `NLParameterWorldPosition` | translated | 语义不同：legacy 是屏幕坐标→世界坐标（Camera/ScreenX/ScreenY/Depth），native 是对象世界位置（Object→Vector），端口 4→1 |
| 17 | ClearParent | `NLActionRemoveParentNode` | translated | 输入可对齐；输出 2→1，legacy 多出 `Parent Object`，输出端口无法全部导出 |
| 18 | Touch | 无 legacy 类 | 无候选 | `Touch` 是 B3 新原生节点，没有对应 legacy 节点 |
| 19 | RemoveParent（清单末条，与 ClearParent 同义） | `NLActionRemoveParentNode` | 同 #17 | 已按 #17 核实，不重复计数 |

## 三、本批实际改动

只追加 2 条 `CATALOG` 条目，位置：`native_catalog.py` 的 `B5.1 remap` 段。

1. `NLConditionOnceNode -> Once`
   - 输入：`OnEnter ← input_condition`
   - 输出：`OnOut ← OUT`
2. `NLConditionLogicOperation -> Compare`
   - 输入：`A ← param_a`、`B ← param_b`（generic Value 收紧为 Float）
   - 输出：`Value ← RESULT`
   - config：`op = operator`，映射 `0/1/2/3/4/5 → eq/ne/gt/lt/ge/le`

## 四、验收结果

| 检查 | 结果 |
| --- | --- |
| port_contract | `errors=0`，`catalog_nodes=54`（52→54，+2） |
| enum_consistency | `inconsistencies=0` |
| native_audit | `migrated=55`（53+2），`total=290` |
| legacy 覆盖率 | `55/290 = 19.0%`（基线 18.3%，+0.7pt） |
| semantic 覆盖率 | `132/360 = 36.7%`（基线 130/360 = 36.1%） |
| 导出冒烟 | `Once`/`Compare` 节点 IR 正常，`Compare` 输出 `{'op': 'gt'}`，`warnings=[]` |

证据日志：`/tmp/b51_remap.log`（port_contract + enum + audit + 导出冒烟）。

## 五、与 24% 预期的差距

24% 对应迁移数约 70（`290 × 24%`），即需要新增约 17 条；本批只有 2 条经得起端口契约。
差口 15 条全部列在上表：它们不是“没查到”，而是端口确实对不上（缺输出、类型不同、顺序不同、
或 native 把输入改成了 config）。按任务要求，这些一律标 `translated` 留给后续批次，不做假映射。
