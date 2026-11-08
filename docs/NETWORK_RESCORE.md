# 网络域重打分（B6.9-N，2026-11-06）

## 0. 新口径（原文照抄）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：

- **full**：分母计入，分子 1.0
- **approximate**：分母计入，分子 0.5（必须有书面明确限制）
- **unsupported**：引擎层不支持，附 `路径:行号` 可核证据，进 FOREVER，不进分母
- **pending-device**：分母计入，分子 1.0，真机待验单列

pending-device 必须同时满足三条：① 节点有行为测试；② Mock/Headless E2E 通过；③ 阻碍 headless 验证的是具体设备/驱动依赖并给出 API 位置——不是「我们没测」。

**本域的分法**（按本轮指令的关键区分）：

- 「本机两进程可验」→ **full**（`c31_udp_two_proc`、LAN 发现、状态同步这类已经跑通的）；
- 「必须两台机器 / 真实 LAN 才可验」→ **pending-device 子项**（单列进真机清单，不进节点分子，回答「跨机可用性」，不回答「代码侧成熟度」）。

两者不混算：本文件的严格/加权百分比只算代码侧；跨机另设一节，不与节点分母相除。

---

## 1. 证据基线

分母 = `kB6NetSpecs` 的 14 条注册项（`source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Net.cpp:823-857`），不是 11（旧 `CAPABILITY_WEIGHT_AUDIT.md` §2.6 已由 11 改 14）。行号以 2026-11-06 15:40 工作树为准。

| 日志 / 批次 | 时点 | 结果 | 性质 |
|---|---|---|---|
| `tests/.e2e_out/run_20261106-094520/c31_udp.log` | 09:45 | 14 PASS / 0 FAIL | **干净树**（早于他线 13:07 在途改动） |
| `.../c32_nodes.log` | 09:45 | 70 PASS / 0 FAIL | 干净树 |
| `.../c32_rooms.log` | 09:45 | 32 PASS / 0 FAIL | 干净树 |
| `.../b63_network.log` | 09:45 | 17 PASS / 0 FAIL | 干净树 |
| `.../b63_nodes.log` | 09:45 | 36 PASS / 0 FAIL | 干净树 |
| `tests/.e2e_out/run_20261106-132736/c31_udp_two_proc.log` | 13:27 | 14 PASS / 0 FAIL | 两进程真实 UDP |
| 同目录 `n1_discover_two_proc.log` | 13:27 | 14 PASS / 0 FAIL | 两进程 LAN 发现 |
| 同目录 `n2_state_sync_two_proc.log` | 13:27 | 14 PASS / 0 FAIL | 两进程状态同步 |

干净树基线小计 **169 PASS / 0 FAIL**（c31 14 + c32_nodes 70 + c32_rooms 32 + b63_network 17 + b63_nodes 36 = 169）；`run_20261106-132736` 再并入 `c31_udp_two_proc` 14 + `n1` 14 + `n2` 14，合计 **211 PASS / 0 FAIL**。

**如实披露**：13:27–13:28 的三批（`c31_udp_two_proc`、`n1`、`n2`）构建时，他线 `KX_FileAdapter.cpp` 正处于半途编辑态（缺 `operator==`），harness 用 `HARNESS_DROP_TU=KX_FileAdapter.cpp` 排除了这个**与本域无关**的 TU。网络域全部 TU（`KX_NetworkAdapterEngine.cpp`、`KX_FunctionNodesB6Net.cpp`）正常编译链接，未使用 shim、未使用 `/tmp` 补丁头文件。本轮不再沿用该手段；干净树数字来自 09:45 那批，不来自 13:28 那批。

**本轮不编译、不跑 `ninja`**（只读 + 只写本文件），因此不存在新的构建污染。

---

## 2. 14 节点逐节点表

### 2.1 房间控制面（3）

