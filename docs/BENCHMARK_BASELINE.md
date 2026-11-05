# A.4 性能基线（clean rebuild 唯一基线）

- 日期：2026-11-05
- 构建目录：`build_clean_verify`（独立目录；**未触碰** `build_linux`，也未 `rm -rf` 任何构建目录）
- 源码 HEAD：`9e15798a649a9daa1633d86ecdb5f347490ad52c`（`9e15798a649a`）
- 构建标识（baseline build id）：`9e15798a649a-clean_verify-3f074ee0eb1e`
  - `9e15798a649a` = git HEAD 短 hash
  - `3f074ee0eb1e` = `build_clean_verify/lib/libge_logic_bricks.a` 的 sha256 前 12 位
- 基线日志（**新，权威来源**）：`/tmp/a4_benchmark_20261105-235843.log`
  - `3594` 字节、`chmod 444`（`-r--r--r--`）、sha256 `20227622098e08c0df9de75aedd4864665c99492ebdec73126dcff01a34cff64`
  - `build_id=9e15798a649a-clean_verify-3f074ee0eb1e`（与下表一致；`libge_logic_bricks.a` 未变）
- **事故记录（保留，不因新基线删除）**：原始 3×21 日志 `/tmp/a4_benchmark_baseline.log` 中的 `2624.4 / 2525.2 / 8458.1` 于 **2026-11-05 23:39** 被一次重跑覆盖，**不可恢复**；那组数只能以本文档为准，无法从日志复核。
- 构建日志：`/tmp/a1_clean_build.log`；harness 日志：`/tmp/a1_clean_harness.log`

## 0. 基线结论（先看这里）

| 路径 | 轮数（进程 × 内部轮） | ns / frame（中位数） | 节点数 | 构建标识 |
|---|---:|---:|---:|---|
| (a) 原生（内联开启，默认） | 3 × 21 | **2618.3** | 100 | `9e15798a649a-clean_verify-3f074ee0eb1e` |
| (b) `BGE_GAMENODE_INLINE=off`（内联前路径） | 3 × 21 | **2514.7** | 100 | 同上 |
| (c) 纯 Python 等价图（`build_clean_verify/bin/blender -b --python`） | 3 × 21 | **8588.8** | 100 | 同上 |

- **段位：本基线落在慢段。** 判定依据 = 同一二进制 native 三批 `2618.3 / 2558.9 / 2614.6`，**两批落在 `2614–2618`**（与丢失原件 `2624.4` 同段）；`2558.9` 那批含外部干扰离群 `4028.0`（另两次 `2540.9 / 2558.9`），**只记在案，不参与段位判定，也不作任何对比数据**。段位是时序状态，不按幅度反推；本图（100 节点）跨批次整体跨度仅约 5–6%（`2502.0–2643.8`），远小于 `b661_perf` 小图的 `1.54×`（见 §0.2）。**注意：本次 native 的三个子运行本身就在 `2520.7–2680.0`（`6.3%`）内波动**，说明会话内部也存在段间移动；「慢段」是对本批**中位数**的标注，**不保证每个子运行同段**。
- 每轮固定 **1000 帧**；每个进程内部 21 轮取中位数；再用 3 个独立进程的中位数取中位数。
- 单进程原始中位数（ns/frame）：
  - native：2618.3 / 2520.7 / 2680.0
  - inline_off：2507.6 / 2514.7 / 2550.6
  - python_equiv：8456.9 / 8588.8 / 8640.5
- 内联确已生效（native 启动时诊断 `inlined: 1 blocks, 98 nodes -> 1 blocks`；off 路径诊断数为 0）。
- **待查项（不是结论）：本 100 节点链上，内联开启慢 4.1%**（新基线 `native 2618.3` vs `inline_off 2514.7`；`avg` 2632.0 vs 2517.0）。它与 21:08 那次的 `−3.9%` **同向**，是**第二个同向数据点**；21:45 重跑给出 `+1.1%`，是反向点。按 §0.3 的精度口径（`a4_bench` median 约 5%），目前只能记作「**方向不稳定**」——但这**不等于「落在噪声内所以无事」**，必须保留为待查项。可能方向（均未被排除）：
  1. **内联后 value-only 路径多承担分支/搬移**：折叠成单块后，块内取值/写回路径变长，在纯值链上固定开销可能大于省掉的块边界；
  2. **`inline-off` 恰好落在更有利的缓存/内存布局**：两条路径的节点对象分配顺序与缓存行对齐不同，可能让 off 路径受益；
  3. **段间漂移伪装成路径差异**：native 3 批与 inline_off 3 批在同一次会话内先后执行，而本图段间差约 4%——`2514.7 → 2618.3` 恰好是 4.1%，若批次边界正好跨段，这 4.1% 可完全由段位解释。
  结论：**原因未明**，以上三条都未被排除；要定因必须做 path 交错测量（native/off 交替执行、同一进程内成对）。
