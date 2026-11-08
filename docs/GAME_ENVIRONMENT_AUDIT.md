# 游戏环境只读审计（GAME_ENVIRONMENT_AUDIT）

- 仓库：`/home/eridanusepsilon/Code/EBC/EBC5.3`
- 日期：2026-11-06
- 模式：**只读**。未改源码、未删文件、未 commit/push、未进入 `build_linux`、未触碰 7 个未跟踪的 `_lib.glsl`、未持有构建锁。
- 范围：Logic Bricks（`SCA_*`）运行时、`space_logic` 编辑器、原生逻辑节点系统依赖、Bricky Nodes 插件、`bge.logic` API；物理/渲染/音频/输入仅做快速评估。
- 方法：以 include、调用点、继承关系、CMake、RNA/DNA 注册为准，逐处上下文确认。**未使用 `grep -v "//"` 式的匹配计数作为结论**（块注释、字符串、宏都会漏判）。

> **B7.5 形态变更（2026-11-06 之后）**：包已迁至 `scripts/startup/bge_gamenodes`，**随引擎启动加载、不可禁用**；`AddonPreferences` 已删除，偏好改为引擎原生 `bpy.context.preferences.game_nodes`（面板 `USERPREF_PT_game_gamenodes`，在 **Preferences → Game**）；Python 逻辑执行器（`tree_code_generator` 等 6 个文件）与「原生运行时」偏好开关均已删除，manifest 导出**恒执行**。本文第 1 节与第 7 节保留的是 B7.5 **之前**的只读审计快照，其中「插件 / 需要启用 / 默认 False / Python 兜底」等描述均已被 B7.5 取代，仅作历史取证；数值未变。

---

## 0. 结论先行

最终结论（二选一选定）：

> **逻辑砖块运行时保留（逻辑节点依赖），砖块编辑器移除。**

三张清单条目数：**A = 4 条（其中 3 条同属一个编辑器移除簇）；B = 6 条；C = 5 条。**

最关键的一句判断：`KX_GameObject` 直接继承 `SCA_IObject`、`KX_Scene` 直接继承 `SCA_IScene`，且原生节点的输入适配器直接从 `SCA_KeyboardManager`/`SCA_MouseManager` 取设备状态——**砖块运行时的基类与调度器就是节点系统的底座，整体删除会先炸掉引擎本身**。

---

## 1. 两个前提的校正

### 1.1 「0.36 之后逻辑节点改为 Python 组件注册」不能直接套用

本树是**双轨并存**，不是单轨替换：

| 轨道 | 证据 | 状态 |
|---|---|---|
| 原生 C++ 节点运行时 | `KX_GraphSceneManager` / `KX_CompiledGraph` / `KX_ExecutionContext` | 代码完整，由 `KX_Scene::GraphTick` 驱动（`KX_Scene.cpp:2490-2517`） |
| Python 组件执行器 | `tree_code_generator.py:110` 生成 `bge.logic.getCurrentScene()`、`:134` 继承 `bge.types.KX_PythonComponent` | **B7 前是默认路径；B7.5 已删除该执行器（6 文件），原生运行时恒执行** |

关键闸门（**B7 前历史**，以下机制已随 B7.5 全部删除）：

- 旧偏好项 `use_native_graph_runtime` 默认 `False`（`scripts/startup/bge_gamenodes/preferences.py:32-36`，该文件与 `AddonPreferences` 已删除）。
- 关闭时 `write_scene_manifest()` 会删除场景清单属性，原生路径保持严格 no-op（`generator/ir_export.py:479-492`）；B7.5 后导出恒执行，不再有该分支。
- 游戏启动时若未开启原生运行时，则生成 Python 模块并写清单（`__init__.py:86-98`）；B7.5 后 Python 执行器已删除。
- `KX_Scene.cpp:2511-2512` 注释亦明确：图编译失败只告警，**Python 执行器继续兜底**（B7.5 后无 Python 兜底，编译失败即节点不执行）。

因此：「逻辑节点已经全部原生化」在 B7 前**不成立**；B7.5 后 Python 执行器删除、原生运行时恒执行，**该结论已反转**——原生 C++ 运行时是唯一执行路径。

### 1.2 「逻辑砖块在性能上比逻辑节点更快」

**仓库内无砖块 vs 节点的性能测量数据**（`tests/`、`docs/` 未见对照 benchmark）。此处只给结构推断，不编造结论：

| 路径 | 每帧结构 | 证据 |
|---|---|---|
| 砖块 | 事件管理器逐个 `UpdateFrame()` → 活跃执行器链表遍历；传感器/控制器为虚调用 | `SCA_LogicManager.cpp:155-167` |
| 原生节点 | 已编译的 `KX_ExecutionPlan`：先跑纯值节点数组，再按触发队列传播；无逐帧 IR 解释 | `KX_ExecutionContext.cpp:210-298`；编译期 `KX_CompiledGraph.cpp:115-150`；`KX_InlinePass.h` |

结论：两者都是「虚调用 + 遍历」，**不存在「节点每帧解释 IR、砖块直接 C++ 调用」这类量级差异**；谁更快必须实测，当前**无数据**。

---

## 2. 四项必查事项

### 2.1 砖块运行时（`SCA_*`）除砖块自身路径外还被谁引用

