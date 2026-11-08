# 文件 / 消息域重打分（新口径）

> 范围：B6.9-Fi（文件 5 节点）+ B6.9-Ms（消息 3 节点），并把文件域回算到 18 节点。
> 日期：2026-11-06。所有百分比都给分子/分母，可在本文件内复算。
> **本文件不并入附录 B**（附录 B 本轮由 K 线单点重写），也不改
> `CAPABILITY_WEIGHT_AUDIT.md` 的 286 分母与任何历史百分比。

## 0. 新口径（原文照抄）

代码侧成熟度 = 实现完成 + Mock/Headless E2E 通过：

| 子类 | 定义 | 计入分母 | 计入分子 |
|---|---|---|---|
| **full** | 实现完成 + Headless/Mock E2E 通过 + 无已知限制 | 是 | 1.0 |
| **approximate** | 实现完成 + Headless E2E 通过 + 有明确限制（文档写清） | 是 | 0.5 |
| **unsupported** | 引擎层不支持，附 `路径:行号` 可核证据，进 FOREVER | 否（进 FOREVER） | — |
| **pending-device** | 实现完成 + Mock E2E 通过 + 需真实设备验证 | 是 | 1.0（代码侧）+ 真机待验单列 |

pending-device 必须同时满足三条：
1. 节点有行为测试；
2. Mock/Headless E2E 通过；
3. 阻碍 headless 验证的是**具体设备/驱动依赖**并给出 API 位置——不是「我们没测」。

### 本轮重分类口径（2026-11-06，照抄上级原文）

- `D = full + approximate`；`unsupported` 与 `pending-device` **不进分母**；
- `approximate` 只表示「节点实现本身不完整」；**限制来自引擎能力时记 `full`**，限制写 `KNOWN_LIMITATIONS`；
- `pending-device` 剔除分母需同时满足：① 有 Mock/Headless E2E 通过日志；② 有可核理由说明为何 headless 验不了。

> 本文件没有 `pending-device` 节点，因此新口径只改变 `EncryptData` / `DecryptData` 的分类，
> 分母仍为 18。

两个百分比的定义（沿用旧审计 A/B 的分解方式，权重换新口径）：

- **严格** = `full / 分母`（`approximate` 记 0；只认已上真实引擎/真实传输的证据）。
- **加权（主口径，代码侧成熟度）** = `(full×1.0 + approximate×0.5) / 分母`。

---

## 1. 结论表

| 域 | 分母 | full | approximate | pending-device | unsupported | 严格 | **加权（主口径）** |
|---|---:|---:|---:|---:|---:|---:|---:|
| 文件域（全量 18） | 18 | 18 | 0 | 0 | 0 | 18/18 = **100.0%** | 18/18 = **100.0%** |
| 文件（本轮新增 5） | 5 | 5 | 0 | 0 | 0 | 5/5 = **100.0%** | 5/5 = **100.0%** |
| 消息（本轮新增 3，本机两进程） | 3 | 3 | 0 | 0 | 0 | 3/3 = **100.0%** | 3/3 = **100.0%** |
| 消息·跨机（单列，不计入上行分母） | — | — | — | 1 | — | — | — |

> 跨机可达性只有一台机器的证据，单列为 pending-device，**不并入**上面 3 个节点的分母
> （否则本机两进程的 full 会被一台机器无法验证的项拖回 0）。

**旧口径对照（可核对，不是照抄）**

