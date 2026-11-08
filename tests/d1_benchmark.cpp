// D.1 benchmark: 100-node FloatMath chain, three paths.
//
//   direct-call   100 ApplyOp() calls per frame, inside this TU. No
//                 KX_GraphInstance, no ExecutionPlan, no scheduler, no node
//                 objects. This is a CONTROL: it shows the cost of the raw
//                 float chain and is NOT comparable with the graph paths.
//   inline-off   KX_GraphInstance::Tick -> KX_ExecutionContext::RunFrame ->
//                 ExecuteValueNodes -> KX_GameNodeRuntime::Execute, once per
//                 value node (KX_InlinePass disabled).
//   inline-on    same plan and same nodes, but the foldable run is wrapped in
//                 KX_InlinedBlock; ExecuteValueNodes calls the block, and
//                 KX_InlinedBlock::Execute -> ExecuteValueOnly -> node->Execute
//                 for each wrapped node.
//
// Every graph path prints the plan counters that prove the work is real:
// value_entries (entries in plan.value_nodes), inline_blocks, inlined_nodes,
// executed_nodes_per_frame (= plain entries + nodes inside inline blocks),
// plan_nodes and tick_nodes. inline-off and inline-on must report the same
// executed_nodes_per_frame; otherwise they are not running the same graph.
//
// The direct-call result is compared bit-for-bit with the graph Sink last
// value; a mismatch means the control path is not the same 100-op chain.
//
// Prints "path=<name> ns_per_frame=<median> ns_per_node=<median>" lines.

#include <algorithm>
#include <chrono>
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
  const KX_NodeSignature *signature = registry.GetSignature(registry.FindByEditorType(type_id));
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

/** OnUpdate -> #nodes FloatMath chain -> Sink. */
GraphIR BuildMathGraph(uint32_t seed, int nodes, std::vector<int> *ops)
{
  GraphIR ir;
  int update = AddNode(ir, "OnUpdate", "Frame");
  uint32_t state = seed;
  int prev = AddConstFloat(ir, "SeedA", NextFloat(state, -4.0f, 4.0f));
  int other = AddConstFloat(ir, "SeedB", NextFloat(state, 0.5f, 3.0f));
  const char *op_names[] = {"Add", "Sub", "Mul", "Min", "Max"};
  for (int index = 0; index < nodes; ++index) {
    const int op = int(NextRandom(state) % 5u);
    char config[64];
    std::snprintf(config, sizeof(config), "{\"op\": \"%s\"}", op_names[op]);
    int math = AddNode(ir, "FloatMath", "M" + std::to_string(index), config);
    Connect(ir, prev, 0, math, 0);
    Connect(ir, other, 0, math, 1);
    prev = math;
    if (ops != nullptr) {
      ops->push_back(op);
    }
  }
  int sink = AddNode(ir, "Sink", "Sink");
  Connect(ir, update, 0, sink, 0);
  Connect(ir, prev, 0, sink, 1);
  return ir;
}

float ApplyOp(int op, float a, float b)
{
  switch (op) {
    case 0:
      return a + b;
    case 1:
      return a - b;
    case 2:
      return a * b;
    case 3:
      return a < b ? a : b;
    default:
      return a > b ? a : b;
  }
}

double Median(std::vector<double> &values)
{
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

bool LoadGraph(KX_GraphInstance &instance, const GraphIR &ir, const char *label)
{
  std::vector<std::string> warnings;
  if (!instance.Load(ir, &warnings)) {
    std::fprintf(stderr, "%s failed to load\n", label);
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "  %s\n", warning.c_str());
    }
    return false;
  }
  return true;
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

/** What the frame driver really executes, read back from the compiled plan. */
struct PathStats {
  size_t plan_nodes = 0;
  size_t value_entries = 0;
  size_t plain_entries = 0;
  size_t inline_blocks = 0;
  size_t inlined_nodes = 0;
  size_t tick_nodes = 0;
  size_t executed_nodes = 0;
};

