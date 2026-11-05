# tests/ — D.1 全量回归入口

本目录下的四个入口脚本共用**同一份 GameNodes TU 清单**（`tests/harness_config.sh`）。
清单从 `build_linux/build.ninja` 派生，不再手写源文件列表；新增 TU 会自动进入所有
E2E / stability / benchmark harness。

## 1. 文件

| 文件 | 作用 |
|---|---|
| `harness_config.sh` | 唯一 TU 清单 + 编译/链接/符号检查 + ninja 锁等待 + 结果解析 |
| `run_all_e2e.sh` | 构建并运行所有 E2E 批次，输出汇总表 |
| `run_stability.sh` | 重建循环 N 轮 + ASan（独立 `tests/build_asan`） |
| `run_jit_equivalence.sh` | 同一随机图在 InlinePass 关/开下位模式必须一致 |
| `run_benchmark.sh` | native / inline-off / inline-on 三条路径 ns/frame，可对比基线 |
| `d1_jit_equivalence.cpp` | JIT 等价性 harness（`run_jit_equivalence.sh` 的被测程序） |
| `d1_benchmark.cpp` | 性能 harness（`run_benchmark.sh` 的被测程序） |

**证据资产落点约定**：C++ / Python harness 与 shell 构建脚本一律放 `tests/`；
需要保持相对引用的 `verify_gap/` 资产放 `tests/verify_gap/`。`/tmp` 只保留运行期产物。

### 1.1 证据资产副本与 `/tmp` 原件差异登记

**本表列出全部与 `/tmp` 原件不一致的文件；未列入者均已 `cmp` 验证逐字节一致。**

`/tmp` 原件保留不动（原始证据）；入库副本仅允许**路径补丁**，不得改逻辑。
2026-11-05 全量比对：`tests/` 顶层 39 个 + `tests/verify_gap/` 5 个，共 **44 个**有
`/tmp` 原件的迁移文件逐个 `cmp`——**40 个逐字节一致、4 个不一致、0 个原件丢失**。
仓库原生文件（`harness_config.sh`、`run_*.sh`、`d1_*.cpp`）没有 `/tmp` 原件，不在本表范围。

| 仓库路径 | `/tmp` 原件 | cmp | 差异行与性质 | 原件仍存在 | sha256（原件 → 副本） |
|---|---|---|---|---|---|
| `tests/a1_clean_harness.sh` | `/tmp/a1_clean_harness.sh` | DIFF | L11-12 新增 `HERE`；L45 / L46 源码路径 `/tmp/$h.cpp` → `$HERE/$h.cpp`；L79 `--python /tmp/b45_gui.py` → `"$HERE/b45_gui.py"`。**仅路径** | 是 | `1001e405…b30f` → `a56cfc3b…d639` |
| `tests/a4_benchmark.sh` | `/tmp/a4_benchmark.sh` | DIFF | L7-8 新增 `HERE`；L43 `/tmp/a4_bench.cpp` → `$HERE/a4_bench.cpp`；L73 `/tmp/a4_bench_py.py` → `$HERE/a4_bench_py.py`。**仅路径** | 是 | `be74ccf8…64e1` → `44bae82c…cbda` |
| `tests/b661_cross_build_all.sh` | `/tmp/b661_cross_build_all.sh` | DIFF | L4 新增 `HERE`；L6-11 新增注释 + `source harness_config.sh` + `REG_MAP`；L37 / L38 源码路径改仓库内；**L42-63 新增 TU 裁剪**（按 harness 的 `Register*GameNodes()` stub 剔除真实 TU，再叠加静态排除表）。**路径 + 链接集合修复（新增行）** | 是 | `5feed05e…f459` → `c7deaa6a…3a27` |
| `tests/verify_gap/b661_perf_v.cpp` | `/tmp/verify_gap/b661_perf_v.cpp` | DIFF | L294 默认基线 `/tmp/verify_gap/perf_baseline_v.txt` → `tests/verify_gap/perf_baseline_v.txt`。**仅路径**（相对仓库根，须在仓库根运行） | 是 | `b3594630…970e` → `5865942b…e90c` |

