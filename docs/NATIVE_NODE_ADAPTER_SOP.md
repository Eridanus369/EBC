# 原生节点适配器 SOP（B4 模板）

适用于把 legacy 节点能力迁移到 `KX_*Adapter` + 规格表驱动的原生运行时。
每一步都以 B3.4 已落地的音频、场景控制、Touch 节点为参考实现。

## 代码位置

| 层 | 文件 |
| --- | --- |
| 接口与 Mock | `source/gameengine/GameLogic/GameNodes/KX_GameAdapters.h` |
| Mock / Null 实现 | `source/gameengine/GameLogic/GameNodes/KX_GameAdapters.cpp` |
| 引擎实现 | `source/gameengine/GameLogic/GameNodes/KX_GameAdaptersEngine.cpp` |
| 适配器集合装配 | `KX_GameAdaptersEngine.cpp` 的 `KX_CreateEngineAdapters` |
| 运行上下文 | `KX_ExecutionContext.h/.cpp` |
| 节点规格与函数 | `KX_FunctionNodesB2.cpp` / `KX_FunctionNodesB3.cpp` |
| 节点注册表 | `KX_NodeRegistry.cpp` |
| 编辑器条目 | `scripts/addons_core/bge_netlogic/editor/nodes/native_b2.py` / `native_b3.py` |
| 导出器 | `scripts/addons_core/bge_netlogic/generator/ir_export.py` |

## 第 1 步：适配器接口与 Mock

在 `KX_GameAdapters.h` 新增或扩展一个 `KX_*Adapter`。接口只暴露节点需要的数据，
不引用 Ketsji 类型的具体实现（`KX_GameObject` 只前向声明即可）。

```cpp
class KX_FooAdapter {
 public:
  virtual ~KX_FooAdapter() = default;

  virtual bool DoThing(KX_GameObject *object, float value)
  {
    (void)object;
    (void)value;
    return false;
  }
};

class KX_MockFooAdapter : public KX_FooAdapter {
 public:
  std::vector<std::string> requests;
  uint32_t calls = 0;
  bool result = true;

  bool DoThing(KX_GameObject *object, float value) override;
  void Reset();
};
```

约定：

- 每个虚函数必须有中性默认实现（返回 `false` / `nullptr` / `0`），Null 适配器才能是空类。
- Mock 记录入参、调用次数和可注入的返回值，`Reset()` 清空全部状态。
- 输出型数据用结构体（如 `KX_RayHit`），避免节点直接接触引擎数学类型。
- 如果是新的独立服务，在 `KX_AdapterSet` 加 `unique_ptr`，在 `KX_ExecutionContext`
  加 `SetAdapters` 尾参数（默认 `nullptr`）和访问器；`KX_CreateEngineAdapters` 与
  `KX_CreateNullAdapters` 都要装配，`KX_GraphSceneManager` 负责传入。
- 若扩展已有适配器，直接在接口末尾追加虚函数，保持已有调用点不变。

Mock 实现写在 `KX_GameAdapters.cpp`：

```cpp
bool KX_MockFooAdapter::DoThing(KX_GameObject *object, float value)
{
  ++calls;
  requests.push_back(object->GetName());
  return result;
}
```

## 第 2 步：引擎实现

在 `KX_GameAdaptersEngine.cpp` 的匿名命名空间内实现 `Engine*Adapter`。
引擎实现必须走真实引擎路径，不允许留 `return false` 占位。

```cpp
class EngineFooAdapter final : public KX_FooAdapter {
 public:
  explicit EngineFooAdapter(KX_Scene *scene) : scene_(scene) {}

  bool DoThing(KX_GameObject *object, float value) override
  {
    if (scene_ == nullptr || object == nullptr) {
      return false;
    }
    /* 调用 KX_Scene / KX_GameObject / 物理环境的现有方法。 */
    return true;
  }

 private:
  KX_Scene *scene_ = nullptr;
};
```

要点：

- 复用 legacy 的同一路径（例如 `KX_Scene::AddReplicaObject`、`KX_RayCast::RayTest`、
  `aud::DeviceManager::getDevice()`），不要另建平行实现。
- 场景相关服务在构造函数接收 `KX_Scene *`，并在 `KX_CreateEngineAdapters` 里装配。
- 资源缺失时 `fprintf(stderr, "GameNode: ...")` 报错并返回失败值，不能崩溃。
- 新状态位只加最小语义。例如 `KX_Scene::m_suspended` 只控制逻辑 tick 和物理步进，
  渲染保持；`KX_KetsjiEngine` 在对应的 `IsSuspended()` 分支 `continue`。
