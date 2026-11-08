# B6.3 / B6.7 C.3 网络（LAN 基础版 + UDP / 房间列表 / 重连）

日期：2026-11-05
范围：B6.3 的 8 个节点，加 B6.7 C.3 的 `ListRooms`、`RefreshRooms`、`Reconnect`
运行时：`KX_FunctionNodesB6Net.cpp`（表驱动，`kB6NetSpecs`）
后端：`KX_NetworkAdapterEngine.cpp`（裸 BSD socket，不少任何第三方网络库）

## 1. 范围与结论

- 只做 **同一 LAN 内的星型房间**：一个 host 监听，客户端直连 host。
- **不做 NAT 穿透、不做回滚（rollback）、不做客户端预测**。链路语义就是「发出去的消息/属性快照」，不是确定性的同步状态机。
- 所有 socket 均为非阻塞；有界等待只有三处：`JoinRoom` 的 connect 超时（300 ms）与 welcome 超时（100 ms）、`ListRooms/RefreshRooms` 的 300 ms 应答窗口、`Reconnect` 的有限次退避重试。
- 主机掉线、端口被占用、连接失败、peer 断开都有明确失败路径和 stderr 诊断。

## 2. 接入位置

| 层 | 文件 | 位置 |
|---|---|---|
| 适配器接口 | `source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h` | `KX_NetworkEvent` 1505、`KX_NetworkAdapter` 1534、`KX_MockNetworkAdapter` 1611、`KX_AdapterSet::network` 1673、`KX_NullNetworkAdapter()` 1697、`KX_CreateEngineNetworkAdapter()` 1699 |
| Null / Mock | `.../KX_GameAdapters.cpp` | Null 注入、`BeginFrame` 调用 `network->Tick()` 714、Mock 实现 1081-1245 |
| 引擎实现 | `.../KX_NetworkAdapterEngine.cpp` | 1079 行；非阻塞 socket、帧编解码、Host/Join/Leave/Send/Property/Poll/Tick |
| 引擎装配 | `.../KX_GameAdaptersEngine.cpp` | `set->network = KX_CreateEngineNetworkAdapter();` 2052 |
| 执行上下文 | `.../KX_ExecutionContext.h/.cpp` | `KX_NetworkAdapter &Network();` 188 / 438 |
| 节点实现 | `.../KX_FunctionNodesB6Net.cpp` | 监听/加入/离开 77-150、事件 152-246、发送 252-298、属性 304-356、表 358-380、注册 409 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6NetworkFunctionGameNodes()` 248 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 243 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | `KX_NetworkAdapterEngine.cpp` 224、`KX_FunctionNodesB6Net.cpp` 225 |
| 编辑器表 | `scripts/startup/bge_gamenodes/editor/nodes/native_b6c.py` | `NATIVE_B6C` 49-119 |

适配器接口按任务给定的 11 个方法实现，另加 `Tick()`、`GetRole()`、`IsConnected()`、`LastError()`（可增不可删）。

## 3. 节点与端口

| # | 节点 | 输入 | 输出 | config |
|---|---|---|---|---|
| 1 | HostRoom | OnEnter, Port(Int), MaxPlayers(Int) | OnSuccess, OnFail | `room_name` string（缺省 `room`） |
| 2 | JoinRoom | OnEnter, Host(String), Port(Int) | OnSuccess, OnFail, LocalPlayerId(Int) | 无 |
| 3 | LeaveRoom | OnEnter | OnDone | 无 |
| 4 | OnNetworkEvent | — | OnPlayerJoin, OnPlayerLeave, OnMessage, OnDisconnect, PlayerId(Int), Data(String) | `event_type` enum：0 any / 1 join / 2 leave / 3 message / 4 disconnect |
| 5 | SendNetworkMessage | OnEnter, TargetId(Int), Data(String) | OnSuccess, OnFail | `reliable` bool（缺省 true） |
| 6 | BroadcastMessage | OnEnter, Data(String) | OnSuccess | 无 |
| 7 | SyncProperty | OnEnter, Key(String), Value(动态) | OnDone | 无 |
| 8 | GetRemoteProperty | OnEnter, PlayerId(Int), Key(String) | Value(动态), OnSuccess, OnFail | 无 |
| 9 | ListRooms | OnEnter, Host(String), Port(Int) | OnSuccess, OnFail, Rooms(List), Count(Int), Error(String) | 无 |
| 10 | RefreshRooms | OnEnter, Host(String), Port(Int) | OnSuccess, OnFail, Rooms(List), Count(Int), Error(String) | 无 |
| 11 | Reconnect | OnEnter | OnSuccess, OnFail, Attempts(Int), Status(String) | `max_attempts` int（缺省 3）、`backoff_ms` int（缺省 250） |

`SyncProperty` / `GetRemoteProperty` 走 `ports_from_ir`（`B6_PORTFLEX`），IR 里 `Value` 的实际类型决定运行时读写类型；`GetRemoteProperty` 失败时按 Float 0 输出并清掉其它值输出。编辑器侧注册后设置 `native_dynamic_ports = True` 并挂 `resolve_polymorphic`（`native_b6c.py` 121-151）。

## 4. 线上协议（TCP + UDP，v2）

- 16 字节帧头：magic `"B6N1"`(U32)、version U16、type U8、flags U8、sender I32、payload length U32（`KX_NetworkAdapterEngine.cpp` 67-69、261-287）。
- 包类型：`HELLO=1`、`WELCOME=2`、`JOIN=3`、`LEAVE=4`、`MESSAGE=5`、`PROPERTY=6`；C.3 新增 `ROOMLIST=7`（请求，空 payload）、`ROOMINFO=8`（应答：name(U16+bytes)、players(I32)、max_players(I32)），两者只走 UDP。
- **协议版本递增为 2**。帧头与六类 v1 包的类型号/语义不变，只追加字段：
  - `WELCOME`：`assigned_id(I32)` 之后追加 `host_udp_port(U16)`；
  - `HELLO`：客户端收到 WELCOME 后回送 `client_udp_port(U16)`（v1 的 HELLO 未使用）。
  - 刻意递增版本号：v1 对端会被帧头版本检查干净拒绝，不会出现「TCP 能连、UDP 不可用」的半兼容。
- 字符串/属性值长度字段是 **U16**（`ReadU16`）；这是首轮两进程 E2E 失败后修正的协议 bug（之前按 U32 读导致 `bad frame header`）。
- **可靠通道**：`SendReliable` / `BroadcastReliable` 走 TCP outbox（上限 256 包，不丢、保序）。
- **不可靠通道**：`SendUnreliable` 走独立 `O_NONBLOCK` UDP socket；host 在 TCP 监听端口同号的 UDP 端口 `bind`，client `bind` 临时端口，双方通过 v2 握手交换端口。单包 payload 上限 `65507 - 16` 字节。UDP 无重传、无拥塞控制、不保证顺序，丢包由内核/网络决定。**本通道不做可靠层**：没有 ACK、序号、重传队列或拥塞窗口，上层需要可靠语义时必须改用 `SendReliable`。
- UDP 收包校验：host 只接受已登记 `(TCP 源 IP, HELLO 端口)` 的 peer，client 只接受 host 的 `(IP, WELCOME 端口)`；`sender` 必须等于该来源 peer 的 id，否则丢弃（防伪造）。

## 5. 非阻塞与失败路径

- 监听/接受/收发全部 `O_NONBLOCK`（TCP 与 UDP 同）；`Tick()` 用 `poll` 轮询，无阻塞读。UDP 每帧最多 drain 256 个 datagram，剩余留在内核缓冲等下一帧。
- `JoinRoom` 的 `connect()` 用非阻塞 + `poll(300 ms)`，随后在有界 `poll(100 ms)` 内等 `WELCOME`；超时返回 false 并把原因写入 `LastError()`。C.3 起 welcome 超时不再返回「成功但 id=-1」，而是显式失败。
- `ListRooms` 查询有 300 ms 窗口；无应答即 `OnFail`，`Error`/`LastError()` 写 `no room-list reply within 300 ms`。
- `HostRoom` 校验 1..65535 端口、`max_players >= 1`；`bind()` 失败（含端口占用）带 `strerror` 诊断。
- 连接断开：`MarkDead(peer)` 写 `GameNode: network <host|client> dropped peer <id> (<reason>)`（794-806）并入队 `Disconnect` / `PlayerLeave` 事件。
- 节点侧失败统一走 `OnFail` 或 stderr：`SendNetworkMessage` 失败会打印目标与 `LastError()`（272-277）。

## 6. 事件展开（P3.5 pending）

`OnNetworkEvent` 是零触发输入的条件节点：

- `Tick` 每帧最多 drain 64 个 `PollEvent`，按 `event_type` 过滤后存进节点状态（195-213），**不阻塞 tick**。
- `FuncOnNetworkEventPending` 返回事件数；`...Element` 对每个事件输出 `PlayerId`/`Data` 并点亮对应触发；`...Finish` 释放缓冲（215-246）。
- 帧顺序是 Tick → 值节点 → 触发传播，所以**同一个帧内由 `OnNetworkEvent` 点亮的下游触发节点能看到正确的 PlayerId/Data**；但普通值节点读到的是上一帧的值。

## 7. 同步语义：diff + 快照

- `SyncProperty` 把 key/value 写进本地快照表（Mock 里是 `local_props`，引擎实现是 `PropertyKeyValue`），并以 `PROPERTY` 包广播给所有 peer；接收端按 `player_id` 存进 `remote_props`。
- 没有权威状态校验、没有冲突合并、没有预测。属性值是**最后一次收到的快照**。
- `GetRemoteProperty` 是本地查表，不会发起网络请求。

## 8. 房间列表与重连（C.3.2）

### 8.1 ListRooms / RefreshRooms

- 不做发现协议：节点输入就是用户手填的 `Host` / `Port`（IPv4 数字地址）。
- 查询走 UDP：向 host 的 UDP 端口（与 TCP 监听端口同号）发 `ROOMLIST`，host 在自己的 `Tick()` 里收到后回 `ROOMINFO`，带房间名、当前人数、人数上限。300 ms 内无应答则 `OnFail` + 非空 `Error`，不会挂起或静默失败。
- 房间名来自 `HostRoom` 的 `room_name` config（缺省 `room`），写入 `SetRoomName`；当前人数来自 host 的 peer 表（host 计 1）。
- `RefreshRooms` 与 `ListRooms` 语义相同，只是编辑器上区分「重新拉取」的意图；引擎实现复用同一条查询。
- 因为应答由 host 的 Tick 驱动，host 未运行/未 tick 时查询会超时失败——这是预期行为，不是 bug。

### 8.2 Reconnect

- 适配器记录最近一次 `JoinRoom(host, port)`；显式 `LeaveRoom()` 会清空该记录，避免误连回已离开的房间。
- `Reconnect(max_attempts, backoff_ms)`：已连接直接成功（`attempts=0`）；host 角色明确失败；否则按指数退避（`backoff_ms * 2^(n-1)`，上限 4000 ms）重试。
- 成功条件必须是「TCP 已连接且收到 `WELCOME`」（`local id >= 0`），半开连接不算成功；每次失败先清理 socket/role/local id 再重试。
- `max_attempts` 夹在 1..10。节点输出 `OnSuccess` / `OnFail`、`Attempts(Int)`、`Status(String)`；失败时 `Status` 非空并写明最后一次 `JoinRoom` 错误。
- 只有客户端能 `Reconnect`；host 退出后需要自己重新 `HostRoom`。

## 9. 已知限制（真实失败项/边界）

1. 仅 LAN 直连；无 NAT 穿透、无中继服务器。
2. 无客户端预测、无回滚；高延迟下表现就是「晚到」。
3. UDP 通道**无重传、无拥塞控制、无顺序保证**，允许丢包与乱序；只承诺「已到达的消息内容正确」。
4. 一个房间只有一个 host；host 退出后客户端只收到 `Disconnect`，需要显式调用 `Reconnect`，且只会重连到原来记录的 `host:port`，不会迁移到其它房间。
5. `MaxPlayers` 只限制接入数量（host 计入 1），不做排队/踢人策略。
6. `Reconnect` 在节点 Action 内同步执行退避等待，最坏会占用当前帧约 `max_attempts * (connect 300 ms + welcome 100 ms) + 退避`；这是为了让失败状态确定，不引入后台线程。
7. 房间列表没有发现协议，且答复依赖 host 的 Tick；host 不在运行时查询必然 300 ms 超时。
8. 跨平台代码保留了 `_WIN32` 分支，但本轮只在 Linux 上编译与 E2E。
9. B6.9-N 的「发现」仍不是发现协议：没有 mDNS、没有握手、没有房间公告；只是**客户端广播查询 + host 单播应答**，host 之间不会互相应答。手填 `Host`/`Port` 的路径完全保留。

## 10. 验收证据

- 两进程真实 socket E2E：`/tmp/b63_network_e2e.log`，`RESULT failures=0`。覆盖 host 绑定、子进程 join、host 广播、子进程定向回复、双向属性（host float `score` / client string 属性）、`PlayerJoin`/`PlayerLeave`、掉线诊断 `dropped peer 1 (connection closed)`。
- 节点级 E2E：`/tmp/b63_nodes_e2e.log` 第 1-9 行，网络图编译无警告，HostRoom/Send/Broadcast/SyncProperty/OnNetworkEvent 全部 PASS。
- 端口契约/枚举：`/tmp/b63_port_contract.log`（errors=0）、`/tmp/b63_enum_consistency.log`（checked_pairs=63 inconsistencies=0，本批 enum 全覆盖）。
- C.3.1 UDP E2E：`/tmp/c31_udp_e2e`（完整输出在 `/tmp/c3_network.log`），`RESULT failures=0`。host→client 50 条、client→host 30 条不可靠消息全部到达，corrupt=0 / duplicate=0；50 条一次性入队成功（旧 TCP 8 包背压上限已不存在）；TCP 可靠 30 条全到且保序。
- D.1 真不可靠两进程 E2E（2026-11-06）：`tests/c31_udp_two_proc.sh` → `/tmp/c31_udp_two_proc_20261106-095409.log`（2751 B，444，sha256 `f6a15ad6667af83d146d8d367e91faa4ac502d933421365c87cb828d403d7bd8`），host pid 34823 / client pid 34824 两个独立进程。host→client 周期段 199 发 199 到（首次发送时 peer UDP 端点尚未登记，被拒 1 条）；client→host 周期段 200 发 200 到（丢包 0%，仅记录不作断言）；拥塞段 20000 发、166 到、丢包 `99.17%`，随后再 tick 500 ms 计数不变（无重传），`0 < 到达 < accepted` 与 `duplicates=0` 均成立。该段证明消息走真实 UDP 且允许被内核丢弃，不是 TCP 缓冲后全量补发。**注意：`99.17%` 丢包是 host 停止 tick 期间内核接收缓冲溢出所致，属真 UDP 的正常行为，不是实现缺陷**；恢复 tick 后只收到缓冲区里残余的 166 条，且不会重传。
- C.3.2 房间/重连 E2E：`/tmp/c32_rooms_e2e`，32 PASS / `RESULT failures=0`。覆盖 Mock 编程行为、`ListRooms` 房间名/人数/上限/端点、无 host 时 300 ms 明确失败、host 恢复后 `Reconnect` 成功（attempts=1）、重连后 UDP 握手仍可用、host 持续离线时 attempts=2 且 `Status` 非空。
- 节点层：`port_contract.py` 扫描到 3 个新节点（`native_nodes=223, catalog_nodes=68, errors=0, warnings=4`），`enum_consistency.py` `checked_pairs=108 inconsistencies=0`；`ninja` 全量编译通过。
- 节点级图执行 E2E（`/tmp/c32_nodes_e2e`）：**closed**，`RESULT failures=0`。此前崩在 `KX_CompiledGraph::Compile` 内，根因不是 JIT 线代码缺陷，而是独立的 harness 构建脚本（`tests/b63_net_build.sh`）漏编新 TU `KX_InlinePass.cpp`：`Compile` 内部调用 `KX_InlinePass::Run`，未链接时该符号解析为 0 地址，进入 `Compile` 即段错误。把该 TU 加入源文件列表后崩溃消失，全程未改动 `KX_CompiledGraph*` / `KX_InlinePass*` / `KX_GraphCompiler*`。
- 真实图执行覆盖（引擎适配器，非 Mock）：`HostRoom` 节点建房间并应用 `room_name=graphroom`；`JoinRoom` 节点建连成功；`SendNetworkMessage(reliable=false)` 节点逐帧发送，`OnNetworkEvent` 图侧收到 277 条 `udp-graph`；`ListRooms` 节点 `OnSuccess` + `Count=1`；host 下线后 `RefreshRooms` 节点 `OnFail`、`Count=0`，`Error` 为 `ListRooms: no room-list reply within 300 ms`；host 恢复后 `Reconnect` 节点 `OnSuccess`、`Status=connected`。
- 工程提醒（写给 D.1）：每新增一个 TU，所有 E2E / harness 构建脚本都要同步更新，这已连续两次导致运行期 0 地址崩溃。`tests/b5_rerun/Makefile` 已补 `KX_InlinePass.cpp`；后续应优先让测试构建从 `build_linux/build.ninja` 的 GameNodes 对象列表派生，而不是手写源文件清单（注意 b5 harness 里 `b5_stubs.cpp` 会与真实节点表 TU 冲突，派生时需要排除已 stub 的注册函数所属 TU）。
- 构建：`ninja -C build_linux -j2` 增量全绿（未 clean、未改动他人文件）。

## 11. B6.9-N N-1：LAN 发现（简化版）

### 11.1 三条语义（先定死，后实现）

1. **广播间隔与离线超时**：`DiscoverRooms(Port, TimeoutMs)` 在 `TimeoutMs` 窗口内每 **200 ms** 发一次 `PKT_DISCOVER`；默认窗口 **1000 ms**（5 次广播），夹在 100..5000 ms。**「离线」= 一次查询窗口内没有任何应答**：每次调用都从空列表开始，所以 host 停服后下一次查询在窗口结束时就看不到它，不需要额外状态机。
2. **重复应答去重键**：按 **`ip:port`（应答源端点）**，不按房间名。房间名可重复且加入需要地址；同一端点六轮广播的应答收敛成 1 条。只有广播 `sendto` 失败（无广播路由的容器）时才补发回环单播，避免同机房间因 `127.0.0.1` 与 LAN IP 两个源地址被算成两间房。
3. **不回应别人的广播**：host 只对 `PKT_DISCOVER` 查询做**单播**应答；收到 `PKT_ROOMINFO`（别的房间的应答）一律丢弃、绝不回应。host 也不主动广播自身。因此 N 个 host 不会因为互答形成 N×N 风暴；查询者之外的 host 看不到应答。

返回语义：查询窗口完成即返回 `true`，**空 LAN 返回 `true` + 空列表**（不是错误）；只有端口非法或 socket/bind 失败才返回 `false` 并填 `Error`。

### 11.2 实现

- 新包类型：`PKT_DISCOVER = 9`（查询，空 payload）、`PKT_STATE = 10`（见 §12）。`PKT_ROOMINFO = 8` 复用为发现应答，房间名/人数/上限与原 `ListRooms` 完全一致。
- 新接口：`KX_NetworkAdapter::DiscoverRooms(port, timeout_ms, out, error)`；引擎实现使用独立临时 UDP socket（`SO_BROADCAST` + `SO_REUSEADDR`），不占用 host 或 client 的通信 socket。
- 新节点：`DiscoverRooms`（OnEnter / Port / TimeoutMs → OnSuccess / OnFail / Rooms / Count / Error），已加入编辑器目录。
- 顺带修了两个既有 bug（都是本轮 E2E 暴露、与发现路径直接相关）：`HostRoom` 未把 host 自己计入 `player_count_`（client 未接入时房间广告 `players=0`）；`LeaveRoom` 在没有任何 peer 时索引 `peers_[0]` 导致段错误（host 独自停服即崩）。

### 11.3 E2E（两进程，非 mock）

`tests/n1_discover_two_proc.sh` 启动 **room 进程**（`HostRoom` + `Tick` 应答）与 **client 进程**（广播收集 + 原始 socket 伪造包），共享目录做阶段同步。

- 14 断言 / 0 失败：六轮广播收敛为 1 间房、房间名/端口/人数/上限正确、伪造 `ROOMINFO` 400 ms 内**无任何回应**、伪造 `DISCOVER` 必须收到回应、host 停服后 1000 ms 查询 `rooms=0`。
- 实测房间：`name='alpha-room' host=172.19.0.80 port=46312 players=1 max=4`（广播应答源是 LAN 接口，非常量 `127.0.0.1`）。
- 证据：`/tmp/n1_discover_two_proc_20261106-131849.log`（2186 B，`chmod 444`，sha256 `8510a8056e4a549948906b2e593d3085a1ef3fbe87775922b6f9822d2929fab0`），`RESULT two_process_pids=136346,136347 host_rc=0 client_rc=0 verdict=PASS`。

## 12. B6.9-N N-2：玩家状态同步

### 12.1 频率、字节数与量化（先估算再实现）

- **频率**：固定 **20 Hz**（50 ms 一包），不是每帧、也不是纯变化触发。理由：状态包很小，固定频率让带宽可预测，且丢包检测有稳定的 tick 序列。
- **单包字节数**：协议头 16 B + payload 18 B（`seq U16 + flags U8 + pos 3×I16 + rot 3×I16 + anim U8 + phase U16`）= **34 B/包**；无动画时 31 B（flags bit0=0）。
- **带宽**：每对 peer **34 B × 20 = 680 B/s**（无动画 620 B/s）。8 人星型房：host 上行约 `8 × 680 = 5.3 KiB/s`，每个 client 下行同样约 5.3 KiB/s。对比「每帧发大包」在 60 fps 下会翻 3 倍且与帧率耦合；这里与帧率无关。
- **量化**：位置 `int16`（1 cm，范围 ±327.67 m）；旋转 `int16`（0.01°，范围 ±327.67°）；动画 id `uint8`，相位 `uint16`（1/65535）。最大量化误差：位置 5 mm、旋转 0.005°、相位 3.05e-5。
- **丢包行为**：**保持上一位姿**：不插值、不外推、不重传。sequence 是 20 Hz tick 索引（不是发送计数器），停顿时下一包会跨过缺口；接收端用 16 位环绕安全的比较丢弃过期/乱序包，只接受更新的 sequence。`GetPlayerState` 返回的就是最后应用的那一份，没有时间戳插值。

### 12.2 实现

- 新接口：`SetLocalPlayerState(KX_PlayerState)` / `GetRemotePlayerState(player_id, out)`；`Tick()` 里按 20 Hz 发送 `PKT_STATE`，host 把收到的 client 状态**原包转发**给其他 client（sender 保留），client 只接受来自 host UDP 端点的状态。
- 新节点：`SyncPlayerState`（OnEnter / Position / Rotation / Animation / AnimationPhase → OnDone，config `sync_animation`）与 `GetPlayerState`（OnEnter / PlayerId → Position / Rotation / Animation / AnimationPhase / OnSuccess / OnFail）。已加入编辑器目录。
- 这些节点是「自动同步」的显式入口：场景每帧把 transform 喂给 `SyncPlayerState`，网络层自己按 20 Hz 节流；没有额外的每帧网络调用。

### 12.3 E2E（两进程，含人为丢 tick）

`tests/n2_state_sync_two_proc.sh` 启动 host/client 两进程；host 发位姿 A，client 发位姿 C，双向校验；随后 host **停 `Tick()` 200 ms**（该窗口不产生任何 `PKT_STATE`，接收端等价于丢一个 tick），client 每 2 ms 采样并检查位姿是否被保持，恢复后再校验 B 与 sequence 缺口。

- 14 断言 / 0 失败：位置/旋转误差 ≤ 0.011（实测回读 `1.000,2.000,3.000` / `10.00,20.00,30.00`）、动画 id 与相位一致、丢 tick 期间位置极差 `0.00000`（保持上一位姿）、恢复后 `sequence_delta=5`（200 ms 缺 4 个 tick + 1 个节流边界）。
- 证据：`/tmp/n2_state_sync_two_proc_20261106-131853.log`（2720 B，`chmod 444`，sha256 `cd224172e2aab27a168316442d48e7f18ff50f1376ee243afb6667f5afa9b421`），`RESULT two_process_pids=136856,136857 host_rc=0 client_rc=0 verdict=PASS`。

### 12.4 回归

N-1/N-2 修改后重跑了既有网络批次（`--only 'c31|c32|b63'`）：`b63_network 17/0`、`b63_nodes 36/0`、`c31_udp 14/0`、`c31_udp_two_proc 14/0`、`c32_rooms 32/0`、`c32_nodes 70/0`，合计 `183 assertions / 0 failures`（`tests/.e2e_out/run_20261106-131904/`）。

## 13. B6.9-Ms：RPC 与延迟消息（追加）

代码：`KX_FunctionNodesB6Net.cpp`（新增规格表 `kB6MsgSpecs[]`，与既有 `kB6NetSpecs[]`
分开注册，B6.3/B6.7/B6.9-N 的条目一行未动）；适配器 `KX_MessageAdapter` /
`KX_NetworkAdapter` 均只在末尾追加虚函数，既有槽位不动。

### 13.1 `SendRPC` / `OnRPC`

端口：

| 节点 | 输入 | 输出 | 配置 |
|---|---|---|---|
| `SendRPC` | `OnEnter`、`Subject`(String)、`Payload`(String)、`Target`(Object) | `OnSuccess`、`OnFail` | — |
| `OnRPC` | `Object`(Object)、`Subject`(String) | `OnRPC`(trigger)、`Subject`(String)、`Payload`(String)、`Sender`(String) | `subject`、`match` |

**发送语义（Ms-1）：**

- **本地回环永远执行，单机不失败。** 无论有没有网络适配器、`IsConnected()` 真假，
  `SendRPC` 都先把消息投到本场景消息总线，主题为 `"rpc:" + Subject`。
  `Target` 为空 → 广播（接收方 `to=""`）；`Target` 非空 → 只投给该对象名。
  这也是"无网络适配器时 `SendRPC` 走本地回环而不是失败"的实现位置。
- **网络只是附加通道。** `ctx.Network().IsConnected()` 为真时，再 `BroadcastReliable`
  一个 RPC 信封；广播失败 → `OnFail`。注意此时本地回环**已经投递过**，
  `OnFail` 只表示"远端没发出去"，不回滚本地消息。
- **空 `Subject` → `OnFail`**（诊断 `subject must not be empty`），本地和网络都不发。
- `SendRPC` 是 `ACTION` 节点：只在 `OnEnter` 触发帧执行，不按帧轮询。

**RPC 信封（网络线格式，小端）：**

```
"EBCRPC1" (7 B) | u32 subject_len | u32 payload_len | subject | payload
```

固定头 15 B。`OnRPC` 只解析 `KX_NetworkEvent::Message` 且 magic 匹配的包；
magic 不匹配的 `Message` 事件**不是 RPC**，按"其他事件"处理（回队，见下）。

**接收语义：**

- 接收方对象：`Object` 输入优先，为空则用 `ctx.Owner()`；都没有时
  `receiver_name=""`，**只收广播**（定向 RPC 需要接收对象）。
- 主题过滤：`Subject` 输入优先，为空则回退 config `subject`；二者都为空 → 匹配所有 RPC。
- **匹配沿用既有 `MessageReceived` 的枚举语义：`match = ConfigInt("match", 0) == 1`**
  （`0` = 精确相等，`1` = 前缀），**不是字符串比较**，也不接受 `"prefix"` 之类的文本值。
- 输出：`Subject` 为去掉 `"rpc:"` 前缀后的主题；`Payload` 为原始 payload；
  `Sender` 本地消息取发送对象名，远端包取 `"player:<player_id>"`。
- **同一帧多条消息 = 每条各触发一次，按到达顺序排队，不合并。** `OnRPC` 是
  `TICK_SEQUENCE`：每帧一次性收取，然后逐条 `element` 触发；两条 `a.ping` 同帧到达
  就是两次 `OnRPC`，最后一个 `Payload` 是后发的那条。测试断言就是"两条同帧 RPC
  触发两次，元素按发送序"。
- **网络事件队列与其他节点共享。** `OnRPC` 每帧最多 drain 64 个事件，只消费 RPC 信封；
  其他事件按原顺序**逆序 `RequeueEvent` 回队首**，因此 `OnNetworkEvent` 仍能看到
  自己的事件，不丢、不乱序。上限 64 是本轮明确的实现边界（超出部分留到下一帧）。
- 本地消息总线是双缓冲：本帧 `Send` 的消息下一帧才被 `Receive` 看到（与既有
  `MessageReceived` 一致），所以"发→收"至少隔一帧。

### 13.2 `SendDelayedMessage`

端口：输入 `OnEnter`、`Subject`(String)、`Payload`(String)、`Delay`(Float)；
输出 `OnSuccess`、`OnFail`。

**语义先定死：**

- **delay 单位是秒（float），不是帧。** 到期判定按**逻辑帧 `delta_time` 累加**，
  不看墙钟（不 `sleep`、不用 `steady_clock`）。`KX_ExecutionContext::BeginFrame`
  每帧调用一次 `message_adapter_->TickDelayed(delta_time_)`，场景停帧/暂停时延迟消息
  也随之中止，恢复后继续倒计时。
- `delay <= 0` → 立即 `Send`（仍然经消息总线双缓冲，**下一帧可见**，不是同步回调）。
  `delay` 为 `NaN`/`Inf`/负数 → `OnFail`（诊断 `delay must be finite and >= 0`）。
- 空 `Subject` → `OnFail`。
- 延迟消息挂在**场景消息适配器**上（`to=""` 广播，`from=ctx.Owner()`），
  不是节点内部状态；节点销毁不影响已排队的消息。
- **跨场景卸载：丢弃 + 诊断。** `EngineMessageAdapter` 析构时若仍有挂起的延迟消息，
  打印 `GameNode: message bus: N delayed message(s) dropped on scene unload`
  到 stderr，然后随适配器一起销毁（不迁移到新场景、不补发）。
  Mock 适配器在 `Reset()` 里把挂起数累加到 `dropped_delayed`（计数只增不清零），
  供测试断言"卸载有丢弃且被记录"。
- 到期帧：`TickDelayed` 把 `remaining` 减去本帧 `delta_time`，`<= 0` 即 `Send`
  并从队列移除；因此**恰好 0.5 s 的一帧是最后一次递减后触发，只触发一次**。
  `delta_time <= 0` 的帧直接跳过，不推进。

### 13.3 验证

- `tests/b69_msg_e2e.cpp`（真实编译图 + 双缓冲测试消息总线 + Mock 网络适配器，
  `Tick(1/60)` 显式推帧，无 sleep）：`20 assertions / 0 failures`，
  日志 `/tmp/b69_msg_e2e.log`；统一入口复跑 `--only 'b69_file|b69_msg'` →
  `b69_msg 20/0`，`TOTAL assertions=39 failures=0 build_failures=0 crashes=0`
  （`/tmp/e2e_b69_fi_ms`）。
- 覆盖：无网络适配器时 `SendRPC` 本地回环成功；`OnRPC` 精确主题收到并输出
  subject/payload；不匹配主题不触发；同帧两条触发两次且元素按发送序；
  前缀节点收 `a.ack` 而精确节点不收；注入 `EBCRPC1` 网络包被解码并报
  `player:<id>` 与 payload；非 RPC 网络事件被 requeue 回队列；
  `SendDelayedMessage` 0.25 s 未触发、0.5 s 恰好触发一次、payload 保留、
  负 delay 走 `OnFail`、挂起消息归适配器所有、卸载丢弃被计数；
  `KX_MockMessageAdapter` 的 `SendDelayed`/`TickDelayed`/`Reset` 契约 3 条。
- 回归：改后重跑 `--only 'c31|c32|b63'`（N 线已记录 183/0 的同一批）未受影响。
- 构建：`flock /tmp/build_linux.lock -c 'ninja -C build_linux -j6'` 本轮第一次 `exit 0`
  （13/13，`/tmp/b69_full_ninja.log`）；改完 `chunk_size` 扫描归属后，四个相关 TU
  （`KX_FunctionNodesB6Net.cpp` / `KX_FunctionNodesB6File.cpp` / `KX_FileAdapter.cpp` /
  `KX_FileAdapterEngine.cpp`）单独编译 `exit 0`（`/tmp/b69_tu_ninja.log`）。
  同一次全包尝试被**并行线的半成品** `BL_ArmatureObject.cpp`
  （`'Euler' is not a member of 'blender'`，非本轮文件）挡住，未回退他人代码，待其修好后由 K 重跑。
- 编辑器侧：`native_b6c.py` 追加 `SendRPC` / `OnRPC` / `SendDelayedMessage` 三行（`match`
  用 `values={'exact':0,'prefix':1}` 的枚举导出，C++ 用 `ConfigInt` 读同一整数），
  `port_contract.py` / `enum_consistency.py` 的 `CPP_TABLES` 追加读取 `kB6MsgSpecs`，
  `gen_metadata.py` 重新生成后 `up to date (325 nodes)`，
  `b64_metadata_check.py` `checked=325 missing=0 unknown=0 port_mismatch=0 config_mismatch=0`。

### 13.4 明确不做

- RPC **远程目标不是点对点**：连网时一律 `BroadcastReliable` 广播，`Target` 只影响
  本地回环；不做远端定向路由/寻址。
- 不做 RPC 请求-响应关联 id、超时、重传、去重；`EBCRPC1` 信封没有 sequence，
  同一包重复到达会重复触发。
- 不做可靠交付保证：`BroadcastReliable` 失败只反映在 `OnFail`，不重试。
- 延迟消息不做序列化：跨场景/存档不保存挂起队列，卸载即丢弃（见 13.2）。
- 延迟消息没有 per-message 取消 API；节点层也没有"取消延迟消息"节点。
