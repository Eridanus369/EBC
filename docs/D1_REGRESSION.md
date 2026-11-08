# D.1 全量回归（第一批，clean 构建；benchmark 留给 D.3）

- 日期：2026-11-05
- 构建：`build_clean_verify`（未触碰 `build_linux`，未删除任何构建目录）
- 源码 HEAD：`9e15798a649a9daa1633d86ecdb5f347490ad52c`（工作区含未提交改动）
- 完整输出：`/tmp/d1_regression_partial.log`

## 0. 结论

| 检查项 | 结果 |
|---|---|
| 增量重建 `ninja -C build_clean_verify -j4` | **exit=0**；11 个步骤，本次重编 **5 个 TU**（1 个 CXX + 4 个 C，均为 buildinfo / `RAS_Rasterizer`；GameNodes 无 TU 需重编） |
| 全量 E2E（`tests/run_all_e2e.sh`） | 首跑（25 批、脚本未修复前）原始 `TOTAL assertions=737 failures=3 build_failures=0`，**rc=1** |
| E2E 最终值（K 冻结，46 批） | **assertions=1456 failures=0 crashes=0**（`RUN_ID=20261106-224926`）；J 块当时 26 批 / 851（含 b66）/ 25 批 831（历史） |
| JIT 一致性（20 图 × 100 节点） | **identical=yes，failures=0**（逐值 float 位模式相同） |
| 稳定性 + ASan | `rounds=10 round_failures=0 asan_build_fail=0 asan_reports=0`，**无 leak/UAF** |

## 1. 增量重建

```text
ninja -C build_clean_verify -j4
NINJA_EXIT=0
本次重编 TU（5 个）:
  source/blender/render/intern/engine/RAS_Rasterizer.cc.o
  source/creator/buildinfo.c.o（+ buildinfo_static.h / buildinfo.h_fake 等生成步骤）
链接: bin/blender、bin/blenderplayer
```

- 说明：GameNodes 源码最新 mtime 为 20:44，clean 构建的 `libge_logic_bricks.a` 为 21:03，故 C 阶段改动**已包含在 clean 产物中**；本次增量没有 GameNodes TU 需要重编。
- `bin/blender`/`blenderplayer` 重新链接成功，`--version` 仍为 `Blender 5.3.0 Alpha`，build hash `9e15798a649a`。
- 日志：`/tmp/d1_ninja_build.log`（已并入 `/tmp/d1_regression_partial.log`）。

## 2. 全量 E2E

命令：

```bash
HARNESS_OBJ_ROOT=/tmp/d1_harness_obj HARNESS_JOBS=4 E2E_JOBS=4 \
E2E_OUTDIR=/tmp/d1_e2e_out tests/run_all_e2e.sh
```

| batch | assertions | failures | 备注 |
|---|---:|---:|---|
| b5_p1 | 76 | 0 | builtin |
| b5_p2 | 40 | 0 | builtin |
| b5_p3 | 23 | 0 | builtin |
| b5_p3_input | 53 | 0 | builtin |
| b2_b3_reg | 15 | 0 | |
| b4_reg | 55 | 0 | 新增 10 条 JSON 值断言；日志声明 `assertions=55`（`^PASS` 行 56，多出的是 `reg_b4_e2e.cpp:522` 的手工汇总句） |
| p12_core | 9 | 0 | |
| p41_rebuild | 23 | 0 | |
| p42_hot_reload | 9 | 0 | |
| p43_trace | 9 | 0 | |
| p44_stability | 35 | 0 | `p44_stability.cpp:215` 是手工汇总句，不计断言 |
| b3_audio | 31 | 0 | |
| b61 | 26 | 0 | 脚本修复前启动即 SIGSEGV，修复后正常（见 §2.1） |
| b62 | 68 | 0 | 脚本汇总行曾多计 1（打印 69），已修复 |
| b63_network | 17 | 0 | |
| b63_nodes | 36 | 0 | |
| b64 | 61 | 0 | 同 b61（见 §2.1） |
| b65a | 29 | 0 | 脚本汇总行曾多计 1（打印 30），已修复 |
| b66_uncovered | 20 | 0 | 新增批：`ProximitySensor` 10 / `JoystickAxis` 5 / `JoystickButton` 4 + 1 编译检查 |
| b661 | 19 | 0 | |
| b661_query | 41 | 0 | |
| b661_logicgate | 22 | 0 | |
| **b662_jit** | **18** | **0** | 40% 旧阈值已由 J 块改为同窗成对判据：median(off/native)=1.0091 ≤ 1.05（5/6 对偏慢，见 §2.3） |
| c31_udp | 14 | 0 | C 阶段新增 |
| c32_rooms | 32 | 0 | C 阶段新增 |
| c32_nodes | 70 | 0 | C 阶段新增 |
| **TOTAL（K 冻结）** | **1456** | **0** | 46 批；`build_failures=0`；J 块当时为 26 批 / 851 / 831（历史） |

