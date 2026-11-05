// B4 regression E2E rebuilt for P4: all 17 B4 nodes (9 UI + 8 file) compiled
// into one graph, then hot reloaded repeatedly.
//
// The point is not only that every node runs, but that a rebuild keeps the same
// UI handles and the same file adapter: the migrated node state must make the
// UI nodes reuse their elements instead of creating duplicates.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_FileAdapter.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

/* The B5.2 source is being written by a parallel line; stub it so the harness
 * links without that translation unit. */
void RegisterB5FunctionGameNodes() {}

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

/* JSON texts used by the round-trip fixture. They are stored in string fields,
 * because the JSON node schema only exposes string/float/int/bool ports. */
const std::string kNestedJson = "{\"a\":{\"b\":{\"c\":7}}}";
const std::string kArrayJson = "[1,2,{\"k\":3}]";
const std::string kEscapedText = "quote\" backslash\\ end";
/* The config JSON parser does not understand "\n", so the newline is carried as
 * the two characters backslash+n inside a JSON document and decoded by
 * JSONParse instead. */
const std::string kEscJson =
    "{\"payload\":\"quote \\\" backslash \\\\ newline \\n end\"}";
const std::string kEscPayload = "quote \" backslash \\ newline \n end";
const std::string kSiblingJson =
    "{\"id\":42,\"name\":\"Ada\",\"nested\":{\"x\":[1,2]},\"list\":[true,3]}";

