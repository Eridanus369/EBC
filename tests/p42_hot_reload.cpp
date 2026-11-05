// P4.2 end to end: running player loop polls .hot_reload.flag, re-reads the
// manifest, rebuilds the mounted graph and the new behavior is visible in the
// same frame.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_GraphSceneManager.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

/* The B5.2 source is being written by a parallel line; stub it so the harness
 * links without that translation unit. */
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

class MockUI : public KX_UIAdapter {
 public:
  int canvases = 0;
  int labels = 0;
  int buttons = 0;
  int CreateCanvas(const std::string &) override { return 100 + canvases++; }
  int CreateLabel(int, const std::string &) override { return 200 + labels++; }
  int CreateButton(int, const std::string &) override { return 300 + buttons++; }
  bool SetAttribute(int, const std::string &, const KX_RuntimeValue &) override { return true; }
};

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

/** Graph A (definition at player start): OnInit -> CreateUICanvas. */
GraphIR BuildStartGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("CreateUICanvas", "canvas"));
  Connect(ir, 0, 0, 1, 0);
  return ir;
}

/** Graph B (edited while running): plus a label and a button under the canvas. */
GraphIR BuildEditedGraph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("CreateUICanvas", "canvas"));
  ir.nodes.push_back(MakeNode("CreateUILabel", "label"));
  ir.nodes.push_back(MakeNode("CreateUIButton", "button"));
  Connect(ir, 0, 0, 1, 0);
  Connect(ir, 1, 0, 2, 0);
  Connect(ir, 1, 1, 2, 1);
  Connect(ir, 1, 0, 3, 0);
  Connect(ir, 1, 1, 3, 1);
  return ir;
}

const char *KindName(IRPortKind kind)
{
  return kind == IRPortKind::Trigger ? "trigger" : "value";
}

const char *ValueTagName(KX_ValueType tag)
{
  switch (tag) {
    case KX_VALUE_NONE:
      return "None";
    case KX_VALUE_BOOL:
      return "Bool";
    case KX_VALUE_INT:
      return "Int";
    case KX_VALUE_FLOAT:
      return "Float";
    case KX_VALUE_VECTOR:
      return "Vector";
    case KX_VALUE_COLOR:
      return "Color";
    case KX_VALUE_STRING:
      return "String";
    case KX_VALUE_LIST:
      return "List";
    case KX_VALUE_DATABLOCK:
      return "DataBlock";
  }
  return "None";
}

nlohmann::json PortsJson(const std::vector<IRPort> &ports)
{
  nlohmann::json array = nlohmann::json::array();
  for (const IRPort &port : ports) {
    nlohmann::json entry;
    entry["name"] = port.name;
    entry["kind"] = KindName(port.kind);
    entry["value_tag"] = ValueTagName(port.value_tag);
    array.push_back(std::move(entry));
  }
  return array;
}

/** Same JSON shape the editor exporter writes into scene["game_graph_manifest"]. */
std::string SerializeManifest(const GraphIR &ir, const std::string &graph_id)
{
  nlohmann::json root;
  root["assets"] = nlohmann::json::array();
  nlohmann::json graphs = nlohmann::json::array();
  nlohmann::json graph;
  graph["graph_id"] = graph_id;
  nlohmann::json nodes = nlohmann::json::array();
  for (const IRNode &node : ir.nodes) {
    nlohmann::json entry;
    entry["type_id"] = node.type_id;
    entry["debug_name"] = node.debug_name;
    entry["inputs"] = PortsJson(node.inputs);
    entry["outputs"] = PortsJson(node.outputs);
    if (!node.config_json.empty()) {
      entry["config"] = nlohmann::json::parse(node.config_json);
    }
    nodes.push_back(std::move(entry));
  }
  graph["nodes"] = std::move(nodes);
  nlohmann::json connections = nlohmann::json::array();
  for (const IRConnection &connection : ir.connections) {
    nlohmann::json entry;
    entry["from_node"] = connection.from_node;
    entry["from_port"] = connection.from_port;
    entry["to_node"] = connection.to_node;
    entry["to_port"] = connection.to_port;
    connections.push_back(std::move(entry));
  }
  graph["connections"] = std::move(connections);
  graphs.push_back(std::move(graph));
  root["graphs"] = std::move(graphs);
  root["mounts"] = nlohmann::json::array();
  return root.dump();
}

void WriteFile(const std::string &path, const std::string &content)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  const std::string dir = "/tmp/p4_hot_dir";
  const std::string flag_path = dir + "/.hot_reload.flag";
  const std::string manifest_path = dir + "/.hot_reload_manifest.json";
  std::system("mkdir -p /tmp/p4_hot_dir");

  WriteFile(manifest_path, SerializeManifest(BuildStartGraph(), "graph_a"));
  WriteFile(flag_path, "v1");

  KX_GraphSceneManager manager;
  manager.SetHotReloadSources(
      [flag_path]() { return flag_path; },
      [manifest_path]() {
        std::ifstream in(manifest_path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
      });

  MockUI ui;
  KX_GraphInstance *instance =
      manager.MountGraph(nullptr, "graph_a", BuildStartGraph());
  Check(instance != nullptr, "player mounted the start graph");
  instance->GetContext()->SetAdapters(
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ui, nullptr);

  manager.Tick(0.016f);
  Check(ui.canvases == 1, "start graph created the canvas");

  /* Prime the flag baseline the same way a scene load would. */
  manager.CheckHotReload();
  manager.Tick(0.016f);
  Check(ui.canvases == 1, "unchanged flag does not duplicate the canvas");

  /* Player is running: the editor saves the edited graph and touches the flag. */
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  WriteFile(manifest_path, SerializeManifest(BuildEditedGraph(), "graph_a"));
  WriteFile(flag_path, "v2");
  /* Container filesystems may quantize mtimes to a whole second, so advance it
   * explicitly instead of relying on the wall clock. */
  std::error_code mtime_error;
  const auto previous = std::filesystem::last_write_time(flag_path, mtime_error);
  std::printf("INFO mtime_error=%d previous=%lld\n", mtime_error.value(), (long long)previous.time_since_epoch().count());
  std::filesystem::last_write_time(
      flag_path,
      previous + std::chrono::seconds(2),
      mtime_error);

  manager.Tick(0.016f);
  Check(manager.InstanceCount() == 1, "rebuild reuses the mounted instance");
  Check(instance->GetPlan()->nodes.size() == 4, "edited graph installed on the next frame");
  Check(ui.canvases == 1, "canvas handle survived the hot reload");
  Check(ui.labels == 1, "new label node executed after the reload");
  Check(ui.buttons == 1, "new button node executed after the reload");

  /* Second frame proves the migrated handles are not recreated. */
  manager.Tick(0.016f);
  Check(ui.canvases == 1 && ui.labels == 1 && ui.buttons == 1,
        "no duplicate UI elements after the reload");

  std::printf("RESULT failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}

/* B6.2 tables are not linked into this regression harness. */
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}

/* Additional B6 register entry points not linked into this harness. */
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}