- 纯 Python 路径约为 C++ 原生路径的 **3.3×** 耗时（8588.8 / 2618.3）。

### 0.1 与 JIT 线自测数字的关系（不可跨集合对比）

- 同一 clean 构建、同一 100 节点图内：内联开启 **2618.3** vs 关闭 **2514.7** ns/frame，即**开启比关闭慢 4.1%**（新基线，慢段）。丢失原件给出的 `2624.4 / 2525.2`（慢 3.9%）方向相同，但那次日志不可复核。
- JIT 线自测为 **-0.40%**：方向一致（内联没有带来正收益、略慢），但量级不同。原因是测量集合不同（节点图结构、帧定义、是否包含引擎循环、二进制/对象集合都不同）。
- **规则：本基线内部（同一 `build_id`）的数字可以直接比较；与 JIT 线自测数字、旧构建、其他机器的数字一律不可跨集合对比。** 报 JIT 数字时必须同时给出其 `build_id` 与测量口径。
- **追加限定（见 §0.2）**：即使集合完全相同（同 `build_id`、同二进制、同协议），**换时窗也会有 1.54× 的整体漂移**；因此「同 build_id 可直接比较」只对**同一时窗内**的测量成立，跨时窗只能用同一进程内的比值。
- 另：另一条线的 `tests/run_benchmark.sh` 曾报 `native 26.3 ns/frame`，比本基线快约两个数量级，已判定为其自身测量 bug。**本 A.4 数字是当前唯一权威基线**；若其修复后仍与 2618.3 差一个数量级，按同一口径（100 节点、1000 帧、`KX_GraphInstance::Tick`）复核，以本基线为准并在报告中写明差异原因。

### 0.2 机器双峰与可比性判据（2026-11-05 增补，读任何性能数字前先看）

本机不是「稳定但吵」，而是**双峰**：同一二进制、同一协议（`frames=1000 × rounds=20 × best(min)`）、仅换时窗，整体漂移 **1.54×**，**中间没有连续过渡**。与负载无关，与绑核也无关。

| 分段 | 区间（µs/1000 帧） | 中位 | 证据 |
|---|---:|---:|---|
| 快段 | `310.142–320.772` | `313.027`（10 次） | `/tmp/c4_perf_recheck.log` D 组 |
| 慢段 | `446.208–485.972` | `482.686` | 同日志 A/B 组；本轮 A1 独立复测 10/10 在此（中位 `480.757`，`/tmp/a1_b661_perf_repro_20261105-230351.log`） |
| 无过渡 | `320.772 → 446.208`（**+39%** 跳变） | — | 24 次采样（A/B/D 组）无一落在此段区间 |

- 两个分段的中位比值 = `482.686 / 313.027` = **1.54×**。
- 被测二进制全程同一 md5：`a0200513869049a7e5970213ceb55111`（clean 链接集，`3,242,464` B）。
- **适用范围说明**：上表分段区间是在 `b661_perf`（约 4 节点小图）上采到的，`1.54×` 也是该图的量；本基线（100 节点图）的跨批次跨度只有约 5–6%（`2502.0–2643.8`）。**段位是时序状态，与该图的幅度无关**：本基线按「同一二进制三批聚类」判为**慢段**（见 §0 第一条），丢失原件 `2624.4 / 2525.2` 与之同段，但两批仍不可跨时窗对比。
- 机器状态：`scaling_governor=powersave`；逐核 `scaling_cur_freq` 在 **1.1–4.5 GHz** 跳变（同日志 G 组采样 `1,097,346`–`4,541,083` Hz）。**峰位是整段时窗的属性**，不能由某次 governor/freq 瞬时读数预测。
- **两条反证（推翻了常见直觉判据）**：
  1. **`loadavg` 不是判据**：`loadavg=3.67`（高于空闲段）时，同一二进制仍 **10/10** 跑出 `310.142–320.772`（快段，D 组）。
  2. **`taskset` 绑核在本机是反向的**：`taskset -c 2` 反而稳定在 `550.643–553.108` µs（E 组，比快段慢约 76%、比慢段还慢），即绑单核破坏了调度器跨核/升频的能力。
