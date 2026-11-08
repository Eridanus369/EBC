# GameNode 实机验证操作指南（Blender 5.3 fork）

本文件是**可照抄的操作手册**，不是成绩单，也不是能力清单。按顺序逐项做，不需要再回头问任何问题。

- 计数口径：实机验证**不参与**代码侧能力域的百分比计算，也不因未验证而扣分。代码侧证据见 `docs/PROJECT_STATUS.md`。
- **第 0 节先做这一步（manifest 自检）**：确认 Game Nodes 模块随引擎启动加载（`scripts/startup/bge_gamenodes`），并重新导出场景 manifest。manifest 导出**恒执行**，没有开关要勾；旧 `.blend` 里的 `nl_` 组件不会被自动转换，运行时只警告并跳过。
- **编辑器清理已确认（清理线实测）**：砖块编辑器入口已从编辑器类型下拉、`Preferences → Themes → Area` 下拉中消失；`blender_default.py` 的 `km_logic()` 与注册已删；旧 `.blend` 的 `LOGIC_EDITOR` 区域在新二进制 GUI 下回退成 `VIEW_3D`，`--background` 下该区域类型读成空串（screen 不初始化）属正常。详见第 12 节。
- `unsupported` 项只在 `docs/DEFERRED_FOREVER.md` 登记，不列进度检查表（触摸输入见第 11 节）。
- 第 10 项（传感器 shape overlap）已由 headless 测试覆盖，不需要实机；总检查表保留该行并标 `N/A`，说明这一项想过且已闭环。
- 本文所有 `文件:行号` 均为仓库内真实位置，改动源码后行号可能漂移，按函数名搜索即可。
- 记录目录：所有日志、截图、结论都落在自建的 `real_device_check/<RUN_ID>/` 下，不写 `/tmp`（`/tmp` 只放脚本自身的运行日志）。

---

## 0. 环境准备

### 0.1 目的

确认编辑器与独立 player 能在本机启动，确认 SYCL 运行库链接正确，确认 CJK 字体栈可用，并确认 **C++ 原生图运行时真正接管**（原生运行时恒执行，场景里已写入 manifest），最后建立后续各项共用的目录、场景与操作习惯。对应域：全局运行环境，不是某个节点。

### 0.2 前置条件

- Linux 桌面会话（X11 或 Wayland 均可），有可用的 OpenGL/Vulkan 上下文；纯 SSH 无显示环境不能做本指南中的任何一项。
- 仓库位于 `/home/eridanusepsilon/Code/EBC/EBC5.3`，且 `build_linux/bin/blender`、`build_linux/bin/blenderplayer`、`lib/dpcpp/lib/`、`lib/shaderc/lib/` 均已存在（只做验证，不需要重新编译）。
- 可写目录：`/home/eridanusepsilon/Code/EBC/EBC5.3/real_device_check/`。
- 两张输入设备不需要现在插上；第 1、2 项再插。

### 0.3 操作步骤

本节所有命令都先在同一终端、仓库根目录执行一次（若当前不在，先 `cd /home/eridanusepsilon/Code/EBC/EBC5.3`）：

```bash
export LD_LIBRARY_PATH="$PWD/lib/dpcpp/lib:$PWD/lib/shaderc/lib"
```

后面各节一律写「沿用第 0 节的 export」，不再重复这条命令；只记 `lib/dpcpp/lib` 会因缺 `libshaderc_shared.so.1` 直接 exit 127。

1. **确认 manifest 已写出（唯一的硬前置）。** Game Nodes 已随引擎启动加载（`scripts/startup/bge_gamenodes`），不在偏好设置的插件管理页列表里、不可禁用，也没有“原生运行时”开关；manifest 在保存与启动游戏时恒写（`scripts/startup/bge_gamenodes/__init__.py:74`、`:79-80`）。启动编辑器：

   ```bash
   build_linux/bin/blender &
   ```

   在编辑器里做：
   1) 打开要验证的 `.blend`；场景里必须已经有一棵绑定到对象的树。全新空场景先做步骤 7（选中 Cube → 面板上点 `New` 建树），再回到这里——没有 `graphs`/`mounts` 时导出会主动删掉该属性（`scripts/startup/bge_gamenodes/generator/ir_export.py:465-468`），这不是环境失败；
   2) 按 `Ctrl+S` 保存——保存处理器每次都重写 `scene["game_graph_manifest"]`；也可以按 `F3` 搜 `Hot Reload Game Graphs` 手动导出；
   3) 把鼠标悬停在任意面板上按 `Shift+F4` 切到 Python Console，输入：

      ```python
      "game_graph_manifest" in bpy.context.scene
      len(bpy.context.scene["game_graph_manifest"])
      ```

      两行分别必须返回 `True` 和一个大于 0 的整数；也可直接打印属性，确认 JSON 里含 `"graphs"` 与 `"mounts"`。
   4) 判定依据：C++ 运行时只按这个属性工作，读不到就直接不跑图（`source/gameengine/Ketsji/KX_Scene.cpp:2498-2502`、`:2520-2533`）。
   5) 想确认设置项时，路径是 `Edit → Preferences → Game → Game Nodes`（引擎原生 `bpy.context.preferences.game_nodes`，`source/blender/makesrna/intern/rna_userdef.cc:8267`）；这里不需要改任何开关。

   **两个容易踩的点**（照做即可避开）：
   - C++ 只认场景里的 manifest：用 `blenderplayer` 跑一个已带 manifest 的 `.blend` 就会跑 C++；`.blend` 里没有 manifest（例如只建了树但没挂到对象）则节点毫无反应（`KX_Scene.cpp:2498-2502`）。
   - 旧 `.blend` 里 `nl_` 前缀的 Python 组件**不会自动转换**：运行时会打印一次警告并跳过执行（`source/gameengine/Ketsji/KX_GameObject.cpp:1991-1998`）。

2. 打开终端，进入仓库根目录：

   ```bash
   cd /home/eridanusepsilon/Code/EBC/EBC5.3
   export CK_ROOT="$PWD/real_device_check"
   export RUN_ID="$(date +%Y%m%d-%H%M%S)"
   export CK_DIR="$CK_ROOT/$RUN_ID"
   mkdir -p "$CK_DIR/scenes" "$CK_DIR/logs" "$CK_DIR/shots"
   echo "$RUN_ID" | tee "$CK_DIR/run_id.txt"
   ```

   `RUN_ID` 只创建一次，已存在就换时间重跑，不要覆盖旧目录。

3. 在一个**没有 export** 的终端里直接跑版本，确认会复现运行期链接问题（这是实测到的部署现象，不是构建失败）：

   ```bash
   env -u LD_LIBRARY_PATH build_linux/bin/blender --version 2>&1 | tee "$CK_DIR/logs/00_version_no_env.log"
   ```

   预期先看到 `libshaderc_shared.so.1: cannot open shared object file`（`libsycl.so.8` 同样缺，但动态链接器先报 shaderc）。

4. 回到已 export 的终端再跑：

   ```bash
   build_linux/bin/blender --version 2>&1 | tee "$CK_DIR/logs/00_version.log"
   ```

5. 对两个可执行文件用 `ldd` 确认没有悬空依赖（比启动窗口更快，也更可判定）：

   ```bash
   ldd build_linux/bin/blender     2>&1 | tee "$CK_DIR/logs/00_ldd_blender.log"
   ldd build_linux/bin/blenderplayer 2>&1 | tee "$CK_DIR/logs/00_ldd_player.log"
   ```

   **不要用 `blenderplayer --version` 测环境**：实测它会把 `--version` 当文件名解析并 exit 139（段错误），容易被误判成环境故障；player 侧只用 `ldd` 与真实 `.blend`。

6. 确认 CJK 字体在发行数据里（player 会在第一次绘制 UI 时调用 `BLF_load_font_stack()`，见 `source/gameengine/GameLogic/GameNodes/KX_UIAdapterEngine.cpp:61`）：

   ```bash
   ls release/datafiles/fonts | grep -F "Noto Sans CJK Regular.woff2" | tee "$CK_DIR/logs/00_fonts.log"
   ```

7. 建立第一棵 Game Nodes 树（后续每一项都按同一套路）。**不要找 `Logic Editor` / 砖块编辑器入口**，节点树一律在 Node Editor 里建（原因见第 12 节）：

   1) 先在 3D 视图点选 Cube，让它成为活动对象（`New` 只绑活动对象，见下方“编辑器体感”）；
   2) 把任意一个面板切成 `Node Editor`；
   3) 顶栏树类型下拉选 `Game Nodes`（显示名；`bl_idname` 仍为 `BGELogicTree`）；这时编辑器自动显示 Cube 的主树，Cube 还没有树时是空状态面板「该对象还没有 Game Nodes 树」；
   4) 在空状态面板上点 `New`，把树重命名为 `env_check`——树同时绑定到 Cube，不需要再点任何 Apply；
   5) 按 `N` 打开侧栏，切到 `Game Nodes` 分类，`Node Palette` 面板里按分类点节点名即可添加节点（如 `OnInit`、`Print`）。

   **编辑器体感（本轮交互改动，照此理解）**：
   - **自动显示**：树类型切到 `Game Nodes` 后，编辑器始终显示当前活动对象的主树；在 3D 视图换选中的物体，编辑器内容跟着换，不需要 Apply。
   - **空状态**：活动对象没有树时，编辑器是一块空状态面板，写着「该对象还没有 Game Nodes 树」，带一个 `New` 按钮；点它就为当前活动对象建一棵新树。
   - **多选时 `New` 只绑活动对象**（几何节点语义）：多选 3 个对象点 `New`，只有活动对象得到树，另外两个保持不变。要让同一棵树挂到多个对象，用 `F3` 搜 `game_nodes.apply_logic_tree`（该算子仍存在、语义仍是多选，但已无侧栏面板入口）。
   - **`pin`**：手动从编辑器头部下拉换到别的树只是临时查看，下一次重绘会被当前活动对象的主树覆盖；要固定看某一棵树，先点头部下拉旁的 `pin`。固定后再切换选中对象，编辑器也不跟着换。
   - 旧的侧栏 `Dashboard → Administration → Apply To Selected` 面板已删除，`Administration` 分类整体不存在；本指南里任何“点 Apply”字样都已改为上面的新流程。