| # | 节点 | 分类 | 代码位置（引擎 / 注册） | Headless / Mock E2E 证据 | 限制 |
|---|------|------|------------------------|--------------------------|------|
| 1 | `HostRoom` | **full** | `KX_NetworkAdapterEngine.cpp:437` / `KX_FunctionNodesB6Net.cpp:824` | `c32_nodes_e2e.cpp:189`（图节点）；`:426`、`:572`（真引擎图，含重开房）；`b63_nodes_e2e.cpp:197`；`c32_rooms_e2e.cpp:140,185` | 无 |
| 2 | `JoinRoom` | **full** | `:518` / `:825` | `c32_nodes_e2e.cpp:447`（真引擎图）；`c32_rooms_e2e.cpp:144`；`b63_network_e2e.cpp:71` | 无 |
| 3 | `LeaveRoom` | **full** | `:655` / `:826` | 适配器级：`b63_network_e2e.cpp:110,166`；`c31_udp_e2e.cpp:294-295`；`c31_udp_two_proc.cpp:296,369`；`n1_discover_two_proc.cpp:226`；`n2_state_sync_two_proc.cpp:289,435` | **节点包装器未图级执行**：函数体只做 `ctx.Network().LeaveRoom()` + 单触发器 `OnDone`，无分支，透传风险为 0；但严格说这是适配器证据，不是节点图证据 |

### 2.2 消息与属性同步（5）

| # | 节点 | 分类 | 代码位置（引擎 / 注册） | Headless / Mock E2E 证据 | 限制 |
|---|------|------|------------------------|--------------------------|------|
| 4 | `OnNetworkEvent` | **full** | `:827-834`（tick/pending/element 三段） | `c32_nodes_e2e.cpp:194`、`:450`（真引擎图收消息）；`b63_nodes_e2e.cpp:202` | 每帧最多排空 64 条（`FuncOnNetworkEventTick` 的 `guard < 64`），超出的留到下帧；非丢弃 |
| 5 | `SendNetworkMessage` | **full** | `:684`（可靠）/ `:689`（不可靠）/ `:835` | `c32_nodes_e2e.cpp:190`（`reliable=true`）、`:477`（`reliable=false`）；`b63_nodes_e2e.cpp:198`；`b63_network_e2e.cpp:88` | `reliable` 走 TCP、否则走 UDP，语义由配置决定，无自动降级 |
| 6 | `BroadcastMessage` | **full** | `:904` / `:839` | `c32_nodes_e2e.cpp:191`；`b63_nodes_e2e.cpp:199`；`b63_network_e2e.cpp:136`（房主广播→客户端收） | 仅可靠广播（`BroadcastReliable`），无 UDP 广播消息 |
| 7 | `SyncProperty` | **full** | `:920` / `:840` | `c32_nodes_e2e.cpp:192`；`b63_nodes_e2e.cpp:200`；`b63_network_e2e.cpp:158-159`（主机读到子客户端字符串属性） | 端口数随属性数动态（`B6_PORTFLEX`），端口契约有 `port_contract.py` 校验 |
| 8 | `GetRemoteProperty` | **full** | `:939` / `:841` | `c32_nodes_e2e.cpp:193`；`b63_nodes_e2e.cpp:201`；`b63_network_e2e.cpp:102-103` | 返回最近一次收到的快照，不请求、不阻塞 |

### 2.3 房间列表与重连（3）

| # | 节点 | 分类 | 代码位置（引擎 / 注册） | Headless / Mock E2E 证据 | 限制 |
|---|------|------|------------------------|--------------------------|------|
| 9 | `ListRooms` | **full** | `:743` / `:846` | `c32_nodes_e2e.cpp:285`（Mock 适配器，检查 `:298-301`）、`:508`（真引擎，检查 `:521`）；`c32_rooms_e2e.cpp:159-163`、`:177-178`（空列表） | 固定 300 ms 超时（`KX_NetworkAdapterEngine.cpp:823` 的 `ListRooms: no room-list reply within 300 ms`），真实 WAN 下可能不够（见 §3.1 第 9 项） |
| 10 | `RefreshRooms` | **full** | `:835` / `:847` | `c32_nodes_e2e.cpp:310`（检查 `:323-326`）、`:544`（检查 `:560-562`）；`c32_rooms_e2e.cpp:168` | 实现是 `ListRooms` 的直接别名（`:840` `return ListRooms(host, port, out, error);`），不维护缓存，无「刷新间隔」语义 |
| 11 | `Reconnect` | **full** | `:843` / `:848` | `c32_nodes_e2e.cpp:336`（检查 `:345-349`、`:367-368`）、`:591`（检查 `:604-608`）；`c32_rooms_e2e.cpp:194-197`、`:239-246`（重连失败分支） | 次数/退避由输入端口给定（`max_attempts`、`backoff_ms`），不做自适应；房主不能重连自己（`:849`） |