PathStats CollectStats(const KX_ExecutionPlan *plan)
{
  PathStats stats;
  if (plan == nullptr) {
    return stats;
  }
  stats.plan_nodes = plan->nodes.size();
  stats.tick_nodes = plan->tick_nodes.size();
  stats.value_entries = plan->value_nodes.size();
  for (const uint32_t index : plan->value_nodes) {
    const KX_GameNodeRuntime *node = plan->nodes[index].get();
    const KX_InlinedBlock *block = dynamic_cast<const KX_InlinedBlock *>(node);
    if (block != nullptr) {
      ++stats.inline_blocks;
      stats.inlined_nodes += block->Nodes().size();
    }
    else {
      ++stats.plain_entries;
    }
  }
  /* A plain value entry runs one node; an inline block runs every node it
   * wraps. Both paths must report the same number for the same graph. */
  stats.executed_nodes = stats.plain_entries + stats.inlined_nodes;
  return stats;
}

}  // namespace

int main(int argc, char **argv)
{
  int nodes = 100;
  int frames = 1000;
  int rounds = 5;
  uint32_t seed = 12345u;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--nodes") == 0 && index + 1 < argc) {
      nodes = std::atoi(argv[++index]);
    }
    else if (std::strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
      frames = std::atoi(argv[++index]);
    }
    else if (std::strcmp(argv[index], "--rounds") == 0 && index + 1 < argc) {
      rounds = std::atoi(argv[++index]);
    }
    else if (std::strcmp(argv[index], "--seed") == 0 && index + 1 < argc) {
      seed = uint32_t(std::strtoul(argv[++index], nullptr, 10));
    }
  }

  RegisterBuiltinGameNodes();
  std::vector<int> ops;
  GraphIR ir = BuildMathGraph(seed, nodes, &ops);

  KX_GraphInstance offline(nullptr, nullptr, nullptr);
  KX_GraphInstance online(nullptr, nullptr, nullptr);

  /* Compile must happen while the switch is in the wanted state: Load()
   * compiles immediately, so the old order (set enabled true before both
   * Load calls) folded the "offline" graph as well. */
  KX_InlinePass::SetEnabled(false);
  if (!LoadGraph(offline, ir, "inline-off")) {
    return 2;
  }
  KX_InlinePass::SetEnabled(true);
  if (!LoadGraph(online, ir, "inline-on")) {
    return 2;
  }

  const PathStats off_stats = CollectStats(offline.GetPlan());
  const PathStats on_stats = CollectStats(online.GetPlan());
  KX_Node_Sink *off_sink = FindSink(offline.GetPlan());
  KX_Node_Sink *on_sink = FindSink(online.GetPlan());
  if (off_sink == nullptr || on_sink == nullptr) {
    std::fprintf(stderr, "graph has no Sink\n");
    return 2;
  }

  uint32_t state = seed;
  const float seed_a = NextFloat(state, -4.0f, 4.0f);
  const float seed_b = NextFloat(state, 0.5f, 3.0f);

  std::printf("# path=direct-call: %d ApplyOp() calls per frame in this TU; no graph, no "
              "scheduler, no node objects; CONTROL ONLY, not comparable to graph paths\n",
              nodes);
  std::printf("# path=inline-off: KX_GraphInstance::Tick -> ExecuteValueNodes -> "
              "KX_GameNodeRuntime::Execute, one call per value node\n");
  std::printf("# path=inline-on: same plan, foldable run wrapped in KX_InlinedBlock; "
              "ExecuteValueNodes -> block->Execute -> ExecuteValueOnly -> node->Execute\n");

  std::vector<double> direct_ns;
  std::vector<double> offline_ns;
  std::vector<double> online_ns;
  const float delta_time = 1.0f / 60.0f;
  float control_last = 0.0f;
  float control_checksum = 0.0f;
  /* This read is deliberately volatile: seed_a/seed_b/ops are loop invariant,
   * so without it -O1 hoists the whole 100-node chain out of the frame loop
   * (that is exactly how the first "native = 26 ns/frame" number was produced).
   * The bias is always 0, so the control stays bit-identical to the graph. */
  volatile float control_bias = 0.0f;

  for (int round = 0; round < rounds; ++round) {
    /* direct-call control */
    const auto direct_begin = std::chrono::steady_clock::now();
    float value = seed_a;
    for (int frame = 0; frame < frames; ++frame) {
      value = seed_a + control_bias;
      for (int index = 0; index < nodes; ++index) {
        value = ApplyOp(ops[size_t(index)], value, seed_b);
      }
      control_checksum += value;
      control_last = value;
    }
    const auto direct_end = std::chrono::steady_clock::now();
    direct_ns.push_back(double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   direct_end - direct_begin)
                                   .count()) /
                        double(frames));

    /* inline off */
    const auto off_begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
      offline.Tick(delta_time);
    }
    const auto off_end = std::chrono::steady_clock::now();
    offline_ns.push_back(double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    off_end - off_begin)
                                    .count()) /
                         double(frames));

    /* inline on */
    const auto on_begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
      online.Tick(delta_time);
    }
    const auto on_end = std::chrono::steady_clock::now();
    online_ns.push_back(double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   on_end - on_begin)
                                   .count()) /
                        double(frames));
  }

  const float off_sink_value = off_sink->LastValue().AsFloat();
  const float on_sink_value = on_sink->LastValue().AsFloat();
  const bool off_match = off_sink_value == control_last;
  const bool on_match = on_sink_value == control_last;
  const bool count_match = off_stats.executed_nodes == on_stats.executed_nodes;
  const bool counted = off_stats.executed_nodes == size_t(nodes) ||
                       off_stats.executed_nodes == size_t(nodes) + 2 ||
                       off_stats.executed_nodes == size_t(nodes) + 2 + 1;
  const int failures = (off_match && on_match && count_match) ? 0 : 1;

  std::printf("bench nodes=%d frames=%d rounds=%d seed=%u\n",
              nodes,
              frames,
              rounds,
              unsigned(seed));

  std::printf("path=direct-call ns_per_frame=%.1f ns_per_node=%.2f node_calls_per_frame=%d "
              "graph=no\n",
              Median(direct_ns),
              Median(direct_ns) / double(nodes > 0 ? nodes : 1),
              nodes);
  std::printf("path=inline-off ns_per_frame=%.1f ns_per_node=%.2f plan_nodes=%zu "
              "value_entries=%zu inline_blocks=%zu inlined_nodes=%zu "
              "executed_nodes_per_frame=%zu tick_nodes=%zu\n",
              Median(offline_ns),
              Median(offline_ns) / double(off_stats.executed_nodes > 0 ? off_stats.executed_nodes : 1),
              off_stats.plan_nodes,
              off_stats.value_entries,
              off_stats.inline_blocks,
              off_stats.inlined_nodes,
              off_stats.executed_nodes,
              off_stats.tick_nodes);
  std::printf("path=inline-on ns_per_frame=%.1f ns_per_node=%.2f plan_nodes=%zu "
              "value_entries=%zu inline_blocks=%zu inlined_nodes=%zu "
              "executed_nodes_per_frame=%zu tick_nodes=%zu\n",
              Median(online_ns),
              Median(online_ns) / double(on_stats.executed_nodes > 0 ? on_stats.executed_nodes : 1),
              on_stats.plan_nodes,
              on_stats.value_entries,
              on_stats.inline_blocks,
              on_stats.inlined_nodes,
              on_stats.executed_nodes,
              on_stats.tick_nodes);
  std::printf("control_checksum=%.9g control_last=%.9g graph_sink_off=%.9g "
              "graph_sink_on=%.9g match_off=%s match_on=%s count_match=%s "
              "count_plausible=%s\n",
              double(control_checksum),
              double(control_last),
              double(off_sink_value),
              double(on_sink_value),
              off_match ? "yes" : "no",
              on_match ? "yes" : "no",
              count_match ? "yes" : "no",
              counted ? "yes" : "no");
  if (!off_match || !on_match) {
    std::fprintf(stderr,
                 "FAIL: direct-call control does not match the graph Sink value\n");
  }
  if (!count_match || !counted) {
    std::fprintf(stderr,
                 "FAIL: executed node count inconsistent (inline-off=%zu inline-on=%zu "
                 "expected ~%d)\n",
                 off_stats.executed_nodes,
                 on_stats.executed_nodes,
                 nodes);
  }
  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