8. 确认绑定并运行：回到 3D 视图选中 Cube，再看节点编辑器——应直接显示 `env_check` 这棵树（这一步不需要任何 Apply）。按 `Ctrl+S` 存到 `$CK_DIR/scenes/env_check.blend`（保存会再写一次 manifest，见步骤 1）；然后在终端启动 player，并检查日志里没有 Python 回退：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/env_check.blend" 2>&1 | tee -a "$CK_DIR/logs/00_player.log"
   grep -c "failed to compile, the Python fallback stays active" "$CK_DIR/logs/00_player.log"
   ```

   最后一条命令必须输出 `0`。树绑定到 Cube 后，这次保存已经重写了 manifest，回到步骤 1 的 Python Console 再确认一次 `"game_graph_manifest" in bpy.context.scene` 为 `True`。

9. 屏幕截图一律保存到 `$CK_DIR/shots/`，命名 `<项号>_<动作>.png`（用桌面自带截图工具即可）。

### 0.4 预期结果

- 第 1 步：`"game_graph_manifest" in bpy.context.scene` 返回 `True`，且 manifest 长度大于 0、JSON 里同时含 `"graphs"` 与 `"mounts"`。
- 第 3 步先报 `libshaderc_shared.so.1` 缺失（`libsycl.so.8` 同样缺，只是动态链接器先报 shaderc）；第 4 步打印 `Blender 5.3.x` 版本号，进程退出码 0。
- 第 5 步两个 `ldd` 日志里 `grep "not found"` 结果为空（0 行）。
- 第 6 步字体清单输出含 `Noto Sans CJK Regular.woff2`。
- 编辑器里能看到 `Game Nodes` 树类型（`bl_idname` 仍为 `BGELogicTree`）与 `Node Palette`（`Node Editor` 内）；选中 Cube 时编辑器自动显示它的 `env_check` 树，切换选中对象内容跟着换；Cube 没有树时应看到空状态面板「该对象还没有 Game Nodes 树」+ `New` 按钮。
- player 窗口正常打开，日志里没有 `libsycl` 或 `cannot open shared object file`。
- 第 8 步的 `grep -c` 输出 `0`：没有 “Graph '...' failed to compile, the Python fallback stays active”（该提示由 `source/gameengine/GameLogic/GameNodes/KX_GraphSceneManager.cpp:139` 产生，经 `Warn` 打印成 `GameNode: ...`，`KX_GraphSceneManager.cpp:469-470`）。

### 0.5 失败判定

- 已 export 仍报 `libshaderc_shared.so.1`：漏了 `lib/shaderc/lib`，按 0.3 开头的完整 export 重来；若仍报 `libsycl.so.8`，确认 `lib/dpcpp/lib/libsycl.so.8` 存在（运行库定位逻辑见 `build_files/cmake/Modules/FindSYCL.cmake:1-40`）。日志存 `$CK_DIR/logs/00_ldd_player.log`。
- `--version` 能过但开窗口即崩：记录退出码与 `$CK_DIR/logs/00_player.log`，并截图 `shots/00_crash.png`；保留窗口崩溃前的最后一行。
- 找不到 `Game Nodes` 树类型：你用的是旧二进制或没有从 `build_linux/bin/blender` 启动（Game Nodes 已随引擎启动加载，`scripts/startup/bge_gamenodes`，不可禁用）；设置项在 `Preferences → Game → Game Nodes`（`source/blender/makesrna/intern/rna_userdef.cc:8267`）。
- 见到 `Add-on not loaded`：说明跑的是旧二进制（旧插件形态已删除）；换当前构建。
- 第 1 步 `"game_graph_manifest" in bpy.context.scene` 返回 `False`：场景里没有挂到对象上的树，或保存没发生。回到步骤 1 与步骤 7；导出在没有 `graphs`/`mounts` 时会主动删除该属性（`scripts/startup/bge_gamenodes/generator/ir_export.py:465-468`）。
- player 能开窗但节点毫无反应（没有 `OnInit` 的输出）：场景里没有 manifest，`GraphTick` 直接返回（`source/gameengine/Ketsji/KX_Scene.cpp:2498-2502`）。回到步骤 1 用 `Ctrl+S` 重新导出。
- 日志出现 `Graph '...' failed to compile, the Python fallback stays active`：图编译失败，按失败处理（提示文本仍在源码里，`KX_GraphSceneManager.cpp:139`；不要等 Python 兜底）。日志 `00_player.log`，节点/资产细节看同一行前后的 `GameNode: Graph ...` 警告（`KX_GraphSceneManager.cpp:469-470`）。
- 日志出现 `Object '<对象名>': component '<组件名>' uses the old Python logic executor, please re-export as Game Nodes`：该对象还在用旧 `nl_` 前缀的 Python 组件，运行时已跳过它。重新导出 manifest，并删除该对象上的旧组件，再按第 0 节步骤 7–8 让它成为活动对象、由编辑器自动显示或点 `New` 新建树（旧组件不会自动转换）；要把同一棵树挂到多个对象，用 `F3` 搜 `game_nodes.apply_logic_tree`（该算子仍存在，但无侧栏面板）；来源 `source/gameengine/Ketsji/KX_GameObject.cpp:1991-1998`。
- 中文全部显示为方块：先查步骤 6 的字体文件，再查 player 是否运行在 `--factory-startup` 外的方式下（字体栈只在 UI 首次绘制时加载，见 `KX_UIAdapterEngine.cpp:61`）。

---

## 1. 手柄输入

### 1.1 目的

验证 `GamepadCount`、`GamepadAxis`（左/右摇杆）、`GamepadTrigger`（LT/RT）、`GamepadButton`（边沿 press/release/hold）四条链路，从 SDL 设备层一路到节点输出值。对应域：输入（B6.1 手柄输入族）。

### 1.2 前置条件

- 硬件：Xbox 或 PS 手柄一只，USB 或有线连接；系统 `ls /dev/input/js*` 能看到设备。
- 场景：`$CK_DIR/scenes/b67_input_nodes.blend`（本项首次运行时按 1.3 建立，后续第 2、5 项复用它）。
- 环境变量：运行 player 时沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。
- 不需要装任何驱动包，SDL 自带映射；手柄电量/连接灯正常即可。

### 1.3 操作步骤

1. 新建 `General` 工程，保留默认 Cube，把 Cube 移到视口左侧；选中 Cube（活动对象），新建 Game Nodes 树，命名 `b67_input_nodes`。
2. 按第 0 节步骤 7 的方式加节点，按下表连线（`Node Palette` 里逐个点）：

   | 节点 | 关键配置 | 连线 |
   |------|----------|------|
   | `OnUpdate` | 无 | 触发输出 `OnOut` → `Vector3` 的 OnEnter |
   | `GamepadCount` | 无 | `Count` → `Print` 的数值输入 |
   | `GamepadAxis` | `pad=0`，`axis=0`（左摇杆），`deadzone=0.1` | `X` → `Vector3` 的 X；`Y` → `Vector3` 的 Z |
   | `GamepadAxis` | `pad=0`，`axis=1`（右摇杆），`deadzone=0.1` | 先空挂，观察时接 `Print` 的 X |
   | `GamepadTrigger` | `pad=0`，`trigger=0`（LT），`deadzone=0.05` | `Value` → `Print` |
   | `GamepadTrigger` | `pad=0`，`trigger=1`（RT），`deadzone=0.05` | `Value` → `Print` |
   | `GamepadButton` | `pad=0`，`button=0`（A/叉），`edge=0`（Press），`deadzone=0` | `IsDown` → `Print` |
   | `GamepadButton` | `pad=0`，`button=1`（B/圈），`edge=1`（Release） | `IsDown` → `Print` |
   | `Vector3` | 三个分量为 X/0/Z，第 2 个分量填 `0` | `Vector` → `SetPosition.Value` |
   | `SetPosition` | 无 | `Object` 连 `GetOwner`；`OnEnter` 连 `OnUpdate` 的 `OnOut` |
   | `GetOwner` | 无 | `Object` → `SetPosition.Object` |

   说明：`GamepadAxis` 的 `axis` 配置是**摇杆对**编号（0=左右一对，1=右摇杆一对），不是单轴编号；`pad` 是手柄序号。

   端口名按本轮新命名接线（旧名只在此处作为对照出现）：`OnInit`/`OnUpdate` 的触发输出为 `OnOut`（`KX_RuntimeNodes.cpp:1078`、`:1095`）；`Branch` 的触发口为 `OnEnter`/`OnTrue`/`OnFalse`（布尔输入口仍叫 `Condition`，`KX_RuntimeNodes.cpp:466-473`）；`Delay` 为 `OnEnter`/`OnDone`（`KX_RuntimeNodes.cpp:495-507`）；`Sink` 为 `OnEnter`（`KX_RuntimeNodes.cpp:1159-1165`）；`Asset Get`/`Asset Set` 为 `OnRead`/`OnWritten`（`KX_RuntimeNodes.cpp:596-605`、`:757-765`）；legacy `NLActionPrint` 的 `Condition`/`Done` 已改为 `OnEnter`/`OnDone`（`scripts/startup/bge_gamenodes/editor/nodes/actions/print.py:27-29`）。
3. 选中 Cube（活动对象），切回节点编辑器确认 `Game Nodes` 树类型下自动显示的就是 `b67_input_nodes`（若显示空状态，用 `F3` 搜 `game_nodes.apply_logic_tree` 把这棵树挂到 Cube）；`Ctrl+S` 存到 `$CK_DIR/scenes/b67_input_nodes.blend`。
4. 终端启动 player（保持终端可见，Print 会写 stdout）：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/b67_input_nodes.blend" 2>&1 | tee "$CK_DIR/logs/01_input.log"
   ```

