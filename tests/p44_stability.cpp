// P4.4 stability: 100 rebuilds of a graph that owns an asset reference and
// three UI elements, then a mixed reload/rebuild pass, all under ASan.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_AssetRegistry.h"
#include "KX_CompiledGraph.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

/* The B5.2 source is being written by a parallel line; stub it so the harness
 * links without that translation unit. */
void RegisterB5FunctionGameNodes() {}

namespace {

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

class MockUI : public KX_UIAdapter {
 public:
  int canvases = 0;
  int labels = 0;
  int images = 0;
  int buttons = 0;
  int destroyed = 0;

  int CreateCanvas(const std::string &) override { return 100 + canvases++; }
  int CreateLabel(int, const std::string &) override { return 200 + labels++; }
  int CreateImage(int, const std::string &) override { return 400 + images++; }
  int CreateButton(int, const std::string &) override { return 300 + buttons++; }
  bool DestroyElement(int) override
  {
    ++destroyed;
    return true;
  }
  bool SetAttribute(int, const std::string &, const KX_RuntimeValue &) override
  {
    return true;
  }
};

IRNode MakeNode(const std::string &type_id, const std::string &debug_name)
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = debug_name;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  const KX_NodeTypeId id = registry.FindByEditorType(type_id);
  const KX_NodeSignature *signature = registry.GetSignature(id);
  if (signature == nullptr) {
    std::fprintf(stderr, "unknown node type %s\n", type_id.c_str());
    return node;
  }
  for (const KX_NodePortSignature &input : signature->inputs) {
    IRPort port;
    port.name = input.name;
    port.kind = input.kind;
    port.value_tag = input.value_type;
    port.asset_type_hint = input.asset_type_hint;
    node.inputs.push_back(port);
  }
  for (const KX_NodePortSignature &output : signature->outputs) {
    IRPort port;
    port.name = output.name;
    port.kind = output.kind;
    port.value_tag = output.value_type;
    port.asset_type_hint = output.asset_type_hint;
    node.outputs.push_back(port);
  }
  return node;
}

void Connect(GraphIR &ir, int from_node, int from_port, int to_node, int to_port)
{
  IRConnection connection;
  connection.from_node = from_node;
  connection.from_port = from_port;
  connection.to_node = to_node;
  connection.to_port = to_port;
  ir.connections.push_back(connection);
}

/**
 * Stable graph: one asset write plus a canvas with a label and an image.
 *
 * The asset node holds a registry reference, the UI nodes hold element handles
 * that live in the adapters, so a rebuild that leaked either one is visible
 * after the loop.
 */
