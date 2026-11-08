# B6.2 字符串 / 列表高级（17 节点）

日期：2026-11-05
范围：`StringSplit`、`StringJoin`、`StringRegexMatch`、`StringRegexReplace`、`StringTrim`、`StringPad`、`StringLengthChars`、`StringPadChars`、`ListSort`、`ListFilter`、`ListMap`、`ListFind`、`ListSlice`、`ListConcat`、`ListReverse`、`ListUnique`、`ListIndexOf`
运行时：`KX_FunctionNodesB6String.cpp`（表驱动，`kB6StringSpecs`）

## 0. 字节 vs 字符（中文字符串必读）

| 节点 | 口径 | 说明 |
|---|---|---|
| `StringLength`（B2） | **字节** | `"中文"` 返回 6，不是 2 |
| `StringPad` | **字节** | `Length` 按字节计，PadChar 按字节重复 |
| `StringSplit` / `StringJoin` / `StringTrim` / `StringRegex*` | **字节** | 分隔、截取与正则都基于 UTF-8 字节串 |
| `StringLengthChars`（新增） | **字符** | 按 UTF-8 码点计数，`"中文"` 返回 2 |
| `StringPadChars`（新增） | **字符** | 按码点补齐，`PadChar` 取第一个完整码点重复 |

中文/日文/emoji 场景请用 `StringLengthChars` 与 `StringPadChars`。字节版节点保持不变以兼容已有工程（`StringLength` 改语义会破坏既有图）。

## 1. 接入位置

| 层 | 文件 | 位置 |
|---|---|---|
| 运行时实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp` | 新建；字符串 121-293，列表 294-750，表达式求值器 760-880，列表尾巴 882-951，spec 表 958-996，注册 1025 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6StringFunctionGameNodes()` 242 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 追加调用 241 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | `GameNodes/KX_FunctionNodesB6String.cpp` 200 |
| 编辑器表 | `scripts/startup/bge_gamenodes/editor/nodes/native_b6b.py` | `NATIVE_B6B` 中 17 条 |
| 调色板 | `.../ui/node_palette.py` | 17 条字符串/列表条目 |

## 2. 节点与端口

| 节点 | 输入 | 输出 | config |
|---|---|---|---|
| StringSplit | Source, Separator | Parts(List<String>) | — |
| StringJoin | Parts(List), Separator | Result(String) | — |
| StringRegexMatch | Source, Pattern | Matched(Bool), Groups(List<String>), Rejected(Bool) | — |
| StringRegexReplace | Source, Pattern, Replacement | Result(String), Rejected(Bool) | — |
| StringTrim | Source, Mode(Int) | Result(String) | — |
| StringPad | Source, Length(Int), PadChar, Side(Int) | Result(String) | — |
| StringLengthChars | Source | Count(Int) | — |
| StringPadChars | Source, Length(Int), PadChar, Side(Int) | Result(String) | — |
| ListSort | List | SortedList | `key:string`, `descending:bool` |
| ListFilter | List, Predicate(String) | FilteredList | — |
| ListMap | List, Expression(String) | MappedList | — |
| ListFind | List, Value(Float) | Index(Int), Found(Bool) | — |
| ListSlice | List, Start(Int), End(Int) | Slice | — |
| ListConcat | ListA, ListB | Result | — |
| ListReverse | List | ReversedList | — |
| ListUnique | List | UniqueList | — |
| ListIndexOf | List, Value(Float) | Index(Int) | — |

全部为 `is_value_node=true`，每帧求值；`ListSort.key` 走 `ConfigString`，`ListSort.descending` 走 `ConfigBool`，其余参数都是输入端口。

## 3. 字符串语义