逐字节一致的 **40 个**（`cmp` 无输出，两侧 sha256 相同，不重复抄录）：

- harness / stub（23）：`b3_audio_e2e.cpp`、`b61_e2e.cpp`、`b62_e2e.cpp`、`b63_network_e2e.cpp`、
  `b63_nodes_e2e.cpp`、`b64_e2e.cpp`、`b65a_e2e.cpp`、`b65a_reg_stubs.cpp`、`b66_uncovered_e2e.cpp`、
  `b661_e2e.cpp`、`b661_logicgate.cpp`、`b661_query_e2e.cpp`、`b662_jit.cpp`、`c31_udp_e2e.cpp`、
  `c32_nodes_e2e.cpp`、`c32_rooms_e2e.cpp`、`p41_rebuild.cpp`、`p42_hot_reload.cpp`、`p43_trace.cpp`、
  `p44_stability.cpp`、`reg_b2_b3.cpp`、`reg_b4_e2e.cpp`、`reg_core_p123_t12.cpp`
- 证据资产（13）：`a1_clean_configure.sh`、`a4_bench.cpp`、`a4_bench_py.py`、`b45_gui.py`、
  `b5_engine_check.sh`、`b63_net_build.sh`、`b64_metadata_check.py`、`b661_bridge.cpp`、
  `b661_perf.cpp`、`c1_restitution.cpp`、`c4b_compress_e2e.cpp`、`c7_ui.cpp`、`p4_editor_hot_reload.py`
- `verify_gap/`（4）：`b661_logicgate_v.cpp`、`check_worldpos.py`、`make_worldpos.py`、
  `perf_baseline_v.txt`（新增入库，与 `/tmp` 一致）

迁移进来的 shell 脚本保留原文件模式（`0644`），请用 `bash tests/<script>.sh` 调用。

**自包含验证**：把 `/tmp/a4_bench.cpp`、`/tmp/a4_bench_py.py` 临时改名后，
`bash tests/a4_benchmark.sh` 仍 `rc=0`，三条路径均输出 `RESULT`；跑完已恢复原名并核对
sha256 与原件一致。2026-11-05 23:39 复验（仓库内源码，`/tmp` 原件未改名）：`rc=0`，
`native=2515.3`、`inline_off=2502.0`、`python_equiv=8498.2` ns/帧——**这次运行覆盖了
`/tmp/a4_benchmark_baseline.log` 的原始 3×21 记录**（见 §1.2 事故记录）。改为时间戳
路径后，23:58 的新基线落在 `/tmp/a4_benchmark_20261105-235843.log`。

**`verify_gap/b661_perf_v.cpp` 自包含验证**：用 `harness_config.sh` 从仓库根构建并运行，
`rc=0`、`RESULT assertions=3 failures=0`，基线从 `tests/verify_gap/perf_baseline_v.txt`
读取（两条 ±5% 断言均 PASS）。注意默认路径相对 CWD，从仓库根运行。

**`b661_cross_build_all.sh` 自包含验证**：把 `/tmp/b661_cross*` 与 `/tmp/*.o` 全部移到
备份目录后，从仓库目录复跑仍是 `16 BUILT / 0 LINK_FAIL / 0 MISSING_SRC`；跑完已恢复，
56 个 `b661_cross_*` 产物的 sha256 与迁移前逐一相同。16 个 ELF 也逐个执行，均 `rc=0`
且 0 条 `FAIL`（其中 `b661_perf` 是纯性能二进制，按设计不打印 `RESULT`）。

### 1.2 脚本不得覆盖既有证据日志（硬规则）

**任何 `tests/*.sh` 都不得用 `>` / `: >` 截断一个已存在的日志或产物。** 产出日志必须：

1. 使用**带时间戳的唯一文件名**（默认 `..._$(date +%Y%m%d-%H%M%S).log`），或
2. 用 `>>` 追加到本轮新建的文件；