| 引用点 | 文件:行号 | 性质 |
|---|---|---|
| `KX_GameObject` 继承 `SCA_IObject` | `source/gameengine/Ketsji/KX_GameObject.h:83` | 核心引擎对象的基类 |
| `KX_Scene` 继承 `SCA_IScene` | `source/gameengine/Ketsji/KX_Scene.h:105` | 场景的基类 |
| 场景构造时创建 `SCA_LogicManager` | `source/gameengine/Ketsji/KX_Scene.cpp:214` | 砖块调度器是所有场景的常驻成员 |
| 场景构造时创建键盘/鼠标/摇杆/时间/执行器事件管理器 | `KX_Scene.cpp:216-230` | 节点输入适配器依赖这些管理器 |
| 键盘/鼠标管理器公开 getter | `KX_Scene.cpp:1619-1629`、`KX_Scene.h:481-482` | 节点输入适配器入口 |
| 每帧砖块逻辑调度 | `KX_Scene.cpp:2444`（BeginFrame）、`:2483`（UpdateFrame）、`:2592`（EndFrame） | 即使没有砖块也每帧执行 |
| `KX_GameObject` 的 Python 方法查逻辑管理器 | `KX_GameObject.cpp:2458, 2494, 2538, 2807, 4356, 4586, 4606` |  `sendMessage` 等公共 API |
| 碰撞回调把传感器 `static_cast` 为 `SCA_CollisionSensor` | `KX_CollisionEventManager.cpp:118, 129, 151, 163, 175` | 碰撞回调实现就长在砖块传感器类上 |
| 转换器实例化各类砖块 | `BL_ConvertSensors.cpp:123-547`、`BL_ConvertControllers.cpp:95-128`、`BL_ConvertActuators.cpp:162-330` | `.blend` 内容加载 |
| Python 类型注册 | `KX_PythonInitTypes.cpp:212-300` | `bge.types.SCA_*` 公开 API |
| 原生节点适配器 | 见 2.3 | 直接依赖 |

补充说明：单个 `SCA_*Sensor/Controller/Actuator` 类**没有**被原生节点的 C++ 代码 `new`，但它们被转换器、Python API、事件回调引用，因此**不能按「无引用」归入可删清单**。

### 2.2 `source/blender/editors/space_logic/` 到底画什么

文件清单（6 个）：`CMakeLists.txt`、`space_logic.cc`、`logic_buttons.cc`、`logic_window.cc`、`logic_ops.cc`、`logic_intern.hh`。

- 区域类型：`SPACE_LOGIC`（`space_logic.cc:99`）。
- 绘制内容：传感器/控制器/执行器面板——`logic_window.cc` 中传感器面板（约 `:1099`）、控制器面板（约 `:1526`）、执行器面板（约 `:1644`）；侧栏由 `logic_buttons.cc` 提供。
- 注册入口链：
  - `ED_spacetype_logic()` 定义：`space_logic.cc:332`；`spaceid = SPACE_LOGIC` 在 `:337`；
  - 调用：`source/blender/editors/space_api/spacetypes.cc:84`；
  - 声明：`source/blender/editors/include/ED_space_api.hh:41`；
  - RNA：`rna_space.cc:144`（编辑器名）、`:830-831`（`RNA_SpaceLogicEditor`）、`:9324/9329`（定义）、`:9949`（注册）；
  - DNA：`DNA_space_types.h:928`（`SpaceLogic`）、`DNA_space_enums.h:1201`（`SPACE_LOGIC = 17`）；
  - 键位：`space_logic.cc:238/272`（"Logic Bricks Editor"）。
- **是否承载 Game Nodes 面板（原 Logic Nodes）：否。** 对整个目录检索 `node/Node/NODE` 为 0 命中；逻辑节点 UI 在 Node Editor 内，由 `bge_gamenodes` 提供（`LogicNodeTree`，`editor/nodetree.py:27` 等），与此编辑器无注册关系。

**但有一条隐藏的非编辑器依赖（必须点明）**：`logic_ops.cc` 不只注册砖块增删算子，还注册了节点系统要用的 Python 组件算子：

- `LOGIC_OT_python_component_register` / `_remove`：`logic_ops.cc:1094-1104, 1168-1178`，注册于 `:1386-1404`；
- 节点编辑器调用：`bge_gamenodes/editor/nodetree.py:63, 70`；
- 物体属性面板调用：`scripts/startup/bl_ui/properties_game.py:96-98, 108, 138-141`。

所以 **`source/blender/editors/space_logic/` 不能整目录删除**，必须先拆出 `logic_ops.cc` 的 Python 组件算子。

### 2.3 原生节点系统是否依赖砖块运行时（最关键，依赖成立）

证据链（按强度排序）：

1. **基类依赖**：`KX_GameObject : public SCA_IObject`（`KX_GameObject.h:83`）；`KX_Scene : public KX_PythonProxy, public SCA_IScene`（`KX_Scene.h:105`）。节点系统以 `KX_GameObject`/`KX_Scene` 为操作对象，砖块基类无法剥离。
2. **输入适配器直接读砖块设备管理器**：`KX_GameAdaptersEngine.cpp:244-249` 从 `scene->GetKeyboardManager()` / `GetMouseManager()` 取 `keyboard_`/`mouse_`；`:270-302` 读 `SCA_IInputDevice`/`SCA_InputEvent`；`:176-184` 从 `SCA_LogicManager::FindEventManager(JOY_EVENTMGR)` 取 `SCA_JoystickManager`。
3. **消息/动作适配器走 `SCA_LogicManager`**：`:2177` 通过 `object->GetScene()->GetLogicManager()`。
4. **对象类型枚举即砖块基类枚举**：`:1150, 1251, 1862, 2045` 使用 `SCA_IObject::OBJ_CAMERA/TEXT/ARMATURE`。
5. **帧序**：`LogicBeginFrame` → `LogicUpdateFrame` → `GraphTick`（`KX_KetsjiEngine.cpp:552, 563, 591`），节点图在砖块调度之后运行。
6. **适配器装配点**：`KX_GraphSceneManager.cpp:57` 调用 `KX_CreateEngineAdapters(scene)`（定义见 `KX_GameAdaptersEngine.cpp:2976`），因此所有引擎适配器都绑定在带砖块调度器的场景上。
7. **发布链也依赖砖块**：节点插件的播放器生成器创建 Always 传感器 + Python 控制器 + Game 执行器（`utilities/make_launcher.py:46-55`），启动脚本调用 `bge.logic.getCurrentController().activate('launch')`（`:18`）。

**判定：依赖成立。** 砖块运行时是节点系统的底座；只有编辑器 UI 是可拆的。

### 2.4 Bricky Nodes 插件残留 + `bge.logic` API 归属

**Bricky Nodes（`bge_bricknodes`）现状：**

