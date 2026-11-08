# B6.4 范围 B：文件高级节点

本批新增 5 个文件高级节点，全部走现有 `KX_FileAdapter`，存档路径沿用 B4.2 的
`<blend_dir>/save/` 与文件名安全过滤（`SanitizeSlot`）。

| # | 节点 | 端口（输入 / 输出） | config |
|---|---|---|---|
| 1 | `SaveGameBinary` | OnEnter, Slot(String), Compress(Bool) / OnSuccess, OnFail | 无 |
| 2 | `SaveGameSlots` | 无输入 / Slots(List), Count(Int) | 无 |
| 3 | `EncryptData` | OnEnter, Data(String), Key(String) / OnDone, Encrypted | `algorithm`: aes \| xor |
| 4 | `DecryptData` | OnEnter, Encrypted(String), Key(String) / OnDone, Data | `algorithm`: aes \| xor |
| 5 | `FileReadLines` | OnEnter, Path(String) / OnSuccess, OnFail, Lines(List<String>) | 无 |

实现文件：

- `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6File.cpp`
- 容器编码 `KX_SaveGameBinary.h/.cpp`
- 适配器接口 `KX_FileAdapter.h`（`SaveGameBinary` / `LoadGameBinary` / `ListSaveSlots`）
  与引擎实现 `KX_FileAdapterEngine.cpp`、Mock 实现 `KX_FileAdapter.cpp`
- 读取复用 B4 的 `LoadGame`（新增 `format: binary`），与 JSON 存档同一 schema、
  同一 `B4ApplyAssets` 回填路径
- 编辑器表 `editor/nodes/native_b6file.py`

## 二进制存档格式

```
magic   : 'E' 'B' 'C' 'S'
version : u32 = 1 或 2       (1 = payload 原样；2 = payload zlib 压缩；> 2 不支持)
length  : u32 存储态 payload 字节数
crc32   : u32 对存储态 payload 的 CRC32（0xEDB88320）
payload : version=1 时是递归 tagged 值；version=2 时是 zlib deflate 流，
          解压后为同一套 tagged 值（null / bool / double bits / string len+bytes /
          array / object name+value），嵌套深度上限 64
```

- payload 的根与 JSON 存档相同：`{"version": ..., "assets": [...]}`，
  只是编码从 JSON 文本换成二进制容器，所以读回后走同一个 schema 校验。
- 反序列化在以下任一情况返回错误而不是崩溃：长度不足、magic 不符、
  未知 version、`length` 与剩余字节不符、CRC 不符、payload 解析失败、
  尾部有多余字节、压缩流失效或解压超过 512 MiB。
- `LoadGame(format=binary)` 失败时打印 stderr 诊断并触发 `OnFail`；
  成功触发 `OnSuccess`。

## 压缩（C.4.2）

`SaveGameBinary` 新增 `Compress(Bool)` 输入端口。**压缩只影响 payload，头部不变**：
仍是 16 字节 `magic + version + length + crc32`，字段顺序、宽度和 CRC 的语义都不变；
压缩与否由 `version` 字段本身表示，不额外插入 flag 字节。

| version 值 | payload 形态 | 由谁写出 |
|---|---|---|
| `1` | 原样 tagged 字节流（legacy 格式） | `Compress=false`，以及 B6.7 之前的全部存档 |
| `2` | zlib deflate 流，解压后为 version=1 的同一套 tagged 字节 | `Compress=true` |

- **flag 位置**：头部偏移 `4..7` 的 `version`（u32 小端）。`1`=未压缩，`2`=zlib 压缩。
  没有独立的 flag 字段，因此“头部不变”是字面事实；旧存档的 version=1 语义不变。
- **压缩算法**：zlib `compress2(..., Z_DEFAULT_COMPRESSION)`（标准 zlib 流，
  自带 zlib 头），读取侧用 `inflate` 流式循环解压。源文件
  `KX_SaveGameBinary.cpp`，头文件 `KX_SaveGameBinary.h`。
- `length` 记录压缩后（存储态）的字节数，`crc32` 也针对存储态 payload 计算。
  读取顺序是 **CRC → inflate → tagged 解析**：先拦下损坏/截断，再让 zlib
  报告非法流，最后才解析结构。
