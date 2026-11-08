# B6.4 范围 C：编辑器收尾

## C1 全量节点元数据生成（已完成）

新增两个纯 Python 工具：

- `scripts/startup/bge_gamenodes/generator/gen_metadata.py`
  - 从 C++ `KX_FunctionSpec`（`port_contract.load_cpp_specs()`）与
    `Signature()`（`load_signature_specs()`）读取每个节点的 inputs/outputs；
  - config keys 来自 `enum_consistency` 的 C++ 表（含追加的
    `KX_FunctionNodesB4.cpp/kB4Specs`），保证 enum 默认值可导出；
  - `ports_from_ir` 节点（Formula / StringFormat / ObjectPropertyOperator /
    UIAnimation 等）使用静态端口草图，审计时跳过端口严格比较；
  - 输出按 runtime_id 排序、内容完全确定（同源同输出，`--check` 幂等）。
- `scripts/startup/bge_gamenodes/generator/native_metadata_audit.py`
  - `load_editor_tables()` 读取所有 `editor/nodes/native_*.py` 表；
  - `scan_registered_ids()` 用 AST 扫源码中的 `runtime_type_id` / `deprecated`；
  - `audit()` 返回 `missing / unknown / port_mismatch / config_mismatch`：
    - `unknown` = 已注册但无元数据（仍会标红）；
    - `missing` = 未注册且不在 C++ 已知 id 中。

生成产物：`editor/nodes/native_metadata_gen.py`（当前 260 条）。
`ui/native_inspector.py` 以 `setdefault` 合并生成表（手写条目优先），
`audit_metadata()` 委托给审计模块，数据源覆盖 C++ spec、签名节点与全部编辑器表。

验证（无 bpy 环境，纯 Python）：

- `python3 tests/b64_metadata_check.py` →
  `checked=260 missing=0 unknown=0 port_mismatch=0 config_mismatch=0`
  （日志 `/tmp/b64_metadata_final.log`）。
- 与并发 B6.5a Legacy 新增节点共存：重新生成后仍全零。

复现命令：

```
python3 scripts/startup/bge_gamenodes/generator/gen_metadata.py
python3 -m py_compile scripts/startup/bge_gamenodes/editor/nodes/native_metadata_gen.py
```

## C2 热重载绿框（player 侧已验证，编辑器绘制 pending real device）

player 侧（复用 P4 的 `KX_DebugTrace`，没有另起一套 trace）：

- `KX_DebugTrace.h/.cpp`
  - 新增 `SetSnapshotPath()` / `SnapshotPath()`；`EndFrame()` 在写文本日志后
    按 100ms 节流写 `<blend_dir>/.debug_trace.json`；
  - JSON 结构：`{"frame": N, "nodes": [{"name": ..., "frame": N, "activations": C}]}`；
  - `activations` 是两次快照之间该节点被激活的帧数，写后清零。
- `KX_GraphSceneManager::CheckHotReload()`
  - 在与 `.hot_reload.flag` 同一目录轮询 `.debug_trace.on`：
    出现时 `SetEnabled(true)`，删除时 `SetEnabled(false)`（只在文件状态变化时切换，
    不覆盖 `BGE_GAMENODE_DEBUG` 环境变量或引擎偏好）；
  - 同时把快照路径设为 `<blend_dir>/.debug_trace.json`。
- 跨进程通道沿用 P4.2 的磁盘文件模式，没有新增 IPC。

编辑器侧（`ops/debug_trace.py`）：

- 新操作符 `bpy.ops.netlogic.toggle_debug_trace()`：
  - 第一次执行写 `.debug_trace.on` 并注册 `SpaceNodeEditor` 的 `POST_PIXEL` 绘制；
  - 再次执行删除该文件并移除绘制回调。
- 绘制回调读取 `.debug_trace.json`（mtime 缓存），对快照里出现的节点名
  用 `view2d.view_to_region` 做坐标换算并画绿色线框（`UNIFORM_COLOR`）。
- 已在 `ops/__init__.py` 注册操作符导入。

验证：

- player 侧：E2E 打开 trace 跑一帧后生成
  `{"frame": 1, "nodes": [{"name": "init", ...}, {"name": "anim_linear", ...}, ...]}`
  （日志 `/tmp/b64_e2e.log`，断言 `debug trace snapshot has node names and activation counts`）。
- 编辑器侧绿框绘制：当前环境无 Blender GUI / 虚拟 X，**pending real device**。
  需要在真实 Blender 中保存 .blend、开启操作符、运行播放器后人工确认绿框。
- 回归：P4.3 原有 trace 测试重编译后 `RESULT failures=0`
  （`/tmp/b64_reg_p43.log`）。

## C3 可选增强

时间不足，未实现：`CreateUILayout` 的 Polar 布局、Slider 旋钮 16 边形近似、
`CreateUILabel` 的 `{color:red}` 内联颜色。保持原有实现不变。
