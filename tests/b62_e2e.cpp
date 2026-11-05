// B6.2 E2E harness: input extensions, advanced string/list and audio.
//
// Runs against the mock adapters so gamepad/touch state can be injected and
// audio/spatial calls recorded. Every assertion prints PASS/FAIL and the exit
// code is non zero when something fails.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionContext.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"

void RegisterFunctionGameNodes();
void RegisterExtendedFunctionGameNodes();
void RegisterB2FunctionGameNodes();
void RegisterB3FunctionGameNodes();
void RegisterB4FileFunctionGameNodes();
void RegisterB4UIFunctionGameNodes();
void RegisterB6InputFunctionGameNodes();
void RegisterB6StringFunctionGameNodes();
void RegisterB6AudioFunctionGameNodes();
void RegisterB6FileFunctionGameNodes();

/* Translation units owned by parallel B6 lines are not part of this harness. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6UIFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}

namespace {

int failures = 0;

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  std::printf("%s %s (actual=%g expected=%g)\n",
              ok ? "PASS" : "FAIL",
              what,
              actual,
              expected);
  if (!ok) {
    ++failures;
  }
}

/* Counts how many times a trigger reaches this node. One rack slot per sink. */
int g_sink_hits[8] = {0, 0, 0, 0, 0, 0, 0, 0};

class TestSinkNode : public KX_LogicNodeRuntime {
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

int AddString(GraphIR &ir, const std::string &name, const std::string &value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringConst", name);
  ir.nodes.back().config_json = "{\"value\": \"" + value + "\"}";
  return index;
}

int AddFloat(GraphIR &ir, const std::string &name, float value)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ConstFloat", name);
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": \"%.6f\"}", static_cast<double>(value));
  ir.nodes.back().config_json = config;
  return index;
}

int AddSink(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TestSink" + std::to_string(slot), name);
  return index;
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
        static_cast<int>(info.port_index) == port && !info.trigger)
    {
      return &instance.GetGraph()->Data().Value(info.offset);
    }
  }
  return nullptr;
}

KX_GraphInstance *BuildInstance(GraphIR &ir, KX_MockInputAdapter &input, KX_MockAudioAdapter &audio)
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
  instance->GetContext()->SetAdapters(
      &input, nullptr, nullptr, nullptr, nullptr, nullptr, &audio, nullptr, nullptr, nullptr, nullptr);
  return instance;
}

/* --------------------------------------------------------------- input graph */

struct InputGraph {
  int press = 0;
  int release = 0;
  int hold = 0;
  int combo = 0;
  int axis = 0;
  int trigger = 0;
  int touch = 0;
  int map = 0;
  int get_value = 0;
  int vibrate = 0;
};

GraphIR BuildInputGraph(InputGraph *out)
{
  InputGraph ids;
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");

  ids.press = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GamepadButton", "press");
  ir.nodes.back().config_json = "{\"pad\": 0, \"button\": 0, \"edge\": 0}";
  ids.release = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GamepadButton", "release");
  ir.nodes.back().config_json = "{\"pad\": 0, \"button\": 1, \"edge\": 1}";
  ids.hold = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GamepadButton", "hold");
  ir.nodes.back().config_json = "{\"pad\": 0, \"button\": 2, \"edge\": 2}";
  ids.combo = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "KeyCombo", "combo");
  ir.nodes.back().config_json = "{\"keys\": \"ctrl+shift+a\", \"edge\": 0}";

  const int sink_press = AddSink(ir, "sink_press", 0);
  const int sink_release = AddSink(ir, "sink_release", 1);
  const int sink_hold = AddSink(ir, "sink_hold", 2);
  const int sink_combo = AddSink(ir, "sink_combo", 3);
  const int sink_map = AddSink(ir, "sink_map", 4);
  Connect(ir, ids.press, 0, sink_press, 0);
  Connect(ir, ids.release, 0, sink_release, 0);
  Connect(ir, ids.hold, 0, sink_hold, 0);
  Connect(ir, ids.combo, 0, sink_combo, 0);

  ids.axis = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GamepadAxis", "axis");
  ir.nodes.back().config_json = "{\"pad\": 0, \"axis\": 0}";
  ids.trigger = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GamepadTrigger", "trigger");
  ir.nodes.back().config_json = "{\"pad\": 0, \"trigger\": 0}";
  ids.touch = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "TouchInput", "touch");

  /* InputMap: name "jump" -> "gamepad_button:0:0". */
  ids.map = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "InputMap", "map");
  const int map_name = AddString(ir, "map_name", "jump");
  const int map_binding = AddString(ir, "map_binding", "gamepad_button:0:0");
  Connect(ir, 0, 0, ids.map, 0);
  Connect(ir, map_name, 0, ids.map, 1);
  Connect(ir, map_binding, 0, ids.map, 2);
  Connect(ir, ids.map, 0, sink_map, 0);

  ids.get_value = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "GetInputValue", "get_value");
  Connect(ir, AddString(ir, "get_name", "jump"), 0, ids.get_value, 0);

  /* Pad input is left unconnected: it defaults to pad 0. */
  ids.vibrate = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "VibrateGamepad", "vibrate");
  Connect(ir, 0, 0, ids.vibrate, 0);
  Connect(ir, AddFloat(ir, "vib_intensity", 0.5f), 0, ids.vibrate, 2);
  Connect(ir, AddFloat(ir, "vib_duration", 0.3f), 0, ids.vibrate, 3);

  *out = ids;
  return ir;
}