- **判据（写死，后续报告必须遵守）**：
  - **重复精度按 harness 分开，不能互相外推（见 §0.3）**：`±1%` 只是 `b661_perf`（best-of）的精度；本文使用的 `a4_bench`（median）是 **约 5%**，`d1_benchmark`（median）约 **6–7%**。笼统的「±1% 噪声带」在本文件**不再使用**。
  - **跨时窗的绝对数不可比，只能用同一进程内的比值**：例如 inline-on 对 inline-off 必须在**同一次运行（同进程、同时窗）**内成对测量；拿两次独立运行的绝对数相减得到的百分比无效。
  - 任何依赖**跨运行 / 跨时窗百分比**的结论，在本机双峰前提下**不可复现**，必须在报告中标注（本基线自身的 `2618.3 / 2514.7` 与任何重跑对比都属此类）。

### 0.3 各 harness 的重复精度（不可互换，引用必须带标签）

实测显示**重复精度随 harness / 统计量差一个量级**；此前文档把它们一律写成「±1% 噪声带」，覆盖面说宽了。修正如下：

| harness | 统计量 | 重复精度（实测） | 证据 |
|---|---:|---|---|
| `b661_perf` | **best-of**（`best_us_per_1000_frames`） | **`±1%`**（`312.376 → 313.027`，`+0.21%`）；单次极值 `310.142–320.772`（10 次，spread `3.4%`） | `/tmp/a1_clean_run_b661_perf.log:1`、`/tmp/c4_perf_recheck.log` D 组 |
| `a4_bench`（本文基线） | **median**（每进程 21 轮中位） | **约 5%**（跨运行 `2515.3/2502.0`、`2618.3/2514.7`、`2621.6/2571.5`、`2643.8/2595.2`、`2577.9/2606.9`，全距 `2502.0–2643.8` = `5.7%`） | `2618.3/2514.7` 见 `/tmp/a4_benchmark_20261105-235843.log`（新基线，sha256 `2022762…cff64`）、`2515.3/2502.0` 见 `/tmp/a4_benchmark_baseline.log`（23:39 次）、`2577.9/2606.9` 见 `/tmp/a4_bench_rerun_20261105-214556.log`；另两组由性能基线线提供，本机 `/tmp` 无独立日志。**`4028.0` 外部干扰离群不计入**（见 §0 段位条） |
| `d1_benchmark`（`tests/run_benchmark.sh`） | **median** | **约 6–7%**（同配置 5 次：`inline-off` `2546.9–2729.9` = `7.2%`、`inline-on` `2538.7–2695.9` = `6.2%`；相对中位约 `±4%`） | `/tmp/d3_5p_o1_run{1..5}.log`（5 次运行、同一时窗） |

- **规则 1（精度标签）**：引用重复精度必须带「**哪个 harness + 哪个统计量（best/median）+ 几次运行**」。`±1%` **只属 `b661_perf` best-of**，**不可外推到 `a4_bench` / `d1_benchmark`**。
- **规则 2（阈值不得比精度严）**：`a4_bench` / `d1_benchmark` 若用 `±5%` 作门限，已与它们自身的重复精度同量级——只能判「**未观测到超出测量精度的差异**」，不能当量化门禁，更不能拿 `±1%` 给它们背书。**注意：`tests/` 下并没有以 A.4 为基线的 `±5%` 判定脚本**（`run_benchmark.sh` 默认 `MAX_DRIFT=30`；`b661_perf_v.cpp` 的 `±5%` 判的是 `perf_baseline_v.txt` 的比值，与本基线无关）。此处的 `±5%` 只是叙述量级。
- **同一时窗内更紧**：单次会话内 `a4_bench` 的 3 进程离散只有 `0.34%`（native `2507.2–2515.8`）与 `0.54%`（inline_off `2498.3–2511.7`）——**约 5% 是跨会话量**，两者不要混用。
- **证据完整性**：本文 §0 的权威来源是新基线 `/tmp/a4_benchmark_20261105-235843.log`（`chmod 444`、sha256 已记在前言，不可再被覆盖）。旧的 `/tmp/a4_benchmark_baseline.log` 现内容为 **23:39 的一次重跑**（`native 2515.3 / inline_off 2502.0 / python 8498.2`，同 `build_id`）；原始 3×21 那次（`2624.4 / 2525.2 / 8458.1`）**已不可恢复**。新基线（慢段 `2618.3/2514.7`）与 23:39 那次（快段 `2515.3/2502.0`）的汇总差为 `+4.1% / +0.5%`，正好落在本节的 5% 量级内。