且创建时必须**独占**（`( set -C; : > "$path" )`），目标已存在就**拒绝写入并报错退出**，
不得静默覆盖。唯一允许的固定路径是缓存/中间对象（如 `tests/.harness_obj/`、`/tmp/b661_cross_obj/`）。

**事故记录（2026-11-05）**：`/tmp/a4_benchmark_baseline.log` 原本是 A.4 基线那次
3 进程 × 21 轮 × 1000 帧的产物（`native=2624.4`、`inline_off=2525.2`、`python_equiv=8458.1`），
被 23:39 的一次重跑覆盖成 `2515.3 / 2502.0 / 8498.2`。**原始内容已不可恢复**。根因是
`tests/a4_benchmark.sh` 用固定路径 `LOG=/tmp/a4_benchmark_baseline.log` 加 `: > "$LOG"`。
同轮已因同一风险拒绝执行 `a1_clean_harness.sh`，但没有把这条推广成规则。

当前已改为时间戳的非覆盖式脚本：

| 脚本 | 新路径 |
|---|---|
| `a4_benchmark.sh` | `/tmp/a4_benchmark_<YYYYmmdd-HHMMSS>.log`；二进制 `/tmp/a4_bench_<...>` |
| `a1_clean_harness.sh` | `/tmp/a1_clean_harness_<...>.log`；每次运行独立目录 `/tmp/a1_clean_<...>/` |
| `b5_engine_check.sh` | `/tmp/b5_engine_<...>.o` |
| `b63_net_build.sh` | `/tmp/b63_net_e2e_<...>` |
| `b661_cross_build_all.sh` | `/tmp/b661_cross_compile_<...>.log`；ELF `/tmp/b661_cross_<...>/` |
| `run_all_e2e.sh` | `tests/.e2e_out/run_<...>/` |
| `run_stability.sh` | 轮次/ASan/ninja 日志均带 `<...>` 后缀 |
| `run_jit_equivalence.sh` | `tests/.jit_out/run_<...>/` |
| `run_benchmark.sh` | 默认输出 `tests/.bench_out/bench_..._<...>.txt` |
| `run_b5_rerun.sh` | `/tmp/b661_b5_rerun_<...>.log`、`..._build_<...>.log` |

新增脚本必须遵守；提交前自查：

```bash
grep -nE '(LOG|OUT|OUTFILE)=/tmp/[^$"]*(log|txt|out)' tests/*.sh   # 应无固定路径
grep -nE ': *> *"?(/tmp|$LOG)' tests/*.sh                          # 应只剩独占创建行
```

## 2. 统一 TU 清单（`harness_config.sh`）

### 2.1 派生来源

```bash
harness_ninja_tus
# grep -o 'GameNodes/[A-Za-z0-9_]*\.cpp' build_linux/build.ninja | sort -u
```

只有 `build_linux/build.ninja` 里真实参与构建的 GameNodes TU 才会被列入；清单本身
不维护任何手写路径。`build_linux` 尚未配置时脚本会直接报错，不会退回旧清单。

### 2.2 两类排除

1. **自动排除**：扫描 harness 源码里的空体注册函数
   `void Register*GameNodes() { }`，映射到定义该函数的 TU 并排除（`b5_stubs.cpp`、
   `b65a_reg_stubs.cpp`、`c31/c32` 的 stub 都走这条）。
2. **静态排除表** `HARNESS_STATIC_EXCLUSIONS`：每行 `TU|scope|原因|owner`，只用于非 stub
   原因（引擎后端、真实文件系统后端等被 Mock 替代的 TU）。`scope` 为 `all`（所有
   harness）、`never`（保留审计记录但不排除）或 `only:<glob,...>`（只对匹配的 harness
   源文件名生效）。新增行必须写清原因与 owner。

### 2.3 构建期符号门

