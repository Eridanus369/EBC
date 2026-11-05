// P4.3: opt in debug trace of the nodes activated per frame.
//
// Graph: Source(OnInit) -> Branch -> OnTrue(canvas) -> Delay -> Sink
// Frame 1 activates the first four, the Delay completes on frame 2.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "KX_DebugTrace.h"
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
  int CreateCanvas(const std::string &) override { return 100 + canvases++; }
  int CreateLabel(int, const std::string &) override { return 200; }
  int CreateButton(int, const std::string &) override { return 300; }
  bool SetAttribute(int, const std::string &, const KX_RuntimeValue &) override { return true; }
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

GraphIR BuildTraceGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "Source"));
  ir.nodes.push_back(MakeNode("ConstBool", "Cond"));
  ir.nodes[1].config_json = "{\"value\": \"true\"}";
  ir.nodes.push_back(MakeNode("Branch", "Branch"));
  ir.nodes.push_back(MakeNode("CreateUICanvas", "OnTrue"));
  ir.nodes.push_back(MakeNode("Delay", "Delay"));
  ir.nodes.push_back(MakeNode("Sink", "Sink"));

  Connect(ir, 0, 0, 2, 0);  /* OnInit -> Branch.OnEnter */
  Connect(ir, 1, 0, 2, 1);  /* ConstBool -> Branch.Condition */
  Connect(ir, 2, 0, 3, 0);  /* Branch.OnTrue -> canvas */
  Connect(ir, 3, 0, 4, 0);  /* canvas OnCreated -> Delay */
  Connect(ir, 4, 0, 5, 0);  /* Delay.OnDone -> Sink */
  return ir;
}

std::vector<std::string> ReadLines(const std::string &path)
{
  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

}  // namespace

int main(int argc, char **argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  const std::string log_path = argc > 1 ? argv[1] : "/tmp/p4_debug_trace.log";
  std::remove(log_path.c_str());
  KX_DebugTrace::SetLogPath(log_path);

  const bool environment_enabled = std::getenv("BGE_GAMENODE_DEBUG") != nullptr;
  Check(KX_DebugTrace::Enabled() == environment_enabled,
        environment_enabled ? "BGE_GAMENODE_DEBUG enables the trace"
                            : "trace is off by default");
  if (!environment_enabled) {
    KX_DebugTrace::SetEnabled(true);
  }
  Check(KX_DebugTrace::Enabled(), "trace enabled for the run");

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  Check(instance.Load(BuildTraceGraph(), nullptr), "trace graph compiled");
  MockUI ui;
  instance.GetContext()->SetAdapters(
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ui, nullptr);

  instance.Tick(0.016f);
  instance.Tick(0.016f);
  instance.Tick(0.016f);

  const std::vector<std::string> lines = ReadLines(log_path);
  Check(!lines.empty(), "trace log was written");
  if (!lines.empty()) {
    std::printf("INFO line0: %s\n", lines[0].c_str());
  }
  if (lines.size() >= 2) {
    std::printf("INFO line1: %s\n", lines[1].c_str());
  }
  Check(!lines.empty() && lines[0] == "frame 1 activated: [Source, Branch, OnTrue, Delay]",
        "frame 1 line lists the trigger chain in execution order");
  /* OnInit is a trigger root: it is executed (and recorded) every frame even
   * after its single shot fired, so frame 2 lists the delayed Sink first and
   * then the root. */
  Check(lines.size() >= 2 && lines[1] == "frame 2 activated: [Sink, Source]",
        "frame 2 line records the delayed OnDone chain");
  Check(lines.size() >= 3 && lines[2] == "frame 3 activated: [Source]",
        "frame 3 line records the idle root");
  Check(ui.canvases == 1, "trace run still executed the UI node exactly once");

  /* Disabling stops the file from growing. */
  const size_t before = lines.size();
  KX_DebugTrace::SetEnabled(false);
  if (!environment_enabled) {
    KX_GraphInstance second(nullptr, nullptr, nullptr);
    second.Load(BuildTraceGraph(), nullptr);
    second.Tick(0.016f);
    second.Tick(0.016f);
    Check(ReadLines(log_path).size() == before, "disabled trace writes no more lines");
  }

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
