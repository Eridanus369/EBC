# EEVEE Next 高级高度场置换（光线步进 G-Buffer 重建）

本轮目标：在 EBC5.3 的 EEVEE Next（`BLENDER_EEVEE`）中内置「包围盒 + 光线步进」式高度场置换，
默认关闭，开启后覆盖深度/法线并交给 EEVEE 原生光照。本文是实施前的源码结构、焦散先例、
插点位置与方案说明；后续实测数据追加在文末「验收记录」。

## 1. 焦散先例（同形态集成）

| 层 | 文件 | 说明 |
|---|---|---|
| 数据设置 | `source/blender/makesdna/DNA_scene_types.h` (`EEVEE_SpectralCausticsSettings`, `SceneEEVEE::caustics`) | 场景级开关 + 参数 |
| 默认值 | `source/blender/blenkernel/intern/scene.cc` | `BKE_scene_eevee_defaults` 里初始化 |
| RNA | `source/blender/makesrna/intern/rna_scene.cc` (`rna_def_scene_eevee_spectral_caustics`) | 属性暴露 |
| UI | `scripts/startup/bl_ui/properties_render.py` (`RENDER_PT_eevee_spectral_caustics`) | Render 属性面板，标题栏开关 |
| 模块 | `source/blender/draw/engines/eevee/eevee_caustics.{hh,cc}` | 自包含模块：`init()/sync()/render()` |
| 装配 | `eevee_instance.hh/.cc` | `CausticModule caustics;` + `init/sync` 调用 |
| 管线 | `eevee_pipeline.cc` (`DeferredLayer::render`) | 在 G-Buffer 后、combine 前插 `inst_.caustics.render()` |
| 着色器 | `shaders/eevee_photon_*.glsl` + `shaders/infos/eevee_photon_infos.hh` | 独立 shader，CMake 与 `eevee_shader.{hh,cc}` 注册 |
| 采样接入 | `shaders/eevee_deferred_combine.bsl.hh` | combine 直接采样 caustic 贴图 |

结论：本功能按同一形态新增 `HeightfieldModule`，不改动焦散模块。

## 2. EEVEE Next 关键结构与插点

```
ShadingView::render()                          eevee_view.cc
  rbufs.acquire()
  prepass_fb_.ensure(depth, prepass_normal, object_id, vector)
  gbuffer_fb_.ensure(depth, combined, gbuf.header[0], gbuf.normal[0], gbuf.closure[0..1])
  ...
  DeferredPipeline::render()
    DeferredLayer::render()                    eevee_pipeline.cc:1268
      prepass_.render()                        -> 深度 = 载体平面
      hiz_buffer.swap_layer(); hiz_buffer.update()
      caustics.render()
      shadows.render()
      lights.eval_*_light_shaders()
      submit(gbuffer_ps_)                      DEPTH_EQUAL，写入 closure/normal
      [插点 A] heightfield.render()            <- 本功能
      [插点 B] hiz_buffer.update(force)        <- 用新深度重建 HiZ
      raytracing.render()
      submit(eval_light_ps_)                   <- 读 HiZ 得 P，读 gbuffer 得 N/closure
      submit(combine_ps_)                      <- 最终 radiance + 法线/位置等 pass
```

插点 A 必须在 G-Buffer 之后：G-Buffer pass 使用 `DRW_STATE_DEPTH_EQUAL` 与 prepass 深度逐像素
比较（`eevee_pipeline.cc:812`），若提前改深度，载体平面片元会整片失配、丢失 closure。

### 2.1 为什么能改「位置」

`eval_light_ps_` 的着色点位置不是从 `depth_tx` 直接算，而是读 HiZ：
`eevee_deferred_eval.bsl.hh:143,150`（`texelFetch(hiz.hiz_tx) -> point_screen_to_world`）。
因此：**写深度 → 重新生成 HiZ → 原生光照自动拿到交点 P**，无需改光照位置代码。

法线来自 G-Buffer 层：`gbuf.surface_N()`（`eevee_gbuffer_read.bsl.hh:99`）。
因此在 `LightEval` 读取 gbuffer 后把各层 `N` 覆盖为解析法线即可（插点 C）。
`combine` 的 Normal/Position render pass 也做同样覆盖（插点 D），保证 AOV 输出与光照一致。

## 3. 数据与资源

### 3.1 用户设置（不新增 DNA/RNA）

为避开与物理节点线的 DNA 冲突、避免文件格式变更，设置使用 **ID 自定义属性 + 原生 Python 面板**
（UI 位置与焦散一致：Render 属性 / Object 属性，标题栏开关）。C++ 通过 `IDProperty` 读取。

场景（`Scene`）：
- `hf_enabled` (bool) 总开关，默认关闭
- `hf_shadow` (bool)、`hf_ao` (bool)、`hf_debug` (int)、`hf_max_steps` (int, 默认 12)
- `hf_shadow_steps` (int, 默认 6)、`hf_ao_rays` (int, 默认 3)、`hf_jitter` (float)

对象（`Object`，载体平面）：
- `hf_enabled` (bool)、`hf_heightmap` (string, `bpy.data.images` 名称)
- `hf_min_height` / `hf_max_height`（本地 Z 范围）、`hf_height_scale`
- `hf_steps` / `hf_binary_steps`、`hf_normal_strength`
- `hf_shadow_strength` / `hf_ao_strength` / `hf_ao_radius`、`hf_debug`

### 3.2 GPU 数据

- `HeightfieldInstance` SSBO（≤32 个）：`world_to_local`、`local_to_world`、本地 AABB、UV 映射、
  hmin/hmax、贴图尺寸、mip 层数、每层 SSBO 偏移、调试/质量参数。
- `float pyramid[]` SSBO：高度图与本模块生成的**最大 Mipmap** 金字塔，按层拼接。
  存储量 = 原图 4/3（约 +33%），高度图只有这一份。
- 每帧两张 aux 贴图（屏幕尺寸，16F）：
  - `hf_geom_tx`：`rgb = 世界解析法线`，`a = 命中掩码`
  - `hf_shade_tx`：`r = 软阴影`，`g = AO`，`b = 命中距离`，`a = 步数/调试`
  关闭时以 1×1 清零贴图绑定，shader 读到的掩码为 0，行为与现状一致。

### 3.3 预处理（CPU，每张贴图一次）

`BKE_image_acquire_ibuf` 读取像素 → R 通道归一化 → 逐层 2×2 取 max 建金字塔 → 上传 SSBO。
缓存键 = `Image* + 分辨率 + session_uid + hf_rebuild`；图像内容原地修改需改 `hf_rebuild`
或重载图像（写入文档的已知限制）。

## 4. 求交算法（shader）

1. 每像素由 `draw::View` 构造世界射线；对每个启用实例变换到本地空间，做射线-AABB 求交，
   并把区间收窄到 `z ∈ [hmin, hmax]`（`t0..t1`）。
2. 用**最大 Mipmap 分层步进**，不做线性步进：
   - 当前层 `L` 的单元格最大高度 `Hmax`；
   - 若射线高度 `z > Hmax`：安全，前进到 `min(离开该单元格的 t, z 降到 Hmax 的 t)`；
   - 若 `z <= Hmax` 且 `L > 0`：降一层细化；
   - `L == 0` 时判定穿越，在 `[prev_t, t]` 做 `hf_binary_steps`（4–6）次二分锁交点。
3. 解析法线：`dPdu` / `dPdv` 用高度图中心差分（按 `hf_normal_strength`），叉乘归一化。
4. 软阴影：交点向光源发次级射线，做简化分层步进（只判遮挡，不求精确交点），
   每 TAA 样本抖动一次，多像素在时间维度累积。
5. AO：交点半球内 `hf_ao_rays` 条短射线，命中即遮挡，输出 `[0,1]` 因子。
6. 抗锯齿：单帧步数 8–12，`Sampling::sample_index()` 参与抖动，交给 EEVEE 原生 TAA 累积。
7. 深度写入：`[[frag_depth(greater)]]`（EEVEE 为 reverse-Z，越近越大），只在交点比现有深度更近时写入；
   写完后强制重跑 HiZ，使原生光照、AO、反射、景深看到新深度。

遮挡判定：仅当该像素现有深度确实落在载体平面（本地 z ≈ 平面 z）时才处理，
因此前方其他物体不会被高度场穿透。

## 5. 接入与 UI

- Render 属性新增 `RENDER_PT_eevee_heightfield`，标题栏开关 `scene["hf_enabled"]`，默认关。
- Object 属性新增 `OBJECT_PT_heightfield`，开关 `ob["hf_enabled"]`，含高度图选择、范围、质量。
- 关闭开关：不跑 pass、不建金字塔，aux 贴图为 1×1 零值，渲染路径与现状一致（验收项）。

## 6. 已知限制 / UNSUPPORTED（如实标注，后续验收逐条复核）

1. **真实轮廓（silhouette）**：EEVEE Next 的 deferred G-Buffer 由材质光栅化产生，
   屏幕外像素没有 closure，无法仅靠深度回写被光照。本版不做「按视线角度顶点位移的剪影」，
   标记 `UNSUPPORTED`；缓解方式是把载体平面做得略大于高度图域、域外高度取 0，避免平面边缘切断峰值。
2. **投射到其他物体的阴影**：阴影图仍按载体平面生成，位移后的峰不会给场景其他物体投影；
   自阴影（高度场内部微阴影）由屏幕空间阴影射线提供。
3. **3D 程序纹理/非平面 UV**：材质 closure 在载体平面片元处求值，UV 与交点一致（平面投影），
   但 3D 坐标/生成坐标类纹理不会按交点重新求值，属 deferred 架构限制。
4. 透明/折射层、运动模糊的深度一致性未验证，默认按不透明表面处理。
5. 高度图原地像素修改需 `hf_rebuild` 或重载（缓存策略见 3.3）。

## 7. 验收计划

1. 深度/法线 vs CPU 参考求交：逐像素误差分布（均值/p95/max、命中率）。
2. 对照图：剪影（标注 UNSUPPORTED 的现状）、自阴影、AO。
3. 1080p 与 4K 帧时间：开关两组，包含 pass 单独耗时。
4. 关闭开关回归：与基线逐像素一致（无高度场对象的场景 + 有对象但关闭）。
5. 构建/日志纪律：`flock /tmp/build_linux.lock` 串行；日志进 `logs/`；不 commit、不用 sudo；
   不触碰物理节点线文件族。