### 2.4 发现与玩家状态（3）

| # | 节点 | 分类 | 代码位置（引擎 / 注册） | Headless / Mock E2E 证据 | 限制 |
|---|------|------|------------------------|--------------------------|------|
| 12 | `DiscoverRooms` | **full** | `:992`（全函数，末行为 `:1113` 的双路 `sendto` 失败分支）/ `:850` | `n1_discover_two_proc`（两进程，14 PASS / 0 FAIL）；节点用例 `n1_discover_two_proc.cpp:247,248,299`；伪造 `PKT_ROOMINFO` 不应答、伪造 `PKT_DISCOVER` 应答应答、host 停止后房间消失 | **无图级节点执行测试**（`c32_nodes_e2e.cpp` 不含该节点），只有引擎级两进程 + 端口/元数据契约；广播只在广播 `sendto` 失败时才回退 loopback（`:1113`） |
| 13 | `SyncPlayerState` | **full** | `:975`（`Tick()` → `PumpStateSync`）、`:1128` / `:852` | `n2_state_sync_two_proc`（两进程，14 PASS / 0 FAIL）；主机位姿 A→B、客户端位姿 C 双向可见；停 Tick 200 ms 模拟丢 tick，`sequence_delta=5`、保持上一位姿 | 20 Hz 定频、16 位定点（位置 1 cm、旋转 0.01°、动画相位 1/65535）；**无插值/外推**；**无图级节点执行测试** |
| 14 | `GetPlayerState` | **full** | `:1134` / `:856` | `n2_state_sync_two_proc` 断言 `GetRemotePlayerState`（主机读到客户端 C：`pos=-1.5,0.25,7`）；未更新玩家返回 false | **无图级节点执行测试**；返回最近收到的快照，不触发请求 |

---

## 3. 三类合计与百分比

| full | approximate | unsupported | pending-device（节点级） | 分母 |
|---:|---:|---:|---:|---:|
| 14 | 0 | 0 | 0 | **14** |

- **严格**（只 full 记 1）：14 / 14 = **100%**
- **加权**（full 1.0、approximate 0.5、pending-device 1.0；unsupported 不进分母）：(14×1.0 + 0×0.5 + 0×1.0) / 14 = 14 / 14 = **100%**
- **覆盖率**（14 个节点均有代码位置 + 至少一条可跑 E2E）：14 / 14 = **100%**
- **图级节点执行覆盖**（`c32_nodes_e2e.cpp` 直接 `AddNode` 并跑图的节点）：10 / 14 = **71.4%**。缺的 4 个是 `LeaveRoom`、`DiscoverRooms`、`SyncPlayerState`、`GetPlayerState`——它们有引擎/适配器级两进程 E2E，但没有节点图 E2E。这个 71.4% 不是成熟度，是**证据强度**指标，单列以免把「引擎通过」说成「节点图通过」。

### 新旧口径对照

| 口径 | 分子 / 分母 | 百分比 | 跨机 |
|---|---|---|---|
| 旧（上一轮报告） | 实现 14 / 14 | **100%**（无加权概念） | 0 / 14（真机/跨机） |
| 新·严格 | full 14 / 14 | **100%** | 0 / 14（单列，不进节点分母） |
| 新·加权 | (14×1.0) / 14 | **100%** | 同上 |
| 新·覆盖 | 14 / 14 | **100%** | — |

**结论与预期表的关系**：本轮指令 §5 的预计表把网络写成「70% → 85% → 90%+」。按本域可核证据，**70% 在本域找不到对应的分子分母**——14 个节点在干净树上全部有通过记录。70% 只可能来自「把跨机 0/14 塞进分母」这类混算（例如 14/20）。新口径做的正是把它拆开：**代码侧 14/14 = 100%，跨机可用性 0/14 已验、14/14 待验**。所以网络域对全局 95% 的贡献不是「涨了 15 个点」，而是「不再用 0/14 把本域拉低」。

### 3.1 pending-device 子项（跨机真机清单，单列，不扣分）

这些是节点内部的**真实 LAN 依赖项**，节点本身已 full。按口径，即便整体按 pending-device 记也是 1.0，分数不变；它们的用途是给 `REAL_DEVICE_VERIFICATION.md` 供货。

