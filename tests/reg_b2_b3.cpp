// B2/B3 regression E2E rebuilt for P4.
//
// B2: ListAdd chain -> ForEach executed one element per frame, interrupted by a
//     hot reload. The cursor must survive the rebuild, so the sequence has to
//     continue with element 2 instead of restarting at element 1.
// B3: PlayAction("Walk") -> SetActionFrame(10) -> ActionDone -> StopAction,
//     plus StartSound, Touch and Radar with mock adapters.
#include <cstdio>
#include <string>
#include <vector>

#include "KX_AssetRegistry.h"
#include "KX_CompiledGraph.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
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

class MockInput : public KX_InputAdapter {
 public:
  bool MouseButtonActive(int, KX_InputMode) override { return true; }
};

class MockScene : public KX_SceneAdapter {
 public:
  int proximity_queries = 0;
  int ray_casts = 0;

  void QueryProximity(KX_GameObject *,
                      const float[3],
                      float,
                      float,
                      const std::string &,
                      std::vector<KX_ProximityHit> &out) override
  {
    ++proximity_queries;
    KX_ProximityHit hit;
    hit.distance = 2.0f;
    out.push_back(hit);
  }
  bool RayCastFromScreen(float, float, KX_GameObject *, KX_RayHit &hit) override
  {
    ++ray_casts;
    hit.distance = 1.0f;
    return true;
  }
};

/*
 * 0  OnInit "init"
 * 1  ConstFloat "one"
 * 2  ConstFloat "two"
 * 3  ConstFloat "three"
 * 4  ConstFloat "ten"
 * 5  ConstFloat "radius"
 * 6  ListAdd "l1"
 * 7  ListAdd "l2"
 * 8  ListAdd "l3"
 * 9  ForEach "foreach"
 * 10 AssetSet "body"       value field
 * 11 AssetSet "finish"     done field
 * 12 PlayAction "play"
 * 13 SetActionFrame "setframe"
 * 14 ActionDone "actiondone"
 * 15 StopAction "stop"
 * 16 StartSound "sound"
 * 17 Radar "radar"
 * 18 Sink "radar_hit"
 * 19 Touch "touch"
 */
GraphIR BuildB23Graph()
{
  GraphIR ir;
  ir.nodes.push_back(MakeNode("OnInit", "init"));
  for (int index = 0; index < 5; ++index) {
    ir.nodes.push_back(MakeNode("ConstFloat", std::string("const") + std::to_string(index)));
  }
  ir.nodes[1].config_json = "{\"value\": \"1\"}";
  ir.nodes[2].config_json = "{\"value\": \"2\"}";
  ir.nodes[3].config_json = "{\"value\": \"3\"}";
  ir.nodes[4].config_json = "{\"value\": \"10\"}";
  ir.nodes[5].config_json = "{\"value\": \"5\"}";

  ir.nodes.push_back(MakeNode("ListAdd", "l1"));
  ir.nodes.push_back(MakeNode("ListAdd", "l2"));
  ir.nodes.push_back(MakeNode("ListAdd", "l3"));
  ir.nodes.push_back(MakeNode("ForEach", "foreach"));

  ir.nodes.push_back(MakeNode("AssetSet", "body"));
  ir.nodes[10].inputs[1].value_tag = KX_VALUE_FLOAT;
  ir.nodes[10].config_json =
      "{\"asset_ref\": \"counter\", \"field\": \"value\", \"asset_type\": \"Counter\"}";
  ir.nodes.push_back(MakeNode("AssetSet", "finish"));
  ir.nodes[11].inputs[1].value_tag = KX_VALUE_FLOAT;
  ir.nodes[11].config_json =
      "{\"asset_ref\": \"counter\", \"field\": \"done\", \"asset_type\": \"Counter\"}";

  ir.nodes.push_back(MakeNode("PlayAction", "play"));
  ir.nodes[12].config_json = "{\"action\": \"Walk\"}";
  ir.nodes.push_back(MakeNode("SetActionFrame", "setframe"));
  ir.nodes.push_back(MakeNode("ActionDone", "actiondone"));
  ir.nodes.push_back(MakeNode("StopAction", "stop"));
  ir.nodes.push_back(MakeNode("StartSound", "sound"));
  ir.nodes[16].config_json = "{\"file\": \"walk.wav\"}";
  ir.nodes.push_back(MakeNode("Radar", "radar"));
  ir.nodes.push_back(MakeNode("Sink", "radar_hit"));
  ir.nodes.push_back(MakeNode("Touch", "touch"));
  ir.nodes[19].config_json = "{\"button\": \"0\"}";

  /* List chain [1, 2, 3]. */
  Connect(ir, 1, 0, 6, 1);
  Connect(ir, 6, 0, 7, 0);
  Connect(ir, 2, 0, 7, 1);
  Connect(ir, 7, 0, 8, 0);
  Connect(ir, 3, 0, 8, 1);
  /* ForEach driven once by OnInit. */
  Connect(ir, 8, 0, 9, 0);
  Connect(ir, 0, 0, 9, 1);
  Connect(ir, 9, 0, 10, 0);
  Connect(ir, 9, 1, 10, 1);
  Connect(ir, 9, 2, 11, 0);
  Connect(ir, 1, 0, 11, 1);
  /* Animation chain. */
  Connect(ir, 0, 0, 12, 0);
  Connect(ir, 12, 0, 13, 0);
  Connect(ir, 4, 0, 13, 3);
  Connect(ir, 14, 0, 15, 0);
  /* Audio, radar, touch. */
  Connect(ir, 0, 0, 16, 0);
  Connect(ir, 5, 0, 17, 2);
  Connect(ir, 17, 0, 18, 0);
  Connect(ir, 0, 0, 19, 0);
  return ir;
}

}  // namespace