链接使用 `-Wl,--unresolved-symbols=ignore-all`（harness 会引用未 bind 的引擎符号），
因此**链接本身不会报错**；随后 `harness_check_required_symbols` 用 `nm -C` 检查
`HARNESS_REQUIRED_SYMBOLS`（`KX_CompiledGraph::Compile`、`KX_InlinePass::Run`、
`CompileGraph(GraphIR const&)`、`KX_GraphInstance::Load`）。缺失时在构建阶段输出

```
harness_config: undefined required symbol: KX_InlinePass::Run
```

并返回 1，调用方不会进入运行阶段。这正是把历史上两次「运行期调用 0 地址」提前到构建期。

自测钩子：`HARNESS_DROP_TU=KX_InlinePass.cpp` 可把任一 TU 从清单剔除，用来复验该门禁。

### 2.4 对象缓存与锁

- 目标文件缓存在 `tests/.harness_obj/<flags-hash>/`，按源文件/头文件/配置时间戳增量编译。
- 清单与生成头目录默认取 `build_linux`；用 `HARNESS_NINJA=<另一构建目录>/build.ninja`
  可整体切到 clean 构建（对象缓存按 flags hash 自动分开）。
- `harness_wait_ninja_lock` 在检测到别的线持有同一构建目录的 ninja 锁时**等待**，
  不删除目录、不 clean。

## 3. 四个入口脚本

### 3.1 `run_all_e2e.sh`

```bash
tests/run_all_e2e.sh                     # 全部批次
tests/run_all_e2e.sh --only 'b5|c32'     # 按 name/tag 正则过滤
tests/run_all_e2e.sh -j 4                # 并行构建
tests/run_all_e2e.sh --parallel-run      # 并行运行（每批独立日志）
tests/run_all_e2e.sh --list
```

- 批次清单在脚本内的 `MANIFEST`（`kind|name|source|extra|env|tags`）。
  `builtin` 表示 `tests/b5_rerun` 的既有二进制；`harness` 表示用统一清单现场构建。
- b5 四条固定 `BGE_GAMENODE_INLINE=0`：其期望的节点数是未内联时的节点数，与当前
  InlinePass 折叠结果不同，这不是清单问题。
- 输出：`tests/.e2e_out/run_<YYYYmmdd-HHMMSS>/<name>.build.log`、`<name>.log`、`<name>.rc`，prologue 汇总：
  ```
  BATCH              ASSERTS    FAILURES
  b5_p1              76         0
  TOTAL assertions=108 failures=0 build_failures=0 crashes=0
  ```
- 崩溃批次（`rc>=128`、日志缺失、或没有任何结果输出）显示为 `HARNESS_CRASH`，
  计入 failures 与 `crashes`，不会以「0 断言 0 失败」混过。
- 退出码：0 = 全部构建成功且每批 0 failure；构建失败或任一运行非 0 退出 / 有 failure = 1；
  参数错误 = 2。

#### 3.1.1 断言计数规则（唯一口径）

`harness_summarize_log()` 按固定判定链计算，四个入口脚本共用：

1. **日志声明优先**：有 `RESULT assertions=N failures=M`（或 `INFO assertions=N`、
   `P1: 76 assertions, 0 failures`）时，直接采用声明值。
2. **否则按行计数**：数 `^(PASS|FAIL) ` 开头的行，并排除
   - 汇总行 `PASS total failures=...`；
   - **手工打印的 `PASS` 汇总句不得计入断言数**（它们不经过 harness 的 `Check()`，
     不递增断言计数，代表一个聚合结论而不是一条断言）。当前已知：
     `reg_b4_e2e.cpp:522`、`p44_stability.cpp:215`（模式表
     `HARNESS_AGGREGATE_PASS_PATTERNS`，源头修复后可删除）。
3. **禁止裸计数**：不得使用 `grep -c '^PASS'` 或 `grep -cE '^PASS|^FAIL'`。

- 历史原因：`b61`、`b62`、`b64`、`b65a`、`b661_bridge` 会打印 `PASS total failures=0`，
  裸计数让每个这类 batch 系统性 +1；旧的 `TOTAL assertions=737` 其实是 735
  （多出的 2 条来自 `b62` 与 `b65a`）。
