// B6.6 scheduler performance probe.
//
// Measures 1000 frames of a graph that deliberately contains only nodes that
// must NOT opt into trigger snapshots: OnUpdate -> Branch, Delay, Sink plus a
// three node FloatMath value chain. The same binary is rebuilt before and after
// the scheduler change so the numbers are directly comparable.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

namespace {

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
  for (const KX_NodePortSignature &port : signature->inputs) {
    IRPort ir_port;
    ir_port.name = port.name;
    ir_port.kind = port.kind;
    ir_port.value_tag = port.value_type;
    node.inputs.push_back(ir_port);
  }
  for (const KX_NodePortSignature &port : signature->outputs) {
    IRPort ir_port;
    ir_port.name = port.name;
    ir_port.kind = port.kind;
    ir_port.value_tag = port.value_type;
    node.outputs.push_back(ir_port);
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

GraphIR BuildBenchGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnUpdate", "update"));   // 0
  ir.nodes.push_back(MakeNode("ConstFloat", "one"));    // 1
  ir.nodes.push_back(MakeNode("ConstFloat", "two"));    // 2
  ir.nodes.push_back(MakeNode("ConstBool", "yes"));     // 3
  ir.nodes.push_back(MakeNode("FloatMath", "m1"));      // 4
  ir.nodes.push_back(MakeNode("FloatMath", "m2"));      // 5
  ir.nodes.push_back(MakeNode("FloatMath", "m3"));      // 6
  ir.nodes.push_back(MakeNode("Branch", "branch"));     // 7
  ir.nodes.push_back(MakeNode("Delay", "delay"));       // 8
  ir.nodes.push_back(MakeNode("Sink", "sink"));         // 9

  ir.nodes[1].config_json = "{\"value\": \"1\"}";
  ir.nodes[2].config_json = "{\"value\": \"2\"}";
  ir.nodes[3].config_json = "{\"value\": \"true\"}";
  ir.nodes[4].config_json = "{\"op\": \"Add\"}";
  ir.nodes[5].config_json = "{\"op\": \"Mul\"}";
  ir.nodes[6].config_json = "{\"op\": \"Sub\"}";

  Connect(ir, 1, 0, 4, 0);
  Connect(ir, 2, 0, 4, 1);
  Connect(ir, 4, 0, 5, 0);
  Connect(ir, 2, 0, 5, 1);
  Connect(ir, 5, 0, 6, 0);
  Connect(ir, 1, 0, 6, 1);

  Connect(ir, 0, 0, 7, 0);
  Connect(ir, 3, 0, 7, 1);
  Connect(ir, 0, 0, 8, 0);
  Connect(ir, 1, 0, 8, 1);
  Connect(ir, 0, 0, 9, 0);
  Connect(ir, 6, 0, 9, 1);
  return ir;
}

constexpr int kFramesPerRound = 1000;
constexpr int kRounds = 20;

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> diagnostics;
  const GraphIR graph = BuildBenchGraph();
  if (!instance.Load(graph, &diagnostics)) {
    std::fprintf(stderr, "compile failed\n");
    for (const std::string &diagnostic : diagnostics) {
      std::fprintf(stderr, "  %s\n", diagnostic.c_str());
    }
    return 1;
  }

  /* Warm up so allocator and registry costs settle before the timed rounds. */
  for (int i = 0; i < 200; ++i) {
    instance.Tick(0.016f);
  }

  double best_us = 0.0;
  double total_us = 0.0;
  for (int round = 0; round < kRounds; ++round) {
    const auto begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFramesPerRound; ++frame) {
      instance.Tick(0.016f);
    }
    const auto end = std::chrono::steady_clock::now();
    const double us =
        std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(end - begin)
            .count();
    if (round == 0 || us < best_us) {
      best_us = us;
    }
    total_us += us;
  }

  std::printf("PERF frames=%d rounds=%d best_us_per_1000_frames=%.3f avg_us_per_1000_frames=%.3f "
              "total_ms=%.3f\n",
              kFramesPerRound,
              kRounds,
              best_us,
              total_us / kRounds,
              total_us / 1000.0);
  return 0;
}

/* Translation units that this probe does not link. Registration functions are
 * called unconditionally by RegisterBuiltinGameNodes, so they need bodies. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}