## 8. 验收记录

构建：共享树 HEAD `325d3071cd2f`（本线改动未提交），`2026-11-08`，Vulkan 后端，GPU = RTX 4050 Laptop。
重建一律 `flock /tmp/build_linux.lock -c 'ninja -C build_linux -j6 blender'`，日志在 `/tmp/hf_build*.log`。

### 8.1 本轮修掉的四个实际缺陷

1. **pass 命令录制顺序**：`PassSimple` 按录制顺序执行。此前 `init()` 里录了 draw，`render()` 每帧只在
   draw 之后再追加 SSBO / push constant，导致 shader 读到的 `instance_count` 是默认值 0（逐像素直接
   return）。改为每帧按「state → shader → framebuffer → 资源绑定 → push constant → draw」重录。
2. **每帧只 trace 一次**：`sync()` 每帧只调用一次，而 `rendered_` 守卫没有按样本重置，实际只有第 1 个
   TAA 样本走了置换（32 spp 时置换只占 1/32，画面接近平面）。改为跟随 `sampling.sample_index()` 重置。
3. **上行射线步进塌缩**：`t_height = (hz - p.z)/d.z` 对阴影/AO 上行射线为负，`min()` 后前进量被钳到
   接近 0，射线 12 步几乎不动 → 阴影几乎完全缺失。改为仅当 `t_height > 0` 才参与 `min()`。
4. **射线起点抖动**：起点离表面仅 `2ε`，level 0 的穿越判定落在 `ε` 阈值附近，逐样本闪烁成点阵噪声。
   抬高到 `8ε` 后稳定。

### 8.2 检查点结论

| 检查点 | 结论 | 依据 |
|---|---|---|
| 编译通过 | PASS | `ninja blender` EXIT=0 |
| 主路径出图 | PASS | 512² 位移地形连贯，无平面穿帮 |
| 法线正确 | PASS（视觉） | 解析法线使山脊明暗随坡度变化；法线通道渲染 pass 不可用（见 8.4） |
| 阴影可用 | PASS | 同视角 ON(shadow off→on) 差值 mean 7.2/255，max 132 |
| AO 可用 | PASS | 同视角 ON(ao off→on) 差值 mean 1.7/255，max 81 |
| 1080p 实时 | **FAIL（环境）** | 见 8.3：本机基线 EEVEE 帧就约 77 ms |

证据拼图：[hf_evidence.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/hf_evidence.png)
（OFF / 几何+法线 / +阴影 / +AO / full / TAA 1·16·64 spp，同视角 512²，24 spp）。

### 8.3 性能（1920×1080，1 TAA 样本，进程内计时，取 16 帧中位数）

| 配置 | 帧时间 | 相对 OFF |
|---|---|---|
| OFF | 77.0 ms | — |
| ON，steps=12，binary=5，shadow 2 射线，AO 3 射线×6 步 | 80.6 ms | +3.6 ms（+4.7%） |
| ON，steps=24 | 79.5 ms | +2.5 ms（测量噪声内） |
| OFF @3840×2160 | 275.6 ms | — |
| ON @3840×2160 | 283.6 ms | +8.0 ms（+2.9%） |

结论：高度场 pass 自身增量约 2–4 ms（1080p）、约 8 ms（4K），**不是**绝对帧时间的主因；
本机基线 EEVEE 帧本身约 77 ms（13 fps），因此「1080p 实时」在本环境 **未达成**，如实标 FAIL，
不通过降低画质/验收标准来凑。

### 8.4 步进配置收敛（256²，16 spp，几何+法线，与 32 步参考逐像素比较）

| steps | mean abs | p95 | px>3 |
|---|---|---|---|
| 8 | 13.10 | 117 | 9177 |
| 12 | 8.36 | 77 | 7273 |
| 16 | 4.22 | 17 | 4436 |
| 24 | 0.80 | 0 | 1371 |

本测试高度图含高频脊线（`|sin(22x+7y)|`），12 步**未收敛**（约 3.3% 平均偏差）。推荐配置：
**steps=24 / binary=5**（16 步可作性价比折中）。这是诚实的配置结论，不改高度图去迁就 8–12 步。

### 8.5 TAA 抖动收敛（阴影变体，512²，高频能量 = 相邻像素梯度均值）

| TAA spp | 1 | 4 | 16 | 64 |
|---|---|---|---|---|
| hf energy | 13.80 | 7.48 | 6.26 | 5.78 |

抖动 + 少量步进随样本收敛；剩余 5.78 主要是几何本身的脊线高频，不是阴影噪声。

### 8.6 仍未完成 / UNSUPPORTED

1. **剪影（包围盒边缘顶点位移）**：本版仍 `UNSUPPORTED`（见第 6 节第 1 条）。现状是载体平面边缘
   形成开放切口，未做域外封口。方案与代价：
   - 方案 A（推荐，约 30 行 + 2 张验证图）：shader 内在射线横向离开纹理域时，把高度钳到边缘单元
     高度（等价无限外延），不新增几何；代价是一次无效边缘像素的二次采样。
   - 方案 B（约 80 行）：按周长从边缘高度生成一圈 skirt 四边形（CPU，每次金字塔重建一次，
     O(perimeter)），彻底闭合开放边，也便于域外接近 0 的高度过渡。
2. **深度/法线渲染 pass**：`Z` / `Normal` pass 取自 prepass，不反映位移后的深度与解析法线
   （开关 ON/OFF 的 Z/Normal EXR 逐像素一致已验证）。逐像素 CPU 参考对比因此暂标记
   `UNSUPPORTED`，证据以 beauty 图像 + 解析法线着色为准。
3. 投射到其他物体的阴影、3D 程序纹理、透明/折射层一致性：维持第 6 节限制说明。

### 8.7 关闭开关回归

关闭路径不提交 trace pass、不建金字塔，aux 绑定 1×1 无效贴图（`is_valid` 恒假，combine 乘 1.0）。
此前的「ON 但未写任何命中」帧与 OFF 逐像素完全一致，间接验证了 OFF 路径无副作用。

## 9. 第二轮：参考图对齐、方案 A 剪影、SPOM 自遮蔽、掠射角

### 9.1 参考图量化（2048×1280，最大亮连通域，lum>100 & sat<30）

| 特征 | 数值 | 用途 |
|---|---|---|
| 平面 bbox | 771×354 px（斜视，约 2.18:1） | 定相机与平面比例 |
| 顶部剪影起伏 | 146 px = 宽度的 **18.9%**，全宽约 6 个波峰（12 次方向变化） | 定高度幅度 ≈ 2 单位平面上 0.38 |
| 逐列垂直跨度 | 均值 212 px = 宽度的 **27.5%** | 定相机俯角 |
| 亮度 p2/p50/p98 | 194/251/254，`dark_ratio = p2/p50 = 0.77` | 暗部很浅，cavity 要弱、阈值化 |
| 梯度 mean/p95/p99 | 4.5 / 18.3 / 77.4 | 平滑低频，高梯度只在剪影 |

参考图是 Blender 5.3 真位移（True Displacement，Noise Scale 3 / Detail 2 / Distance 1）的白色
clay 视口：低频起伏、无石缝碎块、无深黑裂缝。据此新增 `hf_scene.py --hm smooth` 预设（3 个整
周期 ≈ 6 波峰、峰谷约 22% 宽度），并给 cavity 加阈值（低于 5% 高度范围的凹陷不压暗）。

### 9.2 方案 A：边缘封口

实现两层（`eevee_heightfield_trace.bsl.hh`，参数 `hf_edge_extend`）：

1. **横向钳边**：射线盒横向外扩 `hf_edge_extend` 个纹素，越界 uv 由 `hf_fetch_level` 的 clamp 取
   边缘单元高度。
2. **屏幕空间 skirt**：射线横向离开纹理域仍未命中时，若它「进入侧壁」（该处 z 低于边缘单元
   高度），把边界交点投影为命中，使剪影跟随边缘高度。

同时修掉一个**掠射角致命问题**：域外采样此前被当作实心侧壁，掠射相机射线一进盒就在边界
「命中」夹紧高度 → 整个画面变平板。现在域外视为空气（`inside` 判定，0.5 纹素内缩），射线继续
前进到真实表面。

| 检查点 | 结论 | 数据（960×540，32 spp，24 步，背光） |
|---|---|---|
| 边缘封口（正/斜视） | **PARTIAL** | 特写差值 mean 3.33、max 113、18% 像素变化；边缘条带被填但呈「窗帘」状侧壁，非真实薄边 |
| 边缘封口（掠射） | **FAIL** | 差值 mean 2.83；剪影仍基本为直线，见 9.4 |

方案 A 的天花板：屏幕空间只能把「载体矩形内」的像素投影到边缘点，无法新增矩形外的剪影像素。
真实薄边仍需方案 B（skirt 几何随 prepass 光栅化）。把 `hf_edge_extend=300`（等价无限外延）实测
会把整个掠射画面压成平板（外延平台先被射线命中），已记录为反例，默认值保持 1。

### 9.3 SPOM 式像素级自遮蔽 / 接触暗部

命中点取 `h0` 与两级粗 mip（4×4 / 16×16 纹素的最大高度）比较，凹陷亏空归一化后乘
`hf_cavity`，压到 AO 分量：

```glsl
deficit = (0.35*max(h_near-h0,0) + 0.65*max(h_far-h0,0)) / height_range
ao *= 1 - clamp((deficit - 0.05) * hf_cavity, 0, 0.9)
```

阈值 0.05 保证平面/凸起完全不压暗（非整体变暗）；峰值处 `deficit=0`。由 `hf_cavity` 控制强度。

| 检查点 | 结论 | 数据（ridge 高度图，cavity 0 → 1.5） |
|---|---|---|
| SPOM 遮光（正/斜视） | **PASS（局部）** | mean 0.594，变化像素 mean 5.47、max 37、6.7% 像素、全部为变暗；峰值像素不变 |
| SPOM 遮光（掠射） | **PARTIAL** | mean 0.339、max 37、4.0% 像素；因掠射几何本身未收敛，效果弱且被条带噪声掩盖 |

### 9.4 掠射角（独立验收项）

`--cam grazing`：距原点 5.94 m、仰角 8.6°、85 mm。