- `StringSplit`：`Separator` 的**每个字符**都是一个分隔符（`",;"` 会把 `"a,b;c"` 拆成 `a` / `b` / `c`）；**保留空字段**，包括首段、连续分隔符与尾段（`"a,,b"` → 3 项，`",a,"` → `"" / a / ""`）；`Separator` 为空时按 ASCII 空白切分并把连续空白折叠为一个分隔符。元素上限 `KX_ValuePool::kMaxListElements`。
  语义变更：此前多字符 `Separator` 按整串精确匹配，现在按字符集合拆分；需要整串分隔时应改用单字符或先替换。
  实现按**字节**比较（`separator.find(source[end])`，147-158），因此适合 `,` `;` `|` 等 ASCII 分隔符；把多字节 UTF-8 字符（如 `；`）放进 `Separator` 会按它的每个字节分别拆开，不要这样用。
- `StringJoin`：元素按类型转文本（String 原样、Int 十进制、Bool `true/false`、Float `%.6g`），其他类型视为空串。
- `StringTrim`：`Mode` 0=两端、1=左侧、2=右侧。
- `StringPad`：`Side` 0=左补、1=右补；`PadChar` 为空时用空格，否则取第一个字符；目标长度非法（`<=` 当前长度或超过元素上限）时原样返回。
- `StringLengthChars` / `StringPadChars`：按 UTF-8 码点处理。解码器只接受合法的前导字节 + 后续 `0x80..0xBF` 组合；遇到非法前导、非法后续或截断尾部时，**按一个字符容错**并只打印一次 `[StringLengthChars]` / `[StringPadChars] invalid UTF-8 sequence: counted as one character`，绝不越界读取。截断的多字节尾部整体算一个字符，不会按剩余字节数重复计数。
- `StringRegexMatch`：`Groups` 第 0 项为完整匹配，之后按**左括号出现顺序**依次是捕获组（嵌套组外层在前、内层在后）；未参与匹配的可选组输出空串（无法与“匹配到空串”区分）；整体未匹配时 `Matched=false` 且 `Groups` 为空列表。输出另有 `Rejected(Bool)`（索引 2）：守卫拒绝、源串超长、非法 Pattern 时为 `true` 且 `Matched=false`；正常不匹配为 `false` + `Matched=false`；正常匹配为 `false` + `Matched=true`。ECMAScript 语法不支持 `(?<name>...)` 命名组，命名组按非法 Pattern 处理。
- `StringRegexReplace`：等价 `std::regex_replace`，替换串支持 `$1` 等 ECMAScript 格式。输出 `Result(String)` + `Rejected(Bool)`（索引 0/1）：被拒（含非法 Pattern）时 `Rejected=true` 且 `Result` 原样返回 `Source`，正常替换/无替换时 `Rejected=false`。

### 正则限制

- 使用 C++11 `std::regex`（ECMAScript 语法），不引入 PCRE。
- **非法 Pattern** 由 `std::regex_error` 捕获：每个节点实例只写一次 `[StringRegexMatch] invalid pattern ...` / `[StringRegexReplace] invalid pattern ...` 诊断；前者返回 `Matched=false`，后者原样返回 `Source`，均置 `Rejected=true`，不崩溃。
- **守卫规则（2026-11-06 收紧）**：两个节点共用同一份 `IsSafeRegexPattern`（`KX_FunctionNodesB6String.cpp:146-300`），在 `std::regex` 构造前用单趟 `O(n)` 扫描 + 括号深度栈判定，**不借用 `std::regex` 做这项判定**。量词修饰的分组在**以下任一条件成立**时被拒：
  1. 组体**整体就是一个无界重复**（`+` / `*` / `{n,}` 覆盖整个组体），含跨层传播：`(a+)+`、`(a*)*`、`(.*)*`、`(a+){2,}`、`((a+))+`；
  2. 组体**可匹配空串**：`(a?)+`、`(a*)+`、`(b?)*`。

  连续量词（`a**`、`a++`、`a?*`、`a*+`）直接拒绝。判定表：

  | 模式 | 判定 |
  |---|---|
  | `(\d+,)+` | 放行（组体是「数字 + 逗号重复」的拼接，不是裸重复） |
  | `(ab?)+` | 放行（`?` 有界，组体不可匹配空串） |
  | `(a+)+` / `(a*)*` / `(.*)*` | 拒绝 |
  | `(a?)+` | 拒绝（组体可匹配空串） |
  | `(a\|aa)+` | 放行（无嵌套量词；见下方「交替不受保护」） |
  | `(ab)+` / `(a\|b)*` / `[+*?]` / `\+` / `a+?` | 放行（不得误杀） |

  命中时每个实例只写一次 `[Regex] unsafe pattern rejected: nested quantifier`，**不编译正则**；`Rejected=true`，Match 的 `Matched=false` + 空 `Groups`，Replace 原样返回 `Source`。