- 另一类同等偏差：`reg_b4_e2e` 的 46 行里有 1 行手工汇总，真实 45；`p44_stability`
  的 36 行里同样有 1 行，真实 35。
- `b5` 系列没有逐条 `PASS` 行，只在首行声明 `P1: 76 assertions, 0 failures`，
  按第 1 条采用声明值。
- **根因建议**：所有 harness 统一输出 `RESULT assertions=N failures=M`，
  手工汇总句统一改成 `INFO ...` 前缀。这从源头消灭偏差，比在解析端排除可靠；
  完成后可删除 `HARNESS_AGGREGATE_PASS_PATTERNS`。

#### 3.1.2 MANIFEST 源码必须入库（硬规则）

**`run_all_e2e.sh` 的 `MANIFEST` 里不得出现 `/tmp` 源码路径。**

- 新增 batch 时，`source` 与附加 stub 一律指向仓库内路径（`tests/<name>.cpp`），
  由 `build_one` 统一解析为 `$ROOT/tests/...`；
- `/tmp` **只允许用于运行期产物**：日志、trace、临时目录（例如 `b64` 的
  `/tmp/b64_trace.json`、`p42` 的 `/tmp/p4_hot_dir`、`p43` 的 `/tmp/p4_debug_trace.log`）；
- 不允许把 harness 源码或 stub 留在 `/tmp`——`/tmp` 不是持久存储，清理后 batch 会直接消失。

自查命令（应输出 `0`、且不打印任何路径）：

```bash
tests/run_all_e2e.sh --list | grep -c /tmp || true
tests/run_all_e2e.sh --list | grep /tmp
```

**规则由来（2026-11-05）**：一次全量扫描发现 21 个 MANIFEST 条目、22 个文件
（第 22 个是 `b65a` 的附加 stub `b65a_reg_stubs.cpp`）仍指向 `/tmp`，包括断言数最大
的 `b62`（68 条）。这些源码已全部迁入 `tests/` 并逐批实跑核对断言数不变
（b62 = 68）。新增 batch 请沿用这个格式：

```bash
"harness|name|tests/name.cpp|tests/name_stubs.cpp||tag"
```

### 3.2 `run_stability.sh`

```bash
tests/run_stability.sh 3                 # N=3 轮重建循环
tests/run_stability.sh 3 --with-ninja    # 额外跑一次增量 ninja（会等锁）
```

- 默认**完全不修改 `build_linux`**；只有 `--with-ninja` 才跑增量 `ninja -C build_linux -j2`。
- 普通二进制与 ASan 二进制分别构建；ASan 对象与可执行文件只写到
  `tests/build_asan/`（`obj/`、`asan_stability`、`link_<...>.log`）。
- ASan 运行参数 `detect_leaks=1:abort_on_error=0:print_summary=1`，扫描
  `ERROR: (Address|Leak)Sanitizer`。
- 输出：`tests/.stability_out/rebuild_round_<YYYYmmdd-HHMMSS>_<n>.log`、
  `asan_<...>.log`、`ninja_<...>.log`；汇总行
  `STABILITY rounds=2 round_failures=0 asan_build_fail=0 asan_reports=0`。
- 退出码：0 = 无轮次失败、ASan 构建成功且无报告；否则 1。

### 3.3 `run_jit_equivalence.sh`

```bash
tests/run_jit_equivalence.sh --graphs 3 --nodes 100     # 小样本
tests/run_jit_equivalence.sh                            # D.1 全量 20 × 100
```

- 构建 `d1_jit_equivalence.cpp`，分别以 `BGE_GAMENODE_INLINE=0` / `=1` 运行，去掉首行
  `inline=<0|1>` 后 diff 全部 `graph=<i> bits=%08x`。