/* ------------------------------------------------------- string/list graph */

struct StringGraph {
  int split = 0;
  int split_multi = 0;
  int split_empty = 0;
  int split_edges = 0;
  int split_whitespace = 0;
  int group_nested = 0;
  int group_optional = 0;
  int group_nomatch = 0;
  int join = 0;
  int match = 0;
  int replace = 0;
  int trim = 0;
  int pad = 0;
  int len_chars = 0;
  int pad_chars = 0;
  int len_chars_bad = 0;
  int sort = 0;
  int filter = 0;
  int map = 0;
  int find = 0;
  int slice = 0;
  int concat = 0;
};

GraphIR BuildStringGraph(StringGraph *out)
{
  StringGraph ids;
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");

  ids.split = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringSplit", "split");
  Connect(ir, AddString(ir, "split_src", "a,b,c"), 0, ids.split, 0);
  Connect(ir, AddString(ir, "split_sep", ","), 0, ids.split, 1);

  ids.split_multi = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringSplit", "split_multi");
  Connect(ir, AddString(ir, "split_multi_src", "a,b;c"), 0, ids.split_multi, 0);
  Connect(ir, AddString(ir, "split_multi_sep", ",;"), 0, ids.split_multi, 1);

  ids.split_empty = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringSplit", "split_empty");
  Connect(ir, AddString(ir, "split_empty_src", "a,,b"), 0, ids.split_empty, 0);
  Connect(ir, AddString(ir, "split_empty_sep", ","), 0, ids.split_empty, 1);

  ids.split_edges = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringSplit", "split_edges");
  Connect(ir, AddString(ir, "split_edges_src", ",a,"), 0, ids.split_edges, 0);
  Connect(ir, AddString(ir, "split_edges_sep", ","), 0, ids.split_edges, 1);

  ids.split_whitespace = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringSplit", "split_whitespace");
  Connect(ir, AddString(ir, "split_ws_src", "  a   b "), 0, ids.split_whitespace, 0);
  Connect(ir, AddString(ir, "split_ws_sep", ""), 0, ids.split_whitespace, 1);

  ids.group_nested = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringRegexMatch", "group_nested");
  Connect(ir, AddString(ir, "group_nested_src", "ab"), 0, ids.group_nested, 0);
  Connect(ir, AddString(ir, "group_nested_pat", "((a)(b))"), 0, ids.group_nested, 1);

  ids.group_optional = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringRegexMatch", "group_optional");
  Connect(ir, AddString(ir, "group_optional_src", "b"), 0, ids.group_optional, 0);
  Connect(ir, AddString(ir, "group_optional_pat", "(a)?(b)"), 0, ids.group_optional, 1);

  ids.group_nomatch = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringRegexMatch", "group_nomatch");
  Connect(ir, AddString(ir, "group_nomatch_src", "abc"), 0, ids.group_nomatch, 0);
  Connect(ir, AddString(ir, "group_nomatch_pat", "(z)"), 0, ids.group_nomatch, 1);

  ids.join = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringJoin", "join");
  Connect(ir, ids.split, 0, ids.join, 0);
  Connect(ir, AddString(ir, "join_sep", "|"), 0, ids.join, 1);

  ids.match = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringRegexMatch", "match");
  Connect(ir, AddString(ir, "match_src", "abc123"), 0, ids.match, 0);
  Connect(ir, AddString(ir, "match_pat", "([a-z]+)([0-9]+)"), 0, ids.match, 1);

  ids.replace = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringRegexReplace", "replace");
  Connect(ir, AddString(ir, "replace_src", "abc123"), 0, ids.replace, 0);
  Connect(ir, AddString(ir, "replace_pat", "[0-9]+"), 0, ids.replace, 1);
  Connect(ir, AddString(ir, "replace_rep", "X"), 0, ids.replace, 2);

  ids.trim = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringTrim", "trim");
  Connect(ir, AddString(ir, "trim_src", "  hi  "), 0, ids.trim, 0);
  /* Mode defaults to 0 (trim both). */

  ids.pad = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringPad", "pad");
  Connect(ir, AddString(ir, "pad_src", "7"), 0, ids.pad, 0);
  const int pad_len = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringLength", "pad_len");
  Connect(ir, AddString(ir, "pad_len_src", "abcd"), 0, pad_len, 0);
  Connect(ir, pad_len, 0, ids.pad, 1);
  Connect(ir, AddString(ir, "pad_char", "0"), 0, ids.pad, 2);
  /* Side defaults to 0 (pad left). */

  ids.len_chars = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringLengthChars", "len_chars");
  Connect(ir, AddString(ir, "len_src", "中文ab"), 0, ids.len_chars, 0);

  ids.len_chars_bad = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringLengthChars", "len_chars_bad");
  Connect(ir, AddString(ir, "len_bad_src", "\xE4\xB8"), 0, ids.len_chars_bad, 0);

  ids.pad_chars = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringPadChars", "pad_chars");
  Connect(ir, AddString(ir, "padc_src", "中"), 0, ids.pad_chars, 0);
  const int padc_len = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringLength", "padc_len");
  Connect(ir, AddString(ir, "padc_len_src", "abc"), 0, padc_len, 0);
  Connect(ir, padc_len, 0, ids.pad_chars, 1);
  Connect(ir, AddString(ir, "padc_unit", "文"), 0, ids.pad_chars, 2);

  /* Float list [3, 1, 2] built through ListAdd. */
  const int add3 = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListAdd", "add3");
  Connect(ir, AddFloat(ir, "three", 3.0f), 0, add3, 1);
  const int add1 = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListAdd", "add1");
  Connect(ir, add3, 0, add1, 0);
  Connect(ir, AddFloat(ir, "one", 1.0f), 0, add1, 1);
  const int add2 = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListAdd", "add2");
  Connect(ir, add1, 0, add2, 0);
  Connect(ir, AddFloat(ir, "two", 2.0f), 0, add2, 1);

  ids.sort = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListSort", "sort");
  ir.nodes.back().config_json = "{\"key\": \"\", \"descending\": false}";
  Connect(ir, add2, 0, ids.sort, 0);

  ids.filter = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListFilter", "filter");
  Connect(ir, add2, 0, ids.filter, 0);
  Connect(ir, AddString(ir, "filter_expr", "x > 1"), 0, ids.filter, 1);

  ids.map = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListMap", "map");
  Connect(ir, add2, 0, ids.map, 0);
  Connect(ir, AddString(ir, "map_expr", "x * 2"), 0, ids.map, 1);

  ids.find = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListFind", "find");
  Connect(ir, add2, 0, ids.find, 0);
  Connect(ir, AddFloat(ir, "find_value", 1.0f), 0, ids.find, 1);

  ids.slice = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListSlice", "slice");
  Connect(ir, add2, 0, ids.slice, 0);
  /* Start defaults to 0. */
  const int slice_end = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StringLength", "slice_end");
  Connect(ir, AddString(ir, "slice_end_src", "ab"), 0, slice_end, 0);
  Connect(ir, slice_end, 0, ids.slice, 2);

  ids.concat = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ListConcat", "concat");
  Connect(ir, add2, 0, ids.concat, 0);
  Connect(ir, add2, 0, ids.concat, 1);

  *out = ids;
  return ir;
}

