// B6.9-M math / vector E2E.
//
// M-1: Formula RPN gains sin/cos/tan/sqrt/abs/min/max/floor/ceil/round.
//      Domain cases are asserted explicitly: sqrt(-1) -> 0 and x/0 -> 0, so no
//      NaN can spread through the graph; tan(pi/2) must stay finite.
// M-2: VectorLerp / VectorRotate / VectorSlerp / VectorClamp /
//      VectorNormalizeSafe / VectorAngleSigned. Slerp endpoints are exact.
// M-3: Lerp / SmoothStep / Step / PingPong / Repeat with boundary values.
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
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

bool Near(float a, float b, float epsilon = 1e-4f)
{
  return std::fabs(a - b) <= epsilon;
}

bool Near3(const std::vector<float> &values, float x, float y, float z)
{
  return values.size() >= 3 && Near(values[0], x) && Near(values[1], y) && Near(values[2], z);
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

std::vector<float> ParseNumbers(const std::string &text)
{
  std::vector<float> values;
  const char *cursor = text.c_str();
  while (*cursor != '\0') {
    if ((*cursor >= '0' && *cursor <= '9') || *cursor == '-' || *cursor == '.') {
      char *end = nullptr;
      const float value = std::strtof(cursor, &end);
      if (end != cursor) {
        values.push_back(value);
        cursor = end;
        continue;
      }
    }
    ++cursor;
  }
  return values;
}

class StdoutCapture {
 public:
  explicit StdoutCapture(const char *path) : path_(path) {}

  bool Begin()
  {
    std::fflush(stdout);
    saved_fd_ = dup(fileno(stdout));
    if (saved_fd_ < 0) {
      return false;
    }
    return std::freopen(path_, "w", stdout) != nullptr;
  }

  std::vector<std::string> End()
  {
    std::fflush(stdout);
    dup2(saved_fd_, fileno(stdout));
    close(saved_fd_);
    saved_fd_ = -1;
    std::vector<std::string> lines;
    std::ifstream input(path_);
    std::string line;
    while (std::getline(input, line)) {
      lines.push_back(line);
    }
    return lines;
  }

 private:
  const char *path_;
  int saved_fd_ = -1;
};

/* Builds OnInit -> Print and returns the graph plus the Print node index. */
GraphIR NewGraph(int *print_node, KX_ValueType print_tag)
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("Print", "print"));
  ir.nodes[1].inputs[1].value_tag = print_tag;
  Connect(ir, 0, 0, 1, 0);
  *print_node = 1;
  return ir;
}

/* Runs one graph and returns the single printed line parsed into numbers. */
bool RunGraph(GraphIR &ir,
              const char *capture_path,
              std::vector<float> *numbers,
              std::string *raw_line)
{
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> diagnostics;
  const bool loaded = instance.Load(ir, &diagnostics);
  if (!loaded) {
    for (const std::string &diagnostic : diagnostics) {
      std::printf("INFO compile: %s\n", diagnostic.c_str());
    }
    return false;
  }
  StdoutCapture capture(capture_path);
  if (!capture.Begin()) {
    return false;
  }
  instance.Tick(0.016f);
  const std::vector<std::string> lines = capture.End();
  if (lines.empty()) {
    return false;
  }
  if (raw_line != nullptr) {
    *raw_line = lines.front();
  }
  *numbers = ParseNumbers(lines.front());
  return !numbers->empty();
}

/* Adds a Formula node whose expression uses only literals. */
int AddFormula(GraphIR &ir, const std::string &expression, KX_ValueType output_tag)
{
  IRNode node = MakeNode("Formula", "formula");
  node.config_json = "{\"expression\": \"" + expression + "\"}";
  IRPort result;
  result.name = "Result";
  result.kind = IRPortKind::Value;
  result.value_tag = output_tag;
  node.outputs.push_back(result);
  ir.nodes.push_back(node);
  return static_cast<int>(ir.nodes.size()) - 1;
}