5. 只插一只手柄，观察 `GamepadCount` 的打印值；拔掉手柄再看一次（不需重启 player，SDL 热插拔会更新）。
6. 左摇杆推到最右，再推到最上、最下、最左，每次保持 1 秒；Cube 应随摇杆在视口内移动（X 对应左右，Z 对应上下）。
7. 右摇杆同样推到最右，观察 `Print` 里右摇杆 X 是否也接近 +1。
8. 按住 LT 到底，再按 RT 到底；观察两个触发值的打印区间。
9. 按 A/叉键一次（按下即看日志），再按 B/圈键一次（松开后看日志）。
10. 截图：`shots/01_stick_right.png`（摇杆右推 + Cube 位置）。

### 1.4 预期结果

- `GamepadCount`：插一只打印 `1`，拔掉打印 `0`；插两只打印 `2`。
- 左摇杆：右推 `X ≈ +1.0`，左推 `X ≈ -1.0`；**上推 `Y ≈ -1.0`，下推 `Y ≈ +1.0`**（适配器把 SDL 原始轴直接除以 32767，未翻转 Y，见 `KX_GameAdaptersEngine.cpp:449-460`）；回中 `|X|,|Y| ≤ 0.1`（死区 0.1 生效）。
- 右推时 Cube 沿 +X 移动；上推时 Cube 向视口的“上”（世界 -Z 方向）移动，位移随摇杆幅度单调变化。
- 右摇杆 X 与左摇杆独立：推右摇杆时左摇杆输出不变化（±0.05 内）。
- LT/RT：从 0 平滑升到约 `1.0`；松开回 0；两扳机互不影响（`KX_GameAdaptersEngine.cpp:462-471`，轴 4/5）。
- 按键：A 键按下后 `IsDown` 变为 1 并保持到松开（输出每帧都写，`KX_FunctionNodesB6Input.cpp:137`）；`edge=0` 的触发口只在按下沿触发一次，按住不重复触发（`KX_FunctionNodesB6Input.cpp:144-146`）。B 键配 `edge=1`，只在松开沿触发一次（`KX_FunctionNodesB6Input.cpp:126-156`）。

### 1.5 失败判定

- `GamepadCount` 恒 0：系统层没识别手柄（先 `ls /dev/input/js*`），或 SDL 初始化失败；日志 `$CK_DIR/logs/01_input.log`，截图 `shots/01_no_pad.png`；设备枚举入口见 `KX_GameAdaptersEngine.cpp:438-447`。
- 摇杆有值但方向相反（上推 `Y > 0`）：记录实际符号并截图；轴值未翻转属实现事实（`KX_GameAdaptersEngine.cpp:457-459`），此项记为“方向约定失败”，不要改判据。
- 按键按了没反应：先在终端确认 `button` 配置号，A/B/X/Y 对应 0/1/2/3；数字键仍无反应则记录 `KX_GameAdaptersEngine.cpp:427-436` 与日志，判断是设备层还是节点层问题。
- 摇杆回中不为 0：确认 `deadzone` 配置被写成 0.1（节点配置字段），回中残值 >0.1 时记录 `01_input.log` 并截图。

---

## 2. 手柄震动

### 2.1 目的

验证 `VibrateGamepad` 能把强度与时长送到真实马达，且 `OnDone` 在抑制（强度 ≤ 死区、无马达、无手柄）时仍会触发，图不会卡住。对应域：输入（震动子项）。

### 2.2 前置条件

- 硬件：带马达的手柄（Xbox One/Series、DualSense、DualShock 4 等）；部分第三方手柄没有马达，会直接走失败分支。
- 场景：复用第 1 项的 `$CK_DIR/scenes/b67_input_nodes.blend`。
- 环境变量：沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）；player 从终端启动，便于看 stderr。

### 2.3 操作步骤

1. 打开 `b67_input_nodes.blend`，在节点编辑器里另建一条链（不改第 1 项那条）：`OnUpdate` → `VibrateGamepad`。
2. `VibrateGamepad` 连线与配置：`Pad=0`、`Intensity=0.8`、`Duration=1.0`（秒）；`OnDone` 接 `Print` 输入（打印固定字符串 `VIB_DONE`）。为避免每帧都震动，在 `OnUpdate` 与 `VibrateGamepad` 之间加 `Timer`，间隔设 2 秒（或用 `KeyCombo` 触发，见步骤 4 的可选做法）。
3. `Ctrl+S`，终端启动 player：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/b67_input_nodes.blend" 2>&1 | tee "$CK_DIR/logs/02_rumble.log"
   ```

4. 可选：把触发改成 `KeyCombo`（Space）→ `VibrateGamepad`，按一次空格震一次，便于计数。
5. 记录手腕感受：强度 0.8、时长 1 秒，每 2 秒一次。
6. 把 `Intensity` 改成 0（小于等于死区），再跑一次。
7. 拔掉手柄再跑一次。
8. 截图/录屏：`shots/02_rumble.png`（可选，震动本身无法截图，日志与体感为主）。
9. 空槽自检（不需要手柄）：另放一个 `Print`，`Value` 故意**不连线、也不在节点上填默认值**，`OnEnter` 接同一个 `OnUpdate`；保存后跑 player，看 stdout。

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/b67_input_nodes.blend" 2>&1 | tee "$CK_DIR/logs/02_print_unset.log"
   ```

### 2.4 预期结果

- 每次触发都有一次约 1 秒的双马达震动（左右马达同时，代码把同一强度传给左右，见 `KX_GameAdaptersEngine.cpp:537-540`）。
- 终端每次震动后打印一行 `VIB_DONE`；不震动时也打印（抑制分支仍 `OnDone`，见 `KX_FunctionNodesB6Input.cpp:462-467`）。
- `Intensity=0` 时完全没有震动，但 `VIB_DONE` 仍按节拍打印。
- 拔掉手柄后没有震动，stderr 出现 `GameNode: VibrateGamepad: no gamepad at index 0`（`KX_GameAdaptersEngine.cpp:524-529`）。
- 时长超过 10 秒会被截断到 10 秒（`KX_GameAdaptersEngine.cpp:538-539`）；本项只测 1 秒，不触发截断。
- 第 9 步的空槽 `Print` 打印 `<Print: Value input unset>`（**不再打印 `None`**；实现见 `KX_FunctionNode.cpp:412-430`，行为登记见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) #32）。本项其它 `Print` 填了固定字符串，保存时会物化成 `ConstString`，仍原样打印——两种行为应同时出现。

### 2.5 失败判定

- 有马达但不震：日志出现 `has no rumble support`（`KX_GameAdaptersEngine.cpp:531-535`）——设备层报告无马达能力，记录手柄型号并截图设备管理器信息，存 `$CK_DIR/logs/02_rumble.log`。
- 震动一触发就持续不停：说明 `Duration` 单位填错（应为秒，内部乘以 1000 转毫秒，见 `KX_GameAdaptersEngine.cpp:538-539`），把该次运行的日志与配置截图存档。
- `OnDone` 从不出现：触发链不同帧，检查 `Timer`/`OnUpdate` 是否接到 `OnEnter`；节点侧见 `KX_FunctionNodesB6Input.cpp:457-473`。
- 空槽 `Print` 打出 `None` 或空行：跑的是旧二进制或旧 IR；重新导出 manifest 并按第 0 节确认版本，日志存 `$CK_DIR/logs/02_print_unset.log`。

---

## 3. 3D 音效

### 3.1 目的

验证 `Set3DListener` + `PlaySound3D` + `SetAudioAttenuation` 的空间化链路：左右声道分离、距离衰减、播放前必须先设衰减。对应域：音频（B6.2 3D 音频族）。

### 3.2 前置条件

- 硬件：双声道耳机或双声道音箱（单声道设备无法判断左右，直接跳过本项的左右判据）。
- 场景：`$CK_DIR/scenes/b62_audio.blend`（本项建立）；一个 1–2 秒的稳态正弦/wav 文件（建议 440 Hz，文件名 `b62_tone.wav`）。
- 资源：sound 必须作为 Blender 数据块加载进 `.blend`，**且必须打包或与 `.blend` 同目录**，否则独立 `blenderplayer` 按名字找不到：
  - 方式 A：`File → External Data → Pack Resources`（把 wav 打进 `.blend`）；
  - 方式 B：wav 与 `.blend` 放在同一目录，sound 的路径用 `//` 相对路径（中文路径可用）；
  - 关键坑：`users=0` 的 Sound 数据块在保存时不会写进文件——编辑器按 P 有声音、player 找不到，这是最常见的误判（[KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) #31）。
- 环境变量：沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。

### 3.3 操作步骤

1. 新建工程，保留默认 Cube，把摄像机设为 `(0,-5,0)` 并朝 +Y 看（或用默认摄像机，站在原点看 -Y 也可；下文的“左/右”以摄像机自身左右为准）。
2. 载入音频：在 `Scripting` 工作区或任意面板按 `F3` 搜 `Load Sound`（`game_nodes.load_sound`），选 `b62_tone.wav`。到 `Outliner → Blender File → Sounds` 看数据块名，**记下这个名字**（注册名是去掉 `SO` 前缀的 ID 名，见 `source/gameengine/Converter/BL_Converter.cpp:214-218`），例如 `b62_tone.wav`。
   再确认该数据块的 **Users ≥ 1**（`Outliner` 里名字右侧的数字；为 0 就先让节点引用它或把它挂到场景，再保存）；随后 `File → External Data → Pack Resources`，或用 `//b62_tone.wav` 相对路径与 `.blend` 同目录。保存前这两件事任一未满足，player 段就会报 `not found`。
