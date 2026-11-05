// P4.1 rebuild unit test: 3 node graph -> 5 node graph, state migration,
// invalid graph keeps the old plan running.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
  int buttons = 0;

  int CreateCanvas(const std::string &) override
  {
    return 100 + canvases++;
  }
  int CreateLabel(int, const std::string &) override
  {
    return 200 + labels++;
  }
  int CreateButton(int, const std::string &) override
  {
    return 300 + buttons++;
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

GraphIR BuildThreeNodeGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("Delay", "delay"));
  ir.nodes.push_back(MakeNode("Sink", "sink"));
  Connect(ir, 0, 0, 1, 0);
  Connect(ir, 1, 0, 2, 0);
  return ir;
}

GraphIR BuildFiveNodeGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("CreateUICanvas", "canvas"));
  ir.nodes.push_back(MakeNode("CreateUILabel", "label"));
  ir.nodes.push_back(MakeNode("CreateUIButton", "button"));
  ir.nodes.push_back(MakeNode("Delay", "delay"));
  Connect(ir, 0, 0, 1, 0);
  Connect(ir, 1, 0, 2, 0);
  Connect(ir, 1, 1, 2, 1);
  Connect(ir, 1, 0, 3, 0);
  Connect(ir, 1, 1, 3, 1);
  /* The Delay OnEnter hangs off the Button OnClick output, which no injected
   * mouse event reaches, so the migrated countdown is observed rather than
   * restarted. An unconnected trigger input would make Delay a root and
   * execute it every frame. */
  Connect(ir, 3, 2, 4, 0);
  return ir;
}

GraphIR BuildBrokenGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  IRNode broken;
  broken.type_id = "MissingNodeType";
  broken.debug_name = "broken";
  ir.nodes.push_back(broken);
  Connect(ir, 0, 0, 1, 0);
  return ir;
}

KX_DelayState *FindDelayState(KX_GraphInstance &instance)
{
  const KX_ExecutionPlan *plan = instance.GetPlan();
  if (plan == nullptr) {
    return nullptr;
  }
  for (uint32_t index = 0; index < plan->nodes.size(); ++index) {
    if (plan->debug_names[index] != "delay") {
      continue;
    }
    const uint32_t offset = plan->nodes[index]->StateOffset();
    if (offset == KX_INVALID_OFFSET) {
      return nullptr;
    }
    KX_RuntimeData &data = const_cast<KX_CompiledGraph *>(instance.GetGraph())->Data();
    return data.State<KX_DelayState>(offset);
  }
  return nullptr;
}

}  // namespace

int main()
{
  /* Registers the builtin P1/P2 nodes plus every table driven family. */
  RegisterBuiltinGameNodes();

  MockUI ui;
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  instance.GetContext()->SetAdapters(
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ui, nullptr);

  std::vector<std::string> warnings;
  const GraphIR graph_a = BuildThreeNodeGraph();
  Check(instance.Load(graph_a, &warnings), "Load 3-node graph");
  Check(instance.GetPlan()->nodes.size() == 3, "plan has 3 nodes");

  instance.Tick(0.0f);
  KX_DelayState *delay = FindDelayState(instance);
  Check(delay != nullptr, "delay state located");
  if (delay != nullptr) {
    delay->remaining_sec = 1.5f;
    delay->pending = true;
  }

  const GraphIR graph_b = BuildFiveNodeGraph();
  Check(instance.Rebuild(graph_b, &warnings), "Rebuild to 5-node graph");
  Check(instance.HasPendingRebuild(), "rebuild waits for the frame boundary");
  Check(instance.GetPlan()->nodes.size() == 3, "old plan still bound before Tick");

  instance.Tick(0.1f);
  Check(instance.GetPlan()->nodes.size() == 5, "new plan installed at Tick start");
  Check(!instance.HasPendingRebuild(), "pending graph consumed");

  delay = FindDelayState(instance);
  if (delay != nullptr) {
    std::printf("INFO delay remaining=%.3f pending=%d\n", delay->remaining_sec, delay->pending);
  }
  Check(delay != nullptr && delay->pending, "delay pending flag migrated");
  Check(delay != nullptr && delay->remaining_sec > 1.39f && delay->remaining_sec < 1.41f,
        "delay remaining_sec migrated (1.4)");

  Check(ui.canvases >= 1, "new CreateUICanvas node executed");
  Check(ui.labels >= 1, "new CreateUILabel node executed");
  Check(ui.buttons >= 1, "new CreateUIButton node executed");

  instance.Tick(0.1f);
  Check(ui.canvases == 1, "canvas handle reused after rebuild, no duplicate");
  Check(ui.labels == 1, "label handle reused after rebuild, no duplicate");
  Check(ui.buttons == 1, "button handle reused after rebuild, no duplicate");
  Check(instance.GetGraph()->UIHandle("canvas") >= 0, "UI handle map holds canvas");
  Check(instance.GetGraph()->UIHandle("label") >= 0, "UI handle map holds label");

  // Invalid rebuild: old graph must keep running.
  const KX_ExecutionPlan *before = instance.GetPlan();
  const uint64_t frame_before = instance.GetContext()->Frame();
  const GraphIR broken = BuildBrokenGraph();
  Check(!instance.Rebuild(broken, &warnings), "invalid graph rebuild returns false");
  Check(!instance.HasPendingRebuild(), "invalid rebuild leaves no pending graph");
  Check(instance.GetPlan() == before, "old plan kept after failed rebuild");
  instance.Tick(0.1f);
  Check(instance.GetContext()->Frame() == frame_before + 1, "old graph keeps ticking");
  Check(instance.GetPlan()->nodes.size() == 5, "old 5-node plan still live");

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