- 插件存在且**默认强制启用**：`bl_info` 自述 "A node based view for logic bricks"（`scripts/addons_core/bge_bricknodes/__init__.py:6-15`）；强制启用点 `blendfile.cc:1538`、`versioning_userdef.cc:1703`。
- 它注册独立 NodeTree `BGEBrickTree`（`ui.py:120-121`），在 Node Editor 内工作，**不依赖 `space_logic`**；但依赖 `logic_ops.cc` 的砖块增删算子（`nodes.py:6-29`）。
- 与 `bge_gamenodes` 无调用关系（后者是逻辑节点系统本体，同样强制启用：`blendfile.cc:1537`、`versioning_userdef.cc:1702`）。
- 「0.33 曾关闭砖块节点视图」这一历史说法在本仓库**无法证实**；现状是它默认开启（未证实）。

**`bge.logic` API 归属：**

| API | 定义/实现 | 归属判定 |
|---|---|---|
| `expandPath` | `KX_PythonInit.cpp:783`（实现 `:211-221`） | 通用（路径工具），Python 用户与插件共用 |
| `sendMessage` | `KX_PythonInit.cpp:795`（实现 `:290-320`） | 通用（网络消息），走 `KX_NetworkMessageScene`（`KX_Scene.cpp:232`） |
| `getCurrentController` | `KX_PythonInit.cpp:796` | **砖块独占**（返回 `SCA_PythonController`），但公开给 Python/插件（`make_launcher.py:18`） |
| `getCurrentScene` | `KX_PythonInit.cpp:800` | 通用（节点默认执行器生成代码使用：`tree_code_generator.py:110`） |
| 其余 `game_methods` | `KX_PythonInit.cpp:782-843` | 通用 Python API |

结论：`expandPath`/`sendMessage` **不是**砖块独占；真正砖块独占的只有 `getCurrentController`，但它同时被节点插件的发布器使用，因此不能随编辑器一起删。

---

## 3. 三张分类清单

### A. 可直接移除

| 编号 | 对象 | 判定依据 | 移除后断什么（风险） |
|---|---|---|---|
| A-1 | `space_logic` 编辑器 UI：`space_logic.cc`、`logic_window.cc`、`logic_buttons.cc`、`logic_intern.hh`、`CMakeLists.txt`（5 文件） | 目录内 0 处节点引用；面板只画传感器/控制器/执行器；不承载 Logic Nodes（2.2） | **不能整目录删**：`logic_ops.cc` 须先拆出（见 C-1）。删 UI 后需处理 `.blend` 中 `SPACE_LOGIC(17)` 区域的读回退（`DNA_space_enums.h:1201`），否则旧文件区域类型失效 |
| A-2 | `SPACE_LOGIC` 注册/RNA/DNA/主题/键位/过滤等 15 处管线 | 仅为该编辑器存在：`spacetypes.cc:84`、`ED_space_api.hh:41`、`rna_space.cc:144/830-831/9324/9949`、`DNA_space_types.h:928`、`DNA_space_enums.h:1201`、`wm_draw.cc:610`、`resources.cc:174`、`screen_ops.cc:550`、`context.cc:1108`、`bpy_rna_callback.cc:236`、`interface_template_search_menu.cc:650`、`grease_pencil_convert_legacy.cc:3040`、`anim_filter.cc:396`、`versioning_280.cc:2398/2410`、`blender_default.py:2464`、`rna_userdef.cc:4530/4648` | 移除 `bpy.types.SpaceLogicEditor` 会破坏引用该 RNA 类的脚本；键位/主题中会留下悬空条目；`versioning_280.cc` 是旧文件兼容，需保留或改写为映射到其他编辑器 |
| A-3 | `scripts/startup/bl_ui/space_logic.py` + `bl_ui/__init__.py:82` | 面板 `LOGIC_PT_properties` / 菜单 `LOGIC_MT_logicbricks_add` 的 `bl_space_type='LOGIC_EDITOR'`（`space_logic.py:23-24, 84, 96`） | 启动脚本导入失败需同步删除注册项 |
| A-4 | `scripts/addons_core/bge_bricknodes/`（8 个文件，4 个 `.py`）+ 强制启用两行 `blendfile.cc:1538`、`versioning_userdef.cc:1703` | 独立 `BGEBrickTree`，不依赖节点系统；只依赖 `logic_ops` 砖块算子 | 用户已保存的 `BGEBrickTree` 节点组会变成无类型数据；偏好文件中的 addon 键需清理 |

> 统计口径：A-1/A-2/A-3 同属**一个编辑器移除簇**，A-4 是独立插件簇，故记 **4 条 / 2 个簇**。

**A 清单的前置硬约束**：`logic_ops.cc` **不得**随 A-1 删除。它承载节点系统与物体属性面板使用的 `logic.python_component_*` 算子（见 2.2），必须先迁移（C-1）。

### B. 必须保留

| 编号 | 对象 | 判定依据 |
|---|---|---|
| B-1 | `SCA_IObject` / `SCA_IScene` / `SCA_ILogicBrick` 基类层 | `KX_GameObject.h:83`、`KX_Scene.h:105`；节点系统所有对象操作都建立在其上 |
| B-2 | `SCA_LogicManager`、`SCA_EventManager` 家族、`SCA_KeyboardManager`、`SCA_MouseManager`、`SCA_JoystickManager`、`SCA_IInputDevice`、`SCA_InputEvent` | 场景常驻（`KX_Scene.cpp:214-230`）；节点输入/消息适配器直接调用（`KX_GameAdaptersEngine.cpp:176-184, 244-249, 2177`）；每帧调度（`KX_Scene.cpp:2444/2483/2592`） |
| B-3 | 各 `SCA_*Sensor` / `SCA_*Controller` / `SCA_*Actuator` 个体类 | Python API 注册（`KX_PythonInitTypes.cpp:212-300`）；`.blend` 加载（`BL_ConvertSensors.cpp:123-547`、`BL_ConvertControllers.cpp:95-128`、`BL_ConvertActuators.cpp:162-330`）；碰撞回调（`KX_CollisionEventManager.cpp:118-175`）。**可清理无引用的废弃分支，不可删类** |
| B-4 | `bge.logic` 模块 API（`expandPath`、`sendMessage`、`getCurrentScene`、`getCurrentController` 等） | `KX_PythonInit.cpp:782-843`；Python 用户依赖；节点默认执行器生成代码调用 `getCurrentScene`（`tree_code_generator.py:110`）；发布器调用 `getCurrentController`（`make_launcher.py:18`） |
| B-5 | `KX_GameObject`/`KX_Scene` 中经逻辑管理器的公共方法（`sendMessage`、`collide`、`EndObject` 等）及 `KX_NetworkMessageScene` | `KX_GameObject.cpp:2458-4606`；`KX_Scene.cpp:232`；与 `bge.logic.sendMessage` 同源 |
| B-6 | `BL_ConvertSensors/Controllers/Actuators/Properties` 转换路径 | 砖块内容加载与 Python 组件属性依赖；删掉会让旧 `.blend` 丢失逻辑数据 |