| 项 | 结论 | 证据 |
|---|---|---|
| 剪影随高度起伏 | **FAIL** | 上边缘仍基本平直，仅远处 1–2 个峰突出 |
| 表面细节 | **FAIL** | 近半屏近似平板 + 斜向条纹，24 步未收敛（掠射下射线与表面近切） |
| 是否露平板 | **FAIL（如实）** | 这是屏幕空间高度场在近切视角的固有限制，不是参数问题 |

### 9.5 性能（1920×1080，1 TAA 样本，进程内计时，16 帧中位数）

| 配置 | 帧时间 | 相对 OFF |
|---|---|---|
| OFF | 78.8 ms | — |
| ON（steps=24、cavity 1.5、skirt、阴影+AO） | 83.5 ms | **+4.7 ms（+6.0%）** |

绝对帧时间 83.5 ms ≈ 12 fps，**1080p 实时 FAIL**（与 8.3 同因：本机基线 EEVEE 帧本身约 79 ms）。
pass 增量 4.7 ms 可接受。

### 9.6 证据

[hf_evidence_round2.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/hf_evidence_round2.png)
（3×3：正视图 cavity / 边缘特写 skirt / 掠射 skirt，各 before / after / 差值×4）。
新增工具 `tests/heightfield/hf_diff.py`（PIL+numpy，输出 mean/p95/p99/max/变化像素与差值图）。

## 10. 第三轮：真几何 skirt、保守 mip 步进、掠射分档、剪裁、TAA

第三轮的出发点是上一轮结论：屏幕空间的仿射射线只能在「载体矩形已光栅化的像素内」移动深度，
矩形外一个像素也长不出来，掠射角近切时尤其明显。因此本轮把「薄边」交给真实几何，把「条纹」
交给步进算法与采样，并逐条验证后决定采纳或否决。

### 10.1 五条思路的裁决

| # | 思路 | 裁决 | 依据 |
|---|---|---|---|
| 1 | 真几何 skirt（边界边逐纹素起伏、外扩+下压、单面薄片进 prepass） | **采纳** | 侧视剪影由一刀切直边变真实起伏轮廓，见 10.2 表；掠射 8.5° 远端由直角切边变锯齿轮廓 |
| 2 | 最大/最小 mip 保守区间步进（cone/RCSM 式安全跨越 + 命中后降级） | **采纳（相机射线）**；「8–12 步」**否决** | 区间步进本身成立（见 10.3），但 12 步图像误差 4.10 ≫ 24 步 0.72，耗时只差 1.3 ms，砍步数不划算 |
| 3 | 掠射角单独渲染路径（2D 屏幕空间追踪 / 极掠射解析近似） | **否决硬分档，改为连续混合** | 硬切会在阈值处产生一整条不连续带；改为按 `sin(仰角)` 连续混入粗 mip 法线 |
| 4 | 半透明剪裁（按到边缘距离裁 alpha） | **否决** | 剪裁只能改已光栅化像素的 alpha，无法新增矩形外像素，与方案 A 同一物理上限；且需把 alpha 回写 deferred combine，成本高、收益为零 |
| 5 | 每像素抖动 + TAA 累积 | **采纳（辅助）** | 压噪有效（10.4 表），但压不掉结构性台阶条纹；1 spp 下条纹仍可见 |

**思路 3 的分档阈值与不连续处表现**：已知 `sin_el = |d.z| / |d|`，阈值 `hf_grazing_sin = 0.18`
（约 10.4°）。权重 `w = clamp((0.18 - sin_el) / 0.09, 0, 1)`：`sin_el ≥ 0.18`（仰角 > 10.4°）完全用
解析法线；`sin_el ≤ 0.09`（仰角 ≤ 5.2°）完全用 level 2 粗法线；中间线性过渡。实测 8.5° / 8.6° 落
在过渡区中部（w≈0.5），画面**没有**可见的硬边或色带，代价是掠射区法线被平滑（细节下降）。

### 10.2 真几何 skirt（思路 1）before/after

skirt 在 `hf_scene.py::build_skirt` 里按四条边界逐纹素建段：每条边 256 段、每段 2 个三角形
（共 2048 tris / 4096 verts）。顶点 `top = height[edge] * max_height - delta`，即**跟随边缘高度图
起伏**，向下挤到 `z_base = -0.02`、向外扩 `skirt_out`、顶部下压 `skirt_delta`。`visible_shadow = False`
（薄单面片进 VSM 会自阴影 acne）。

| 视角（1280×720，32 spp，24 步） | mean_abs | 变化像素比 | p95 | 结论 |
|---|---|---|---|---|
| front（3/4 视角） | 9.963 | 21.5% | 79 | PASS：边缘由直边变轮廓 |
| side（5° 侧视，85 mm） | 18.117 | 25.7% | 98 | PASS：剪影起伏可见，逐纹素台阶清晰 |
| skim（8.5° 掠射表面） | 8.693 | 12.4% | 83 | PARTIAL：远端轮廓有起伏，但近半屏结构条纹仍在 |
| grazing（8.6° 斜视） | 13.392 | 27.8% | 81 | PARTIAL：同上 |

证据图 1/2（side）与 3/4（skim）对比：skirt=0 是「刀切直边 + 横条纹」，skirt=1 出现随高度起伏的
真实轮廓，台阶就是纹素尺度（这是有意保留的真实几何，不是走样）。

### 10.3 保守区间步进（思路 2）

实现（`eevee_heightfield_trace.bsl.hh::hf_march`）：CPU 端在最大 mip 金字塔旁再建一份**最小 mip
金字塔**（`mip_offsets_min`，level 0 复用 max level 0）。shader 里对当前层级 cell 做区间判定：

- 射线在整个 cell 驻留期间都在 cell 最大高度之上 → 不可能相交，**整 cell 一步跳过**（父 cell 也安全时上升一层）；
- 射线在整个驻留期间都在 cell 最小高度之下 → 已在实心内部，直接早停命中（阴影/AO 射线用）；
- 否则降一层，并用 `t_height` 包络跳跃直接落到该层包络面，避免 1 纹素爬行；
- 到 level 0 时单纹素高度恒定 → **解析求交**，不再二分（`binary_steps` 已从 shader 调用中移除）。

初始层级由射线在可见长度内跨越的纹素数决定（`span_texels` → `floor(log2)`），这是「自适应」的部分。

步数收敛（960×540，16 spp，以 32 步为参考）：

| 步数 | 6 | 8 | 12 | 16 | 20 | 24 | 32 |
|---|---|---|---|---|---|---|---|
| mean_abs | 5.56 | — | 4.10 | 2.44 | 1.44 | 0.72 | 0 |

**结论：8–12 步不收敛（FAIL），固定 24 步保留。** 原因不是步长不自适应，而是掠射时射线仍在每个
层级上多次跨越 cell 边界；层级只降低「跳空区」的次数，不降低「沿表面滑行」的次数。

### 10.4 抖动 + TAA（思路 5）

`hf_march_jitter = 0.5` 纹素（起点相位抖动，小于 1 纹素，不会跳过交点）。skim 视角，24 步，以
32 spp 为参考：

| spp | 1 | 4 | 8 | 16 |
|---|---|---|---|---|
| mean_abs | 0.85 | 0.28 | 0.15 | 0.10 |
| rms | 3.58 | 1.19 | 0.71 | 0.45 |

**1 spp 下结构性台阶条纹仍在**（抖动只把点状噪声摊平，不改变命中层面的几何台阶）；16 spp 视觉
干净但仍有残留条纹。即「抖动+TAA」是减噪手段，不是解条纹的手段。

### 10.5 三个坑单独验收

| 坑 | 结论 | 数据 / 证据 |
|---|---|---|
| 裙边与屏幕空间深度 z-fighting | **PASS（需 delta > 0）** | `out=0, delta=0` 时 0.47% 像素（6768 px）在轮廓线上出现点状变暗（max 77，全部为变暗）；`delta=0.0014` 后消失。差值图见证据图 5，干净交界见 6 |
| 侧光下真阴影与假法线接缝 | **FAIL（新发现，未解决）** | 见下 |
| 裙边进 prepass 的光栅化开销 | **PASS（≈0）** | 1080p 1 spp：OFF 84.0 ms vs OFF+skirt 81.7 ms（在噪声内） |

**阴影条纹（FAIL）**：`--cam edge --sun-az 0` 下整片表面出现等高线状、点状虚线的暗条带。隔离实验：
`--shadow 0`（只开 AO）干净，`--ao 0`（只开阴影）条纹仍在 → 是**射线步进阴影**，不是 AO。根因是
阴影射线对 level 0 的纹素台阶做求交，台阶在近切光照下自阴影成带；换层级（`hf_shadow_level = 1`）
不是解法：变化像素 8.9%、`mean_abs` 2.27，且**全部为变暗**（max 76）→ 保守包络只会过度遮挡；
level 2 更差（12.2%、`mean_abs` 3.69，出现片状暗块）。同时把阴影步数从 `max(4, steps/2)` 提到
`max(8, steps)`，az0 前后只差 **3 个像素** → 说明它也不是步数预算问题。该修法已否决，作为已知
残余记录（10.7）。

另外，`visible_shadow = False` 使裙边不投 VSM 真阴影：侧光下墙体本身没有投影，只有高度场内部
的射线步进阴影。这是薄单面片（会 acne）与「要真阴影」之间的取舍，如实记录。

### 10.6 性能（1920×1080，1 TAA 样本，进程内计时，16 帧中位数、剔除首帧）

| 配置 | 帧时间 | 相对 OFF | 相对 OFF+skirt |
|---|---|---|---|
| OFF（平面载体，无 skirt） | 84.0 ms | — | — |
| OFF + skirt | 81.7 ms | −2.3 ms | — |
| ON，12 步，skirt | 88.5 ms | **+4.5 ms（+5.4%）** | +6.8 ms |
| ON，24 步，skirt | **89.8 ms** | **+5.8 ms（+6.9%）** | +8.1 ms |
| ON，24 步，无 skirt | 89.1 ms | +5.1 ms | — |

绝对帧时间 89.8 ms ≈ 11 fps，**1080p 实时仍 FAIL**（本机 EEVEE 基线本身 84 ms，与本功能无关）。
pass 增量 5.8 ms 可接受；12→24 步只差 1.3 ms，进一步说明 1 spp 下不是 step-bound。

### 10.7 已知残余（如实标注）