- 不在节点层包含 Ketsji 头文件；所有引擎耦合限定在本文件。

## 第 3 步：节点规格与执行函数

在对应的 `KX_FunctionNodes*.cpp` 中定义端口表和执行函数，并加入注册表：

```cpp
const KX_FuncPort kFooInputs[] = {
    TP("OnEnter"),
    FP("Object", KX_VALUE_OBJECT),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kFooOutputs[] = {
    TP("OnDone"),
    FP("Result", KX_VALUE_BOOL),
};

void FuncFoo(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const bool ok = ctx.Foo().DoThing(node.InObject(ctx, 1), node.InFloat(ctx, 2));
  node.OutBool(ctx, 1, ok);
  node.OutTrigger(ctx, 0, ok);
}
```

注册（沿用现有宏）：

```cpp
B3_ACTION("Foo", kFooInputs, kFooOutputs, FuncFoo),
```

约定：

- 端口顺序就是编辑器 socket 顺序，也是 C++ 索引顺序。
- 配置读取用 `node.ConfigString("key")`，输入为空时回落到配置。
- 轮询节点用 `B3_TICK_STATE` + 状态结构体；动作节点用 `B3_ACTION`。
- 常量字符串/数值端口优先 `FP`，触发端口用 `TP`。
- 输出对象用 `OutObject`，向量用 `OutVector(x, y, z)`。

## 第 4 步：编辑器条目

在 `native_b2.py` / `native_b3.py` 中追加一行，保持与 C++ 端口同名同序：

```python
('Foo', 'Foo Label', 'ACT',
 [('OnEnter', 'Condition'), ('Object', 'Object'), ('Value', 'Float')],
 [('OnDone', 'Condition'), ('Result', 'Bool')],
 {})
```

需要配置时：

```python
{'name': ('string', 'Name', ''),
 'match': ('enum', 'Match', {
     'items': [('exact', 'Exact', ''), ('prefix', 'Prefix', '')],
     'default': 'exact',
     'values': {'exact': 0, 'prefix': 1},
 })}
```

- `ACT` / `CON` / `PAR` 与节点语义一致。
- 编辑器条目名 = `GameNode<runtime_id>`，由 `_make_node` 自动生成。
- 配置不能留 `{}` 后再依靠输入口兜底暴露给用户；必须给出显式的配置项。
- 新条目会进入 `native_audit.py` 的清单；如果没有 legacy 对应，审计里默认计入
  `migrated_rewritten`。

## 第 5 步：构建、测试与回归

构建：

```bash
ninja -C build_linux -j6 > /tmp/build_<batch>.log 2>&1
```

运行 Blender / blenderplayer 必须带引擎库路径：

```bash
LD_LIBRARY_PATH=$PWD/lib/dpcpp/lib ./build_linux/bin/blender ...
LD_LIBRARY_PATH=$PWD/lib/dpcpp/lib ./build_linux/bin/blenderplayer /tmp/<scene>.blend
```

测试分层：

1. 编辑器注册：后台 Blender 创建 `BGELogicTree`，`nodes.new('GameNodeFoo')`，
   打印输入输出端口，断言数量与名字。
2. Mock 单元：`KX_MockFooAdapter` 注入结果，断言调用次数与入参。
3. 引擎路径：headless 场景 + 原生图清单，把结果写入 `/tmp/<case>.log`。
   例如 LoadScene 用两个场景，日志先出现 `L1 1` 再出现 `L2 ...` 即切换成功。
4. 回归：每批完成后重跑上一批的 E2E（B2 / B3.1 / B3.2 / B3.3），对比日志行数与
   关键输出，不能退化。

交付证据统一放 `/tmp`：构建日志、节点注册日志、运行日志、审计输出。
不要 `git commit` / `git push`，不要修改 `_lib.glsl`。

## B4 提交前检查表

- [ ] 接口有中性默认实现，Mock 有记录和 `Reset()`
- [ ] 引擎实现走现有引擎路径，无 `return false` 占位
- [ ] 新增状态位只覆盖必要语义，并有引擎侧跳过点
- [ ] C++ 端口表与编辑器 socket 同名同序
- [ ] 配置项显式，无空 `{}` 兜底
- [ ] 节点注册进 `KX_NodeRegistry`
- [ ] 编辑器节点可 `nodes.new()` 创建
- [ ] `/tmp` 有构建日志、注册日志、E2E 日志
- [ ] 上一批 E2E 回归无退化
