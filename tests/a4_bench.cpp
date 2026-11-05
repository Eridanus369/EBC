// A.4 performance baseline: 100-node graph, 1000 frames per round.
//
// The graph is deliberately shaped so the inline pass has something to merge:
//   OnUpdate -> Sink(trigger)
//   ConstFloat x2 -> 96 chained FloatMath nodes -> Sink(value)
// Node count: 1 + 2 + 96 + 1 = 100.
//
// Path selection is done outside the process (BGE_GAMENODE_INLINE=0/off), see
// KX_InlinePass::EnabledFlag. The same binary is run twice.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

namespace {

constexpr int kNodeCount = 100;
constexpr int kMathNodes = 96;
constexpr int kFramesPerRound = 1000;
constexpr int kRounds = 21;

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
  ir.nodes.push_back(MakeNode("OnUpdate", "update"));  // 0
  ir.nodes.push_back(MakeNode("ConstFloat", "one"));   // 1
  ir.nodes.push_back(MakeNode("ConstFloat", "two"));   // 2

  ir.nodes[1].config_json = "{\"value\": \"1\"}";
  ir.nodes[2].config_json = "{\"value\": \"2\"}";

  // 96 chained FloatMath nodes: indices 3 .. 98.
  for (int i = 0; i < kMathNodes; ++i) {
    ir.nodes.push_back(MakeNode("FloatMath", "m" + std::to_string(i)));
    ir.nodes.back().config_json = (i % 2 == 0) ? "{\"op\": \"Add\"}" : "{\"op\": \"Mul\"}";
  }
  ir.nodes.push_back(MakeNode("Sink", "sink"));  // 99

  const int first_math = 3;
  Connect(ir, 1, 0, first_math, 0);
  Connect(ir, 2, 0, first_math, 1);
  for (int i = 0; i + 1 < kMathNodes; ++i) {
    Connect(ir, first_math + i, 0, first_math + i + 1, 0);
    Connect(ir, 2, 0, first_math + i + 1, 1);
  }
  Connect(ir, first_math + kMathNodes - 1, 0, 99, 1);
  Connect(ir, 0, 0, 99, 0);
  return ir;
}

double Median(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  if (n == 0) {
    return 0.0;
  }
  return (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

std::string BuildId()
{
  const char *env = std::getenv("A4_BUILD_ID");
  return (env != nullptr) ? std::string(env) : std::string("unset");
}

std::string PathLabel()
{
  const char *env = std::getenv("A4_PATH_LABEL");
  return (env != nullptr) ? std::string(env) : std::string("native");
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> diagnostics;
  const GraphIR graph = BuildBenchGraph();
  if (graph.nodes.size() != static_cast<size_t>(kNodeCount)) {
    std::fprintf(stderr, "graph node count mismatch: %zu\n", graph.nodes.size());
    return 2;
  }
  if (!instance.Load(graph, &diagnostics)) {
    std::fprintf(stderr, "compile failed\n");
    for (const std::string &diagnostic : diagnostics) {
      std::fprintf(stderr, "  %s\n", diagnostic.c_str());
    }
    return 1;
  }
  std::printf("DIAG count=%zu", diagnostics.size());
  for (const std::string &diagnostic : diagnostics) {
    std::printf(" [%s]", diagnostic.c_str());
  }
  std::printf("\n");

  // Warm up allocator and registry before timing.
  for (int i = 0; i < 200; ++i) {
    instance.Tick(0.016f);
  }

  std::vector<double> ns_per_frame;
  ns_per_frame.reserve(kRounds);
  for (int round = 0; round < kRounds; ++round) {
    const auto begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFramesPerRound; ++frame) {
      instance.Tick(0.016f);
    }
    const auto end = std::chrono::steady_clock::now();
    const double total_ns =
        std::chrono::duration_cast<std::chrono::duration<double, std::nano>>(end - begin).count();
    ns_per_frame.push_back(total_ns / kFramesPerRound);
  }

  const double median = Median(ns_per_frame);
  double sum = 0.0;
  for (double value : ns_per_frame) {
    sum += value;
  }
  std::printf(
      "BENCH path=%s frames=%d rounds=%d nodes=%d median_ns_per_frame=%.1f "
      "mean_ns_per_frame=%.1f min_ns_per_frame=%.1f build_id=%s\n",
      PathLabel().c_str(),
      kFramesPerRound,
      kRounds,
      kNodeCount,
      median,
      sum / kRounds,
      *std::min_element(ns_per_frame.begin(), ns_per_frame.end()),
      BuildId().c_str());
  return 0;
}