## 1. 被测图与测量口径

100 个节点，结构固定：

```
OnUpdate(0) ──────────────────────────────► Sink(99).trigger
ConstFloat(1)=1 ─┐
ConstFloat(2)=2 ─┴─► FloatMath(3) ─► FloatMath(4) ─► … ─► FloatMath(98) ─► Sink(99).value
                     (96 个 FloatMath，Add/Mul 交替，每个节点第二输入都接 ConstFloat(2))
节点数 = 1 + 2 + 96 + 1 = 100
```

- C++ 测量程序：`tests/a4_bench.cpp`。它链接 **clean 构建产出的** `libge_logic_bricks.a` 全部 113 个对象（这些就是 `bin/blender` 里同一批代码），不链接引擎主程序，以免窗口/渲染/音频线程噪声进入数字。注意：被测库代码是 Release `-O3 -DNDEBUG`（与 `bin/blender` 一致），基准 `main()` 为 `-O1`；计时主体是库内图执行，不受 `main` 优化级别影响。
- 计时：`std::chrono::steady_clock`，每轮 1000 次 `KX_GraphInstance::Tick(0.016f)`；预热 200 帧后才开始计时。
- Python 测量：`tests/a4_bench_py.py`，在 `build_clean_verify/bin/blender -b` 内用 `time.perf_counter_ns()` 执行同一 100 节点等价图（1 OnUpdate + 2 const + 96 math + 1 sink）。**这是纯 Python 参考实现，不是引擎内旧 Python 节点执行器**：旧执行器依赖 `uplogic/bge_netlogic` 运行时，本机未安装，启动不了；因此 (c) 只代表“同样图用 Python 解释执行”的成本下界/对照点，不可当作旧执行器的历史值。
- 机器：16 核，`-j4` 构建；基准测试期间其他线仍可能在跑增量构建，故一律用中位数，不取平均值/最优值。

## 2. 可重复命令

### 2.1 clean 构建（等价 `build_linux` 配置）

```bash
cd /home/eridanusepsilon/Code/EBC/EBC5.3
# 生成 build_clean_verify 的 configure（完整参数见附录 A）
bash tests/a1_clean_configure.sh
# 完整构建（-j4 给其他线留余量；exit=0）
ninja -C build_clean_verify -j4
```

### 2.2 重链 harness 并运行（16 个 C++ + GUI）

```bash
bash tests/a1_clean_harness.sh
```

### 2.3 A.4 基准（三条路径）

```bash
bash tests/a4_benchmark.sh
```

等价的单次手工命令：

```bash
# (a) 原生
env -u BGE_GAMENODE_INLINE A4_PATH_LABEL=native A4_BUILD_ID=<build_id> /tmp/a4_bench

# (b) 内联前
BGE_GAMENODE_INLINE=off A4_PATH_LABEL=inline_off A4_BUILD_ID=<build_id> /tmp/a4_bench

# (c) 纯 Python 等价图（跑在 clean 二进制里）
A4_PATH_LABEL=python_equiv A4_BUILD_ID=<build_id> \
  build_clean_verify/bin/blender -b --python tests/a4_bench_py.py
```

> 说明：`A4_PATH_LABEL` / `A4_BUILD_ID` 只影响输出行，不参与计时。

## 3. 规则（强制）

1. **此后所有性能对比以本基线为准。** 新优化必须报告相对 `9e15798a649a-clean_verify-3f074ee0eb1e` 的变化百分比。
2. **旧数字不可跨构建集合对比。** 只要满足任一条件，就必须重建基线：
   - git HEAD 变化；
   - `libge_logic_bricks.a` 的 sha256 前 12 位变化；
   - configure 参数变化（尤其 `CMAKE_BUILD_TYPE`、`WITH_UNITY_BUILD`、`WITH_COMPILER_PRECOMPILED_HEADERS`）；
   - 编译器/优化级别变化。
