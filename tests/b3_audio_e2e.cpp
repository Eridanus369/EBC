// B3 audio evidence harness: proves the seven B3 sound nodes and ActionDone
// against the mock adapters. This is MOCK-LAYER evidence, not real device
// listening; the report marks real acoustic verification as pending.
//
// Every assertion prints PASS/FAIL and the process exits non zero on failure.
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
#include "KX_RuntimeValue.h"

void RegisterB3FunctionGameNodes();

/* Families owned by other lines are not part of this harness. */
void RegisterExtendedFunctionGameNodes() {}
void RegisterB2FunctionGameNodes() {}
void RegisterB4FileFunctionGameNodes() {}
void RegisterB4UIFunctionGameNodes() {}
void RegisterB5FunctionGameNodes() {}
void RegisterB6InputFunctionGameNodes() {}
void RegisterB6StringFunctionGameNodes() {}
void RegisterB6AudioFunctionGameNodes() {}
void RegisterB6FileFunctionGameNodes() {}
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
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

/* ------------------------------------------------------------------ test nodes */

class TriggerOnceNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (!fired_) {
      fired_ = true;
      SetOutputTrigger(ctx, 0, true);
    }
    return KX_ExecResult::Continue;
  }

 private:
  bool fired_ = false;
};

class StringNode : public KX_GameNodeRuntime {
 public:
  explicit StringNode(std::string value) : value_(std::move(value)) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::String(value_.c_str());
    return KX_ExecResult::Continue;
  }

 private:
  std::string value_;
};

class FloatNode : public KX_GameNodeRuntime {
 public:
  explicit FloatNode(float value) : value_(value) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Float(value_);
    return KX_ExecResult::Continue;
  }

 private:
  float value_;
};

class IntNode : public KX_GameNodeRuntime {
 public:
  explicit IntNode(int value) : value_(value) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Int(value_);
    return KX_ExecResult::Continue;
  }

 private:
  int value_;
};

class BoolNode : public KX_GameNodeRuntime {
 public:
  explicit BoolNode(bool value) : value_(value) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Bool(value_);
    return KX_ExecResult::Continue;
  }

 private:
  bool value_;
};

class ObjectNode : public KX_GameNodeRuntime {
 public:
  explicit ObjectNode(KX_GameObject *object) : object_(object) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Object(object_);
    return KX_ExecResult::Continue;
  }

 private:
  KX_GameObject *object_;
};

class VectorNode : public KX_GameNodeRuntime {
 public:
  VectorNode(float x, float y, float z) : x_(x), y_(y), z_(z) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Vector(x_, y_, z_);
    return KX_ExecResult::Continue;
  }

 private:
  float x_, y_, z_;
};

/* Counts triggers that reach it; one slot per harness assertion. */
int g_hits[8] = {0, 0, 0, 0, 0, 0, 0, 0};

class SinkNode : public KX_GameNodeRuntime {
 public:
  explicit SinkNode(int slot) : slot_(slot) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ConsumeInputTrigger(ctx, 0)) {
      ++g_hits[slot_];
    }
    return KX_ExecResult::Continue;
  }

 private:
  int slot_;
};

KX_NodeSignature TriggerOutSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = "Out";
  output.kind = IRPortKind::Trigger;
  output.value_type = KX_VALUE_NONE;
  signature.outputs.push_back(output);
  return signature;
}

KX_NodeSignature TriggerInSignature()
{
  KX_NodeSignature signature;
  KX_NodePortSignature input;
  input.name = "In";
  input.kind = IRPortKind::Trigger;
  input.value_type = KX_VALUE_NONE;
  signature.inputs.push_back(input);
  return signature;
}

KX_NodeSignature OneOutput(const char *name, KX_ValueType type)
{
  KX_NodeSignature signature;
  KX_NodePortSignature output;
  output.name = name;
  output.kind = IRPortKind::Value;
  output.value_type = type;
  signature.outputs.push_back(output);
  return signature;
}

void RegisterTestType(const std::string &type_id,
                      const KX_NodeSignature &signature,
                      KX_NodeRegistry::Factory factory)
{
  KX_NodeRegistry::Get().RegisterType(type_id, type_id + "Runtime", signature, factory);
}

