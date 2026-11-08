// B6.9 E2E: audio playlist, multi hit ray query, radar property filtering and
// the flat scene push/pop pair, all through the mock adapter layer.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include "KX_FunctionNode.h"
#include "KX_GraphInstance.h"
#include "KX_GameAdapters.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"

/* Parallel work streams: stub only what this harness does not exercise. */
void RegisterB5FunctionGameNodes() {}
void RegisterBuiltinGameNodes();

namespace {

int failures = 0;

void Check(bool ok, const char *what)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

/* Assertions that must show the measured value, so a boolean pass cannot be
 * mistaken for an uninitialised 0/0 comparison. */
void CheckValue(bool ok, const char *what, double actual, double expected)
{
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

/* PlaySoundPlaylist reports truncation through stderr, so redirect fd 2 to a
 * pipe for the duration of one graph run and read the text back. */
class StderrCapture {
 public:
  void Start()
  {
    if (pipe(fds_) != 0) {
      return;
    }
    saved_ = dup(STDERR_FILENO);
    dup2(fds_[1], STDERR_FILENO);
    close(fds_[1]);
    fds_[1] = -1;
  }

  std::string Stop()
  {
    if (saved_ < 0) {
      return {};
    }
    std::fflush(stderr);
    dup2(saved_, STDERR_FILENO);
    close(saved_);
    saved_ = -1;
    std::string text;
    char buffer[4096];
    ssize_t read_count = 0;
    while ((read_count = read(fds_[0], buffer, sizeof(buffer) - 1)) > 0) {
      text.append(buffer, size_t(read_count));
    }
    close(fds_[0]);
    fds_[0] = -1;
    return text;
  }

 private:
  int fds_[2] = {-1, -1};
  int saved_ = -1;
};

int CountOccurrences(const std::string &haystack, const std::string &needle)
{
  int count = 0;
  size_t at = 0;
  while ((at = haystack.find(needle, at)) != std::string::npos) {
    ++count;
    at += needle.size();
  }
  return count;
}

IRNode MakeNode(const char *type_id, const char *config = "{}")
{
  IRNode node;
  node.type_id = type_id;
  node.debug_name = type_id;
  node.config_json = config;
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  KX_NodeTypeId id = registry.FindByEditorType(type_id);
  if (id == KX_INVALID_NODE_TYPE_ID) {
    id = registry.FindByRuntimeType(type_id);
  }
  if (id == KX_INVALID_NODE_TYPE_ID) {
    std::printf("FAIL unknown runtime type %s\n", type_id);
    ++failures;
    return node;
  }
  const KX_NodeSignature *signature = registry.GetSignature(id);
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

void Connect(GraphIR &ir, int from, int from_port, int to, int to_port)
{
  IRConnection connection;
  connection.from_node = from;
  connection.from_port = from_port;
  connection.to_node = to;
  connection.to_port = to_port;
  ir.connections.push_back(connection);
}

int AddNode(GraphIR &ir, const char *type_id, const char *config = "{}")
{
  ir.nodes.push_back(MakeNode(type_id, config));
  return int(ir.nodes.size()) - 1;
}

int AddFloat(GraphIR &ir, float value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": \"%.4f\"}", double(value));
  return AddNode(ir, "ConstFloat", config);
}

int AddInt(GraphIR &ir, int value)
{
  char config[64];
  std::snprintf(config, sizeof(config), "{\"value\": %d}", value);
  return AddNode(ir, "IntLiteral", config);
}

int AddString(GraphIR &ir, const char *value)
{
  char config[160];
  std::snprintf(config, sizeof(config), "{\"value\": \"%s\"}", value);
  return AddNode(ir, "StringConst", config);
}

/* ------------------------------------------------------------------------- */
/* Test scaffolding                                                           */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kIntLiteralOutput[] = {{"Value", KX_VALUE_INT, false}};

void FuncIntLiteral(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutInt(ctx, 0, node.ConfigInt("value", 0));
}

KX_FunctionSpec kIntLiteralSpec = {"IntLiteral", nullptr,
                                   0,           kIntLiteralOutput,
                                   1,           FuncIntLiteral,
                                   nullptr,     nullptr,
                                   0,           8,
                                   true,        false,
                                   false};

struct Capture {
  std::vector<KX_GameObject *> objects;
  std::vector<float> distances;
  int count = 0;
};

Capture *g_capture = nullptr;

/* Reads a List of objects plus the optional distance list and count. */
const KX_FuncPort kSinkInputs[] = {
    {"OnEnter", KX_VALUE_NONE, true},
    {"Objects", KX_VALUE_LIST, false},
    {"Distances", KX_VALUE_LIST, false},
    {"Count", KX_VALUE_INT, false},
};

void FuncListSink(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  if (g_capture == nullptr) {
    return;
  }
  g_capture->objects.clear();
  g_capture->distances.clear();
  KX_ValueList *objects = static_cast<KX_ValueList *>(node.In(ctx, 1).AsList());
  KX_ValueList *distances = static_cast<KX_ValueList *>(node.In(ctx, 2).AsList());
  if (objects != nullptr) {
    for (size_t index = 0; index < objects->size(); ++index) {
      g_capture->objects.push_back(
          reinterpret_cast<KX_GameObject *>((*objects)[index].data.ptr));
    }
  }
  if (distances != nullptr) {
    for (size_t index = 0; index < distances->size(); ++index) {
      g_capture->distances.push_back((*distances)[index].AsFloat());
    }
  }
  g_capture->count = node.InInt(ctx, 3);
}

KX_FunctionSpec kListSinkSpec = {"TestListSink", kSinkInputs, 4, nullptr, 0, FuncListSink,
                                 nullptr,        nullptr,     0, 8,       false, false,
                                 false};

/* Constant vector source, since the shipped catalog has no vector literal. */
const KX_FuncPort kVectorOutput[] = {{"Value", KX_VALUE_VECTOR, false}};

void FuncVectorConst(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutVector(ctx,
                 0,
                 node.ConfigFloat("x", 1.0f),
                 node.ConfigFloat("y", 0.0f),
                 node.ConfigFloat("z", 0.0f));
}

KX_FunctionSpec kVectorConstSpec = {"TestVectorConst", nullptr, 0, kVectorOutput, 1, FuncVectorConst,
                                    nullptr,           nullptr, 0, KX_RUNTIME_DATA_ALIGNMENT,
                                    true,              false,   false};

void RegisterTestNodes()
{
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();

  KX_NodeSignature int_signature;
  KX_NodePortSignature int_port;
  int_port.name = "Value";
  int_port.kind = IRPortKind::Value;
  int_port.value_type = KX_VALUE_INT;
  int_signature.outputs.push_back(int_port);
  registry.RegisterType("IntLiteral", "KX_Func_IntLiteral", int_signature, []() {
    return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(&kIntLiteralSpec));
  });

  KX_NodeSignature sink_signature;
  for (const KX_FuncPort &port : kSinkInputs) {
    KX_NodePortSignature signature_port;
    signature_port.name = port.name;
    signature_port.kind = port.trigger ? IRPortKind::Trigger : IRPortKind::Value;
    signature_port.value_type = port.type;
    sink_signature.inputs.push_back(signature_port);
  }
  registry.RegisterType("TestListSink", "KX_Func_TestListSink", sink_signature, []() {
    return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(&kListSinkSpec));
  });