3. 比较时至少报告：路径、process runs、每轮帧数、内部轮数、节点数、`build_id`。
4. 内联/非内联必须用同一二进制、同一图、同一轮数；不能拿“内联开 + 旧二进制”对比“内联关 + 新二进制”。
5. 跨机器对比无效；本基线是 `16 核 Linux / Release / -march=x86-64-v2` 下的数字。
6. **跨时窗的绝对数不可比（§0.2）；重复精度必须按 harness 标注（§0.3）**：报告必须标明每个数字所属的时窗，并写明「harness + 统计量 + 运行次数」。`±1%` 仅属 `b661_perf` best-of；`a4_bench` 约 5%、`d1_benchmark` 约 6–7%。凡是用两次独立运行的绝对数算出的百分比，一律标「**在该双峰前提下不可复现**」，不得作为门禁或结论。

## 4. 已知偏差与限制

- 基准是“图执行”微基准，不含渲染、物理、场景遍历；不能直接外推为游戏帧率。
- 100 节点是纯值链（无 trigger 分支），内联收益/损耗不代表含分支图的收益。
- (c) 纯 Python 用 `blender -b` 启动，启动成本不计入；循环体内为普通 Python 函数与列表索引，不代表 Cython/生成代码路径。
- 机器非独占，中位数只降低噪声，不能消除其他构建线的 CPU 争用。
- **本机性能是双峰（§0.2）**：同一二进制整段落进快段（`310–321` µs）或慢段（`446–486` µs），整体差 `1.54×`。中位数只能在同一分段内抗噪，**不能跨分段平均或对比**；本基线（慢段）的 `2618.3 / 2514.7` 记录的是它自己那个时窗的落点（丢失原件 `2624.4 / 2525.2` 同段，但已不可复核）。
- `scaling_governor=powersave` 且逐核频率 `1.1–4.5 GHz` 跳变；`loadavg` 与 `taskset` 都不能作为分段判据（§0.2 两条反证）。
- 重复精度随 harness/统计量差一个量级（§0.3）：`b661_perf` best-of `±1%`、`a4_bench` median 约 5%、`d1_benchmark` median 约 6–7%。跨 harness 借用精度会把结论说强一个量级。

## 附录 A：`build_clean_verify` 的完整 configure 命令行