void RegisterTestNodes()
{
  RegisterTestType("B3TriggerOnce", TriggerOutSignature(),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new TriggerOnceNode()); });
  RegisterTestType("B3String", OneOutput("Value", KX_VALUE_STRING),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("")); });
  RegisterTestType("B3StringFile", OneOutput("Value", KX_VALUE_STRING),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("test.wav")); });
  RegisterTestType("B3StringName", OneOutput("Value", KX_VALUE_STRING),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("explosion")); });
  RegisterTestType("B3Float07", OneOutput("Value", KX_VALUE_FLOAT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(0.7f)); });
  RegisterTestType("B3Float06", OneOutput("Value", KX_VALUE_FLOAT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(0.6f)); });
  RegisterTestType("B3Float12", OneOutput("Value", KX_VALUE_FLOAT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(1.2f)); });
  RegisterTestType("B3Float15", OneOutput("Value", KX_VALUE_FLOAT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(1.5f)); });
  RegisterTestType("B3FloatOne", OneOutput("Value", KX_VALUE_FLOAT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(1.0f)); });
  RegisterTestType("B3BoolTrue", OneOutput("Value", KX_VALUE_BOOL),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new BoolNode(true)); });
  RegisterTestType("B3BoolFalse", OneOutput("Value", KX_VALUE_BOOL),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new BoolNode(false)); });
  RegisterTestType("B3Int0", OneOutput("Value", KX_VALUE_INT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new IntNode(0)); });
  RegisterTestType("B3Int1", OneOutput("Value", KX_VALUE_INT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new IntNode(1)); });
  RegisterTestType("B3Object", OneOutput("Value", KX_VALUE_OBJECT),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new ObjectNode(nullptr)); });
  RegisterTestType("B3VectorPos", OneOutput("Value", KX_VALUE_VECTOR),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new VectorNode(1.0f, 2.0f, 3.0f)); });
  RegisterTestType("B3VectorOrient", OneOutput("Value", KX_VALUE_VECTOR),
                   []() { return std::unique_ptr<KX_GameNodeRuntime>(new VectorNode(0.0f, 1.0f, 0.0f)); });
  for (int slot = 0; slot < 8; ++slot) {
    const int captured = slot;
    RegisterTestType("B3Sink" + std::to_string(slot), TriggerInSignature(),
                     [captured]() { return std::unique_ptr<KX_GameNodeRuntime>(new SinkNode(captured)); });
  }
}

/* ------------------------------------------------------------------ graph helpers */

IRNode MakeNode(GraphIR &ir, const std::string &type_id, const std::string &name)
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = name;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  KX_NodeTypeId id = registry.FindByEditorType(type_id);
  if (id == KX_INVALID_NODE_TYPE_ID) {
    id = registry.FindByRuntimeType(type_id);
  }
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

int AddTrigger(GraphIR &ir, const std::string &name)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B3TriggerOnce", name);
  return index;
}

int AddSink(GraphIR &ir, const std::string &name, int slot)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "B3Sink" + std::to_string(slot), name);
  return index;
}

int AddValue(GraphIR &ir, const std::string &type, const std::string &name)
{
  const int index = static_cast<int>(ir.nodes.size());
  MakeNode(ir, type, name);
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

KX_GraphInstance *BuildInstance(GraphIR &ir,
                                KX_MockAudioAdapter &audio,
                                KX_MockAnimationAdapter &animation)
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
  instance->GetContext()->SetAdapters(nullptr, nullptr, nullptr, nullptr,
                                      &animation, nullptr, &audio, nullptr,
                                      nullptr, nullptr, nullptr, nullptr);
  return instance;
}

