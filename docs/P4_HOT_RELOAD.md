# P4 热重载

P4 把「编辑器改图 → 运行中的 player 下一帧生效」做成一条默认可用、失败可回退的路径，
并让这条路径可调试、可回归。本文记录实现边界、协议与实测证据。

## 结论

| 阶段 | 结论 | 关键证据 |
| --- | --- | --- |
| P4.1 编译产物 + 双缓冲 Rebuild | 通过 | `/tmp/p41_rebuild`：23 PASS，3 节点图 Rebuild 成 5 节点图，新节点执行、Delay 剩余时间保留、非法图返回 false 且旧图继续 |
| P4.2 flag 轮询 + 编辑器操作符 | 通过 | `/tmp/p42_hot_reload`：9 PASS，`hot reload detected, rebuilding 1 instances`，改图后下一帧新 UI 节点生效且画布不重复；`bpy.ops.netlogic.hot_reload()` 写出 manifest/flag 并刷新场景属性 |
| P4.3 调试日志 | 通过 | `/tmp/p43_trace`：默认关，`BGE_GAMENODE_DEBUG=1` 或 `SetEnabled(true)` 打开，日志 `frame 1 activated: [Source, Branch, OnTrue, Delay]` |
| P4.4 稳定性 | 通过 | `/tmp/p44_stability` + ASan：100 次 Rebuild 资产 refcount / UI 句柄 / RuntimeData 全部回基线，重载与 Rebuild 混合后 refcount 归零，无泄漏无 UAF |

## P4.1 编译产物：`KX_CompiledGraph`

`KX_CompiledGraph` 持有一次编译的全部结果：

* `ExecutionPlan`：节点顺序、触发队列、输入输出偏移；
* `KX_RuntimeData`：节点私有状态块（按 `state_size` / 对齐分配）；
* 资产指针槽位：`asset_handle -> KX_GameDataAsset*`，从 plan 的绑定表建立；
* UI 句柄映射：`debug_name -> int`，每帧从节点状态回填。

`KX_CompiledGraph::Compile(ir, scene, owner, manager, diagnostics)` 只走纯编译，不做任何
场景副作用；编译失败返回 `nullptr` 并把 `error: ...` 诊断写进 `diagnostics`。

`KX_GraphInstance` 现在持有 `std::shared_ptr<KX_CompiledGraph> graph_` 和 `pending_`，
Rebuild 分三步：

1. `Rebuild`：编译新图；失败时 `fprintf(stderr, "rebuild failed, keeping old graph: %s\n", diag)`
   并返回 `false`，正在跑的图一个字节都不动。
2. 成功时 `next->MigrateStateFrom(*graph_)`，结果放进 `pending_`，本帧继续用旧图。
3. 下一次 `Tick` 的最开头执行 `graph_.swap(pending_)` 并 `BindGraph`，随后整帧都在新图上跑。
   因此任何一次 Tick 内部都只可能看到完整的新图或完整的旧图，不存在半替换状态。

### 状态迁移键

只迁移下列状态，其余私有状态一律重建为默认值：

| 状态 | 匹配键 |
| --- | --- |
| 节点私有状态块 | `runtime_type_id + '\n' + debug_name` |
| 资产槽 | `asset_handle`（旧图与新图的句柄并集，指针按句柄对应） |
| UI 句柄 | `debug_name`（只迁移 `KX_UIHandleState` / `KX_UISliderState`） |
| Delay 剩余时间 | `debug_name`（`KX_DelayState.remaining_sec` / `pending`） |
| ForEach 游标 | `debug_name`（`KX_ReadForEachCursor` / `KX_WriteForEachCursor`） |

`CopyStateBlock<T>` 在复制前校验状态块大小，偏移或大小不匹配就跳过并保留默认值，
这样并行线新增节点时不会读到越界内存。

UI 句柄存在场景级 `KX_UIAdapterEngine` 里，不在 plan 内。迁移的是「这个节点已经创建了
哪个 handle」，所以 Rebuild 后 UI 节点看到 `created = true` 直接复用，不会重复创建。

## P4.2 触发协议

运行时只做轮询，不需要共享内存：

1. `KX_GraphSceneManager::Tick` 每帧先调用 `CheckHotReload()`；
2. 读取 `<blend_dir>/.hot_reload.flag` 的 raw mtime tick，与已记录值比较；
3. mtime 变化 → 重新读取 manifest → `KX_ParseSceneManifestJson` → `ReloadGraphs`；
   日志为 `hot reload detected, rebuilding N instances`；
4. 每个实例 `Rebuild`，在同一帧边界统一换图。失败实例继续跑旧图。