  KX_NodeSignature vector_signature;
  KX_NodePortSignature vector_port;
  vector_port.name = "Value";
  vector_port.kind = IRPortKind::Value;
  vector_port.value_type = KX_VALUE_VECTOR;
  vector_signature.outputs.push_back(vector_port);
  registry.RegisterType("TestVectorConst", "KX_Func_TestVectorConst", vector_signature, []() {
    return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(&kVectorConstSpec));
  });
}

/* ------------------------------------------------------------------------- */
/* Fakes                                                                      */
/* ------------------------------------------------------------------------- */

class B69Objects : public KX_ObjectAdapter {
 public:
  float position[3] = {0.0f, 0.0f, 0.0f};

  bool GetWorldPosition(KX_GameObject *, float out[3]) override
  {
    out[0] = position[0];
    out[1] = position[1];
    out[2] = position[2];
    return true;
  }
};

class B69Physics : public KX_PhysicsAdapter {
 public:
  std::vector<KX_RaycastResult> hits;

  void RaycastAll(const float[3],
                  const float[3],
                  float,
                  const KX_GameObject *,
                  std::vector<KX_RaycastResult> &out) override
  {
    /* Deliberately unsorted: the node contract, not the fake, must order them. */
    out.insert(out.end(), hits.begin(), hits.end());
  }
};