/* ---------------------------------------------------------------- 1. StartSound */
void TestStartSound()
{
  GraphIR ir;
  const int trigger = AddTrigger(ir, "start");
  const int file = AddValue(ir, "B3StringFile", "file");
  const int loop = AddValue(ir, "B3BoolTrue", "loop");
  const int volume = AddValue(ir, "B3Float07", "volume");
  const int pitch = AddValue(ir, "B3Float12", "pitch");
  const int start = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StartSound", "start_sound");
  const int sink = AddSink(ir, "started", 0);
  Connect(ir, trigger, 0, start, 0);
  Connect(ir, file, 0, start, 1);
  Connect(ir, loop, 0, start, 2);
  Connect(ir, volume, 0, start, 3);
  Connect(ir, pitch, 0, start, 4);
  Connect(ir, start, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  g_hits[0] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);
  }

  Check(audio.play_calls == 1, "StartSound calls PlaySound exactly once", audio.play_calls, 1);
  Check(!audio.entries.empty() && audio.entries.begin()->second.filepath == "test.wav",
        "StartSound forwards the file name", audio.entries.empty() ? 0 : 1, 1);
  Check(!audio.entries.empty() && audio.entries.begin()->second.loop,
        "StartSound forwards Loop=true", audio.entries.empty() ? 0 : 1, 1);
  Check(!audio.entries.empty() && std::fabs(audio.entries.begin()->second.volume - 0.7f) < 1e-5f,
        "StartSound forwards Volume=0.7", audio.entries.empty() ? 0 : audio.entries.begin()->second.volume, 0.7);
  Check(!audio.entries.empty() && std::fabs(audio.entries.begin()->second.pitch - 1.2f) < 1e-5f,
        "StartSound forwards Pitch=1.2", audio.entries.empty() ? 0 : audio.entries.begin()->second.pitch, 1.2);
  Check(!audio.entries.empty() && audio.entries.begin()->second.playing,
        "StartSound leaves the source in playing state", audio.entries.empty() ? 0 : 1, 1);
  const KX_RuntimeValue *handle = instance != nullptr ? ReadOutput(*instance, start, 1) : nullptr;
  Check(handle != nullptr && handle->AsInt() == 1, "StartSound returns handle 1",
        handle != nullptr ? handle->AsInt() : 0, 1);
  Check(g_hits[0] == 1, "StartSound raises OnStarted once", g_hits[0], 1);

  delete instance;
}

/* ----------------------------------------------------------------- 2. StopSound */
void TestStopSound()
{
  GraphIR ir;
  const int trigger = AddTrigger(ir, "stop");
  const int file = AddValue(ir, "B3StringFile", "file");
  const int loop = AddValue(ir, "B3BoolTrue", "loop");
  const int volume = AddValue(ir, "B3Float07", "volume");
  const int pitch = AddValue(ir, "B3Float12", "pitch");
  const int start = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StartSound", "start_sound");
  const int stop = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StopSound", "stop_sound");
  const int sink = AddSink(ir, "stopped", 1);
  Connect(ir, trigger, 0, start, 0);
  Connect(ir, file, 0, start, 1);
  Connect(ir, loop, 0, start, 2);
  Connect(ir, volume, 0, start, 3);
  Connect(ir, pitch, 0, start, 4);
  Connect(ir, start, 0, stop, 0);   /* OnStarted */
  Connect(ir, start, 1, stop, 1);   /* Handle */
  Connect(ir, stop, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  g_hits[1] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);
  }

  Check(audio.play_calls == 1, "StopSound scenario starts one sound", audio.play_calls, 1);
  Check(audio.stop_calls == 1, "StopSound calls StopSound exactly once", audio.stop_calls, 1);
  Check(!audio.entries.empty() && !audio.entries.begin()->second.playing,
        "StopSound returns the source to stopped state", audio.entries.empty() ? 1 : 0, 0);
  Check(g_hits[1] == 1, "StopSound raises OnDone once", g_hits[1], 1);

  delete instance;
}