- **长度上限（B6.9 3.3）**：`Source` 超过 **4096** 字节或 `Pattern` 超过 **512** 字节时直接拒绝，每个实例只写一次 `[StringRegexMatch] input too long ...` / `[StringRegexReplace] input too long ...`，同样置 `Rejected=true`；前者 `Matched=false` + 空 `Groups`，后者原样返回 `Source`。上限是常量（`KX_FunctionNodesB6String.cpp:54-55`），不做配置。
- **无墙钟超时（保留的明确限制）**：`std::regex` 没有超时机制；嵌套量词与空串组体已在编译前拒绝，但**其余模式仅受长度限制**（模式 512 / 源串 4096）。`(a|aa)+$` 这类「交替项前缀重叠」的模式不受保护：**40 字符输入可达 29.7 秒，44 字符超 45 秒**，属引擎层限制。处理不可信输入时需在上层限制复杂度或改用非回溯匹配。该风险已登记 `KNOWN_LIMITATIONS.md` #30。
- **评分**：`StringRegexMatch` / `StringRegexReplace` 本轮判 `full`（新增拒绝规则 + `Rejected` 端口 + 长度上限；剩余为已知的书面限制）。字符串域内 37/37 全 `full`（**域内 100%，不是全局 100%**）。全局口径：`N=325`（`TypeCount()` 实测），`F=299 / A=4 / U=7 / P=15`，`D = F + A = 303`；严格 `F/D = 299/303 = 98.68%`、加权 `(F+0.5A)/D = (299+0.5×4)/303 = 301/303 = 99.34%`、覆盖 `(F+A+P)/(D+U+P) = (299+4+15)/(303+7+15) = 318/325 = 97.85%`。`unsupported` 7 与 `pending-device` 15 已剔出严格/加权分母；这是 **C++ 实现正确率**，不是用户默认体验。**数值已冻结（K 实测，`RUN_ID=20261106-224926`；46 批 / 1456 断言 / 0 失败）。**

## 4. 列表语义与生命周期

- 所有生成的列表都经 `ctx.Pool().AllocList(...)` 分配，节点从不释放或持有跨帧指针（`CopyIntoPool` 64-72）。沿用 B2/B3 的 `KX_ValuePool` 两代回收约定：本帧产生的列表在本帧与下一帧内有效，跨节点传递无需额外引用计数，也**不能**缓存到节点 state 里跨帧使用。
- `ListSort` 对输入做浅拷贝后排序，不修改输入列表。`key` 为空时按元素自身值排序；非空时把元素当作对象，用 `KX_PropertyAccessor` 读取该属性（数值或字符串）作为排序键（311-346）。比较器对降序使用 `less_key(b, a)`，满足严格弱序，`stable_sort` 保持相等元素原序。
- `ListFilter` 保留使表达式结果为非 0 的元素；`ListMap` 输出 `Float` 结果列表。求值遇到错误时：`ListFilter` 停止并把已接受的元素作为结果输出；`ListMap` 清空结果并输出空列表；两者各只打一次诊断。
- `ListFind` 按数值相等（Float/Int/Bool 互转）或字符串内容相等匹配，返回 `Index=-1 / Found=false` 表示未找到。
- `ListSlice`（B6.9-S1 定稿的负索引语义）：
  - `Start >= 0`：绝对位置，越界截断到 `size`。
  - `Start < 0`：从尾部计数，`-1` 是最后一个元素，`-size` 是第一个元素；小于 `-size` 截断到 0。
  - `End < 0`：同样从尾部计数，`-1` **不含**最后一个元素（即 `size-1`）。
  - `End == 0`：保留旧的哨兵语义“切到末尾”（兼容既有图）；要排除最后一个元素请用 `End = -1`。
  - `End < Start` 视为空；所有索引最终截断到 `[0, size]`。
