// B6.4 E2E harness: advanced UI and file nodes.
//
// Drives the mock UI/file adapters through a real compiled graph. The graph
// entry rules used here rely on the documented behaviour that unconnected
// trigger inputs execute every frame, while OnInit fires once.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_DebugTrace.h"
#include "KX_ExecutionContext.h"
#include "KX_FileAdapter.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_UIRenderer.h"
#include "KX_UIElement.h"
#include "KX_ValuePool.h"

/* Parallel B6 lines are not part of this harness. */
void RegisterB5FunctionGameNodes() {}
void RegisterB6NetworkFunctionGameNodes() {}
void RegisterB6SensorFunctionGameNodes() {}
void RegisterB6SceneFunctionGameNodes() {}
void RegisterB6LegacyFunctionGameNodes() {}
void RegisterB6LegacyQueryFunctionGameNodes() {}
void RegisterB6AdvancedFunctionGameNodes() {}

namespace {

int failures = 0;

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

/* ------------------------------------------------------------------ harness */

int g_sink_hits[16] = {0};
int g_ints[12] = {0};
std::vector<std::vector<std::string>> g_lists(12);
float g_vectors[12][3] = {{0.0f, 0.0f, 0.0f}};
bool g_gates[12] = {false};

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

class TestListNode : public KX_GameNodeRuntime {
 public:
  explicit TestListNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    const std::vector<std::string> &source = g_lists[slot_];
    KX_ValueList &list = ctx.Pool().AllocList(source.size());
    for (const std::string &text : source) {
      list.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(text)));
    }
    OutputValue(ctx, 0) = KX_RuntimeValue::List(&list);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

class TestVectorNode : public KX_GameNodeRuntime {
 public:
  explicit TestVectorNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    const float *v = g_vectors[slot_];
    OutputValue(ctx, 0) = KX_RuntimeValue::Vector(v[0], v[1], v[2]);
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

/* Root node that raises its trigger only while the harness flag is set. */
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

KX_NodeSignature ListSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "List";
  output.kind = IRPortKind::Value;
  output.value_type = KX_VALUE_LIST;
  signature.outputs.push_back(output);
  return signature;
}

KX_NodeSignature VectorSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Vector";
  output.kind = IRPortKind::Value;
  output.value_type = KX_VALUE_VECTOR;
  signature.outputs.push_back(output);
  return signature;
}

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