/* -------------------------------------------------------------- audio graph */

GraphIR BuildAudioGraph()
{
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");

  const int attenuation = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SetAudioAttenuation", "attenuation");
  Connect(ir, 0, 0, attenuation, 0);
  Connect(ir, AddFloat(ir, "ref_dist", 1.0f), 0, attenuation, 3);
  Connect(ir, AddFloat(ir, "max_dist", 10.0f), 0, attenuation, 4);

  const int doppler = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SetAudioDoppler", "doppler");
  Connect(ir, 0, 0, doppler, 0);
  Connect(ir, AddFloat(ir, "doppler_factor", 2.0f), 0, doppler, 2);

  const int zone = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "AudioZone", "zone");
  Connect(ir, 0, 0, zone, 0);
  Connect(ir, AddString(ir, "zone_name", "forest"), 0, zone, 1);
  Connect(ir, AddString(ir, "zone_sound", "beep"), 0, zone, 2);
  Connect(ir, AddFloat(ir, "zone_volume", 0.7f), 0, zone, 3);

  const int bus = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SetAudioBusVolume", "bus");
  Connect(ir, 0, 0, bus, 0);
  Connect(ir, AddString(ir, "bus_name", "master"), 0, bus, 1);
  Connect(ir, AddFloat(ir, "bus_volume", 0.5f), 0, bus, 2);

  const int play = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "PlaySound3D", "play");
  Connect(ir, 0, 0, play, 0);
  Connect(ir, AddString(ir, "play_sound", "beep"), 0, play, 1);
  const int color = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ColorConst", "pos_color");
  ir.nodes.back().config_json = "{\"r\": 1.0, \"g\": 2.0, \"b\": 3.0, \"a\": 1.0}";
  const int vector = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "VectorFromColor", "pos_vector");
  Connect(ir, color, 0, vector, 0);
  Connect(ir, vector, 0, play, 2);
  Connect(ir, AddFloat(ir, "play_volume", 0.8f), 0, play, 3);
  Connect(ir, AddFloat(ir, "play_pitch", 1.2f), 0, play, 4);

  return ir;
}

/* ------------------------------------------------------ sound handle graph */

struct StartSoundGraph {
  int start = 0;
  int sink = 0;
};

GraphIR BuildStartSoundGraph(StartSoundGraph *out, const std::string &file)
{
  StartSoundGraph ids;
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  ids.start = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StartSound", "start");
  Connect(ir, 0, 0, ids.start, 0);
  Connect(ir, AddString(ir, "file", file), 0, ids.start, 1);
  ids.sink = AddSink(ir, "sound_started", 5);
  Connect(ir, ids.start, 0, ids.sink, 0);
  *out = ids;
  return ir;
}

struct StartStopGraph {
  int start = 0;
  int stop = 0;
};

GraphIR BuildStartStopGraph(StartStopGraph *out, const std::string &file)
{
  StartStopGraph ids;
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  ids.start = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StartSound", "start");
  Connect(ir, 0, 0, ids.start, 0);
  Connect(ir, AddString(ir, "file", file), 0, ids.start, 1);
  ids.stop = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StopSound", "stop");
  Connect(ir, ids.start, 0, ids.stop, 0);
  Connect(ir, ids.start, 1, ids.stop, 1);
  *out = ids;
  return ir;
}

}  // namespace