| 域 | 我上一轮的原话（无单一百分比） | 旧审计 §4 数值 | 新严格 | 新加权 | 差异原因 |
|---|---|---|---:|---:|---|
| 文件新增 5 | 「实现 5/5、Mock E2E 5/5、**真实磁盘 0/5**」 | —（旧审计只有 13 节点） | 100.0% | 100.0% | 旧报法把「真实磁盘未验」当扣分项；新口径按 §3.6 判为**可闭环覆盖缺口**，不是设备依赖、不是能力缺口 |
| 文件域 13（旧） | — | 84.6% / 92.3% | — | — | 旧分母 13、11 full + 2 approximate（AES 两项） |
| 文件域 18（上一轮新分母） | — | — | 88.9% | 94.4% | 分母 13→18，新增 5 个 full；AES 两项当时仍 approximate |
| **文件域 18（本轮重分类后）** | — | — | **100.0%** | **100.0%** | AES 两项按引擎限制重分类为 full（见 §7），分母仍 18 |
| 消息 3 | 「实现 3/3、本地+调度 E2E 3/3、**真实 UDP/跨机 0/3**」 | —（旧审计无消息域） | 100.0% | 100.0% | 补齐本机两进程 RPC E2E 后，`SendRPC`/`OnRPC` 的 socket 往返已验；跨机单列 pending-device，不再计入本域 3 节点分母 |

> `70%`（消息）与 `82%`（文件）这两个旧数字在上一轮回报里**没有出现过**，旧审计也没有消息域行，
> 本文无法复算其来源，故不引用、不沿用。

---

## 2. 文件域逐节点

域组成：B4File 8 + B6File 5 + B6.9-Fi 5 = 18（与 `CAPABILITY_WEIGHT_AUDIT.md` §2.10 的 13 相加）。
`JSONStringify` / `JSONParse` 是纯进程内序列化，仍计入本域（与 §2.10 一致）。

| # | 节点 | 批次 | 实现位置 | 行为测试（Mock） | 真实磁盘（`b69_file_engine`） | 分类 | 分子 |
|---|---|---|---|---|---|---|---:|
| 1 | `SaveGame` | B4File | `KX_FunctionNodesB4File.cpp` | `b4_reg` | ✅ 节点级 | full | 1.0 |
| 2 | `LoadGame` | B4File | 同上 | `b64` / `b4_reg` | ✅ 节点级 | full | 1.0 |
| 3 | `ReadFile` | B4File | 同上 | `b4_reg` | ✅ 节点级 | full | 1.0 |
| 4 | `WriteFile` | B4File | 同上 | `b66_uncovered` / `b4_reg` | ✅ 节点级 | full | 1.0 |
| 5 | `FileExists` | B4File | 同上 | `b4_reg` | ✅ 节点级 | full | 1.0 |
| 6 | `DeleteFile` | B4File | 同上 | `b4_reg` | ✅ 节点级 | full | 1.0 |
| 7 | `JSONStringify` | B4File | 同上 | `b4_reg` | 不需要（不触碰磁盘） | full | 1.0 |
| 8 | `JSONParse` | B4File | 同上 | `b4_reg` | 不需要（不触碰磁盘） | full | 1.0 |
| 9 | `SaveGameBinary` | B6.5 | `KX_FunctionNodesB6File.cpp` | `b64` | ✅ 节点级 | full | 1.0 |
| 10 | `SaveGameSlots` | B6.5 | 同上 | `b64` | ✅ 节点级 | full | 1.0 |
| 11 | `EncryptData` | B6.5 | 同上（`av_aes_*`） | `b64` | 不需要（不触碰磁盘） | **full**（本轮重分类） | 1.0 |
| 12 | `DecryptData` | B6.5 | 同上（`av_aes_*`） | `b64` | 不需要（不触碰磁盘） | **full**（本轮重分类） | 1.0 |
| 13 | `FileReadLines` | B6.5 | 同上 | `b64` | ✅ 节点级 | full | 1.0 |
| 14 | `FileReadChunk` | B6.9-Fi | `KX_FunctionNodesB6File.cpp:427` | `b69_file` | ✅ 节点级 | full | 1.0 |
| 15 | `FileWriteChunk` | B6.9-Fi | `KX_FunctionNodesB6File.cpp:473` | `b69_file` | ✅ 节点级 | full | 1.0 |
| 16 | `ListDirectory` | B6.9-Fi | `KX_FunctionNodesB6File.cpp:529` | `b69_file` | ✅ 节点级 | full | 1.0 |
| 17 | `CreateDirectory` | B6.9-Fi | `KX_FunctionNodesB6File.cpp:566` | `b69_file` | ✅ 节点级 | full | 1.0 |
| 18 | `RemoveDirectory` | B6.9-Fi | `KX_FunctionNodesB6File.cpp:592` | `b69_file` | ✅ 节点级 | full | 1.0 |