void RegisterHarnessNodes()
{
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  for (int slot = 0; slot < 16; ++slot) {
    const int captured = slot;
    registry.RegisterType("B64Sink" + std::to_string(slot),
                          "B64Sink" + std::to_string(slot),
                          SinkSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestSinkNode(captured));
                          });
  }
  for (int slot = 0; slot < 12; ++slot) {
    const int captured = slot;
    registry.RegisterType("B64Int" + std::to_string(slot),
                          "B64Int" + std::to_string(slot),
                          IntSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestIntNode(captured));
                          });
    registry.RegisterType("B64List" + std::to_string(slot),
                          "B64List" + std::to_string(slot),
                          ListSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestListNode(captured));
                          });
    registry.RegisterType("B64Gate" + std::to_string(slot),
                          "B64Gate" + std::to_string(slot),
                          GateSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestGateNode(captured));
                          });
  }
  for (int slot = 0; slot < 12; ++slot) {
    const int captured = slot;
    registry.RegisterType("B64Vector" + std::to_string(slot),
                          "B64Vector" + std::to_string(slot),
                          VectorSignature(),
                          [captured]() {
                            return std::unique_ptr<KX_GameNodeRuntime>(new TestVectorNode(captured));
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

int AddSink(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B64Sink" + std::to_string(slot), name);
  return index;
}

int AddInt(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B64Int" + std::to_string(slot), name);
  return index;
}

int AddList(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B64List" + std::to_string(slot), name);
  return index;
}

int AddVector(GraphIR &ir, const std::string &name, int slot, float x, float y, float z)
{
  g_vectors[slot][0] = x;
  g_vectors[slot][1] = y;
  g_vectors[slot][2] = z;
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B64Vector" + std::to_string(slot), name);
  return index;
}

int AddGate(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B64Gate" + std::to_string(slot), name);
  return index;
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

const KX_ValueList *AsListOf(const KX_RuntimeValue *value)
{
  return value != nullptr ? static_cast<const KX_ValueList *>(value->AsList()) : nullptr;
}

KX_GraphInstance *BuildInstance(GraphIR &ir,
                                KX_MockInputAdapter &input,
                                KX_MockUIAdapter &ui,
                                KX_MockFileAdapter &file)
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
  instance->GetContext()->SetAdapters(&input,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      &ui,
                                      &file,
                                      nullptr);
  return instance;
}

/* The mock adapter derives hover/press events in BeginFrame, outside the graph. */
void Frame(KX_GraphInstance &instance, KX_MockUIAdapter &ui, float dt, float mx, float my, bool down)
{
  ui.BeginFrame(dt, mx, my, down);
  instance.Tick(dt);
}

/* Last recorded attribute value for `handle` and `key`, NaN when absent. */
float LastAttr(const KX_MockUIAdapter &ui, int handle, const std::string &key)
{
  for (auto it = ui.attribute_calls.rbegin(); it != ui.attribute_calls.rend(); ++it) {
    if (it->handle == handle && it->key == key) {
      return it->value.AsFloat();
    }
  }
  return std::nanf("");
}

/* Last recorded vector attribute (x/y) for `handle` and `key`. */
bool LastVector2(const KX_MockUIAdapter &ui, int handle, const std::string &key, float out[2])
{
  for (auto it = ui.attribute_calls.rbegin(); it != ui.attribute_calls.rend(); ++it) {
    if (it->handle == handle && it->key == key) {
      float vector[3] = {0.0f, 0.0f, 0.0f};
      it->value.VectorTo(vector);
      out[0] = vector[0];
      out[1] = vector[1];
      return true;
    }
  }
  return false;
}

int CanvasHandle(const KX_MockUIAdapter &ui, int index)
{
  int seen = 0;
  for (const KX_UIElement &element : ui.elements) {
    if (element.type == KX_UIElementType::Canvas) {
      if (seen == index) {
        return element.handle;
      }
      ++seen;
    }
  }
  return -1;
}

int CanvasHandleByName(const KX_MockUIAdapter &ui, const std::string &name)
{
  for (size_t i = 0; i < ui.canvas_names.size(); ++i) {
    if (ui.canvas_names[i] == name) {
      return CanvasHandle(ui, static_cast<int>(i));
    }
  }
  return -1;
}

int CountLabels(const KX_MockUIAdapter &ui, int canvas)
{
  int count = 0;
  for (const KX_UIElement &element : ui.elements) {
    if (element.canvas == canvas && element.type == KX_UIElementType::Label) {
      ++count;
    }
  }
  return count;
}

int FindSlider(const KX_MockUIAdapter &ui, int canvas)
{
  for (const KX_UIElement &element : ui.elements) {
    if (element.canvas == canvas && element.type == KX_UIElementType::Slider) {
      return element.handle;
    }
  }
  return -1;
}

/* --------------------------------------------------------------- UI graph */

struct UiGraph {
  int scroll = 0;
  int list_lit = 0;
  int list_virt = 0;
  int tab = 0;
  int anchor = 0;
  int stack_a = 0;
  int stack_b = 0;
  int stack_pop = 0;
};

GraphIR BuildUiGraph(UiGraph *out)
{
  UiGraph ids;
  GraphIR ir;
  g_lists[0] = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta"};
  g_lists[1] = {"One", "Two", "Three"};

  const int parent = AddInt(ir, "parent_ui", 0);
  g_ints[0] = -1;
  const int pos0 = AddVector(ir, "pos_ui", 0, 0.0f, 0.0f, 0.0f);
  const int view_size = AddVector(ir, "size_ui", 1, 200.0f, 48.0f, 0.0f);
  const int scroll_pos = AddVector(ir, "pos_scroll", 4, 0.0f, 200.0f, 0.0f);
  const int scroll_size = AddVector(ir, "size_scroll", 5, 200.0f, 100.0f, 0.0f);
  const int items = AddList(ir, "items", 0);
  const int tab_names = AddList(ir, "tab_names", 1);
  const int gate_list_clear = AddGate(ir, "gate_list_clear", 7);

  ids.scroll = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateScrollView", "scroll");
  ir.nodes.back().config_json = "{\"direction\": \"vertical\"}";
  Connect(ir, parent, 0, ids.scroll, 1);
  Connect(ir, scroll_pos, 0, ids.scroll, 2);
  Connect(ir, scroll_size, 0, ids.scroll, 3);

  ids.list_lit = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateListView", "list_litera");
  ir.nodes.back().config_json = "{\"item_height\": 24, \"virtualize\": false}";
  Connect(ir, parent, 0, ids.list_lit, 1);
  Connect(ir, pos0, 0, ids.list_lit, 2);
  Connect(ir, view_size, 0, ids.list_lit, 3);
  Connect(ir, items, 0, ids.list_lit, 4);
  Connect(ir, gate_list_clear, 0, ids.list_lit, 5);

  ids.list_virt = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateListView", "list_virt");
  ir.nodes.back().config_json = "{\"item_height\": 24, \"virtualize\": true}";
  Connect(ir, parent, 0, ids.list_virt, 1);
  Connect(ir, pos0, 0, ids.list_virt, 2);
  Connect(ir, view_size, 0, ids.list_virt, 3);
  Connect(ir, items, 0, ids.list_virt, 4);
  Connect(ir, gate_list_clear, 0, ids.list_virt, 5);

  ids.tab = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "CreateTabView", "tab");
  Connect(ir, parent, 0, ids.tab, 1);
  Connect(ir, pos0, 0, ids.tab, 2);
  Connect(ir, AddVector(ir, "tab_size", 3, 300.0f, 40.0f, 0.0f), 0, ids.tab, 3);
  Connect(ir, tab_names, 0, ids.tab, 4);

  g_ints[1] = 777;
  ids.anchor = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenAnchor", "anchor");
  ir.nodes.back().config_json = "{\"anchor\": \"top_right\"}";
  Connect(ir, AddInt(ir, "anchor_widget", 1), 0, ids.anchor, 1);
  const int resize_sink = AddSink(ir, "sink_resize", 8);
  Connect(ir, ids.anchor, 0, resize_sink, 0);

  const int gate_a = AddGate(ir, "gate_push_a", 0);
  const int gate_b = AddGate(ir, "gate_push_b", 1);
  const int gate_pop = AddGate(ir, "gate_pop", 2);
  ids.stack_a = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenStack", "stack_a");
  ir.nodes.back().config_json = "{\"mode\": \"push\"}";
  Connect(ir, gate_a, 0, ids.stack_a, 0);
  Connect(ir, AddString(ir, "screen_a", "ScreenA"), 0, ids.stack_a, 1);

  ids.stack_b = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenStack", "stack_b");
  ir.nodes.back().config_json = "{\"mode\": \"push\"}";
  Connect(ir, gate_b, 0, ids.stack_b, 0);
  Connect(ir, AddString(ir, "screen_b", "ScreenB"), 0, ids.stack_b, 1);

  ids.stack_pop = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenStack", "stack_pop");
  ir.nodes.back().config_json = "{\"mode\": \"pop\"}";
  Connect(ir, gate_pop, 0, ids.stack_pop, 0);
  Connect(ir, AddString(ir, "screen_pop", "ScreenB"), 0, ids.stack_pop, 1);

  *out = ids;
  return ir;
}

void CheckScrollView(KX_GraphInstance &instance, const UiGraph &ids, KX_MockUIAdapter &ui)
{
  const KX_RuntimeValue *view_value = ReadOutput(instance, ids.scroll, 1);
  const KX_RuntimeValue *content_value = ReadOutput(instance, ids.scroll, 2);
  const int view = view_value != nullptr ? view_value->AsInt() : -1;
  const int content = content_value != nullptr ? content_value->AsInt() : -1;
  Check(view >= 0 && content >= 0, "CreateScrollView produced view/content handles",
        static_cast<double>(view), static_cast<double>(content));
  if (view < 0 || content < 0) {
    return;
  }

  /* The content starts at view size; the scroll domain grows it afterwards. */
  ui.SetAttribute(content, "size", KX_RuntimeValue::Vector(200.0f, 300.0f, 0.0f));
  const int bar = FindSlider(ui, view);
  Check(bar >= 0, "CreateScrollView created a scrollbar slider");

  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  KX_UIElement *bar_element = ui.Find(bar);
  if (bar_element == nullptr) {
    Check(false, "scrollbar element resolvable");
    return;
  }
  Check(bar_element->visible, "scrollbar visible when the content overflows");

  /* Drag the bar from the bottom to the middle using real mouse events. */
  Frame(instance, ui, 0.016f, 194.0f, 300.0f, true);
  Frame(instance, ui, 0.016f, 194.0f, 250.0f, true);
  Frame(instance, ui, 0.016f, 194.0f, 250.0f, false);
  KX_UIElement *content_element = ui.Find(content);
  Check(content_element != nullptr && std::fabs(content_element->position[1] - 100.0f) < 0.5f,
        "scroll drag moves the content by half the extent",
        content_element != nullptr ? content_element->position[1] : 0.0, 100.0);

  /* Out of range slider values clamp: content can never leave the view. */
  bar_element->value = 1.5f;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  Check(content_element != nullptr && std::fabs(content_element->position[1] - 0.0f) < 0.5f,
        "scroll offset clamped to the content extent",
        content_element != nullptr ? content_element->position[1] : 0.0, 0.0);

  bar_element->value = 0.0f;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  Check(content_element != nullptr && std::fabs(content_element->position[1] - 200.0f) < 0.5f,
        "scroll offset clamped at zero",
        content_element != nullptr ? content_element->position[1] : 0.0, 200.0);

  Check(ui.Find(view) != nullptr && ui.Find(view)->clip, "scroll view sets clip on the view");
}

void CheckLists(KX_GraphInstance &instance,
                const UiGraph &ids,
                KX_MockUIAdapter &ui,
                int *lit_view,
                int *virt_view)
{
  const KX_RuntimeValue *lit_value = ReadOutput(instance, ids.list_lit, 1);
  const KX_RuntimeValue *virt_value = ReadOutput(instance, ids.list_virt, 1);
  const int lit = lit_value != nullptr ? lit_value->AsInt() : -1;
  const int virt = virt_value != nullptr ? virt_value->AsInt() : -1;
  *lit_view = lit;
  *virt_view = virt;
  Check(lit >= 0 && virt >= 0, "CreateListView created both views");

  const int lit_labels = CountLabels(ui, lit);
  const int virt_labels = CountLabels(ui, virt);
  Check(lit_labels == 6, "virtualize=false creates every item label",
        static_cast<double>(lit_labels), 6.0);
  Check(virt_labels == 3, "virtualize=true creates only visible slots",
        static_cast<double>(virt_labels), 3.0);

  /* Both lists scroll to the same offset; a click then has to agree. */
  const int lit_bar = FindSlider(ui, lit);
  const int virt_bar = FindSlider(ui, virt);
  Check(lit_bar >= 0 && virt_bar >= 0, "both lists own a scrollbar");
  if (lit_bar >= 0) {
    ui.Find(lit_bar)->value = 0.5f;
  }
  if (virt_bar >= 0) {
    ui.Find(virt_bar)->value = 0.5f;
  }

  Frame(instance, ui, 0.016f, 100.0f, 12.0f, false);
  Frame(instance, ui, 0.016f, 100.0f, 24.0f, true);

  const KX_RuntimeValue *lit_click = ReadOutput(instance, ids.list_lit, 2);
  const KX_RuntimeValue *virt_click = ReadOutput(instance, ids.list_virt, 2);
  const int lit_index = lit_click != nullptr ? lit_click->AsInt() : -100;
  const int virt_index = virt_click != nullptr ? virt_click->AsInt() : -100;
  Check(lit_index == 3 && virt_index == lit_index,
        "item click index identical for virtualize true/false",
        lit_index,
        3.0);
  Frame(instance, ui, 0.016f, 100.0f, 24.0f, false);

  /* Selection is latched: it must survive later frames without a new click. */
  for (int i = 0; i < 3; ++i) {
    Frame(instance, ui, 0.016f, 100.0f, 24.0f, false);
  }
  const KX_RuntimeValue *lit_hold = ReadOutput(instance, ids.list_lit, 2);
  const KX_RuntimeValue *virt_hold = ReadOutput(instance, ids.list_virt, 2);
  Check(lit_hold != nullptr && lit_hold->AsInt() == 3,
        "selection stays latched after 3 idle frames",
        lit_hold != nullptr ? lit_hold->AsInt() : -100.0, 3.0);
  Check(virt_hold != nullptr && virt_hold->AsInt() == 3,
        "virtual list latches the same index after 3 idle frames",
        virt_hold != nullptr ? virt_hold->AsInt() : -100.0, 3.0);

  /* A new click replaces the latched index (scroll back to the first row). */
  if (lit_bar >= 0) {
    ui.Find(lit_bar)->value = 0.0f;
  }
  if (virt_bar >= 0) {
    ui.Find(virt_bar)->value = 0.0f;
  }
  Frame(instance, ui, 0.016f, 100.0f, 12.0f, false);
  Frame(instance, ui, 0.016f, 100.0f, 24.0f, true);
  const KX_RuntimeValue *lit_switch = ReadOutput(instance, ids.list_lit, 2);
  Check(lit_switch != nullptr && lit_switch->AsInt() == 1,
        "a later click updates the latched index",
        lit_switch != nullptr ? lit_switch->AsInt() : -100.0, 1.0);

  /* Clear is an explicit edge-triggered reset, not a per-frame wipe. */
  g_gates[7] = true;
  Frame(instance, ui, 0.016f, 100.0f, 24.0f, false);
  g_gates[7] = false;
  Frame(instance, ui, 0.016f, 100.0f, 24.0f, false);
  const KX_RuntimeValue *cleared = ReadOutput(instance, ids.list_lit, 2);
  Check(cleared != nullptr && cleared->AsInt() == -1,
        "Clear resets the selection to -1",
        cleared != nullptr ? cleared->AsInt() : 100.0, -1.0);
  for (int i = 0; i < 3; ++i) {
    Frame(instance, ui, 0.016f, 100.0f, 24.0f, false);
  }
  const KX_RuntimeValue *still_clear = ReadOutput(instance, ids.list_lit, 2);
  Check(still_clear != nullptr && still_clear->AsInt() == -1,
        "the cleared selection stays -1 on later frames",
        still_clear != nullptr ? still_clear->AsInt() : 100.0, -1.0);
}

void CheckTab(KX_GraphInstance &instance, const UiGraph &ids, KX_MockUIAdapter &ui)
{
  const KX_RuntimeValue *view_value = ReadOutput(instance, ids.tab, 1);
  const int view = view_value != nullptr ? view_value->AsInt() : -1;
  Check(view >= 0, "CreateTabView created its view");
  Frame(instance, ui, 0.016f, 150.0f, 20.0f, false);
  Frame(instance, ui, 0.016f, 150.0f, 20.0f, true);
  Frame(instance, ui, 0.016f, 150.0f, 20.0f, false);
  const KX_RuntimeValue *current = ReadOutput(instance, ids.tab, 2);
  Check(current != nullptr && current->AsInt() == 1, "tab click switches to index 1",
        current != nullptr ? current->AsInt() : -1.0, 1.0);
  Check(ui.create_button_calls == 3, "CreateTabView created one button per tab",
        static_cast<double>(ui.create_button_calls), 3.0);
}

void CheckAnchor(KX_GraphInstance &instance, const UiGraph &ids, KX_MockUIAdapter &ui)
{
  ui.DrawScreen(1280, 720);
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  const KX_RuntimeValue *width = ReadOutput(instance, ids.anchor, 1);
  const KX_RuntimeValue *height = ReadOutput(instance, ids.anchor, 2);
  Check(width != nullptr && width->AsFloat() == 1280.0f, "anchor reports screen width",
        width != nullptr ? width->AsFloat() : 0.0, 1280.0);
  Check(height != nullptr && height->AsFloat() == 720.0f, "anchor reports screen height",
        height != nullptr ? height->AsFloat() : 0.0, 720.0);

  Check(g_sink_hits[8] == 0, "anchor does not emit OnResize on the first poll");
  ui.DrawScreen(1920, 1080);
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  Check(g_sink_hits[8] == 1, "anchor emits OnResize once after a resize",
        static_cast<double>(g_sink_hits[8]), 1.0);
  const float anchor_x = LastAttr(ui, 777, "anchor_x");
  Check(std::fabs(anchor_x - 1.0f) < 0.001f, "top_right anchor_x is 1", anchor_x, 1.0);
}

void CheckScreenStack(KX_GraphInstance &instance,
                      const UiGraph &ids,
                      KX_MockUIAdapter &ui)
{
  (void)ids;
  const uint32_t destroy_before = ui.destroy_calls;

  g_gates[0] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[0] = false;
  Check(ui.canvas_names.size() == 1 && ui.canvas_names[0] == "ScreenA",
        "push creates the ScreenA canvas");
  const int handle_a = CanvasHandle(ui, 0);
  Check(ui.Find(handle_a) != nullptr && ui.Find(handle_a)->visible,
        "ScreenA visible after push");

  g_gates[1] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[1] = false;
  Check(ui.canvas_names.size() == 2 && ui.canvas_names[1] == "ScreenB",
        "push creates the ScreenB canvas");
  const int handle_b = CanvasHandle(ui, 1);
  Check(ui.Find(handle_a) != nullptr && !ui.Find(handle_a)->visible,
        "push hides the previous screen");
  Check(ui.Find(handle_b) != nullptr && ui.Find(handle_b)->visible,
        "ScreenB visible after push");

  g_gates[2] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[2] = false;
  Check(ui.Find(handle_a) != nullptr && ui.Find(handle_a)->visible,
        "pop makes the previous screen visible again");
  Check(ui.Find(handle_b) != nullptr && !ui.Find(handle_b)->visible,
        "pop hides the popped screen");
  Check(ui.destroy_calls == destroy_before,
        "screen stack never destroys canvases (handles stay valid)",
        static_cast<double>(ui.destroy_calls - destroy_before),
        0.0);

  /* Re-push must reuse the existing canvas instead of allocating a new one. */
  g_gates[0] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[0] = false;
  Check(ui.canvas_names.size() == 2 && ui.create_canvas_calls == 2,
        "re-push does not create another canvas",
        static_cast<double>(ui.canvas_names.size()), 2.0);
  Check(ui.Find(handle_a) != nullptr && ui.Find(handle_a)->visible,
        "re-push reuses the original handle");
}

/* -------------------------------------- per-instance stack / hot reload */

struct IndepStack {
  int push_a = 0;
  int push_b = 0;
  int pop = 0;
};

GraphIR BuildIndepStackGraph(IndepStack *out, const std::string &screen_a,
                             const std::string &screen_b,
                             int gate_push_a, int gate_push_b, int gate_pop)
{
  GraphIR ir;
  const int gate_a = AddGate(ir, "gate_push_a", gate_push_a);
  const int gate_b = AddGate(ir, "gate_push_b", gate_push_b);
  const int gate_p = AddGate(ir, "gate_pop", gate_pop);

  out->push_a = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenStack", "push_a");
  ir.nodes.back().config_json = "{\"mode\": \"push\"}";
  Connect(ir, gate_a, 0, out->push_a, 0);
  Connect(ir, AddString(ir, "screen_a", screen_a), 0, out->push_a, 1);

  out->push_b = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenStack", "push_b");
  ir.nodes.back().config_json = "{\"mode\": \"push\"}";
  Connect(ir, gate_b, 0, out->push_b, 0);
  Connect(ir, AddString(ir, "screen_b", screen_b), 0, out->push_b, 1);

  out->pop = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIScreenStack", "pop");
  ir.nodes.back().config_json = "{\"mode\": \"pop\"}";
  Connect(ir, gate_p, 0, out->pop, 0);
  /* pop needs a non-empty name to pass the entry check; it hides the top. */
  Connect(ir, AddString(ir, "pop_name", screen_a), 0, out->pop, 1);

  return ir;
}

void CheckStackIsolation(KX_MockUIAdapter &ui, KX_MockInputAdapter &input,
                         KX_MockFileAdapter &file)
{
  IndepStack ids_a, ids_b;
  GraphIR ir_a = BuildIndepStackGraph(&ids_a, "IndA1", "IndA2", 3, 4, 5);
  GraphIR ir_b = BuildIndepStackGraph(&ids_b, "IndB1", "IndB2", 8, 9, 10);
  KX_GraphInstance *inst_a = BuildInstance(ir_a, input, ui, file);
  KX_GraphInstance *inst_b = BuildInstance(ir_b, input, ui, file);
  Check(inst_a != nullptr && inst_b != nullptr,
        "both stack graphs compile", inst_a != nullptr && inst_b != nullptr, 1.0);
  if (inst_a == nullptr || inst_b == nullptr) {
    delete inst_a;
    delete inst_b;
    return;
  }

  g_gates[3] = true;
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[3] = false;
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  const int handle_a1 = CanvasHandleByName(ui, "IndA1");
  Check(handle_a1 >= 0 && ui.Find(handle_a1)->visible,
        "instance A push shows its first screen");

  /* B pushes while A holds a screen: A must be untouched. */
  g_gates[8] = true;
  Frame(*inst_b, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[8] = false;
  Frame(*inst_b, ui, 0.016f, 0.0f, 0.0f, false);
  const int handle_b1 = CanvasHandleByName(ui, "IndB1");
  Check(ui.Find(handle_b1) != nullptr && ui.Find(handle_b1)->visible,
        "instance B push shows its own screen");
  Check(ui.Find(handle_a1) != nullptr && ui.Find(handle_a1)->visible,
        "instance B push does not hide instance A's screen");

  /* B pops its own screen; A stays on top of its own stack. */
  g_gates[10] = true;
  Frame(*inst_b, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[10] = false;
  Frame(*inst_b, ui, 0.016f, 0.0f, 0.0f, false);
  Check(ui.Find(handle_b1) != nullptr && !ui.Find(handle_b1)->visible,
        "instance B pop hides only its own screen");
  Check(ui.Find(handle_a1) != nullptr && ui.Find(handle_a1)->visible,
        "instance A still shows its screen after B pops");

  /* A pushes a second screen, then the graph is rebuilt (hot reload path). */
  g_gates[4] = true;
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[4] = false;
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  const int handle_a2 = CanvasHandleByName(ui, "IndA2");
  Check(ui.Find(handle_a2) != nullptr && ui.Find(handle_a2)->visible &&
            ui.Find(handle_a1) != nullptr && !ui.Find(handle_a1)->visible,
        "instance A push shows its second screen and hides the first");

  const size_t canvases_before = ui.canvas_names.size();
  std::vector<std::string> warnings;
  Check(inst_a->Rebuild(ir_a, &warnings), "hot reload rebuild accepts the graph");
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  Check(ui.canvas_names.size() == canvases_before,
        "rebuild does not recreate canvases",
        static_cast<double>(ui.canvas_names.size()),
        static_cast<double>(canvases_before));
  Check(ui.Find(handle_a2) != nullptr && ui.Find(handle_a2)->visible &&
            ui.Find(handle_a1) != nullptr && !ui.Find(handle_a1)->visible,
        "rebuild keeps the stack (top still IndA2)");

  /* A pop after rebuild proves the depth survived, not just the top handle. */
  g_gates[5] = true;
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[5] = false;
  Frame(*inst_a, ui, 0.016f, 0.0f, 0.0f, false);
  Check(ui.Find(handle_a1) != nullptr && ui.Find(handle_a1)->visible &&
            ui.Find(handle_a2) != nullptr && !ui.Find(handle_a2)->visible,
        "pop after rebuild returns to the screen below");

  delete inst_a;
  delete inst_b;
}

/* --------------------------------------------------------- animation graph */

struct AnimGraph {
  int anim[4] = {0, 0, 0, 0};
  int cancel_anim = 0;
  int gate = 0;
  int done = 0;
};

GraphIR BuildAnimGraph(AnimGraph *out)
{
  AnimGraph ids;
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  const char *easings[4] = {"linear", "ease_in", "ease_out", "ease_in_out"};
  for (int index = 0; index < 4; ++index) {
    g_ints[2 + index] = 801 + index;
    ids.anim[index] = static_cast<int>(ir.nodes.size());
    MakeNode(ir, "UIAnimation", std::string("anim_") + easings[index]);
    ir.nodes.back().inputs[4].value_tag = KX_VALUE_FLOAT;
    ir.nodes.back().inputs[5].value_tag = KX_VALUE_FLOAT;
    ir.nodes.back().config_json = std::string("{\"easing\": \"") + easings[index] + "\"}";
    Connect(ir, 0, 0, ids.anim[index], 0);
    Connect(ir, AddInt(ir, std::string("widget_") + easings[index], 2 + index), 0, ids.anim[index], 1);
    Connect(ir, AddFloat(ir, std::string("duration_") + easings[index], 1.0f), 0, ids.anim[index], 2);
    Connect(ir, AddString(ir, std::string("property_") + easings[index], "opacity"), 0, ids.anim[index], 3);
    Connect(ir, AddFloat(ir, std::string("from_") + easings[index], 0.0f), 0, ids.anim[index], 4);
    Connect(ir, AddFloat(ir, std::string("to_") + easings[index], 100.0f), 0, ids.anim[index], 5);
  }

  /* A fifth animation is re-entered half way through to check cancel-on-entry. */
  ids.gate = AddGate(ir, "gate_anim", 3);
  ids.cancel_anim = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIAnimation", "anim_cancel");
  ir.nodes.back().inputs[4].value_tag = KX_VALUE_FLOAT;
  ir.nodes.back().inputs[5].value_tag = KX_VALUE_FLOAT;
  ir.nodes.back().config_json = "{\"easing\": \"linear\"}";
  Connect(ir, 0, 0, ids.cancel_anim, 0);
  Connect(ir, ids.gate, 0, ids.cancel_anim, 0);
  Connect(ir, AddInt(ir, "widget_cancel", 6), 0, ids.cancel_anim, 1);
  g_ints[6] = 805;
  Connect(ir, AddFloat(ir, "duration_cancel", 1.0f), 0, ids.cancel_anim, 2);
  Connect(ir, AddString(ir, "property_cancel", "opacity"), 0, ids.cancel_anim, 3);
  Connect(ir, AddFloat(ir, "from_cancel", 0.0f), 0, ids.cancel_anim, 4);
  Connect(ir, AddFloat(ir, "to_cancel", 100.0f), 0, ids.cancel_anim, 5);

  ids.done = AddSink(ir, "sink_anim_done", 9);
  Connect(ir, ids.anim[0], 0, ids.done, 0);
  *out = ids;
  return ir;
}

void CheckAnimations(KX_GraphInstance &instance, const AnimGraph &ids, KX_MockUIAdapter &ui)
{
  ui.DrawScreen(1280, 720);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  const float linear = LastAttr(ui, 801, "opacity");
  const float ease_in = LastAttr(ui, 802, "opacity");
  const float ease_out = LastAttr(ui, 803, "opacity");
  const float ease_in_out = LastAttr(ui, 804, "opacity");
  std::printf("anim t=0.25 linear=%g in=%g out=%g in_out=%g\n",
              linear,
              ease_in,
              ease_out,
              ease_in_out);
  Check(std::fabs(linear - 25.0f) < 0.01f, "linear mid value", linear, 25.0);
  Check(std::fabs(ease_in - 6.25f) < 0.01f, "ease_in mid value", ease_in, 6.25);
  Check(std::fabs(ease_out - 43.75f) < 0.01f, "ease_out mid value", ease_out, 43.75);
  Check(std::fabs(ease_in_out - 12.5f) < 0.01f, "ease_in_out mid value", ease_in_out, 12.5);
  Check(!(std::fabs(linear - ease_in) < 0.01f) && !(std::fabs(linear - ease_out) < 0.01f) &&
            !(std::fabs(ease_in - ease_in_out) < 0.01f),
        "four easings differ at the same duration/position");

  /* Re-entry after 0.75s must restart, not run a second animation in parallel. */
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  g_gates[3] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[3] = false;
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  const float after_reentry = LastAttr(ui, 805, "opacity");
  Check(std::fabs(after_reentry - 25.0f) < 0.01f, "re-entry cancels the running animation and restarts",
        after_reentry, 25.0);

  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  const float finished = LastAttr(ui, 801, "opacity");
  Check(std::fabs(finished - 100.0f) < 0.01f, "animation ends at the target value", finished, 100.0);
  Check(g_sink_hits[9] == 1, "OnDone fires exactly once",
        static_cast<double>(g_sink_hits[9]), 1.0);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false);
  Check(g_sink_hits[9] == 1, "OnDone does not repeat after completion",
        static_cast<double>(g_sink_hits[9]), 1.0);
}

/* ------------------------------------------- multi-property animation E2E */

struct MultiAnimGraph {
  int position = 0;
  int size = 0;
  int opacity = 0;
  int done_position = 0;
  int done_size = 0;
  int done_opacity = 0;
};

GraphIR BuildMultiAnimGraph(int widget, MultiAnimGraph *out)
{
  MultiAnimGraph ids;
  GraphIR ir;
  MakeNode(ir, "OnInit", "init");
  g_ints[7] = widget;
  const int widget_node = AddInt(ir, "widget_multi", 7);

  /* Position: 2 s, linear, (0,0) -> (200,200). */
  ids.position = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIAnimation", "anim_multi_position");
  ir.nodes.back().inputs[4].value_tag = KX_VALUE_VECTOR;
  ir.nodes.back().inputs[5].value_tag = KX_VALUE_VECTOR;
  ir.nodes.back().config_json = "{\"easing\": \"linear\"}";
  Connect(ir, 0, 0, ids.position, 0);
  Connect(ir, widget_node, 0, ids.position, 1);
  Connect(ir, AddFloat(ir, "multi_duration_position", 2.0f), 0, ids.position, 2);
  Connect(ir, AddString(ir, "multi_property_position", "position"), 0, ids.position, 3);
  Connect(ir, AddVector(ir, "multi_from_position", 2, 0.0f, 0.0f, 0.0f), 0, ids.position, 4);
  Connect(ir, AddVector(ir, "multi_to_position", 6, 200.0f, 200.0f, 0.0f), 0, ids.position, 5);

  /* Scale/size: 4 s, linear, (100,100) -> (500,500). */
  ids.size = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIAnimation", "anim_multi_size");
  ir.nodes.back().inputs[4].value_tag = KX_VALUE_VECTOR;
  ir.nodes.back().inputs[5].value_tag = KX_VALUE_VECTOR;
  ir.nodes.back().config_json = "{\"easing\": \"linear\"}";
  Connect(ir, 0, 0, ids.size, 0);
  Connect(ir, widget_node, 0, ids.size, 1);
  Connect(ir, AddFloat(ir, "multi_duration_size", 4.0f), 0, ids.size, 2);
  Connect(ir, AddString(ir, "multi_property_size", "size"), 0, ids.size, 3);
  Connect(ir, AddVector(ir, "multi_from_size", 7, 100.0f, 100.0f, 0.0f), 0, ids.size, 4);
  Connect(ir, AddVector(ir, "multi_to_size", 8, 500.0f, 500.0f, 0.0f), 0, ids.size, 5);

  /* Opacity: 1 s, ease_out, 0 -> 1. */
  ids.opacity = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "UIAnimation", "anim_multi_opacity");
  ir.nodes.back().inputs[4].value_tag = KX_VALUE_FLOAT;
  ir.nodes.back().inputs[5].value_tag = KX_VALUE_FLOAT;
  ir.nodes.back().config_json = "{\"easing\": \"ease_out\"}";
  Connect(ir, 0, 0, ids.opacity, 0);
  Connect(ir, widget_node, 0, ids.opacity, 1);
  Connect(ir, AddFloat(ir, "multi_duration_opacity", 1.0f), 0, ids.opacity, 2);
  Connect(ir, AddString(ir, "multi_property_opacity", "opacity"), 0, ids.opacity, 3);
  Connect(ir, AddFloat(ir, "multi_from_opacity", 0.0f), 0, ids.opacity, 4);
  Connect(ir, AddFloat(ir, "multi_to_opacity", 1.0f), 0, ids.opacity, 5);

  /* Slots 13..15 keep clear of the file graph's 10/11 sinks, which are
   * counted in the same global table later in this binary. */
  ids.done_position = AddSink(ir, "sink_multi_position", 13);
  ids.done_size = AddSink(ir, "sink_multi_size", 14);
  ids.done_opacity = AddSink(ir, "sink_multi_opacity", 15);
  Connect(ir, ids.position, 0, ids.done_position, 0);
  Connect(ir, ids.size, 0, ids.done_size, 0);
  Connect(ir, ids.opacity, 0, ids.done_opacity, 0);

  *out = ids;
  return ir;
}

void CheckMultiAnimations(int widget,
                          KX_GraphInstance &instance,
                          const MultiAnimGraph &ids,
                          KX_MockUIAdapter &ui)
{
  ui.DrawScreen(1280, 720);
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* trigger */
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* 0.25 s */
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* 0.50 s */

  float position[2] = {0.0f, 0.0f};
  float size[2] = {0.0f, 0.0f};
  Check(LastVector2(ui, widget, "position", position) &&
            std::fabs(position[0] - 50.0f) < 0.01f,
        "multi-anim position x at 0.5 s (2 s clock)",
        position[0],
        50.0);
  Check(std::fabs(position[1] - 50.0f) < 0.01f,
        "multi-anim position y at 0.5 s (2 s clock)",
        position[1],
        50.0);
  Check(LastVector2(ui, widget, "size", size) && std::fabs(size[0] - 150.0f) < 0.01f,
        "multi-anim size x at 0.5 s (4 s clock)",
        size[0],
        150.0);
  Check(std::fabs(size[1] - 150.0f) < 0.01f,
        "multi-anim size y at 0.5 s (4 s clock)",
        size[1],
        150.0);
  const float opacity_mid = LastAttr(ui, widget, "opacity");
  Check(std::fabs(opacity_mid - 0.75f) < 0.001f,
        "multi-anim opacity at 0.5 s follows its 1 s ease_out curve",
        opacity_mid,
        0.75);

  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* 0.75 s */
  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* 1.00 s */

  Check(LastVector2(ui, widget, "position", position) &&
            std::fabs(position[0] - 100.0f) < 0.01f,
        "multi-anim position x is halfway at 1.0 s",
        position[0],
        100.0);
  Check(std::fabs(position[1] - 100.0f) < 0.01f,
        "multi-anim position y is halfway at 1.0 s",
        position[1],
        100.0);
  Check(LastVector2(ui, widget, "size", size) && std::fabs(size[0] - 200.0f) < 0.01f,
        "multi-anim size x is one quarter at 1.0 s",
        size[0],
        200.0);
  Check(std::fabs(size[1] - 200.0f) < 0.01f,
        "multi-anim size y is one quarter at 1.0 s",
        size[1],
        200.0);
  const float opacity_done = LastAttr(ui, widget, "opacity");
  Check(std::fabs(opacity_done - 1.0f) < 0.001f,
        "multi-anim opacity completed at 1.0 s while others still run",
        opacity_done,
        1.0);

  Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* 1.25 s */
  Check(g_sink_hits[15] == 1, "opacity OnDone fires at its own 1 s finish",
        static_cast<double>(g_sink_hits[15]), 1.0);
  Check(g_sink_hits[14] == 0, "size OnDone has not fired at 1.25 s",
        static_cast<double>(g_sink_hits[14]), 0.0);

  for (int frame = 0; frame < 11; ++frame) {
    Frame(instance, ui, 0.25f, 0.0f, 0.0f, false); /* 4.00 s */
  }
  Check(LastVector2(ui, widget, "size", size) && std::fabs(size[0] - 500.0f) < 0.01f,
        "multi-anim size x reaches its 4 s target",
        size[0],
        500.0);
  Check(std::fabs(size[1] - 500.0f) < 0.01f,
        "multi-anim size y reaches its 4 s target",
        size[1],
        500.0);
  Check(LastVector2(ui, widget, "position", position) &&
            std::fabs(position[0] - 200.0f) < 0.01f,
        "multi-anim position x stays at its 2 s target",
        position[0],
        200.0);
  Check(std::fabs(position[1] - 200.0f) < 0.01f,
        "multi-anim position y stays at its 2 s target",
        position[1],
        200.0);
  Check(g_sink_hits[14] == 1, "size OnDone fires exactly once at 4 s",
        static_cast<double>(g_sink_hits[14]), 1.0);
}

/* -------------------------------------------------------------- file graph */

struct FileGraph {
  int save_binary = 0;
  int load_binary = 0;
  int slots = 0;
  int encrypt = 0;
  int decrypt = 0;
  int read_lines = 0;
  int read_escape = 0;
  int encrypt_aes = 0;
  int gate_save = 0;
  int gate_load = 0;
  int gate_encrypt = 0;
  int gate_decrypt = 0;
  int gate_lines = 0;
  int gate_escape = 0;
  int gate_aes = 0;
};

GraphIR BuildFileGraph(FileGraph *out)
{
  FileGraph ids;
  GraphIR ir;

  ids.gate_save = AddGate(ir, "gate_save", 0);
  ids.save_binary = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SaveGameBinary", "save_binary");
  Connect(ir, ids.gate_save, 0, ids.save_binary, 0);
  Connect(ir, AddString(ir, "slot_save", "slotA"), 0, ids.save_binary, 1);
  const int save_ok = AddSink(ir, "sink_save_ok", 0);
  const int save_fail = AddSink(ir, "sink_save_fail", 1);
  Connect(ir, ids.save_binary, 0, save_ok, 0);
  Connect(ir, ids.save_binary, 1, save_fail, 0);

  ids.gate_load = AddGate(ir, "gate_load", 1);
  ids.load_binary = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "LoadGame", "load_binary");
  ir.nodes.back().config_json = "{\"format\": \"binary\"}";
  Connect(ir, ids.gate_load, 0, ids.load_binary, 0);
  Connect(ir, AddString(ir, "slot_load", "slotA"), 0, ids.load_binary, 1);
  const int load_ok = AddSink(ir, "sink_load_ok", 2);
  const int load_fail = AddSink(ir, "sink_load_fail", 3);
  Connect(ir, ids.load_binary, 0, load_ok, 0);
  Connect(ir, ids.load_binary, 1, load_fail, 0);

  ids.slots = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SaveGameSlots", "slots");

  ids.gate_encrypt = AddGate(ir, "gate_encrypt", 2);
  ids.encrypt = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "EncryptData", "encrypt");
  ir.nodes.back().config_json = "{\"algorithm\": \"xor\"}";
  Connect(ir, ids.gate_encrypt, 0, ids.encrypt, 0);
  Connect(ir, AddString(ir, "plain", "hello world"), 0, ids.encrypt, 1);
  Connect(ir, AddString(ir, "key", "s3cr3t"), 0, ids.encrypt, 2);
  const int encrypt_done = AddSink(ir, "sink_encrypt", 4);
  Connect(ir, ids.encrypt, 0, encrypt_done, 0);

  ids.gate_decrypt = AddGate(ir, "gate_decrypt", 3);
  ids.decrypt = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "DecryptData", "decrypt");
  ir.nodes.back().config_json = "{\"algorithm\": \"xor\"}";
  Connect(ir, ids.gate_decrypt, 0, ids.decrypt, 0);
  Connect(ir, ids.encrypt, 1, ids.decrypt, 1);
  Connect(ir, AddString(ir, "key_dec", "s3cr3t"), 0, ids.decrypt, 2);
  const int decrypt_done = AddSink(ir, "sink_decrypt", 5);
  Connect(ir, ids.decrypt, 0, decrypt_done, 0);

  ids.gate_lines = AddGate(ir, "gate_lines", 4);
  ids.read_lines = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileReadLines", "read_lines");
  Connect(ir, ids.gate_lines, 0, ids.read_lines, 0);
  Connect(ir, AddString(ir, "lines_path", "save/crlf.txt"), 0, ids.read_lines, 1);
  const int lines_ok = AddSink(ir, "sink_lines_ok", 6);
  const int lines_fail = AddSink(ir, "sink_lines_fail", 7);
  Connect(ir, ids.read_lines, 0, lines_ok, 0);
  Connect(ir, ids.read_lines, 1, lines_fail, 0);

  ids.gate_escape = AddGate(ir, "gate_escape", 5);
  ids.read_escape = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "FileReadLines", "read_escape");
  Connect(ir, ids.gate_escape, 0, ids.read_escape, 0);
  Connect(ir, AddString(ir, "escape_path", "../etc/passwd"), 0, ids.read_escape, 1);
  const int escape_fail = AddSink(ir, "sink_escape_fail", 10);
  Connect(ir, ids.read_escape, 1, escape_fail, 0);

  ids.gate_aes = AddGate(ir, "gate_aes", 6);
  ids.encrypt_aes = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "EncryptData", "encrypt_aes");
  ir.nodes.back().config_json = "{\"algorithm\": \"aes\"}";
  Connect(ir, ids.gate_aes, 0, ids.encrypt_aes, 0);
  Connect(ir, AddString(ir, "aes_plain", "secret"), 0, ids.encrypt_aes, 1);
  Connect(ir, AddString(ir, "aes_key", "key"), 0, ids.encrypt_aes, 2);
  const int aes_done = AddSink(ir, "sink_aes", 11);
  Connect(ir, ids.encrypt_aes, 0, aes_done, 0);

  *out = ids;
  return ir;
}