口径备注（实数，J 块历史）：本表**共 26 行**，其中含 `b66_uncovered`；不含它是 **25 行**。`851 − 20 = 831` 与此一致。K 冻结后为 **46 批 / 1456**（`RUN_ID=20261106-224926`），未在上表逐批展开。

- **K 冻结 = 46 批 / 1456 断言 / 0 失败**；J 块当时 = 26 批 / 851 断言（不含 b66 25 批 / 831），属历史。
- **J 块不含 b66 = 25 批 / 831 断言**（历史）
- **不存在第 27 批**。先前「27 批」的说法源于审计侧口径笔误，已更正。

脚本侧状态：`tests/run_all_e2e.sh` 的 MANIFEST 已为 **46 条**（K 冻结 46 批）；上表 26 行为 J 块历史视图。

### 2.1 脚本假红（另一条线脚本的排除表，已修复）

`b61`、`b64` 在**首跑**（脚本未修复）时 `.rc` 为 139（SIGSEGV），日志无任何 PASS/FAIL 输出，因此首跑汇总记为 `assertions=0 failures=1`；下表的 26 / 61 是 J 块脚本修复后的值（K 冻结为 **44 / 94**）。gdb 栈顶：

```text
b61: FuncCreateVehicle(KX_FunctionNode&, KX_ExecutionContext&)   <- 0x40 null call
b64: (anonymous namespace)::FuncSaveGameBinary(KX_FunctionNode&, KX_ExecutionContext&)
```

根因在 `tests/harness_config.sh` 的静态排除表：

