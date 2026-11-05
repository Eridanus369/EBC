# 证据溯源审计（docs/*.md）

- 审计日期：2026-11-05
- 状态同步：2026-11-05 已按 `A1_BUILD_AUDIT.md` §11 权威复算（`TOTAL=850`，26 批含 `b66_uncovered`；不含 b66 为 25 批 / 830）更新 P0-1、P1-1；旧判定已标注失效
- 审计规则：**审计文档的状态必须跟上被审计文档的实际状态**（数值、批数、引用路径、状态标注）。被审计文档一改，审计侧的引用与判定必须同步改，否则审计本身会变成第二个过期源
- 范围：`/home/eridanusepsilon/Code/EBC/EBC5.3/docs/*.md`（34 份，7076 行）
- 方式：只读脚本扫描路径引用与精确数值，对照 `/tmp` 下所有 `.log/.txt/.json` 语料；**未重跑 harness、未改任何既有文档/源码、未 commit**
- 原始取证：`/tmp/provenance_audit.log`；扫描中间数据：`/tmp/provenance_refs2.json`、`/tmp/provenance_scan.json`

## 结论

共扫出 **390 处路径引用**（374 存在、4 缺失、12 个占位符模板），识别 **P0 无源之数 5 项（P0-1 已关闭）、P1 引用不匹配 8 项（P1-1 已演化、旧判定作废）、P2 口径可疑 8 项**。

最严重的三条：