void CheckFileNodes(KX_GraphInstance &instance,
                    const FileGraph &ids,
                    KX_MockUIAdapter &ui,
                    KX_MockFileAdapter &file)
{
  file.files["save:slotB"] = "{}";
  file.files["save/crlf.txt"] = "alpha\r\nbeta\r\n\r\ngamma\r\n";

  g_gates[0] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[0] = false;
  Check(g_sink_hits[0] == 1, "SaveGameBinary reports OnSuccess", static_cast<double>(g_sink_hits[0]), 1.0);
  Check(file.savebin_calls == 1, "SaveGameBinary reached the adapter",
        static_cast<double>(file.savebin_calls), 1.0);
  const auto stored = file.files.find(KX_MockBinaryFileKey("slotA"));
  Check(stored != file.files.end() && stored->second.size() > 16,
        "binary save stored a container blob");

  g_gates[1] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[1] = false;
  Check(g_sink_hits[2] == 1, "good binary save loads with OnSuccess",
        static_cast<double>(g_sink_hits[2]), 1.0);
  Check(file.loadbin_calls == 1, "LoadGame(binary) reached the adapter",
        static_cast<double>(file.loadbin_calls), 1.0);

  /* Corrupted blob: truncate and load again; must fail without crashing. */
  if (stored != file.files.end()) {
    stored->second.resize(stored->second.size() / 2);
  }
  g_gates[1] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[1] = false;
  Check(g_sink_hits[3] == 1, "truncated binary save reports OnFail",
        static_cast<double>(g_sink_hits[3]), 1.0);
  Check(g_sink_hits[2] == 1, "corrupt load does not report success",
        static_cast<double>(g_sink_hits[2]), 1.0);

  const KX_RuntimeValue *slots_value = ReadOutput(instance, ids.slots, 0);
  const KX_ValueList *slots = AsListOf(slots_value);
  Check(slots != nullptr && slots->size() == 2, "SaveGameSlots lists both slots",
        slots != nullptr ? static_cast<double>(slots->size()) : 0.0, 2.0);
  Check(slots != nullptr && std::string((*slots)[0].AsString()) == "slotA" &&
            std::string((*slots)[1].AsString()) == "slotB",
        "slot list is sorted");

  g_gates[2] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[2] = false;
  Check(g_sink_hits[4] == 1, "EncryptData reports OnDone", static_cast<double>(g_sink_hits[4]), 1.0);
  const KX_RuntimeValue *encrypted = ReadOutput(instance, ids.encrypt, 1);
  Check(encrypted != nullptr && std::string(encrypted->AsString()) != "hello world",
        "XOR encryption changed the payload");

  g_gates[3] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[3] = false;
  const KX_RuntimeValue *decrypted = ReadOutput(instance, ids.decrypt, 1);
  Check(g_sink_hits[5] == 1, "DecryptData reports OnDone", static_cast<double>(g_sink_hits[5]), 1.0);
  Check(decrypted != nullptr && std::string(decrypted->AsString()) == "hello world",
        "XOR round trip restores the plaintext");

  g_gates[4] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[4] = false;
  const KX_RuntimeValue *lines_value = ReadOutput(instance, ids.read_lines, 2);
  const KX_ValueList *lines = AsListOf(lines_value);
  Check(g_sink_hits[6] == 1, "FileReadLines reports OnSuccess", static_cast<double>(g_sink_hits[6]), 1.0);
  Check(lines != nullptr && lines->size() == 4, "CRLF file keeps the internal blank line",
        lines != nullptr ? static_cast<double>(lines->size()) : 0.0, 4.0);
  Check(lines != nullptr && std::string((*lines)[2].AsString()).empty(),
        "blank line inside the file stays an element");
  Check(lines != nullptr && std::string((*lines)[3].AsString()) == "gamma",
        "CR is stripped from the last line");

  g_gates[5] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[5] = false;
  Check(g_sink_hits[10] == 1, "FileReadLines rejects a .. path with OnFail",
        static_cast<double>(g_sink_hits[10]), 1.0);

  g_gates[6] = true;
  Frame(instance, ui, 0.016f, 0.0f, 0.0f, false);
  g_gates[6] = false;
  Check(g_sink_hits[11] == 0, "aes encryption takes the failure path (no OnDone)",
        static_cast<double>(g_sink_hits[11]), 0.0);
  const KX_RuntimeValue *aes_output = ReadOutput(instance, ids.encrypt_aes, 1);
  Check(aes_output == nullptr || aes_output->AsString() == nullptr ||
            std::string(aes_output->AsString()).empty(),
        "aes does not silently produce XOR output");
}