1. **掠射结构台阶条纹**：8.5–8.6° 下，命中层面本身是纹素台阶，1 spp 可见虚线状条纹，抖动+TAA
   只减噪。要根治需要屏幕空间 2D 追踪或对命中做亚纹素插值，本轮未做。
2. **3/4 视角侧向入射区的平板边带**：射线从侧面进入域内时未铺满，skirt 只覆盖最外一圈。
3. **侧光阴影等高线状条纹**（10.5）：层级提升不是解，留待下一轮（候选：命中点做纹素插值、
   阴影射线垂直于纹素阶面加权，或阴影改用屏幕空间 cone trace）。
4. **裙边不能做 shadow caster**：薄单面片进 VSM 会 acne，因此侧光下没有墙体投影。
5. **裙边台阶是真实几何**：1 纹素一阶，近距特写可见；这是「像真细分」的代价，非缺陷。

### 10.8 证据

[hf_evidence_round3.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/hf_evidence_round3.png)
（4×3：side/skim/grazing 的 skirt before·after、z-fight 差值点线、干净交界、az0 条纹 vs az90 对照、
TAA 1 vs 16 spp）。
构建全程 `flock /tmp/build_linux.lock` 串行，只编 `libbf_draw_shaders.a` / `libbf_draw.a` 再手工
重链（`/tmp/hf_link.sh`），未触碰物理节点线文件；未 commit、未用 sudo。

## 11. 第四轮：阴影步进、亚纹素插值、裙边遮挡补丁

### 11.1 阴影等高线条纹

上一轮定位到：仅开阴影有条纹、仅开 AO 干净 → 阴影射线在纹素台阶上的步进走样。本轮按四条机制改：

| 机制 | 实现 | 结果 |
|---|---|---|
| 起点抖动落在阴影射线上 | `hf_shadow` 里 `t_start = shadow2.z * texel_world * rand`，沿 `dir` 偏移（不超过 1 纹素，不会跳过遮挡物） | **FAIL**：条纹结构不变，只把硬边打散成点状噪声；`--shadow-jitter 0` vs `1` 有 9.2% 像素差异，但带型不变 |
| 进入包围盒后不直接判遮挡 | `hf_march` 的 "低于 cell min" 分支在 `l > min_level` 时先降一层继续精搜 | **PARTIAL**：判定改变（62k 像素变暗），条纹仍在（3/4 视角消融图一致） |
| 阴影最大追踪距离 | `t1 = min(t1, shadow2.x * texel_world)`，默认 32 纹素 | 远距离假暗条减少，性能 ~0；近处带型不变 |
| PCSS 半影（按遮挡距离衰减） | 每条射线贡献 `w = clamp(1 - t_occ / soft_dist, 0, 1)`，soft 默认 6 纹素 | **FAIL 作为去带手段**：只把暗带变成半色调噪声（2 条射线不足以收敛）；作为 PCSS 近似可保留 |

关键数据（edge 视角，1600×900，az0）：
- R4 默认 vs R3：`mean_abs 2.63`、13.5% 像素变化、42k 变暗、p95 24；
- R4 默认 vs R4 全机制关闭（8 spp）：`mean_abs 0.70`、9.2% 变化、62k 变暗，**带型完全一致**。

结论：条纹不是采样噪声，而是**射线与纹素台阶的分类边界**（4 号面板关阴影即干净，3 号特写是规则点阵）。
抖动只改变相位、不改变分类，所以 TAA 压不下去。真正的解需要换求交模型（对平滑曲面做 sphere
tracing，或阴影改屏幕空间 cone trace），本轮**否决抖动/PCSS 作为解法**，机制保留为可选参数
（`hf_shadow_jitter` / `hf_shadow_soft` / `hf_shadow_max`，默认 1 / 6 / 32）。

### 11.2 掠射纹素台阶

- 交点高度改为**双线性重建**（`hf_fetch_bilinear`，4 taps；仅用于表面重建与法线中心差分，
  max/min 包络仍用最近邻以保证保守性）；法线中心差分同样走双线性，避免只插值高度、不插值法线。
- 双三次**未上**（16 邻域 × 每像素，按用户判断会吃掉 6.9% 增量）；未做 ddx/ddy 自适应高阶路径，
  因为 11.3 的实测显示台阶来源在阴影而不是交点高度。
- 实测：skim `mean_abs 0.70` / 4.9% 像素变化 vs R3；grazing `0.094` / 0.78%；side `0.055` / 0.44%。
  面片轮廓与近距台阶几乎不变。**PARTIAL**：命中高度连续了，但掠射区可见的层状台阶由阴影造成，
  该改动无法消除。

### 11.3 裙边：不投 VSM、只写深度 + 边缘接触阴影

- 裙边保持 `visible_shadow = False`：不进 VSM 投射集合（避免薄单面片 acne），但仍在 prepass 里
  正常光栅化写深度 —— 第 10.5 节 z-fighting 对照证明屏幕空间求交能读到它的深度。
- 新增**边界接触阴影**：在边界内 12 纹素宽的带内，按光源水平方向朝向该边界的分量加权压暗
  AO（`shadow2.w`，默认 0.35），模拟「光穿过该侧墙体本应产生的投影」。
  实测：front + az225（光从近侧边来）`mean_abs 0.308`、3.3% 像素变化、全部变暗、max 32；
  side + az180 `0.043`、0.45%（侧视时带被极度压缩，像素少）。
  **PASS**，但只在「光朝向该边界」时出现，背光侧无效果（这是有意的，避免整圈变暗）。

### 11.4 渲染后端与基线构成（如实核查）

- `gpu.platform`：**vendor NVIDIA / renderer GeForce RTX 4050 Laptop GPU / driver 595.99.02 /
  backend VULKAN**，不是 llvmpipe 软件渲染；`/dev/dri/renderD128` 存在，`nvidia-smi` 正常。
- 测试场景特性：EEVEE Next，**无光追、无体积、无运动模糊、无景深**，1 TAA 样本，1 个 Sun + 世界光，
  载体 2×2 平面 + 裙边 2048 tris，1280/1600/1920 分辨率。
- 基线构成：同一配置 320×180 为 **25.4 ms**，1920×1080 为 **79.6 ms** → 约 25 ms 是每帧固定开销
  （进程内同步 / pass 调度 / Vulkan 驱动），其余约 54 ms 随像素数增长。所以「84 ms 基线」不是
  软件渲染，也不是本功能的开销。

### 11.5 性能（1920×1080，1 TAA 样本，16 帧中位数、剔除首帧）

| 配置 | 帧时间 | 相对 OFF |
|---|---|---|
| OFF | 79.6 ms | — |
| ON 12 步 | 91.0 ms | +11.4 ms（+14.3%） |
| ON 24 步 | **86.8 ms** | **+7.2 ms（+9.0%）** |

12 步反而比 24 步慢 4.2 ms，属 1 spp 同进程噪声（两轮均出现类似抖动）；绝对帧时间 86.8 ms ≈ 11.5 fps。

### 11.6 证据

[hf_evidence_round4.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/hf_evidence_round4.png)
（4×3：az0 条纹 R3/R4/特写/关阴影、skim 与 grazing R3·R4、side 边缘 AO、front az225 边缘 AO 开/关）。
构建仍 `flock` 串行、只编本线目标再手工重链；未 commit、未用 sudo。

## 12. 第五轮：SDF sphere tracing 阴影 + 亚纹素重建（2026-11-08）

第四轮的两条 FAIL 都是**求交模型**的问题，本轮不改步数 / 抖动幅度 / 射程参数，而是换模型。

### 12.1 阴影：保守距离场 + sphere tracing

高度面是图 `z = H(x, y)`，因此到面的距离存在一个解析下界：

```
phi(p) = (p.z - H(p.xy)) / lip,   lip = sqrt(1 + L^2),   L = 全程最大世界梯度
```

`L` 由 level 0 金字塔逐纹素的前向差分最大值得到（`HeightfieldPyramidCache::lip_texel`），
host 端换算成世界单位后放进 `HeightfieldData::sdf.x`。`phi` 在 `p` 上连续且处处为正于面外，
这正是 R4 的区间步进缺的性质：R4 的区间分类（全高于 max / 全低于 min）在斜向与掠射射线上
按纹素周期翻转，抖动只能把翻转打散成噪声，不能消除。sphere tracing 把翻转变成距离驱动的连续量。

实现要点（`eevee_heightfield_trace.bsl.hh`）：

- **两段式步进**：远场按 `phi` 前进（`t += max(phi, min(min_step, margin))`），不再用固定
  `min_step` 作硬下限。R4/早期 R5 的固定下限有两个方向相反的副作用：下限相对 penumbra 过宽时，
  掠射射线在起点附近就被判成"贴面遮挡"，产生大片假阴影；下限过窄时，长阴影又走不到。
- **penumbra 相**：一旦 `phi < margin`，改用 0.5 纹素细步并记录**最近接近点** `phi_min`
  （12 次上限，超过最近点即提前退出），而不是取"第一个落进 margin 的采样点"。阴影边缘的
  点状残差就来自后者的采样相位，`phi` 本身是连续的。
- **penumbra 宽度取物理锥**：`margin(t) = max(1 texel, tan(太阳半角) * t)`，不再复用 AO 半径，
  也不再与粗步长绑定。默认 `hf_shadow_cone = 0.026`（3° 太阳）。
- **单条确定性射线**：SDF 路径只用 1 条射线。两条抖动射线对 3° 锥只能给出 0 / 0.5 / 1 三档，
  在阴影内部留下椒盐噪声；锥宽已经由 `margin(t)` 解析给出，第二条射线只增加方差。
- **起点偏置**：`4 texel` 的**法线 + 光方向**双偏置。R4 的 `8 * eps`（≈16 纹素）沿光方向偏置会把
  采样点塞进坡体，产生假自阴影——这是 R4 "斜向条纹"被误判为阴影问题的直接原因。
- **penumbra 相用 C1 距离**：细步阶段改用双三次高度求 `phi`（`hf_surface_distance_smooth`）。
  双线性高度是 C0，其梯度逐纹素跳变，会让 `phi` 在每个纹素边界出现折点，sphere trace 把它
  变成**纹素周期的判定图案**（受光坡面上的细横条纹与锯齿阴影边；128 spp 不变，属结构性问题）。
  Catmull-Rom 是 C1，折点消失。16 tap 只在 penumbra 相付出，远场仍用便宜的 4 tap 双线性。