int AddFloat(GraphIR &ir, const std::string &value)
{
  IRNode node = MakeNode("ConstFloat", "const");
  node.config_json = "{\"value\": \"" + value + "\"}";
  ir.nodes.push_back(node);
  return static_cast<int>(ir.nodes.size()) - 1;
}

/* Vector3(X, Y, Z) -> Vector. */
int AddVector(GraphIR &ir, float x, float y, float z)
{
  IRNode node = MakeNode("Vector3", "vector");
  ir.nodes.push_back(node);
  const int index = static_cast<int>(ir.nodes.size()) - 1;
  const int x_node = AddFloat(ir, std::to_string(x));
  const int y_node = AddFloat(ir, std::to_string(y));
  const int z_node = AddFloat(ir, std::to_string(z));
  Connect(ir, x_node, 0, index, 0);
  Connect(ir, y_node, 0, index, 1);
  Connect(ir, z_node, 0, index, 2);
  return index;
}

bool RunFormula(const std::string &expression,
                std::vector<float> *numbers,
                std::string *raw_line)
{
  int print_node = 0;
  GraphIR ir = NewGraph(&print_node, KX_VALUE_FLOAT);
  const int formula = AddFormula(ir, expression, KX_VALUE_FLOAT);
  Connect(ir, formula, 0, print_node, 1);
  return RunGraph(ir, "/tmp/b69_m_formula.log", numbers, raw_line);
}

bool RunFloatNode(const std::string &type,
                  const std::vector<std::string> &input_values,
                  std::vector<float> *numbers)
{
  int print_node = 0;
  GraphIR ir = NewGraph(&print_node, KX_VALUE_FLOAT);
  IRNode node = MakeNode(type, type);
  ir.nodes.push_back(node);
  const int index = static_cast<int>(ir.nodes.size()) - 1;
  for (size_t i = 0; i < input_values.size(); ++i) {
    const int constant = AddFloat(ir, input_values[i]);
    Connect(ir, constant, 0, index, static_cast<int>(i));
  }
  Connect(ir, index, 0, print_node, 1);
  return RunGraph(ir, "/tmp/b69_m_float.log", numbers, nullptr);
}

