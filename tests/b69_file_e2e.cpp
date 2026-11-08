/*
 * B6.9-Fi E2E: FileReadChunk / FileWriteChunk / ListDirectory /
 * CreateDirectory / RemoveDirectory against the in-memory file adapter.
 *
 * Frame progression is explicit (Tick with a fixed delta); no sleeps.
 * Exit code is 0 only when every assertion passes.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_FileAdapter.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool condition, const std::string &label)
{
  if (condition) {
    ++g_pass;
    std::printf("PASS %s\n", label.c_str());
  }
  else {
    ++g_fail;
    std::printf("FAIL %s\n", label.c_str());
  }
}

const int kGateCount = 14;
bool g_gates[kGateCount] = {false};
int g_ints[4] = {0};
int g_sink_hits[12] = {0};

class TestGateNode : public KX_GameNodeRuntime {
 public:
  explicit TestGateNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    SetOutputTrigger(ctx, 0, g_gates[slot_]);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

class TestSinkNode : public KX_GameNodeRuntime {
 public:
  explicit TestSinkNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      ++g_sink_hits[slot_];
    }
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

class TestIntNode : public KX_GameNodeRuntime {
 public:
  explicit TestIntNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Int(g_ints[slot_]);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

KX_NodeSignature GateSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Out";
  output.kind = IRPortKind::Trigger;
  output.value_type = KX_VALUE_NONE;
  signature.outputs.push_back(output);
  return signature;
}

KX_NodeSignature SinkSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature input;
  input.name = "In";
  input.kind = IRPortKind::Trigger;
  input.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(input);
  return signature;
}

KX_NodeSignature IntSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Value";
  output.kind = IRPortKind::Value;
  output.value_type = KX_VALUE_INT;
  signature.outputs.push_back(output);
  return signature;
}

void RegisterHarnessNodes()
{
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  for (int slot = 0; slot < kGateCount; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69Gate" + std::to_string(slot),
                          "B69Gate" + std::to_string(slot),
                          GateSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestGateNode(captured));
                          });
  }
  for (int slot = 0; slot < 12; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69Sink" + std::to_string(slot),
                          "B69Sink" + std::to_string(slot),
                          SinkSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestSinkNode(captured));
                          });
  }
  for (int slot = 0; slot < 4; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69Int" + std::to_string(slot),
                          "B69Int" + std::to_string(slot),
                          IntSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestIntNode(captured));
                          });
  }
}

IRNode MakeNode(GraphIR &ir, const std::string &type_id, const std::string &name)
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
  return ir.nodes.back();
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

int AddGate(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69Gate" + std::to_string(slot), "gate" + std::to_string(slot));
  return index;
}

int AddSink(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69Sink" + std::to_string(slot), "sink" + std::to_string(slot));
  return index;
}

int AddInt(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69Int" + std::to_string(slot), "int" + std::to_string(slot));
  return index;
}

int AddString(GraphIR &ir, const std::string &value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringConst", value);
  ir.nodes.back().config_json = "{\"value\": \"" + value + "\"}";
  return index;
}

int AddWrite(GraphIR &ir,
             int gate,
             const std::string &path,
             const std::string &content,
             const std::string &config)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileWriteChunk", "write");
  ir.nodes.back().config_json = config;
  Connect(ir, gate, 0, index, 0);
  Connect(ir, AddString(ir, path), 0, index, 1);
  Connect(ir, AddString(ir, content), 0, index, 2);
  return index;
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

std::string OutputString(KX_GraphInstance &instance, int node, int port)
{
  const KX_RuntimeValue *value = ReadOutput(instance, node, port);
  const char *text = value != nullptr ? value->AsString() : nullptr;
  return text != nullptr ? std::string(text) : std::string();
}

const KX_ValueList *OutputList(KX_GraphInstance &instance, int node, int port)
{
  const KX_RuntimeValue *value = ReadOutput(instance, node, port);
  return value != nullptr ? static_cast<const KX_ValueList *>(value->AsList()) : nullptr;
}

struct GraphIds {
  int write_a = -1, write_b = -1, write_c = -1;
  int read = -1;
  int bad_zero = -1, bad_big = -1;
  int create_build = -1, create_sub = -1, list = -1;
  int remove_sub = -1, remove_norec = -1, remove_rec = -1;
  int traversal_create = -1, traversal_list = -1, traversal_remove = -1;
  int write_a_txt = -1, write_b_txt = -1;
  int sinks[12] = {};
};

GraphIR BuildGraph(GraphIds *ids)
{
  GraphIR ir;
  /* Sinks first so their indices are stable and easy to reference. */
  for (int slot = 0; slot < 12; ++slot) {
    ids->sinks[slot] = AddSink(ir, slot);
  }

  const int gate_a = AddGate(ir, 0);
  const int gate_b = AddGate(ir, 1);
  const int gate_c = AddGate(ir, 2);
  const int gate_read = AddGate(ir, 3);
  const int gate_bad_zero = AddGate(ir, 4);
  const int gate_bad_big = AddGate(ir, 5);
  const int gate_dirs = AddGate(ir, 6);
  const int gate_list = AddGate(ir, 12);
  const int gate_remove_sub = AddGate(ir, 7);
  const int gate_remove_norec = AddGate(ir, 8);
  const int gate_remove_rec = AddGate(ir, 9);
  const int gate_a_txt = AddGate(ir, 10);
  const int gate_b_txt = AddGate(ir, 11);

  ids->write_a = AddWrite(ir, gate_a, "stream.bin", "AAAA", "{\"chunk_size\": 2, \"append\": false}");
  ids->write_b = AddWrite(ir, gate_b, "stream.bin", "BBBB", "{\"chunk_size\": 2, \"append\": true}");
  ids->write_c = AddWrite(ir, gate_c, "stream.bin", "CCCC", "{\"chunk_size\": 2, \"append\": true}");
  Connect(ir, ids->write_a, 0, ids->sinks[7], 0);
  Connect(ir, ids->write_b, 0, ids->sinks[7], 0);
  Connect(ir, ids->write_c, 0, ids->sinks[7], 0);

  ids->read = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileReadChunk", "read");
  ir.nodes.back().config_json = "{\"chunk_size\": 4}";
  Connect(ir, gate_read, 0, ids->read, 0);
  Connect(ir, AddString(ir, "stream.bin"), 0, ids->read, 1);
  Connect(ir, AddInt(ir, 0), 0, ids->read, 2);
  Connect(ir, ids->read, 0, ids->sinks[8], 0);

  ids->bad_zero = AddWrite(ir, gate_bad_zero, "bad.bin", "x", "{\"chunk_size\": 0}");
  ids->bad_big = AddWrite(ir, gate_bad_big, "bad.bin", "x", "{\"chunk_size\": 1048577}");
  Connect(ir, ids->bad_zero, 1, ids->sinks[0], 0);
  Connect(ir, ids->bad_big, 1, ids->sinks[1], 0);

  ids->create_build = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateDirectory", "create_build");
  ir.nodes.back().config_json = "{\"parents\": true}";
  Connect(ir, gate_dirs, 0, ids->create_build, 0);
  Connect(ir, AddString(ir, "build"), 0, ids->create_build, 1);
  Connect(ir, ids->create_build, 0, ids->sinks[2], 0);

  ids->create_sub = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateDirectory", "create_sub");
  ir.nodes.back().config_json = "{\"parents\": false}";
  Connect(ir, gate_list, 0, ids->create_sub, 0);
  Connect(ir, AddString(ir, "build/sub"), 0, ids->create_sub, 1);
  Connect(ir, ids->create_sub, 0, ids->sinks[3], 0);

  ids->list = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListDirectory", "list");
  Connect(ir, gate_list, 0, ids->list, 0);
  Connect(ir, AddString(ir, "build"), 0, ids->list, 1);
  Connect(ir, ids->list, 0, ids->sinks[4], 0);

  ids->traversal_create = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateDirectory", "traversal_create");
  Connect(ir, gate_list, 0, ids->traversal_create, 0);
  Connect(ir, AddString(ir, "../evil"), 0, ids->traversal_create, 1);
  Connect(ir, ids->traversal_create, 1, ids->sinks[10], 0);

  ids->traversal_list = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListDirectory", "traversal_list");
  Connect(ir, gate_list, 0, ids->traversal_list, 0);
  Connect(ir, AddString(ir, "../"), 0, ids->traversal_list, 1);
  Connect(ir, ids->traversal_list, 1, ids->sinks[11], 0);

  ids->traversal_remove = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "RemoveDirectory", "traversal_remove");
  ir.nodes.back().config_json = "{\"recursive\": true}";
  Connect(ir, gate_list, 0, ids->traversal_remove, 0);
  Connect(ir, AddString(ir, "a/../../x"), 0, ids->traversal_remove, 1);
  Connect(ir, ids->traversal_remove, 1, ids->sinks[11], 0);

  ids->write_a_txt = AddWrite(ir, gate_a_txt, "build/a.txt", "a", "{\"append\": false}");
  ids->write_b_txt = AddWrite(ir, gate_b_txt, "build/b.txt", "b", "{\"append\": false}");

  ids->remove_sub = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "RemoveDirectory", "remove_sub");
  ir.nodes.back().config_json = "{\"recursive\": false}";
  Connect(ir, gate_remove_sub, 0, ids->remove_sub, 0);
  Connect(ir, AddString(ir, "build/sub"), 0, ids->remove_sub, 1);
  Connect(ir, ids->remove_sub, 0, ids->sinks[5], 0);

  ids->remove_norec = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "RemoveDirectory", "remove_norec");
  ir.nodes.back().config_json = "{\"recursive\": false}";
  Connect(ir, gate_remove_norec, 0, ids->remove_norec, 0);
  Connect(ir, AddString(ir, "build"), 0, ids->remove_norec, 1);
  Connect(ir, ids->remove_norec, 1, ids->sinks[9], 0);

  ids->remove_rec = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "RemoveDirectory", "remove_rec");
  ir.nodes.back().config_json = "{\"recursive\": true}";
  Connect(ir, gate_remove_rec, 0, ids->remove_rec, 0);
  Connect(ir, AddString(ir, "build"), 0, ids->remove_rec, 1);
  Connect(ir, ids->remove_rec, 0, ids->sinks[6], 0);

  return ir;
}