- `ListConcat` 依次追加 A、B，总长度受 `kMaxListElements` 截断。
- `ListReverse`：对输入做浅拷贝后逆序输出，**不修改输入列表**；空输入输出空列表；长度受 `kMaxListElements` 截断（从尾部开始取，所以截断时保留原列表末尾元素）。
- `ListUnique`：**保序去重，保留每个值第一次出现的位置**（不是任意顺序，也不保留最后一次）。相等判定与 `ListFind` 一致：Float/Int/Bool 按数值互转比较，字符串按内容比较，其他类型按类型 + 指针比较；空输入输出空列表，结果长度受 `kMaxListElements` 截断。
- `ListIndexOf`：返回目标值**第一次出现的下标**，未找到返回 `-1`；`Value` 端口是 `Float`（与 `ListFind` 相同），需要显式 `Found` 布尔时用 `ListFind`。输入为空或未找到都返回 `-1`，不产生诊断。
  **精度边界**：`Float` 只有 24 位尾数，**绝对值超过 `2^24`（16777216）的整数无法精确表示**，相邻的大整数可能被判为相等而返回错误下标。`Index` 输出是 `Int`，不受影响。列表长度受 `KX_ValuePool::kMaxListElements = 4096`（`KX_ValuePool.h:52`）限制，远低于 `2^24`，所以正常图不会触及；只有列表元素**值本身**是大整数（如超过 1677 万的 ID / 时间戳）时才需要注意。

## 5. Predicate / Expression 最小语法子集

`ListFilter` 与 `ListMap` 使用同一个递归下降求值器（390-576），**不是完整表达式引擎**。支持：

```
x | X                    当前元素的数值（AsFloat）
整数 / 小数常量            123, 3.5
+  -  *  /  %            二元算术，/ 与 % 除零报错
-  !                    一元负号、逻辑非
==  !=  <  <=  >  >=     比较（结果为 0/1）
&&  ||                   逻辑与/或（短路语义未实现，两侧都会求值）
(  )                     分组
```

不支持：属性访问（`x.foo`）、函数调用、字符串字面量、三目运算符、变量赋值、列表/字典索引。

错误诊断（每个节点实例一次，stderr）：

- `[ListFilter] unsupported predicate '<expr>': <reason>`
- `[ListMap] unsupported expression '<expr>': <reason>`

`<reason>` 为 `unsupported token 'c'`、`unexpected token '...'`、`missing ')'`、`invalid number '...'`、`division by zero`、`modulo by zero`。

## 6. 验证

- 编译：目标文件级验证，`flock /tmp/build_linux.lock -c "ninja -C build_linux ... KX_FunctionNodesB6String.cpp.o"` exit=0
  （唯一警告是既有的 `EXP_Value::SetValue` 隐藏重载，与本轮新增无关）。
- E2E：`tests/run_all_e2e.sh --only b62` → **134 assertions / 0 failures**（K 冻结 `RUN_ID=20261106-224926`，批 `b62`；旧 `run_20261106-095219` 的 77 条已被覆盖）；
  `--only b64` metadata harness → 61 assertions / 0 failures（`run_20261106-095233`）。

