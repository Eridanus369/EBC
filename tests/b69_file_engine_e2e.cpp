/*
 * B6.9-Fi real-disk E2E: FileReadChunk / FileWriteChunk / ListDirectory /
 * CreateDirectory / RemoveDirectory against the filesystem-backed
 * KX_FileAdapterEngine.
 *
 * The adapter is rooted at a private temporary directory created with
 * mkdtemp() under std::filesystem::temp_directory_path(); the directory is
 * removed on exit. Every assertion reads the bytes back from disk, so a mock
 * cannot satisfy it.
 *
 * Frame progression is explicit (Tick with a fixed delta); no sleeps.
 * Exit code is 0 only when every assertion passes.
 *
 * Limitations (cannot be asserted portably on a real disk, documented instead
 * of weakened):
 *   - ListDirectory sorting is asserted, but the comparison order is the
 *     adapter's std::sort of raw byte names; a locale-aware filesystem does
 *     not change that (the adapter sorts, not the filesystem iterator).
 *   - permission denied (EACCES), ENOSPC, >4GB files, symlink targets and
 *     atomic replace/fsync are NOT exercised here.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include "KX_CompiledGraph.h"
#include "KX_FileAdapter.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

namespace fs = std::filesystem;

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

/** Owns the private blend directory; removes the whole subtree on destruction. */
struct TempBlendDir {
  std::string path;

  TempBlendDir()
  {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) {
      return;
    }
    std::string pattern = (base / "ebc_b69_file_engine_XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) != nullptr) {
      path.assign(buffer.data());
    }
  }

  ~TempBlendDir()
  {
    if (!path.empty()) {
      std::error_code ec;
      fs::remove_all(fs::path(path), ec);
    }
  }
};

bool DiskExists(const std::string &root, const std::string &relative)
{
  return fs::exists(fs::path(root) / relative);
}

bool DiskIsDirectory(const std::string &root, const std::string &relative)
{
  return fs::is_directory(fs::path(root) / relative);
}

