# B6.3 场景控制高级（5 节点）

日期：2026-11-05
范围：`LoadSceneAsync`、`UnloadScene`、`TransitionScene`、`GetActiveScenes`、`SetSceneTimeScale`
运行时：`KX_FunctionNodesB6Scene.cpp`（表驱动，`kB6SceneSpecs`）

## 1. 接入位置

| 项 | 文件 | 位置 |
|---|---|---|
| 节点实现 | `source/gameengine/GameLogic/GameNodes/KX_FunctionNodesB6Scene.cpp` | LoadSceneAsync 112-194、UnloadScene 195-223、TransitionScene 229-356、GetActiveScenes 357-372、SetSceneTimeScale 378-402、表 404-453、注册 454 |
| 注册声明 | `.../KX_FunctionNode.h` | `RegisterB6SceneFunctionGameNodes()` 254 |
| 启动注册 | `.../KX_RuntimeNodes.cpp` | 245 |
| 构建 | `source/gameengine/GameLogic/CMakeLists.txt` | 227 |
| 场景适配器接口 | `.../KX_GameAdapters.h` | `HasScene` 1216、`UnloadScene` 1222、`ActiveSceneNames` 1227、`SetSceneTimeScale` 1233 |
| 引擎实现 | `.../KX_GameAdaptersEngine.cpp` | `HasScene` 1572、`UnloadScene` 1589、`ActiveSceneNames` 1615、`SetSceneTimeScale` 1629 |
| 编辑器表 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b6c.py` | `NATIVE_B6C` 49-119 |

## 2. 节点与端口

| 节点 | 输入 | 输出 |
|---|---|---|
| LoadSceneAsync | OnEnter, SceneName(String) | OnProgress(Float), OnLoaded, OnFail |
| UnloadScene | OnEnter, SceneName(String) | OnDone, OnFail |
| TransitionScene | OnEnter, FromScene, ToScene, Duration(Float) | OnDone |
| GetActiveScenes | — | Scenes(List\<String\>), Count(Int) |
| SetSceneTimeScale | OnEnter, SceneName, TimeScale(Float) | OnDone |

`GetActiveScenes` 是零输入值节点，每帧刷新；`Scenes` 来自 `SceneControl.ActiveSceneNames`，**引擎不保证顺序**。

## 3. LoadSceneAsync 的真实语义（重要限制）

`KX_KetsjiEngine::LoadScene` 本质是**单帧同步替换场景**，没有增量资产流式加载接口。因此本节点只做到：

1. `OnEnter` 校验 `SceneControl.HasScene(SceneName)`；不存在立即写 stderr、发 `OnFail`，不进入进度阶段（`KX_FunctionNodesB6Scene.cpp` 142-145）。
2. 存在则进入 0.5 秒的分帧进度反馈：每帧 `progress += delta_time / 0.5`，`OnProgress` 输出 0..1（154-169）。
3. 进度到 1.0 时才真正调用一次 `LoadScene` 并点亮 `OnLoaded`；失败写 `failed at replacement` 并发 `OnFail`（171-182）。

**这是「分帧进度反馈 + 末尾单帧替换」，不是真正的异步资产加载。** 进度条推进期间场景仍在原样运行，任何在进度中途期待新场景对象已可用的图都会失败。`verbose` config 打开后会逐帧打印 `progress=`。

E2E 证据（`/tmp/b63_nodes_e2e.log`）：`async progress samples: 0.000 0.100 0.200 0.300 0.400 0.500 0.600 0.700 0.800 0.900 1.000 1.000 1.000 1.000`，且在进度结束前 `LoadScene` 调用次数为 0、结束后恰好 1 次。

## 4. UnloadScene：`RemoveScene` 调查与明确失败语义

### 4.1 为什么 `RemoveScene` 不可用（证据）

`source/gameengine/Ketsji/KX_KetsjiEngine.cpp` 1341-1356：

```cpp
void KX_KetsjiEngine::RemoveScene(const std::string &scenename)
{
  /****************EEVEE INTEGRATION*****************/
  // DISABLE REMOVE SCENES FOR NOW
  std::cout << "KX_KetsjiEngine::RemoveScene: Remove Scenes is temporarily disabled during eevee "
               "integration"
            << std::endl;
  return;
  /**************************************************/
  if (FindScene(scenename)) {
    m_removingScenes.push_back(scenename);
  }
  ...
}
```

- 这是 **EEVEE 集成时期的显式整体禁用**（带标记注释的提前 `return`），不是合并事故、也不是编译宏意外关掉的分支；`m_removingScenes` 入队逻辑仍然写在 `return` 之后。
- 真正的释放链路仍在编译：`KX_KetsjiEngine::RemoveScheduledScenes()`（1358-1374）从队列里取场景，调 `m_converter->RemoveScene(scene)` 再从 `m_scenes` 摘除；`ProcessScheduledScenes()` 每帧末调用它。
- `BL_Converter::RemoveScene`（`source/gameengine/Converter/BL_Converter.cpp` 242-260）会 `Texture::FreeAllTextures(scene)`、清空 mesh slots、`scene->Release()` 并 `m_sceneSlots.erase(scene)` —— **是硬释放**，不是标记删除。
- 结论：这是**引擎真实限制（历史遗留的显式禁用）**，但释放路径本身完整。放开它会在一帧结束时真正释放纹理/网格/场景对象；渲染层（EEVEE）是否仍持有该场景的 GPU 资源没有任何注销钩子可以证明。在没有渲染生命周期审计的前提下，**放开属于不可控风险**，因此本轮不改引擎，只保证节点失败路径正确。

### 4.2 节点语义（不再「假成功」）

节点端口：`OnEnter, SceneName / OnDone, OnFail`（`KX_FunctionNodesB6Scene.cpp` 195-252）。

| 适配器结果 | 输出 | stderr |
|---|---|---|
| `Success` | `OnDone` | — |
| `SceneActive` | `OnFail` | `unload failed: scene is active` |
| `Unsupported`（RemoveScene 禁用） | `OnFail` | `unload unsupported: engine RemoveScene disabled` |
| `Missing` | `OnFail` | `unload failed: scene not found` |

`UnloadScene` 适配器新增 `UnloadSceneDetailed(name, reason)` 返回 `KX_SceneUnloadResult`（`KX_GameAdapters.h` 1338-1344、1364-1369）；旧的 `bool UnloadScene` 保留并委托给它，默认实现把 `true` 映射为 `Success`、`false` 映射为 `Unsupported`，所以自定义/旧适配器不会被破坏。引擎实现见 `KX_GameAdaptersEngine.cpp` 1589-1627（活跃判断改为在 `engine->CurrentScenes()` 里查名字，而不是只比较单个 `scene_`）。

**明确修复的坏状态**：之前「返回 `OnDone` 但什么都没卸载」的路径已不存在——只有真正返回 `Success` 才发 `OnDone`。

### 4.3 该限制对使用者的含义

在当前引擎里 `UnloadScene` 永远走 `OnFail`（除非未来重新启用 `RemoveScene`）。这是**诚实的失败**，而不是静默成功。E2E 断言已按新语义改写：`UnloadScene fired OnFail and reported the unsupported removal`（原断言是 `OnDone`，理由是旧语义正是本任务要消除的假成功）。

## 5. TransitionScene

- `ToScene` 必须存在（`HasScene`），否则 stderr + 直接 `OnDone`，不改场景。
- `Duration <= 0`：直接 `LoadScene(ToScene)` 并 `OnDone`，不建遮罩。
- `Duration > 0`：通过 **UI 适配器**（`ctx.UI()`）创建一个 Canvas + 全屏空 Label，`bg_color` 黑、`opacity` 从 0 升到 1，在 `Duration/2` 处调用一次 `LoadScene(ToScene)`，随后透明度降回 0 并销毁 Canvas/Label，最后 `OnDone`。
- **没有自建渲染路径**：遮罩完全走已有的 `KX_UIAdapter`（`CreateCanvas`/`CreateLabel`/`SetAttribute`/`Destroy`），headless 环境（`ScreenWidth/Height == 0`）时跳过遮罩但仍按中点切场景。
- `FromScene` 只用于诊断：若它不是活跃场景，写一条警告后仍然继续，不做「必须在 From 中」的硬限制。

## 6. SetSceneTimeScale

- 输入 `TimeScale <= 0` 时接受 0（暂停），负数被引擎适配器拒绝。
- 引擎只有**一个全局时间缩放**（`KX_KetsjiEngine::SetTimeScale`）。适配器先校验 `HasScene(name)`，再设置全局值，并写注释说明这是受限实现（`KX_GameAdaptersEngine.cpp` 1629-1640）。
- 节点在返回 false 时写诊断：`declined (the engine only exposes a global time scale)`，仍然发 `OnDone`。
- **限制：不能只缩放某一个场景**；同名场景不存在时不做任何改变。

## 7. 验收证据

- `/tmp/b63_nodes_e2e.log` 场景段：
  - `LoadSceneAsync progress is monotonic across frames`
  - `LoadSceneAsync progress ramps 0..1 over frames`
  - `LoadSceneAsync replaced the scene exactly once at the end`
  - `TransitionScene built the fade overlay through the UI adapter`
  - `TransitionScene destroyed the overlay after fading`
  - `TransitionScene loaded the destination at the fade midpoint`
  - `UnloadScene fired OnFail and reported the unsupported removal`（新语义；旧断言是 `OnDone`，那正是本轮要消除的假成功）
  - `GetActiveScenes exposed the active scene count`
  - `SetSceneTimeScale forwarded name and scale`
- 全量编译 `/tmp/b63_final_build.log` exit=0；契约/枚举 errors=0 / inconsistencies=0。