3. 选中 Cube（活动对象），新建 Game Nodes 树 `b62_audio`，按下表建立两条播放链（同一个帧内先设衰减再播放，顺序不能反）：

   | 顺序 | 节点 | 配置/端口 |
   |------|------|-----------|
   | 1 | `OnInit` | 触发输出 `OnOut` 接下一节点 `OnEnter` |
   | 2 | `Set3DListener` | `Position=(0,-5,0)`，`Orientation=(0,1,0)`（摄像机朝 +Y） |
   | 3 | `SetAudioAttenuation` | `Object` 空着（不连），`Model=1`，`RefDist=1.0`，`MaxDist=20.0` |
   | 4 | `PlaySound3D` | `Sound=<数据块名>`，`Position=(-3,-1,0)`，`Volume=1.0`，`Pitch=1.0` |
   | 5 | `PlaySound3D` | `Sound=<数据块名>`，`Position=(3,-1,0)`，`Volume=1.0`，`Pitch=1.6` |

   要点：
   - 两条 `PlaySound3D` 必须由 `OnInit` 同一帧依次触发；若分成两帧，第二声会覆盖听感。
   - `SetAudioAttenuation` 的 `Object` 留空表示设备级设置，会立刻套到已有 3D 句柄（`KX_GameAdaptersEngine.cpp:2675-2681`）；对象级设置在 `PlaySound3DFor` 播放时才被读取并套用（`:2865-2878`），所以对象级必须**先设后播**，播放后再调不会影响已经在响的实例。
4. 选中 Cube，回到节点编辑器确认自动显示的是 `b62_audio` 树（空状态就用 `F3` 搜 `game_nodes.apply_logic_tree` 把它挂到 Cube）；`Ctrl+S` 存 `$CK_DIR/scenes/b62_audio.blend`。
5. 戴耳机启动 player：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/b62_audio.blend" 2>&1 | tee "$CK_DIR/logs/03_audio.log"
   ```

6. 判断左右分离：左耳应只听到低音（440 Hz），右耳应只听到高音（pitch 1.6 ≈ 704 Hz）。
7. 距离衰减：把两条 `PlaySound3D` 的 `Position` 都改成 `(0,-1,0)`（贴近听者），再跑一次；然后把 `Position` 改成 `(0,25,0)`（超出 MaxDist 20），再跑一次。
8. 可选多普勒：在 `OnInit` 链的 `SetAudioAttenuation` 之后加入 `SetAudioDoppler`（`Object=GetOwner`，`Factor=3`），`PlaySound3D` 的 `Object` 归属由节点拥有者决定；让 Cube 沿 Y 轴匀速远离，听音调是否下沉。
9. 日志与结论写入 `$CK_DIR/logs/03_audio.log`，截图 `shots/03_headphones.png`（拍设备与窗口即可）。

### 3.4 预期结果

- 左右分离：左声道明显只有低频，右声道明显只有高频；用手机声级计贴耳测，左右声道电平差 `> 6 dB`，或主观上“单侧可闻”。两耳同时听到同一音高即失败。
- 衰减：`Position=(0,-1,0)` 时音量明显大于 `Position=(-3,-1,0)`；`Position=(0,25,0)` 超出 `MaxDist=20` 时无声（距离设置在播放时应用，见 `KX_GameAdaptersEngine.cpp:2874-2876`）。
- 无报错：`PlaySound3D` 找不到 sound 名时 stderr 打 `[PlaySound3D] unknown sound`（`KX_FunctionNodesB6Audio.cpp:169-171`），正常时应为空。
- 多普勒（可选）：远离时音调下降，靠近时上升；不明显时按“未通过（可选）”记录，不要改判据。

### 3.5 失败判定

- 左右不分：先确认听者朝向与 `Orientation` 一致；再确认两个 `PlaySound3D` 在同一帧触发；记录 `03_audio.log` 与 `shots/03_no_pan.png`，空间化实现见 `KX_GameAdaptersEngine.cpp:2810-2845`。
- 完全无声但无报错：`Volume=1.0` 是否被覆盖、`MaxDist` 是否小于实际距离；日志与 `03_audio.log` 存档。
- 报 `unknown sound`：数据块名写错（含扩展名/空格），回到步骤 2 用 Outliner 里的确切名字。
- player 报 `not found`、但编辑器里按 P 有声音：资源没打包，或 Sound 数据块 `users=0` 没被写进文件——按 3.2 的方式 A/B 处理后重存再跑（[KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) #31）；这一条**不要**记成节点实现失败。
- 距离改了没区别：确认 `SetAudioAttenuation` 在 `PlaySound3D` **之前**触发（`KX_GameAdaptersEngine.cpp:2865-2878`），或 `Object` 端口没有误接。
- `Set3DListener` 后仍然“贴脸响”：检查 Orientation 是否为零向量；监听位置写入见 `KX_GameAdaptersEngine.cpp:2563-2581`。

### 3.6 实机状态（2026-11-07）：部分通过

- **通过**：节点调度（`OnInit` → `Set3DListener` → `SetAudioAttenuation` → `PlaySound3D` 同帧触发）与 3D 坐标/衰减计算通过（`KX_GameAdaptersEngine.cpp:2563-2581`、`:2675-2681`、`:2810-2845`）。
- **阻断**：独立 `blenderplayer` 下资源加载失败，stderr 报 `not found`，3D 音频无法出声，左右分离/衰减/多普勒三项听感判据无法完成——**Player 资源加载失败阻断验证**，不是节点调度或 3D 计算失败。
- **归因**：现象已确认；根因未确认，候选见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) #31（① `BL_SceneConverter` 未注册 Sound 数据块；② 注册名不一致；③ 资源未打包/相对路径失效）。本项不据此改判节点实现。
- **复测前提**：先按 3.2 完成「打包 或 wav 与 `.blend` 同目录 + users ≥ 1」再跑；未打包时的 `not found` 属于资源层，不计入 3.4/3.5 的节点判据。
- **口径**：该项不改变 325 口径的六量与三个百分比——编辑器内按 P 正常，节点实现本身无缺失，属运行时资源加载器层问题，不进 `approximate`。

---

## 4. 热重载绿框

### 4.1 目的

验证两件事：运行中的 player 会持续写 `.debug_trace.json`，编辑器据此给“最近一帧执行过”的节点画绿色描边；编辑图并触发 hot reload 后，player 下一帧按新图执行，绿框继续跟随。对应域：UI / 图运行时。

### 4.2 前置条件

- 双窗口：一个编辑器窗口打开 `.blend`，一个 `blenderplayer` 进程打开**同一个** `.blend`。
- 场景：`$CK_DIR/scenes/c7_ui.blend`（本项建立），目录必须可写（player 要在同目录写 trace 文件）。
- 环境变量：沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。
- 关键开关：编辑器侧用 `F3 → Toggle Node Debug Trace`（`gamenodes.toggle_debug_trace`）生成 `<blend目录>/.debug_trace.on`。该算子 `poll()` 恒真（`scripts/startup/bge_gamenodes/ops/debug_trace.py:142-146`），**在任何编辑器上下文都能搜到并执行**，不需要先切到节点编辑器。

### 4.3 操作步骤

1. 新建工程，保留 Cube；选中 Cube（活动对象）后新建 Game Nodes 树 `c7_ui`；加 `OnUpdate` → `Vector3(0,0,0)` → `SetRotation`（`Object` 接 `GetOwner`）。
2. 选中 Cube，确认节点编辑器自动显示 `c7_ui` 树（空状态就用 `F3` 搜 `game_nodes.apply_logic_tree` 把它挂到 Cube）；`Ctrl+S` 存到 `$CK_DIR/scenes/c7_ui.blend`。
3. 在**任意编辑器**（3D Viewport、Node Editor 等）按 `F3`，搜 `Toggle Node Debug Trace`，回车（`poll()` 恒真，不必先切节点编辑器）。确认磁盘上出现 `$CK_DIR/scenes/.debug_trace.on`：

   ```bash
   ls -l "$CK_DIR/scenes/.debug_trace.on" | tee "$CK_DIR/logs/04_trace_on.log"
   ```

4. 终端启动 player（不要关闭编辑器，保持 `c7_ui` 树可见）：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/c7_ui.blend" 2>&1 | tee "$CK_DIR/logs/04_player.log"
   ```

5. 观察 3 秒：编辑器里 `OnUpdate`、`Vector3`、`SetRotation` 三个节点外框应为绿色（`debug_trace.py:110` 的颜色 `(0.15, 0.9, 0.25)`）。
6. 确认 trace 文件在持续更新：

   ```bash
   ls -l --time-style=full-iso "$CK_DIR/scenes/.debug_trace.json" | tee -a "$CK_DIR/logs/04_trace_json.log"
   ```

   连续看两次，间隔 1 秒，mtime 必须变化（写入节流 100 ms，见文件头说明 `debug_trace.py:1-9`）。
7. 热重载：把 `Vector3` 的 Z 分量从 `0` 改为 `1.5708`（90°），`Ctrl+S`，然后在节点编辑器里 `F3 → Hot Reload Game Graphs`（`gamenodes.hot_reload`）。观察 player 窗口。
8. 再改一次：Z 改成 `-1.5708`，`Ctrl+S`，再按一次 hot reload；确认方向反转。
9. 截图 `shots/04_green_boxes.png`（绿框）、`shots/04_reloaded.png`（旋转后的方块）。

### 4.4 预期结果

- 绿框：上述三个节点持续有绿色描边；未执行的节点没有描边。描边随执行帧更新，不残留超过 1 个 trace 周期（100 ms）。
- `.debug_trace.json` 在 player 运行期间每 ≤100 ms 更新一次；player 退出后不再更新。
- 热重载：执行后 1 帧内 Cube 旋转 90°；再次热重载后反向。player 不重启、不回黑屏。
- 编辑器状态栏出现 `Hot reload signalled: N graph(s), M mount(s), flag ...`（`scripts/startup/bge_gamenodes/ops/hot_reload.py:74`）。

### 4.5 失败判定

- 没有绿框：
  - 先查 `.debug_trace.on` 是否存在、`.debug_trace.json` 是否在更新；绿框只画已绑定到对象的树——选中 Cube，确认节点编辑器显示的是 `c7_ui` 而不是空状态，再看描边。
  - 绘制入口在 `scripts/startup/bge_gamenodes/ops/debug_trace.py:64-111`（`_draw_boxes`；没有 `.on` 时在 `:73` 直接 return）与 `:110`（颜色）；player 侧启用逻辑见 `source/gameengine/GameLogic/GameNodes/KX_GraphSceneManager.cpp:258-296`。
  - 存档 `04_player.log`、`04_trace_json.log`、`shots/04_no_green.png`。