```bash
cmake -S . -B build_clean_verify -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DCMAKE_C_COMPILER=/usr/bin/cc \
  -DCMAKE_INSTALL_PREFIX=/home/eridanusepsilon/Code/EBC/EBC5.3/build_linux/bin \
  -DPYTHON_VERSION=3.13 \
  -DWITH_ALEMBIC=ON \
  -DWITH_ASSERT_ABORT=ON \
  -DWITH_ASSERT_RELEASE=OFF \
  -DWITH_AUDASPACE=ON \
  -DWITH_BLENDER=ON \
  -DWITH_BLENDER_THUMBNAILER=ON \
  -DWITH_BUILDINFO=ON \
  -DWITH_BULLET=ON \
  -DWITH_CLANG=OFF \
  -DWITH_CLANG_TIDY=OFF \
  -DWITH_CODEC_FFMPEG=ON \
  -DWITH_CODEC_SNDFILE=ON \
  -DWITH_COMPILER_ASAN=OFF \
  -DWITH_COMPILER_ASAN_EXTERN=OFF \
  -DWITH_COMPILER_CCACHE=OFF \
  -DWITH_COMPILER_CODE_COVERAGE=OFF \
  -DWITH_COMPILER_PRECOMPILED_HEADERS=ON \
  -DWITH_COMPILER_SHORT_FILE_MACRO=ON \
  -DWITH_COMPILER_SIMD=ON \
  -DWITH_CPU_CHECK=ON \
  -DWITH_CUDA_DYNLOAD=ON \
  -DWITH_CYCLES=ON \
  -DWITH_CYCLES_CUDA_BINARIES=OFF \
  -DWITH_CYCLES_CUDA_BUILD_SERIAL=OFF \
  -DWITH_CYCLES_DEBUG=OFF \
  -DWITH_CYCLES_DEBUG_NAN=OFF \
  -DWITH_CYCLES_DEVICE_CUDA=ON \
  -DWITH_CYCLES_DEVICE_HIP=ON \
  -DWITH_CYCLES_DEVICE_HIPRT=OFF \
  -DWITH_CYCLES_DEVICE_ONEAPI=OFF \
  -DWITH_CYCLES_DEVICE_OPTIX=ON \
  -DWITH_CYCLES_HIP_BINARIES=OFF \
  -DWITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF \
  -DWITH_CYCLES_KERNEL_ASAN=OFF \
  -DWITH_CYCLES_NATIVE_ONLY=OFF \
  -DWITH_CYCLES_ONEAPI_BINARIES=OFF \
  -DWITH_CYCLES_OSL=ON \
  -DWITH_CYCLES_PARALLEL_DEVICE_KERNEL_BUILD=OFF \
  -DWITH_CYCLES_PATH_GUIDING=ON \
  -DWITH_CYCLES_PRECOMPUTE=OFF \
  -DWITH_CYCLES_STANDALONE=OFF \
  -DWITH_CYCLES_STANDALONE_GUI=OFF \
  -DWITH_CYCLES_TEST_OSL=OFF \
  -DWITH_DLSS=ON \
  -DWITH_DOC_MANPAGE=OFF \
  -DWITH_DRACO=ON \
  -DWITH_DRAW_DEBUG=OFF \
  -DWITH_EMBREE=ON \
  -DWITH_EXPERIMENTAL_FEATURES=ON \
  -DWITH_FFTW3=ON \
  -DWITH_FREESTYLE=ON \
  -DWITH_FRIBIDI=OFF \
  -DWITH_GAMEENGINE=ON \
  -DWITH_GAMEENGINE_BPPLAYER=ON \
  -DWITH_GAMEENGINE_DECKLINK=ON \
  -DWITH_GAMEENGINE_SECURITY=OFF \
  -DWITH_GHOST_CSD=ON \
  -DWITH_GHOST_DBUS=ON \
  -DWITH_GHOST_DBUS_DYNLOAD=ON \
  -DWITH_GHOST_DEBUG=OFF \
  -DWITH_GHOST_SDL=OFF \
  -DWITH_GHOST_WAYLAND=ON \
  -DWITH_GHOST_WAYLAND_APP_ID= \
  -DWITH_GHOST_WAYLAND_DYNLOAD=ON \
  -DWITH_GHOST_X11=ON \
  -DWITH_GHOST_XDND=ON \
  -DWITH_GMP=ON \
  -DWITH_GPU_BACKEND_TESTS=OFF \
  -DWITH_GPU_COMPOSITOR_TESTS=OFF \
  -DWITH_GPU_DRAW_TESTS=OFF \
  -DWITH_GPU_MESH_PAINT_TESTS=OFF \
  -DWITH_GPU_RENDER_TESTS=OFF \
  -DWITH_GPU_RENDER_TESTS_HEADED=OFF \
  -DWITH_GPU_RENDER_TESTS_VULKAN=OFF \
  -DWITH_GPU_SHADER_ASSERT=OFF \
  -DWITH_GPU_SHADER_CPP_COMPILATION=ON \
  -DWITH_GTESTS=OFF \
  -DWITH_HARFBUZZ=OFF \
  -DWITH_HARU=ON \
  -DWITH_HEADLESS=OFF \
  -DWITH_HYDRA=OFF \
  -DWITH_IK_ITASC=ON \
  -DWITH_IK_SOLVER=ON \
  -DWITH_IMAGE_CINEON=ON \
  -DWITH_IMAGE_OPENJPEG=ON \
  -DWITH_IMAGE_WEBP=ON \
  -DWITH_INPUT_IME=ON \
  -DWITH_INPUT_NDOF=ON \
  -DWITH_INSTALL_COPYRIGHT=OFF \
  -DWITH_INSTALL_PORTABLE=ON \
  -DWITH_INTERNATIONAL=ON \
  -DWITH_IO_FBX=ON \
  -DWITH_IO_GREASE_PENCIL=ON \
  -DWITH_IO_PLY=ON \
  -DWITH_IO_STL=ON \
  -DWITH_IO_WAVEFRONT_OBJ=ON \
  -DWITH_JACK=ON \
  -DWITH_JACK_DYNLOAD=OFF \
  -DWITH_LIBMV=ON \
  -DWITH_LIBMV_SCHUR_SPECIALIZATIONS=ON \
  -DWITH_LIBS_PRECOMPILED=ON \
  -DWITH_LINKER_LLD=ON \
  -DWITH_LINKER_MOLD=ON \
  -DWITH_LINUX_OFFICIAL_RELEASE_TESTS=OFF \
  -DWITH_LLVM=OFF \
  -DWITH_MANIFOLD=ON \
  -DWITH_MATERIALX=OFF \
  -DWITH_MEM_VALGRIND=OFF \
  -DWITH_MESHOPTIMIZER=ON \
  -DWITH_MOD_FLUID=ON \
  -DWITH_MOD_OCEANSIM=ON \
  -DWITH_MOD_REMESH=ON \
  -DWITH_NANOVDB=ON \
  -DWITH_NINJA_POOL_JOBS=ON \
  -DWITH_OPENAL=ON \
  -DWITH_OPENGL_BACKEND=ON \
  -DWITH_OPENIMAGEDENOISE=ON \
  -DWITH_OPENSUBDIV=ON \
  -DWITH_OPENVDB=ON \
  -DWITH_OPENVDB_3_ABI_COMPATIBLE=OFF \
  -DWITH_OPENVDB_BLOSC=ON \
  -DWITH_OPTIMIZED_BUILD_TOOLS=ON \
  -DWITH_PIPEWIRE=ON \
  -DWITH_PIPEWIRE_DYNLOAD=OFF \
  -DWITH_PLAYER=ON \
  -DWITH_POTRACE=ON \
  -DWITH_PUGIXML=ON \
  -DWITH_PULSEAUDIO=ON \
  -DWITH_PULSEAUDIO_DYNLOAD=OFF \
  -DWITH_PYTHON=ON \
  -DWITH_PYTHON_INSTALL=ON \
  -DWITH_PYTHON_INSTALL_NUMPY=ON \
  -DWITH_PYTHON_INSTALL_REQUESTS=ON \
  -DWITH_PYTHON_INSTALL_ZSTANDARD=ON \
  -DWITH_PYTHON_MODULE=OFF \
  -DWITH_PYTHON_NUMPY=ON \
  -DWITH_PYTHON_SAFETY=OFF \
  -DWITH_PYTHON_SECURITY=ON \
  -DWITH_QUADRIFLOW=ON \
  -DWITH_RENDERDOC=OFF \
  -DWITH_RUBBERBAND=ON \
  -DWITH_SDL_AUDIO=ON \
  -DWITH_STATIC_LIBS=OFF \
  -DWITH_STORM_TESTS_VULKAN=OFF \
  -DWITH_STRICT_BUILD_OPTIONS=OFF \
  -DWITH_STRSIZE_DEBUG=OFF \
  -DWITH_SYSTEM_AUDASPACE=OFF \
  -DWITH_SYSTEM_BULLET=OFF \
  -DWITH_SYSTEM_FREETYPE=OFF \
  -DWITH_SYSTEM_GFLAGS=OFF \
  -DWITH_SYSTEM_GLOG=OFF \
  -DWITH_SYSTEM_PYTHON_TESTS=OFF \
  -DWITH_TBB=ON \
  -DWITH_TBB_MALLOC_PROXY=ON \
  -DWITH_TESTS_BATCHED=ON \
  -DWITH_TESTS_EXPERIMENTAL=OFF \
  -DWITH_TESTS_SINGLE_BINARY=ON \
  -DWITH_TRACY=OFF \
  -DWITH_UI_TESTS=OFF \
  -DWITH_UI_TESTS_HEADLESS=OFF \
  -DWITH_UI_TESTS_VULKAN=ON \
  -DWITH_UNITY_BUILD=ON \
  -DWITH_USD=OFF \
  -DWITH_UV_SLIM=ON \
  -DWITH_VULKAN_BACKEND=ON \
  -DWITH_X11_XF86VMODE=ON \
  -DWITH_X11_XFIXES=ON \
  -DWITH_X11_XINERAMA=ON \
  -DWITH_X11_XINPUT=ON \
  -DWITH_XR_OPENXR=ON
```

`CMAKE_INSTALL_PREFIX` 是原样抄写的；本次没有执行 `ninja install`，因此它不影响任何产物。