| # | 待验项 | 所在节点 | ① 行为测试 | ② Headless E2E | ③ 设备/驱动依赖 + API 位置 |
|---|---|---|---|---|---|
| 1 | 真实网卡（非 `lo`、非容器网桥）上的 UDP 广播可达 | `DiscoverRooms` | `n1_discover_two_proc.cpp:247-299` | 14/0 | 本机只验了 `127.0.0.1` 与 `172.19.0.80` 网桥；广播发送 `KX_NetworkAdapterEngine.cpp:1087-1113`，`SO_BROADCAST` 设置在 `DiscoverRooms` 函数体内 |
| 2 | 交换机/防火墙丢弃广播时仍能按窗口结束返回空列表 | `DiscoverRooms` | 同上（`n1_discover_two_proc.cpp:299`） | 14/0 | 200 ms × 5 次窗口；丢广播时没有重传，只有窗口耗尽，逻辑位置 `:992-1113` |
| 3 | 两台机器时钟差与 RTT > 200 ms 时 1 s 窗口是否足够 | `DiscoverRooms` | 同 1 | 14/0 | 默认窗口 1000 ms，上限 5000 ms；跨机 RTT 未测 |
| 4 | 同网段多房主时 `ip:port` 去重键是否够（同 IP 多实例、NAT 后同源） | `DiscoverRooms` | 同 1 | 14/0 | 去重键为源 `host:port`，`DiscoverRooms` 收集循环内；本机只验了单房主 |
| 5 | 广播源地址选择：多网卡（有线+无线+VPN）时回包走哪块网卡 | `DiscoverRooms` | 同 1 | 14/0 | `bind` 到 `INADDR_ANY`（`:1034` 附近），回包源由内核路由决定 |
| 6 | 真实 LAN 丢包/乱序下 20 Hz 位姿的平滑度 | `SyncPlayerState` / `GetPlayerState` | `n2_state_sync_two_proc.cpp` 全套 | 14/0 | loopback 丢包率≈0；「丢 tick」是人为停 Tick 模拟，不是真丢包；收包与序列比较在 `HandleStateDatagram` / `SequenceNewer`（同文件内的辅助函数，可按符号名定位） |
| 7 | 跨机 34 B/包 × 20 Hz × N 玩家的实际带宽与 CPU | `SyncPlayerState` | 同上 | 14/0 | 包长 16 头 + 18 负载；8 人星形约 5.3 KiB/s 为估算，非实测（`B6_NETWORK.md` §12） |
| 8 | 已连接客户端在房主进程消失后的断线感知时延 | `OnNetworkEvent` | `b63_network_e2e.cpp:164`（PlayerLeave，同机） | 17/0 | 同机走 TCP FIN/EOF；真机掉线（拔网线/断电）依赖 TCP 超时，未测 |
| 9 | 跨机 TCP 控制面（`JoinRoom`/`ListRooms`/`Reconnect`）在真实 RTT 下是否超时 | `ListRooms` / `Reconnect` | `c32_rooms_e2e.cpp:159-178`、`:194-246` | 32/0 | 固定 300 ms 房间列表超时，`:823`；重连退避由端口给定，`:843` |
| 10 | 真实 UDP 缓冲区溢出边界（高包率下丢包率） | `SendNetworkMessage`（`reliable=false`） | `c31_udp_e2e.cpp:14 PASS` | 14/0 | 同机已测到内核缓冲边界；真实网卡/驱动队列未测 |
| 11 | 状态同步在真实 MTU/分片与 IPv6-only 网络下 | `SyncPlayerState` | 同 6 | 14/0 | 代码按 IPv4 数字地址解析（`ListRooms` 拒绝非数字 IPv4，`:769`），无 IPv6 路径 |
| 12 | 休眠/唤醒后 socket 是否需重建 | 全部房间节点 | `c32_rooms_e2e.cpp:194-197`（同机重连） | 32/0 | 唤醒不对 socket 做探测；`Reconnect` 是显式调用，`:843` |
| 13 | NAT 后的「手填 IP」路径（端口映射是否可达） | `JoinRoom` | `b63_network_e2e.cpp:71` | 17/0 | 代码只做 `connect`，不做映射探测；NAT 穿透本身 FOREVER（§5） |
| 14 | 跨机广播与单播混合时的应答风暴边界 | `DiscoverRooms` | 同 1（「收到 ROOMINFO 不应答」） | 14/0 | 规范位置 `B6_NETWORK.md` §11；真实网络多房主并发未测 |

