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
  - `version > 2` 报 `unsupported save version N` 并失败，不静默返回空值。
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

### 2. 引入第三方的成本评估

| 方案 | 体积 | 许可证 | 交叉编译 | 工期 |
|---|---|---|---|---|
| OpenSSL（libcrypto，静态） | 每架构约 3~5 MB；3 个桌面平台 × 多架构显著增大发行包 | OpenSSL 3.x 为 Apache-2.0；与 Blender 的 GPL-2.0-or-later 组合需法律评审（历史 OpenSSL 许可证与 GPL 不兼容，需例外或按 GPLv3 分发） | 需要为 Windows/macOS/Linux 各交叉目标准备 ABI 一致的库、CVE 跟踪与符号导出，改动顶层 CMake 与打包脚本 | **4~6 天** |
| 内置 tiny-AES-c 一类实现 | 2 个源文件，<100 KB | Public Domain / MIT，兼容 GPL | 无外部依赖，交叉编译无额外成本 | 约 **2 天** |
| 自研 AES | — | — | — | 不建议：侧信道、填充/IV 管理风险高 |

tiny-AES-c 方案虽然能进 2 天，但**只有裸 AES 分组/CTR**，没有 AEAD、密钥派生和常数时间保证；
以“安全加密”名义发布会误导用户，故不采用。

### 3. 决策

**评估后不做**：保持 `algorithm=xor`，AES 明确为不可用。需要接入时按 OpenSSL 方案 **5 天**（4~6 天区间，取中位）排期。

- 节点行为：`algorithm=aes` 打印
  `AES 待接入 (no BLI AES entry point); refusing to fall back to XOR`
  并走失败路径（不触发 `OnDone`、输出保持空值，`KX_FunctionNodesB6File.cpp:266-292`）。
  **绝不会把 xor 当 aes 静默执行。**
- XOR + Base64 的定位：Base64 便于字符串端口传递，XOR **仅防窥不防攻击**
  （已知明文可恢复密钥流，无完整性校验）。
- 编辑器同步标注：枚举标签改为 `AES (not available)`，`EncryptData` / `DecryptData` 的
  tooltip 写明“AES needs a third-party library and is not enabled”
  （`scripts/addons_core/bge_netlogic/editor/nodes/native_b6file.py:9-15 / 30-40`）。
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

## 限制

- AES 未接入：BLI 无入口，OpenSSL 方案需 **5 天**（4~6 天）含许可证与交叉编译评审，
  本轮评估后不做（见 §加密）。`algorithm=aes` 为明确的失败路径，不静默降级为 XOR。
- XOR 仅防窥，不防攻击；存档文件不要用于不受信任输入。
- 二进制容器版本：`1`（payload 原样）与 `2`（payload zlib 压缩）两种；`version > 2`
  一律按不支持拒绝。压缩由 `version` 字段表示，不含独立 flag 字节。