### C. 该改不该删

| 编号 | 对象 | 现状问题 | 建议 |
|---|---|---|---|
| C-1 | `logic_ops.cc` | 砖块编辑器算子与节点系统的 Python 组件算子混在同一文件（`logic_ops.cc:1386-1404` 同时注册两类） | **先拆分**：把 `python_component_*` / `custom_object_*` 迁到中立的编辑器模块（或物品属性相关模块），再移除砖块编辑器算子 |
| C-2 | `bge_gamenodes` 双执行路径（**B7 前历史**） | Python 执行器为默认，原生 C++ 运行时为 opt-in（旧 `preferences.py:32-36`）；两套实现长期并行。**B7.5 已删除 Python 执行器与该开关，原生运行时恒执行** | 明确收敛路线：要么把原生运行时设为默认并保留 Python 作为兜底，要么标记 Python 路径为 legacy；当前不做 |
| C-3 | `bge_gamenodes` 中依赖砖块的旧节点（**B7 前历史**） | `RunActuator` 已标 `deprecated = True`（`editor/nodes/actions/runactuator.py:15`）；`SensorValue` / `SetSensorValue` / `SetActuatorValue` / `ControllerStatus` 走 Python 执行器的砖块 API | 砖块运行时保留时可留；若要清理，需给用户迁移提示，不可静默删除节点类型 |
| C-4 | 砖块查询实现与节点查询实现重叠 | `SCA_NearSensor`/`SCA_RaySensor` 与 `KX_SceneAdapter` 的射线/邻近查询各自实现 | 现阶段保留；后续可统一到场景查询服务，减少语义漂移 |
| C-5 | 若决定保留纯砖块用户 UI | 编辑器 UI 设计陈旧（固定面板、无节点式工作流） | 应先重构（现代化 UI/与节点面板统一），而不是一次删除；本报告建议按 A-1 移除，若用户群反对则转 C-5 |

---

## 4. 快速评估（只评估，不动）

| 模块 | 查了什么 | 结论 |
|---|---|---|
| 物理（Bullet） | `KX_PhysicsAdvancedAdapter` 是否另建 `CcdPhysicsEnvironment` | **否**。适配器统一通过 `Environment()` 取 `PHY_IPhysicsEnvironment`（`KX_PhysicsAdvancedAdapter.cpp:554, 758, 857, 941, 977, 1077, 1147, 1217`），与 B6.1 适配器共用同一物理环境；属同一环境的第二套 API 表面，不是重复路径。不动 |
| 渲染 | `RAS_*` 与 `KX_UIRenderer` 是否重复绘制 | **否**。`KX_UIRenderer` 直接走 `GPU_*`/`BLF`（`KX_UIRenderer.cpp:39-52`），由 `KX_Scene::GraphDrawUI`（`KX_Scene.cpp:2575-2578`）在引擎绘制回调内调用；`RAS_*` 是场景光栅器，两者层级不同。仅需注意 UI 绘制与场景状态的交替顺序。不动 |
| 音频 | `SCA_SoundActuator` 与 `KX_AudioAdapter` 是否双路 | **是双代码路径、单一设备**。两者都取 `aud::DeviceManager::getDevice()`（`SCA_SoundActuator.cpp:153`；`KX_GameAdaptersEngine.cpp:2572`），但各自维护句柄表。无设备冲突；长期可统一 3D 句柄管理。不动 |
| 输入 | `SCA_KeyboardDevice` 与 `KX_InputAdapter` 是否双路 | **不是双设备**。不存在 `SCA_KeyboardDevice` 类；实际是 `SCA_KeyboardManager`/`SCA_MouseManager` 持有 `SCA_IInputDevice`（`KX_Scene.cpp:217-218`），砖块传感器与节点适配器（`KX_GameAdaptersEngine.cpp:244-249`）**共用同一输入源**，属两个消费者。不动 |

---

## 5. 风险与「移除后会断什么」汇总

1. **`logic_ops.cc` 是唯一硬墙**：直接删整目录会让节点编辑器的组件注册（`nodetree.py:63/70`）与物体属性面板（`properties_game.py:96-141`）失效。必须先执行 C-1 拆分。
2. **`.blend` 兼容**：`SPACE_LOGIC = 17`（`DNA_space_enums.h:1201`）已写入用户文件；移除类型需在文件读取/版本化阶段把旧区域映射到其他编辑器（可参考 `versioning_280.cc:2398/2410` 的既有处理方式），否则区域无法识别。
3. **Python API 断裂**：`bpy.types.SpaceLogicEditor`、`SpaceLogic` 相关 RNA 属性移除会影响第三方脚本；需在 release notes 标注。
4. **悬空 UI 配置**：键位 "Logic Bricks Editor"（`blender_default.py:2464`）、主题色（`resources.cc:174`）、用户偏好编辑器列表（`rna_userdef.cc:4530`）需同步清理。
5. **Bricky Nodes 数据**：删除插件后已保存的 `BGEBrickTree` 节点组变成无类型数据；建议只从强制启用列表移除、保留插件文件一轮，或提供明确迁移说明。
6. **不要改构建目标名**：`tests/a1_clean_harness.sh:50` 与 `tests/a4_benchmark.sh:40` 直接引用 `libge_logic_bricks.a`，重命名 GameLogic 归档会连带破坏干净构建与基准脚本（该归档同时包含 `GameNodes`，改名也无法「只删砖块」）。
7. **测试依赖**：`tests/` 未见对 `SPACE_LOGIC`/`SCA_*` 的直接引用，编辑器移除不会误删测试；但若后续动 `GameLogic` 目标名，需按上条同步改脚本。