mtime 用 `int64_t` 保存原始 tick 数，哨兵是 `numeric_limits<int64_t>::min()`。
libstdc++ 的 file clock 计数可以为负，不能用 `-1.0` 表示「没有 flag」。

manifest 有两个来源，按顺序回退：

* `<blend_dir>/.hot_reload_manifest.json`（跨进程 player 用这个）；
* `scene["game_graph_manifest"]`（内嵌 player 共享 bpy 数据时用这个）。

编辑器侧 `bpy.ops.netlogic.hot_reload()`：

1. `write_scene_manifest(scene)` 重导 IR 并刷新场景属性；
2. 把同一个 manifest 写到 `.hot_reload_manifest.json`；
3. 最后写 `.hot_reload.flag`（mtime 就是触发信号）。

先写 manifest 再写 flag，保证 player 看到新 mtime 时能读到完整 manifest。
`.blend` 未保存时操作符直接取消并提示，因为 flag 必须落在 `.blend` 目录旁。

## P4.3 调试日志

`KX_DebugTrace` 默认关闭，两种打开方式：

* 环境变量 `BGE_GAMENODE_DEBUG=1`（player 启动前设置）；
* `KX_DebugTrace::SetEnabled(true)`（供引擎偏好开关调用）。

打开后每帧末尾输出一行，同时追加到 `/tmp/p4_debug_trace.log`：

```
frame 1 activated: [Source, Branch, OnTrue, Delay]
frame 2 activated: [Sink, Source]
```

记录的是「本帧执行过的节点索引」，按首次执行顺序去重；触发根节点（如 `OnInit`）
每帧都会执行，所以即使单次信号已触发也会出现在列表里。只记录索引和 `debug_name`，
不记录任何运行时值。

## P4.4 稳定性

`/tmp/p44_stability` 用一张同时持有资产引用和三个 UI 元素的图（canvas / label / image），
连续 Rebuild 100 次，逐次断言：

* staging 阶段旧图仍完整绑定（`GetPlan()` 仍是旧计划，`HasPendingRebuild()` 为真）；
* staging 时 refcount 为 3（registry + 旧节点 + 新节点），换图后回到 2；
* UI 句柄逐次相同，`CreateCanvas` / `CreateLabel` / `CreateImage` 调用次数恒为 1；
* `RuntimeData::UsedSize()` 每次回到同一基线；
* 重载（`Shutdown` + `Load`）与 Rebuild 混合 5 轮，shutdown 后 refcount 回到 1；
* 最终 `Shutdown` + `Unregister` 后 `refcount == 0`、`registry.Get(handle) == nullptr`、
  `AssetCount() == 0`。

同一用例用 `-fsanitize=address` 重跑，LeakSanitizer 与 AddressSanitizer 均无输出。

GPU 纹理的观测边界：headless 下 `CreateImage` 就是纹理创建的适配器入口，次数恒为 1
说明图没有重复申请纹理；真实 `KX_UIAdapterEngine` 的纹理生命周期跟随元素句柄，
元素不被销毁就不会重新创建。

## 运行回归

```bash
# 全量编译
ninja -C build_linux -j6 > /tmp/p4_ninja_final.log 2>&1

# 单测 / E2E
/tmp/p41_rebuild
/tmp/p42_hot_reload
/tmp/p43_trace                 # 默认关；BGE_GAMENODE_DEBUG=1 走环境变量路径
/tmp/p44_stability             # 日志 /tmp/p4_stability.log
/tmp/reg_b4_e2e                # 17 个 B4 节点 + 10 次热替换
/tmp/reg_b2_b3                 # B2 ForEach + B3 动画/音频/雷达/触碰 + 热替换

# 编辑器操作符
blender --background --python tests/p4_editor_hot_reload.py
```

P4 之后重启环境时 `/tmp` 会被清空，上述 harness 需要按同名文件重建。重建的 harness
必须同时通过下表的独立审计，才能作为回归证据：

| 审计 | 结果 |
| --- | --- |
| `port_contract.py` | `native_nodes=140 catalog_nodes=54 errors=0 warnings=4` |
| `enum_consistency.py` | `checked_pairs=43 inconsistencies=0` |

## 已知限制

* 跨进程 manifest 依赖 `.hot_reload_manifest.json`；如果同一目录被两份 `.blend` 共用，
  最后写入者生效。
* ForEach 的列表指针存在旧 value pool 中，不参与迁移；热重载后若 `OnInit` 重新触发，
  循环会从头再跑一遍（`index` 本身已迁移，只要上游不再重触发就能续跑）。
* `KX_DebugTrace` 是逐帧全量输出，长时间运行请只在排查时打开。
