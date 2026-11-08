// B6.9-F flow control E2E: nested ForEach, While, Switch, Sequence.
//
// The assertions deliberately look at *combination points* instead of totals:
//   - nested ForEach prints "10 1 2 3 20 1 2 3", so index 4/5 prove the inner
//     loop restarted on the second outer pass (total count 6 alone would pass a
//     stale-cursor implementation that prints 10 1 2 3 20 3).
//   - While(i<10) prints exactly ten indices in order, then OnDone.
//   - While(i>=0) with max_iterations=5 stops after five and writes a
//     diagnostic to stderr.
//   - Switch fires exactly one of Case0..Case3 (Default for out-of-range).
//   - Sequence serial fires Out0, Out1, Out2 in order from one OnEnter.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
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

/* Captures C stdout (Print uses std::printf) for the duration of one Tick. */
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
    return freopen(path_, "w", stdout) != nullptr;
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

/* Loads, compiles and ticks one graph with stdout redirected. Compilation
 * messages are reported before the redirect so the capture contains printed
 * data only. */
std::vector<std::string> RunCaptured(KX_GraphInstance &instance,
                                     const GraphIR &graph,
                                     const char *path,
                                     bool *loaded_out)
{
  std::vector<std::string> diagnostics;
  const bool loaded = instance.Load(graph, &diagnostics);
  *loaded_out = loaded;
  for (const std::string &diagnostic : diagnostics) {
    std::printf("INFO compile: %s\n", diagnostic.c_str());
  }
  if (!loaded) {
    return {};
  }
  StdoutCapture capture(path);
  if (!capture.Begin()) {
    *loaded_out = false;
    return {};
  }
  instance.Tick(0.016f);
  return capture.End();
}

/* 10, 1, 2, 3, 20, 1, 2, 3: the outer list is [10, 20], the shared inner loop
 * is [1, 2, 3] must restart for the second outer element. */
GraphIR BuildNestedForEachGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));       /* 0 */
  ir.nodes.push_back(MakeNode("ConstFloat", "outer_a")); /* 1 */
  ir.nodes.push_back(MakeNode("ConstFloat", "outer_b")); /* 2 */
  ir.nodes.push_back(MakeNode("ListAdd", "outer_l1"));  /* 3 */
  ir.nodes.push_back(MakeNode("ListAdd", "outer_l2"));  /* 4 */
  ir.nodes.push_back(MakeNode("ConstFloat", "inner_a")); /* 5 */
  ir.nodes.push_back(MakeNode("ConstFloat", "inner_b")); /* 6 */
  ir.nodes.push_back(MakeNode("ConstFloat", "inner_c")); /* 7 */
  ir.nodes.push_back(MakeNode("ListAdd", "inner_l1"));  /* 8 */
  ir.nodes.push_back(MakeNode("ListAdd", "inner_l2"));  /* 9 */
  ir.nodes.push_back(MakeNode("ListAdd", "inner_l3"));  /* 10 */
  ir.nodes.push_back(MakeNode("ForEach", "outer"));     /* 11 */
  ir.nodes.push_back(MakeNode("ForEach", "inner"));     /* 12 */
  ir.nodes.push_back(MakeNode("Print", "print_outer")); /* 13 */
  ir.nodes.push_back(MakeNode("Print", "print_inner")); /* 14 */

  ir.nodes[1].config_json = "{\"value\": \"10\"}";
  ir.nodes[2].config_json = "{\"value\": \"20\"}";
  ir.nodes[5].config_json = "{\"value\": \"1\"}";
  ir.nodes[6].config_json = "{\"value\": \"2\"}";
  ir.nodes[7].config_json = "{\"value\": \"3\"}";
  ir.nodes[13].inputs[1].value_tag = KX_VALUE_FLOAT;
  ir.nodes[14].inputs[1].value_tag = KX_VALUE_FLOAT;

  /* outer list [10, 20] */
  Connect(ir, 1, 0, 3, 1);
  Connect(ir, 3, 0, 4, 0);
  Connect(ir, 2, 0, 4, 1);
  /* inner list [1, 2, 3] */
  Connect(ir, 5, 0, 8, 1);
  Connect(ir, 8, 0, 9, 0);
  Connect(ir, 6, 0, 9, 1);
  Connect(ir, 9, 0, 10, 0);
  Connect(ir, 7, 0, 10, 1);

  Connect(ir, 4, 0, 11, 0);   /* outer list */
  Connect(ir, 0, 0, 11, 1);   /* OnEnter */
  Connect(ir, 10, 0, 12, 0);  /* inner list */
  Connect(ir, 11, 0, 12, 1);  /* inner OnEnter = outer OnEach */
  Connect(ir, 11, 1, 13, 1);  /* outer element */
  Connect(ir, 11, 0, 13, 0);  /* print outer first */
  Connect(ir, 12, 1, 14, 1);  /* inner element */
  Connect(ir, 12, 0, 14, 0);  /* print inner */
  return ir;
}