**AES 两项为什么重分类为 `full`（限制进 `KNOWN_LIMITATIONS` #10）**：节点有实现、Mock E2E 通过；
`xor` + Base64 是该节点承诺的能力，`algorithm=aes` 时显式拒绝并走失败路径，不静默降级
（`KX_FunctionNodesB6File.cpp:261-309`）。偏差**不在节点代码**：仓库内虽有 `libavutil` 的
`av_aes_*`，但裸分组密码（ECB/CBC/CTR）缺完整性保护，现有 CRC32 不是认证、可被重算，
直接暴露会制造「已加密」的虚假安全感；AES 已按密码学设计理由登记为 FOREVER
（`DEFERRED_FOREVER.md` §3 #3）。节点已做到最好 → 记 `full` + 限制；**AES 本身进 FOREVER，
不是节点的能力缺口**。

**证据批次数**（统一入口 `run_all_e2e.sh`，`/tmp/e2e_rescore_fm`；历史采集，K 冻结后为 `b64 94/0`、全量 `1456/46 批`，见 `NUMBER_CONSISTENCY_AUDIT.md` B.5）：

```
b4_reg            55 / 0
b64               78 / 0
b66_uncovered     20 / 0
b69_file          19 / 0
b69_file_engine   34 / 0   （新增：真实磁盘）
TOTAL assertions=226 failures=0 build_failures=0 crashes=0
```

**真实磁盘证据：14 / 18 个磁盘节点已有节点级真实磁盘断言**（`tests/b69_file_engine_e2e.cpp`，
临时目录 + `KX_FileAdapterEngine`，34/0）。剩下 4 个（`JSONStringify` / `JSONParse` /
`EncryptData` / `DecryptData`）不触碰文件系统，没有可跑的真实磁盘语义。
因此「真实磁盘 0/18」在**磁盘语义维度已清零**，而不是把非磁盘节点也算成磁盘证据。

---

## 3. 消息域逐节点

域组成（本轮定义）= B6.9-Ms 的 3 个节点。
`SendMessage` / `MessageReceived` / `MessageReceivedLegacy` 仍按 `CAPABILITY_WEIGHT_AUDIT.md`
§2.14 计入「其他」域，**不重复计入本域**，以免 286 分母漂移。

| # | 节点 | 实现位置 | 行为测试 | 已验部分 | 真实传输 | 分类 | 分子 |
|---|---|---|---|---|---|---|---:|
| 1 | `SendDelayedMessage` | `KX_FunctionNodesB6Net.cpp:797` | `b69_msg` | 调度、到期、负 delay 拒绝、卸载丢弃计数 | 无网络路径（不需要） | full | 1.0 |
| 2 | `SendRPC` | `KX_FunctionNodesB6Net.cpp:638` | `b69_msg` + `b69_rpc_two_proc` | 本地回环、空 subject 拒绝、`Target` 本地定向、信封编码；两进程 socket 发送 | 本机两进程 ✅ | full | 1.0 |
| 3 | `OnRPC` | `KX_FunctionNodesB6Net.cpp:683/760/766/779` | `b69_msg` + `b69_rpc_two_proc` | 精确/前缀匹配、payload、sender、非 RPC 事件 requeue、同帧多条；两进程接收还原 | 本机两进程 ✅ | full | 1.0 |

**本机两进程 RPC E2E（`tests/b69_rpc_two_proc.sh`，26/0）断言清单**

- 信封 `EBCRPC1`：总长 = `15 + subject_len + payload_len`；`u32 subject_len` / `u32 payload_len`
  与实际字节数一致；subject 与 payload 原样位于固定头之后；