/** Build a StringConst config, escaping the payload for the config JSON. */
std::string StringConstConfig(const std::string &value)
{
  std::string escaped;
  for (char c : value) {
    switch (c) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return "{\"value\": \"" + escaped + "\"}";
}

const char *StringOf(const KX_RuntimeValue *value)
{
  if (value == nullptr || value->type != KX_VALUE_STRING) {
    return nullptr;
  }
  return value->AsString();
}

/** Value assertion: the node output must equal the original text byte for byte. */
void CheckString(const KX_RuntimeValue *value, const std::string &expected, const char *what)
{
  const char *text = StringOf(value);
  const bool equal = text != nullptr && expected == text;
  Check(equal, what);
  std::printf("INFO %s: actual=%s\n", what, text != nullptr ? text : "<null>");
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

/** Read a non-trigger output from the compiled plan. */
const KX_RuntimeValue *ReadOutput(KX_GraphInstance &instance, int node, int port)
{
  const KX_ExecutionPlan *plan = instance.GetGraph() != nullptr ? instance.GetGraph()->Plan() : nullptr;
  if (plan == nullptr) {
    return nullptr;
  }
  for (const KX_PortOffsetInfo &info : plan->port_offsets) {
    if (!info.input && static_cast<int>(info.node_index) == node &&
        static_cast<int>(info.port_index) == port && !info.trigger)
    {
      return &instance.GetGraph()->Data().Value(info.offset);
    }
  }
  return nullptr;
}

/*
 * 0  OnInit "init"
 * 1  ConstFloat "one"                 (helper: numeric -> string path)
 * 2  NumberToString "path"
 * 3  CreateUICanvas "canvas"
 * 4  CreateUILayout "layout"          parent <- canvas
 * 5  CreateUILabel "label"            parent <- canvas
 * 6  CreateUIImage "image"
 * 7  CreateUIButton "button"
 * 8  CreateUISliderWidget "slider"
 * 9  MoveWidget "move"                widget <- label
 * 10 SetUIWidgetAttr "setattr"        widget <- label
 * 11 GetUIWidgetAttr "getattr"        value node
 * 12 SaveGame "save"
 * 13 LoadGame "load"
 * 14 ReadFile "read"
 * 15 WriteFile "write"
 * 16 FileExists "exists"
 * 17 DeleteFile "delete"
 * 18 JSONStringify "jstr"
 * 19 JSONParse "jparse"               source <- jstr result
 */
GraphIR BuildB4Graph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  ir.nodes.push_back(MakeNode("ConstFloat", "one"));
  ir.nodes[1].config_json = "{\"value\": \"1\"}";
  ir.nodes.push_back(MakeNode("NumberToString", "path"));
  ir.nodes.push_back(MakeNode("CreateUICanvas", "canvas"));
  ir.nodes.push_back(MakeNode("CreateUILayout", "layout"));
  ir.nodes.push_back(MakeNode("CreateUILabel", "label"));
  ir.nodes.push_back(MakeNode("CreateUIImage", "image"));
  ir.nodes.push_back(MakeNode("CreateUIButton", "button"));
  ir.nodes.push_back(MakeNode("CreateUISliderWidget", "slider"));
  ir.nodes.push_back(MakeNode("MoveWidget", "move"));
  ir.nodes[9].config_json = "{\"mode\": \"0\"}";
  ir.nodes.push_back(MakeNode("SetUIWidgetAttr", "setattr"));
  ir.nodes[10].config_json = "{\"widget_attr\": \"0\"}";
  /* "show" resolves to Int in the attribute type table. */
  ir.nodes[10].inputs[2].value_tag = KX_VALUE_INT;
  ir.nodes.push_back(MakeNode("GetUIWidgetAttr", "getattr"));
  ir.nodes[11].config_json = "{\"widget_attr\": \"0\"}";
  ir.nodes.push_back(MakeNode("SaveGame", "save"));
  ir.nodes.push_back(MakeNode("LoadGame", "load"));
  ir.nodes.push_back(MakeNode("ReadFile", "read"));
  ir.nodes.push_back(MakeNode("WriteFile", "write"));
  ir.nodes.push_back(MakeNode("FileExists", "exists"));
  ir.nodes.push_back(MakeNode("DeleteFile", "delete"));

  /* JSON nodes take their ports from the IR: one float field plus the status
   * triggers. */
  IRNode stringify = MakeNode("JSONStringify", "jstr");
  stringify.inputs.resize(1);
  stringify.inputs.push_back(IRPort{"v", IRPortKind::Value, KX_VALUE_FLOAT, false, ""});
  stringify.config_json = "{\"schema\": \"v:float\"}";
  ir.nodes.push_back(std::move(stringify));

  IRNode parse;
  parse.type_id = "JSONParse";
  parse.debug_name = "jparse";
  parse.inputs.push_back(IRPort{"OnEnter", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  parse.inputs.push_back(IRPort{"Source", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  parse.outputs.push_back(IRPort{"v", IRPortKind::Value, KX_VALUE_FLOAT, false, ""});
  parse.outputs.push_back(IRPort{"OnSuccess", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  parse.outputs.push_back(IRPort{"OnFail", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  parse.config_json = "{\"schema\": \"v:float\"}";
  ir.nodes.push_back(std::move(parse));

  /* 20-24: round-trip fixture. Three StringConst payloads feed one
   * JSONStringify, whose Result drives the Source of a JSONParse. Every field
   * is a string carrying JSON text; the assertions compare the parsed values
   * with the constants byte for byte. */
  IRNode nested_text = MakeNode("StringConst", "nestedtext");
  nested_text.config_json = StringConstConfig(kNestedJson);
  ir.nodes.push_back(std::move(nested_text));

  IRNode array_text = MakeNode("StringConst", "arraytext");
  array_text.config_json = StringConstConfig(kArrayJson);
  ir.nodes.push_back(std::move(array_text));

  IRNode escaped_text = MakeNode("StringConst", "esctext");
  escaped_text.config_json = StringConstConfig(kEscapedText);
  ir.nodes.push_back(std::move(escaped_text));

  IRNode rt_stringify = MakeNode("JSONStringify", "rtstr");
  rt_stringify.inputs.push_back(IRPort{"nested", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_stringify.inputs.push_back(IRPort{"arr", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_stringify.inputs.push_back(IRPort{"esc", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_stringify.config_json = "{\"schema\": \"nested:string,arr:string,esc:string\"}";
  ir.nodes.push_back(std::move(rt_stringify));

  IRNode rt_parse;
  rt_parse.type_id = "JSONParse";
  rt_parse.debug_name = "rtparse";
  rt_parse.inputs.push_back(IRPort{"OnEnter", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  rt_parse.inputs.push_back(IRPort{"Source", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_parse.outputs.push_back(IRPort{"nested", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_parse.outputs.push_back(IRPort{"arr", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_parse.outputs.push_back(IRPort{"esc", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  rt_parse.outputs.push_back(IRPort{"OnSuccess", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  rt_parse.outputs.push_back(IRPort{"OnFail", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  rt_parse.config_json = "{\"schema\": \"nested:string,arr:string,esc:string\"}";
  ir.nodes.push_back(std::move(rt_parse));

  /* 25-26: a real JSON object with nested-object and array siblings next to
   * the scalar fields. Only the scalars have ports; the assertion proves the
   * unknown siblings do not disturb them. */
  IRNode sibling_text = MakeNode("StringConst", "sibtext");
  sibling_text.config_json = StringConstConfig(kSiblingJson);
  ir.nodes.push_back(std::move(sibling_text));

  IRNode sibling_parse;
  sibling_parse.type_id = "JSONParse";
  sibling_parse.debug_name = "sibparse";
  sibling_parse.inputs.push_back(IRPort{"OnEnter", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  sibling_parse.inputs.push_back(IRPort{"Source", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  sibling_parse.outputs.push_back(IRPort{"id", IRPortKind::Value, KX_VALUE_INT, false, ""});
  sibling_parse.outputs.push_back(IRPort{"name", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  sibling_parse.outputs.push_back(IRPort{"OnSuccess", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  sibling_parse.outputs.push_back(IRPort{"OnFail", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  sibling_parse.config_json = "{\"schema\": \"id:int,name:string\"}";
  ir.nodes.push_back(std::move(sibling_parse));

  /* 27-30: newline coverage. JSONParse decodes the escaped payload into a real
   * newline, then JSONStringify re-escapes it and a second JSONParse proves the
   * round-trip keeps every byte. */
  IRNode esc_json_text = MakeNode("StringConst", "escjson");
  esc_json_text.config_json = StringConstConfig(kEscJson);
  ir.nodes.push_back(std::move(esc_json_text));

  IRNode esc_parse;
  esc_parse.type_id = "JSONParse";
  esc_parse.debug_name = "escparse";
  esc_parse.inputs.push_back(IRPort{"OnEnter", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  esc_parse.inputs.push_back(IRPort{"Source", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  esc_parse.outputs.push_back(IRPort{"payload", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  esc_parse.outputs.push_back(IRPort{"OnSuccess", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  esc_parse.outputs.push_back(IRPort{"OnFail", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  esc_parse.config_json = "{\"schema\": \"payload:string\"}";
  ir.nodes.push_back(std::move(esc_parse));

  IRNode esc_stringify = MakeNode("JSONStringify", "escstr");
  esc_stringify.inputs.push_back(IRPort{"payload", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  esc_stringify.config_json = "{\"schema\": \"payload:string\"}";
  ir.nodes.push_back(std::move(esc_stringify));

  IRNode esc_roundtrip;
  esc_roundtrip.type_id = "JSONParse";
  esc_roundtrip.debug_name = "escrt";
  esc_roundtrip.inputs.push_back(IRPort{"OnEnter", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  esc_roundtrip.inputs.push_back(IRPort{"Source", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  esc_roundtrip.outputs.push_back(IRPort{"payload", IRPortKind::Value, KX_VALUE_STRING, false, ""});
  esc_roundtrip.outputs.push_back(IRPort{"OnSuccess", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  esc_roundtrip.outputs.push_back(IRPort{"OnFail", IRPortKind::Trigger, KX_VALUE_NONE, false, ""});
  esc_roundtrip.config_json = "{\"schema\": \"payload:string\"}";
  ir.nodes.push_back(std::move(esc_roundtrip));

  /* Helpers. */
  Connect(ir, 1, 0, 2, 0);   /* one -> path */
  /* Canvas, then its children from the Done trigger. */
  Connect(ir, 0, 0, 3, 0);
  Connect(ir, 3, 0, 4, 0);
  Connect(ir, 3, 0, 5, 0);
  Connect(ir, 3, 0, 6, 0);
  Connect(ir, 3, 0, 7, 0);
  Connect(ir, 3, 0, 8, 0);
  Connect(ir, 3, 1, 4, 1);   /* canvas -> layout parent */
  Connect(ir, 3, 1, 5, 1);
  Connect(ir, 3, 1, 6, 1);
  Connect(ir, 3, 1, 7, 1);
  Connect(ir, 3, 1, 8, 1);
  /* Handle based nodes follow the label. */
  Connect(ir, 5, 0, 9, 0);
  Connect(ir, 5, 1, 9, 1);
  Connect(ir, 5, 0, 10, 0);
  Connect(ir, 5, 1, 10, 1);
  /* Value stays unconnected: the optional Int slot reads as zero. */
  Connect(ir, 5, 1, 11, 0);
  /* File nodes share the generated path. */
  Connect(ir, 0, 0, 12, 0);
  Connect(ir, 2, 0, 12, 1);
  Connect(ir, 0, 0, 13, 0);
  Connect(ir, 2, 0, 13, 1);
  Connect(ir, 0, 0, 14, 0);
  Connect(ir, 2, 0, 14, 1);
  Connect(ir, 0, 0, 15, 0);
  Connect(ir, 2, 0, 15, 1);
  Connect(ir, 2, 0, 15, 2);
  Connect(ir, 0, 0, 16, 0);
  Connect(ir, 2, 0, 16, 1);
  Connect(ir, 0, 0, 17, 0);
  Connect(ir, 2, 0, 17, 1);
  /* JSON chain. The parse is triggered by the stringify success instead of
   * OnInit so the write happens before the read inside the same tick. */
  Connect(ir, 0, 0, 18, 0);
  Connect(ir, 1, 0, 18, 1);
  Connect(ir, 18, 1, 19, 0);
  Connect(ir, 18, 0, 19, 1);
  /* Round-trip fixture: constants -> stringify -> parse. */
  Connect(ir, 0, 0, 23, 0);
  Connect(ir, 20, 0, 23, 1);
  Connect(ir, 21, 0, 23, 2);
  Connect(ir, 22, 0, 23, 3);
  Connect(ir, 23, 1, 24, 0);
  Connect(ir, 23, 0, 24, 1);
  /* Sibling fixture: object with nested/array fields, scalar ports only.
   * Node 25 is the StringConst payload, node 26 the JSONParse. */
  Connect(ir, 0, 0, 26, 0);
  Connect(ir, 25, 0, 26, 1);
  /* Escaped payload: parse -> stringify -> parse. */
  Connect(ir, 0, 0, 28, 0);
  Connect(ir, 27, 0, 28, 1);
  Connect(ir, 28, 1, 29, 0);
  Connect(ir, 28, 0, 29, 1);
  Connect(ir, 29, 1, 30, 0);
  Connect(ir, 29, 0, 30, 1);
  return ir;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  const char *kB4Names[] = {"CreateUICanvas",
                            "CreateUILayout",
                            "CreateUILabel",
                            "CreateUIImage",
                            "CreateUIButton",
                            "CreateUISliderWidget",
                            "MoveWidget",
                            "SetUIWidgetAttr",
                            "GetUIWidgetAttr",
                            "SaveGame",
                            "LoadGame",
                            "ReadFile",
                            "WriteFile",
                            "FileExists",
                            "DeleteFile",
                            "JSONStringify",
                            "JSONParse"};
  for (const char *name : kB4Names) {
    Check(registry.FindByEditorType(name) != KX_INVALID_NODE_TYPE_ID,
          ("B4 node registered: " + std::string(name)).c_str());
  }

  KX_MockUIAdapter ui;
  KX_MockFileAdapter file;
  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  instance.GetContext()->SetAdapters(
      nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ui, &file);

  const GraphIR graph = BuildB4Graph();
  std::vector<std::string> diagnostics;
  Check(instance.Load(graph, &diagnostics), "all 17 B4 nodes compile into one graph");
  for (const std::string &diagnostic : diagnostics) {
    std::printf("INFO compile: %s\n", diagnostic.c_str());
  }

  instance.Tick(0.016f);

  Check(ui.create_canvas_calls == 1, "canvas created once");
  Check(ui.create_layout_calls == 1, "layout created once");
  Check(ui.create_label_calls == 1, "label created once");
  Check(ui.create_image_calls == 1, "image created once");
  Check(ui.create_button_calls == 1, "button created once");
  Check(ui.create_slider_calls == 1, "slider created once");
  Check(ui.move_calls >= 1, "MoveWidget reached the adapter");
  Check(ui.set_attribute_calls >= 1, "SetUIWidgetAttr reached the adapter");
  Check(ui.get_attribute_calls >= 1, "GetUIWidgetAttr reached the adapter");
  Check(file.save_calls == 1, "SaveGame reached the adapter");
  Check(file.load_calls == 1, "LoadGame reached the adapter");
  Check(file.read_calls == 1, "ReadFile reached the adapter");
  Check(file.write_calls == 1, "WriteFile reached the adapter");
  Check(file.exists_calls == 1, "FileExists reached the adapter");
  Check(file.delete_calls == 1, "DeleteFile reached the adapter");
  Check(file.files.count(KX_MockFileKey("1.000")) == 1, "save slot persisted in the adapter");

  /* JSON value assertions: read the outputs, never just "did not crash". */
  const char *stringified_text = StringOf(ReadOutput(instance, 23, 0));
  std::printf("INFO JSONStringify result: %s\n", stringified_text != nullptr ? stringified_text : "<null>");
  Check(stringified_text != nullptr && std::strstr(stringified_text, "\\\"") != nullptr &&
            std::strstr(stringified_text, "\\\\") != nullptr,
        "JSONStringify escapes quote and backslash");
  CheckString(ReadOutput(instance, 24, 0),
              kNestedJson,
              "JSONParse round-trip keeps the nested object text");
  CheckString(ReadOutput(instance, 24, 1),
              kArrayJson,
              "JSONParse round-trip keeps the array text");
  CheckString(ReadOutput(instance, 24, 2),
              kEscapedText,
              "JSONParse round-trip keeps quote and backslash");
  /* Newline: decoded by the first parse, then re-escaped and parsed back. */
  CheckString(ReadOutput(instance, 28, 0),
              kEscPayload,
              "JSONParse decodes quote/backslash/newline escapes");
  const char *escaped_stringify = StringOf(ReadOutput(instance, 29, 0));
  std::printf("INFO JSONStringify escaped result: %s\n",
              escaped_stringify != nullptr ? escaped_stringify : "<null>");
  Check(escaped_stringify != nullptr && std::strstr(escaped_stringify, "\\n") != nullptr,
        "JSONStringify re-escapes the decoded newline");
  CheckString(ReadOutput(instance, 30, 0),
              kEscPayload,
              "JSONParse round-trip keeps the decoded newline");
  const KX_RuntimeValue *sibling_id = ReadOutput(instance, 26, 0);
  Check(sibling_id != nullptr && sibling_id->AsInt() == 42,
        "JSONParse reads id next to nested/array siblings");
  CheckString(ReadOutput(instance, 26, 1),
              "Ada",
              "JSONParse reads name next to nested/array siblings");
  const KX_RuntimeValue *legacy_v = ReadOutput(instance, 19, 0);
  Check(legacy_v != nullptr && std::fabs(legacy_v->AsFloat() - 1.0f) < 1e-6f,
        "JSONStringify/JSONParse v:float round-trip");

  const int canvas_handle = instance.GetGraph()->UIHandle("canvas");
  const int label_handle = instance.GetGraph()->UIHandle("label");
  const int image_handle = instance.GetGraph()->UIHandle("image");
  const int button_handle = instance.GetGraph()->UIHandle("button");
  const int layout_handle = instance.GetGraph()->UIHandle("layout");
  const int slider_handle = instance.GetGraph()->UIHandle("slider");
  std::printf("INFO handles canvas=%d layout=%d label=%d image=%d button=%d slider=%d\n",
              canvas_handle,
              layout_handle,
              label_handle,
              image_handle,
              button_handle,
              slider_handle);
  Check(ui.create_canvas_calls == 1 && ui.create_slider_calls == 1 &&
            instance.GetGraph()->UIHandle("getattr") == -1,
        "handle map covers the stateful nodes only");

  int loop_failures = 0;
  for (int iteration = 0; iteration < 10; ++iteration) {
    Check(instance.Rebuild(graph, nullptr), "B4 graph rebuild staged");
    instance.Tick(0.016f);
    const bool same_handles = instance.GetGraph()->UIHandle("canvas") == canvas_handle &&
                              instance.GetGraph()->UIHandle("label") == label_handle &&
                              instance.GetGraph()->UIHandle("image") == image_handle &&
                              instance.GetGraph()->UIHandle("button") == button_handle &&
                              instance.GetGraph()->UIHandle("layout") == layout_handle &&
                              instance.GetGraph()->UIHandle("slider") == slider_handle;
    if (!same_handles) {
      std::printf("FAIL iteration %d: UI handles changed after rebuild\n", iteration);
      ++loop_failures;
    }
    if (ui.create_canvas_calls != 1 || ui.create_label_calls != 1 ||
        ui.create_image_calls != 1 || ui.create_button_calls != 1 ||
        ui.create_layout_calls != 1 || ui.create_slider_calls != 1)
    {
      std::printf("FAIL iteration %d: rebuild duplicated a UI element\n", iteration);
      ++loop_failures;
    }
    if (ui.destroy_calls != 0) {
      std::printf("FAIL iteration %d: rebuild destroyed a UI element\n", iteration);
      ++loop_failures;
    }
    if (file.files.count(KX_MockFileKey("1.000")) != 1) {
      std::printf("FAIL iteration %d: file adapter lost its save slot\n", iteration);
      ++loop_failures;
    }
  }
  std::printf("%s 10 rebuilds kept UI handles and the file adapter intact\n",
              loop_failures == 0 ? "PASS" : "FAIL");
  if (loop_failures != 0) {
    ++failures;
  }

  std::printf("INFO file calls save=%zu load=%zu read=%zu write=%zu exists=%zu delete=%zu\n",
              file.save_calls,
              file.load_calls,
              file.read_calls,
              file.write_calls,
              file.exists_calls,
              file.delete_calls);
  std::printf("INFO ui calls canvas=%u layout=%u label=%u image=%u button=%u slider=%u "
              "move=%u attr_set=%u attr_get=%u destroyed=%u\n",
              ui.create_canvas_calls,
              ui.create_layout_calls,
              ui.create_label_calls,
              ui.create_image_calls,
              ui.create_button_calls,
              ui.create_slider_calls,
              ui.move_calls,
              ui.set_attribute_calls,
              ui.get_attribute_calls,
              ui.destroy_calls);

  std::printf("INFO assertions=%d\n", assertions);
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