/* Vector node built from literal vectors plus optional float inputs. */
bool RunVectorNode(const std::string &type,
                   const std::vector<std::array<float, 3>> &vectors,
                   const std::vector<std::string> &floats,
                   std::vector<float> *numbers,
                   KX_ValueType print_tag = KX_VALUE_VECTOR)
{
  int print_node = 0;
  GraphIR ir = NewGraph(&print_node, print_tag);
  IRNode node = MakeNode(type, type);
  ir.nodes.push_back(node);
  const int index = static_cast<int>(ir.nodes.size()) - 1;
  for (size_t i = 0; i < vectors.size(); ++i) {
    const int vector = AddVector(ir, vectors[i][0], vectors[i][1], vectors[i][2]);
    Connect(ir, vector, 0, index, static_cast<int>(i));
  }
  for (size_t i = 0; i < floats.size(); ++i) {
    const int constant = AddFloat(ir, floats[i]);
    Connect(ir, constant, 0, index, static_cast<int>(vectors.size() + i));
  }
  Connect(ir, index, 0, print_node, 1);
  return RunGraph(ir, "/tmp/b69_m_vector.log", numbers, nullptr);
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  /* ---- M-1 Formula functions ----------------------------------------- */
  {
    std::vector<float> values;
    Check(RunFormula("sin(1.57079632679)", &values, nullptr) && Near(values[0], 1.0f),
          "Formula: sin(pi/2) = 1");
    Check(RunFormula("cos(0)", &values, nullptr) && Near(values[0], 1.0f),
          "Formula: cos(0) = 1");
    std::string raw;
    Check(RunFormula("tan(1.57079632679)", &values, &raw) && !values.empty() &&
              std::isfinite(values[0]) && std::fabs(values[0]) > 1e6f,
          "Formula: tan(pi/2) is finite and deterministic (no NaN)");
    Check(RunFormula("sqrt(9)", &values, nullptr) && Near(values[0], 3.0f),
          "Formula: sqrt(9) = 3");
    Check(RunFormula("sqrt(-1)", &values, nullptr) && Near(values[0], 0.0f),
          "Formula: sqrt(-1) = 0 (domain guard, no NaN)");
    Check(RunFormula("abs(-3.5)", &values, nullptr) && Near(values[0], 3.5f),
          "Formula: abs(-3.5) = 3.5");
    Check(RunFormula("min(3, 7)", &values, nullptr) && Near(values[0], 3.0f),
          "Formula: min(3, 7) = 3");
    Check(RunFormula("max(3, 7)", &values, nullptr) && Near(values[0], 7.0f),
          "Formula: max(3, 7) = 7");
    Check(RunFormula("floor(2.7)", &values, nullptr) && Near(values[0], 2.0f),
          "Formula: floor(2.7) = 2");
    Check(RunFormula("ceil(2.1)", &values, nullptr) && Near(values[0], 3.0f),
          "Formula: ceil(2.1) = 3");
    Check(RunFormula("round(2.5)", &values, nullptr) && Near(values[0], 3.0f),
          "Formula: round(2.5) = 3");
    Check(RunFormula("1/0", &values, nullptr) && Near(values[0], 0.0f),
          "Formula: 1/0 = 0 (division guard, no NaN)");
  }

  /* ---- M-2 Vector advanced ------------------------------------------- */
  {
    std::vector<float> values;
    Check(RunVectorNode("VectorLerp", {{1, 2, 3}, {4, 5, 6}}, {"0"}, &values) &&
              Near3(values, 1, 2, 3),
          "VectorLerp: t=0 returns A exactly");
    Check(RunVectorNode("VectorLerp", {{1, 2, 3}, {4, 5, 6}}, {"1"}, &values) &&
              Near3(values, 4, 5, 6),
          "VectorLerp: t=1 returns B exactly");
    Check(RunVectorNode("VectorLerp", {{1, 2, 3}, {3, 4, 5}}, {"0.5"}, &values) &&
              Near3(values, 2, 3, 4),
          "VectorLerp: t=0.5 is the midpoint");

    Check(RunVectorNode("VectorSlerp", {{1, 0, 0}, {0, 1, 0}}, {"0"}, &values) &&
              Near3(values, 1, 0, 0),
          "VectorSlerp: t=0 returns A exactly");
    Check(RunVectorNode("VectorSlerp", {{1, 0, 0}, {0, 1, 0}}, {"1"}, &values) &&
              Near3(values, 0, 1, 0),
          "VectorSlerp: t=1 returns B exactly");
    Check(RunVectorNode("VectorSlerp", {{1, 0, 0}, {0, 1, 0}}, {"0.5"}, &values) &&
              Near3(values, 0.70710678f, 0.70710678f, 0.0f),
          "VectorSlerp: t=0.5 of a 90 degree arc is the 45 degree direction");

    Check(RunVectorNode("VectorRotate", {{1, 0, 0}, {0, 0, 1}}, {"1.57079632679"},
                        &values) &&
              Near3(values, 0, 1, 0),
          "VectorRotate: (1,0,0) rotated 90 deg around +Z = (0,1,0)");
    Check(RunVectorNode("VectorRotate", {{1, 2, 3}, {0, 0, 0}}, {"1.0"}, &values) &&
              Near3(values, 1, 2, 3),
          "VectorRotate: zero axis is a deterministic no-op");

    Check(RunVectorNode("VectorClamp", {{5, -5, 0}, {0, -1, -2}, {2, 1, 2}}, {},
                        &values) &&
              Near3(values, 2, -1, 0),
          "VectorClamp: component-wise clamp to [min, max]");

    Check(RunVectorNode("VectorNormalizeSafe", {{3, 4, 0}}, {}, &values) &&
              Near3(values, 0.6f, 0.8f, 0.0f),
          "VectorNormalizeSafe: (3,4,0) -> (0.6,0.8,0)");
    Check(RunVectorNode("VectorNormalizeSafe", {{0, 0, 0}}, {}, &values) &&
              Near3(values, 0, 0, 0),
          "VectorNormalizeSafe: zero vector stays zero (no NaN)");

    Check(RunVectorNode("VectorAngleSigned", {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {},
                        &values, KX_VALUE_FLOAT) &&
              Near(values[0], 1.5707963f),
          "VectorAngleSigned: +90 deg for A->B around +Z");
    Check(RunVectorNode("VectorAngleSigned", {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}}, {},
                        &values, KX_VALUE_FLOAT) &&
              Near(values[0], -1.5707963f),
          "VectorAngleSigned: -90 deg for A->B around -Z");
    Check(RunVectorNode("VectorAngleSigned", {{1, 0, 0}, {0, 1, 0}, {0, 0, 0}}, {},
                        &values, KX_VALUE_FLOAT) &&
              Near(values[0], 1.5707963f),
          "VectorAngleSigned: zero axis falls back to unsigned angle");
  }

  /* ---- M-3 math completion ------------------------------------------ */
  {
    std::vector<float> values;
    Check(RunFloatNode("Lerp", {"2", "8", "0"}, &values) && Near(values[0], 2.0f),
          "Lerp: t=0 returns A");
    Check(RunFloatNode("Lerp", {"2", "8", "1"}, &values) && Near(values[0], 8.0f),
          "Lerp: t=1 returns B");
    Check(RunFloatNode("Lerp", {"2", "8", "0.5"}, &values) && Near(values[0], 5.0f),
          "Lerp: t=0.5 returns the midpoint");

    Check(RunFloatNode("SmoothStep", {"0", "1", "-1"}, &values) && Near(values[0], 0.0f),
          "SmoothStep: below edge0 = 0");
    Check(RunFloatNode("SmoothStep", {"0", "1", "0.5"}, &values) && Near(values[0], 0.5f),
          "SmoothStep: halfway = 0.5");
    Check(RunFloatNode("SmoothStep", {"0", "1", "2"}, &values) && Near(values[0], 1.0f),
          "SmoothStep: above edge1 = 1");
    Check(RunFloatNode("SmoothStep", {"0", "1", "1"}, &values) && Near(values[0], 1.0f),
          "SmoothStep: exactly edge1 = 1");

    Check(RunFloatNode("Step", {"1", "0.999"}, &values) && Near(values[0], 0.0f),
          "Step: just below the edge = 0");
    Check(RunFloatNode("Step", {"1", "1"}, &values) && Near(values[0], 1.0f),
          "Step: exactly on the edge = 1");
    Check(RunFloatNode("Step", {"1", "2"}, &values) && Near(values[0], 1.0f),
          "Step: above the edge = 1");

    Check(RunFloatNode("PingPong", {"0.5", "1"}, &values) && Near(values[0], 0.5f),
          "PingPong: 0.5 within the first leg");
    Check(RunFloatNode("PingPong", {"1.5", "1"}, &values) && Near(values[0], 0.5f),
          "PingPong: 1.5 folds back to 0.5");
    Check(RunFloatNode("PingPong", {"2", "1"}, &values) && Near(values[0], 0.0f),
          "PingPong: 2.0 wraps to 0");
    Check(RunFloatNode("PingPong", {"0.5", "0"}, &values) && Near(values[0], 0.0f),
          "PingPong: zero length = 0 (no NaN)");

    Check(RunFloatNode("Repeat", {"2.5", "1"}, &values) && Near(values[0], 0.5f),
          "Repeat: 2.5 mod 1 = 0.5");
    Check(RunFloatNode("Repeat", {"-0.25", "1"}, &values) && Near(values[0], 0.75f),
          "Repeat: negative input wraps into [0, length)");
    Check(RunFloatNode("Repeat", {"0.5", "0"}, &values) && Near(values[0], 0.0f),
          "Repeat: zero length = 0 (no NaN)");
  }

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

/* B6 TUs are stubbed so this harness links core + B2 + Extra. */
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