- [`harness_config.sh#L46`](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/harness_config.sh#L46)：排除 `KX_PhysicsAdvancedAdapter.cpp`，注释称“advanced physics backend is never bound by the node harnesses”。但 `b61_e2e` 现在的 `CreateVehicle` 节点必须绑定它，排除后函数指针为 null。
- [`harness_config.sh#L48`](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/harness_config.sh#L48)：排除 `KX_SaveGameBinary.cpp`，注释称“covered by the B4 file harness through mocks”。但 `b64_e2e` 的 `SaveGameBinary` 节点直接调用该 TU。

另：[`harness_config.sh#L30`](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/harness_config.sh#L30) 把 `HARNESS_NINJA` 写死为 `build_linux/build.ninja`，不支持指定构建目录。已核对：

- `build_linux` 与 `build_clean_verify` 的 GameNodes TU 清单 **46 vs 46，diff 为空**；
- 三个生成的 DNA 头（`dna_includes_all.h` / `dna_includes_as_strings.h` / `dna_type_offsets.h`）**逐字节相同**。

因此该硬编码本次不引入行为差异；harness 仍是从当前源码编译，不使用 `build_linux` 的旧对象。

**脚本已修复，本轮结果即修复后的实际值（不再有手工绕过）**：`tests/harness_config.sh` 已把 `KX_PhysicsAdvancedAdapter.cpp` / `KX_SaveGameBinary.cpp` 的静态排除改为 `never` 作用域（只对声明了 `never` 的 harness 生效），因此 b61/b64 能正常绑定这两个 TU，结果为：

| harness | assertions | failures | rc |
|---|---:|---:|---:|
| b61 | 26 | 0 | 0 |
| b64 | 61 | 0 | 0 |

（上表为 J 块脚本修复后的当次值；K 冻结 `RUN_ID=20261106-224926` 后为 **b61 44/0、b64 94/0**。）

**最终 E2E 汇总（J 块历史算式）：assertions = `822 − 46(b4_reg) − 36(p44_stability) + 55 + 35 + 20(b66_uncovered) − 17(b662 旧) + 18(b662 新) = 851`；failures = 0。K 冻结后全量为 **1456 / 46 批**（`RUN_ID=20261106-224926`，见 `NUMBER_CONSISTENCY_AUDIT.md` B.5）。**

- 其中 `822` 是**首跑 25 批修正后的基数**（`735 + 26(b61) + 61(b64)`，见 §2.2 的脚本汇总行修复）。
- `55` / `35` 是 `b4_reg` 新增 10 条 JSON 值断言、`p44_stability` 去掉 1 条手工汇总句后的真实值。
- `20` 是新增批 `b66_uncovered`（`ProximitySensor` 10 / `JoystickAxis` 5 / `JoystickButton` 4 + 1 编译检查）。
- `−17 + 18` 是 `b662_jit` 把 1 条 40% 断言换成 2 条同窗成对断言（总断言 17 → 18）。
- **不含新增 `b66_uncovered` 的 25 批套件为 831。**

（历史说明：本批第一次跑 25 批时，脚本尚未修复，b61/b64 因上述两条排除启动即 SIGSEGV；当时的临时办法是用完整 46 TU + `--allow-multiple-definition` 重链以获得 26/61。脚本修复后同一结果直接由脚本产出，上述临时办法已不再需要。）

### 2.2 汇总逻辑 bug（已修复）：汇总行曾被当作断言

[`harness_config.sh#L341`](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/harness_config.sh#L341) 在日志没有 `N assertions` 时用 `grep -cE '^(PASS|FAIL)'` 统计断言数，于是把 harness 末尾的 `PASS total failures=0` **汇总行也当成一条断言**：

- `b62`：脚本报 69，实际 68；
- `b65a`：脚本报 30，实际 29；
- 因此脚本 `TOTAL assertions=737` 实际应为 **735**（b61/b64 崩溃时为 0，不受影响）。

**已采纳并验证**：脚本作者已把汇总行排除出断言计数（`^PASS total failures=` 不再计入），本轮复验后 `b62=68`、`b65a=29` 与真实断言数一致，`TOTAL=735`（修复前为 737）。

**方法论（两类汇总句都必须排除，这条比「会偏 1」更醒目）**：

1. **`PASS total failures=` 家族**，共 **5 个**：`b61` / `b62` / `b64` / `b65a` / `b661_bridge`（最后一个不在本批清单内）。
2. **代码里手工打印的 `PASS ...` 汇总句**：本套件命中两处——`reg_b4_e2e.cpp:522`、`p44_stability.cpp:215`（它们解释了为什么 `b4_reg` 的 `^PASS` 行是 56 而声明是 55、`p44_stability` 是 36 而声明是 35）；另有 `c1_restitution.cpp:100`，不在本清单内。

后果：朴素 `grep -c '^PASS'` 在**旧日志**（25 批、基数 822）上得 **826**（= 822 + 4 条汇总行，已在 `/tmp/d1_e2e_out/` 与两份 bypass 日志里逐条核对），在新日志上**还会更高**（随新增断言与新增批变动）——**这个数本身没有意义**。

**结论句（写死）**：*断言数只认 `assertions=N` 声明；没有声明时才数行，并同时排除上述两类汇总句。*

### 2.3 b662_jit：40% 阈值已由 J 块降级为历史目标，原失败已消除

历史（2026-11-05，旧判据，仅作追溯）：

```text
BENCH rows=25 ticks_per_row=400 best_off_ms=0.923 best_on_ms=0.910
BENCH off_ns_per_tick=2306.6 on_ns_per_tick=2273.9 reduction=1.42%
BENCH const100 off_ns_per_tick=1453.4 on_ns_per_tick=1204.9 reduction=17.10%
FAIL benchmark reduces frame time by at least 40%
```

- 旧判据要求「内联收益 ≥40%」，实测仅 1.42%，故当时判 FAIL；40% 的不可达性算术见 `docs/B6.8_ACCEPTANCE.md` §2.4.2。
- **J 块已改判据**（用户拍板）：改为「不应显著变慢」，度量改为**同窗成对比较**；判据跑前写死在 `tests/b662_jit.cpp:8-20`，容差 5% 取自 `tests/a4_interleave.sh` 的跨运行散布 `[0.9613, 1.0212]`。旧 40% 目标保留在源码注释中作为历史注记，不删除。
- 实跑（`/tmp/j_b662_detail.log:50-56,63-64`）：6 对 `off/native` 比值 `1.0090 / 1.0163 / 0.9995 / 1.0042 / 1.0145 / 1.0092`，**median=1.0091，min=0.9995，max=1.0163，5/6 对偏慢**；`median ≤ 1.05` 通过。偏慢幅度仅 0.91%，小于 5% 容差，方向仍不可判定。
- 该批现为 **18 断言 / 0 失败**（原 17 断言中 1 条 40% 断言换成 2 条成对断言）。J 块全量复跑 `/tmp/j_full_e2e.log`：**TOTAL assertions=851 failures=0**，rc=0（K 冻结后全量为 1456/46 批）。

## 3. JIT 一致性（全量）

```bash
HARNESS_OBJ_ROOT=/tmp/d1_harness_obj HARNESS_JOBS=4 JIT_OUTDIR=/tmp/d1_jit_out \
tests/run_jit_equivalence.sh --graphs 20 --nodes 100 --seed 12345
```

```text
inline: offline=0 online=1
graphs compared: 20
JIT_EQUIV graphs=20 nodes=100 seed=12345 identical=yes failures=0
```

- `offline.norm` 与 `online.norm` 21 行逐字节相同（`NORM_IDENTICAL`），即 float 位模式逐值一致。
- 链接阶段出现 `warning: creating DT_TEXTREL in a PIE`（因为 harness TU 未用 `-fPIC`，`KX_ExecutionContext` vtable 重定位在只读段），属于链接警告，不影响运行；20 图全部一致、0 失败。
- 日志：`/tmp/d1_jit_out/`。

## 4. 稳定性 + ASan

```bash
HARNESS_OBJ_ROOT=/tmp/d1_harness_obj HARNESS_JOBS=4 STABILITY_OUTDIR=/tmp/d1_stability_out \
tests/run_stability.sh 10
```

```text
round 1: PASS ... round 10: PASS
ASAN: clean (no UAF/leak report)
STABILITY rounds=10 round_failures=0 asan_build_fail=0 asan_reports=0
STABILITY_EXIT=0
```

- ASan 构建落在独立的 `tests/build_asan`（脚本固定路径），**未覆盖 `build_clean_verify`、未触碰 `build_linux`**。
- 复用了 `tests/build_asan/obj` 中比源码新的对象；本次源码未变，等价于从当前源码构建。
- 普通重编译循环 10/10 通过；ASan 运行 0 条 `ERROR: AddressSanitizer` / `ERROR: LeakSanitizer`，无 leak/UAF 报告。
- 完整 ASan 日志：`/tmp/d1_stability_out/asan.log`。

## 5. 未收口项与后续

1. ~~**b662_jit 的 40% 内联性能断言**~~：**已解决（J 块）**——40% 降级为历史目标，判据改为同窗成对 `median(off/native) ≤ 1.05`，实跑 18/0，J 块全量 851/0（K 冻结 1456/46 批）。
2. ~~`tests/harness_config.sh` 两处静态排除导致 b61/b64 假红~~：**已修复**（改为 `never` 作用域 + 按 harness 生效），b61/b64 现由脚本直接产出 **44/0、94/0**（K 冻结）。
3. **`HARNESS_NINJA` 写死 `build_linux`**：本次 TU 清单/DNA 头核对一致，无差异；若后续 C 阶段新增 TU，脚本需要支持指定构建目录，否则会漏 TU。
4. Benchmark 不在本批（D.3），A.4 基线见 [`docs/BENCHMARK_BASELINE.md`](BENCHMARK_BASELINE.md)。

## 6. 证据文件

| 文件 | 内容 |
|---|---|
| `/tmp/d1_regression_partial.log` | 本批完整输出（构建 + E2E + 绕过 + JIT + 稳定性/ASan） |
| `/tmp/d1_ninja_build.log` | 增量构建完整日志 |
| `/tmp/d1_e2e_run.log` | E2E 汇总与批次日志路径 |
| `/tmp/d1_e2e_out/*.log` | 每个 batch 的构建/运行日志、`.rc` |
| `/tmp/d1_bypass_b61.log` / `d1_bypass_b64.log` | 绕过排除表后的 b61/b64 输出 |
| `/tmp/d1_jit_run.log` / `/tmp/d1_jit_out/` | JIT 一致性 20×100 全量结果与归一化输出 |
| `/tmp/d1_stability_run.log` / `/tmp/d1_stability_out/asan.log` | 稳定性 10 轮与 ASan 报告 |