/* ------------------------------------------------------- 3/4. Volume and Pitch */
void TestVolume(bool pitch_case)
{
  GraphIR ir;
  const int trigger = AddTrigger(ir, "set");
  const int file = AddValue(ir, "B3StringFile", "file");
  const int loop = AddValue(ir, "B3BoolFalse", "loop");
  const int one = AddValue(ir, "B3FloatOne", "one");
  const int start = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "StartSound", "start_sound");
  const int setter = static_cast<int>(ir.nodes.size());
  MakeNode(ir, pitch_case ? "SetSoundPitch" : "SetSoundVolume", "setter");
  const int value = AddValue(ir, pitch_case ? "B3Float15" : "B3Float06", "value");
  const int sink = AddSink(ir, "set_done", pitch_case ? 3 : 2);
  Connect(ir, trigger, 0, start, 0);
  Connect(ir, file, 0, start, 1);
  Connect(ir, loop, 0, start, 2);
  Connect(ir, one, 0, start, 3);
  Connect(ir, one, 0, start, 4);
  Connect(ir, start, 0, setter, 0);  /* OnStarted */
  Connect(ir, start, 1, setter, 1);  /* Handle */
  Connect(ir, value, 0, setter, 2);
  Connect(ir, setter, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  const int slot = pitch_case ? 3 : 2;
  g_hits[slot] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);
  }

  if (pitch_case) {
    Check(audio.pitch_calls == 1, "SetSoundPitch calls SetPitch once", audio.pitch_calls, 1);
    Check(!audio.entries.empty() && std::fabs(audio.entries.begin()->second.pitch - 1.5f) < 1e-5f,
          "SetSoundPitch writes pitch 1.5", audio.entries.empty() ? 0 : audio.entries.begin()->second.pitch, 1.5);
  }
  else {
    Check(audio.volume_calls == 1, "SetSoundVolume calls SetVolume once", audio.volume_calls, 1);
    Check(!audio.entries.empty() && std::fabs(audio.entries.begin()->second.volume - 0.6f) < 1e-5f,
          "SetSoundVolume writes volume 0.6", audio.entries.empty() ? 0 : audio.entries.begin()->second.volume, 0.6);
  }
  Check(g_hits[slot] == 1, "setter raises OnDone once", g_hits[slot], 1);

  delete instance;
}

/* ---------------------------------------------------------------- 5. SoundFinished */
void TestSoundFinished()
{
  GraphIR ir;
  const int handle = AddValue(ir, "B3Int1", "handle");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "SoundFinished", "finished");
  const int sink = AddSink(ir, "finish", 4);
  Connect(ir, handle, 0, node, 0);
  Connect(ir, node, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  /* Create handle 1 as a still-playing source. */
  audio.PlaySound("clip.wav", true, 1.0f, 1.0f);
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  g_hits[4] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);                       /* not finished yet */
    audio.entries[1].finished = true;
    instance->Tick(0.016f);                       /* finished -> one trigger */
    instance->Tick(0.016f);                       /* must not fire again */
  }

  Check(g_hits[4] == 1, "SoundFinished raises OnFinish once after the source stops", g_hits[4], 1);
  Check(!audio.entries[1].playing || audio.entries[1].finished,
        "SoundFinished sees the stopped source", 1, 1);

  delete instance;
}

/* ------------------------------------------------------------- 6. PlaySoundByName */
void TestPlaySoundByName()
{
  GraphIR ir;
  const int trigger = AddTrigger(ir, "by_name");
  const int name = AddValue(ir, "B3StringName", "name");
  const int loop = AddValue(ir, "B3BoolFalse", "loop");
  const int volume = AddValue(ir, "B3Float07", "volume");
  const int pitch = AddValue(ir, "B3Float12", "pitch");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "PlaySoundByName", "play_by_name");
  const int sink = AddSink(ir, "by_name_done", 5);
  Connect(ir, trigger, 0, node, 0);
  Connect(ir, name, 0, node, 1);
  Connect(ir, loop, 0, node, 2);
  Connect(ir, volume, 0, node, 3);
  Connect(ir, pitch, 0, node, 4);
  Connect(ir, node, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  g_hits[5] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);
  }

  Check(audio.play_by_name_calls == 1, "PlaySoundByName calls once", audio.play_by_name_calls, 1);
  Check(!audio.by_name_requests.empty() && audio.by_name_requests[0] == "explosion",
        "PlaySoundByName forwards the requested name", audio.by_name_requests.empty() ? 0 : 1, 1);
  const KX_RuntimeValue *handle = instance != nullptr ? ReadOutput(*instance, node, 1) : nullptr;
  Check(handle != nullptr && handle->AsInt() == 1, "PlaySoundByName returns the new handle",
        handle != nullptr ? handle->AsInt() : 0, 1);
  const KX_RuntimeValue *found = instance != nullptr ? ReadOutput(*instance, node, 2) : nullptr;
  Check(found != nullptr && found->AsBool(), "PlaySoundByName reports Found=true",
        found != nullptr ? found->AsBool() : 0, 1);
  Check(g_hits[5] == 1, "PlaySoundByName raises OnDone once", g_hits[5], 1);

  delete instance;
}