- 热重载没反应：`.blend` 未保存时操作会直接拒绝（`scripts/startup/bge_gamenodes/ops/hot_reload.py:60` 的提示 “save the .blend first”）；flag 轮询在 `KX_GraphSceneManager.cpp:292-300`，mtime 未变化就不重建。记录 `04_player.log`。
- 热重载后方块回到原点：`SetRotation` 用的是局部旋转且 `OnUpdate` 每帧重设，属正常；若方向不变，检查是否改的 `Vector3` 的 Z 分量而不是 X/Y。

---

## 5. 鼠标拖拽连线

### 5.1 目的

验证节点编辑器拖线预览在缩放/平移视图时端点不漂移，松手能正确建立连接。对应域：编辑器交互（`rainbow_noodle` 覆盖层）。

### 5.2 前置条件

- 编辑器打开第 1 项的 `$CK_DIR/scenes/b67_input_nodes.blend`（已有可连线的输出/输入端口）。
- 需要真实鼠标（触控板也能做，但双击判定更快）。
- 无需 player 运行，纯编辑器验证。
- 仓库里的 `scripts/startup/bl_ui/rainbow_noodle.py.bak`、`.bak_*`、`.broken` 是**历史快照/坏版本留档，不是要安装的补丁文件**；没有任何脚本引用它们。节点编辑器用的就是 `rainbow_noodle.py` 本身，不要复制或改名这些文件。

### 5.3 操作步骤

1. 打开 `.blend`，进入 `c7_ui` 或 `b67_input_nodes` 树的节点编辑器。
2. 把视图缩放到 **50%**（滚轮向下或 `-` 键），截图 `shots/05_zoom50_before.png`。
3. 从一个输出端口（如 `GamepadAxis` 的 `X`）按下左键并**不松手**，向右下方拖动 5 cm。
4. 在拖拽过程中滚动滚轮把视图缩放到 **200%**、再 **400%**、再回到 **50%**，观察预览线末端。
5. 把预览线末端移到另一个节点的输入端口（如 `Print` 的数值输入），松手。
6. 重复 2–5 一次，这次改为拖拽中按住 `MMB` 平移视图，再松手到端口。
7. 重复一次“拖到空白处松手”，确认没有产生连线。
8. 每次操作后截图：`shots/05_zoom200_drag.png`、`shots/05_zoom400_drag.png`、`shots/05_linked.png`。

### 5.4 预期结果

- 拖拽中无论怎么缩放/平移，预览线末端始终贴住鼠标指针尖端，偏差目测不超过一个光标箭头宽度（修复点就是把端点按当前视图反算，见 `scripts/startup/bl_ui/rainbow_noodle.py:2330-2367`，调用点在 `:2797`）。
- 松手到有效输入端口时连线建立，端口高亮，节点树保存后依然存在。
- 松手到空白处不产生连线，也不弹出报错。

### 5.5 失败判定

- 缩放后预览线端点离开光标、出现“彩虹线漂移”：记录缩放级别并截图 `shots/05_drift_<zoom>.png`，对应代码 `rainbow_noodle.py:2330-2367`（`_drag_cursor_view_loc` 与 `cursor_location` 的过期问题）。
- 松手后连线落到相邻端口（吸附错位）：截图并记录当时缩放级别，吸附逻辑在 `rainbow_noodle.py:2801-2839` 一带（按 `snap_target`/`snap_dist` 搜索）。
- 拖拽过程中编辑器崩溃：保存 `$CK_DIR/logs/05_editor_crash.log`（把终端里启动编辑器的输出重定向到该文件再复现），截图崩溃前画面。

---

## 6. 载具

### 6.1 目的

验证 `CreateVehicle`（射线载具，四轮）与 `VehicleControl`（转向、油门、刹车）在真实 Bullet 物理下工作：车身落地、四个轮子被识别、油门前进、转向画弧。对应域：物理（载具子项）。

### 6.2 前置条件

- 场景：`$CK_DIR/scenes/vehicle_check.blend`（本项建立），包含：
  - 地面 Plane（Physics Type = `Static`，有碰撞）；
  - 车身 Cube（缩放 `2 × 0.5 × 1`，`Ctrl+A → Scale` 应用缩放，Physics Type = `Rigid Body`，Mass `1.0`）；
  - 四个轮子 Cylinder（半径 `0.35`、厚度 `0.2`，`Ctrl+A → Scale`，Physics Type = `No Collision`；若报无控制器，按 6.5 的备选改 `Rigid Body` 并清空碰撞组/掩码）；
  - **轮子不能是底盘 `Chassis` 的 compound child**：四个轮子必须是车身的同级对象；若已 parent，选中轮子 `Alt+P → Clear Parent`（保持世界变换）。否则永远报 `has no physics controller`，诊断会同时给出 `compound_child=1 parent='Chassis'`（`KX_PhysicsAdvancedAdapter.cpp:727`）；
  - 轮子对象名严格为 `Wheel_FL`、`Wheel_FR`、`Wheel_RL`、`Wheel_RR`。
- 环境变量：沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。
- 不需要手柄；用键盘或直接写死常量控制即可。

### 6.3 操作步骤

1. 按 6.2 建好场景与对象，所有对象应用缩放（`Ctrl+A → Scale`），轮子放在车身四角、轮心高度约 `0.35`。在 `Outliner` 里展开层级确认：`Wheel_FL/FR/RL/RR` 与车身 Cube **同级**，不是 Cube 的子项；有 parent 的先按 6.2 清掉。
2. 选中车身（活动对象），新建 Game Nodes 树 `vehicle_check`；`OnInit` → `CreateVehicle`：
   - `Chassis` 接 `GetOwner`（把树挂到车身上）；
   - 节点配置 `wheel_names = Wheel_FL,Wheel_FR,Wheel_RL,Wheel_RR`；
   - 节点配置 `suspension = 0.5,0.5,0.5,0.3`（四值一组；也接受每轮四值，见 `KX_PhysicsAdvancedAdapter.cpp:664-715` 的校验与应用分支）。
   - `OnSuccess` 接 `Print`（打印 `VEH_OK`）；`OnFail` 接 `Print`（打印 `VEH_FAIL`）。`CreateVehicle` 输出 `Vehicle` 句柄留作下一步。
3. 另建控制链：`OnUpdate` → `VehicleControl`：
   - `Vehicle` 接步骤 2 的句柄；
   - `Steering` 填 `0.0`（弧度）、`Throttle` 填 `0.4`、`Brake` 填 `0.0`。
   - 先只验证直线：`Steering=0`，`Throttle=0.4`。
4. 选中车身（活动对象），确认节点编辑器自动显示 `vehicle_check` 树（空状态就用 `F3` 搜 `game_nodes.apply_logic_tree` 把它挂到车身），`Ctrl+S` 存盘。
5. 启动 player：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/vehicle_check.blend" 2>&1 | tee "$CK_DIR/logs/06_vehicle.log"
   ```

6. 记录 5 秒直线运动：车身应沿车身局部 +Y 或 -Y 方向前进（方向取决于轮子朝向与轴顺序，以实际为准并记录）。
7. 把 `Steering` 改成 `0.4` 弧度（约 23°），再跑一次 5 秒；记录轨迹。
8. 把 `Steering` 改回 0、`Throttle` 改 0、`Brake` 改 `1.0`，再跑一次 3 秒；记录是否迅速停住。
9. 把 `Throttle` 改 `1.0`，观察最高速是否明显高于 0.4（引擎力按 `2000 × throttle` 施加，见 `KX_PhysicsAdvancedAdapter.cpp:731-737`）。
10. 截图/录屏：`shots/06_drive_straight.png`、`shots/06_steer_arc.png`（拍窗口与轨迹）。

### 6.4 预期结果

- 启动日志出现 `VEH_OK`，stderr 无 `wheel object ... has no physics controller`。
- 车身落到地面并稳定（不下陷、不抖动超过 1 秒）；四个轮子可见并跟随车身。
- 车身的**高度**判据：应贴地稳定，四轮接地；**不应整车被顶起/悬浮**。轮径取自轮子的碰撞形状真实半径（`GetShapeBoundingRadius`，`KX_PhysicsAdvancedAdapter.cpp:719`），悬挂 `rest_length = 0.5 × 半径`（`:806-818`），不再用 `Object.inertia` 当轮径（旧行为会让车上浮）。BOX 轮按包围球半径计算，判据是“没有被顶起来”，不要求外形像圆轮。
- `Throttle=0.4`：车身持续前进，速度随 `Throttle` 增大而增大；`Throttle=0` 且 `Brake=1` 时在 1–2 秒内停住（引擎力 `2000 × throttle`、刹车按轮施加，见 `KX_PhysicsAdvancedAdapter.cpp:732-736`）。
- `Steering=0.4`：轨迹为连续圆弧，车身绕垂直轴转向。注意本实现把转向角施加到**所有轮位**（四轮同时转，见 `KX_PhysicsAdvancedAdapter.cpp:734-738`），因此弧线比前轮转向更急，这是实现事实，不是失败。
- 转向角为弧度不是角度：`0.4` 约 23°，`1.57` 约 90°；超过 90° 时行为以实际为准，记录即可。

### 6.5 失败判定

- 报 `wheel object '<名字>' from wheel_names has no physics controller`：先看同一条错误里有没有 `compound_child=1 parent='Chassis'`——如果有，是轮子被 parent 到车身（compound child），不是控制器问题，按 6.2 清父级后重试；没有这行才把这些轮子改成 `Rigid Body` 并在 `Object → Physics → Collision` 里取消 `Collision Group/Mask` 的全部勾选。代码位置 `KX_PhysicsAdvancedAdapter.cpp:566-585`（诊断在 `:727`），日志 `06_vehicle.log`。
- 报 `wheel template object ... not found`（用了模板方式时）：模板空物体名字与配置不一致，代码 `KX_PhysicsAdvancedAdapter.cpp:595-607`。
- 车身原地打转或轮子不转：轮子轴顺序/朝向与轮位不匹配；记录 `wheel_names` 顺序与截图，参照 `KX_PhysicsAdvancedAdapter.cpp:565-585` 的取轮顺序（按名字列表顺序）。
- 车穿地/抖动：确认地面是 `Static`、车身缩放已应用、Mass 不是 0；保存日志与 `shots/06_tunnel.png`。
- 完全不动：确认 `CreateVehicle` 的 `OnSuccess` 已连到 `VehicleControl` 的 `OnEnter`（每帧都要触发），且 `Throttle > 0`。

---

## 7. 导航 Recast

### 7.1 目的

验证 `BuildNavMesh` 在运行时用 Recast 烘焙场景几何，`QueryPath` 能返回可行走路径，并验证 `agent_radius` 参数真的改变可通行性（0.3 vs 0.6）。对应域：物理/导航。

### 7.2 前置条件

- 场景：`$CK_DIR/scenes/navmesh_check.blend`（本项建立）：
  - 地面 Plane，Physics Type = `Navigation Mesh`（对应内部 `OB_NAVMESH`）；
  - 两堵平行墙形成 0.8 m 宽的通道（墙用 Cube，Physics Type = `Static`）；
  - 通道两端各有一个标记空物体 `Start`（通道外）和 `Goal`（通道另一端）。
- 环境变量：沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。
- 导航对象必须是**一个** `Navigation Mesh` 类型对象；烘焙入口会按场景查找该对象（`KX_PhysicsAdvancedAdapter.cpp:502-514`）。

### 7.3 操作步骤

1. 按 7.2 建场景：地面缩放 `20 × 20`，两墙间距 `0.8`，墙长 `10`、高 `2`，所有对象 `Ctrl+A → Scale`。
2. 选中地面（活动对象），新建 Game Nodes 树 `navmesh_check`；`OnInit` → `BuildNavMesh`：
   - 节点配置先填 `agent_radius = 0.3`、`agent_height = 1.8`；
   - `OnSuccess` 接 `Print("BUILD_OK")`，`OnFail` 接 `Print("BUILD_FAIL")`。
3. 把 `QueryPath` 接在 `BuildNavMesh` 的 `OnSuccess` 之后（保证烘焙完成再查询）：
   - `From` 用 `Vector3` 常量填 `Start` 的世界坐标（如 `(-6, 0, 0)`，Y 取地面高度 +0.1）；
   - `To` 用 `Vector3` 常量填 `Goal` 的世界坐标（如 `(6, 0, 0)`）；
   - `OnSuccess` 接 `Print("PATH_OK")`，`OnFail` 接 `Print("PATH_FAIL")`；`Waypoints` 可接 `Print`（若列表打印可用）或先不接。
4. 保持地面对象处于选中状态，确认节点编辑器自动显示 `navmesh_check` 树（空状态说明建树时活动对象不是它，用 `F3` 搜 `game_nodes.apply_logic_tree` 重挂）；`Ctrl+S` 存盘。
5. 以 `agent_radius=0.3` 启动 player：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/navmesh_check.blend" 2>&1 | tee "$CK_DIR/logs/07_nav_r030.log"
   ```