- 跨进程还原：subject `同步/通道`（非 ASCII + 分隔符 `/`）与 payload `p|q,r;s/t-v` 在宿主进程
  逐字节一致；
- `Sender == "player:<client id>"`，id 来自 `OnNetworkEvent` 的 `PlayerId`；
- 非 RPC 事件不丢：客户端同一帧先发两条 `SendNetworkMessage` 再发 RPC；宿主 `OnRPC` 先 Tick
  （图中排在 `OnNetworkEvent` 之前），把两条非 RPC 事件逆序 `RequeueEvent` 回队首，
  `OnNetworkEvent` 同 Tick 看到 `plain-1`、`plain-2`，且保持原始顺序；
- `EBCRPC1` 信封不出现在 `OnNetworkEvent::Data`（证明 `OnRPC` 消费了它们而不是泄漏）。

**跨机可达性（单列 pending-device）**

只剩「第二台机器 / 真实网卡拓扑」这一项，API 位置不变：
`KX_NetworkAdapterEngine::BroadcastReliable`（`KX_NetworkAdapterEngine.cpp:904`）、
`PollEvent`（`:959`）、`RequeueEvent`（`:970`）、`IsConnected`（`:985`）。
它不阻碍本机 headless 验证（本机两进程已跑），因此**不并入 3 节点分母**，只单列真机待验。

**证据批次**：`b69_msg 20/0`（`/tmp/e2e_rescore_fm`）、`b69_rpc_two_proc 26/0`（日志见 §5）。

---

## 4. 3.6 判定：pending-device 还是「没做」

### 4.1 结论先行

| 对象 | 判定 | 理由摘要 |
|---|---|---|
| 文件 18 节点 | **full（代码侧 1.0）**；本轮 18/18 全部 full | 磁盘语义已由 Mock **和**真实磁盘双证；`b69_file_engine_e2e` 用临时目录 + `KX_FileAdapterEngine` 跑通 14 个磁盘节点（34/0），其余 4 个不触碰磁盘；AES 两项的限制来自引擎能力，已重分类为 full |
| `SendDelayedMessage` | **full** | 只有本地调度，没有网络路径；不需要任何设备 |
| `SendRPC` / `OnRPC` | **full（本机两进程）** | 本地语义 + 真实 socket 两进程往返均已断言（`b69_rpc_two_proc` 26/0）；跨机单列 pending-device |

**文件 5 个为什么算 full**：实现、端口、错误路径、编辑器目录、契约扫描全部在位，且
`b69_file`（Mock）19/0 与 `b69_file_engine`（真实磁盘）34/0 同时通过。**
原先写的「可 headless 闭环的覆盖缺口」现已用 E2E 关闭**，不是靠改口径。
**消息 2 个为什么从 pending-device 抬成 full**：阻碍本机 headless 的从来不是设备，而是
「没写两进程测试」；`b69_rpc_two_proc` 已把真实 socket、信封字节、跨进程还原、requeue
全部断言。只有「第二台机器」仍是设备级待验，单列而不倒扣节点分。

**文件：为什么 18 个全部 full**：`b69_file`（Mock）与 `b69_file_engine`（真实磁盘）覆盖了
节点定义的全部磁盘语义；AES 两项与磁盘无关，其 `xor` 能力已实现且 `aes` 显式拒绝，
限制来自引擎无 AES 入口（进 FOREVER），按本轮口径重分类为 full。4 个非磁盘节点
（JSON×2、AES×2）没有磁盘语义可验，不再制造「真实磁盘缺口」。

### 4.2 磁盘语义矩阵（文件 5 个）

「Mock 覆盖」= `KX_MockFileAdapter` + `b69_file` 实际断言过；「真实磁盘」= `b69_file_engine`
用 `KX_FileAdapterEngine` 在临时目录里实际断言过。