class B69Scene : public KX_SceneAdapter {
 public:
  std::vector<std::string> *active = nullptr;
  std::vector<std::string> loads;
  std::vector<float> scales;
  std::map<const KX_GameObject *, std::string> properties;
  std::vector<KX_ProximityHit> candidates;

  bool LoadScene(const std::string &name) override
  {
    loads.push_back(name);
    if (active != nullptr) {
      *active = std::vector<std::string>(1, name);
    }
    return true;
  }

  void SetTimeScale(float scale) override { scales.push_back(scale); }

  void QueryProximity(KX_GameObject *,
                      const float[3],
                      float,
                      float,
                      const std::string &property,
                      std::vector<KX_ProximityHit> &out) override
  {
    for (const KX_ProximityHit &hit : candidates) {
      if (!property.empty()) {
        auto entry = properties.find(hit.object);
        if (entry == properties.end() || entry->second != property) {
          continue;
        }
      }
      out.push_back(hit);
    }
  }
};

class B69SceneControl : public KX_SceneControlAdapter {
 public:
  std::vector<std::string> *active = nullptr;
  std::set<std::string> scenes;
  bool unload_supported = false;

  bool HasScene(const std::string &name) override { return scenes.count(name) != 0; }

  void ActiveSceneNames(std::vector<std::string> &out) override
  {
    out = active != nullptr ? *active : std::vector<std::string>();
  }

  KX_SceneUnloadResult UnloadSceneDetailed(const std::string &name,
                                           std::string *reason) override
  {
    if (!HasScene(name)) {
      if (reason != nullptr) {
        *reason = "scene not found";
      }
      return KX_SceneUnloadResult::Missing;
    }
    if (active != nullptr && std::find(active->begin(), active->end(), name) != active->end()) {
      if (reason != nullptr) {
        *reason = "scene is active";
      }
      return KX_SceneUnloadResult::SceneActive;
    }
    if (!unload_supported) {
      if (reason != nullptr) {
        *reason = "engine RemoveScene disabled";
      }
      return KX_SceneUnloadResult::Unsupported;
    }
    if (reason != nullptr) {
      *reason = "removed";
    }
    return KX_SceneUnloadResult::Success;
  }
};

/* ------------------------------------------------------------------------- */
/* Audio playlist                                                             */
/* ------------------------------------------------------------------------- */

void MarkNewestFinished(KX_MockAudioAdapter &audio)
{
  uint64_t newest = 0;
  for (const auto &entry : audio.entries) {
    newest = std::max(newest, entry.first);
  }
  if (newest != 0u) {
    audio.entries[newest].finished = true;
  }
}

std::vector<std::string> RunPlaylist(const std::string &sounds,
                                     int mode,
                                     int seed,
                                     int starts,
                                     std::vector<float> *scales_out = nullptr)
{
  KX_MockAudioAdapter audio;
  KX_MockInputAdapter input;
  B69Scene scene;
  B69Objects objects;
  std::vector<std::string> active = {"main"};
  scene.active = &active;
  B69SceneControl control;
  control.active = &active;
  control.scenes = {"main"};

  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int split = AddNode(ir, "StringSplit");
  int source = AddString(ir, sounds.c_str());
  int separator = AddString(ir, ",");
  int playlist = AddNode(ir, "PlaySoundPlaylist");
  int mode_node = AddInt(ir, mode);
  int seed_node = AddInt(ir, seed);
  int volume = AddFloat(ir, 1.0f);
  int pitch = AddFloat(ir, 1.0f);
  int done = AddNode(ir, "SetTimeScale");
  int done_value = AddFloat(ir, 1.0f);

  Connect(ir, on_init, 0, playlist, 0);
  Connect(ir, source, 0, split, 0);
  Connect(ir, separator, 0, split, 1);
  Connect(ir, split, 0, playlist, 1);
  Connect(ir, mode_node, 0, playlist, 2);
  Connect(ir, seed_node, 0, playlist, 3);
  Connect(ir, volume, 0, playlist, 4);
  Connect(ir, pitch, 0, playlist, 5);
  Connect(ir, playlist, 0, done, 0);
  Connect(ir, done_value, 0, done, 1);

  KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x200)), nullptr);
  std::vector<std::string> warnings;
  if (!instance.Load(ir, &warnings)) {
    std::printf("FAIL playlist graph (mode %d) did not compile\n", mode);
    ++failures;
    return {};
  }
  instance.GetContext()->SetAdapters(&input, &scene, nullptr, &objects, nullptr, nullptr, &audio,
                                     &control, nullptr, nullptr, nullptr, nullptr);

  instance.Tick(0.05f); /* OnInit fires, state prepared */
  instance.Tick(0.05f); /* first track starts */
  for (int index = 1; index < starts; ++index) {
    MarkNewestFinished(audio);
    instance.Tick(0.05f);
  }
  if (scales_out != nullptr) {
    *scales_out = scene.scales;
  }
  return audio.by_name_requests;
}