std::string DiskRead(const std::string &root, const std::string &relative)
{
  std::ifstream stream((fs::path(root) / relative).string(), std::ios::binary);
  if (!stream) {
    return std::string();
  }
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

size_t DiskSize(const std::string &root, const std::string &relative)
{
  std::error_code ec;
  const size_t size = static_cast<size_t>(fs::file_size(fs::path(root) / relative, ec));
  return ec ? size_t(-1) : size;
}

const int kGateCount = 25;
bool g_gates[kGateCount] = {false};
int g_ints[4] = {0};
int g_sink_hits[22] = {0};

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
    registry.RegisterType("B69EGate" + std::to_string(slot),
                          "B69EGate" + std::to_string(slot),
                          GateSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestGateNode(captured));
                          });
  }
  for (int slot = 0; slot < 22; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69ESink" + std::to_string(slot),
                          "B69ESink" + std::to_string(slot),
                          SinkSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestSinkNode(captured));
                          });
  }
  for (int slot = 0; slot < 4; ++slot) {
    const int captured = slot;
    registry.RegisterType("B69EInt" + std::to_string(slot),
                          "B69EInt" + std::to_string(slot),
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
  MakeNode(ir, "B69EGate" + std::to_string(slot), "gate" + std::to_string(slot));
  return index;
}

int AddSink(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69ESink" + std::to_string(slot), "sink" + std::to_string(slot));
  return index;
}

int AddInt(GraphIR &ir, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B69EInt" + std::to_string(slot), "int" + std::to_string(slot));
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
  int trunc_a = -1, trunc_b = -1, read_missing = -1, write_nested = -1;
  int write_plain = -1, read_plain = -1, exists_yes = -1, exists_no = -1;
  int delete_plain = -1, save_game = -1, load_game = -1, save_binary = -1;
  int slots = -1, read_lines = -1;
  int sinks[22] = {};
};

GraphIR BuildGraph(GraphIds *ids)
{
  GraphIR ir;
  /* Sinks first so their indices are stable and easy to reference. */
  for (int slot = 0; slot < 22; ++slot) {
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
  const int gate_trunc_a = AddGate(ir, 13);
  const int gate_trunc_b = AddGate(ir, 14);
  const int gate_read_missing = AddGate(ir, 15);
  const int gate_nested = AddGate(ir, 16);
  const int gate_write_plain = AddGate(ir, 17);
  const int gate_read_plain = AddGate(ir, 18);
  const int gate_delete_plain = AddGate(ir, 19);
  const int gate_save_game = AddGate(ir, 20);
  const int gate_load_game = AddGate(ir, 21);
  const int gate_save_binary = AddGate(ir, 22);
  const int gate_read_lines = AddGate(ir, 23);
  const int gate_exists = AddGate(ir, 24);

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

  /* Real-disk extra semantics: truncate vs append, missing file, nested parent. */
  ids->trunc_a = AddWrite(ir, gate_trunc_a, "trunc.bin", "AAAA", "{\"chunk_size\": 2, \"append\": false}");
  ids->trunc_b = AddWrite(ir, gate_trunc_b, "trunc.bin", "BB", "{\"chunk_size\": 2, \"append\": false}");
  Connect(ir, ids->trunc_a, 0, ids->sinks[12], 0);
  Connect(ir, ids->trunc_b, 0, ids->sinks[12], 0);

  ids->read_missing = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileReadChunk", "read_missing");
  ir.nodes.back().config_json = "{\"chunk_size\": 4}";
  Connect(ir, gate_read_missing, 0, ids->read_missing, 0);
  Connect(ir, AddString(ir, "missing.bin"), 0, ids->read_missing, 1);
  Connect(ir, AddInt(ir, 0), 0, ids->read_missing, 2);
  Connect(ir, ids->read_missing, 1, ids->sinks[13], 0);

  ids->write_nested = AddWrite(ir,
                               gate_nested,
                               "deep/nested/f.txt",
                               "D",
                               "{\"append\": false}");
  Connect(ir, ids->write_nested, 0, ids->sinks[14], 0);

  /* B4File / B6File disk nodes, exercised against the same real adapter so the
   * whole file domain has node-level real-disk evidence. */
  const int write_plain = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "WriteFile", "write_plain");
  Connect(ir, gate_write_plain, 0, write_plain, 0);
  Connect(ir, AddString(ir, "plain.txt"), 0, write_plain, 1);
  Connect(ir, AddString(ir, "hello"), 0, write_plain, 2);
  Connect(ir, write_plain, 0, ids->sinks[15], 0);

  ids->read_plain = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ReadFile", "read_plain");
  Connect(ir, gate_read_plain, 0, ids->read_plain, 0);
  Connect(ir, AddString(ir, "plain.txt"), 0, ids->read_plain, 1);
  Connect(ir, ids->read_plain, 1, ids->sinks[16], 0);

  ids->exists_yes = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileExists", "exists_yes");
  Connect(ir, gate_exists, 0, ids->exists_yes, 0);
  Connect(ir, AddString(ir, "plain.txt"), 0, ids->exists_yes, 1);

  ids->exists_no = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileExists", "exists_no");
  Connect(ir, gate_exists, 0, ids->exists_no, 0);
  Connect(ir, AddString(ir, "nope.txt"), 0, ids->exists_no, 1);

  ids->delete_plain = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "DeleteFile", "delete_plain");
  Connect(ir, gate_delete_plain, 0, ids->delete_plain, 0);
  Connect(ir, AddString(ir, "plain.txt"), 0, ids->delete_plain, 1);
  Connect(ir, ids->delete_plain, 0, ids->sinks[17], 0);

  ids->save_game = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SaveGame", "save_game");
  Connect(ir, gate_save_game, 0, ids->save_game, 0);
  Connect(ir, AddString(ir, "slot1"), 0, ids->save_game, 1);
  Connect(ir, ids->save_game, 0, ids->sinks[18], 0);

  ids->load_game = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "LoadGame", "load_game");
  Connect(ir, gate_load_game, 0, ids->load_game, 0);
  Connect(ir, AddString(ir, "slot1"), 0, ids->load_game, 1);
  Connect(ir, ids->load_game, 0, ids->sinks[19], 0);

  ids->save_binary = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SaveGameBinary", "save_binary");
  Connect(ir, gate_save_binary, 0, ids->save_binary, 0);
  Connect(ir, AddString(ir, "bin1"), 0, ids->save_binary, 1);
  Connect(ir, ids->save_binary, 0, ids->sinks[20], 0);

  ids->slots = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SaveGameSlots", "slots");

  ids->read_lines = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileReadLines", "read_lines");
  Connect(ir, gate_read_lines, 0, ids->read_lines, 0);
  Connect(ir, AddString(ir, "lines.txt"), 0, ids->read_lines, 1);
  Connect(ir, ids->read_lines, 0, ids->sinks[21], 0);

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

  TempBlendDir temp;
  if (temp.path.empty()) {
    std::printf("FAIL could not create the temporary blend directory\n");
    std::printf("RESULT assertions=1 failures=1\n");
    return 1;
  }
  std::printf("INFO blend_dir=%s\n", temp.path.c_str());

  KX_FileAdapterEngine file(temp.path);
  GraphIds ids;
  GraphIR ir = BuildGraph(&ids);
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  if (!instance.Load(ir, &warnings)) {
    for (const std::string &warning : warnings) {
      std::fprintf(stderr, "compile: %s\n", warning.c_str());
    }
    std::printf("FAIL file graph did not compile\n");
    std::printf("RESULT assertions=1 failures=1\n");
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

  /* Fi-1: three writes in sequence, then three reads (chunk_size 2 + append). */
  Frame(instance, 0, true);
  Frame(instance, 1, true);
  Frame(instance, 2, true);
  Check(g_sink_hits[7] == 3, "write: three chunks reached OnSuccess");
  Check(DiskRead(temp.path, "stream.bin") == "AAAABBBBCCCC",
        "write: three chunks concatenate byte-exactly on disk");
  Check(DiskSize(temp.path, "stream.bin") == 12,
        "write: append updates the on-disk file size to 12 bytes");

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

  /* Offset past EOF is rejected, not clamped or wrapped (adapter return false). */
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
  Check(g_sink_hits[2] == 1 && DiskIsDirectory(temp.path, "build"),
        "directory: build/ exists on disk after CreateDirectory");

  Frame(instance, 12, true);
  Check(g_sink_hits[3] == 1 && DiskIsDirectory(temp.path, "build/sub") &&
            g_sink_hits[4] == 1,
        "directory: build/sub exists on disk and the empty listing succeeds");
  Check(g_sink_hits[10] == 1 && !DiskExists(temp.path, "../evil"),
        "directory: '../evil' creation is rejected and creates nothing");
  Check(g_sink_hits[11] == 2,
        "directory: '../' listing and 'a/../../x' removal are rejected");

  Frame(instance, 10, true);
  Frame(instance, 11, true);
  Frame(instance, 12, true);
  Check(g_sink_hits[4] == 2, "directory: listing after writing two files succeeds");
  Check(DiskRead(temp.path, "build/a.txt") == "a" &&
            DiskRead(temp.path, "build/b.txt") == "b",
        "directory: both child files exist on disk");
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
  Check(entries_ok, "list: directory_iterator output is sorted by name, count is 3");
  Check(directories_ok, "list: only 'sub' is reported as a directory");

  Frame(instance, 7, true);
  Check(g_sink_hits[5] == 1 && !DiskExists(temp.path, "build/sub"),
        "remove: empty build/sub is removed from disk");

  Frame(instance, 8, true);
  Check(g_sink_hits[9] == 1, "remove: non-empty build/ is refused when recursive=false");
  Check(DiskExists(temp.path, "build/a.txt") && DiskExists(temp.path, "build/b.txt"),
        "remove: refused removal left the files in place on disk");

  Frame(instance, 9, true);
  Check(g_sink_hits[6] == 1, "remove: recursive delete succeeds");
  Check(!DiskExists(temp.path, "build"),
        "remove: recursive delete removed the whole subtree from disk");

  /* Truncate vs append on a real file: the second non-append write replaces. */
  Frame(instance, 13, true);
  Check(g_sink_hits[12] == 1 && DiskRead(temp.path, "trunc.bin") == "AAAA",
        "write: first non-append write creates trunc.bin");
  Frame(instance, 14, true);
  Check(g_sink_hits[12] == 2 && DiskRead(temp.path, "trunc.bin") == "BB",
        "write: a later non-append write truncates the previous bytes");

  /* Missing file is an adapter failure, not an empty successful read. */
  Frame(instance, 15, true);
  Check(g_sink_hits[13] == 1 && !DiskExists(temp.path, "missing.bin"),
        "read: a missing file takes OnFail");

  /* WriteFileRange creates missing parent directories before opening. */
  Frame(instance, 16, true);
  Check(g_sink_hits[14] == 1 && DiskRead(temp.path, "deep/nested/f.txt") == "D",
        "write: missing parent directories are created (deep/nested/f.txt)");

  /* B4File / B6File nodes on the same real adapter. */
  Frame(instance, 17, true);
  Check(g_sink_hits[15] == 1 && DiskRead(temp.path, "plain.txt") == "hello",
        "WriteFile: node writes plain.txt through the real adapter");

  Frame(instance, 18, true);
  Check(g_sink_hits[16] == 1 && OutputString(instance, ids.read_plain, 0) == "hello",
        "ReadFile: node reads the bytes back from disk");

  Frame(instance, 24, true);
  const KX_RuntimeValue *exists_yes = ReadOutput(instance, ids.exists_yes, 0);
  const KX_RuntimeValue *exists_no = ReadOutput(instance, ids.exists_no, 0);
  Check(exists_yes != nullptr && exists_yes->AsBool() && exists_no != nullptr &&
            !exists_no->AsBool(),
        "FileExists: true for a real file, false for a missing one");

  Frame(instance, 19, true);
  Check(g_sink_hits[17] == 1 && !DiskExists(temp.path, "plain.txt"),
        "DeleteFile: removes the file from disk");

  Frame(instance, 20, true);
  Check(g_sink_hits[18] == 1 && DiskExists(temp.path, "save/slot1.json"),
        "SaveGame: writes save/slot1.json on disk");

  Frame(instance, 21, true);
  Check(g_sink_hits[19] == 1, "LoadGame: reads the JSON slot back from disk");

  Frame(instance, 22, true);
  Check(g_sink_hits[20] == 1 && DiskExists(temp.path, "save/bin1.bin"),
        "SaveGameBinary: writes save/bin1.bin on disk");

  instance.Tick(1.0f / 60.0f);
  const KX_ValueList *slot_names = OutputList(instance, ids.slots, 0);
  const KX_RuntimeValue *slot_count = ReadOutput(instance, ids.slots, 1);
  bool slots_ok = slot_names != nullptr && slot_count != nullptr && slot_count->AsInt() >= 2;
  if (slots_ok) {
    bool saw_json = false;
    bool saw_binary = false;
    for (const KX_RuntimeValue &value : *slot_names) {
      const char *name = value.AsString();
      saw_json = saw_json || (name != nullptr && std::string(name) == "slot1");
      saw_binary = saw_binary || (name != nullptr && std::string(name) == "bin1");
    }
    slots_ok = saw_json && saw_binary;
  }
  Check(slots_ok, "SaveGameSlots: lists both real save slots");

  file.WriteFileRange("lines.txt", "x\ny\n", true);
  Frame(instance, 23, true);
  const KX_ValueList *lines = OutputList(instance, ids.read_lines, 2);
  Check(g_sink_hits[21] == 1 && lines != nullptr && (*lines).size() == 2 &&
            std::string((*lines)[0].AsString()) == "x" &&
            std::string((*lines)[1].AsString()) == "y",
        "FileReadLines: reads two lines from a real file");

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::printf("RESULT assertions=%d failures=%d\n", g_pass + g_fail, g_fail);
  return g_fail == 0 ? 0 : 1;
}