| 磁盘语义 | Mock | 真实磁盘 | 证据（`b69_file` / `b69_file_engine` 断言） | 仍属明确限制 |
|---|---|---|---|---|
| 文件不存在时读 | ✅ | ✅ | `ReadFileRange` 返回 false；两套 E2E 都断言 `OnFail` | 否 |
| 顺序写 + 分块拼接 | ✅ | ✅ | 「three chunks concatenate byte-exactly」= `AAAABBBBCCCC`；真实磁盘读了 12 字节 | 否 |
| 覆盖/截断（`append=false` 首片） | ✅ | ✅ | `WriteFileRange(truncate=true)`；真实磁盘 `trunc.bin` 由 `AAAA` 变 `BB` | 否 |
| 追加（`append=true`） | ✅ | ✅ | 同一次触发的后续分片 append；4 字节内容 / 2 字节块 | 否 |
| 读偏移与 EOF | ✅ | ✅ | 「`Eof` 只在末块为真」「offset==size 空内容 + Eof」「offset>size OnFail」 | 否 |
| `chunk_size` 非法值 | ✅ | ✅ | 0 与 1048577 均 `OnFail`，真实磁盘上文件未被创建/改写 | 否 |
| 目录存在 / 非目录 | ✅ | ✅ | 「not a directory」`KX_FileAdapter.cpp:293` 与 Engine 同义 | 否 |
| 直接子项、排序、目录标记 | ✅ | ✅ | 两套 E2E 都断言 `a.txt`/`b.txt`/`sub` 升序、`sub` 为唯一目录项 | 否 |
| 建目录（parents true/false） | ✅ | ✅ | `build`（parents）与 `build/sub`（非 parents）在磁盘上 `is_directory` | 否 |
| 非空目录拒删 | ✅ | ✅ | 「non-empty build/ is refused」+ 磁盘文件仍在 | 否 |
| 递归删子树 | ✅ | ✅ | 「recursive delete succeeds」+ 磁盘子树消失 | 否 |
| 路径穿越拒绝 | ✅ | ✅ | `../evil` / `../` / `a/../../x` 三条均拒绝且磁盘无副作用 | 否 |
| 适配器失败 → `OnFail` | ✅ | ✅ | 缺失文件读、offset 越界两处断言 `OnFail` | 否 |
| 缺失父目录自动创建 | ✅ | ✅ | 真实磁盘 `deep/nested/f.txt`，父目录自动建立 | 否 |
| 保存槽路径约定 | ✅ | ✅ | 真实磁盘 `save/slot1.json`、`save/bin1.bin` 存在；`SaveGameSlots` 列出两者 | 否 |
| 权限拒绝（EACCES） | ❌ | ❌ | 节点契约只定义「失败 → OnFail」，未定义错误分类 | 是（需 ACL/只读挂载，已写为限制） |
| 磁盘满（ENOSPC） | ❌ | ❌ | 无空间语义；只能在受限挂载上构造 | 是（已写为限制） |
| 大小写/Unicode 路径规范化 | ❌ | ❌ | 适配器按原字符串定位，依赖底层 FS 行为 | 是（已写为限制） |
| 符号链接 | ❌ | ❌ | 明确不做（`DEFERRED_FOREVER.md` 第 7 条） | 是（FOREVER 非功能） |
| 并发句柄 / 文件锁 | ❌ 未实现 | ❌ 未实现 | `FileWriteChunk` 无锁，按调度顺序交错（已定义为行为） | 否（不是设备问题） |
| 原子替换 / fsync | ❌ 未实现 | ❌ 未实现 | `WriteFileRange` 就地 truncate/append，无临时文件改名 | 是（真机崩溃一致性） |
| > 4 GB 大文件 | ❌ 未测 | ❌ 未测 | `size_t` 偏移可用，但无大文件样本 | 是（真机容量） |

**逐节点判定**