void TestPlaylist()
{
  std::printf("--- audio playlist ---\n");

  {
    KX_MockAudioAdapter audio;
    KX_MockInputAdapter input;
    B69Scene scene;
    B69Objects objects;
    std::vector<std::string> active = {"main"};
    scene.active = &active;
    B69SceneControl control;
    control.active = &active;
    control.scenes = {"main"};

    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int split = AddNode(ir, "StringSplit");
    int source = AddString(ir, "one,two,three");
    int separator = AddString(ir, ",");
    int playlist = AddNode(ir, "PlaySoundPlaylist");
    int mode = AddInt(ir, 0);
    int seed = AddInt(ir, 0);
    int volume = AddFloat(ir, 1.0f);
    int pitch = AddFloat(ir, 1.0f);
    int done = AddNode(ir, "SetTimeScale");
    int done_value = AddFloat(ir, 1.0f);
    Connect(ir, on_init, 0, playlist, 0);
    Connect(ir, source, 0, split, 0);
    Connect(ir, separator, 0, split, 1);
    Connect(ir, split, 0, playlist, 1);
    Connect(ir, mode, 0, playlist, 2);
    Connect(ir, seed, 0, playlist, 3);
    Connect(ir, volume, 0, playlist, 4);
    Connect(ir, pitch, 0, playlist, 5);
    Connect(ir, playlist, 0, done, 0);
    Connect(ir, done_value, 0, done, 1);

    KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x201)), nullptr);
    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "sequential playlist graph compiles");
    instance.GetContext()->SetAdapters(&input, &scene, nullptr, &objects, nullptr, nullptr, &audio,
                                       &control, nullptr, nullptr, nullptr, nullptr);
    instance.Tick(0.05f);
    instance.Tick(0.05f);
    Check(audio.by_name_requests.size() == 1 && audio.by_name_requests[0] == "one",
          "sequential playlist starts with the first sound");
    MarkNewestFinished(audio);
    instance.Tick(0.05f);
    Check(audio.by_name_requests.size() == 2 && audio.by_name_requests[1] == "two",
          "sequential playlist advances to the second sound");
    MarkNewestFinished(audio);
    instance.Tick(0.05f);
    Check(audio.by_name_requests.size() == 3 && audio.by_name_requests[2] == "three",
          "sequential playlist advances to the third sound");
    MarkNewestFinished(audio);
    instance.Tick(0.05f);
    instance.Tick(0.05f);
    Check(audio.by_name_requests.size() == 3,
          "sequential playlist stops after the last sound (no fourth request)");
    Check(scene.scales.size() == 1 && scene.scales[0] == 1.0f,
          "sequential playlist fired OnDone exactly once at the end");
  }

  {
    const std::vector<std::string> looped = RunPlaylist("one,two,three", 1, 0, 4);
    Check(looped.size() == 4, "loop playlist requested four tracks");
    Check(looped.size() >= 4 && looped[0] == "one" && looped[1] == "two" && looped[2] == "three" &&
              looped[3] == "one",
          "loop playlist wraps from the last sound back to the first");
  }

  {
    std::vector<float> loop_done;
    const std::vector<std::string> looped =
        RunPlaylist("one,two,three", 1, 0, 7, &loop_done);
    Check(looped.size() == 7, "loop playlist requested seven tracks");
    Check(loop_done.size() == 2, "loop playlist fired OnDone once per pass boundary");
  }

  {
    const std::vector<std::string> first = RunPlaylist("one,two,three", 2, 12345, 3);
    const std::vector<std::string> second = RunPlaylist("one,two,three", 2, 12345, 3);
    Check(first.size() == 3 && second.size() == 3, "shuffle playlist produced three starts");
    Check(first == second, "shuffle playlist is reproducible under a fixed seed");
    std::vector<std::string> sorted = first;
    std::sort(sorted.begin(), sorted.end());
    std::vector<std::string> expected = {"one", "three", "two"};
    Check(sorted == expected, "shuffle playlist is a permutation of the source list");

    const std::vector<std::string> other = RunPlaylist("one,two,three", 2, 777, 3);
    Check(other.size() == 3, "second seed still produced a full pass");

    const std::vector<std::string> boundary = RunPlaylist("one,two", 2, 256, 3);
    Check(boundary.size() == 3 && boundary[0] == "two" && boundary[1] == "one" &&
              boundary[2] == "two",
          "shuffle playlist does not repeat the last track at a pass boundary");
  }

  {
    /* Within the cap nothing is dropped or truncated, so the node must not
     * write anything to stderr: no per-frame spam, no false positives. */
    std::string tracks;
    for (int i = 1; i <= 16; ++i) {
      tracks += "s" + std::to_string(i);
      if (i < 16) {
        tracks += ",";
      }
    }

    StderrCapture capture;
    capture.Start();
    const std::vector<std::string> played = RunPlaylist(tracks, 0, 0, 16);
    const std::string diagnostics = capture.Stop();

    CheckValue(diagnostics.empty(),
               "16-track playlist wrote nothing to stderr",
               double(diagnostics.size()),
               0.0);
    CheckValue(double(played.size()),
               "16-track playlist started all 16 tracks",
               double(played.size()),
               16.0);
  }

  {
    /* 17 tracks: the 16-track cap is a design limit, but the drop must be
     * reported instead of silent. */
    std::string tracks;
    for (int i = 1; i <= 17; ++i) {
      tracks += "s" + std::to_string(i);
      if (i < 17) {
        tracks += ",";
      }
    }

    StderrCapture capture;
    capture.Start();
    const std::vector<std::string> played = RunPlaylist(tracks, 0, 0, 16);
    const std::string diagnostics = capture.Stop();

    const int overflow_reports = CountOccurrences(diagnostics, "tracks requested, only 16 kept");
    CheckValue(overflow_reports == 1,
               "17-track playlist reported the dropped track once",
               double(overflow_reports),
               1.0);
    const bool dropped_text =
        diagnostics.find("17 tracks requested, only 16 kept, 1 dropped") != std::string::npos;
    CheckValue(dropped_text,
               "17-track diagnostic names 17 requested / 16 kept / 1 dropped",
               dropped_text ? 1.0 : 0.0,
               1.0);
    CheckValue(double(played.size()),
               "17-track playlist still started 16 tracks",
               double(played.size()),
               16.0);
    int in_order = 0;
    for (int i = 0; i < int(played.size()) && i < 16; ++i) {
      if (played[size_t(i)] == "s" + std::to_string(i + 1)) {
        ++in_order;
      }
    }
    CheckValue(double(in_order),
               "17-track playlist started s1..s16 in order",
               double(in_order),
               16.0);
  }

  {
    /* Two overlong names: one diagnostic per OnEnter, first index reported,
     * and the truncated names still play. */
    std::string long_first(60, 'a');
    std::string long_second(52, 'b');
    std::string sounds = long_first + "," + long_second + ",short";

    StderrCapture capture;
    capture.Start();
    const std::vector<std::string> played = RunPlaylist(sounds, 0, 0, 3);
    const std::string diagnostics = capture.Stop();

    const int truncation_reports =
        CountOccurrences(diagnostics, "name longer than 47 bytes, truncated");
    CheckValue(double(truncation_reports),
               "overlong names produced exactly one truncation diagnostic",
               double(truncation_reports),
               1.0);
    const bool names_index =
        diagnostics.find("track 0 name longer than 47 bytes, truncated") != std::string::npos;
    CheckValue(names_index ? 1.0 : 0.0,
               "truncation diagnostic names track 0",
               names_index ? 1.0 : 0.0,
               1.0);
    CheckValue(double(played.empty() ? 0 : played[0].size()),
               "truncated first track still plays at 47 bytes",
               double(played.empty() ? 0 : played[0].size()),
               47.0);
    CheckValue(double(played.size()),
               "playlist continues after the truncated names",
               double(played.size()),
               3.0);
  }
}