- **兼容性规则**：
  - 读取侧按 version 自动识别，version 1 和 2 都走同一条
    `LoadGameBinary` → `KXSaveGameBinary::Decode`，读节点不需要新参数。
  - **前向兼容边界**：`version ≥ 3` 一律拒绝，报 `unsupported save version N` 并失败，
    不静默返回空值、绝不解码。未来扩版本号时，老引擎必须明确报错而不是把新格式误解码。
  - 压缩流失效报 `compressed save payload is corrupt or truncated`；
    CRC 不符报 `save checksum mismatch`；长度不符报
    `save payload length mismatch (truncated or trailing data)`。
  - 解压产物超过 512 MiB 报 `compressed save payload expands past the 512 MiB limit`
    （防止压缩炸弹撑爆内存）。
- `Compress=false` 写出的 blob 与旧版逐字节同格式（version=1），因此旧存档、
  旧工具链仍然可读；只有显式打开 `Compress` 才写 version=2。
- 体积：deflate 对小 payload 可能反而更大（固定头开销）。实现不做“压缩后更大就
  自动回退”的隐式切换——`Compress=true` 一定写 version=2，行为可预测。
- 编辑器侧同步：`native_b6file.py` 的 `SaveGameBinary` 输入表加入
  `Compress(Bool)`，`native_metadata_gen.py` 由 `gen_metadata.py` 重新生成。

## 加密

### 1. `BLI_` 现状调研（C.4.1）

| 候选 | 结论 | 源码位置 |
|---|---|---|
| `BLI_hash` | 只有摘要算法（MD5），**不是加密**，不可逆 | `source/blender/blenlib/BLI_hash_md5.hh`、`BLI_hash_fwd.hh:69` |
| `BLI_crypt` / `BLI_encrypt` | 不存在 | 全仓 `grep -rn "BLI_crypt|BLI_encrypt"` 0 命中 |
| `BLI_file` 系列 | 只有 gzip/zstd 压缩与文件读写，无加密原语 | `source/blender/blenlib/BLI_fileops.hh` |
| `AES_*` / `EVP_*` / `SSL_*` | 源码内 **0 处加密 API**；Blender 未链接 OpenSSL | 全仓 `grep -rn -E "AES_|EVP_|CRYPTO_|SSL_" source/` 仅 4 处，均为 Python 证书环境变量名 `PYTHON_SSL_CERT_FILE` / `SSL_CERT_FILE`（`source/blender/python/intern/bpy_interface.cc:562-572`），与加密无关 |

结论：**BLI 没有可用 AES 入口**，也没有已链接的加密库；`algorithm=aes` 无法在当前依赖内真实现。

### 2. 候选方案与成本（三类候选全部保留）

| 类别 | 候选 | 现状 | 许可证 / 体积 | 工期 |
|---|---|---|---|---|
| BLI 内 | `BLI_hash` / `BLI_crypt` / `BLI_file` | **无加密入口**：只有 MD5 摘要与 gzip/zstd 压缩（§1） | — | 不可用 |
| 仓库内已有 | FFmpeg `libavutil` 的 `av_aes_*`（另有 `av_sha_*` / `av_hmac_*`） | 已随 `lib/ffmpeg/lib/libavutil.a` 分发，`nm` 可见 `av_aes_init`/`av_aes_crypt`/`av_aes_ctr_*`；**仅 ECB/CBC/CTR 裸分组** | 随 FFmpeg 构建（LGPL/GPL 兼容），新增 0 B | CBC + HMAC-SHA256 约 **3~5 天**（见下） |
| 引入第三方成熟库 | OpenSSL libcrypto | 未链接进 Blender 核心；自有依赖链以 `no-shared` 静态构建 | Apache-2.0；实测 `.so` 6.1 MiB / `.a` 11.4 MiB（每架构） | **4~6 天** + GPLv3 分发评审 |
| 第三方小实现 | tiny-AES-c 一类 | 2 个源文件，<100 KB | Public Domain / MIT | 约 **2 天**；同样只有裸分组 |
| 自研 | — | — | — | 不建议：侧信道、填充/IV 管理风险高 |

E.1 复审实测数字（本机 Ubuntu / OpenSSL 3.5.5）：
`libcrypto.so.3` = 6,386,528 B（约 6.1 MiB）；`libcrypto.a` = 11,958,004 B（约 11.4 MiB）。
Blender 自有依赖链里的 OpenSSL 以 `no-shared` 静态构建（`build_files/build_environment/cmake/ssl.cmake:53`），
即每个架构需要再链入约 11.4 MiB 的静态库（链接器裁剪前）；本机静态链接还缺 `jitterentropy` 符号。