6. 停止 player；把 `agent_radius` 改成 `0.6`，`Ctrl+S`，再跑一次：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/navmesh_check.blend" 2>&1 | tee "$CK_DIR/logs/07_nav_r060.log"
   ```

7. 两次日志分别记录 `BUILD_*` 与 `PATH_*` 行；截图 `shots/07_nav_r030.png`、`shots/07_nav_r060.png`（拍窗口/关卡）。

### 7.4 预期结果

- 两次都出现 `BUILD_OK`；stderr 无 Recast 构建失败信息（失败会由适配器写入错误串，见 `KX_PhysicsAdvancedAdapter.cpp:1087-1110`）。
- `agent_radius=0.3`：`PATH_OK`——直径 0.6 m 的胶囊能穿过 0.8 m 宽的通道。
- `agent_radius=0.6`：`PATH_FAIL`——直径 1.2 m 的胶囊无法通过 0.8 m 通道，且没有绕行路线（通道两端封闭）。
- 两次运行没有崩溃、没有卡住主线程超过 1 帧（烘焙是同步的，单次应在百毫秒量级）。

### 7.5 失败判定

- 两次都 `PATH_OK`：`agent_radius` 没有传入烘焙。核对节点配置字段名与值，代码读取点在 `KX_FunctionNodesB6.cpp:339-345`，实际传入烘焙实现在 `KX_PhysicsAdvancedAdapter.cpp:1096-1107`。
- 两次都 `BUILD_FAIL`：场景里没有 `Navigation Mesh` 对象，或几何为空；查找逻辑 `KX_PhysicsAdvancedAdapter.cpp:502-514`。日志 `07_nav_r030.log`。
- 报错提到 recast 参数非法（如 `cell_size` 为 0）：记录完整错误串与配置截图；构建参数校验在 `KX_NavMeshBuilder.h:46-62`（`Params` 的默认值与含义）。
- `PATH_FAIL` 出现在 0.3 也在 0.6：先确认 `From/To` 确实在烘焙范围内（Y 高度不要埋进地面），再检查墙体是否挡住起点；日志与截图存档。

---

## 8. 骨骼动画

### 8.1 目的

验证 `PlayActionByLayer`（按层播放 `Spin`）、`BlendActions`（两动作按 factor 混合，0 = A，1 = B）、`SetBoneTransform`（写入的骨骼姿态能压过动作并被保留）与 `GetBoneTransform`（读回同一值）。对应域：动画。

### 8.2 前置条件

- 场景：`$CK_DIR/scenes/anim_check.blend`（本项建立）：
  - 一个带骨骼的角色（Armature + 蒙皮网格），骨骼里至少有一个名为 `Head` 的骨；
  - 动作 `Spin`：`0–60` 帧，让根骨绕 Z 轴转 360°；
  - 动作 `Still`：`0–60` 帧，只有静止关键帧（用于混合对照）。
- 环境变量：沿用第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。
- 动作必须作为 `bAction` 数据块存在于 `.blend` 内；名称严格为 `Spin`、`Still`（按名字查动作，见 `KX_GameAdaptersEngine.cpp:2172-2188`）。

### 8.3 操作步骤

1. 打开/导入角色，确认 `Head` 骨存在（`Pose Mode` 里能看到）；新建 `Spin` 动作（根骨 Z 旋转 0→360°，线性插值，帧范围 0–60），再新建 `Still`（只有一帧，无旋转）。
2. 选中 Armature（活动对象），新建 Game Nodes 树 `anim_check`；`OnInit` → `PlayActionByLayer`：
   - `Object` 接 `GetOwner`（树挂在 Armature 上）；
   - `Action` 填 `Spin`；`Layer=0`；`BlendIn=0.2`；
   - `OnDone` 接 `Print("PLAY_OK")`（该节点只有 `OnDone` 输出）。
3. 加第二条链 `OnInit` → `BlendActions`：
   - `Object` 接 `GetOwner`；`ActionA=Spin`；`ActionB=Still`；`Factor=0.0`；
   - 先只测 factor=0。
4. 加第三条链（验证骨骼覆盖）：`OnInit` → `SetBoneTransform`：
   - `Object` 接 `GetOwner`；`Bone` 填 `Head`；
   - `Position=(0,0,0)`；`Rotation=(0.5,0,0)`（弧度，XYZ 欧拉，见 `KX_GameAdaptersEngine.cpp:2064-2070`）。
5. 加第四条链（读回）：`OnUpdate` → `GetBoneTransform`（`Object=GetOwner`，`Bone=Head`）→ `Print`（打印 `Position`/`Rotation`）。为避免刷屏，可先只跑 2 秒然后关窗口。
6. 保持 Armature 处于选中状态，确认节点编辑器自动显示 `anim_check` 树（空状态说明建树时活动对象不是它，用 `F3` 搜 `game_nodes.apply_logic_tree` 重挂）；`Ctrl+S`。
7. 运行：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/anim_check.blend" 2>&1 | tee "$CK_DIR/logs/08_anim.log"
   ```

8. 记录 `Spin` 表现：角色整体/根骨在 2 秒（60 帧 @30fps）内完成一圈。
9. 把 `BlendActions.Factor` 改成 `1.0`，`Ctrl+S`，再跑一次；记录角色应基本静止（只由 `Still` 驱动）。
10. 把 `Factor` 改成 `0.5`，再跑一次；记录应该是“半圈/半速”或姿态介于两者之间。
11. 截图：`shots/08_spin.png`、`shots/08_blend_f1.png`。

### 8.4 预期结果

- `PlayActionByLayer`：`PLAY_OK` 打印，`Spin` 在 `BlendIn=0.2` 秒内淡入，然后持续旋转；60 帧后循环或停止（取决于动作是否循环，记录实际行为）。
- `BlendActions`：`Factor=0.0` 时可见 `Spin`；`Factor=1.0` 时可见 `Still`（几乎静止）；`0.5` 时介于两者之间。语义是 `(1-factor)·A + factor·B`（`KX_GameAdaptersEngine.cpp:2025-2034`）。
- `SetBoneTransform`：`Head` 骨被固定在 `0.5` 弧度（约 28.6°）的 X 轴倾斜上；`Spin` 继续转身体，但 `Head` 的倾斜不被动作覆盖（姿态覆盖在动作求值之后重放，见 `KX_GameAdaptersEngine.cpp:2071-2075`）。
- `GetBoneTransform`：日志里 `Rotation[0] ≈ 0.5`，且在多帧之间保持稳定（±0.01）。
- 不支持的相邻项：`SetBoneWeight`、`PlayBoneAction` 明确返回失败（顶点权重/单骨动作未暴露，`KX_GameAdaptersEngine.cpp:2079-2105`），本项不测，也不要把它们算作失败。

### 8.5 失败判定