void Frame(KX_GraphInstance &instance, int gate, bool value)
{
  g_gates[gate] = value;
  instance.Tick(1.0f / 60.0f);
  g_gates[gate] = false;
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterHarnessNodes();

  KX_MockFileAdapter file;
  GraphIds ids;
  GraphIR ir = BuildGraph(&ids);
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  if (!instance.Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    std::printf("FAIL file graph did not compile\n");
    return 1;
  }
  instance.GetContext()->SetAdapters(nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &file,
                                     nullptr);

  /* Fi-1: three writes in sequence, then three reads. */
  Frame(instance, 0, true);
  Frame(instance, 1, true);
  Frame(instance, 2, true);
  Check(g_sink_hits[7] == 3, "write: three chunks reached OnSuccess");
  Check(file.files["stream.bin"] == "AAAABBBBCCCC",
        "write: three chunks concatenate byte-exactly");

  g_gates[3] = true;
  const std::string expected[3] = {"AAAA", "BBBB", "CCCC"};
  bool read_ok = true;
  for (int index = 0; index < 3; ++index) {
    g_ints[0] = index * 4;
    instance.Tick(1.0f / 60.0f);
    const KX_RuntimeValue *eof = ReadOutput(instance, ids.read, 4);
    if (OutputString(instance, ids.read, 2) != expected[index] || eof == nullptr ||
        eof->AsBool() != (index == 2)) {
      read_ok = false;
    }
  }
  Check(read_ok, "read: chunks replay in order and set Eof only on the last chunk");
  Check(g_sink_hits[8] == 3, "read: three OnSuccess frames");

  /* Offset at EOF: empty chunk, Eof=true, OnSuccess. */
  g_ints[0] = 12;
  instance.Tick(1.0f / 60.0f);
  const KX_RuntimeValue *eof_at_end = ReadOutput(instance, ids.read, 4);
  Check(OutputString(instance, ids.read, 2).empty() && eof_at_end != nullptr &&
            eof_at_end->AsBool() && g_sink_hits[8] == 4,
        "read: offset at file size returns empty content with Eof=true");

  /* Offset past EOF is rejected, not clamped or wrapped. */
  g_ints[0] = 13;
  instance.Tick(1.0f / 60.0f);
  Check(g_sink_hits[8] == 4, "read: offset past EOF takes OnFail, no success");
  g_gates[3] = false;

  /* Boundary chunk sizes are rejected before touching the file. */
  Frame(instance, 4, true);
  Frame(instance, 5, true);
  Check(g_sink_hits[0] == 1 && g_sink_hits[1] == 1,
        "chunk_size 0 and >1MiB both take OnFail");

  /* Fi-2: create, list, remove empty, refuse non-empty, recursive delete. */
  Frame(instance, 6, true);
  Check(g_sink_hits[2] == 1, "directory: build/ is created");

  Frame(instance, 12, true);
  Check(g_sink_hits[3] == 1 && g_sink_hits[4] == 1,
        "directory: build/sub creation and the empty listing succeed");
  Check(g_sink_hits[10] == 1, "directory: '../evil' creation is rejected");
  Check(g_sink_hits[11] == 2, "directory: '../' listing and 'a/../../x' removal are rejected");

  Frame(instance, 10, true);
  Frame(instance, 11, true);
  Frame(instance, 12, true);
  Check(g_sink_hits[4] == 2, "directory: listing after writing two files succeeds");
  const KX_ValueList *names = OutputList(instance, ids.list, 2);
  const KX_ValueList *directories = OutputList(instance, ids.list, 3);
  const KX_RuntimeValue *count = ReadOutput(instance, ids.list, 4);
  bool entries_ok = names != nullptr && (*names).size() == 3 && count != nullptr &&
                    count->AsInt() == 3;
  if (entries_ok) {
    entries_ok = std::string((*names)[0].AsString()) == "a.txt" &&
                 std::string((*names)[1].AsString()) == "b.txt" &&
                 std::string((*names)[2].AsString()) == "sub";
  }
  const bool directories_ok = directories != nullptr && (*directories).size() == 1 &&
                              std::string((*directories)[0].AsString()) == "sub";
  Check(entries_ok, "list: entries are sorted and count is 3");
  Check(directories_ok, "list: only 'sub' is reported as a directory");

  Frame(instance, 7, true);
  Check(g_sink_hits[5] == 1, "remove: empty build/sub is removed");

  Frame(instance, 8, true);
  Check(g_sink_hits[9] == 1, "remove: non-empty build/ is refused when recursive=false");
  Check(file.directories.find("build") != file.directories.end(),
        "remove: refused removal left the directory in place");

  Frame(instance, 9, true);
  Check(g_sink_hits[6] == 1, "remove: recursive delete succeeds");
  Check(file.directories.find("build") == file.directories.end() &&
            file.files.find("build/a.txt") == file.files.end() &&
            file.files.find("build/b.txt") == file.files.end(),
        "remove: recursive delete removed the whole subtree");

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