**关键判断：成本不在依赖，在密码学设计。** 仓库内 `libavutil` 已有裸分组密码，直接接进来不缺库；
缺的是 AEAD / 密钥派生 / IV(nonce) 管理 / 完整性认证。ECB/CBC/CTR 只给机密性原语，
把 `av_aes` 或 tiny-AES-c 直接暴露成 `algorithm=aes`，会制造“已加密”的错误安全感，所以不采用。

**若真要做（给出数字是为了让“不做”有可比对价，不是排期承诺）**：基于 `av_aes` CBC +
`av_hmac` HMAC-SHA256（`libavutil` 同时导出 `av_sha_*` / `av_hmac_*`；**BLI 侧只有 MD5，
没有 SHA-256 / HMAC**）做 encrypt-then-MAC，容器加 version=3 与 IV/MAC 字段，约 **3~5 天**。
仍必须解决：① 密钥来源（节点只有 String Key，无 KDF/密钥库）；② IV 唯一性/nonce 复用检测；
③ `av_aes` 为查表实现、非常数时间（侧信道）；④ 解密错误信息导致的 padding oracle；
⑤ v1/v2 旧存档迁移。任一未解决都不能对外宣称“安全加密”。

### 3. 决策

**评估后不做**：保持 `algorithm=xor`，当前仅提供 XOR。**仓库内 `libavutil` 已有裸分组密码
（`av_aes_*`，仅 ECB/CBC/CTR、无 AEAD），不采用；引入 OpenSSL 成本 4–6 天且需 GPLv3 分发评审。**
拒绝理由不是“缺依赖”，而是裸分组密码缺完整性保护（现有 CRC32 不是认证，可被重算），
直接接入会制造虚假的安全感（详见 §2 的“若真要做”对价）。

- 节点行为：`algorithm=aes` 打印
  `AES 待接入 (no BLI AES entry point); refusing to fall back to XOR`
  并走失败路径（不触发 `OnDone`、输出保持空值，`KX_FunctionNodesB6File.cpp:266-292`）。
  **绝不会把 xor 当 aes 静默执行。**
- **成本定位**：仓库内已有 AES 实现，成本不在依赖而在密码学设计（AEAD/密钥管理/nonce/侧信道）。
- **XOR 不是加密，只是混淆（obfuscation）**：已知明文即可恢复密钥流，且无完整性校验，
  不能当作任何安全边界的保护手段。XOR + Base64 的定位：Base64 便于字符串端口传递，
  两者合计**仅防窥不防攻击**。
- 编辑器同步标注：枚举标签改为 `AES (not available)`，`EncryptData` / `DecryptData` 的
  tooltip 写明“AES needs a third-party library and is not enabled”
  （`scripts/startup/bge_gamenodes/editor/nodes/native_b6file.py:9-15 / 30-40`）。
- 空 key 同样走失败路径。Base64 解码校验长度 % 4、非法字符与填充顺序。

## FileReadLines

- 路径拒绝包含 `..` 段（`../` 与 `..\\`），失败触发 `OnFail`。
- 读取后剥离 UTF-8 BOM；按 `\n` 分行，去掉每行尾部 `\r`（兼容 CRLF）。
- 末尾换行不产生多余空元素；文件中间的空行保留为一个空字符串元素
  （E2E：`alpha\\r\\nbeta\\r\\n\\r\\ngamma\\r\\n` → 4 个元素）。

## 适配器

- `KX_FileAdapter` 新增三个虚函数，基类默认返回 false（Null 适配器安全降级）。
- Mock 用 `savebin:<slot>` 键保存 blob，`ListSaveSlots` 去重并排序；
  另计 `savebin_calls` / `loadbin_calls` / `list_slots_calls` 供测试断言。
- Engine 写 `<blend_dir>/save/<slot>.bin`，`ListSaveSlots` 扫描同目录
  `.json` 与 `.bin` 并去重排序（与 JSON 存档共享槽位命名空间）。

## 验证

- 构建 exit 0：`/tmp/b64_build_final.log`。
- `port_contract.py` errors=0、`enum_consistency.py` 0 不一致
  （覆盖 `algorithm` 的 aes/xor）：`/tmp/b64_port_contract_final.log`、
  `/tmp/b64_enum_consistency_final.log`。