GraphIR BuildWhileGraph(const char *config)
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));       /* 0 */
  ir.nodes.push_back(MakeNode("While", "while"));       /* 1 */
  ir.nodes.push_back(MakeNode("Print", "print_index")); /* 2 */
  ir.nodes.push_back(MakeNode("ConstFloat", "done"));   /* 3 */
  ir.nodes.push_back(MakeNode("Print", "print_done"));  /* 4 */

  ir.nodes[1].config_json = config;
  ir.nodes[3].config_json = "{\"value\": \"99\"}";
  ir.nodes[2].inputs[1].value_tag = KX_VALUE_INT;
  ir.nodes[4].inputs[1].value_tag = KX_VALUE_FLOAT;

  Connect(ir, 0, 0, 1, 0);   /* OnInit -> While OnEnter */
  Connect(ir, 1, 0, 2, 0);   /* OnIteration */
  Connect(ir, 1, 1, 2, 1);   /* Index */
  Connect(ir, 1, 2, 4, 0);   /* OnDone */
  Connect(ir, 3, 0, 4, 1);   /* 99 */
  return ir;
}

GraphIR BuildWhilePortGraph(const std::string &config,
                            const std::string &threshold,
                            const std::string &step)
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));          /* 0 */
  ir.nodes.push_back(MakeNode("While", "while"));          /* 1 */
  ir.nodes.push_back(MakeNode("Print", "print_index"));     /* 2 */
  ir.nodes.push_back(MakeNode("ConstFloat", "done"));      /* 3 */
  ir.nodes.push_back(MakeNode("Print", "print_done"));      /* 4 */
  ir.nodes.push_back(MakeNode("ConstFloat", "threshold"));  /* 5 */
  ir.nodes.push_back(MakeNode("ConstFloat", "step"));      /* 6 */

  ir.nodes[1].config_json = config;
  ir.nodes[3].config_json = "{\"value\": \"99\"}";
  ir.nodes[5].config_json = "{\"value\": \"" + threshold + "\"}";
  ir.nodes[6].config_json = "{\"value\": \"" + step + "\"}";
  ir.nodes[2].inputs[1].value_tag = KX_VALUE_INT;
  ir.nodes[4].inputs[1].value_tag = KX_VALUE_FLOAT;

  Connect(ir, 0, 0, 1, 0);   /* OnInit -> OnEnter */
  Connect(ir, 1, 0, 2, 0);   /* OnIteration */
  Connect(ir, 1, 1, 2, 1);   /* Index */
  Connect(ir, 1, 2, 4, 0);   /* OnDone */
  Connect(ir, 3, 0, 4, 1);   /* 99 */
  Connect(ir, 5, 0, 1, 2);   /* Threshold input */
  Connect(ir, 6, 0, 1, 3);   /* Step input */
  return ir;
}

GraphIR BuildSwitchGraph(float selector, const std::string &config)
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));          /* 0 */
  ir.nodes.push_back(MakeNode("ConstInt", "selector"));    /* 1 */
  ir.nodes.push_back(MakeNode("Switch", "switch"));        /* 2 */
  const char *case_labels[] = {"case0", "case1", "case2", "case3", "default"};
  const int label_values[] = {0, 1, 2, 3, 99};
  int next = 3;
  for (int index = 0; index < 5; ++index) {
    /* Distinct printer value per branch so a firing proves *which* branch ran,
     * not just that one branch ran. */
    ir.nodes.push_back(MakeNode("ConstFloat", case_labels[index]));
    ir.nodes[next].config_json = "{\"value\": \"" + std::to_string(label_values[index]) + "\"}";
    ir.nodes.push_back(MakeNode("Print", case_labels[index]));
    const int value_node = next;
    const int print_node = next + 1;
    ir.nodes[print_node].inputs[1].value_tag = KX_VALUE_FLOAT;
    Connect(ir, value_node, 0, print_node, 1);
    Connect(ir, 2, index, print_node, 0);
    next += 2;
  }
  ir.nodes[1].config_json = "{\"value\": \"" + std::to_string(static_cast<int>(selector)) + "\"}";
  ir.nodes[2].config_json = config;
  Connect(ir, 0, 0, 2, 0);
  Connect(ir, 1, 0, 2, 1);
  return ir;
}