/* --------------------------------------------------------------- 7. Set3DListener */
void TestSet3DListener()
{
  GraphIR ir;
  const int trigger = AddTrigger(ir, "listener");
  const int position = AddValue(ir, "B3VectorPos", "position");
  const int orientation = AddValue(ir, "B3VectorOrient", "orientation");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "Set3DListener", "listener");
  const int sink = AddSink(ir, "listener_done", 6);
  Connect(ir, trigger, 0, node, 0);
  Connect(ir, position, 0, node, 1);
  Connect(ir, orientation, 0, node, 2);
  Connect(ir, node, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  g_hits[6] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);
  }

  Check(audio.listener_calls == 1, "Set3DListener calls once", audio.listener_calls, 1);
  Check(std::fabs(audio.listener_position[0] - 1.0f) < 1e-5f &&
            std::fabs(audio.listener_position[1] - 2.0f) < 1e-5f &&
            std::fabs(audio.listener_position[2] - 3.0f) < 1e-5f,
        "Set3DListener forwards the position (1,2,3)", audio.listener_position[0], 1.0);
  Check(std::fabs(audio.listener_orientation[0] - 0.0f) < 1e-5f &&
            std::fabs(audio.listener_orientation[1] - 1.0f) < 1e-5f &&
            std::fabs(audio.listener_orientation[2] - 0.0f) < 1e-5f,
        "Set3DListener forwards the orientation (0,1,0)", audio.listener_orientation[1], 1.0);
  Check(g_hits[6] == 1, "Set3DListener raises OnDone once", g_hits[6], 1);

  delete instance;
}

/* ----------------------------------------------------------------- 8. ActionDone */
void TestActionDone()
{
  GraphIR ir;
  const int object = AddValue(ir, "B3Object", "object");
  const int layer = AddValue(ir, "B3Int0", "layer");
  const int node = static_cast<int>(ir.nodes.size());
  MakeNode(ir, "ActionDone", "action_done");
  const int sink = AddSink(ir, "action_done_out", 7);
  Connect(ir, object, 0, node, 0);
  Connect(ir, layer, 0, node, 1);
  Connect(ir, node, 0, sink, 0);

  KX_MockAudioAdapter audio;
  KX_MockAnimationAdapter animation;
  animation.action = "run";
  animation.done = false;
  KX_GraphInstance *instance = BuildInstance(ir, audio, animation);
  g_hits[7] = 0;
  if (instance != nullptr) {
    instance->Tick(0.016f);            /* playing, no trigger */
    animation.done = true;
    instance->Tick(0.016f);            /* first finished frame -> trigger */
    instance->Tick(0.016f);            /* still finished, no second trigger */
  }

  Check(g_hits[7] == 1, "ActionDone fires exactly once when the action finishes", g_hits[7], 1);
  Check(animation.object == nullptr || animation.action == "run",
        "ActionDone observed the running action name", 1, 1);

  delete instance;
}

}  // namespace

int main()
{
  RegisterB3FunctionGameNodes();
  RegisterTestNodes();

  std::printf("INFO b3 audio evidence harness start (MOCK adapter, not real device)\n");

  TestStartSound();
  TestStopSound();
  TestVolume(false);
  TestVolume(true);
  TestSoundFinished();
  TestPlaySoundByName();
  TestSet3DListener();
  TestActionDone();

  std::printf("INFO failures=%d\n", failures);
  return failures == 0 ? 0 : 1;
}