/* ------------------------------------------------------------------------- */
/* Sensors                                                                    */
/* ------------------------------------------------------------------------- */

void TestRaycastAll()
{
  std::printf("--- sensor multi hit ---\n");
  KX_GameObject *near_obj = reinterpret_cast<KX_GameObject *>(uintptr_t(0x11));
  KX_GameObject *mid_obj = reinterpret_cast<KX_GameObject *>(uintptr_t(0x12));
  KX_GameObject *far_obj = reinterpret_cast<KX_GameObject *>(uintptr_t(0x13));

  B69Physics physics;
  physics.hits.resize(3);
  /* Adapter order is far, near, mid on purpose. */
  physics.hits[0].object = far_obj;
  physics.hits[0].point[0] = 8.0f;
  physics.hits[1].object = near_obj;
  physics.hits[1].point[0] = 2.0f;
  physics.hits[2].object = mid_obj;
  physics.hits[2].point[0] = 5.0f;
  for (KX_RaycastResult &hit : physics.hits) {
    hit.hit = true;
  }

  B69Objects objects;
  B69Scene scene;
  std::vector<std::string> active = {"main"};
  scene.active = &active;
  B69SceneControl control;
  control.active = &active;
  control.scenes = {"main"};

  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int ray = AddNode(ir, "RaycastAll");
  int direction = AddNode(ir, "TestVectorConst", "{\"x\": \"1.0\", \"y\": \"0.0\", \"z\": \"0.0\"}");
  int length = AddFloat(ir, 10.0f);
  int sink = AddNode(ir, "TestListSink");
  Connect(ir, on_init, 0, ray, 0);
  Connect(ir, direction, 0, ray, 1);
  Connect(ir, length, 0, ray, 2);
  Connect(ir, ray, 0, sink, 0);
  Connect(ir, ray, 2, sink, 1);
  Connect(ir, ray, 3, sink, 2);
  Connect(ir, ray, 4, sink, 3);

  KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x210)), nullptr);
  std::vector<std::string> warnings;
  Check(instance.Load(ir, &warnings), "raycast all graph compiles");
  instance.GetContext()->SetAdapters(nullptr, &scene, &physics, &objects, nullptr, nullptr, nullptr,
                                     &control, nullptr, nullptr, nullptr, nullptr);
  Capture capture;
  g_capture = &capture;
  instance.Tick(0.05f);

  Check(capture.count == 3 && capture.objects.size() == 3,
        "raycast all returned all three overlapping objects");
  Check(capture.objects.size() == 3 && capture.objects[0] == near_obj &&
            capture.objects[1] == mid_obj && capture.objects[2] == far_obj,
        "raycast all returned objects nearest first");
  Check(capture.distances.size() == 3 && capture.distances[0] == 2.0f &&
            capture.distances[1] == 5.0f && capture.distances[2] == 8.0f,
        "raycast all distances are ordered 2, 5, 8");
  g_capture = nullptr;

  {
    B69Physics empty_physics;
    GraphIR miss_ir;
    int miss_init = AddNode(miss_ir, "OnInit");
    int miss_ray = AddNode(miss_ir, "RaycastAll");
    int miss_direction = AddNode(miss_ir, "TestVectorConst", "{\"x\": \"1.0\", \"y\": \"0.0\", \"z\": \"0.0\"}");
    int miss_length = AddFloat(miss_ir, 10.0f);
    int miss_sink = AddNode(miss_ir, "SetTimeScale");
    int miss_value = AddFloat(miss_ir, 2.0f);
    Connect(miss_ir, miss_init, 0, miss_ray, 0);
    Connect(miss_ir, miss_direction, 0, miss_ray, 1);
    Connect(miss_ir, miss_length, 0, miss_ray, 2);
    Connect(miss_ir, miss_ray, 1, miss_sink, 0);
    Connect(miss_ir, miss_value, 0, miss_sink, 1);

    KX_GraphInstance miss_instance(
        nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x211)), nullptr);
    std::vector<std::string> miss_warnings;
    Check(miss_instance.Load(miss_ir, &miss_warnings), "raycast all miss graph compiles");
    miss_instance.GetContext()->SetAdapters(nullptr, &scene, &empty_physics, &objects, nullptr,
                                            nullptr, nullptr, &control, nullptr, nullptr, nullptr,
                                            nullptr);
    miss_instance.Tick(0.05f);
    Check(scene.scales.size() == 1 && scene.scales[0] == 2.0f,
          "raycast all with no hit fired OnMiss exactly once");
  }
}