---

## 6. 最终结论

> **逻辑砖块运行时保留（逻辑节点依赖），砖块编辑器移除。**

具体处置：

- 保留 `SCA_*` 运行时（基类、调度器、事件管理器、全部传感器/控制器/执行器类、转换器、`bge.logic` API）。
- 移除 `space_logic` 编辑器 UI（A-1/A-2/A-3），但**先把 `logic_ops.cc` 中的 Python 组件算子迁出**（C-1）。
- 移除 `bge_bricknodes` 砖块节点视图插件（A-4）。
- 不整体移除、不重命名 `libge_logic_bricks.a`；不改物理/渲染/音频/输入四条路径。

涉及规模（若按上述执行）：**编辑器簇 5 个源文件 + 约 15 处注册管线 + 1 个启动 UI 脚本 + 1 个插件目录（8 文件）**；运行时 **0 个类删除**，仅允许清理无引用的废弃分支。

---

## 7. 原生运行时偏好开关的完整证据链（B7 前历史；该开关已于 B7.5 删除）

### 7.1 定义位置、默认值、读取封装

| 项 | 证据 |
|---|---|
| 定义（历史） | `scripts/startup/bge_gamenodes/preferences.py:32-36`，`LogicNodesAddonPreferences`（`bl_idname='bge_netlogic'`，`:20-21`）内的 `BoolProperty`，`default=False`；B7.5 已连同 `AddonPreferences` 删除 |
| 读取封装 | `scripts/startup/bge_gamenodes/generator/ir_export.py:64-68`：`native_runtime_enabled()` → `getattr(prefs, 'use_native_graph_runtime', False)`，无 prefs 时返回 `False` |

**关键性质：这是“导出期开关”，不是引擎运行期开关。** C++ 侧没有任何代码读这个偏好；C++ 只看场景里是否存在 `game_graph_manifest`（`KX_Scene.cpp:2523-2538`）或热重载旁路文件（`KX_Scene.cpp:2540-2565`）。

### 7.2 全量读取点与分支

| 读取点 | 文件:行号 | 分支行为 |
|---|---|---|
| 游戏启动生成 | `scripts/startup/bge_gamenodes/__init__.py:94-97` | `native_runtime_enabled() or bpy.app.background` → 只写 manifest 并返回；否则 `generate_logic_node_code()` 生成 Python 模块 + 写 manifest |
| 保存时同步 | `__init__.py:104-106` | 仅在原生开启时写 manifest |
| manifest 元数据字段 | `generator/ir_export.py:460` | 清单内写入 `'native_runtime': native_runtime_enabled()` |
| manifest 写入闸门 | `generator/ir_export.py:486-492` | 关闭时**删除**场景属性 `game_graph_manifest`，原生路径严格 no-op |

### 7.3 是否暴露到 UI / RNA、用户怎么打开

- UI（B7.5 已删除）：`preferences.py:89-93` 曾在 Add-on Preferences 面板画该开关。现形：偏好为 `bpy.context.preferences.game_nodes`，面板 `USERPREF_PT_game_gamenodes`，位置 **Preferences → Game**；**没有任何「原生运行时」开关**，manifest 导出恒执行。
- 打开路径（现形）：`Edit > Preferences > Game > Game Nodes`。
- 属性可脚本访问（现形）：`bpy.context.preferences.game_nodes`；它不是 addon preferences，模块随引擎启动、**不可禁用**。
- 无其他 UI 入口（全仓库该字符串只出现在 `preferences.py:32/91` 与测试 mock）。

### 7.4 关闭与开启分别走哪条执行路径（B7 前历史；两条分支均已于 B7.5 删除，仅保留原生路径）

**关闭（默认值）——Python 执行器：**

1. `__init__.py:94-98` → `generate_logic_node_code()`（`generator/tree_code_generator.py`）。
2. 生成代码调用 `bge.logic.getCurrentScene()`（`tree_code_generator.py:110`），并生成继承 `bge.types.KX_PythonComponent` 的类（`:134`）。
3. `write_scene_manifest()` 在关闭状态删除场景属性（`ir_export.py:486-492`）→ 场景无 manifest。
4. C++ 侧 `KX_Scene::GraphTick` 读不到 JSON 直接返回（`KX_Scene.cpp:2498-2502`）。
5. 每帧实际执行者：`KX_GameObject::Update()` 遍历组件并调用 `comp->Update()`（`KX_GameObject.cpp:1980-1992`）；`KX_PythonProxy.cpp:78-84` 在 `start` 时取 Python 的 `update`/`dispose` 方法。

**开启——原生 C++ 运行时：**

1. `__init__.py:94-96` 只写 manifest，**不再生成 Python 模块**（避免每条节点跑两次）。
2. `write_scene_manifest()` → `ir_export.py:479-497` 写入 `scene["game_graph_manifest"]`。
3. C++ 读取：`KX_Scene::ReadGraphManifestJson()`（`KX_Scene.cpp:2523-2538`）→ `GraphTick`（`:2490-2517`）→ `KX_GraphSceneManager::OnSceneLoad/Tick`。
4. 适配器装配：`KX_GraphSceneManager.cpp:57` → `KX_CreateEngineAdapters`（`KX_GameAdaptersEngine.cpp:2976`）。
5. 热重载另有旁路：`.hot_reload_manifest.json`（`KX_Scene.cpp:2540-2565`）。

**推论**：一个已经写入了 manifest 的 `.blend`，即使本机偏好为 `False`，C++ 运行时仍会启动；反之本地开启偏好但未重新导出/保存，场景无 manifest，C++ 仍不运行。开关只影响“导出动作”，不是“运行时开关”。

### 7.5 我们的 E2E 实际走哪条