GraphIR BuildSequenceGraph(const std::string &config)
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));     /* 0 */
  ir.nodes.push_back(MakeNode("Sequence", "sequence")); /* 1 */
  ir.nodes.push_back(MakeNode("ConstFloat", "a"));    /* 2 */
  ir.nodes.push_back(MakeNode("ConstFloat", "b"));    /* 3 */
  ir.nodes.push_back(MakeNode("ConstFloat", "c"));    /* 4 */
  ir.nodes.push_back(MakeNode("Print", "print_a"));   /* 5 */
  ir.nodes.push_back(MakeNode("Print", "print_b"));   /* 6 */
  ir.nodes.push_back(MakeNode("Print", "print_c"));   /* 7 */

  ir.nodes[1].config_json = config;
  ir.nodes[2].config_json = "{\"value\": \"30\"}";
  ir.nodes[3].config_json = "{\"value\": \"40\"}";
  ir.nodes[4].config_json = "{\"value\": \"50\"}";
  for (int index = 5; index <= 7; ++index) {
    ir.nodes[index].inputs[1].value_tag = KX_VALUE_FLOAT;
  }
  Connect(ir, 0, 0, 1, 0);
  Connect(ir, 1, 0, 5, 0);
  Connect(ir, 2, 0, 5, 1);
  Connect(ir, 1, 1, 6, 0);
  Connect(ir, 3, 0, 6, 1);
  Connect(ir, 1, 2, 7, 0);
  Connect(ir, 4, 0, 7, 1);
  return ir;
}

GraphIR BuildSequenceParallelFrameGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));            /* 0 */
  ir.nodes.push_back(MakeNode("Sequence", "sequence"));      /* 1 */
  ir.nodes.push_back(MakeNode("FrameTime", "frame"));        /* 2 */
  ir.nodes.push_back(MakeNode("Print", "print_f0"));         /* 3 */
  ir.nodes.push_back(MakeNode("Print", "print_f1"));         /* 4 */
  ir.nodes.push_back(MakeNode("Print", "print_f2"));         /* 5 */

  ir.nodes[1].config_json = "{\"mode\": \"parallel\"}";
  for (int index = 3; index <= 5; ++index) {
    ir.nodes[index].inputs[1].value_tag = KX_VALUE_INT;
  }

  Connect(ir, 0, 0, 1, 0);
  for (int index = 0; index < 3; ++index) {
    Connect(ir, 1, index, 3 + index, 0);  /* all branches from the same OnEnter */
    Connect(ir, 2, 2, 3 + index, 1);      /* Frame counter */
  }
  return ir;
}

