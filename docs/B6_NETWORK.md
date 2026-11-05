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
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6c.py` | `NATIVE_B6C` 49-119 |

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
- **不可靠通道**：`SendUnreliable` 走独立 `O_NONBLOCK` UDP socket；host 在 TCP 监听端口同号的 UDP 端口 `bind`，client `bind` 临时端口，双方通过 v2 握手交换端口。单包 payload 上限 `65507 - 16` 字节。UDP 无重传、无拥塞控制、不保证顺序，丢包由内核/网络决定。
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

## 10. 验收证据

- 两进程真实 socket E2E：`/tmp/b63_network_e2e.log`，`RESULT failures=0`。覆盖 host 绑定、子进程 join、host 广播、子进程定向回复、双向属性（host float `score` / client string 属性）、`PlayerJoin`/`PlayerLeave`、掉线诊断 `dropped peer 1 (connection closed)`。
- 节点级 E2E：`/tmp/b63_nodes_e2e.log` 第 1-9 行，网络图编译无警告，HostRoom/Send/Broadcast/SyncProperty/OnNetworkEvent 全部 PASS。
- 端口契约/枚举：`/tmp/b63_port_contract.log`（errors=0）、`/tmp/b63_enum_consistency.log`（checked_pairs=63 inconsistencies=0，本批 enum 全覆盖）。
- C.3.1 UDP E2E：`/tmp/c31_udp_e2e`（完整输出在 `/tmp/c3_network.log`），`RESULT failures=0`。host→client 50 条、client→host 30 条不可靠消息全部到达，corrupt=0 / duplicate=0；50 条一次性入队成功（旧 TCP 8 包背压上限已不存在）；TCP 可靠 30 条全到且保序。
- C.3.2 房间/重连 E2E：`/tmp/c32_rooms_e2e`，32 PASS / `RESULT failures=0`。覆盖 Mock 编程行为、`ListRooms` 房间名/人数/上限/端点、无 host 时 300 ms 明确失败、host 恢复后 `Reconnect` 成功（attempts=1）、重连后 UDP 握手仍可用、host 持续离线时 attempts=2 且 `Status` 非空。
- 节点层：`port_contract.py` 扫描到 3 个新节点（`native_nodes=223, catalog_nodes=68, errors=0, warnings=4`），`enum_consistency.py` `checked_pairs=108 inconsistencies=0`；`ninja` 全量编译通过。
- 节点级图执行 E2E（`/tmp/c32_nodes_e2e`）：**closed**，`RESULT failures=0`。此前崩在 `KX_CompiledGraph::Compile` 内，根因不是 JIT 线代码缺陷，而是独立的 harness 构建脚本（`tests/b63_net_build.sh`）漏编新 TU `KX_InlinePass.cpp`：`Compile` 内部调用 `KX_InlinePass::Run`，未链接时该符号解析为 0 地址，进入 `Compile` 即段错误。把该 TU 加入源文件列表后崩溃消失，全程未改动 `KX_CompiledGraph*` / `KX_InlinePass*` / `KX_GraphCompiler*`。
- 真实图执行覆盖（引擎适配器，非 Mock）：`HostRoom` 节点建房间并应用 `room_name=graphroom`；`JoinRoom` 节点建连成功；`SendNetworkMessage(reliable=false)` 节点逐帧发送，`OnNetworkEvent` 图侧收到 277 条 `udp-graph`；`ListRooms` 节点 `OnSuccess` + `Count=1`；host 下线后 `RefreshRooms` 节点 `OnFail`、`Count=0`，`Error` 为 `ListRooms: no room-list reply within 300 ms`；host 恢复后 `Reconnect` 节点 `OnSuccess`、`Status=connected`。
- 工程提醒（写给 D.1）：每新增一个 TU，所有 E2E / harness 构建脚本都要同步更新，这已连续两次导致运行期 0 地址崩溃。`tests/b5_rerun/Makefile` 已补 `KX_InlinePass.cpp`；后续应优先让测试构建从 `build_linux/build.ninja` 的 GameNodes 对象列表派生，而不是手写源文件清单（注意 b5 harness 里 `b5_stubs.cpp` 会与真实节点表 TU 冲突，派生时需要排除已 stub 的注册函数所属 TU）。
- 构建：`ninja -C build_linux -j2` 增量全绿（未 clean、未改动他人文件）。
