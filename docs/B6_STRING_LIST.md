# B6.2 字符串 / 列表高级（14 节点）

日期：2026-11-05
范围：`StringSplit`、`StringJoin`、`StringRegexMatch`、`StringRegexReplace`、`StringTrim`、`StringPad`、`StringLengthChars`、`StringPadChars`、`ListSort`、`ListFilter`、`ListMap`、`ListFind`、`ListSlice`、`ListConcat`
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
| 运行时实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6String.cpp` | 新建；字符串 121-293，列表 294-750，表达式求值器 760-880，spec 表 884-910，注册 948-958 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6StringFunctionGameNodes()` 242 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 追加调用 241 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | `GameNodes/KX_FunctionNodesB6String.cpp` 200 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6b.py` | `NATIVE_B6B` 中 12 条 |
| 调色板 | `.../ui/node_palette.py` | 12 条字符串/列表条目 |

## 2. 节点与端口

| 节点 | 输入 | 输出 | config |
|---|---|---|---|
| StringSplit | Source, Separator | Parts(List<String>) | — |
| StringJoin | Parts(List), Separator | Result(String) | — |
| StringRegexMatch | Source, Pattern | Matched(Bool), Groups(List<String>) | — |
| StringRegexReplace | Source, Pattern, Replacement | Result(String) | — |
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

全部为 `is_value_node=true`，每帧求值；`ListSort.key` 走 `ConfigString`，`ListSort.descending` 走 `ConfigBool`，其余参数都是输入端口。

## 3. 字符串语义

- `StringSplit`：`Separator` 的**每个字符**都是一个分隔符（`",;"` 会把 `"a,b;c"` 拆成 `a` / `b` / `c`）；**保留空字段**，包括首段、连续分隔符与尾段（`"a,,b"` → 3 项，`",a,"` → `"" / a / ""`）；`Separator` 为空时按 ASCII 空白切分并把连续空白折叠为一个分隔符。元素上限 `KX_ValuePool::kMaxListElements`。
  语义变更：此前多字符 `Separator` 按整串精确匹配，现在按字符集合拆分；需要整串分隔时应改用单字符或先替换。
  实现按**字节**比较（`separator.find(source[end])`，147-158），因此适合 `,` `;` `|` 等 ASCII 分隔符；把多字节 UTF-8 字符（如 `；`）放进 `Separator` 会按它的每个字节分别拆开，不要这样用。
- `StringJoin`：元素按类型转文本（String 原样、Int 十进制、Bool `true/false`、Float `%.6g`），其他类型视为空串。
- `StringTrim`：`Mode` 0=两端、1=左侧、2=右侧。
- `StringPad`：`Side` 0=左补、1=右补；`PadChar` 为空时用空格，否则取第一个字符；目标长度非法（`<=` 当前长度或超过元素上限）时原样返回。
- `StringLengthChars` / `StringPadChars`：按 UTF-8 码点处理。解码器只接受合法的前导字节 + 后续 `0x80..0xBF` 组合；遇到非法前导、非法后续或截断尾部时，**按一个字符容错**并只打印一次 `[StringLengthChars]` / `[StringPadChars] invalid UTF-8 sequence: counted as one character`，绝不越界读取。截断的多字节尾部整体算一个字符，不会按剩余字节数重复计数。
- `StringRegexMatch`：`Groups` 第 0 项为完整匹配，之后按**左括号出现顺序**依次是捕获组（嵌套组外层在前、内层在后）；未参与匹配的可选组输出空串（无法与“匹配到空串”区分）；整体未匹配时 `Matched=false` 且 `Groups` 为空列表。ECMAScript 语法不支持 `(?<name>...)` 命名组，命名组按非法 Pattern 处理。
- `StringRegexReplace`：等价 `std::regex_replace`，替换串支持 `$1` 等 ECMAScript 格式。

### 正则限制

- 使用 C++11 `std::regex`（ECMAScript 语法），不引入 PCRE。
- **非法 Pattern** 由 `std::regex_error` 捕获：每个节点实例只写一次 `[StringRegexMatch] invalid pattern ...` / `[StringRegexReplace] invalid pattern ...` 诊断；前者返回 `Matched=false`，后者原样返回 `Source`，均不崩溃（191-247）。
- **未防护灾难性回溯**：`std::regex` 没有超时机制，病态模式（如嵌套量词对长输入）仍可能长时间占用 CPU。**不要对用户输入的任意长度文本使用复杂正则**；需要处理不可信输入时，请在上层限制字符串长度或改用非回溯匹配。节点层不做额外兜底。

## 4. 列表语义与生命周期

- 所有生成的列表都经 `ctx.Pool().AllocList(...)` 分配，节点从不释放或持有跨帧指针（`CopyIntoPool` 64-72）。沿用 B2/B3 的 `KX_ValuePool` 两代回收约定：本帧产生的列表在本帧与下一帧内有效，跨节点传递无需额外引用计数，也**不能**缓存到节点 state 里跨帧使用。
- `ListSort` 对输入做浅拷贝后排序，不修改输入列表。`key` 为空时按元素自身值排序；非空时把元素当作对象，用 `KX_PropertyAccessor` 读取该属性（数值或字符串）作为排序键（311-346）。比较器对降序使用 `less_key(b, a)`，满足严格弱序，`stable_sort` 保持相等元素原序。
- `ListFilter` 保留使表达式结果为非 0 的元素；`ListMap` 输出 `Float` 结果列表。求值遇到错误时：`ListFilter` 停止并把已接受的元素作为结果输出；`ListMap` 清空结果并输出空列表；两者各只打一次诊断。
- `ListFind` 按数值相等（Float/Int/Bool 互转）或字符串内容相等匹配，返回 `Index=-1 / Found=false` 表示未找到。
- `ListSlice`：`Start<=0` 视为 0，`End<=0` 视为 `size`，`End<Start` 视为空；越界自动截断到 `size`。
- `ListConcat` 依次追加 A、B，总长度受 `kMaxListElements` 截断。

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

- 编译：前一轮 `ninja -C build_linux -j6` exit=0（`/tmp/b62_build2.log`）。本轮编译器文件（`KX_CompiledGraph.cpp`）由 JIT 并行线占用，**未跑全量 ninja**；改为目标文件级验证：
  `c++ -c KX_FunctionNodesB6String.cpp` exit=0，输出 0 字节（无警告），产物 `/tmp/b6string_c6.o`，日志 `/tmp/b62_b6string_obj.log`。
- E2E：`/tmp/b62_e2e_v3` 58 项断言 + 汇总行全 PASS（59 行 `total failures=0`，`/tmp/b62_e2e_v3.log`）。
  该 harness 的 `KX_FunctionNodesB6String.cpp` 来自当前源码，只有 `KX_CompiledGraph.cpp` 取 19:16 快照对象
  （`/tmp/b661_cross_obj/game/KX_CompiledGraph.o`）以绕开正在被改写的编译器源文件；全量现源码链接会段错误，属并行线阻塞，不是字符串代码问题。

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

- 注册与枚举：`native_b6b.py` 27 行与 C++ 27 条 spec 完全一致 `MISSING []`；
  本轮全仓复跑 `enum_consistency.py` → `checked_pairs=108 inconsistencies=0`；
  `port_contract.py` → `native_nodes=223 errors=0 warnings=4`（4 条为既有的索引对齐 NOTE，非错误）。

## 7. 限制

1. 正则无回溯/时间预算（§3），恶意输入需在上层限制字符串长度。
2. Predicate/Expression 只支持 §5 子集，元素必须是数值语义（`AsFloat`）；对象/列表元素按 0 处理。
3. `ListSort.key` 只读取对象的直接属性，不支持嵌套路径或索引。
4. 列表长度上限固定为 `kMaxListElements`，超出静默截断。
5. 字节版字符串节点（`StringLength`、`StringPad` 等）不感知 UTF-8 边界；中文场景请改用 §0 的 `StringLengthChars` / `StringPadChars`。字符版按码点而不是字素簇计数（组合字符、ZWJ emoji 会分别计数）。