bool LinesAre(const std::vector<std::string> &lines, const std::vector<std::string> &expected)
{
  if (lines.size() != expected.size()) {
    return false;
  }
  for (size_t index = 0; index < expected.size(); ++index) {
    if (lines[index] != expected[index]) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  /* stderr is held in a scratch file so the While overflow diagnostic can be
   * asserted instead of only being printed. */
  std::fflush(stderr);
  const int saved_stderr = dup(fileno(stderr));
  if (std::freopen("/tmp/b69_f_stderr.log", "w", stderr) == nullptr) {
    std::printf("FAIL could not redirect stderr\n");
    ++failures;
  }

  /* ---- F-1 nested ForEach --------------------------------------------- */
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance, BuildNestedForEachGraph(), "/tmp/b69_f_nested.log", &loaded);
    Check(loaded, "nested ForEach graph compiled");
    Check(LinesAre(lines, {"10", "1", "2", "3", "20", "1", "2", "3"}),
          "nested ForEach: second outer pass restarts at inner element 1 "
          "(10 1 2 3 20 1 2 3)");
    Check(lines.size() == 8, "nested ForEach: exactly 2 outer x 3 inner elements");
    if (lines.size() >= 6) {
      Check(lines[4] == "20", "nested ForEach: combination point outer#2 reached");
      Check(lines[5] == "1", "nested ForEach: combination point outer#2 / inner#1");
    }
  }

  /* ---- F-2 While(i<10) ------------------------------------------------- */
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines =
        RunCaptured(instance,
                    BuildWhileGraph("{\"condition\": \"i<10\", \"max_iterations\": 100}"),
                    "/tmp/b69_f_while.log",
                    &loaded);
    Check(loaded, "While(i<10) graph compiled");
    std::vector<std::string> expected;
    for (int index = 0; index < 10; ++index) {
      expected.push_back(std::to_string(index));
    }
    expected.push_back("99");
    Check(LinesAre(lines, expected), "While(i<10): fires 10 times in order then OnDone");
    Check(lines.size() == 11, "While(i<10): 10 iterations + 1 OnDone");
  }

  /* ---- F-2 While overflow --------------------------------------------- */
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines =
        RunCaptured(instance,
                    BuildWhileGraph("{\"condition\": \"i>=0\", \"max_iterations\": 5}"),
                    "/tmp/b69_f_while_max.log",
                    &loaded);
    Check(loaded, "While(i>=0, max=5) graph compiled");
    Check(LinesAre(lines, {"0", "1", "2", "3", "4", "99"}),
          "While overflow: stops at max_iterations=5 and still emits OnDone");
    std::fflush(stderr);
    std::string stderr_text;
    {
      std::ifstream input("/tmp/b69_f_stderr.log");
      stderr_text.assign(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    }
    Check(stderr_text.find("[While] stopped at max_iterations=5") != std::string::npos,
          "While overflow: diagnostic written to stderr");
  }

  /* ---- 3.2 While float condition + arbitrary input ports --------------- */
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance,
        BuildWhileGraph("{\"condition\": \"i<2.5\", \"max_iterations\": 100}"),
        "/tmp/b69_f_while_float.log",
        &loaded);
    Check(loaded, "While float threshold graph compiled");
    Check(LinesAre(lines, {"0", "1", "2", "99"}),
          "While float threshold: i<2.5 fires three times (0 1 2)");
  }
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance,
        BuildWhilePortGraph(
            "{\"condition\": \"i<Threshold\", \"max_iterations\": 100}", "4.5", "1"),
        "/tmp/b69_f_while_port.log",
        &loaded);
    Check(loaded, "While Threshold port graph compiled");
    Check(LinesAre(lines, {"0", "1", "2", "3", "4", "99"}),
          "While arbitrary port: i<Threshold with Threshold=4.5 fires five times");
  }
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance,
        BuildWhilePortGraph(
            "{\"condition\": \"i*Step<Threshold\", \"max_iterations\": 100}",
            "7.5",
            "2.5"),
        "/tmp/b69_f_while_two_ports.log",
        &loaded);
    Check(loaded, "While two-port graph compiled");
    Check(LinesAre(lines, {"0", "1", "2", "99"}),
          "While arbitrary ports: i*Step<Threshold with Step=2.5 Threshold=7.5 fires three times");
  }
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance,
        BuildWhileGraph("{\"condition\": \"i<Missing\", \"max_iterations\": 5}"),
        "/tmp/b69_f_while_unknown.log",
        &loaded);
    Check(loaded, "While unknown identifier graph compiled");
    Check(LinesAre(lines, {"99"}),
          "While unknown identifier: zero iterations and OnDone instead of silent spin");
    std::fflush(stderr);
    std::string stderr_text;
    {
      std::ifstream input("/tmp/b69_f_stderr.log");
      stderr_text.assign(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    }
    Check(stderr_text.find("invalid condition 'i<Missing'") != std::string::npos,
          "While unknown identifier: diagnostic lists the expression");
  }

  /* ---- F-3 Switch ------------------------------------------------------ */
  for (int selector = 0; selector < 4; ++selector) {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance, BuildSwitchGraph(float(selector), "{}"), "/tmp/b69_f_switch.log", &loaded);
    char label[96];
    std::snprintf(label, sizeof(label), "Switch selector %d graph compiled", selector);
    Check(loaded, label);
    char assertion[128];
    std::snprintf(assertion, sizeof(assertion),
                  "Switch: selector %d fires only Case%d", selector, selector);
    Check(LinesAre(lines, {std::to_string(selector)}), assertion);
  }
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance, BuildSwitchGraph(9.0f, "{}"), "/tmp/b69_f_switch_default.log", &loaded);
    Check(loaded, "Switch default graph compiled");
    Check(LinesAre(lines, {"99"}), "Switch: out-of-range selector fires only Default");
  }

  /* ---- F-4 Sequence ---------------------------------------------------- */
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance, BuildSequenceGraph("{}"), "/tmp/b69_f_sequence.log", &loaded);
    Check(loaded, "Sequence serial graph compiled");
    Check(LinesAre(lines, {"30", "40", "50"}),
          "Sequence serial: three branches fire in order (30 40 50)");
  }
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines =
        RunCaptured(instance, BuildSequenceGraph("{\"mode\": \"parallel\"}"),
                    "/tmp/b69_f_sequence_parallel.log", &loaded);
    Check(loaded, "Sequence parallel graph compiled");
    Check(LinesAre(lines, {"30", "40", "50"}),
          "Sequence parallel: three branches fire (30 40 50)");
  }
  {
    KX_GraphInstance instance(nullptr, nullptr, nullptr);
    bool loaded = false;
    const std::vector<std::string> lines = RunCaptured(
        instance,
        BuildSequenceParallelFrameGraph(),
        "/tmp/b69_f_sequence_parallel_frame.log",
        &loaded);
    Check(loaded, "Sequence parallel frame graph compiled");
    Check(lines.size() == 3 && lines[0] == lines[1] && lines[1] == lines[2],
          "Sequence parallel: all three branches run on the same frame "
          "(identical FrameTime counter)");
  }

  std::fflush(stderr);
  dup2(saved_stderr, fileno(stderr));
  close(saved_stderr);

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

/* B6 TUs are stubbed so this flow harness links only core + B2. */
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