| 测试入口 | 文件:行号 | 走法 | 是否经过偏好开关 |
|---|---|---|---|
| `b69` | `tests/b69_e2e.cpp:14, 260-282` | 直接 `new KX_FunctionNode(...)` / `KX_FunctionNode` | **否，绕过** |
| `b661_query` | `tests/b661_query_e2e.cpp:11, 297` | 直接构造 `KX_FunctionNode`、取 `KX_ExecutionContext`（`:468, 572`） | **否，绕过** |
| `c32_nodes` | `tests/c32_nodes_e2e.cpp:15, 144, 224` | 直接 `KX_FunctionNode` + `KX_GraphInstance` | **否，绕过** |
| `b69_file_engine` | `tests/b69_file_engine_e2e.cpp:34-36` | 直接 `KX_CompiledGraph` / `KX_FunctionNode` | **否，绕过** |
| `b69_rpc_two_proc` | `tests/b69_rpc_two_proc.cpp:32-34` | 直接 `KX_CompiledGraph` / `KX_FunctionNode` | **否，绕过** |
| `b66_uncovered` | `tests/b66_uncovered_e2e.cpp:16` | 直接 `KX_CompiledGraph` | **否，绕过** |
| `p42_hot_reload` | `tests/p42_hot_reload.cpp:17, 214` | 直接构造 `KX_GraphSceneManager`，JSON 直接喂给 `KX_ParseSceneManifestJson` | **否，绕过**（runner 注册于 `tests/run_all_e2e.sh:66`） |
| `b5_p3` | `tests/b5_rerun/p3_harness.cpp:5, 82-83` | 直接 `KX_ParseSceneManifestJson` → `KX_GraphInstance` | **否，绕过**（`run_all_e2e.sh:54`） |
| `p3_export` | `tests/b5_rerun/p3_export.py:87` | mock `Preferences.use_native_graph_runtime = True`，跑真实 `ir_export.py` | **是，但只覆盖导出分支**（`run_b5_rerun.sh:55`，不在 `run_all_e2e.sh` 清单） |
| `p4_editor_hot_reload` | `tests/p4_editor_hot_reload.py:30` | 真实 addon，`prefs.use_native_graph_runtime = True` 后跑 `netlogic.hot_reload()` | **是，但不在主 E2E**（`run_all_e2e.sh:51-98` 的 MANIFEST 无此项，仅 `tests/README.md:52` 提及） |
| `verify_gap/make_*` | `tests/verify_gap/make_worldpos.py:126`、`make_anim.py:332` | 直接写 `scene['game_graph_manifest']` | **否，绕过偏好**；且不在 `run_all_e2e.sh` |

结论（B7 前）：**`run_all_e2e.sh` 里的节点类 harness 全部直连 C++ 或直喂 JSON，没有一条经过当时的用户默认路径（Python 执行器）。** 只有 `p3_export`（导出分支）和 `p4_editor_hot_reload`（不在主 E2E）真正碰了开关。**B7.5 已删除 Python 执行器与开关，回退路径不存在了；但入口覆盖问题仍成立——多数 harness 仍直连节点类/编译图、绕过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口。**

### 7.6 该开关是否为本仓库自加

- `git blame` `preferences.py:32-36` → 提交 `e2a2d35e`（作者 `Eridanus369`，2026-11-04 23:53），提交信息「feat: 底层原生 Game Nodes 及 UI 模块更新」。
- 同一提交经 `git log -S"GraphTick" -- source/gameengine/Ketsji/KX_Scene.cpp` 命中：**开关与 C++ 运行时是同一笔仓库内提交引入的。**
- 远程配置：`origin = git@github.com:Eridanus369/EBC.git`，`upstream = https://projects.blender.org/blender/blender.git`；**没有 UPBGE 远程/引用**，故「UPBGE 上游是否已有此标志」**未证实**（未联网核对）。可确证的是：它不在上游 Blender 的提交历史里，是本仓库自加。

### 7.7 结论（一句话）

> **「C++ 节点实现正确」与「用户默认跑到 C++ 节点」是两件事。**

前者由直连 `KX_FunctionNode`/`KX_CompiledGraph`/`KX_GraphSceneManager` 的 harness 证明；后者在 B7 前**不成立**——默认执行的是 Python 执行器，用户必须手动勾选偏好并重新导出 manifest 才走 C++。因此 B7 前的节点断言只证明「C++ 实现正确」，不证明生产默认路径。

**B7.5 后的变化**：Python 逻辑执行器（`tree_code_generator` 等 6 文件）已删除，manifest 导出恒执行，旧 `.blend` 的 `nl_` 组件运行时只告警并跳过（每对象一次，带对象名与树名，不自动转换），通用 Python 组件不受影响——**回退路径已消失**。**但这不等于 E2E 已覆盖用户路径**：绝大多数 harness 仍直连 `KX_FunctionNode`/`KX_CompiledGraph`，不经过 `KX_Scene::GraphTick`，仅 `p3` manifest harness 走真实入口。因此「C++ 实现正确」成立，「用户点 P 的默认体验」的证据缺口仍需实机验证补齐。

---

## 8. 可施工清理方案（含对第 3 节的修正）

### 8.1 对第 3 节 A 类的两处修正

1. **A-1 不是“5 文件全删”**：`logic_ops.cc` 必须保留（承载节点系统与属性面板的算子）。正确做法是删 4 个 UI 实现文件、改 `CMakeLists.txt` 的 `SRC` 只留 `logic_ops.cc`。因此第 6 节“5 个源文件”应改为 **4 个源文件删除 + 1 个 CMake 改动**。
2. **A-2 不是“15 处管线全删”**：最小施工只摘 **2 处注册入口**（`spacetypes.cc:84`、`bl_ui/__init__.py:82`），DNA/RNA 暂留。旧 `.blend` 中 `SPACE_LOGIC` 区域会安全回退为 3D Viewport（`area.cc:2355-2362` 的 `area_init_type_fallback(area, SPACE_VIEW3D)`），不会崩溃；等到可选深清理阶段再做 RNA/DNA 删除与版本化映射。