判定：14 条均满足「有行为测试 + headless 通过 + 阻碍是网卡/网络拓扑依赖且有 API 位置」，符合 pending-device 定义。**结论：跨机可用性 0 / 14 已验，14 / 14 待验**；不进代码侧分母。

---

## 4. 为什么本域没有 approximate / unsupported / pending-device 节点

- **approximate = 0**：14 个节点没有「实现完成但有书面限制、只能记 0.5」的项。`RefreshRooms` 是 `ListRooms` 别名、`OnNetworkEvent` 有 64/帧排空上限、`SyncPlayerState` 无插值——这些是**语义选择**且已写进 `B6_NETWORK.md` §11/§12，不是「做不到一半」，不适用 0.5。
- **unsupported = 0**：NAT 穿透、回滚、预测**不是节点**，是能力边界，按口径进 FOREVER、不进分母（§5）。14 个节点本身在引擎里都有实现，没有「引擎层不支持」的节点。
- **pending-device 节点 = 0**：14 个节点都在本机两进程/无头下跑通了；跨机差异落在**子项**（§3.1），不是整个节点。把整节点记作 pending-device 会把「已跑通的本机语义」也标成未验，属于反向失真。
- 需要单独盯的**证据强度短板**（不是分类问题）：`LeaveRoom`、`DiscoverRooms`、`SyncPlayerState`、`GetPlayerState` 无图级节点执行测试（§3 的 10/14 = 71.4%）。补这 4 条属于「补断言」，不是「补实现」。

---

## 5. FOREVER 项（不进分母，逐条给可核位置）

分母是 14 个节点；下列能力项本来就不是节点，列在这里只为说明「它们没有被偷偷算进 100%」。

| 项 | 可核位置 | 仍然 FOREVER 的理由 |
|---|---|---|
| NAT 穿透 / 打洞 / 中继服务器 | `B6_NETWORK.md:11`、`:106-107` | 文档明写 LAN-only、无 NAT 遍历、无中继；引擎侧只有 TCP `listen`（`KX_NetworkAdapterEngine.cpp:437` 起的 `HostRoom`）与 UDP `bind`（`:1413-1428`），没有 STUN/TURN/UPnP 任何入口 |
| 客户端预测 / 延迟补偿 | `B6_NETWORK.md:11`、`:83`、`:106-107` | 没有权威帧、没有快照回放、没有外推；`GetRemotePlayerState`（`:1134`）只返回最近收到的位姿，`SyncPlayerState` 的 `PumpStateSync` 只发本地真实位姿 |
| 状态回滚（本地消息/状态回滚） | `B6_NETWORK.md:205` | `OnFail` 只报告失败，不回滚已写状态；文档明确「不做本地消息回滚」 |

补充可核性说明：`DEFERRED_FOREVER.md:291-327`（§B6.9 段）**没有** NAT/回滚/预测条目，所以这三项的理由权威在 `B6_NETWORK.md`，不在 `DEFERRED_FOREVER.md`。本轮未改 `DEFERRED_FOREVER.md`（终稿归 K 线）。

---

## 6. 本轮边界

- 未修改 `NUMBER_CONSISTENCY_AUDIT.md` 附录 B（不追加任何编号；K 线单点重写）。
- 未改任何 TOTAL / 全局百分比文档；本文件是新增的独立重打分表，不改 `850`/`851`/`957`/`1172`（这些为该文写作时的历史值，K 冻结后全量为 **1456 / 46 批**，见 `NUMBER_CONSISTENCY_AUDIT.md` B.5）。
- 未编译、未跑 `ninja`；未删 `build_linux`；未用 shim / `HARNESS_DROP_TU` / `/tmp` 补丁头文件。
- 未 commit / push；未碰 7 个未跟踪 `_lib.glsl`。
- 行号以 2026-11-06 15:40 工作树为准；若他线改动 `KX_NetworkAdapterEngine.cpp` 行号会漂移，届时按符号名复核。