- 额外校验开关真的生效（offline 必须是 `inline=0`，online 必须是 `inline=1`）。
- 输出：`tests/.jit_out/run_<YYYYmmdd-HHMMSS>/offline.log`、`online.log`、`equivalence.diff`；汇总行
  `JIT_EQUIV graphs=3 nodes=100 seed=12345 identical=yes failures=0`。
- 退出码：0 = 两次运行成功、开关生效、所有图位模式一致且 0 load failure；否则 1。

### 3.4 `run_benchmark.sh`

```bash
tests/run_benchmark.sh                                    # 默认 100 节点 / 1000 帧 / 5 轮
tests/run_benchmark.sh --frames 500 --rounds 3
tests/run_benchmark.sh --baseline tests/.bench_out/bench_12345_100_500_<YYYYmmdd-HHMMSS>.txt
```

**交错测量（内联方向）**：`bash tests/a4_interleave.sh`（默认 6 对 `native → inline_off`，逐对求 `off/native` 比值；判据与离群规则写死在脚本头注释）。

**频率无关计数（可行性探测）**：`bash tests/a4_hwcount.sh`——探测 `perf` 的 `cycles,instructions` 与 `valgrind`；本机 `perf_event_paranoid=4` 且 `CapEff=0000000000000000`（无 `CAP_PERFMON`/`CAP_SYS_ADMIN`），`perf stat -e cycles,instructions -- true` 返回 `rc=1` 并报 `Error: No supported events found.`；`valgrind`/`callgrind_annotate`/`cachegrind_annotate` 均未安装，故结论为 `no-frequency-invariant-counters`。判据（`off_cycles/native_cycles` 区间是否含 1）写死在脚本头注释，**ns 不作判据**。

**三条路径测的到底是什么**（这也决定了哪些数字可以互相对比）：

| `path=` | 实际链路 | 能否与图路径对比 |
|---|---|---|
| `direct-call` | harness 本 TU 里的 100 次 `ApplyOp()` 循环。没有 `KX_GraphInstance`、没有 `KX_ExecutionPlan`、没有调度器、没有节点对象。 | **不能**。它只是原始 float 链的对照（control），不包含任何图执行开销。 |
| `inline-off` | `KX_GraphInstance::Tick` → `KX_ExecutionContext::RunFrame` → `ExecuteValueNodes` → 对 `plan.value_nodes` 里的每个条目调用 `KX_LogicNodeRuntime::Execute`；`KX_InlinePass` 关闭。 | 可以，与 `inline-on` 对比。 |
| `inline-on` | 同一张图、同一批节点，但可折叠的连续 value 节点被 `KX_InlinePass` 包进 `KX_InlinedBlock`；`ExecuteValueNodes` 调用块，块内 `ExecuteValueOnly` 再逐个 `node->Execute`。 | 可以，与 `inline-off` 对比。 |

harness 会打印证明「真的执行了 100 节点」的计数：

- `path=inline-off ... value_entries=102 inline_blocks=0 inlined_nodes=0 executed_nodes_per_frame=102`
- `path=inline-on  ... value_entries=1   inline_blocks=1 inlined_nodes=102 executed_nodes_per_frame=102`
- `executed_nodes_per_frame` 两条图路径必须相等（102 = 2 个 ConstFloat + 100 个 FloatMath）；
  不等则 harness 失败。
- `control_checksum=... graph_sink_off=... graph_sink_on=... match_off=yes match_on=yes`
  把 `direct-call` 的最终值与两条图路径 Sink 的 `LastValue` 按位比较；不等同样失败。
  这证明 control 跑的是同一串 100 次运算，而不是被优化掉。

**历史事故（2026-11-05）**：第一版把该路径叫 `native`，报出 `26.3 ns/帧`
（≈0.26 ns/节点）。根因是内层 100 节点链的结果没有任何消费者，`-O1` 把整条链提升/删除，
只留下帧循环开销。现在用 `volatile` 的零偏置读入阻断循环不变量提升，并强制打印
`control_checksum` 与图 Sink 比对。**看到 `direct-call` 数字比图路径小一个数量级是正常的
（它本来就绕过了图执行）；看到它小于约 100 ns/帧才说明又没真跑。**