### 8.2 A-1：`space_logic` UI 逐文件/符号/引用复核

| 文件 | 删除后消失的符号（文件:行号） | 谁引用（复核结果） |
|---|---|---|
| `space_logic.cc` | `ED_spacetype_logic`（`:332`）；`logic_has_buttons_region`（`:65`）；`logic_listener`（`:189`）；`logic_context`（`:218`）；`logic_main_region_init/draw`（`:229/:242`）；`logic_buttons_region_init/draw`（`:266/:276`）；`logic_header_region_init/draw`（`:284/:289`）；`logic_blend_read_data/write`（`:309/:326`） | `ED_spacetype_logic` 仅 `spacetypes.cc:84`（调用）+ `ED_space_api.hh:41`（声明）；`logic_has_buttons_region` 被 `logic_ops.cc:820`、`logic_buttons.cc:60`、本文件 `:65` 使用 |
| `logic_window.cc` | `logic_buttons`（`:2598`）及全部 `static draw_*` 面板函数（约 `:1737-2524`） | `logic_buttons` 仅 `space_logic.cc:254`；声明仅 `logic_intern.hh:50` |
| `logic_buttons.cc` | `LOGIC_OT_properties`（`:68`）、`LOGIC_OT_links_cut`（`:151`）及 `cut_links_*` 静态函数 | 仅 `space_logic.cc:175-176` 注册 |
| `logic_intern.hh` | 上述 3 个导出声明（`:42/:45-46/:50`） | 仅 `space_logic` 目录内部；`logic_ops.cc` 未 include 该头（其 include 列表 `:31-56` 无 `logic_intern.hh`） |
| `CMakeLists.txt` | 不删文件；`SRC`（`:38-45`）删 4 项只留 `logic_ops.cc` | 目标名 `bf_editor_space_logic`（`:62`）被 `space_api/CMakeLists.txt:32` 链接，**保持目标名不变** |

**连带的必删项（否则链接失败）**：`logic_ops.cc` 中 `LOGIC_OT_view_all`（`:800-812`）与 `LOGIC_OT_region_flip`（`:836-856`）依赖 `logic_has_buttons_region`（`:820`、`:843`），必须与 `space_logic.cc` 同批删除，并从 `ED_operatortypes_logic()`（`:1408-1409`）摘除注册。`ED_operator_logic_active`（`screen_ops.cc:548-551`、`ED_screen.hh:860`）随两条算子一起变为无引用，可同批清掉。

### 8.3 A-2：注册入口（最小集）

| 改动 | 文件:行号 | 复核 |
|---|---|---|
| 删调用 | `source/blender/editors/space_api/spacetypes.cc:84`（`ED_spacetype_logic();`） | 全仓仅此调用点 |
| 删声明 | `source/blender/editors/include/ED_space_api.hh:41` | 仅被 `space_logic.cc:332` 实现 |
| 删启动 UI 导入 | `scripts/startup/bl_ui/__init__.py:82`（`"space_logic",`） | 仅此一行 |
| 删启动 UI 文件 | `scripts/startup/bl_ui/space_logic.py` | `LOGIC_PT_properties`（`:23-24`）、`LOGIC_MT_logicbricks_add`（`:84,96`）、`LOGIC_MT_editor_menus`（`:106`）、`LOGIC_MT_view`（`:119`），无外部引用 |
| **暂不删** | `rna_space.cc:144/830-831/9324/9949`、`DNA_space_types.h:928`、`DNA_space_enums.h:1201`、`wm_draw.cc:610`、`resources.cc:174`、`context.cc:1108`、`bpy_rna_callback.cc:236`、`interface_template_search_menu.cc:650`、`anim_filter.cc:396`、`versioning_280.cc:2398/2410`、`blender_default.py:2464`、`rna_userdef.cc:4530/4648` | 保留可让旧文件/旧键位继续加载；旧区域已被 `area.cc:2359-2361` 回退为 3D Viewport |

### 8.4 A-3 / A-4（插件侧）

- A-3 已在 8.3 合并；删除 `space_logic.py` 后启动 UI 无残留。
- A-4 `bge_bricknodes`：删 8 个文件（4 个 `.py`：`__init__.py`/`nodes.py`/`ops.py`/`ui.py`），摘 `blendfile.cc:1538` 与 `versioning_userdef.cc:1703` 两行强制启用。消失符号：`BGEBrickTree`（`ui.py:120-121`）、`BGE_PT_GamePropertyPanel`/`BGE_PT_BrickyTreeOptions`/`BGEBN_PT_GameComponentPanel`、`ops.*` 全部算子、`nodes._*` 节点类。复核：`tests/` 0 引用；`bge_gamenodes` 0 引用；唯一外部耦合是它调用 `bpy.ops.logic.sensor_add/controller_add`（`nodes.py:1108` 起），这些算子保留在 `logic_ops.cc`。

### 8.5 `space_logic` 拆分方案（`logic_ops.cc` 归属）

`logic_ops.cc` 实际承载三类算子（注册表 `:1386-1409`）：

| 类别 | 算子 | 依赖方 |
|---|---|---|
| 砖块增删移 | `sensor/controller/actuator_add/remove/move` | 节点插件播放器 `utilities/make_launcher.py:46-48`；Bricky Nodes `nodes.py:1108` 起；无 `tests/` 引用 |
| Python 组件 | `python_component_register/reload/create/remove/move_up/move_down`（`:1097-1384`） | `editor/nodetree.py:63,70`；`utilities/__init__.py:528,707`；`ops/unapplylogictree.py:50`；`ops/reloadcomponents.py:68`；`properties_game.py:96-141` |
| 自定义对象 | `custom_object_register/reload/create/remove`（`:919-1031`） | `properties_game.py:56-77` |
| 视图（可删） | `view_all`、`region_flip` | 仅编辑器 UI |

**两个迁移方案：**