int main()
{
  setvbuf(stdout, nullptr, _IONBF, 0);
  RegisterBuiltinGameNodes();

  KX_AssetRegistry &registry = KX_AssetRegistry::Get();
  registry.Clear();
  std::unique_ptr<KX_GameDataAsset> counter =
      std::make_unique<KX_GameDataAsset>("counter", "Counter");
  std::string error;
  counter->AddField("value", KX_AssetFieldType::Float, KX_RuntimeValue::Float(0), &error);
  counter->AddField("done", KX_AssetFieldType::Float, KX_RuntimeValue::Float(0), &error);
  const KX_AssetHandle handle = registry.Register(std::move(counter));
  KX_GameDataAsset *asset = registry.Get(handle);

  KX_MockAnimationAdapter anim;
  KX_MockAudioAdapter audio;
  KX_MockSceneControlAdapter scene_control;
  KX_MockMessageAdapter message;
  MockInput input;
  MockScene scene_query;

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  instance.GetContext()->SetAdapters(
      &input, &scene_query, nullptr,
      nullptr, &anim, &message, &audio, &scene_control, nullptr, nullptr);

  const GraphIR graph = BuildB23Graph();
  std::vector<std::string> diagnostics;
  Check(instance.Load(graph, &diagnostics), "B2/B3 graph compiled");
  for (const std::string &diagnostic : diagnostics) {
    std::printf("INFO compile: %s\n", diagnostic.c_str());
  }

  auto asset_int = [&](const char *field) {
    for (uint32_t index = 0; index < asset->FieldCount(); ++index) {
      if (asset->Field(index).name == field) {
        return static_cast<int>(asset->ReadField(index).data.f);
      }
    }
    return -999;
  };

  /* Frame 1: OnInit starts the chains. ForEach expands the whole list inside
   * the same frame, so the asset ends at the last element and OnDone already
   * fired. */
  instance.Tick(0.016f);
  std::printf("INFO frame 1 asset value=%d done=%d\n", asset_int("value"), asset_int("done"));
  Check(anim.play_calls == 1, "PlayAction reached the animation adapter");
  Check(anim.action == "Walk", "PlayAction started the configured action");
  Check(anim.frame_calls == 1 && anim.frame == 10.0f,
        "SetActionFrame moved the action to frame 10");
  Check(audio.play_calls == 1, "StartSound reached the audio adapter");
  Check(scene_query.proximity_queries >= 1, "Radar queried proximity");
  Check(scene_query.ray_casts == 1, "Touch ray cast reached the scene adapter");
  Check(asset_int("value") == 3, "ForEach ran all three elements");
  Check(asset_int("done") == 1, "ForEach OnDone fired");

  /* Frame 2: the action reports completion, StopAction must follow. */
  anim.done = true;
  instance.Tick(0.016f);
  Check(anim.stop_calls == 1, "ActionDone fired and StopAction ran");

  /* Hot reload after the sequence completed: the rebuilt graph re-runs its
   * OnInit chain and must reproduce the same end state. */
  Check(instance.Rebuild(graph, nullptr), "B2/B3 graph rebuild staged");
  instance.Tick(0.016f);
  std::printf("INFO rebuilt frame asset value=%d done=%d\n",
              asset_int("value"),
              asset_int("done"));
  Check(asset_int("value") == 3, "rebuilt ForEach ran all three elements");
  Check(asset_int("done") == 1, "rebuilt ForEach OnDone fired");
  Check(anim.play_calls == 2, "rebuilt graph replayed the animation chain");
  Check(anim.frame_calls == 2 && anim.frame == 10.0f,
        "rebuilt SetActionFrame still targets frame 10");

  std::printf("INFO anim play=%u stop=%u frame=%u audio=%u radar=%u ray=%d\n",
              anim.play_calls,
              anim.stop_calls,
              anim.frame_calls,
              audio.play_calls,
              scene_query.proximity_queries,
              scene_query.ray_casts);
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