- `PlayActionByLayer` 无反应且 stderr 打 `action 'Spin' has no frame range`：动作帧范围是 0–0 或名字不对（`KX_GameAdaptersEngine.cpp:1987-1993`）；日志 `08_anim.log`。
- `BlendActions` 报 `factor ... outside [0, 1]` 或 `both actions need a frame range`：分别见 `KX_GameAdaptersEngine.cpp:2015-2017`。
- `SetBoneTransform` 报 `object ... is not an armature` / `bone 'Head' not found`：树挂错了对象或骨名拼错，代码 `KX_GameAdaptersEngine.cpp:2045-2061`。
- `GetBoneTransform` 一直返回 0：`Head` 未被覆盖或读回路径未接 `OnUpdate`，读回实现在 `KX_GameAdaptersEngine.cpp:2157-2169`。
- 动作被覆盖后“整条角色僵硬”：`SetBoneTransform` 只写单骨，不会冻结全身；若全身静止，先查 `BlendActions.Factor` 是否被误设为 1。

---

## 9. 网络跨机

### 9.1 目的

验证两台同 LAN 机器之间：host 建房间、client 加入、join 事件到达 host、host 广播消息到达 client。对应域：网络（B6.9）。

### 9.2 前置条件

- 两台机器（Host、Client），同一 LAN，能互相 `ping`。
- 两台都跑同一份 `build_linux/bin/blenderplayer`（版本差异不在本项验证范围，但必须一致）。
- 场景：Host 用 `$CK_DIR/scenes/net_host.blend`，Client 用 `$CK_DIR/scenes/net_client.blend`（本项建立）。
- 端口：固定 `46312`（host 同时开 TCP 监听与 UDP 发现，见 `KX_NetworkAdapterEngine.cpp:488-504`）；两台机器防火墙都要放行 TCP+UDP `46312`。
- 环境变量：两台都先执行第 0 节的 export（`lib/dpcpp/lib` + `lib/shaderc/lib`）。

### 9.3 操作步骤

1. 先在**同一台机器**上做自环预检（先排除玩家代码问题，再排查网络）：

   ```bash
   cd /home/eridanusepsilon/Code/EBC/EBC5.3
   bash tests/n1_discover_two_proc.sh 2>&1 | tee -a "$CK_DIR/logs/09_n1_selfloop.log"
   bash tests/n2_state_sync_two_proc.sh 2>&1 | tee -a "$CK_DIR/logs/09_n2_selfloop.log"
   ```

   两个脚本各自输出 `RESULT ... verdict=PASS` 才算预检通过；脚本日志另存 `/tmp/n1_*`、`/tmp/n2_*`（脚本自身目录约定，勿改）。

2. Host 机器：新建工程，保留 Cube（重命名为 `HostCube`，保持它是活动对象）；选中 `HostCube` 后新建树 `net_host`：
   - `OnInit` → `HostRoom`：`Port=46312`，`MaxPlayers=4`；`OnSuccess` → `Print("HOST_READY")`，`OnFail` → `Print("HOST_FAIL")`；
   - `OnNetworkEvent`（节点配置 `event_type=1`，即 PlayerJoin）的 `OnPlayerJoin` → `SetPosition`（`Object=GetOwner`，`Value=(0,2,0)`），并同时 → `BroadcastMessage(Data="welcome-host")`。
3. Client 机器：新建工程，保留 Cube（重命名为 `ClientCube`，保持它是活动对象）；选中 `ClientCube` 后新建树 `net_client`：
   - `OnInit` → `JoinRoom`：`Host=<Host 的 IPv4>`，`Port=46312`；`OnSuccess` → `Print`（接 `LocalPlayerId`），并同时 → `BroadcastMessage(Data="hello-client")`；`OnFail` → `Print("JOIN_FAIL")`；
   - `OnNetworkEvent`（`event_type=3`，即 Message）的 `OnMessage` → `Print`（接 `Data`）。
4. 两台机器上各自选中本机的 Cube，确认节点编辑器自动显示对应的树（`net_host` / `net_client`；空状态就用 `F3` 搜 `game_nodes.apply_logic_tree` 重挂），再 `Ctrl+S`；Host 先存 `net_host.blend`，Client 存 `net_client.blend`。
5. 取 Host 的 IP：

   ```bash
   hostname -I | tee "$CK_DIR/logs/09_host_ip.log"
   ```