static void RegisterCoreGameNodesForHarness()
{
  /* Same core set as RegisterBuiltinGameNodes, without the families owned by
   * parallel B6 lines. */
  RegisterGameNodeType<KX_Node_OnInit>("OnInit", "KX_Node_OnInit", KX_Node_OnInit::Signature());
  RegisterGameNodeType<KX_Node_OnUpdate>(
      "OnUpdate", "KX_Node_OnUpdate", KX_Node_OnUpdate::Signature());
  RegisterGameNodeType<KX_Node_ConstFloat>(
      "ConstFloat", "KX_Node_ConstFloat", KX_Node_ConstFloat::Signature());
  RegisterGameNodeType<KX_Node_ConstBool>(
      "ConstBool", "KX_Node_ConstBool", KX_Node_ConstBool::Signature());
  RegisterGameNodeType<KX_Node_Sink>("Sink", "KX_Node_Sink", KX_Node_Sink::Signature());
  RegisterGameNodeType<KX_Node_FloatMath>(
      "FloatMath", "KX_Node_FloatMath", KX_Node_FloatMath::Signature());
  RegisterGameNodeType<KX_Node_VectorMath>(
      "VectorMath", "KX_Node_VectorMath", KX_Node_VectorMath::Signature());
  RegisterGameNodeType<KX_Node_Branch>("Branch", "KX_Node_Branch", KX_Node_Branch::Signature());
  RegisterGameNodeType<KX_Node_Delay>("Delay", "KX_Node_Delay", KX_Node_Delay::Signature());
  RegisterGameNodeType<KX_Node_Raycast>("Raycast", "KX_Node_Raycast", KX_Node_Raycast::Signature());
  RegisterGameNodeType<KX_Node_AssetGet>(
      "AssetGet", "KX_Node_AssetGet", KX_Node_AssetGet::Signature());
  RegisterGameNodeType<KX_Node_AssetSet>(
      "AssetSet", "KX_Node_AssetSet", KX_Node_AssetSet::Signature());
  RegisterGameNodeType<KX_Node_ObjectGet>(
      "ObjectGet", "KX_Node_ObjectGet", KX_Node_ObjectGet::Signature());
  RegisterGameNodeType<KX_Node_ObjectSet>(
      "ObjectSet", "KX_Node_ObjectSet", KX_Node_ObjectSet::Signature());
}

