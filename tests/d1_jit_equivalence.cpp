// D.1 JIT equivalence: the same random pure-value graph must produce the same
// float bits whether KX_InlinePass is off or on.
//
// Usage:
//   d1_jit_equivalence --graphs 20 --nodes 100 --seed 12345
//
// Prints one "graph=<i> bits=%08x" line per graph. The driver script runs the
// binary twice (BGE_GAMENODE_INLINE=0/1) and diffs the output.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionPlan.h"
#include "KX_FunctionNode.h"
#include "KX_GraphInstance.h"
#include "KX_InlinePass.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

namespace {

IRNode MakeNode(GraphIR &ir, const std::string &type_id, const std::string &name,
                const std::string &config = std::string())
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = name;
  node.config_json = config;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  const KX_NodeTypeId id = registry.FindByEditorType(type_id);
  const KX_NodeSignature *signature = registry.GetSignature(id);
  if (signature != nullptr) {
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
  }
  ir.nodes.push_back(node);
  return ir.nodes.back();
}

int AddNode(GraphIR &ir, const std::string &type_id, const std::string &name,
            const std::string &config = std::string())
{
  MakeNode(ir, type_id, name, config);
  return int(ir.nodes.size()) - 1;
}

int AddConstFloat(GraphIR &ir, const std::string &name, float value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": \"%.9g\"}", double(value));
  return AddNode(ir, "ConstFloat", name, config);
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

uint32_t NextRandom(uint32_t &state)
{
  state = state * 1664525u + 1013904223u;
  return state;
}

float NextFloat(uint32_t &state, float low, float high)
{
  const float unit = float(NextRandom(state) >> 8) / float(1u << 24);
  return low + (high - low) * unit;
}

/** OnUpdate -> chain of #nodes pure value nodes -> Sink. */
GraphIR BuildValueGraph(uint32_t seed, int nodes)
{
  GraphIR ir;
  int update = AddNode(ir, "OnUpdate", "Frame");
  uint32_t state = seed;
  int prev = AddConstFloat(ir, "SeedA", NextFloat(state, -4.0f, 4.0f));
  int other = AddConstFloat(ir, "SeedB", NextFloat(state, 0.5f, 3.0f));

  const char *ops[] = {"Add", "Sub", "Mul", "Min", "Max"};
  for (int index = 0; index < nodes; ++index) {
    const char *op = ops[NextRandom(state) % 5u];
    char config[64];
    std::snprintf(config, sizeof(config), "{\"op\": \"%s\"}", op);
    int math = AddNode(ir, "FloatMath", "M" + std::to_string(index), config);
    Connect(ir, prev, 0, math, 0);
    Connect(ir, other, 0, math, 1);
    prev = math;
  }

  int sink = AddNode(ir, "Sink", "Sink");
  Connect(ir, update, 0, sink, 0);
  Connect(ir, prev, 0, sink, 1);
  return ir;
}

KX_Node_Sink *FindSink(const KX_ExecutionPlan *plan)
{
  if (plan == nullptr) {
    return nullptr;
  }
  for (size_t index = 0; index < plan->debug_names.size(); ++index) {
    if (plan->debug_names[index] == "Sink") {
      return dynamic_cast<KX_Node_Sink *>(plan->nodes[index].get());
    }
  }
  return nullptr;
}

}  // namespace

int main(int argc, char **argv)
{
  int graphs = 20;
  int nodes = 100;
  uint32_t seed = 12345u;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--graphs") == 0 && index + 1 < argc) {
      graphs = std::atoi(argv[++index]);
    }
    else if (std::strcmp(argv[index], "--nodes") == 0 && index + 1 < argc) {
      nodes = std::atoi(argv[++index]);
    }
    else if (std::strcmp(argv[index], "--seed") == 0 && index + 1 < argc) {
      seed = uint32_t(std::strtoul(argv[++index], nullptr, 10));
    }
  }

  RegisterBuiltinGameNodes();
  std::printf("inline=%d graphs=%d nodes=%d seed=%u\n",
              KX_InlinePass::Enabled() ? 1 : 0,
              graphs,
              nodes,
              unsigned(seed));

  int failures = 0;
  for (int graph = 0; graph < graphs; ++graph) {
    GraphIR ir = BuildValueGraph(seed + uint32_t(graph) * 7919u, nodes);
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    std::vector<std::string> warnings;
    if (!instance.Load(ir, &warnings)) {
      std::fprintf(stderr, "graph=%d failed to load\n", graph);
      for (const std::string &warning : warnings) {
        std::fprintf(stderr, "  %s\n", warning.c_str());
      }
      ++failures;
      continue;
    }
    const KX_ExecutionPlan *plan = instance.GetPlan();
    KX_Node_Sink *sink = FindSink(plan);
    if (sink == nullptr) {
      std::fprintf(stderr, "graph=%d has no Sink\n", graph);
      ++failures;
      continue;
    }
    instance.Tick(1.0f / 60.0f);
    const float value = sink->LastValue().AsFloat();
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    std::printf("graph=%d bits=%08x\n", graph, unsigned(bits));
  }

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