### 12.2 亚纹素重建

- 高度改为 Catmull-Rom 双三次（`hf_bicubic`，`a = -0.5`，4×4 = 16 tap），同时返回解析梯度。
- 法线 `hf_analytic_normal` 增加 `footprint` 参数：`level 0` 且 `footprint <= 1` 用双三次梯度；
  否则用 `step_uv = texel * clamp(footprint, 1, 8)` 的宽模板中心差分（按距离自适应过滤），
  不再堆步数。
- 相机命中点 `hf_refine_hit`：对双线性面做 2 次阻尼 Newton（每 0.25 纹素内停止），把命中点
  从纹素格上移开；梯度与高度用同一套插值，法线不再与命中点脱节。
- 同一套 footprint 由 `dFdx/dFdy` 与纹理尺寸推出，TAA 抖动与 SPOM / 真几何 skirt 组合不变。

### 12.3 消融（同视角 before/after，800×800，16 spp，`hf_diff.py`）

**亚纹素法线**（`az0` / 平滑图 `az90`，`--shadow 0 --ao 0`，只看法线重建）：

| 对比 | mean_abs | mean_changed | max | p95 / p99 | changed_px |
|---|---|---|---|---|---|
| az0：双线性 → 双三次 C1 | 4.015 | 30.96 | 68 | 38 / 51 | 82 720（12.93%） |
| az90：双线性 → 双三次 C1 | 1.514 | 30.326 | 122 | 2 / 52 | 31 774（4.96%） |

**margin 模型的证伪**（同一 `az30` 视角，`--ao 0 --shadow-jitter 0`）：

| 配置 | darkened_px | changed_ratio | mean_changed |
|---|---|---|---|
| 宽 margin（4 纹素，2 条抖动射线） | 105 526 | 16.49% | 26.151 |
| 紧 margin（`max(1 texel, cone*t)`，单射线） | 118 | 0.02% | 8.449 |

两行只差 penumbra 模型，被"变暗"的像素从 16.5% 掉到 0.02%：**R4 看到的大片"阴影"绝大部分是
近表面部分遮挡，不是真实投射阴影**。这也解释了为什么 R4 调射程 / 抖动幅度都无法收敛。

**真实投射阴影**（低太阳，lamp tilt 75° ≈ 15° 仰角；纯几何基线 `--shadow 0 --ao 0` 同光照）：

| 配置 | darkened_px | changed_ratio | mean_abs | mean_changed | max |
|---|---|---|---|---|---|
| interval march 24 步 | 74 | 0.01% | 0.001 | 4.797 | 14 |
| interval march 64 步 | 1 463 | 0.25% | 0.029 | 7.603 | 42 |
| **SDF sphere trace 32 迭代** | **8 699** | **1.36%** | 0.407 | 29.648 | 50 |

SDF 与 march 64 步在同光照下相差 9 653 px（1.51%）：**march 找到的阴影像素只有 SDF 的 1/5.4**
（1 463 vs 8 699）。迭代收敛在 az30/e20 对照配置上单独实测：32 迭代与 128 迭代输出逐像素一致
（max 0），说明 SDF 侧不是靠加迭代数堆出来的。

其余视角（SDF vs 同光照纯几何）：

| 视角 | changed_px | changed_ratio | mean_changed | p95 / p99 |
|---|---|---|---|---|
| grazing | 163 | 0.03% | 10.233 | 0 / 0 |
| skim（近切面） | 48 185 | 7.53% | 40.808 | 45 / 49 |

grazing 下 march 64 步为 254 px、SDF 为 163 px，两者都在 0.05% 量级，无回归。

**收敛与残留**：15° 太阳下 16 spp 与 128 spp 的 SDF 结果只差 0.71% 像素（mean_abs 0.082），
阴影边缘已基本收敛；残留的是硬本影边缘约 1 像素的阶梯（3° 太阳在几十纹素距离上的物理半影
本就小于 1 像素），不再是 R4 那种斜向点阵 / 等高线。减小它只能加宽太阳角或提高分辨率，
不属于求交模型问题。

### 12.4 性能（1920×1080，1 TAA 样本，16 帧中位数、剔除首帧）

| 配置 | 帧时间 | 相对 OFF |
|---|---|---|
| OFF | 79.69 ms | — |
| ON SDF sphere trace 32 迭代 | 86.93 ms | +7.24 ms（+9.1%） |
| ON interval march 64 步 | 86.69 ms | +7.00 ms（+8.8%） |

SDF 与 march 的增量几乎相同（+7.24 vs +7.00 ms），但同光照下多找到 5.4 倍阴影像素；
单条确定性射线 + penumbra 相最多 12×16 tap 的代价被"少一条射线"抵消，性能可接受，因此
**没有转去做屏幕空间 cone trace**。

### 12.5 未实现 / 受限路径（如实标注）

- **屏幕空间深度缓冲 cone trace**：`UNSUPPORTED / BLOCKED`。卡点：本 pass 输出只有
  shadow/AO 两张 aux target，deferred 之后没有可读的 HiZ / 早期深度金字塔；更关键的是
  屏幕空间 trace 只能看到**已经重建的深度**，没有厚度与多方向法线可供锥积分，且 skirt 是
  `visible_shadow = False` 的零厚度单面片（不能用它做 caster）。因此只作为计划 B 记录，
  未伪造实现。
- **逐纹素真距离变换 SDF**：`UNSUPPORTED`。256² × 高度区间的 3D EDT 在内存与构建时间上不划算；
  本轮用的是 Lipschitz 保守下界。它的已知代价是本例 `lip = 4.12`：距离被整体缩小约 4 倍，
  所以 penumbra 宽度必须由太阳锥给出，绝不能拿"几个纹素"当 margin——12.3 的消融就是这个坑。
- **skirt 仍不是 shadow caster**：边缘阴影继续由 `hf_edge_ao`（`shadow2.w`）伪造，本轮未改。

### 12.6 与第四轮的差异

- R4：固定采样密度 + 抖动 → 条纹被打散成噪声；R5：换求交模型（保守距离场 + sphere tracing），
  条纹来源同时被改判为**法线 C0 跳变 + 纹理缩减走样**，双三次 + footprint 过滤后纯几何渲染干净。
- R4 的 `min_step` / `Max Trace Distance` / 抖动幅度在本轮全部退居次要（`hf_sdf_min` 默认改为
  4.0，只作为被封顶的步进下限，不再决定遮挡判定）。
- 回归：march 路径仍可切换并可用；`--ao`、TAA 抖动、真几何 skirt、SPOM 组合未改；
  未降低任何既有验收阈值，未伪造数据，未用零数组冒充未实现路径。

### 12.7 证据

[hf_evidence_round5.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/hf_evidence_round5.png)
（4×3：az0 双线性/双三次、平滑 az90 双线性/双三次、15° 太阳 关阴影/march64/SDF32/SDF32-128spp、
grazing 关/开、skim 关/开）。单图见同目录 `r5_*.png`。
长日志在 `/tmp/r5_*.log`、基准 JSON 在 `/tmp/r5perf/`，终端只保留摘要。

## 13. 验收脚本恢复与重跑（2026-11-08）

`tests/heightfield/hf_acceptance.py` 此前在 `scene.use_nodes` 处崩溃，整条验收链无法运行。
本节记录当前树里 compositor 的实际 API、脚本侧修复、重跑数字，以及每一处修复的归因。

### 13.1 当前树的 compositor 现状（只读确认）

- `Scene.node_tree` 已被**彻底移除**，不是改名。Blender 5.3 的合成节点组改挂在场景效果上：
  `scene.compositor_effects.active.node_group`；`scene.compositing_node_group` 只是过渡别名
  （6.0 移除）。`scene.use_nodes` 属性仍在，但它的 setter 不再创建/绑定节点树，所以崩溃点
  `scene.use_nodes = True` 之后的第一次 `scene.node_tree` 访问就是 AttributeError。
- 文件输出节点：`CompositorNodeOutputFile.directory` 取代旧的 `base_path`。
- 服务器上的当前 HEAD 就是 `325d3071`（物理节点线），说明这次 API 变更随该提交进入本树，
  **不是**高度场本轮改动引入的；本轮高度场改动只落在 `source/blender/draw/engines/eevee/*`
  与 `tests/heightfield/*`，没有任何 compositor 侧提交。

### 13.2 脚本侧修复（全部在 `tests/heightfield/`，未动引擎/着色器）

1. **compositor 访问**：`scene.node_tree` → `scene.compositor_effects.active.node_group`；
   关闭高度场那一遍改为遍历 `node_group.nodes` 里的 `OUTPUT_FILE` 节点并改 `node.directory`。
   若节点组不存在则回退到 `hf_scene.setup_passes()` 重建。
2. **相机矩阵**：`build_scene()` 之后 `cam.matrix_world` 仍是单位阵（未求值 depsgraph），
   参考射线从原点竖直发出、全部脱靶。补 `bpy.context.view_layer.update()`。
3. **CPU 参考步进盒**：`bmax.z` 曾传 `0.0`，但参考内部 `bmin/bmax.z` 由
   `height_to_z(min/max)` 生成，真正的薄盒问题出在 `max_height=0.02` 的零厚度平面上；
   现在 flat 与 disp 都统一用 `max_height=0.6`，与 shader 的遍历盒一致。
4. **`march` 的 `t_exit` 差一**：射线在 `t >= t_exit` 时在**下一次相交测试之前**就被判死，
   恰好落在遍历盒底面的表面（平面 z=0）永远测不到。改为允许到达 `t_exit` 但不越过，
   由下一轮在 `t_exit` 处做最后一次相交测试。修复前 flat 命中 110、disp 8965；
   修复后 flat 15258（= 有效像素）、disp 15255。
5. **`march` descend 死循环**：降层时错误地对 safe 像素做 `lv -= 1`，当所有 active 像素都
   不安全时永不收敛（验收曾 11 分钟 101% CPU 挂死）。改为只降 unsafe，并加层数上限。
6. **像素行朝向**：CPU 参考按 `v = row / res_y` 生成射线，得到的是自下而上的行序，而
   `load_exr` 已把 Blender 的像素缓冲翻成自上而下，两者上下镜像。改为 `v = 1 - row/res_y`。