void TestRadarFilter()
{
  std::printf("--- radar property filter ---\n");
  KX_GameObject *enemy_a = reinterpret_cast<KX_GameObject *>(uintptr_t(0x21));
  KX_GameObject *enemy_b = reinterpret_cast<KX_GameObject *>(uintptr_t(0x22));
  KX_GameObject *ally = reinterpret_cast<KX_GameObject *>(uintptr_t(0x23));
  KX_GameObject *neutral = reinterpret_cast<KX_GameObject *>(uintptr_t(0x24));

  B69Scene scene;
  std::vector<std::string> active = {"main"};
  scene.active = &active;
  scene.properties[enemy_a] = "enemy";
  scene.properties[enemy_b] = "enemy";
  scene.properties[ally] = "ally";
  scene.properties[neutral] = "neutral";
  scene.candidates.resize(4);
  scene.candidates[0].object = enemy_a;
  scene.candidates[0].distance = 1.0f;
  scene.candidates[1].object = ally;
  scene.candidates[1].distance = 2.0f;
  scene.candidates[2].object = enemy_b;
  scene.candidates[2].distance = 3.0f;
  scene.candidates[3].object = neutral;
  scene.candidates[3].distance = 4.0f;

  B69Objects objects;
  B69SceneControl control;
  control.active = &active;
  control.scenes = {"main"};

  GraphIR ir;
  int on_init = AddNode(ir, "OnInit");
  int radar = AddNode(ir, "RadarExtended",
                      "{\"property\": \"enemy\", \"radius\": \"5.0\", \"angle\": \"0.0\"}");
  int sink = AddNode(ir, "TestListSink");
  Connect(ir, on_init, 0, radar, 0);
  Connect(ir, radar, 0, sink, 0);
  Connect(ir, radar, 1, sink, 1);

  KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x220)), nullptr);
  std::vector<std::string> warnings;
  Check(instance.Load(ir, &warnings), "radar graph compiles");
  instance.GetContext()->SetAdapters(nullptr, &scene, nullptr, &objects, nullptr, nullptr, nullptr,
                                     &control, nullptr, nullptr, nullptr, nullptr);
  Capture capture;
  g_capture = &capture;
  instance.Tick(0.05f);
  g_capture = nullptr;

  Check(capture.objects.size() == 2, "radar returned only the two matching objects");
  int non_matching = 0;
  for (KX_GameObject *object : capture.objects) {
    if (object != enemy_a && object != enemy_b) {
      ++non_matching;
    }
  }
  Check(non_matching == 0, "radar returned zero non-matching objects");
  Check(std::find(capture.objects.begin(), capture.objects.end(), ally) == capture.objects.end() &&
            std::find(capture.objects.begin(), capture.objects.end(), neutral) ==
                capture.objects.end(),
        "radar excluded ally and neutral explicitly");
}