1. **`313.959 µs/1000 帧`**（[NUMBER_CONSISTENCY_AUDIT.md](NUMBER_CONSISTENCY_AUDIT.md#L182)，另见 L314-L321、L386）：`grep -rn "313.959" /tmp --include="*.log"` **零命中**；当前唯一 clean 日志 `/tmp/a1_clean_run_b661_perf.log` 是 **`312.376`**。**已关闭**：A1、NUMBER_CONSISTENCY_AUDIT 与本文均已用 `312.376` 取代，`313.959` 仅保留为历史注并标注“无 `/tmp` 日志来源，已作废”。
2. **断言总数 `850`（26 批，含 `b66_uncovered`）**（权威复算 `/tmp/d1_assertion_recount.log:28`；算式 `822 − 46 − 36 + 55 + 35 + 20 = 850`，不含 `b66_uncovered` 的 25 批为 `830`）：[B6.8_ACCEPTANCE.md](B6.8_ACCEPTANCE.md#L20)、[L49](B6.8_ACCEPTANCE.md#L49)、[L117](B6.8_ACCEPTANCE.md#L117) 处的 `822 断言 / 1 失败` 引用了 `/tmp/d1_regression_partial.log`，该文件实际是 **`TOTAL assertions=737 failures=3`**（未修脚本的旧结果）；`822/1` 只是 `/tmp/d1_clean_rerun.log:33` 的中间态。**旧判定已作废**：问题不只是“指错文件”，而是“引用日志已过期 + 权威值又演化为 850”，后续一律按 `850` 复核。
3. **`2634.3 / 2562.1 ns/帧`**（[BENCHMARK_FINAL.md](BENCHMARK_FINAL.md#L21)、L79）：声称“21:31 用 `/tmp/a4_bench` 重跑”得到这两个值，但 `/tmp` 下**没有任何 21:31 的日志**，且 `/tmp/a4_bench` 是 ELF 二进制，无法作为文本证据；现有 `/tmp/a4_benchmark_baseline.log`（21:08）只有 `2624.4 / 2525.2`。

---

## P0 无源之数（5 项）

> 定义：精确数值，但在 `/tmp` 全库语料中找不到该值，也没有算式/采集产物可复核。

| # | 数值 | 位置 | 现状 | 建议 |
| --- | --- | --- | --- | --- |
| P0-1 | `313.959 µs/1000 帧` | `NUMBER_CONSISTENCY_AUDIT.md:182,314-321,386` | **已关闭**：无任何日志命中；A1、NUMBER_CONSISTENCY_AUDIT 与本文三份均已改用 `312.376`，`313.959` 仅作历史注 | 历史注保留并标“无 `/tmp` 日志来源，已作废”；权威值 `312.376` 引 `/tmp/a1_clean_run_b661_perf.log` |
| P0-2 | `2634.3 / 2562.1 ns/帧` | `BENCHMARK_FINAL.md:21,79` | 无 21:31 重跑日志；证据是二进制 `/tmp/a4_bench` | 保存 `/tmp/a4_bench` 的 stdout（含时间戳/负载）为 `/tmp/a4_bench_2131.log` 后引用；否则删除这组数 |
| P0-3 | `759720 B / 10147 B` | `UI_B4_TECH_SELECTION.md:662-663` | `/tmp` 无该值；同节 `/tmp/b41_ui_mock.log` 已丢失 | 补一份 bpy 输出日志记录 `packed_file` 字节数；或去掉精确字节、只写“大小不变” |
| P0-4 | `load average 1.88 → 3.51` | `BENCHMARK_FINAL.md:97` | 无 `uptime`/`/proc/loadavg` 采集产物 | 保存测量前后 `uptime` 输出；否则改为“高负载（未记录具体值）” |
| P0-5 | `62.17 px / 28.02 px` | `UI_B4_LINK_DRAG_FIX_20261104.md:44-45` | `/tmp` 无该值，也没有坐标脚本/日志 | 补截图坐标提取脚本输出；或标注“目测/估算”，不要给到 0.01 px |

## P1 引用不匹配（8 项）

> 定义：路径存在，但内容不含被引用的数值/结论；或引用的日志根本是另一个实验。

| # | 数值/结论 | 位置 | 引用路径 | 实际内容 | 建议 |
| --- | --- | --- | --- | --- | --- |
| P1-1 | `822 断言 / 1 失败`（中间态）→ 权威 `850 / 26 批` | `B6.8_ACCEPTANCE.md:20,49,117` | `/tmp/d1_regression_partial.log` | 该文件是 **`TOTAL assertions=737 failures=3`**（L71，未修脚本的旧结果）；`822/1` 在 `/tmp/d1_clean_rerun.log:33`，仅为中间态；权威复算 `/tmp/d1_assertion_recount.log:28` 为 `TOTAL=850`（26 批，含 `b66_uncovered`；不含 b66 为 25 批 / `830`） | **旧判定作废**：不要按“改引 clean 822”复核，否则会得到第三次不一致。应写三级演进：`737/3`（历史）→ `822/1`（中间态）→ `850`（权威），并注明各自日志路径 |
| P1-2 | `1.42%`（b662_jit） | `B6.8_ACCEPTANCE.md:21,49` | `/tmp/d1_regression_partial.log` | 该文件无 `1.42`；实际在 `/tmp/d1_e2e_out/b662_jit.log` | 改引 `/tmp/d1_e2e_out/b662_jit.log` |
| P1-3 | `1.679e-6`、`(−3.327339, −1.871628, −4.242609)` | `A2_TASK345_STATUS.md:185`；`B6.8_ACCEPTANCE.md:41`；`PROJECT_STATUS.md:110,131` | `/tmp/verify_gap/worldpos_real.log`、`worldpos_player4.log` | 日志只有 5 位小数 `(-3.32734,-1.87163,-4.24261)`；无 `1.679e-6`、无 6 位小数的“手算值” | `check_worldpos.py` 本就会打印 `max_err={:.3e}`。**A2 已补证**：2026-11-05 21:47 重跑并落盘 `/tmp/verify_gap/worldpos_real_final.log:32`（`max_err=1.679e-06`、`RESULT assertions=2 failures=0`），A2 已改指该行；`B6.8_ACCEPTANCE.md` 与 `PROJECT_STATUS.md` 仍待补 |
| P1-4 | `0.5279` | `B6_PHYSICS_ADVANCED.md:169` | `tests/c1_restitution.cpp`（代码） | 文档值在全库无命中；`/tmp/c1_physics.log:54` 与 `/tmp/c1_restitution_run.log:1` 都是 **`0.5280`** | 改为 `0.5280` 并引 `/tmp/c1_physics.log:54`；若 `0.5279` 来自旧 run，注明旧 run 时间 |
| P1-5 | `/tmp/sizo.cpp` | `UI_B4_TECH_SELECTION.md:637` | 文件已不存在 | `ls: No such file` | 补回脚本或删除该引用 |
| P1-6 | `/tmp/b41_ui_mock.log` | `UI_B4_TECH_SELECTION.md:657` | 文件已不存在 | 同上 | 补日志；与 P0-3 一起处理 |
| P1-7 | `/tmp/ui_probe_ticks.txt` | `UI_B4_TECH_SELECTION.md:669` | 文件已不存在 | 同上 | 补输出或删引用 |
| P1-8 | `/tmp/sizeof_audit.log` | `UI_B4_TECH_SELECTION.md:677` | 文件已不存在 | 同上 | 补输出或删引用 |

补充：`BENCHMARK_FINAL.md:21` 用 ELF 二进制 `/tmp/a4_bench` 当数字证据，本身也属于“引用不匹配”（二进制不含可 grep 文本）；已并入 P0-2，不重复计数。

## P2 口径可疑（8 项）

> 定义：路径与数值都能对上，但计数口径/推导过程混用，读者容易误读。

| # | 问题 | 位置 | 说明 | 建议 |
| --- | --- | --- | --- | --- |
| P2-1 | 断言数三种口径混用 | `A1_BUILD_AUDIT.md` §11；`A2_TASK345_STATUS.md` §E2E；`B6.8_ACCEPTANCE.md:7,41`；`NUMBER_CONSISTENCY_AUDIT.md:81` | A1 §11 已改为「只认 `assertions=N`，无声明才数 `^PASS` 行并排除两类汇总句」；A2 的 LogicGate 用 `RESULT assertions=N`，Query 用 `PASS` 行；B6.8/NUMBER 又把“29 断言 + 汇总=30”放在一起 | 统一写“断言数只认 `assertions=N`；无声明时才数 `^PASS` 行，并排除两类汇总句（`PASS total failures=` 与手工 `PASS ...`）”；旧口径数字保留时注明拆分 |
| P2-2 | `b62` 版本漂移 | `B6.8_ACCEPTANCE.md` 引 `b62_e2e_v3.log` 的 58；`A1/A2` 已用扩展后的 68 | 新增 v4 音频 17 条后旧数字未同步 | 在 B6.8 处注明“v3 旧值 58，v4 已扩到 68”，避免被读成丢断言 |
| P2-3 | `89.8% / 10.2%` 缺分母 | `VERIFICATION_LEVEL_AUDIT.md:11-13` | 44/49 与 5/49，表中未写 `n=49` | 在表头或脚注写 `n=49` |
| P2-4 | `0.174533` 精度高于日志 | `B5_NODES.md:92` | 日志只有 `0.9895→1.1640→1.3385`（差 `0.1745`）；`0.174533` 是 10° 的精确弧度值 | 引日志 4 位小数，或注明“0.174533 = 10° 精确值” |
| P2-5 | `192` 为表内合计 | `B5_RERUN_REPORT.md:17` | 76+40+23+53=192，但 `/tmp/b661_b5_rerun.log` 不打印 192 | 加一句“由表中四批求和”，或让日志打印 total |
| P2-6 | `15.03 / -6.70 / 8.20` 未给路径 | `A2_TASK345_STATUS.md:194` | 值实际在 `/tmp/verify_gap/worldpos_frames_player2.log:4-8`，但该行未引 | 补日志路径和行号 |
| P2-7 | `25.90 ns/node` 未写算式 | `BENCHMARK_FINAL.md:65` | 由 `2642.3 / 102` 推得 | 在表下写“= 2642.3 ns / 102 nodes” |
| P2-8 | 旧百分比残留 | `CAPABILITY_WEIGHT_AUDIT.md:505-506`；`LEGACY_COVERAGE_POLICY.md` | `92.0% / 95.8%` 在 CAP 表仍有旧列；LCP 已标“旧值作废” | 确认 CAP 表同样加“旧值”标注，或删除旧列 |

## 同类问题的统一处理规则（给各文档作者）

P2 的 8 条里，属于“口径混用”的（P2-1 至 P2-7）根因相同：**文档在复述数字时没有同时复述“这个数是怎么数的”**。不需要逐条回改旧文，但新写或修订任何包含数字的文档时，必须按下面 6 条执行。

### R1 断言数：全文只认一种口径

- 首选 harness 末尾的 `RESULT assertions=N failures=M`；这是唯一不含“汇总行”歧义的口径。
- 若 harness 无 `assertions=N` 声明、只能数 `^PASS` 行，必须排除两类汇总句：① `PASS total failures=`；② 手工汇总 `PASS ...`（如 `reg_b4_e2e.cpp:522`、`p44_stability.cpp:215`、`c1_restitution.cpp:100`）。写成“`PASS` 行 N，扣除 M 条汇总句后为断言数”。
- 禁止在同一表/同一段里混用“PASS 行数”“断言数”“含汇总的 PASS 行”；三者数值不同时必须分别命名。
- 旧口径数字（如含汇总的 30）保留时，必须紧跟“= 29 条断言 + 1 条汇总”，不能只留 30。

### R2 精度：文档精度不得高于来源

- 来源日志只打印 4 位小数（如 `0.9895→1.1640`），文档最多写 4 位；要写更高精度，必须标注“理论值/精确值”并给算式（如 `0.174533 = 10° 的弧度值`）。
- “实测”和“理论/手算”必须分开标注；不得把 6 位小数的理论值写成实测值。
- 科学计数法保留打印时的有效位数（`max_err={:.3e}` 就是 3 位有效数字，写 `1.679e-6`，不要写成 `1.679000e-6`）。

### R3 派生值：同一处给算式

- 合计、百分比、差值必须在出现数字的同一处给出加总项或分子/分母：
  - `76 + 40 + 23 + 53 = 192`；
  - `44 / 49 = 89.8%（n=49）`；
  - `2642.3 ns / 102 nodes = 25.90 ns/node`。
- 工具能打印 total 的，优先让工具打印，而不是让读者自己加。

### R4 版本：数字必须绑定 harness 版本

- 每个断言数旁标注 harness 版本或日志文件名（`v3` / `v4` / `b62_e2e_v4.log`）。
- harness 扩容后，旧数字标“旧版本作废，当前为 N”；禁止新旧数字无标注地同时存在。
- 历史百分比（如 `92.0% / 95.8%`）保留时，必须在同一表头标“旧值/已作废”，不能只靠另一份文档的说明。

### R5 引用：指到含该字面值的行

- 引用格式统一为 `路径:行号`；行号必须是该数值或该结论出现的那一行。
- 若数值是脚本计算的（如 `check_worldpos.py` 的 `max_err`），必须先把脚本 stdout 落盘，再引用“输出日志:行号”；不能只引输入日志或脚本源码。
- 禁止用二进制（ELF/`.o`/`.blend`）作为文本数值的证据。

### R6 可复现：日志头记录启动条件

- 重跑日志头至少记录：日期、完整命令、二进制版本（如 `Blender 5.3.0 Alpha`）、输入文件 `sha256`。
- 影响结果的参数必须写进“运行前提”：例如 `-w` 窗口尺寸必须与场景 aspect 一致；`-w 800 600`（4:3）会改变 NDC 映射，不能与 1920×1080 的结果混用。
- 任何“重跑得到 X”的结论，都要能由日志头 + 命令原样复现；否则按 P0 处理。

### 对应关系

| P2 条目 | 适用规则 |
| --- | --- |
| P2-1 断言数三种口径 | R1 |
| P2-2 `b62` 58 vs 68 | R4 |
| P2-3 `89.8%/10.2%` 缺分母 | R3 |
| P2-4 `0.174533` 精度高于日志 | R2 |
| P2-5 `192` 为表内合计 | R3 |
| P2-6 `15.03` 坐标未给路径 | R5 |
| P2-7 `25.90 ns/node` 未写算式 | R3 |
| P2-8 旧百分比残留 | R4 |

## 扫描说明与边界

- **390 处引用**：4 处缺失（全部集中在 `UI_B4_TECH_SELECTION.md`），12 处是 `<harness>`/`<dir>` 占位符，无法字面检查；其余 374 处路径存在。
- **43 处“路径存在但数字不匹配”**：逐条复核后，绝大多数是 `path:行号` 形式——数字属于 C++/文档行号，不属于被引用日志的数据（例如 `PROJECT_STATUS.md:91` 的 `1355` 是 `KX_FunctionNode.cpp` 行号）。真正属于数据不匹配的已列入 P1。
- **百分比**：`78%` 未在任何文档残留；`92.0%/95.8%` 有明确分母（267/286、282/286）并在 LCP 标为作废，按 P2 处理；`89.8%/10.2%` 可加总出 `n=49`，按 P2 处理。未发现“分子分母全无”的百分比。
- **无法确认**：`/tmp` 中可能存在的非文本产物（截图、二进制、`.blend`）无法 grep；P0-3/P0-5 的像素/字节数可能来自截图测量，但没有任何可复核脚本或输出，因此仍归 P0。

## 约束遵守

- 首轮扫描未修改任何既有文档或源码；唯一新增文件是本报告与 `/tmp/provenance_audit.log`；
- 2026-11-05 状态同步轮只改了 `EVIDENCE_PROVENANCE_AUDIT.md` 与 `A1_BUILD_AUDIT.md`（`313.959 → 312.376`、`822 → 850/26 批`、§11 断言表三类变化），未改源码、未 commit、未碰构建目录与 `_lib.glsl`；
- 未跑 harness/构建，未删除文件，未 `git commit/push`，未触碰两个构建目录与 `_lib.glsl`。
