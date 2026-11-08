// B6.9-S1: ListSlice negative index contract.
//
// Contract under test (docs/B6_STRING_LIST.md):
//   start/end >= 0  absolute position, clamped to [0, size]
//   start/end <  0  counts from the end: -1 = last element, -size = first
//   End == 0        legacy sentinel "through the end"
//   End < Start     empty slice
//
// ListReverse / ListUnique / ListIndexOf and the StringSplit multi-delimiter
// and StringRegexMatch capture-group leftovers are already asserted in
// tests/b62_e2e.cpp and are re-run as part of the same regression sweep.
#include <cstdio>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}

namespace {

int failures = 0;

void Check(bool ok, const char *what, int actual = 0, int expected = 0)
{
  std::printf("%s %s (actual=%d expected=%d)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

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

int AddFloat(GraphIR &ir, float value)
{
  IRNode node = MakeNode("ConstFloat", "f");
  node.config_json = "{\"value\": \"" + std::to_string(value) + "\"}";
  ir.nodes.push_back(node);
  return static_cast<int>(ir.nodes.size()) - 1;
}

int AddInt(GraphIR &ir, int value)
{
  IRNode node = MakeNode("ConstInt", "i");
  node.config_json = "{\"value\": \"" + std::to_string(value) + "\"}";
  ir.nodes.push_back(node);
  return static_cast<int>(ir.nodes.size()) - 1;
}

const KX_ValueList *AsListOf(const KX_RuntimeValue *value)
{
  return value != nullptr ? static_cast<const KX_ValueList *>(value->AsList()) : nullptr;
}

const KX_RuntimeValue *ReadOutput(KX_GraphInstance &instance, int node, int port)
{
  const KX_ExecutionPlan *plan = instance.GetGraph()->Plan();
  for (const KX_PortOffsetInfo &info : plan->port_offsets) {
    if (!info.input && static_cast<int>(info.node_index) == node &&
        static_cast<int>(info.port_index) == port && !info.trigger) {
      return &instance.GetGraph()->Data().Value(info.offset);
    }
  }
  return nullptr;
}

struct SliceCase {
  int start;
  int end;
  std::vector<float> expected;
  const char *label;
};

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  /* Base list [1, 2, 3, 4, 5]. */
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));

  /* ListAdd with an unconnected List input starts a fresh list. */
  int list = -1;
  for (float value = 1.0f; value <= 5.0f; value += 1.0f) {
    IRNode node = MakeNode("ListAdd", "list");
    ir.nodes.push_back(node);
    const int index = static_cast<int>(ir.nodes.size()) - 1;
    if (list >= 0) {
      Connect(ir, list, 0, index, 0);
    }
    Connect(ir, AddFloat(ir, value), 0, index, 1);
    list = index;
  }

  const SliceCase cases[] = {
      {-2, 0, {4, 5}, "ListSlice start=-2 end=0 -> last two [4,5]"},
      {0, -1, {1, 2, 3, 4}, "ListSlice start=0 end=-1 excludes the last [1,2,3,4]"},
      {1, -1, {2, 3, 4}, "ListSlice start=1 end=-1 -> [2,3,4]"},
      {-99, 0, {1, 2, 3, 4, 5}, "ListSlice start=-99 clamps to the first element"},
      {-1, -1, {}, "ListSlice start=-1 end=-1 -> empty slice"},
      {3, 2, {}, "ListSlice end<start -> empty"},
  };

  std::vector<int> slice_nodes;
  for (const SliceCase &entry : cases) {
    IRNode node = MakeNode("ListSlice", entry.label);
    ir.nodes.push_back(node);
    const int index = static_cast<int>(ir.nodes.size()) - 1;
    Connect(ir, list, 0, index, 0);
    Connect(ir, AddInt(ir, entry.start), 0, index, 1);
    Connect(ir, AddInt(ir, entry.end), 0, index, 2);
    slice_nodes.push_back(index);
  }

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> diagnostics;
  const bool loaded = instance.Load(ir, &diagnostics);
  Check(loaded, "ListSlice graph compiled");
  for (const std::string &diagnostic : diagnostics) {
    std::printf("INFO compile: %s\n", diagnostic.c_str());
  }
  if (loaded) {
    instance.Tick(0.016f);
    for (size_t index = 0; index < slice_nodes.size(); ++index) {
      const SliceCase &entry = cases[index];
      const KX_ValueList *slice = AsListOf(ReadOutput(instance, slice_nodes[index], 0));
      const int actual = slice != nullptr ? static_cast<int>(slice->size()) : -1;
      bool ok = slice != nullptr &&
                static_cast<size_t>(actual) == entry.expected.size();
      if (ok) {
        for (size_t item = 0; item < entry.expected.size(); ++item) {
          if (slice->at(item).AsFloat() != entry.expected[item]) {
            ok = false;
            break;
          }
        }
      }
      Check(ok, entry.label, actual, static_cast<int>(entry.expected.size()));
    }
  }

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
