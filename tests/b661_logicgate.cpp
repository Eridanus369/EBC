// B6.6 LogicGate trigger snapshot acceptance.
//
// Unit cases (AND):
//   only A            -> Result false + OnFalse
//   A + B same frame  -> Result true  + OnTrue
//   A frame 1, B 2    -> both frames false
// Matrix: 4 ops x 4 input combinations (none / A / B / AB) = 16 assertions.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_AssetRegistry.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

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

namespace {

int failures = 0;
int assertions = 0;

void Check(bool ok, const char *what)
{
  ++assertions;
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

struct CaseResult {
  float result = -1.0f;
  float true_hits = 0.0f;
  float false_hits = 0.0f;
};

/*
 * Builds a graph that drives LogicGate A/B from the requested sources.
 *   a_on      A is fed by OnInit
 *   b_on      B is fed by OnInit (same frame as A)
 *   b_delayed B is fed by Delay(OnInit) so it arrives one frame later
 * Inputs that stay false are fed by OnUpdate -> Branch(false).OnTrue.
 */
GraphIR BuildGraph(const std::string &op,
                   bool a_on,
                   bool b_on,
                   bool b_delayed,
                   const std::string &asset_name,
                   int *gate_index,
                   int *one_index)
{
  GraphIR ir;
  int gate = -1;
  int one = -1;

  auto add = [&](const std::string &type, const std::string &name) {
    ir.nodes.push_back(MakeNode(type, name));
    return static_cast<int>(ir.nodes.size()) - 1;
  };

  /* Node 0 is always the gate so debug traces read naturally. */
  gate = add("LogicGate", "gate");
  ir.nodes[gate].config_json = "{\"op\": \"" + op + "\"}";
  one = add("ConstFloat", "one");
  ir.nodes[one].config_json = "{\"value\": \"1\"}";

  auto add_never_source = [&](const std::string &suffix, int target_port) {
    const int update = add("OnUpdate", "update_" + suffix);
    const int no = add("ConstBool", "no_" + suffix);
    ir.nodes[no].config_json = "{\"value\": \"false\"}";
    const int branch = add("Branch", "branch_" + suffix);
    Connect(ir, update, 0, branch, 0);
    Connect(ir, no, 0, branch, 1);
    Connect(ir, branch, 0, gate, target_port);
  };

  if (a_on) {
    const int init = add("OnInit", "a_init");
    Connect(ir, init, 0, gate, 0);
  }
  else {
    add_never_source("a", 0);
  }

  if (b_delayed) {
    const int init = add("OnInit", "b_init");
    const int half = add("ConstFloat", "half");
    ir.nodes[half].config_json = "{\"value\": \"0.5\"}";
    const int delay = add("Delay", "b_delay");
    Connect(ir, init, 0, delay, 0);
    Connect(ir, half, 0, delay, 1);
    Connect(ir, delay, 0, gate, 1);
  }
  else if (b_on) {
    const int init = add("OnInit", "b_init");
    Connect(ir, init, 0, gate, 1);
  }
  else {
    add_never_source("b", 1);
  }

  /* Path counters and Result observation. Each path needs its own writer
   * because a node input only accepts a single connection. */
  const char *debug_names[] = {"result_true", "result_false", "true_hits", "false_hits"};
  const char *fields[] = {"result", "result", "true_hits", "false_hits"};
  int writers[4] = {-1, -1, -1, -1};
  for (int index = 0; index < 4; ++index) {
    writers[index] = add("AssetSet", std::string("write_") + debug_names[index]);
    ir.nodes[writers[index]].inputs[1].value_tag = KX_VALUE_FLOAT;
    ir.nodes[writers[index]].config_json = "{\"asset_ref\": \"" + asset_name +
                                          "\", \"field\": \"" + fields[index] +
                                          "\", \"asset_type\": \"GateLog\"}";
  }

  /* OnTrue: result <- Result, true_hits <- 1. */
  Connect(ir, gate, 1, writers[0], 0);
  Connect(ir, gate, 0, writers[0], 1);
  Connect(ir, gate, 1, writers[2], 0);
  Connect(ir, one, 0, writers[2], 1);
  /* OnFalse: result <- Result, false_hits <- 1. */
  Connect(ir, gate, 2, writers[1], 0);
  Connect(ir, gate, 0, writers[1], 1);
  Connect(ir, gate, 2, writers[3], 0);
  Connect(ir, one, 0, writers[3], 1);

  *gate_index = gate;
  *one_index = one;
  return ir;
}

int g_asset_serial = 0;

bool RunCase(const std::string &op,
             bool a_on,
             bool b_on,
             bool b_delayed,
             int frames,
             CaseResult *out,
             std::string *diagnostic,
             bool reset_after_first_frame = false)
{
  KX_AssetRegistry &registry = KX_AssetRegistry::Get();
  const std::string asset_name = "gate_log_" + std::to_string(++g_asset_serial);
  std::unique_ptr<KX_GameDataAsset> asset =
      std::make_unique<KX_GameDataAsset>(asset_name, "GateLog");
  std::string error;
  asset->AddField("result", KX_AssetFieldType::Float, KX_RuntimeValue::Float(-1.0f), &error);
  asset->AddField("true_hits", KX_AssetFieldType::Float, KX_RuntimeValue::Float(0.0f), &error);
  asset->AddField("false_hits", KX_AssetFieldType::Float, KX_RuntimeValue::Float(0.0f), &error);
  const KX_AssetHandle handle = registry.Register(std::move(asset));
  KX_GameDataAsset *log = registry.Get(handle);

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  int gate = -1;
  int one = -1;
  const GraphIR graph = BuildGraph(op, a_on, b_on, b_delayed, asset_name, &gate, &one);
  std::vector<std::string> diagnostics;
  if (!instance.Load(graph, &diagnostics)) {
    for (const std::string &line : diagnostics) {
      std::printf("INFO compile %s: %s\n", op.c_str(), line.c_str());
    }
    if (diagnostic != nullptr && !diagnostics.empty()) {
      *diagnostic = diagnostics.front();
    }
    return false;
  }

  for (int frame = 0; frame < frames; ++frame) {
    instance.Tick(0.5f);
    if (reset_after_first_frame && frame == 0) {
      /* Clear the observations so frame 2 has to write them again: this proves
       * the gate ran on the frame where B arrived, not just on frame 1. */
      for (uint32_t index = 0; index < log->FieldCount(); ++index) {
        const std::string &name = log->Field(index).name;
        if (name == "result") {
          log->WriteField(index, KX_RuntimeValue::Float(-1.0f));
        }
        else {
          log->WriteField(index, KX_RuntimeValue::Float(0.0f));
        }
      }
    }
  }

  auto read = [&](const char *field) {
    for (uint32_t index = 0; index < log->FieldCount(); ++index) {
      if (log->Field(index).name == field) {
        return log->ReadField(index).data.f;
      }
    }
    return -999.0f;
  };
  out->result = read("result");
  out->true_hits = read("true_hits");
  out->false_hits = read("false_hits");
  return true;
}

bool Near(float a, float b)
{
  return (a - b) < 0.0001f && (b - a) < 0.0001f;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();
  KX_AssetRegistry::Get().Clear();

  /* ----- Unit cases, AND -------------------------------------------------- */
  CaseResult unit;
  std::string diagnostic;
  Check(RunCase("and", true, false, false, 2, &unit, &diagnostic), "unit builds A-only graph");
  std::printf("INFO unit A-only result=%g true=%g false=%g\n",
              static_cast<double>(unit.result),
              static_cast<double>(unit.true_hits),
              static_cast<double>(unit.false_hits));
  Check(Near(unit.result, 0.0f) && Near(unit.true_hits, 0.0f) && Near(unit.false_hits, 1.0f),
        "unit only A: Result=false + OnFalse");

  Check(RunCase("and", true, true, false, 1, &unit, &diagnostic), "unit builds AB graph");
  std::printf("INFO unit AB result=%g true=%g false=%g\n",
              static_cast<double>(unit.result),
              static_cast<double>(unit.true_hits),
              static_cast<double>(unit.false_hits));
  Check(Near(unit.result, 1.0f) && Near(unit.true_hits, 1.0f) && Near(unit.false_hits, 0.0f),
        "unit A+B same frame: Result=true + OnTrue");

  Check(RunCase("and", true, false, true, 2, &unit, &diagnostic, true),
        "unit builds A frame1 / B frame2 graph");
  std::printf("INFO unit split (frame 2 only) result=%g true=%g false=%g\n",
              static_cast<double>(unit.result),
              static_cast<double>(unit.true_hits),
              static_cast<double>(unit.false_hits));
  Check(Near(unit.result, 0.0f) && Near(unit.true_hits, 0.0f) && Near(unit.false_hits, 1.0f),
        "unit A frame1 + B frame2: both frames false");

  /* ----- Matrix: 4 ops x 4 combinations -------------------------------- */
  struct Expectation {
    const char *op;
    bool a_on;
    bool b_on;
    bool executes;
    float result;
  };
  const Expectation expectations[] = {
      {"and", false, false, false, -1.0f},
      {"and", true, false, true, 0.0f},
      {"and", false, true, true, 0.0f},
      {"and", true, true, true, 1.0f},
      {"or", false, false, false, -1.0f},
      {"or", true, false, true, 1.0f},
      {"or", false, true, true, 1.0f},
      {"or", true, true, true, 1.0f},
      {"xor", false, false, false, -1.0f},
      {"xor", true, false, true, 1.0f},
      {"xor", false, true, true, 1.0f},
      {"xor", true, true, true, 0.0f},
      {"not", false, false, false, -1.0f},
      {"not", true, false, true, 0.0f},
      {"not", false, true, true, 1.0f},
      {"not", true, true, true, 0.0f},
  };

  for (const Expectation &expectation : expectations) {
    const bool built = RunCase(expectation.op,
                               expectation.a_on,
                               expectation.b_on,
                               false,
                               expectation.executes ? 2 : 2,
                               &unit,
                               &diagnostic);
    char label[160];
    std::snprintf(label,
                  sizeof(label),
                  "matrix op=%s A=%d B=%d",
                  expectation.op,
                  expectation.a_on ? 1 : 0,
                  expectation.b_on ? 1 : 0);
    std::printf("INFO %s result=%g true=%g false=%g\n",
                label,
                static_cast<double>(unit.result),
                static_cast<double>(unit.true_hits),
                static_cast<double>(unit.false_hits));
    const bool ok = built &&
                    Near(unit.result, expectation.result) &&
                    (expectation.executes ? (Near(unit.true_hits, expectation.result) &&
                                             Near(unit.false_hits, 1.0f - expectation.result))
                                          : (Near(unit.true_hits, 0.0f) &&
                                             Near(unit.false_hits, 0.0f)));
    Check(ok, label);
  }

  std::printf("RESULT assertions=%d failures=%d\n", assertions, failures);
  return failures == 0 ? 0 : 1;
}