- 注册检查：文件 5/5，`MISSING [] EXTRA []`，`/tmp/b64_registration_final.log`。
- E2E（`/tmp/b64_e2e.log`）：
  - 二进制存档写入后读回 `OnSuccess`，截断 blob 后读回 `OnFail` 且不崩溃；
  - `SaveGameSlots` 输出 `['slotA', 'slotB']`、`Count=2`；
  - XOR 加密往返恢复 `hello world`，密文与明文不同；
  - `algorithm=aes` 不触发 `OnDone`、不出 XOR 结果；
  - CRLF 逐行读取 4 元素、拒绝 `../` 路径。
- B6.6（C.4.1）复核：本轮只做目标文件级编译（`KX_FunctionNodesB6File.cpp`）与编辑器表静态检查，
  **未跑全量 `ninja -C build_linux -j6`**（构建目录被 JIT 线占用）。BLI/OpenSSL 调研见 §加密。
- B6.7（C.4.2）压缩 E2E（`/tmp/c4b_compress.log`，harness `tests/c4b_compress_e2e.cpp`，
  `RESULT assertions=20 failures=0`）：
  - 同一棵 JSON 树分别以 `compress=false` / `compress=true` 写入 → 两种 blob 解码后
    再编码字节完全一致（`decoded content is byte-identical across both variants`）；
  - 合成大 payload：raw 690946 B → compressed 20019 B（约 2.90%，合成数据重复度高，
    真实存档视内容重复度而定）；
  - 篡改压缩流（翻转 payload 中间字节并修正 CRC，使 CRC 通过、inflate 失败）→
    `compressed save payload is corrupt or truncated`，断言失败而非崩溃；
  - 仅改 payload 不修正 CRC → `save checksum mismatch`；
  - 旧格式 v1（无压缩 flag）存档仍可解码；
  - `version=3` → `unsupported save version 3`；截断压缩 blob → 长度不符错误；
    全零 payload → inflate 失败。
- 契约：`port_contract.py` `errors=0`（223 native / 68 catalog，4 条既有 rename note）、
  `enum_consistency.py` `checked_pairs=108 inconsistencies=0`。
- 构建：`ninja -C build_linux -j2`（增量），日志 `/tmp/c4b_ninja.log`。

E 块复审（本轮重跑，不新增代码）：
- 压缩 harness 重新编译并运行：`RESULT assertions=20 failures=0`，日志 `/tmp/e2_compress_rerun.log`；
  其中 `legacy v1 blob (no compression flag) still decodes` 一条即老存档兼容验证。
- 全量增量构建（flock 串行）：`NINJA_EXIT=0`，`/tmp/e_block_ninja.log`；
  `KX_SaveGameBinary.cpp` 与 zlib 链接随 `libge_logic_bricks.a` 正常产出。
- AES 勘察复核：`BLI_crypt`/`BLI_encrypt` 0 命中；`BLI_hash` 仅 MD5 摘要；
  Blender 自有 OpenSSL 依赖为 `no-shared` 静态构建，未链接进核心；FFmpeg `libavutil`
  虽有 `av_aes_*` 但无 AEAD/完整性，按 §加密 结论仍不接入（仅 XOR）。

## 限制

- AES 未接入：BLI 无入口；仓库内 `libavutil` 有 `av_aes_*` 裸分组（仅 ECB/CBC/CTR、无 AEAD），
  不采用；OpenSSL 方案需 **4~6 天**并需 GPLv3 分发评审。拒绝理由是密码学设计而非依赖缺失
  （见 §加密）。`algorithm=aes` 为明确的失败路径，不静默降级为 XOR。
- XOR 不是加密、只是混淆；存档文件不要用于不受信任输入。
- 二进制容器版本：`1`（payload 原样）与 `2`（payload zlib 压缩）两种；**`version ≥ 3`**
  一律按不支持拒绝。压缩由 `version` 字段表示，不含独立 flag 字节。

## B6.9-Fi：流式读写与目录操作（追加）

本节写死 Fi-1 / Fi-2 的语义。代码：`KX_FunctionNodesB6File.cpp`（新增规格表
`kB6FileStreamSpecs[]`，与既有 `kB6FileSpecs[]` 分开注册，旧表未改）、
`KX_FileAdapter.h/.cpp`、`KX_FileAdapterEngine.cpp`（只追加虚函数与实现）。

### Fi-1 `FileReadChunk` / `FileWriteChunk`

端口：