7. **Z pass 编码**：Blender 的 Z pass 是**线性相机空间深度**，不是 1/z。旧脚本拟合
   `a*(1/view_z)+b` 得到负斜率；改成 `z_gpu = a*view_z + b` 后斜率 1.0000，残差 6.1e-5。
8. **Normal pass 解码**：Normal pass 存的是 `[-1,1]` 的原始分量（平面法线读回就是
   `(0,0,1)`），不是 `0..1` 编码。旧的 `rgb*2-1` 把所有法线整体转了约 90°。
9. **步数一致性**：`build_scene()` 的 `steps` 默认 12，而 CLI 与参考用 24；验收调用显式传 24。
10. **skirt 遮挡像素**：近边的真几何 skirt 侧壁会挡在载体前面，CPU 参考只建模载体、无法
    预测这些深度。用 OFF 渲染（平面 + skirt）反推遮挡带并从标定/比对中显式排除，
    数量写进结果（本次 3394 px），不静默丢弃。

### 13.3 重跑结果（256²，1 spp，`--timing 0`）

结果文件：`tests/heightfield/out/hf_acc_r6/results.json`。

| 检查 | 数字 | 结论 |
|---|---|---|
| depth_encoding | a=1.0000119, b=-5.45e-5, 最大残差 6.07e-5 | PASS |
| depth_vs_cpu | 10758 px；mean 0.0633，p95 0.4595，max 0.8631 | FAIL |
| normal_vs_cpu | 7340 px；mean 48.51°，p95 72.77°，max 124.18° | FAIL（见 13.5） |
| displacement_depth_delta | mean 0.3909，max 1.0641 | 记录 |
| shading_contribution | mean 0.0330，max 0.9503（阈值 mean>=1e-3） | PASS |
| disabled quality variant | max_abs_diff = 0.0 | PASS |
| disabled pristine scene | max_abs_diff = 0.0 | PASS |
| silhouette | changed 1106 px / 1.69%，supported=false | 未变 |
| timing | 本次 `--timing 0` 跳过 1080p/4K 计时 | 未跑 |

### 13.4 归因：是否本轮引入、是否影响既有结论

- **崩溃不是本轮引入**。`Scene.node_tree` 的移除来自物理节点线的 5.3 compositor 改造
  （HEAD `325d3071`）；本轮高度场没有提交任何 compositor 代码。
- 其余 9 处（参考盒、`t_exit`、死循环、行朝向、Z 编码、Normal 解码、步数、skirt 掩膜、
  相机求值）都是**测试脚本自身的既有缺陷**，此前从未跑到这些分支，所以从未暴露。
- 修复只改 `tests/heightfield/`，**没有改引擎与着色器**。第五轮已交付的掠射/阴影结论来自
  GPU 对 GPU 的消融（OFF/ON、SDF/march、16/128 spp），不依赖这条 CPU 验收链，
  因此结论不受本次修复影响；本次也没有降低任何阈值。

### 13.5 仍 FAIL / 无法用该通道验证的两层（如实标注）

**depth_vs_cpu（FAIL，卡在两层）**
- 16.0%（1725 / 10758）的像素在 ON 渲染里**深度完全没变**（与 OFF 逐位相同），
  即这些像素的 GPU trace 没有命中位移面，仍保留了 prepass 的平面深度。
- 在剩下 84%（9033 px）**确实发生位移**的像素上：mean 0.0026，p95 0.0094，max 0.2612。
  p95 仍高于 2e-3 阈值——CPU 参考用的是双线性 + 最大 mip 步进，GPU 命中后走
  `hf_refine_hit`（双三次 C1 表面）与逐纹素线性化，两者在脊线附近不是同一个面。
  这是**参考模型与引擎模型的差异**，不是把阈值放宽的理由；阈值保持 2e-3。

**normal_vs_cpu（无法用 Normal pass 验证）**
- GPU Normal pass 的法线均值 `(0.167, -0.089, 0.766)`，与平面法线 `(0,0,1)` 的平均夹角只有
  **9.70°**；CPU 解析法线均值 `(0.478, 0.064, 0.639)`。也就是说这个 pass 导出的是
  **载体平面的几何/着色法线**，而不是重建法线。
- 重建法线只存在 `geom_tx_`（`HeightfieldSample::override_normal`）并被 deferred 着色消费；
  `deferred_combine` 虽然把 `gbuf.layer.N` 改写成 `N_hf`，但当前树的 Normal 渲染通道
  没有把这个量导出成可供 Python 读取的纹理。要量化验证需要新增 AOV/debug 输出，
  属于引擎侧改动，本次按要求不动。
- 因此该检查标记为**该通道不可验证**，保留原始数字与阈值（p95 < 5°），不伪造 PASS。

**silhouette**：仍是 `supported=false`（本 pass 没有可导出的轮廓掩膜），数字照记。
**timing**：本次按 `--timing 0` 未跑 1080p/4K 计时；第五轮的 79.69 / 86.93 / 86.69 ms
已在 12.4 记录，本轮验收修复不涉及性能路径。

### 13.6 复现

```
cd /home/eridanusepsilon/Code/EBC/EBC5.3
flock /tmp/build_linux.lock ninja -C build_linux lib/libbf_draw_shaders.a lib/libbf_draw.a
cd build_linux && flock /tmp/build_linux.lock bash /tmp/hf_link.sh
cd .. && build_linux/bin/blender -b --python tests/heightfield/hf_acceptance.py -- \
  --outdir /tmp/hf_acc_r6 --res 256 --samples 1 --timing 0
```
长日志 `/tmp/hf_acc_r6.log`，结果与证据图已复制到
`tests/heightfield/out/hf_acc_r6/`（`results.json`、`depth_error.png`、`normal_error.png`、
`depth_delta_on_vs_off.png`、`on.png`、`off.png`）。

## 14. 第六轮：薄面片形态（去掉实体侧壁，2026-11-08 同日）

目标形态是「先细分、再被噪声扭曲的薄面片」：高度连续、法线平滑、轮廓收在面片自身边界上，
**不能出现可见的竖直侧壁或带厚度的边**。参考图是原生像素级置换（顶点级），我们没有那条路径，
所以本轮改的是我们自己这条 trace 路径的**命中/未命中语义**，不改求交、法线、阴影、SPOM、TAA。

### 14.1 成因：两种「厚度」来源

前一轮的「厚板」不是 trace 求交错了，而是两个独立来源：

1. **真几何 skirt 的侧壁**。`build_skirt` 在四条边上各建一道竖直墙，底在 `z_base=-0.02`，
   顶逐纹素跟随边界高度（最高 0.6）。它本来只为给 prepass 一个真实深度/轮廓，但在正视与掠射下
   读到的是**一整圈实体墙**（正视 640² 里四面都可见，侧壁面高 0.62）。
2. **载体平面在轮廓下的平条**。极平平面在掠射角下会在位移轮廓之外露出一条平台带，宽度约
   `h_edge / tan(elev)`：本场景 `h_edge` 最大 0.6、载体 2×2、256²（1 纹素 = 0.0078 世界单位），
   30° 仰角约 **1.04 世界单位（≈133 纹素）**，8.6° 掠射约 **3.97 世界单位（≈508 纹素）**。

### 14.2 两条候选的实测（都不采用）

| 候选 | 实现 | 实测 | 结论 |
|---|---|---|---|
| 刀片式 skirt（压成亚像素） | `--skirt 2`：`delta=0, outward=0, z_base=0` | 与 `--skirt 1` **肉眼一致** | 厚度线索是墙的**高度**，不是横向偏移；压掉横向偏移仍留整面墙 → 否决 |
| 加宽载体 + 边缘 clamp | `--carrier-band N`（域内缩 N 纹素、域外 clamp 边界高度，载体网格同步放大，不拉伸高度图） | band 8：平条仍然存在，还多出 trace 未命中的暗缝；band 32：变成平台帽，带内出现矩形伪影/孔洞 | 要盖住 8.6° 的约 508 纹素就必须铺成平台，和旧 `hf_edge_extend=300` 同一种失败 → 只能作可选开关，默认 0 |

因此这两条都不作为解；`hf_carrier_band`（`hf.shadow3.y`）保留为 opt-in，默认 0，
`hf_edge_extend` 在 skirt 或 band 生效时强制 0，避免两种轮廓补丁叠加。

### 14.3 采用的解：薄面片覆盖（thin sheet coverage）

语义：载体平面只是**光栅化/着色代理**，不是实体表面。一个像素如果 prepass 深度落在载体平面上
（`|base_local.z - bounds_min.z| <= params.w`），而相机射线**没有穿过位移面片**，那它看到的就是
面片边界之外的空气，不是「平面」也不是「墙」。

- 新参数 `hf_thin_sheet`（`hf.shadow3.z`，引擎默认 1，`hf_scene` 程序化默认 0，见 14.5）。
- 命中失败且 `carrier && thin` 时：`frag_depth = 0.0`（reverse-Z 的远值），
  `geom = (0,0,1,0)`、`shade = (1,1,1,0)`，让世界背景 pass（`DEPTH_EQUAL` 于清值）接管该像素。
  轮廓因此收在面片自身的图上边界，不再有平面平条。
- **必须同时改深度状态**：trace pass 从 `DRW_STATE_DEPTH_GREATER` 改为 `DRW_STATE_DEPTH_ALWAYS`，
  fragment 注解从 `[[frag_depth(greater)]]` 改为 `[[frag_depth(any)]]`。`greater` 会在驱动层
  强制比较，把「更远的清值」写回直接丢掉 —— 第一次尝试擦除时像素变黑（背景没接管）就是这个原因，
  改成 `1.0f` 更糟（完全无洞），最后定位到测试状态而不是深度约定。
- **ALWAYS 会丢掉隐式遮挡保护**（旧 `greater` 会挡住画在实体前面的更远深度写入）。因此 shader 内
  显式补回：`frag_depth = carrier ? (1 - screen_depth + 1e-6) : (1 - base_depth)`。
  只有载体像素拥有深度，墙/其它前景几何保持 prepass 深度。
- 场景侧 `tests/heightfield/hf_scene.py`：`--skirt 0/1/2`、`--carrier-band`、`--thin`（命令行默认 1）。

### 14.4 与上一轮的差异

- **未动**：`hf_march` 的保守区间步进、`hf_refine_hit`、`hf_bicubic`、`hf_analytic_normal`、
  `hf_sphere_trace`（SDF 阴影）、SPOM 组合、TAA 抖动与采样；没有改任何阈值、没有降采样、
  没有拿参考实现的原生置换图充数。