当前参考值（100 节点、500 帧、3 轮中位数，本机）：

```
path=direct-call ns_per_frame≈201  ns_per_node≈2.0
path=inline-off  ns_per_frame≈2550 ns_per_node≈25.0  executed_nodes_per_frame=102
path=inline-on   ns_per_frame≈2542 ns_per_node≈24.9  executed_nodes_per_frame=102
```

与 JIT 线同规格 100 节点 Math 图的 1680 ns/帧比，图路径约 1.5 倍，属同一数量级；
`direct-call` 不与它比较。

#### 3.4.1 `KX_InlinePass` 开关是**编译期**语义

`KX_GraphInstance::Load()` 会立即编译，`KX_InlinePass::Enabled()` 只在编译时读取。
因此必须严格按这个顺序：

```cpp
KX_InlinePass::SetEnabled(false);
offline.Load(ir);          // 真正的未内联计划
KX_InlinePass::SetEnabled(true);
online.Load(ir);           // 真正的内联计划
```

第一版 benchmark 先 `SetEnabled(true)` 再连续 `Load()` 两次，导致 `inline-off` 也被内联
（`value_entries=1`），两条图路径实际测的是同一条链路。**任何新基准都要先关、再 Load、再开。**

#### 3.4.2 已知现象：内联生效，但净收益在噪声内

100 节点图（500 帧、3 轮中位数）：

| | `plan_nodes` | `value_entries` | `inlined_nodes` | ns/帧 |
|---|---|---|---|---|
| inline-off | 104 | 102 | 0 | 2549.2 |
| inline-on | 105（多 1 个 block 条目） | 1 | 102 | 2541.6 |

- 内联**确实生效**：`value_entries` 从 102 塌到 1，100 个 FloatMath 全部进块；
- 但净收益约 **0.3%**，在噪声内；A.4 clean 基线甚至测到 inline-on 慢 **3.9%**
  （2624.4 / 2525.2）。
- 这解释了为什么 JIT 线「40% 提速」的目标达不到：当前 `KX_InlinedBlock` 只是把循环
  换了个入口（`block->Execute → ExecuteValueOnly → node->Execute`），没有消除逐节点的
  虚调用与运行时数据访问，额外还多了 block 分发。

## 4. 新增/修改时的规则

1. **新增 GameNodes TU**：只改 `source/gameengine/GameLogic/CMakeLists.txt`，重新配置
   `build_linux` 后即可，所有脚本自动继承。**不要**在任何脚本里手写 TU 路径。
2. **新增 harness**：在 `run_all_e2e.sh` 的 `MANIFEST` 加一行；若 harness 自己 stub 了
   `Register*GameNodes()`，无需额外配置（自动检测）。
3. **确实需要排除 TU**：加到 `HARNESS_STATIC_EXCLUSIONS`，必须写原因与 owner。
4. **新增必须链接的入口符号**：加到 `HARNESS_REQUIRED_SYMBOLS`（用 demangled 名字，
   `nm -C` 可见）。
5. `tests/b5_rerun/Makefile` 与 `/tmp/b63_net_build.sh` 已经改为 `source harness_config.sh`，
   新脚本也应如此；不要再复制一份源文件列表。

## 5. 已知非致命告警

- 链接阶段的 `relocation against '_ZTV19KX_ExecutionContext' in read-only section`
  与 `creating DT_TEXTREL in a PIE` 是既有布局问题，不影响 harness 运行。
- `build_linux` 被别的线占用时，`run_all_e2e.sh` / `run_stability.sh` 不使用它；
  只有显式 `--with-ninja` 才会等待锁后增量构建。

## 6. 自测记录

`/tmp/d1_scripts.log` 保存四个脚本的首轮自测输出，以及
`HARNESS_DROP_TU=KX_InlinePass.cpp` 的构建期 undefined-symbol 验证。