| 节点 | 输入 | 输出 | 配置 |
|---|---|---|---|
| `FileReadChunk` | `OnEnter`、`Path`(String)、`Offset`(Int) | `OnSuccess`、`OnFail`、`Content`(String)、`Size`(Int)、`Eof`(Bool) | `chunk_size` |
| `FileWriteChunk` | `OnEnter`、`Path`(String)、`Content`(String) | `OnSuccess`、`OnFail` | `chunk_size`、`append` |

三条先定死的语义：

1. **块大小是节点配置，不是全局配置。** `chunk_size` 默认 `65536`，允许 `[1, 1048576]`；
   `0`、负数、`> 1048576` 或非整数一律 `OnFail` 并写诊断（`chunk_size must be in
   [1, 1048576] bytes`），**在读/写任何字节之前就拒绝**。同一张图里不同节点可以用不同块大小。
2. **读到头 = 成功 + 空内容 + `Eof=true`。** 判定用的是显式 `Offset` 而不是内部游标：
   - `Offset < 文件大小`：返回 `[Offset, min(Offset+chunk_size, size))` 的切片，`Size` 为实际字节数，
     `Eof = (Offset + Size == size)`；
   - `Offset == 文件大小`：`OnSuccess`、`Content=""`、`Size=0`、`Eof=true`；
   - `Offset > 文件大小`：适配器拒绝 → `OnFail`（**不做钳制、不回绕、不静默返回空串**）。
   - `Offset < 0`、`Path` 为空：`OnFail`。
   节点**无内部游标**，读第 N 块由图自己传 `Offset = N * chunk_size`，因此同一节点被多个触发器
   调用时不会串状态，重放/跳读都是显式的。
3. **多节点写同一文件：无锁，按调度顺序交错。** `FileWriteChunk` 把输入 `Content` 按
   `chunk_size` 切片，逐片交给适配器：
   - `append=false`（默认）：本次触发的**第一片**以 truncate 写入，其余片 append ——
     即"每次触发重写整个文件"；
   - `append=true`：所有片都 append，接在当前文件尾。
   - 因此两个 `append=false` 的节点写同一路径，后触发者覆盖前者；一个 `append=true` 与
     一个 `append=false` 交错写，结果是调度顺序决定的字节交错，**不保证任何块的原子性**，
     也不提供文件锁。需要确定结果时，图作者必须自己串行化（用一个触发器/序列节点排队）。
   - 空 `Content` + `append=false` 会写入 0 字节（等价于清空文件）；`append=true` 是 no-op。
   - 任一片失败即停在该片并 `OnFail`，已写的前几片不回滚。

适配器契约（`KX_FileAdapter`，末尾追加，既有槽位不动）：
`ReadFileRange(path, offset, length, out, eof)`、`WriteFileRange(path, data, truncate)`；
基类默认返回 false（Null 适配器安全降级），Mock 与 Engine 各实现一份。
Engine 端 `ReadFileRange` 对 `offset > file_size` 返回 false，short read 视为错误。

### Fi-2 `ListDirectory` / `CreateDirectory` / `RemoveDirectory`

端口：

| 节点 | 输入 | 输出 | 配置 |
|---|---|---|---|
| `ListDirectory` | `OnEnter`、`Path`(String) | `OnSuccess`、`OnFail`、`Entries`(List)、`Directories`(List)、`Count`(Int) | — |
| `CreateDirectory` | `OnEnter`、`Path`(String) | `OnSuccess`、`OnFail` | `parents`（默认 `true`） |
| `RemoveDirectory` | `OnEnter`、`Path`(String) | `OnSuccess`、`OnFail` | `recursive`（默认 `false`） |

**安全边界（`KX_FileAdapterDirectoryPathIsSafe`，Mock / Engine 共用同一份校验）：**