- **动了**：未命中分支（覆盖语义）、载体域可选内缩、trace pass 的深度状态与 carrier 门控。
- **观感**：正视里四面墙消失，面片轮廓就是位移图的边界；掠射前图里那道起伏的轮廓线本来就是
  skirt 墙顶的剖面（墙高跟随边界高度），因此它消失是目标形态的一部分，不是 trace 面片丢失。
  掠射后图仍是位移面片本体（8.6° 下高度差在屏幕上被压扁，面片读作薄板而不是波形剖面）。
- **残留**：命中失败像素现在被显式擦成背景，已知的 trace 漏命中（14.5）沿脊线切点露出为
  抖动黑斑/暗缺口。这是下一项待办要修的引擎 bug，不是本轮形态改动引入的新错误。

### 14.5 对验收链（`depth_vs_cpu` 那条 trace 漏命中）的影响

验收脚本的程序化入口保持 `thin=0`（`build_scene` 的 Python 默认值；命令行默认仍是 1），
所以 r6 → r7 的基线**逐项未变**：

| 检查 | r6 | r7（thin=0） | 变化 |
|---|---|---|---|
| depth_encoding | a=1.0000119, 残差 6.07e-5 | 同 | 无 |
| depth_vs_cpu | 10758 px；mean 0.0633，p95 0.4595 | 同 | 无 |
| normal_vs_cpu | mean 48.51°，p95 72.77° | 同 | 无 |
| displacement_depth_delta | mean 0.3909，max 1.0641 | 同 | 无 |
| shading_contribution | mean 0.0330，max 0.9503 | mean **0.0036**，max 0.4797 | 仍 PASS（阈值 mean>=1e-3 未动） |
| silhouette | 1106 px / 1.69% | 同 | 无 |
| disabled 两项 | max_abs_diff 0.0 | 同 | 无（关闭路径仍严格 no-op） |

`shading_contribution` 的下降有明确归因：该检查比较「开阴影/AO（24 步）」与「关阴影/AO（12 步）」。
旧 `greater` 下 12 步那一版在 misses 处保留平面深度，差值里混进了几何差；现在 12 步的命中也会
落到面片上，指标改成只反映真实的阴影/AO 贡献（仍非零）。SDF 阴影本身未改：15° 仰角
（`--sun-elev 75 --sun-az 30`，`edge` 视角，640²，16 spp）阴影开/关仍有 **185 px 变暗、max 16**，
与第五轮掠射量级（163 px）一致。

如果**打开 shipped 默认** `hf_thin_sheet=1`，被擦除的漏命中像素会写成远深度：
`depth_vs_cpu` 的 p95 与 `displacement_depth_delta` 的 max 从 0.4595 / 1.0641 变成 **1e10**，
pass/fail 状态不变（本来就 FAIL）。这正好把 13.5 记的「16.0%（1725/10758）ON 后深度完全没变」
从「静默保留平面」变成「显式远值」——**同一条引擎 bug，更可见**，不是参考实现差异。
修好命中之前，验收用的基线继续跑 `thin=0`，形态验收用 `--thin 1` 单独出图。

### 14.6 证据与复现

改前/改后（同一机位、同一光照、同一 `smooth` 噪声高度图、同一二进制，只切 `skirt`/`thin`）：

- [r6_thin_sheet_before_after.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r6_thin_sheet_before_after.png)
  （2×2：正视 + 掠射 8.6°，左列 `skirt=1 thin=0`，右列 `skirt=0 thin=1`）
- [r6_thin_sheet_edge_zoom.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r6_thin_sheet_edge_zoom.png)
  （左边框 3× 局部放大：改前是整面竖墙，改后只有面片边界与背景）
- [r6_thin_sheet_candidates.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r6_thin_sheet_candidates.png)
  （刀片 skirt、band 8、band 32 三个否决候选）

```
cd /home/eridanusepsilon/Code/EBC/EBC5.3
flock /tmp/build_linux.lock ninja -C build_linux lib/libbf_draw_shaders.a lib/libbf_draw.a
cd build_linux && flock /tmp/build_linux.lock bash /tmp/hf_link.sh
cd .. && build_linux/bin/blender -b --python tests/heightfield/hf_scene.py -- \
  --res 640 --samples 32 --hm smooth --edge-ao 0 --cam front --skirt 1 --thin 0 --out before.png
build_linux/bin/blender -b --python tests/heightfield/hf_scene.py -- \
  --res 640 --samples 32 --hm smooth --edge-ao 0 --cam front --skirt 0 --thin 1 --out after.png
```

长日志在 `/tmp/hf_form/`（`acc_r7c.log`、`r_*.log`），验收结果
`tests/heightfield/out/hf_acc_r7/results.json`。

### 14.7 待办（按用户要求，本轮之后再做）

1. **修真实引擎 bug**：`depth_vs_cpu` 的「16% 像素 ON 后深度完全没变、GPU trace 未命中位移面」
   不是参考实现差异，要修（本轮只在 `thin=1` 下把它显式暴露出来）。
2. **`normal_vs_cpu` 量化**：需要新增 AOV/debug 输出把重建法线导出，当前 Normal pass 只给
   载体几何法线（13.5），本轮未动。

## 15. 第七轮：trace 漏命中根因与修复（2026-11-08 同日）

### 15.1 定位方法：把 miss 路径写进深度

`hf_march` 不再返回裸 `-1`，而是返回 `-1 - code`（`hf_miss(code)`）；main 里把
`miss_path = 4 + code` 记下，再用物体属性 `hf_debug_reason`（`hf.shadow3.w`）当作选择器：
当 `int(miss_path) == 选择器` 时把该像素的 `frag_depth` 写成 0（远值）。于是**一次 Z pass
渲染只统计一条路径**，路径集合与 `compare`/`miss` 掩码都是同一份 r7 基线，逐条对得上。

用 `hf_scene.build_scene(..., debug_reason=N)` + Z pass 回读即可复现整张表
（脚本 `/tmp/hf_path.py`，`render N` 与 `tally` 两个子命令）。

| 选择器 | 路径 | 命中 miss 数 | 占 miss |
|---|---|---|---|
| 1 | 载体门控拒绝（不是载体像素） | 0 | 0.0% |
| 2 | 射线 box 拒绝 / `t1 <= t0` | 0 | 0.0% |
| 3 | march 返回 t 但被 `t >= best_t` 丢弃 | 0 | 0.0% |
| 5 | miss code 1：入口就 `t >= t_exit` | 0 | 0.0% |
| 7 | miss code 3：层级未收敛（`!advanced`） | 0 | 0.0% |
| 8 | miss code 4：`on_exit` 第二次触边 | 0 | 0.0% |
| 9 | miss code 5：步数耗尽 | 1725 | 100.0% |
| 13 | 空气入口：入口参数 `<= 1e-6`（前沿接触） | 1276 | **74.0%** |
| 14 | 空气入口：入口在射线后方（两个 slab 不重叠） | 0 | 0.0% |

（`stray_on_compare = 0` 对每一条都成立：选择器只标真正的 no-hit 像素，不会误标已命中像素。）

### 15.2 成因一（占 74%）：空气入口把「落在前沿上」当成「从未进入」

`hf_air_entry` 用 `(t0 > 1e-6f && t0 <= t1)` 判定是否进入 inset 定义域。射线本来在域外时，
这一步把射线**精确吸附到 inset 前沿**（`uv = uv_inset` 或 `1 - uv_inset`）。下一轮由
`p = o + t * d` 反算 `uv`，浮点误差可能让它落在前沿外侧一个 ulp 上，于是又进 `!inside` 分支；
此时 `t0 ~ 0` 被 `> 1e-6` 的守卫拒绝，直接返回 miss。**落在前沿上不是「没有入口」，而是入口本身。**

最小可复现场景：默认 `front` 机位（相机 (2.6, -2.6, 2.1)，50 mm，256²，`hf_steps=24`），
射线从 z = 0.6 顶面进入 box（`t0 ≈ 0.71…0.98`），先沿空气前进、吸附到前沿，然后被误判。
命中这类的像素在 `compare` 里 1276 个，占当时全部 miss 的 74.0%。

### 15.3 成因二（修掉成因一后的全部残留）：步数预算。

修完成因一后剩下的 510 个 miss 全部是 `hf_miss(5)`，再拆分后 **100% 是「预算在到达
遍历终点之前耗尽」**（不是「到达终点未相交」）。空间统计显示它们沿**脊线**成带状分布
（见图）：掠射段贴着脊顶走，max 金字塔的保守区间测试在几乎每个 cell 都判「不安全」，
只能逐纹素走，24 步覆盖不了整条脊线跨度。这是 max 金字塔步进的固有保守性，
不是本次改动引入的。

### 15.4 改法

1. `hf_air_entry` 返回诊断码（`-1` 平行轴在外、`-2` 前沿接触、`-3` 入口在后方），
   由调用方映射成 miss 码 —— 只为定位，最终仍合并为 miss。
2. **修复**：`inside` 测试加 `1.0e-5`（约 0.0025 纹素）的 uv 容差。吸附到前沿后
   下一轮一定被判为「在域内」，走正常步进；射线不会在分支间来回。
3. 保留上一轮已做的边界约定：盒 `t1 = min(t1, 1 + 1e-3)`、步进用 `t = min(t + t_cell, t_exit)`、
   `on_exit` 只允许一次触边访问。选择器 5/8 全为 0，说明这几条已经生效。

### 15.5 改前 / 改后（同一二进制之外的唯一变量是这两处改动，`thin=0`，24 步）

| 指标（`compare` = 10758 px） | 改前（r7） | 改后（r8） | 变化 |
|---|---|---|---|
| miss（ON 后深度逐位未变） | 1725 = **16.035%** | 510 = **4.741%** | −3.4× |
| `depth_vs_cpu` mean | 0.06335 | **0.01827** | −3.5× |
| `depth_vs_cpu` p95 | 0.45948 | **0.01738** | −26.4× |
| `depth_vs_cpu` max | 0.86305 | 0.86305 | 不变（单像素极值） |
| 仅命中像素 p50 / p95 / max | 0.0007 / 0.0093 / 0.2612 | 同 | 不变 |