/* ------------------------------------------------- renderer virtualize unit */

void CheckRendererCulling()
{
  std::vector<KX_UIElement> elements;
  KX_UIElement canvas;
  canvas.type = KX_UIElementType::Canvas;
  canvas.handle = 1;
  canvas.canvas = -1;
  elements.push_back(canvas);

  KX_UIElement view;
  view.type = KX_UIElementType::Layout;
  view.handle = 2;
  view.canvas = 1;
  view.clip = true;
  view.position[0] = 0.0f;
  view.position[1] = 0.0f;
  view.size[0] = 200.0f;
  view.size[1] = 48.0f;
  elements.push_back(view);

  for (int index = 0; index < 6; ++index) {
    KX_UIElement item;
    item.type = KX_UIElementType::Label;
    item.handle = 10 + index;
    item.canvas = 2;
    item.virtualized = true;
    item.position[0] = 0.0f;
    item.position[1] = -36.0f + float(index) * 24.0f;
    item.size[0] = 200.0f;
    item.size[1] = 24.0f;
    elements.push_back(item);
  }

  /* Heap allocated: the renderer destructor talks to the GPU layer, which the
   * desktop harness does not initialise. */
  KX_UIRenderer *renderer = new KX_UIRenderer();
  renderer->Draw(elements, 1280, 720, false);
  Check(renderer->Stats().culled == 3,
        "renderer culls the three virtualized items outside the clip",
        static_cast<double>(renderer->Stats().culled),
        3.0);
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterHarnessNodes();

  KX_MockInputAdapter input;
  KX_MockUIAdapter ui;
  KX_MockFileAdapter file;

  /* --------------------------------------------------------------- UI E2E */
  {
    UiGraph ids;
    GraphIR ir = BuildUiGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, ui, file);
    if (instance == nullptr) {
      std::printf("FAIL UI graph did not compile\n");
      return 1;
    }
    ui.DrawScreen(1280, 720);
    Frame(*instance, ui, 0.016f, 0.0f, 0.0f, false);
    CheckScrollView(*instance, ids, ui);
    int lit_view = -1;
    int virt_view = -1;
    CheckLists(*instance, ids, ui, &lit_view, &virt_view);
    CheckTab(*instance, ids, ui);
    CheckAnchor(*instance, ids, ui);
    CheckScreenStack(*instance, ids, ui);
    delete instance;
  }

  /* ------------------- per-instance stack isolation and hot reload */
  {
    CheckStackIsolation(ui, input, file);
  }

  /* ------------------------------------------------------- animation E2E */
  {
    AnimGraph ids;
    GraphIR ir = BuildAnimGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, ui, file);
    if (instance == nullptr) {
      std::printf("FAIL animation graph did not compile\n");
      return 1;
    }
    CheckAnimations(*instance, ids, ui);
    delete instance;
  }

  /* ------------------------------------- multi-property animation E2E */
  {
    const int multi_canvas = ui.CreateCanvas("AnimMulti");
    const int multi_widget = ui.CreateLabel(multi_canvas, "Multi");
    MultiAnimGraph ids;
    GraphIR ir = BuildMultiAnimGraph(multi_widget, &ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, ui, file);
    if (instance == nullptr) {
      std::printf("FAIL multi-animation graph did not compile\n");
      return 1;
    }
    CheckMultiAnimations(multi_widget, *instance, ids, ui);
    delete instance;
  }

  /* ---------------------------------------------------------- file E2E */
  {
    FileGraph ids;
    GraphIR ir = BuildFileGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, ui, file);
    if (instance == nullptr) {
      std::printf("FAIL file graph did not compile\n");
      return 1;
    }
    file.Reset();
    CheckFileNodes(*instance, ids, ui, file);
    delete instance;
  }

  CheckRendererCulling();

  /* ------------------------------------------------ debug trace snapshot */
  {
    std::remove("/tmp/b64_trace.json");
    KX_DebugTrace::SetSnapshotPath("/tmp/b64_trace.json");
    KX_DebugTrace::SetEnabled(true);
    AnimGraph ids;
    GraphIR ir = BuildAnimGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, input, ui, file);
    if (instance != nullptr) {
      ui.DrawScreen(1280, 720);
      Frame(*instance, ui, 0.25f, 0.0f, 0.0f, false);
      delete instance;
    }
    KX_DebugTrace::SetEnabled(false);
    KX_DebugTrace::SetSnapshotPath("");

    std::FILE *trace = std::fopen("/tmp/b64_trace.json", "r");
    std::string json;
    if (trace != nullptr) {
      char buffer[4096];
      size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, trace);
      buffer[read] = '\0';
      json = buffer;
      std::fclose(trace);
    }
    Check(!json.empty() && json.find("\"nodes\"") != std::string::npos &&
              json.find("\"activations\"") != std::string::npos,
          "debug trace snapshot has node names and activation counts");
    std::printf("trace snapshot: %s\n", json.c_str());
  }

  std::printf("%s total failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
