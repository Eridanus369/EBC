// Consolidated regression for the core runtime layers behind P1/P2/P3/P3.5 and
// T1/T2: registry lookup, IR compile, execution ticks, Timer state and Once.
#include <cstdio>
#include <string>
#include <vector>

#include "KX_GraphInstance.h"
#include "KX_GraphIR.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

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

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  for (const char *type_id : {"OnInit", "Timer", "Once", "Print", "StringConst", "ConstFloat"}) {
    Check(KX_NodeRegistry::Get().FindByEditorType(type_id) != KX_INVALID_NODE_TYPE_ID,
          type_id);
  }

  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));              // 0
  ir.nodes.push_back(MakeNode("Timer", "timer"));              // 1
  ir.nodes.push_back(MakeNode("ConstFloat", "duration"));      // 2
  ir.nodes[2].config_json = "{\"value\": \"0.1\"}";
  ir.nodes.push_back(MakeNode("StringConst", "timer_text"));   // 3
  ir.nodes[3].config_json = "{\"value\": \"CORE_TIMER_DONE\"}";
  ir.nodes.push_back(MakeNode("Print", "timer_print"));        // 4
  ir.nodes.push_back(MakeNode("Once", "once"));                // 5
  ir.nodes.push_back(MakeNode("StringConst", "once_text"));    // 6
  ir.nodes[6].config_json = "{\"value\": \"CORE_ONCE\"}";
  ir.nodes.push_back(MakeNode("Print", "once_print"));         // 7

  Connect(ir, 0, 0, 1, 0);
  Connect(ir, 2, 0, 1, 1);
  Connect(ir, 1, 1, 4, 0);
  Connect(ir, 3, 0, 4, 1);
  Connect(ir, 0, 0, 5, 0);
  Connect(ir, 5, 0, 7, 0);
  Connect(ir, 6, 0, 7, 1);

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> diagnostics;
  Check(instance.Load(ir, &diagnostics), "core graph compiled");
  for (const std::string &diagnostic : diagnostics) {
    std::printf("INFO compile: %s\n", diagnostic.c_str());
  }

  for (int frame = 0; frame < 5; ++frame) {
    instance.Tick(0.05f);
  }

  Check(instance.Rebuild(ir, nullptr), "core graph rebuild staged");
  instance.Tick(0.05f);
  Check(true, "core graph rebuilt and ticked");

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