int main()
{
  /* Only register the families compiled into this harness; the parallel B6
   * lines own the rest and are stubbed above. */
  RegisterCoreGameNodesForHarness();
  RegisterFunctionGameNodes();
  RegisterExtendedFunctionGameNodes();
  RegisterB2FunctionGameNodes();
  RegisterB3FunctionGameNodes();
  RegisterB4FileFunctionGameNodes();
  RegisterB4UIFunctionGameNodes();
  RegisterB6InputFunctionGameNodes();
  RegisterB6StringFunctionGameNodes();
  RegisterB6AudioFunctionGameNodes();
  for (int slot = 0; slot < 6; ++slot) {
    const std::string type_id = "TestSink" + std::to_string(slot);
    const int captured = slot;
    KX_NodeRegistry::Get().RegisterType(
        type_id,
        type_id,
        SinkSignature(),
        [captured]() { return std::unique_ptr<KX_LogicNodeRuntime>(new TestSinkNode(captured)); });
  }

  std::printf("INFO harness start\n");

  KX_MockInputAdapter input;
  KX_MockAudioAdapter audio;

  /* -------------------------------------------------------------- input E2E */
  {
    InputGraph ids;
    GraphIR ir = BuildInputGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL input graph did not compile\n");
      return 1;
    }

    input.SetGamepadButton(0, 0, true);
    input.SetGamepadButton(0, 1, true);
    input.SetGamepadButton(0, 2, true);
    input.SetGamepadAxis(0, 0, 0.5f, -0.25f);
    input.SetGamepadTrigger(0, 0, 0.75f);
    input.SetTouch(0, 0.25f, 0.5f, true);
    input.SetTouch(1, 0.75f, 0.9f, false);
    instance->Tick(0.016f);

    /* InputMap binding must be visible in the same frame (no next-frame delay). */
    const KX_RuntimeValue *mapped_same = ReadOutput(*instance, ids.get_value, 0);
    Check(mapped_same != nullptr && mapped_same->AsFloat() == 1.0f,
          "GetInputValue same frame",
          mapped_same != nullptr ? mapped_same->AsFloat() : 0.0,
          1.0);

    instance->Tick(0.016f);

    const KX_RuntimeValue *down = ReadOutput(*instance, ids.press, 1);
    Check(down != nullptr && down->AsBool(), "GamepadButton IsDown");

    /* InputMap was registered on the first frame; the action points at
     * gamepad button 0, which is still held here. */
    const KX_RuntimeValue *mapped = ReadOutput(*instance, ids.get_value, 0);
    const KX_RuntimeValue *mapped_down = ReadOutput(*instance, ids.get_value, 1);
    Check(mapped != nullptr && mapped->AsFloat() == 1.0f, "GetInputValue value",
          mapped != nullptr ? mapped->AsFloat() : 0.0, 1.0);
    Check(mapped_down != nullptr && mapped_down->AsBool(), "GetInputValue IsDown");

    input.SetGamepadButton(0, 0, false);
    input.SetGamepadButton(0, 1, false);
    input.SetGamepadButton(0, 2, false);
    instance->Tick(0.016f);

    const KX_RuntimeValue *axis_x = ReadOutput(*instance, ids.axis, 0);
    const KX_RuntimeValue *axis_y = ReadOutput(*instance, ids.axis, 1);
    Check(axis_x != nullptr && std::fabs(axis_x->AsFloat() - 0.5f) < 1e-5f,
          "GamepadAxis X",
          axis_x != nullptr ? axis_x->AsFloat() : 0.0f,
          0.5f);
    Check(axis_y != nullptr && std::fabs(axis_y->AsFloat() + 0.25f) < 1e-5f,
          "GamepadAxis Y",
          axis_y != nullptr ? axis_y->AsFloat() : 0.0f,
          -0.25f);
    const KX_RuntimeValue *trigger = ReadOutput(*instance, ids.trigger, 0);
    Check(trigger != nullptr && std::fabs(trigger->AsFloat() - 0.75f) < 1e-5f,
          "GamepadTrigger value",
          trigger != nullptr ? trigger->AsFloat() : 0.0f,
          0.75f);
    const KX_RuntimeValue *count = ReadOutput(*instance, ids.touch, 0);
    const KX_RuntimeValue *touch0 = ReadOutput(*instance, ids.touch, 1);
    const KX_RuntimeValue *down0 = ReadOutput(*instance, ids.touch, 3);
    Check(count != nullptr && count->AsInt() == 2, "TouchInput count",
          count != nullptr ? count->AsInt() : 0.0, 2.0);
    Check(touch0 != nullptr && std::fabs(touch0->AsFloat() - 0.25f) < 1e-5f,
          "TouchInput Position0.x",
          touch0 != nullptr ? touch0->AsFloat() : 0.0f,
          0.25f);
    Check(down0 != nullptr && down0->AsBool(), "TouchInput Down0");

    /* KeyCombo uses the mock keyboard state and is pressed on the next frame. */
    input.SetKeyDown("ctrl", true);
    input.SetKeyDown("shift", true);
    input.SetKeyDown("a", true);
    instance->Tick(0.016f);
    instance->Tick(0.016f);
    input.SetKeyDown("a", false);
    instance->Tick(0.016f);

    Check(input.vibrate_calls == 1, "VibrateGamepad reached the adapter",
          static_cast<double>(input.vibrate_calls), 1.0);
    Check(g_sink_hits[0] == 1, "Press fired exactly once",
          g_sink_hits[0], 1.0);
    Check(g_sink_hits[1] == 1, "Release fired exactly once",
          g_sink_hits[1], 1.0);
    Check(g_sink_hits[2] == 2, "Hold fired every held frame", g_sink_hits[2], 2.0);
    Check(g_sink_hits[3] == 1, "KeyCombo fired exactly once",
          g_sink_hits[3], 1.0);

    float probe_value = 0.0f;
    bool probe_down = false;
    const bool probe_ok = input.ReadInputMap("jump", probe_value, probe_down);
    std::printf("INFO InputMap lookup ok=%d name_count=%s\n",
                probe_ok ? 1 : 0,
                probe_ok ? "hit" : "miss");
    Check(g_sink_hits[4] == 1, "InputMap OnDone fired", g_sink_hits[4], 1.0);

    delete instance;
  }

  /* --------------------------------------------------------- string/list E2E */
  {
    StringGraph ids;
    GraphIR ir = BuildStringGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL string graph did not compile\n");
      return 1;
    }
    instance->Tick(0.016f);

    const KX_RuntimeValue *parts = ReadOutput(*instance, ids.split, 0);
    const KX_ValueList *part_list = AsListOf(parts);
    Check(part_list != nullptr && part_list->size() == 3, "StringSplit a,b,c -> 3",
          part_list != nullptr ? static_cast<double>(part_list->size()) : 0.0, 3.0);
    Check(part_list != nullptr && part_list->size() == 3 &&
              std::string((*part_list)[1].AsString()) == "b",
          "StringSplit second element is b");

    const KX_ValueList *multi_list = AsListOf(ReadOutput(*instance, ids.split_multi, 0));
    Check(multi_list != nullptr && multi_list->size() == 3 &&
              std::string((*multi_list)[0].AsString()) == "a" &&
              std::string((*multi_list)[1].AsString()) == "b" &&
              std::string((*multi_list)[2].AsString()) == "c",
          "StringSplit multi delimiter ,; splits a,b;c into a b c",
          multi_list != nullptr ? static_cast<double>(multi_list->size()) : 0.0,
          3.0);

    const KX_ValueList *empty_list = AsListOf(ReadOutput(*instance, ids.split_empty, 0));
    Check(empty_list != nullptr && empty_list->size() == 3 &&
              std::string((*empty_list)[0].AsString()) == "a" &&
              std::string((*empty_list)[1].AsString()).empty() &&
              std::string((*empty_list)[2].AsString()) == "b",
          "StringSplit keeps the empty field in a,,b",
          empty_list != nullptr ? static_cast<double>(empty_list->size()) : 0.0,
          3.0);

    const KX_ValueList *edge_list = AsListOf(ReadOutput(*instance, ids.split_edges, 0));
    Check(edge_list != nullptr && edge_list->size() == 3 &&
              std::string((*edge_list)[0].AsString()).empty() &&
              std::string((*edge_list)[1].AsString()) == "a" &&
              std::string((*edge_list)[2].AsString()).empty(),
          "StringSplit keeps leading and trailing empty fields",
          edge_list != nullptr ? static_cast<double>(edge_list->size()) : 0.0,
          3.0);

    const KX_ValueList *ws_list = AsListOf(ReadOutput(*instance, ids.split_whitespace, 0));
    Check(ws_list != nullptr && ws_list->size() == 2 &&
              std::string((*ws_list)[0].AsString()) == "a" &&
              std::string((*ws_list)[1].AsString()) == "b",
          "StringSplit empty separator collapses whitespace",
          ws_list != nullptr ? static_cast<double>(ws_list->size()) : 0.0,
          2.0);

    const KX_ValueList *nested_list = AsListOf(ReadOutput(*instance, ids.group_nested, 1));
    Check(nested_list != nullptr && nested_list->size() == 4 &&
              std::string((*nested_list)[0].AsString()) == "ab" &&
              std::string((*nested_list)[1].AsString()) == "ab" &&
              std::string((*nested_list)[2].AsString()) == "a" &&
              std::string((*nested_list)[3].AsString()) == "b",
          "StringRegexMatch nested groups are full match then outer then inner",
          nested_list != nullptr ? static_cast<double>(nested_list->size()) : 0.0,
          4.0);

    const KX_RuntimeValue *optional_matched = ReadOutput(*instance, ids.group_optional, 0);
    const KX_ValueList *optional_list = AsListOf(ReadOutput(*instance, ids.group_optional, 1));
    Check(optional_matched != nullptr && optional_matched->AsBool() &&
              optional_list != nullptr && optional_list->size() == 3 &&
              std::string((*optional_list)[0].AsString()) == "b" &&
              std::string((*optional_list)[1].AsString()).empty() &&
              std::string((*optional_list)[2].AsString()) == "b",
          "StringRegexMatch unmatched optional group is an empty string",
          optional_list != nullptr ? static_cast<double>(optional_list->size()) : 0.0,
          3.0);

    const KX_RuntimeValue *nomatch_matched = ReadOutput(*instance, ids.group_nomatch, 0);
    const KX_ValueList *nomatch_list = AsListOf(ReadOutput(*instance, ids.group_nomatch, 1));
    Check(nomatch_matched != nullptr && !nomatch_matched->AsBool() &&
              nomatch_list != nullptr && nomatch_list->size() == 0,
          "StringRegexMatch no match yields an empty group list",
          nomatch_list != nullptr ? static_cast<double>(nomatch_list->size()) : 0.0,
          0.0);

    const KX_RuntimeValue *joined = ReadOutput(*instance, ids.join, 0);
    Check(joined != nullptr && std::string(joined->AsString()) == "a|b|c",
          "StringJoin result");

    const KX_RuntimeValue *matched = ReadOutput(*instance, ids.match, 0);
    const KX_RuntimeValue *groups = ReadOutput(*instance, ids.match, 1);
    const KX_ValueList *group_list = AsListOf(groups);
    Check(matched != nullptr && matched->AsBool(), "StringRegexMatch matched");
    Check(group_list != nullptr && group_list->size() == 3,
          "StringRegexMatch groups include full match",
          group_list != nullptr ? static_cast<double>(group_list->size()) : 0.0,
          3.0);

    const KX_RuntimeValue *replaced = ReadOutput(*instance, ids.replace, 0);
    Check(replaced != nullptr && std::string(replaced->AsString()) == "abcX",
          "StringRegexReplace result");

    const KX_RuntimeValue *trimmed = ReadOutput(*instance, ids.trim, 0);
    Check(trimmed != nullptr && std::string(trimmed->AsString()) == "hi",
          "StringTrim result");

    const KX_RuntimeValue *padded = ReadOutput(*instance, ids.pad, 0);
    Check(padded != nullptr && std::string(padded->AsString()) == "0007",
          "StringPad result");

    const KX_RuntimeValue *char_count = ReadOutput(*instance, ids.len_chars, 0);
    Check(char_count != nullptr && char_count->AsInt() == 4, "StringLengthChars 中文ab",
          char_count != nullptr ? char_count->AsInt() : 0.0, 4.0);
    const KX_RuntimeValue *bad_count = ReadOutput(*instance, ids.len_chars_bad, 0);
    Check(bad_count != nullptr && bad_count->AsInt() == 1, "invalid UTF-8 counts as one",
          bad_count != nullptr ? bad_count->AsInt() : 0.0, 1.0);
    const KX_RuntimeValue *char_pad = ReadOutput(*instance, ids.pad_chars, 0);
    Check(char_pad != nullptr && std::string(char_pad->AsString()) == "文文中",
          "StringPadChars pads by characters");

    const KX_RuntimeValue *sorted_value = ReadOutput(*instance, ids.sort, 0);
    const KX_ValueList *sorted = AsListOf(sorted_value);
    Check(sorted != nullptr && sorted->size() == 3 &&
              (*sorted)[0].AsFloat() == 1.0f && (*sorted)[1].AsFloat() == 2.0f &&
              (*sorted)[2].AsFloat() == 3.0f,
          "ListSort Float ascending");

    const KX_RuntimeValue *filtered_value = ReadOutput(*instance, ids.filter, 0);
    const KX_ValueList *filtered = AsListOf(filtered_value);
    Check(filtered != nullptr && filtered->size() == 2, "ListFilter x > 1",
          filtered != nullptr ? static_cast<double>(filtered->size()) : 0.0, 2.0);

    const KX_RuntimeValue *mapped_value = ReadOutput(*instance, ids.map, 0);
    const KX_ValueList *mapped_list = AsListOf(mapped_value);
    Check(mapped_list != nullptr && mapped_list->size() == 3 &&
              (*mapped_list)[0].AsFloat() == 6.0f && (*mapped_list)[1].AsFloat() == 2.0f &&
              (*mapped_list)[2].AsFloat() == 4.0f,
          "ListMap x * 2");

    const KX_RuntimeValue *found_index = ReadOutput(*instance, ids.find, 0);
    const KX_RuntimeValue *found_flag = ReadOutput(*instance, ids.find, 1);
    Check(found_index != nullptr && found_index->AsInt() == 1, "ListFind index",
          found_index != nullptr ? found_index->AsInt() : -1.0, 1.0);
    Check(found_flag != nullptr && found_flag->AsBool(), "ListFind found");

    const KX_RuntimeValue *slice_value = ReadOutput(*instance, ids.slice, 0);
    const KX_ValueList *slice = AsListOf(slice_value);
    Check(slice != nullptr && slice->size() == 2, "ListSlice [0,2)",
          slice != nullptr ? static_cast<double>(slice->size()) : 0.0, 2.0);

    const KX_RuntimeValue *concat_value = ReadOutput(*instance, ids.concat, 0);
    const KX_ValueList *concat = AsListOf(concat_value);
    Check(concat != nullptr && concat->size() == 6, "ListConcat 3 + 3",
          concat != nullptr ? static_cast<double>(concat->size()) : 0.0, 6.0);

    delete instance;
  }

  /* --------------------------------------------------------------- audio E2E */
  {
    GraphIR ir = BuildAudioGraph();
    KX_GraphInstance *instance = BuildInstance(ir, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL audio graph did not compile\n");
      return 1;
    }
    KX_GameObject *owner = reinterpret_cast<KX_GameObject *>(
        static_cast<uintptr_t>(0x5000));
    instance->GetContext()->SetOwner(owner);
    instance->Tick(0.016f);

    const uint64_t owned_handle = audio.next_handle - 1;
    Check(audio.OwnerOf(owned_handle) == owner,
          "PlaySound3D attributes the handle to the graph owner");

    Check(audio.attenuation_calls == 1, "SetAudioAttenuation reached the adapter",
          static_cast<double>(audio.attenuation_calls), 1.0);
    Check(audio.attenuation_requests.size() == 1 &&
              std::fabs(audio.attenuation_requests[0].reference_distance - 1.0f) < 1e-5f &&
              std::fabs(audio.attenuation_requests[0].maximum_distance - 10.0f) < 1e-5f,
          "Attenuation distances");
    Check(audio.doppler_calls == 1 &&
              std::fabs(audio.doppler_requests[0].factor - 2.0f) < 1e-5f,
          "SetAudioDoppler factor",
          audio.doppler_requests.empty() ? 0.0 : audio.doppler_requests[0].factor,
          2.0);
    Check(audio.zone_calls == 1 && audio.zone_requests[0].zone == "forest" &&
              audio.zone_requests[0].sound == "beep",
          "AudioZone registration");
    Check(audio.bus_calls == 1 && audio.bus_requests[0].bus == "master" &&
              std::fabs(audio.bus_requests[0].volume - 0.5f) < 1e-5f,
          "SetAudioBusVolume master 0.5",
          audio.bus_requests.empty() ? 0.0 : audio.bus_requests[0].volume,
          0.5);
    Check(audio.spatial_calls == 1, "PlaySound3D reached the adapter",
          static_cast<double>(audio.spatial_calls), 1.0);
    Check(audio.spatial_requests.size() == 1 &&
              std::fabs(audio.spatial_requests[0].position[0] - 1.0f) < 1e-5f &&
              std::fabs(audio.spatial_requests[0].position[1] - 2.0f) < 1e-5f &&
              std::fabs(audio.spatial_requests[0].position[2] - 3.0f) < 1e-5f,
          "PlaySound3D position 1,2,3");
    Check(audio.spatial_requests.size() == 1 &&
              std::fabs(audio.spatial_requests[0].volume - 0.8f) < 1e-5f &&
              std::fabs(audio.spatial_requests[0].pitch - 1.2f) < 1e-5f,
          "PlaySound3D volume/pitch");

    /* ------------------------------------------------- object scoped isolation */
    KX_GameObject *object_a = reinterpret_cast<KX_GameObject *>(
        static_cast<uintptr_t>(0x5100));
    KX_GameObject *object_b = reinterpret_cast<KX_GameObject *>(
        static_cast<uintptr_t>(0x5200));
    float origin[3] = {0.0f, 0.0f, 0.0f};
    const uint64_t handle_a = audio.PlaySound3DFor(object_a, "beep", origin, 1.0f, 1.0f);
    const uint64_t handle_b = audio.PlaySound3DFor(object_b, "beep", origin, 1.0f, 1.0f);
    Check(audio.OwnerOf(handle_a) == object_a && audio.OwnerOf(handle_b) == object_b,
          "two objects own their own handles");

    /* Snapshot B before A changes: null-object calls earlier in the graph set
     * the global defaults, so B starts from those values. */
    const float b_ref_before = audio.SettingsOf(handle_b).reference_distance;
    const float b_max_before = audio.SettingsOf(handle_b).maximum_distance;
    const float b_doppler_before = audio.SettingsOf(handle_b).doppler;
    const float b_volume_before = audio.SettingsOf(handle_b).volume;
    audio.SetAttenuation(object_a, 0, 2.0f, 5.0f);
    Check(std::fabs(audio.SettingsOf(handle_a).reference_distance - 2.0f) < 1e-5f,
          "object A attenuation applied",
          audio.SettingsOf(handle_a).reference_distance,
          2.0f);
    Check(std::fabs(audio.SettingsOf(handle_a).maximum_distance - 5.0f) < 1e-5f,
          "object A max distance applied",
          audio.SettingsOf(handle_a).maximum_distance,
          5.0f);
    Check(audio.SettingsOf(handle_b).reference_distance == b_ref_before &&
              audio.SettingsOf(handle_b).maximum_distance == b_max_before &&
              audio.SettingsOf(handle_b).volume == b_volume_before,
          "object B attenuation and volume unchanged",
          audio.SettingsOf(handle_b).reference_distance,
          b_ref_before);

    audio.SetDopplerFactor(object_a, 3.0f);
    Check(std::fabs(audio.SettingsOf(handle_a).doppler - 3.0f) < 1e-5f,
          "object A doppler applied",
          audio.SettingsOf(handle_a).doppler,
          3.0f);
    Check(audio.SettingsOf(handle_b).doppler == b_doppler_before,
          "object B doppler unchanged",
          audio.SettingsOf(handle_b).doppler,
          b_doppler_before);

    audio.ForgetObject(object_a);
    Check(audio.HandlesFor(object_a).empty() &&
              audio.OwnerOf(handle_a) == nullptr,
          "ForgetObject drops A ownership");
    Check(audio.OwnerOf(handle_b) == object_b,
          "ForgetObject keeps B ownership");

    delete instance;
  }

  /* ---------------------------------------------------- sound handle E2E */
  {
    /* 1) A handle inside 32 bits round-trips unchanged and OnStarted fires. */
    audio.Reset();
    g_sink_hits[5] = 0;
    StartSoundGraph small;
    GraphIR ir_small = BuildStartSoundGraph(&small, "beep.wav");
    KX_GraphInstance *instance = BuildInstance(ir_small, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL small handle graph did not compile\n");
      return 1;
    }
    instance->Tick(0.016f);
    const KX_RuntimeValue *handle_small = ReadOutput(*instance, small.start, 1);
    Check(handle_small != nullptr && handle_small->AsInt() == 1,
          "StartSound small handle round-trip",
          handle_small != nullptr ? handle_small->AsInt() : -1.0,
          1.0);
    Check(g_sink_hits[5] == 1,
          "StartSound OnStarted fires",
          static_cast<double>(g_sink_hits[5]),
          1.0);
    delete instance;

    /* 2) The largest representable id survives the port untouched. */
    audio.Reset();
    g_sink_hits[5] = 0;
    audio.next_handle = 0x7fffffffu;
    StartSoundGraph max;
    GraphIR ir_max = BuildStartSoundGraph(&max, "beep.wav");
    instance = BuildInstance(ir_max, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL INT32_MAX handle graph did not compile\n");
      return 1;
    }
    instance->Tick(0.016f);
    const KX_RuntimeValue *handle_max = ReadOutput(*instance, max.start, 1);
    Check(handle_max != nullptr && handle_max->AsInt() == 2147483647,
          "StartSound INT32_MAX handle is not truncated",
          handle_max != nullptr ? handle_max->AsInt() : -1.0,
          2147483647.0);
    Check(g_sink_hits[5] == 1, "StartSound INT32_MAX still starts", g_sink_hits[5], 1.0);
    delete instance;

    /* 3) An id above INT32_MAX is rejected instead of wrapping negative. */
    audio.Reset();
    g_sink_hits[5] = 0;
    audio.next_handle = 0x80000000u;
    StartSoundGraph overflow;
    GraphIR ir_over = BuildStartSoundGraph(&overflow, "beep.wav");
    instance = BuildInstance(ir_over, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL overflow handle graph did not compile\n");
      return 1;
    }
    instance->Tick(0.016f);
    const KX_RuntimeValue *handle_over = ReadOutput(*instance, overflow.start, 1);
    Check(handle_over != nullptr && handle_over->AsInt() == 0,
          "StartSound rejects a handle above INT32_MAX",
          handle_over != nullptr ? handle_over->AsInt() : -1.0,
          0.0);
    Check(g_sink_hits[5] == 0, "StartSound does not fire OnStarted on rejection",
          g_sink_hits[5], 0.0);
    Check(audio.stop_calls == 1, "rejected handle is stopped, not leaked",
          audio.stop_calls, 1.0);
    auto overflow_entry = audio.entries.find(0x80000000u);
    Check(overflow_entry != audio.entries.end() && !overflow_entry->second.playing,
          "rejected handle stops playing");
    delete instance;

    /* 4) The 32-bit value StartSound writes is what StopSound consumes. */
    audio.Reset();
    StartStopGraph start_stop;
    GraphIR ir_stop = BuildStartStopGraph(&start_stop, "beep.wav");
    instance = BuildInstance(ir_stop, input, audio);
    if (instance == nullptr) {
      std::printf("FAIL start/stop graph did not compile\n");
      return 1;
    }
    instance->Tick(0.016f);
    Check(audio.play_calls == 1 && audio.stop_calls == 1,
          "StartSound then StopSound reach the adapter once each",
          static_cast<double>(audio.stop_calls),
          1.0);
    auto stopped_entry = audio.entries.find(1);
    Check(stopped_entry != audio.entries.end() && !stopped_entry->second.playing,
          "StartSound handle stops the sound it started");
    delete instance;
  }

  std::printf("%s total failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