| 节点 | 判定 | 依赖上述哪几行 |
|---|---|---|
| `FileReadChunk` | full | 读/EOF/chunk 边界 Mock + 真实磁盘双证 |
| `FileWriteChunk` | full | 写/截断/追加/分块 Mock + 真实磁盘双证；并发交错是定义行为 |
| `ListDirectory` | full | 存在性/排序/直接子项/穿越拒绝 Mock + 真实磁盘双证 |
| `CreateDirectory` | full | parents 两档、穿越拒绝 Mock + 真实磁盘双证 |
| `RemoveDirectory` | full | 空删/非空拒/递归删/穿越拒绝 Mock + 真实磁盘双证 |

> 这 5 个不扣 0.5：❌ 行全是「未定义的 OS 级失败模式」「FOREVER 非功能」或「未实现的原子性」，
> 不是「实现了但语义打折」；它们已在 `DEFERRED_FOREVER.md` / `B6_FILE_ADVANCED.md` 写为明确限制。

### 4.3 消息语义矩阵（3 个）

| 语义 | 已验 | 证据 | 只有真实传输/跨机能验 |
|---|---|---|---|
| 本地回环投递（发→收） | ✅ | `b69_msg`「SendRPC: loopback succeeds without a network adapter」 | 否 |
| subject 精确 / 前缀（`match` 枚举） | ✅ | 精确节点收 `a.ping` 不收 `a.ack`；前缀节点相反 | 否 |
| 不匹配不触发 | ✅ | 同上，计数断言 | 否 |
| 同帧多发不合并、按序 | ✅ | 同帧两条 `a.ping` → 两次触发、元素按发送序 | 否 |
| payload / sender 透传 | ✅ | 本地 sender=对象名；远端 sender=`player:<id>` | 否 |
| 网络信封编解码 | ✅ | 注入 `EBCRPC1` 事件被解码并输出 payload | 否 |
| 非 RPC 事件不被吞 | ✅ | `PlayerJoin` 事件 requeue 后仍在队列 | 否 |
| 延迟消息调度 / 到期 / 提前不触发 | ✅ | 0.25 s 未触发、0.5 s 恰好一次、payload 保留 | 否 |
| 卸载丢弃 + 诊断 | ✅（Mock 计数） | `Reset()` 累加 `dropped_delayed`；Engine 析构打印 | 部分（stderr 文案未在真机场景断言） |
| 真实 socket 往返（本机两进程） | ✅ | `b69_rpc_two_proc` 26/0：client `SendRPC` → host `OnRPC` 走真实 adapter | 否 |
| 信封 `magic + u32 subject_len + u32 payload_len` | ✅ | 同上，客户端在 `BroadcastReliable` 处按字节断言，含总长度 | 否 |
| 跨进程序列化还原（非 ASCII / 分隔符） | ✅ | subject `同步/通道`、payload `p|q,r;s/t-v` 逐字节一致 | 否 |
| 非 RPC 事件逆序 requeue 后不丢且保序 | ✅ | 两条 `plain-*` 在同 Tick 被 `OnNetworkEvent` 看到，`plain-1` 在前 | 否 |
| **跨机可达** | ❌ | 无第二台机器 | **是（网卡/网络环境）** |

**逐节点判定**

| 节点 | 判定 | 说明 |
|---|---|---|
| `SendDelayedMessage` | full | 纯本地调度，无第③条依赖 |
| `SendRPC` | full（本机两进程） | 本地 + 真实 socket 广播均已断言；跨机单列 pending-device |
| `OnRPC` | full（本机两进程） | 本地/注入信封 + 真实 `PollEvent`/`RequeueEvent` 路径均已断言；跨机单列 pending-device |

### 4.4 哪些「不是」缺口，必须说清

- **文件真实磁盘缺口已关闭**：`b69_file_engine_e2e` 用临时目录 + `KX_FileAdapterEngine`
  在 14 个磁盘节点上做了节点级真实磁盘断言（34/0）；剩余 4 个不触碰磁盘。
- **消息本机两进程缺口已关闭**：`b69_rpc_two_proc`（26/0）覆盖真实 socket、信封字节、
  跨进程还原与 requeue；跨机才是真机项，单列而不倒扣节点分。