/* ------------------------------------------------------------------------- */
/* Scene stack                                                                */
/* ------------------------------------------------------------------------- */

void TestSceneStack()
{
  std::printf("--- scene mount / unmount ---\n");

  {
    std::vector<std::string> active = {"main"};
    B69Scene scene;
    scene.active = &active;
    B69SceneControl control;
    control.active = &active;
    control.scenes = {"main", "level"};

    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int mount = AddNode(ir, "MountChildScene");
    int parent = AddString(ir, "main");
    int child = AddString(ir, "level");
    int done = AddNode(ir, "SetTimeScale");
    int done_value = AddFloat(ir, 1.0f);
    int fail = AddNode(ir, "SetTimeScale");
    int fail_value = AddFloat(ir, 2.0f);
    Connect(ir, on_init, 0, mount, 0);
    Connect(ir, parent, 0, mount, 1);
    Connect(ir, child, 0, mount, 2);
    Connect(ir, mount, 0, done, 0);
    Connect(ir, done_value, 0, done, 1);
    Connect(ir, mount, 1, fail, 0);
    Connect(ir, fail_value, 0, fail, 1);

    KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x230)), nullptr);
    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "mount child scene graph compiles");
    instance.GetContext()->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       &control, nullptr, nullptr, nullptr, nullptr);
    instance.Tick(0.05f);
    Check(scene.loads.size() == 1 && scene.loads[0] == "level",
          "mount loaded the child scene exactly once");
    Check(active.size() == 1 && active[0] == "level", "mount made the child scene the active one");
    Check(scene.scales.size() == 1 && scene.scales[0] == 1.0f,
          "mount fired OnDone, not OnFail");
  }

  {
    std::vector<std::string> active = {"level"};
    B69Scene scene;
    scene.active = &active;
    B69SceneControl control;
    control.active = &active;
    control.scenes = {"main", "level"};

    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int unmount = AddNode(ir, "UnmountChildScene");
    int parent = AddString(ir, "main");
    int child = AddString(ir, "level");
    int done = AddNode(ir, "SetTimeScale");
    int done_value = AddFloat(ir, 1.0f);
    int fail = AddNode(ir, "SetTimeScale");
    int fail_value = AddFloat(ir, 2.0f);
    Connect(ir, on_init, 0, unmount, 0);
    Connect(ir, parent, 0, unmount, 1);
    Connect(ir, child, 0, unmount, 2);
    Connect(ir, unmount, 0, done, 0);
    Connect(ir, done_value, 0, done, 1);
    Connect(ir, unmount, 1, fail, 0);
    Connect(ir, fail_value, 0, fail, 1);

    KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x231)), nullptr);
    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "unmount child scene graph compiles");
    instance.GetContext()->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       &control, nullptr, nullptr, nullptr, nullptr);
    instance.Tick(0.05f);
    Check(scene.loads.size() == 1 && scene.loads[0] == "main",
          "unmount returned to the parent scene");
    Check(active.size() == 1 && active[0] == "main", "unmount made the parent active again");
    Check(scene.scales.size() == 1 && scene.scales[0] == 1.0f,
          "unmount fired OnDone, not OnFail");
  }

  {
    std::vector<std::string> active = {"level"};
    B69Scene scene;
    scene.active = &active;
    B69SceneControl control;
    control.active = &active;
    control.scenes = {"level"}; /* parent deliberately missing */

    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int unmount = AddNode(ir, "UnmountChildScene");
    int parent = AddString(ir, "main");
    int child = AddString(ir, "level");
    int fail = AddNode(ir, "SetTimeScale");
    int fail_value = AddFloat(ir, 2.0f);
    Connect(ir, on_init, 0, unmount, 0);
    Connect(ir, parent, 0, unmount, 1);
    Connect(ir, child, 0, unmount, 2);
    Connect(ir, unmount, 1, fail, 0);
    Connect(ir, fail_value, 0, fail, 1);

    KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x232)), nullptr);
    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "unmount missing parent graph compiles");
    instance.GetContext()->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       &control, nullptr, nullptr, nullptr, nullptr);
    instance.Tick(0.05f);
    Check(scene.scales.size() == 1 && scene.scales[0] == 2.0f,
          "unmount with missing parent fired OnFail");
    Check(scene.loads.empty(), "unmount with missing parent did not load anything");
  }

  {
    /* Freeing a child scene goes through UnloadScene and must stay Unsupported. */
    std::vector<std::string> active = {"main"};
    B69Scene scene;
    scene.active = &active;
    B69SceneControl control;
    control.active = &active;
    control.scenes = {"main", "sub"};
    control.unload_supported = false;

    GraphIR ir;
    int on_init = AddNode(ir, "OnInit");
    int unload = AddNode(ir, "UnloadScene");
    int name = AddString(ir, "sub");
    int done = AddNode(ir, "SetTimeScale");
    int done_value = AddFloat(ir, 1.0f);
    int fail = AddNode(ir, "SetTimeScale");
    int fail_value = AddFloat(ir, 2.0f);
    Connect(ir, on_init, 0, unload, 0);
    Connect(ir, name, 0, unload, 1);
    Connect(ir, unload, 0, done, 0);
    Connect(ir, done_value, 0, done, 1);
    Connect(ir, unload, 1, fail, 0);
    Connect(ir, fail_value, 0, fail, 1);

    KX_GraphInstance instance(nullptr, reinterpret_cast<KX_GameObject *>(uintptr_t(0x233)), nullptr);
    std::vector<std::string> warnings;
    Check(instance.Load(ir, &warnings), "unload child scene graph compiles");
    instance.GetContext()->SetAdapters(nullptr, &scene, nullptr, nullptr, nullptr, nullptr, nullptr,
                                       &control, nullptr, nullptr, nullptr, nullptr);
    instance.Tick(0.05f);
    Check(scene.scales.size() == 1 && scene.scales[0] == 2.0f,
          "UnloadScene reports OnFail for the engine Unsupported path");
  }
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterFunctionGameNodes();
  RegisterB6AudioFunctionGameNodes();
  RegisterB6StringFunctionGameNodes();
  RegisterB6SensorFunctionGameNodes();
  RegisterB6SceneFunctionGameNodes();
  RegisterTestNodes();

  TestPlaylist();
  TestRaycastAll();
  TestRadarFilter();
  TestSceneStack();

  std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures,
              failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