- **方案 A（最省事，推荐）**：`logic_ops.cc` 原地保留，`bf_editor_space_logic` 目标只留它一个源文件。不做文件搬迁、不改 `spacetypes.cc:107` 的 `ED_operatortypes_logic()` 调用、不动 `ED_logic.hh:29` 声明。目录名仍叫 `space_logic`，可在后续单独重命名（纯改名，不改逻辑）。
- **方案 B（彻底去目录名）**：新建 `source/blender/editors/object/object_component_ops.cc`，迁 `custom_object_*` + `python_component_*` 及其静态辅助函数；在 `source/blender/editors/object/CMakeLists.txt` 的 `SRC` 加一行；`logic_ops.cc` 只留砖块算子。代价：新增跨目录声明头、调整链接依赖，收益只是目录命名整洁，**不建议本轮做**。

**“能否只摘注册入口而不删文件”**：可以。只改 `spacetypes.cc:84` 与 `bl_ui/__init__.py:82` 即可让编辑器不可达；文件留着也不影响构建。但旧 `.blend` 的 `SPACE_LOGIC` 区域会回退成 3D Viewport（不会崩）。若要求旧区域保留为“空编辑器”，则保留一个只注册类型、不挂面板的 stub SpaceType；本报告不推荐（会留下半个编辑器）。

### 8.6 最小可施工顺序（每步含验证）

> 以下为施工阶段动作；当前只读，未执行。施工时仍不得碰 `build_linux`，构建验证放到独立验证目录或经批准后再做。

**Step 0 — 基线冻结**
- `git status --short` 确认仅文档变更；记录 `grep` 基线：
  - `grep -rn "ED_spacetype_logic" source/`
  - `grep -rn "logic_buttons\|logic_has_buttons_region" source/`
  - `grep -rn "bge_bricknodes\|BGEBrickTree" source/ scripts/`
  - `grep -rn "bpy.ops.logic.python_component\|bpy.ops.logic.sensor_add" scripts/`
- 判据：命令输出可存档，作为后续对照。

**Step 1 — 算子分流确认（不改文件）**
- 保留集：砖块 add/remove/move + `custom_object_*` + `python_component_*`（依据 8.5）。
- 待删集：`LOGIC_OT_view_all`、`LOGIC_OT_region_flip`。
- 验证：`grep -rn "bpy.ops.logic.python_component_register" scripts/` 非空；`grep -rn "bpy.ops.logic.sensor_add" scripts/` 非空（说明保留集被真实使用）。

**Step 2 — 摘编辑器注册 + 删启动 UI**
- 改 `spacetypes.cc:84`（删调用）、`ED_space_api.hh:41`（删声明）、`bl_ui/__init__.py:82`（删导入），删除 `scripts/startup/bl_ui/space_logic.py`。
- 验证：
  - `grep -rn "ED_spacetype_logic" source/` → 仅剩 `space_logic.cc:332` 定义（死代码）；
  - `grep -rn "LOGIC_EDITOR" scripts/startup/bl_ui/` → 空；
  - 启动 `blender --background --factory-startup` 不报 Python 导入错误（如允许运行）。

**Step 3 — 删 UI 实现 + 缩减 CMake + 摘视图算子**
- 删除 `space_logic.cc`、`logic_window.cc`、`logic_buttons.cc`、`logic_intern.hh`；改 `space_logic/CMakeLists.txt:38-45` 只留 `logic_ops.cc`；删 `logic_ops.cc:800-856` 两条视图算子及 `:1408-1409` 注册；清 `screen_ops.cc:548-551` 与 `ED_screen.hh:860`。
- 验证：
  - `grep -rn "logic_buttons\|LOGIC_OT_properties\|LOGIC_OT_links_cut\|logic_has_buttons_region\|ED_operator_logic_active" source/` → 空；
  - `grep -rn "ED_spacetype_logic" source/` → 空；
  - 构建目标 `bf_editor_space_logic` 仍可编（`ninja -C <验证目录> bf_editor_space_logic`）。

**Step 4 — 删 Bricky Nodes 插件**
- 删 `scripts/addons_core/bge_bricknodes/`（8 文件），摘 `blendfile.cc:1538`、`versioning_userdef.cc:1703`。
- 验证：
  - `grep -rn "bge_bricknodes\|BGEBrickTree" source/ scripts/ --include="*.py" --include="*.cc" --include="*.hh"` → 空（`.pyc` 缓存不计）；
  - `grep -rn "bpy.ops.logic.sensor_add" scripts/startup/bge_gamenodes/` 仍非空（播放器未断）。

**Step 5 — 可选深清理（RNA/DNA/主题/键位/版本化）**
- 删除前先在文件载入阶段把 `SPACE_LOGIC` 映射到 `SPACE_NODE`（参照 `versioning_280.cc:2398/2410` 的区域类型改写方式），再删 `rna_space.cc:9324/9949`、`DNA_space_types.h:928`、`DNA_space_enums.h:1201`、`resources.cc:174`、`blender_default.py:2464`、`rna_userdef.cc:4530/4648` 等。
- 验证：用一份含 `SPACE_LOGIC` 区域的 `.blend` 做载入测试（合成或旧资产），确认区域变成 Node Editor 而不是空白；`grep -rn "SPACE_LOGIC" source/` 仅剩版本化映射分支。

**Step 6 — 回归（施工后）**
- `grep -rn "space_logic\|SPACE_LOGIC\|BGEBrickTree" tests/` 必须仍为 0（编辑器移除不误伤测试）；
- `grep -rn "logic.python_component_register" scripts/` 非空（节点系统 UI 未断）；
- 跑 `tests/run_all_e2e.sh --only 'p4|b69|b661|c32|p42'` + 全量 E2E，确认 0 failure；
- 注意：`tests/a1_clean_harness.sh:50`、`tests/a4_benchmark.sh:40` 依赖 `libge_logic_bricks.a` **目标名**，本轮不动该归档名，故不受影响。

### 8.7 本轮不做的事（明确边界）

- 不删 `SCA_*` 任何类、不改 `GameLogic` 归档名、不动 `build_linux`。
- 不把 `use_native_graph_runtime` 改默认值（这属于产品决策，见第 7 节；改默认会连带要求所有节点 harness 改走真实导出路径）。
- 不在本轮删除 RNA/DNA；那是 Step 5 的可选项，需先加版本化映射。