6. Host 先启动（保持终端）：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/net_host.blend" 2>&1 | tee "$CK_DIR/logs/09_host.log"
   ```

7. Client 后启动：

   ```bash
   build_linux/bin/blenderplayer \
     "$CK_DIR/scenes/net_client.blend" 2>&1 | tee "$CK_DIR/logs/09_client.log"
   ```

8. 观察 Host 窗口的 `HostCube` 是否在 Client 连接瞬间上移 2 m；两侧终端是否各自打印。
9. 保持连接 30 秒，确认没有掉线；截图 `shots/09_host_joined.png`、`shots/09_client_joined.png`。
10. 关闭 Client 窗口，观察 Host 是否收到 leave（可选；若节点未接 `OnPlayerLeave`，记录日志即可）。

### 9.4 预期结果

- Host 日志：`HOST_READY`；Client 连接后出现 `OnPlayerJoin` 链打印的 `hello-client`（Host 的 `OnMessage` 打出 Data）。
- Client 日志：`JOIN_FAIL` 不出现；`LocalPlayerId` 为 `>= 1`（host 自占 id 0，见 `KX_NetworkAdapterEngine.cpp:507-513`）；`OnMessage` 打出 `welcome-host`。
- Host 窗口：`HostCube` 在连接后上移 2 m，位置在 30 秒内不变（不抖动、不回弹）。
- 自环预检两个脚本均 `verdict=PASS`（说明网络层本身正常，剩下的问题只可能在防火墙或跨机 IP）。

### 9.5 失败判定

- `JoinRoom ... connect timed out after 300 ms` / `connect failed`：跨机不可达。先 `ping`、再查防火墙 TCP 46312、再查 Host IP 是否填错；错误串代码 `KX_NetworkAdapterEngine.cpp:566-591`，日志 `09_client.log`。
- `HostRoom: bind() failed on port 46312`：端口被占用或权限问题，代码 `KX_NetworkAdapterEngine.cpp:479-487`。
- Client 一直 `JOIN_FAIL` 但 Host 显示 `HOST_READY`：多半是发现/防火墙 UDP 46312 被挡；只用直连 Join 时 TCP 就够，若 TCP 通仍失败，抓 `ss -tnp | grep 46312` 结果存档。
- Host 收到 join 但 Client 没收到 `welcome-host`：核对 `BroadcastMessage` 的 `Data` 是否非空、`OnNetworkEvent` 的过滤值是否为 3（`KX_FunctionNodesB6Net.cpp:185-196`），日志两侧都存档。
- 自环预检就失败：先修网络层，不要继续跨机测试；记录 `/tmp/n1_*`、`/tmp/n2_*` 与两份 `09_*_selfloop.log`。

---

## 10. 传感器 shape overlap：已由 headless 测试覆盖，无需实机验证

### 10.1 目的

`AreaSensor` 对**长条物体**必须按真实碰撞形状判定占用，而不是只看物体原点是否落在盒子内：长杆一端插入区域、原点留在区域外时仍应算占用。对应域：物理传感器。

**本项不需要实机验证。** 它只依赖 Bullet 窄相检测，不依赖显示设备、手柄或网卡，headless 已闭环；不要为了这一项去找长条物体场景。

### 10.2 覆盖方式（替代实机前置条件）

- 节点契约层：`tests/c6_shape_overlap.cpp`。用精确 box/sphere 参考后端驱动同一套节点契约：长杆（原点距 3.0）形状重叠 → 命中；细杆（原点距 0.5、几何错开）→ 不命中；旧的原点规则在同一场景会误报。
- 真实 Bullet 层：`tests/c6_shape_overlap_bullet.cpp`。直接构造 `btCollisionWorld` + `CcdPhysicsEnvironment`，调用 `TestShapeContact` / `TestControllerContact`：长杆重叠报接触、细杆错开不报接触，结论与参考后端一致。
- 不需要 `.blend`、不需要 player 窗口、不需要 `LD_LIBRARY_PATH`。

### 10.3 操作步骤（仅在需要复现 headless 测试时执行）

1. 在 `/home/eridanusepsilon/Code/EBC/EBC5.3` 打开终端；先按第 0 节 0.3 初始化 `$CK_DIR`（这一步只用来存日志，不建场景）。
2. 同时跑两个用例（`--only` 是名字/标签正则，会匹配到 `c6_shape_overlap` 与 `c6_shape_overlap_bullet` 两个批次）：

   ```bash
   tests/run_all_e2e.sh --only 'c6_shape_overlap' 2>&1 \
     | tee "$CK_DIR/logs/10_shape_overlap_headless.log"
   ```

3. 取证据：批次日志在 `tests/.e2e_out/run_<RUN_ID>/` 下，按批次名 `c6_shape_overlap`、`c6_shape_overlap_bullet` 找。

不要为这一项改 `tests/run_all_e2e.sh` / `tests/harness_config.sh`；断言数量以测试文件本身与 `docs/PROJECT_STATUS.md` 为准，本文不抄数字。

### 10.4 预期结果

- 两个批次都构建成功、退出码 0，输出中没有 `FAIL` 行。
- `c6_shape_overlap`：长杆用例判“命中”，细杆用例判“不命中”。
- `c6_shape_overlap_bullet`：同一组长杆/细杆用例的接触判定与参考后端一致（两者只有一个通过，说明只改了阈值、没有真正测形状）。
- 本项不要求截图；若你另外做了目测，把观察写进备注列，不作为通过依据。

### 10.5 失败判定

- `c6_shape_overlap` 出现 `FAIL`：节点契约层退化。对照形状重叠判定分支 `KX_FunctionNodesB6Sensor.cpp:343-350` 与查询入口 `KX_GameAdaptersEngine.cpp:725-800`；日志 `10_shape_overlap_headless.log`。
- `c6_shape_overlap_bullet` 出现 `FAIL`：真实 Bullet 窄相与参考后端不一致。只读核对 `tests/harness_config.sh:383-408` 中该批次额外的 flags/libs 是否按预期传入，不要改脚本。
- 两个批次一过一挂：形状重叠只在其中一层成立；把两份批次日志都存档，按测试用例名定位，不要用实机场景去“补证”。
- 出现 `shape overlap query exceeded ... using origin distance`：对象数超过预算、查询退化（提示代码 `KX_FunctionNodesB6Sensor.cpp:75-85`）；headless 用例正常时不应出现这行。

---

## 11. 触摸输入：不支持，无需验证

**此项不支持，不要去插触摸设备，也不要在清单里找触摸屏。**

- 引擎输入层没有 touch 设备 API：`GetTouch` 恒返回 `x=0, y=0, down=false`，`GetTouchCount` 恒返回 `0`；实现位置 `source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp:473-485`。
- 依赖它的节点（`TouchInput`、`TouchGet`、`TouchGesture`）仍会注册，但只会输出空值；节点包装见 `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Input.cpp:224-292`。
- 这些节点已在 `docs/DEFERRED_FOREVER.md` 登记为不支持，不参与能力百分比，也不列入实机验证。
- 若你在触摸屏上看到鼠标点击能驱动 `Mouse`/`KeyCombo` 节点，那是鼠标事件，不是触摸事件；不要据此把触摸记为“通过”。

---

## 12. 砖块编辑器（Logic Editor）已移除：操作影响

清理线已正式回报并实测通过，本节按最终状态写；不需要再等任何回报。

### 12.1 目的

确认砖块编辑器清理后的用户可见状态，避免把旧 `.blend` 的区域回退、或 `--background` 下读到空串误判成崩溃。已确认的清理内容：摘掉 `space_logic` 的空间类型注册与 UI 模块注册，删除 `space_logic.cc`、`logic_window.cc`、`logic_buttons.cc`、`logic_intern.hh`，`logic_ops.cc` 原地保留并继续编译（`source/blender/editors/space_logic/CMakeLists.txt:32-34`）；`scripts/presets/keyconfig/blender_default.py` 的 `km_logic()` 与注册调用已删除（不删会在启动期由 `KeyMaps.new` 抛 `TypeError`）。对应域：编辑器 UI，不是某个节点。

### 12.2 前置条件

- 构建产物与当前源码树一致（重新编译过）；未重新编译的旧产物仍可能带入该入口。
- 一个含 `LOGIC_EDITOR` 区域的旧 `.blend`：用清理前的二进制在 GUI 里存一份即可；没有就跳过 12.3 的步骤 4–5，只做菜单与键位检查。
- 无需硬件；`--background` 检查只用终端。

### 12.3 操作步骤

1. 在任意面板左上角的编辑器类型下拉里逐项找 `Logic Editor` / «Logic Bricks Editor»：**不应存在**（实测 `LOGIC_IN_EDITOR_TYPE_MENU = False`）。
2. `Edit → Preferences → Themes → Area`（区域主题下拉）里同样不应有 `LOGIC_EDITOR` / «Logic Bricks Editor»（实测 `LOGIC_IN_THEME_AREA_MENU = False`）。
3. 确认键位表加载不报错：

   ```bash
   build_linux/bin/blender --background \
     --python-expr "import bpy; print('KM_OK', len(bpy.context.window_manager.keyconfigs.default.keymaps))" \
     2>&1 | tee "$CK_DIR/logs/12_keymaps.log"
   grep -c "KeyMaps.new" "$CK_DIR/logs/12_keymaps.log"
   ```

   第二条命令必须输出 `0`；第一条应打印 `KM_OK` 和一个非 0 的键位表长度（3D View 与 Outliner 的键位都在）。
4. GUI 打开那个旧 `.blend`（含 `LOGIC_EDITOR` 区域）：该区域应显示为 3D Viewport 并正常工作，退出码 0、无 assert（回退分支 `source/blender/editors/screen/area.cc:2355-2362`，`area_init_type_fallback(area, SPACE_VIEW3D)`）。
5. 用 `--background` 打开同一个旧 `.blend`：该区域的类型会读成空串（后台模式不初始化 screen）。**这是正常现象，不是崩溃，不要判失败。**
6. 把“两处菜单是否干净、`--background` 是否为空串”记到总检查表第 0 项的备注列。

### 12.4 预期结果

- 编辑器类型下拉与 `Preferences → Themes → Area` 两处都找不到 `Logic Editor` / «Logic Bricks Editor»。
- 第 3 步 `grep -c "KeyMaps.new"` 为 `0`，启动日志无 `TypeError`，默认键位表完整（3D View、Outliner 键位都在）。
- GUI 打开旧 `.blend`：原区域变成 `VIEW_3D` 并拿到 `SpaceView3D`，exit 0，无 assert。
- `--background` 打开旧 `.blend`：该区域类型为空串（`''`），进程 exit 0——正常，不是崩溃。
- 仅存的残留是 RNA 主题结构里一个不可达的 `logic_editor` 指针（`source/blender/makesrna/intern/rna_userdef.cc:4643`），没有任何 UI 入口，用户看不到。
- 建树（选中对象 → 面板上点 `New`）、`Ctrl+S`、player 运行照常（第 0 节步骤 7–8 通过即证明）。
- 附带说明：`bge_bricknodes` 仍是独立 addon，不随本次清理移除，属单列项，不影响本指南任何步骤。

### 12.5 失败判定

- 任一处菜单仍能看到 `Logic Editor` / «Logic Bricks Editor»：旧构建；重新编译后再验。若新构建仍有，按失败记录并写明构建时间。
- 启动日志出现 `TypeError` 且提到 `KeyMaps.new`：`km_logic()` 的清理不完整（`scripts/presets/keyconfig/blender_default.py`）；日志 `12_keymaps.log`。
- GUI 打开旧 `.blend` 报错、assert 或崩溃，而不是回退成 3D 视图：真失败。保存 `$CK_DIR/logs/12_open_legacy.log` 与截图，对照 `area.cc:2355-2362` 的回退分支。
- `--background` 下进程崩溃或 assert（注意：读到空串不算）：真失败，日志 `12_open_legacy.log`。
- 本指南任何步骤里再出现“在 Logic Editor 里……”：一律视为文档错误，改走 Node Editor（当前正文已无此类步骤）。

---

## 附录 A：既有已修复问题（保留记录）

以下问题曾经在真机/编辑器上复现，已修复；本指南的对应项就是回归入口，不需要重复修。

1. **拖线预览在缩放后漂移**：预览端点改用当前视图反算，修复在 `scripts/startup/bl_ui/rainbow_noodle.py:2330-2367`，调用点在 `:2797`。回归项：第 5 项。
2. **节点属性改动后 player 侧不生效**：编辑器更新回调已补齐，改完节点配置必须保存 `.blend` 再触发 hot reload。回归项：第 4 项。
3. **热重载绿框不出现**：根因是 player 没有写 `.debug_trace.json` 或编辑器没有 `.debug_trace.on`；当前链路为“编辑器写 flag → player 每 100 ms 写 trace → 编辑器绘制”，实现见 `scripts/startup/bge_gamenodes/ops/debug_trace.py:1-9`（文件通道说明）、`:64-111`（`_draw_boxes` 绘制入口）、`:110`（绿色描边颜色）。回归项：第 4 项。

## 附录 B：已知问题

1. **player 关闭时可能以退出码 -11 结束**。运行期行为正常，但会让 `set -e` 的脚本在收尾时失败；用 `... || true` 包住启动命令，并在日志里记录退出码。若本轮复现，把退出码写进对应项的备注列，不要因此把该项判失败。
2. **富文本/排版特性不在支持范围**：混合 CJK/Latin 排版规则未实现（`source/gameengine/GameLogic/GameNodes/KX_UIRenderer.cpp:809-813`）。中文能显示不等于排版完全正确；本指南不对排版做判据。
3. **轮子必须带物理控制器**：`CreateVehicle` 的 `wheel_names`/模板轮子若没有控制器会直接返回错误（`KX_PhysicsAdvancedAdapter.cpp:566-585`），这是实现约束，不是场景搭错就一定失败——按第 6 项 6.5 的备选做法可解。

## 附录 C：记录与归档约定

- 每次验证一个独立 `RUN_ID`，目录为 `real_device_check/<RUN_ID>/`；不要复用旧目录，不要覆盖旧日志。
- 日志文件命名 `NN_<项>.log`，截图 `NN_<动作>.png`，与本指南编号一致。
- 通过/失败只写进下面的总检查表，不在日志里改判据；判据以本文“预期结果”为准。
- 失败时至少保留：日志、截图、当时的节点配置截图（节点本体上的配置字段）、以及对应的 `文件:行号`。

---

## 总检查表

| 项 | 结果（通过/失败/跳过） | 日期 | 备注 |
|----|------------------------|------|------|
| 0 环境准备 |  |  |  |
| 1 手柄输入 |  |  |  |
| 2 手柄震动 |  |  |  |
| 3 3D 音效 | 部分通过 | 2026-11-07 | 节点调度与 3D 坐标计算通过；独立 `blenderplayer` 资源加载失败（`not found`）阻断听感验证，根因未确认，见 `KNOWN_LIMITATIONS.md` #31 |
| 4 热重载绿框 |  |  |  |
| 5 鼠标拖拽连线 |  |  |  |
| 6 载具 |  |  |  |
| 7 导航 Recast（agent_radius 0.3/0.6） |  |  |  |
| 8 骨骼动画（Spin / BlendActions / SetBoneTransform） |  |  |  |
| 9 网络跨机 |  |  |  |
| 10 传感器 shape overlap | N/A（headless 已覆盖） |  | `tests/c6_shape_overlap*.cpp` |
| 11 触摸输入 | 跳过（不支持，无需验证） |  | 见 `docs/DEFERRED_FOREVER.md` |

勾完这张表即算本轮实机验证完成；未验证项写“跳过”并在备注里写原因，不参与代码侧百分比。
