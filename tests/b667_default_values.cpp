// B6.6/B6.7 regression: unconnected editor defaults must reach the runtime.
//
// Bug: Print's Value is a String socket. Filling it in the editor without a
// link used to print "None", because the IR exporter only materialized
// Float/Bool defaults, so the runtime slot stayed empty.
//
// This harness covers the runtime half of the fix: the IR produced by the
// exporter now contains ConstString/ConstInt/ConstVector nodes, and Print has
// to print the literal instead of "None".
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionContext.h"
#include "KX_ExecutionPlan.h"
#include "KX_FunctionNode.h"
#include "KX_GraphIR.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

void RegisterBuiltinGameNodes();
void RegisterFunctionGameNodes();

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char *what, const std::string &actual, const std::string &expected)
{
  ++g_checks;
  std::printf("%s %s (actual=%s expected=%s)\n",
              ok ? "PASS" : "FAIL",
              what,
              actual.empty() ? "<empty>" : actual.c_str(),
              expected.c_str());
  if (!ok) {
    ++g_failures;
  }
}

int AddNode(GraphIR &ir, const std::string &type_id, const std::string &name)
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = name;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  const KX_NodeTypeId id = registry.FindByEditorType(type_id);
  const KX_NodeSignature *signature = registry.GetSignature(id);
  if (signature == nullptr) {
    std::fprintf(stderr, "unknown node type %s\n", type_id.c_str());
  }
  else {
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
  }
  ir.nodes.push_back(node);
  return static_cast<int>(ir.nodes.size()) - 1;
}

void SetConfig(GraphIR &ir, int index, const std::string &json)
{
  ir.nodes[index].config_json = json;
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

KX_GraphInstance *BuildInstance(const GraphIR &ir)
{
  KX_GraphInstance *instance = new KX_GraphInstance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  if (!instance->Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    delete instance;
    return nullptr;
  }
  instance->GetContext()->SetAdapters(nullptr, nullptr, nullptr, nullptr);
  return instance;
}

/** Redirect fd 1 into a file for one Print tick, then restore and read it back. */
class StdoutCapture {
 public:
  explicit StdoutCapture(const char *path) : path_(path)
  {
    std::fflush(stdout);
    saved_ = dup(STDOUT_FILENO);
    std::freopen(path, "w", stdout);
  }

  std::string Finish()
  {
    std::fflush(stdout);
    if (saved_ >= 0) {
      dup2(saved_, STDOUT_FILENO);
      close(saved_);
      saved_ = -1;
    }
    std::string text;
    FILE *file = std::fopen(path_, "r");
    if (file != nullptr) {
      char buffer[256];
      while (std::fgets(buffer, sizeof(buffer), file) != nullptr) {
        text += buffer;
      }
      std::fclose(file);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
      text.pop_back();
    }
    return text;
  }

 private:
  const char *path_;
  int saved_ = -1;
};

/** Run one port of a Print node and return what it wrote to stdout. */
std::string RunPrint(const GraphIR &ir)
{
  KX_GraphInstance *instance = BuildInstance(ir);
  if (instance == nullptr) {
    return "<compile failed>";
  }
  StdoutCapture capture("/tmp/b667_print_out.txt");
  instance->Tick(0.016f);
  const std::string text = capture.Finish();
  delete instance;
  return text;
}

/**
 * Pre-fix shape of the IR: Print with an empty Value slot and no constant.
 * The runtime reports the unset input explicitly; it must not print the
 * literal "None" (see KNOWN_LIMITATIONS.md #32).
 */
void TestPrintWithoutConstant()
{
  GraphIR ir;
  const int init = AddNode(ir, "OnInit", "init");
  const int print = AddNode(ir, "Print", "print");
  Connect(ir, init, 0, print, 0);
  const std::string actual = RunPrint(ir);
  Check(actual == "<Print: Value input unset>",
        "Print with an empty slot (pre-fix IR)",
        actual,
        "<Print: Value input unset>");
}

void TestPrintFromConstString()
{
  GraphIR ir;
  const int init = AddNode(ir, "OnInit", "init");
  const int print = AddNode(ir, "Print", "print");
  const int text = AddNode(ir, "ConstString", "value");
  SetConfig(ir, text, "{\"value\": \"E\"}");
  Connect(ir, init, 0, print, 0);
  Connect(ir, text, 0, print, 1);
  const std::string actual = RunPrint(ir);
  Check(actual == "E", "ConstString feeds Print", actual, "E");
}

/** Read a value node's declared output slot after one tick. */
KX_RuntimeValue SlotValue(KX_GraphInstance &instance, const std::string &debug_name)
{
  const KX_ExecutionPlan *plan = instance.GetGraph()->Plan();
  for (size_t node_index = 0; node_index < plan->debug_names.size(); ++node_index) {
    if (plan->debug_names[node_index] != debug_name) {
      continue;
    }
    for (const KX_PortOffsetInfo &info : plan->port_offsets) {
      if (info.node_index == node_index && !info.input && info.name == "Value") {
        return instance.GetContext()->Value(info.offset);
      }
    }
  }
  return KX_RuntimeValue();
}

void TestConstIntSlot()
{
  GraphIR ir;
  const int number = AddNode(ir, "ConstInt", "width");
  SetConfig(ir, number, "{\"value\": 1920}");
  KX_GraphInstance *instance = BuildInstance(ir);
  Check(instance != nullptr, "ConstInt graph compiles", instance == nullptr ? "null" : "ok", "ok");
  if (instance == nullptr) {
    return;
  }
  instance->Tick(0.016f);
  const KX_RuntimeValue value = SlotValue(*instance, "width");
  char actual[64];
  std::snprintf(actual, sizeof(actual), "%d", value.type == KX_VALUE_INT ? value.data.i : -1);
  Check(value.type == KX_VALUE_INT && value.data.i == 1920, "ConstInt slot", actual, "1920");
  delete instance;
}

void TestConstVectorSlot()
{
  GraphIR ir;
  const int gravity = AddNode(ir, "ConstVector", "gravity");
  SetConfig(ir, gravity, "{\"value\": [0, 0, -9.8]}");
  KX_GraphInstance *instance = BuildInstance(ir);
  Check(instance != nullptr, "ConstVector graph compiles", instance == nullptr ? "null" : "ok", "ok");
  if (instance == nullptr) {
    return;
  }
  instance->Tick(0.016f);
  const KX_RuntimeValue value = SlotValue(*instance, "gravity");
  char actual[96];
  std::snprintf(actual,
                sizeof(actual),
                "(%g, %g, %g)",
                value.type == KX_VALUE_VECTOR ? value.data.vec[0] : 0.0f,
                value.type == KX_VALUE_VECTOR ? value.data.vec[1] : 0.0f,
                value.type == KX_VALUE_VECTOR ? value.data.vec[2] : 0.0f);
  Check(value.type == KX_VALUE_VECTOR && std::fabs(value.data.vec[2] + 9.8f) < 1e-5f,
        "ConstVector slot",
        actual,
        "(0, 0, -9.8)");
  delete instance;
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterFunctionGameNodes();

  TestPrintWithoutConstant();
  TestPrintFromConstString();
  TestConstIntSlot();
  TestConstVectorSlot();

  std::printf("RESULT assertions=%d failures=%d\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