GraphIR BuildStableGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("CreateUICanvas", "canvas"));
  ir.nodes.push_back(MakeNode("CreateUILabel", "label"));
  ir.nodes.push_back(MakeNode("CreateUIImage", "image"));
  ir.nodes.push_back(MakeNode("AssetSet", "write"));
  /* The dynamic value port is resolved by the asset node; the IR declares the
   * concrete field type. */
  ir.nodes[4].inputs[1].value_tag = KX_VALUE_INT;
  ir.nodes[4].config_json =
      "{\"asset_ref\": \"counter\", \"field\": \"value\", \"asset_type\": \"Counter\"}";

  Connect(ir, 0, 0, 1, 0);
  Connect(ir, 1, 0, 2, 0);
  Connect(ir, 1, 1, 2, 1);
  Connect(ir, 1, 0, 3, 0);
  Connect(ir, 1, 1, 3, 1);
  Connect(ir, 0, 0, 4, 0);
  return ir;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  KX_AssetRegistry &registry = KX_AssetRegistry::Get();
  registry.Clear();

  std::unique_ptr<KX_GameDataAsset> counter =
      std::make_unique<KX_GameDataAsset>("counter", "Counter");
  std::string error;
  Check(counter->AddField("value", KX_AssetFieldType::Int, KX_RuntimeValue::Int(0), &error),
        "asset field registered");
  const KX_AssetHandle handle = registry.Register(std::move(counter));
  Check(handle != KX_INVALID_ASSET_HANDLE, "asset registered");
  KX_GameDataAsset *asset = registry.Get(handle);
  Check(asset != nullptr && asset->refcount == 1, "baseline refcount is the registry reference");

  MockUI ui;
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  instance.GetContext()->SetAdapters(
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ui, nullptr);

  const GraphIR graph = BuildStableGraph();
  Check(instance.Load(graph, nullptr), "stable graph compiled");
  Check(asset->refcount == 2, "one asset node holds one extra reference");
  const size_t data_size = instance.GetGraph()->Data().UsedSize();
  std::printf("INFO runtime_data_size=%zu\n", data_size);

  instance.Tick(0.016f);
  const int baseline_canvases = ui.canvases;
  const int baseline_labels = ui.labels;
  const int baseline_images = ui.images;
  Check(baseline_canvases == 1 && baseline_labels == 1 && baseline_images == 1,
        "first frame created every UI element exactly once");
  const int canvas_handle = instance.GetGraph()->UIHandle("canvas");
  const int label_handle = instance.GetGraph()->UIHandle("label");
  const int image_handle = instance.GetGraph()->UIHandle("image");
  std::printf("INFO handles canvas=%d label=%d image=%d\n",
              canvas_handle,
              label_handle,
              image_handle);
  Check(canvas_handle >= 0 && label_handle >= 0 && image_handle >= 0,
        "UI handle map is populated");

  int loop_failures = 0;
  auto stable = [&](bool ok, const char *what, int iteration) {
    if (!ok) {
      ++loop_failures;
      std::printf("FAIL iteration %d: %s\n", iteration, what);
    }
  };

  for (int iteration = 0; iteration < 100; ++iteration) {
    stable(instance.Rebuild(graph, nullptr), "rebuild staged", iteration);
    /* Double buffering: the running plan is untouched until Tick starts. */
    stable(instance.HasPendingRebuild(), "pending graph waits for the frame boundary", iteration);
    stable(instance.GetPlan() != nullptr && instance.GetPlan()->nodes.size() == 5,
           "old plan still bound while a rebuild is pending",
           iteration);
    stable(instance.GetPendingGraph() != nullptr &&
               instance.GetPendingGraph()->Plan()->nodes.size() == 5,
           "pending plan is complete before the swap",
           iteration);
    /* Both graphs are alive here: registry + old node + new node. */
    stable(asset->refcount == 3, "staged graph holds its own asset reference", iteration);
    stable(ui.canvases == baseline_canvases && ui.images == baseline_images,
           "no UI element created before the swap",
           iteration);

    instance.Tick(0.016f);

    stable(!instance.HasPendingRebuild(), "pending graph consumed at Tick start", iteration);
    stable(asset->refcount == 2, "old graph released its asset reference", iteration);
    stable(instance.GetGraph()->Data().UsedSize() == data_size,
           "runtime data layout returns to the baseline size",
           iteration);
    stable(ui.canvases == baseline_canvases, "canvas handle survived the rebuild", iteration);
    stable(ui.labels == baseline_labels, "label handle survived the rebuild", iteration);
    stable(ui.images == baseline_images, "image handle survived the rebuild", iteration);
    stable(ui.buttons == 0 && ui.destroyed == 0,
           "hot reload neither creates nor destroys extra elements",
           iteration);
    stable(instance.GetGraph()->UIHandle("canvas") == canvas_handle,
           "canvas handle stays identical after rebuild",
           iteration);
  }
  std::printf("%s 100 rebuilds kept refcount, handles and runtime data at baseline\n",
              loop_failures == 0 ? "PASS" : "FAIL");
  if (loop_failures != 0) {
    ++failures;
  }

  /* Mixed scene reload and rebuild: dropping the instance must release the node
   * reference, loading again must re-acquire it. */
  for (int cycle = 0; cycle < 5; ++cycle) {
    Check(instance.Rebuild(graph, nullptr), "mixed rebuild staged");
    instance.Tick(0.016f);
    Check(asset->refcount == 2, "mixed rebuild keeps a single node reference");

    instance.Shutdown();
    Check(asset->refcount == 1, "shutdown released the node reference");

    Check(instance.Load(graph, nullptr), "reload compiled");
    Check(asset->refcount == 2, "reload re-acquired the node reference");
    instance.Tick(0.016f);
  }

  instance.Shutdown();
  Check(asset->refcount == 1, "final shutdown leaves only the registry reference");

  registry.Unregister(handle);
  Check(registry.Get(handle) == nullptr, "unregister destroyed the asset");
  Check(registry.AssetCount() == 0, "asset registry is back to the empty baseline");
  std::printf("INFO ui canvases=%d labels=%d images=%d buttons=%d destroyed=%d\n",
              ui.canvases,
              ui.labels,
              ui.images,
              ui.buttons,
              ui.destroyed);

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

/* B6.2 tables are not linked into this regression harness. */
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

/* Additional B6 register entry points not linked into this harness. */
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}