- **`Target` 远端不生效**（RPC 连网一律广播）是**明确不做的非功能**（`DEFERRED_FOREVER.md` 第 4 条），
  不按 approximate 扣分；但它是一条必须写进文档的用户可见限制。
- **排序断言不降级**：`KX_FileAdapterEngine::ListDirectory` 自己 `std::sort`，不依赖
  文件系统返回顺序，因此真实磁盘上仍可做稳定断言；未把这条写成限制。

---

## 5. 复算脚本（可核对）

```text
文件严格 = 18 / 18 = 1.0000          （本轮：EncryptData / DecryptData 重分类为 full）
文件加权 = (18*1.0 + 0*0.5) / 18 = 18 / 18 = 1.0000
文件上一轮严格/加权 = 16/18 = 0.8889 / 17/18 = 0.9444   （仅作历史对照）
文件新增严格 = 5 / 5 = 1.0000
文件新增加权 = (5*1.0) / 5 = 1.0000
消息严格 = 3 / 3 = 1.0000
消息加权 = (3*1.0) / 3 = 3 / 3 = 1.0000
消息·跨机（单列，不计入上式）= pending-device 1
```

E2E 原始数据（历史采集，`b64` 已由 K 冻结为 94/0）：`/tmp/e2e_rescore_fm`（`b4_reg 55/0`、`b64 78/0`、`b66_uncovered 20/0`、
`b69_file 19/0`、`b69_msg 20/0`，`TOTAL 192/0`），加上本轮新增：
`b69_file_engine 34/0`、`b69_rpc_two_proc 26/0`（统一入口 `run_all_e2e.sh` 单次跑出的
`TOTAL assertions=60 failures=0 build_failures=0 crashes=0` 见
`tests/.e2e_out/run_20261106-160938/`）。

## 6. 留给 K 的两件事

1. 把文件域分母 13 → 18、消息域新增 3 节点并入全域分母后**重算总百分比**（本文不落 286/新总数）。
2. 「真实磁盘 0/18」与「RPC 本机两进程」已由 `tests/b69_file_engine_e2e.cpp`（34/0）与
   `tests/b69_rpc_two_proc.sh`（26/0）关闭，K 只需把这两条证据并入附录 B 的重写。
   跨机可达性仍是单列 pending-device。

## 7. 本轮重分类：EncryptData / DecryptData approximate → full（2026-11-06，追加）

本节只做文档重分类，**不改源码、不改断言总数、不碰附录 B 编号**。

| 节点 | ① 实现是否正确 | ② 偏差来自哪 | ③ 能否改代码消除 | 结论 |
|---|---|---|---|---|
| `EncryptData` | 是；`xor` + Base64 可用，`algorithm=aes` 打印「AES 待接入 (no BLI AES entry point); refusing to fall back to XOR」并走失败路径，不触发 `OnDone`（`KX_FunctionNodesB6File.cpp:261-284`） | 偏差不在节点代码：`av_aes_*` 虽在仓库内，但裸分组密码缺完整性保护（CRC32 非认证），AES 已按密码学理由进 FOREVER；XOR 仅防窥 | 不能（引入认证加密需 3~5 天设计 + 密钥管理，且不是节点代码问题；见 `DEFERRED_FOREVER.md` §3 #3） | **full** + `KNOWN_LIMITATIONS` #10；AES 进 FOREVER |
| `DecryptData` | 是；同一实现与拒绝路径（`KX_FunctionNodesB6File.cpp:286-309`），非法 Base64 / 空密钥走失败 | 同上 | 不能 | **full** + #10；AES 进 FOREVER |

复算：

```text
文件域 D = full 18 + approximate 0 = 18（本轮口径：unsupported / pending-device 不进分母）
严格 = 18/18 = 1.0000
加权 = (18 + 0.5×0)/18 = 18/18 = 1.0000
```

仍留 approximate：本文件两域（文件 18、消息 3）没有剩余 approximate；消息·跨机仍是单列
`pending-device`（不并入 3 节点分母）。