- 所有目录路径都相对 **blend 目录**解析，**不接受绝对路径**：首字符 `/`、尾字符 `/` 均拒绝。
- 拒绝 `..`、`.`、空路径组件（`a//b`）、反斜杠 `\`、冒号 `:`（Windows 盘符/ADS 写法）、
  以及所有控制字符（`< 0x20`、`0x7f`）。
- **唯一例外**：`ListDirectory` 允许空路径，含义是"列 blend 根目录"；
  `CreateDirectory` / `RemoveDirectory` 拒绝空路径，因此**根目录不可被删除**。
- 路径穿越（`../evil`、`../`、`a/../../x`）在这些规则下直接 `OnFail`，不会落到适配器。
- 已知边界：**未做符号链接逃逸检测**。若 blend 目录内已有指向外部的符号链接，
  引擎后端会跟随该链接（`fs::directory_iterator` / `create_directories` 的标准行为）。
  本节点层不额外拦截；只应在可信的工程目录中使用。

**列目录语义：**

- 只返回**直接子项**（不递归），`Entries` 按名称升序排序（引擎用 `std::sort`，
  Mock 同样排序后按 `name` 去重 —— 文件和目录同名时只保留一条）。
- `Directories` 是 `Entries` 中 `directory=true` 的子集；`Count = Entries.size()`。
- 路径不存在或不是目录：`OnFail`，错误串 `not a directory: '...'`；**不会返回空列表冒充成功**。
- 空目录 → `OnSuccess` + 两个空 List + `Count=0`。

**建/删语义：**

- `CreateDirectory(parents=true)`：`create_directories`，父目录缺失时逐级补齐；
  `parents=false`：只建最后一级，父目录缺失即 `OnFail`（`create_directory` 的语义），
  **不会静默退化成"什么都不做"**。
- `RemoveDirectory(recursive=false)`：目录非空 → `OnFail`，错误串
  `directory is not empty: '...' (recursive=false)`，**不删除任何东西**；
  `recursive=true`：`remove_all` 删除整棵子树（含普通文件）。
- 只删目录；指向普通文件时 `OnFail`（`not a directory`）。

**明确不做：**

- 不提供 `blend_dir` 之外的任何访问，也不提供"删除任意目录"的开关；`recursive` 只在
  `blend_dir` 相对路径内生效。
- 不实现文件锁、跨进程互斥、写入回滚。
- 不实现符号链接逃逸检测（见上）。

### 验证

- `tests/b69_file_e2e.cpp`（Mock 适配器 + 真实编译图，`Tick(1/60)` 显式推帧，无 sleep）：
  `19 assertions / 0 failures`，日志 `/tmp/b69_file_e2e.log`；
  统一入口复跑 `tests/run_all_e2e.sh --only 'b69_file|b69_msg'` → `b69_file 19/0`，
  `TOTAL assertions=39 failures=0 build_failures=0 crashes=0`，日志 `/tmp/e2e_b69_fi_ms`。
- 覆盖：写 3 块（`chunk_size=2`）逐字节等于 `AAAABBBBCCCC`；按 `Offset=0/4/8` 读回 3 块，
  `Eof` 只在末块为真；`Offset=12`（恰在文件尾）空内容 + `Eof=true` + 成功；
  `Offset=13` 走 `OnFail`；`chunk_size=0` 与 `1048577` 均 `OnFail`；
  建目录 → 建子目录 → 列目录（`a.txt`/`b.txt`/`sub`，`Count=3`，仅 `sub` 在 `Directories`）
  → 删空子目录 → 非空 `recursive=false` 拒绝且目录仍在 → `recursive=true` 删除整棵子树；
  `../evil` 建目录、`../` 列目录、`a/../../x` 删目录全部拒绝。
- 构建：`flock /tmp/build_linux.lock -c 'ninja -C build_linux -j6'` 本轮第一次 `exit 0`
  （13/13，`/tmp/b69_full_ninja.log`）；`KX_FileAdapter.cpp` 单 TU 单独验证通过；
  改完 `chunk_size` 扫描归属后四个相关 TU（`KX_FunctionNodesB6File.cpp` /
  `KX_FileAdapter.cpp` / `KX_FileAdapterEngine.cpp` / `KX_FunctionNodesB6Net.cpp`）
  再单独编译 `exit 0`（`/tmp/b69_tu_ninja.log`）。另一次全包尝试被并行线的半成品
  `BL_ArmatureObject.cpp`（`'Euler' is not a member of 'blender'`，非本轮文件）挡住，
  未回退他人代码，待其修好后由 K 重跑。
- 契约扫描：`chunk_size` 的 `ConfigInt` 读取写在 `FuncFileReadChunk` / `FuncFileWriteChunk`
  函数体内（范围校验仍共用 `B6FileValidateChunkSize`），因为 `enum_consistency.py` 按
  `Func*` 归属统计配置读取，放在辅助函数里会被判成"未读取该键"。改后
  `enum_consistency.py` `checked_pairs=125 inconsistencies=0`、`b64_metadata_check.py`
  `checked=325 missing=0 unknown=0 port_mismatch=0 config_mismatch=0`。