阈值一条未动（`depth_vs_cpu` 仍要求 p95 < 2.0e-3，仍然 **FAIL**：4.7% 的残留在 24 步下
无法压到 5% 以下，p95 落在 0.017）。`normal_vs_cpu` 也未动（mean 48.51° / p95 72.77°），
原因是 Normal pass 收到的仍是载体几何法线而不是 trace 的解析法线（13.5），属于 AOV 接线，
留到量化通道那一项。

关闭路径未改：`quality.z & 1 == 0` 时不进 `hf_march`，`hf_debug_reason` 默认 0 时选择器
分支也不可能触发，disabled 两项仍是 `max_abs_diff = 0.0`。

### 15.6 结论：`thin=1` 还不具备默认值条件

残漏像素沿脊线成带，`thin=1` 会把它们擦成远值 —— 即用户看到的就是**沿每条脊线切点的
暗斑/针孔**，只是从「静默保留平面」变成「显式可见」。因此：

- **不设默认**：`eevee_heightfield.cc` 的 `hf_thin_sheet` fallback 从 1 改回 **0**，
  shipped 默认继续保留平面深度（静默、不产生孔）。
- 还差什么：把脊线掠射段的 miss 压到「擦除不可见」量级（经验上 <0.5%）。可选路径：
  (a) 用可靠的「最近接近」接收器把预算耗尽但仍在两纹素内的射线判成命中；
  (b) 换成能整段跳过脊线的加速结构（如沿射线的 min-of-max 剖面/地平线），
  代价都不小，且 (a) 会引入 <= 0.01 量级的深度误差，需要单独验收。
  在做到之前，`thin=1` 只能作为实验开关（`--thin 1`）出形态图，不能进默认。

### 15.7 证据

- [r8_miss_map.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r8_miss_map.png)
  （左上：`depth_vs_cpu` 误差热图（0–0.02 拉伸）；右上：miss 掩码（红点）；下方：最密 64×64
  窗口 4× 放大。红点沿脊线成带，是 15.3 的形态证据）

复现：

```
build_linux/bin/blender -b --factory-startup -noaudio --python /tmp/hf_path.py -- render 13
build_linux/bin/blender -b --factory-startup -noaudio --python /tmp/hf_path.py -- tally
```

诊断脚手架（`miss_path`、`hf_debug_reason`、选择器分支）在量化通道（normal AOV）轮统一删除；
它们只在 `hf_debug_reason > 0` 时改变输出，默认路径逐位等价。

## 16. 第八轮：边界 taper 收口，去掉侧面墙（2026-11-08 同日）

### 16.1 为什么上一轮「薄面片」在用户那里仍然有侧面

14.1 已经证明：把深度沿视线推走只改变**面的位置**，不改变**轮廓形状**。去掉真实裙边后，
载体平面本身仍然是一个 2×2 的矩形，`thin=1` 只能擦掉「射线没打中面片」的像素。
15.6 因为脊线切点黑斑把 `thin=1` 退回 **0** 之后，这些没打中的载体像素又恢复成平面深度 ——
侧看时，平面在轮廓下方露出一条**平的延伸带**；正视时裙边墙本身就是一圈竖直面。
两者都是「体积感」的来源：前者是平面条带，后者是裙边几何。

根因不在 trace 的命中判定，而在**面片没有在载体边界收口**：高度图在边界处的值非零
（smooth 预设归一化到 `[0.06, 0.94]`，边界约 0.2–0.5），面片在边界处悬空结束，
只能靠裙边或平面条带去补轮廓。

### 16.2 改法：把 taper 烘焙进高度场

在 `HeightfieldModule::cache_get` 读取原图后、构建 max/min 金字塔和 Lipschitz 常量**之前**，
对高度做边界衰减：

```
d = min(x, w-1-x, y, h-1-y)          // 到最近边界的纹素数
t = clamp(d / taper, 0, 1)
w = quintic(t)                       // 最外圈纹素恰好为 0
level[x,y] *= w
```

关键点：

1. **必须在金字塔之前改**。若只在 `hf_fetch_*` 里乘 taper，max 包络描述的是未衰减场，
   而表面是衰减场，`min` 包络会偏高 →「已经在实体内」的判定会过早命中，边界出现高度台阶。
   烘焙后 trace、解析法线、SDF 阴影、AO 全部看到同一张曲面。
2. `taper` 与 `taper_mode` 加入 `HeightfieldPyramidCache` 的缓存身份（同图不同 taper 各建一份，
   避免两个物体共享图像时互相洗掉金字塔）。默认 0 → 完全不进入衰减循环，逐位等价。
3. 最外圈纹素严格为 0，双线性在 `uv=0/1` 处取到的就是 0：面片在载体边界**正好落到平面**，
   轮廓与载体矩形重合，不再需要 skirt / carrier band / 屏幕空间边缘投影撑边。
4. 形态模式同时要求 `skirt=0`、`hf_edge_extend=0`、`hf_edge_ao=0`；`thin` 可以保持 0，
   因为轮廓已经收口，不再需要擦除来隐藏平面条带。

曲线对比（`taper=16`，front，`thin=0`）：linear 在 `d=0` 处斜率不连续，边缘仍有 1 纹素宽的
小楔形；smoothstep 是 C1；quintic 在两端值和斜率都为 0，侧看最不会留下「边缘竖条」，
所以选 **quintic**。宽度对比（front，`thin=1` 孔洞占比）：8 / 16 / 32 纹素 = 10.30% / 9.59% / 8.39%；
32 纹素会把 12.5% 的可用起伏削成平坡，16（6.25%）是观感与可用面积的折中。

### 16.3 量化：轮廓像素是否只属于面片

判据：把 Z pass 反投影回世界坐标，统计前景像素落在载体矩形 `|x|,|y| <= 1`
（容差 2 mm）之外的比例。只有载体平面和（可选的）裙边两种几何，矩形外的像素必然是侧面墙。

| 视角 | 改前 `skirt=1, taper=0` | 改后 `skirt=0, taper=16` |
|---|---|---|
| 正视 45° | 59.07% | **0.00%**（0 px） |
| 掠射 8.6° | 98.86% | **0.00%**（0 px） |
| 侧视 5° | 100.00% | **0.00%**（0 px） |

改后三个视角的 z 都在 `[-0.0015, 0.565]`（下界是 1 mm 深度重建噪声），`|x|max / |y|max <= 1.001`
（1.001 是平面边缘的抗锯齿像素），即**所有前景像素都属于载体矩形内的同一张面片**。
smooth 预设下 `thin=0` 的「载体像素显示为背景」只有 116 / 61024（0.190%）、
124（0.488%）、36（0.239%），全部是载体边界的亚像素抗锯齿，不是孔。

### 16.4 `thin=1` 的结论：仍然不具备默认值条件

taper 解决了形态，但没有解决 trace 的脊线切点漏命中。同一配置下把 `thin` 打开，
载体像素显示为背景（含约 0.19%–0.49% 的边界抗锯齿，`thin=0` 时同样存在）：

| 视角（taper=16，24 步） | 显示为背景 | 占载体 |
|---|---|---|
| 正视（smooth） | 4755 | 7.79% |
| 掠射 8.6°（smooth） | 163 | 0.64% |
| 侧视 5°（smooth） | 36 | 0.24% |
| 正视（ridge） | 5851 | 9.59% |
| 正视（ridge，48 步） | 2717 | 4.45% |

孔洞沿脊线/山脊切点成带（[r9_form_thin1_holes.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r9_form_thin1_holes.png)），
正视最严重、侧视几乎为零（侧视时载体在屏幕上的有效面积小）。因此：

- **`thin=1` 仍不设默认**，`eevee_heightfield.cc` 的 `hf_thin_sheet` fallback 保持 **0**。
  形态不再依赖 `thin`，所以「默认值」这条不再是形态的拦路虎。
- 要让它可默认，还差：把正视的切点漏命中压到 <0.5%（24 步）。可选路径仍是 15.6 的
  (a) 最近接近接收器 / (b) 整段跳过脊线的加速结构；两者都需要单独验收深度误差。
- 不降阈值：`depth_vs_cpu` 仍要求 p95 < 2.0e-3；本轮 `taper=0` 的 acceptance `r8`
  与 15.5 完全一致（mean 0.01825 / p95 0.01733 / max 0.86305，仍 FAIL），
  `normal_vs_cpu` 仍 FAIL，`shading_contribution` 仍 PASS，disabled 两项仍为 0.0。
  `taper` 默认 0，所以关闭路径和既有 SDF 阴影 / 掠射结论不受影响。

### 16.5 证据

- [r9_form_before_after.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r9_form_before_after.png)
  （上排：改前 `skirt=1, taper=0`，正视 / 掠射 8.6° / 侧视 5°，可见实体箱壁；
  下排：改后 `skirt=0, taper=16 quintic`，三个视角都收成一张面片）
- [r9_form_after_side.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r9_form_after_side.png)
  与 [r9_form_silhouette_zoom.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r9_form_silhouette_zoom.png)
  （侧视轮廓 3× 放大：改前是竖直墙带，改后是收口斜坡）
- [r9_form_border_zoom.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r9_form_border_zoom.png)
  （正视左边界 2×：改前墙+平面，改后 taper 落在平面上）
- [r9_form_thin1_holes.png](file:///home/eridanusepsilon/Code/EBC/EBC5.3/tests/heightfield/out/r9_form_thin1_holes.png)
  （同一山脊区域 `thin=0` vs `thin=1`，黑斑即擦除像素，支持 16.4）

侧视 5° 的远端角上有一根细亮条：它是 taper 后**贴平面的面片本身**被边缘看的结果，
`thin=1` 下仍然亮（说明是命中而不是漏命中写到远值），投影也落在载体矩形内，不是竖直面。
近边界的同心条纹是既有的 SDF 阴影半影台阶（12.5 / 13.5 的 FAIL），本轮没有改动它。

复现（不 commit，构建仍走 flock）：

```
flock /tmp/build_linux.lock ninja -C build_linux bin/blender
build_linux/bin/blender -b --python tests/heightfield/hf_scene.py -- \
  --res 640 --samples 32 --cam side --hm smooth --skirt 0 --edge 0 --edge-ao 0 \
  --taper 16 --taper-mode 2 --thin 0 --out /tmp/r9_after_side.png
build_linux/bin/blender -b --python /tmp/hf_form_check.py -- /tmp/chk side smooth 0 0 16 0
```