| 断言 | 结果 |
|---|---|
| `StringSplit("a,b,c")` → 3 个元素，第 2 个为 `b` | PASS |
| `StringSplit("a,b;c", ",;")` → `a` / `b` / `c`（多分隔符） | PASS |
| `StringSplit("a,,b", ",")` → `a` / `""` / `b`（保留连续空段） | PASS |
| `StringSplit(",a,", ",")` → `""` / `a` / `""`（保留首尾空段） | PASS |
| `StringSplit("  a   b ", "")` → `a` / `b`（空分隔符折叠空白） | PASS |
| `StringJoin` → `a|b|c` | PASS |
| `StringRegexMatch("abc123","([a-z]+)([0-9]+)")` 匹配，Groups=3 | PASS |
| `StringRegexMatch("ab","((a)(b))")` → `ab` / `ab` / `a` / `b`（嵌套组按左括号顺序） | PASS |
| `StringRegexMatch("b","(a)?(b)")` → `b` / `""` / `b`（未参与的可选组输出空串） | PASS |
| `StringRegexMatch("x","(z)")` → `Matched=false`，Groups 为空列表 | PASS |
| `StringRegexReplace("abc123","[0-9]+","X")` → `abcX` | PASS |
| `StringTrim("  hi  ")` → `hi`；`StringPad("7",4,"0")` → `0007` | PASS |
| `StringLengthChars("中文ab")` → 4（字节版为 8） | PASS |
| `StringPadChars("中",3,"文")` → `文文中`（按字符补齐） | PASS |
| 截断 UTF-8 序列 `\xE4\xB8` → 计 1 个字符并只警告一次 | PASS |
| `ListSort` Float `[3,1,2]` 升序 → `[1,2,3]` | PASS |
| `ListFilter("x > 1")` → 2 项；`ListMap("x * 2")` → `[6,2,4]` | PASS |
| `ListFind(1)` → Index=1/Found=true | PASS |
| `ListSlice([0,2))` → 2 项；`ListConcat` 3+3 → 6 | PASS |
| `ListReverse([1,2,1,3,2])` → `[2,3,1,2,1]`，且输入不变 | PASS |
| `ListUnique([1,2,1,3,2])` → `[1,2,3]`（保序、保留首次出现） | PASS |
| `ListIndexOf(1)` → `0`（首次出现）；`ListIndexOf(9)` → `-1` | PASS |
| 两个 3D 句柄的衰减参数各自独立（A=2/5、B=4/20 互不影响） | PASS |

- 注册与枚举：`gen_metadata.py --check` → `up to date (325 nodes)`；b64 metadata harness 61/0。
  `native_b6b.py` 现有 30 条（字符串 17 + 音频 5 + 输入 8），与三段 C++ spec 表一一对应。
- 2026-11-06 正则双修改了端口契约：`StringRegexMatch` 输出 2→3（新增 `Rejected`，索引 2），
  `StringRegexReplace` 输出 1→2（新增 `Rejected`，索引 1）。已同步 `native_b6b.py`、重生成
  `native_metadata_gen.py`（325 节点）并重跑 `enum_consistency.py`（125 pairs / 0）与 `port_contract.py`（errors=0）。

## 7. 限制

1. 正则无墙钟超时（§3）：嵌套量词与可匹配空串的组体已在编译前被 `IsSafeRegexPattern` 拒绝，其余模式仅受长度限制（`Source` 4096 字节 / `Pattern` 512 字节）；上限之内 `(a|aa)+$` 这类交替项前缀重叠的回溯仍不受保护（40 字符约 29.7 秒、44 字符超 45 秒），恶意输入需在上层限制复杂度。
2. Predicate/Expression 只支持 §5 子集，元素必须是数值语义（`AsFloat`）；对象/列表元素按 0 处理。
3. `ListSort.key` 只读取对象的直接属性，不支持嵌套路径或索引。
4. 列表长度上限固定为 `kMaxListElements`，超出静默截断。
5. 字节版字符串节点（`StringLength`、`StringPad` 等）不感知 UTF-8 边界；中文场景请改用 §0 的 `StringLengthChars` / `StringPadChars`。字符版按码点而不是字素簇计数（组合字符、ZWJ emoji 会分别计数）。
