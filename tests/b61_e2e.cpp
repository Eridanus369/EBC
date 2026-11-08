// B6.1 E2E harness: advanced physics + advanced animation nodes.
//
// Builds graph IR from the real node registry and runs it against the mock
// adapters, so every assertion covers node dispatch, port wiring and the
// adapter contract in one pass. Frame 0 and frame 1 are split with gated
// triggers so creation and removal can both be observed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_PhysicsAdvancedAdapter.h"
#include "KX_RuntimeValue.h"
#include "KX_ValuePool.h"

namespace {

int failures = 0;

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

KX_GameObject *g_object_a = reinterpret_cast<KX_GameObject *>(static_cast<uintptr_t>(0x6100));
KX_GameObject *g_object_b = reinterpret_cast<KX_GameObject *>(static_cast<uintptr_t>(0x6200));

KX_NodePortSignature Port(const char *name, IRPortKind kind, KX_ValueType type)
{
  KX_NodePortSignature port;
  port.name = name;
  port.kind = kind;
  port.value_type = type;
  return port;
}

KX_NodeSignature OneOutput(const char *name, KX_ValueType type)
{
  KX_NodeSignature signature;
  signature.outputs.push_back(Port(name, IRPortKind::Value, type));
  return signature;
}

KX_NodeSignature TriggerOutSignature()
{
  KX_NodeSignature signature;
  signature.outputs.push_back(Port("Out", IRPortKind::Trigger, KX_VALUE_NONE));
  return signature;
}

/* --------------------------------------------------------------- test nodes */

/** Emits its trigger every frame from @a min_frame onwards. */
class TriggerNode : public KX_GameNodeRuntime {
 public:
  explicit TriggerNode(uint32_t min_frame) : min_frame_(min_frame) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (ctx.Frame() >= min_frame_) {
      SetOutputTrigger(ctx, 0, true);
    }
    return KX_ExecResult::Continue;
  }

 private:
  uint32_t min_frame_;
};

/** Emits once on the first frame so a stateful node gets a single OnEnter. */
class TriggerOnceNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    (void)ctx;
    if (!fired_) {
      SetOutputTrigger(ctx, 0, true);
      fired_ = true;
    }
    return KX_ExecResult::Continue;
  }

 private:
  bool fired_ = false;
};

/** Counts how many times its trigger input fired; exposes the count as a value. */
class TriggerCounterNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (InputTrigger(ctx, 0)) {
      ++count_;
    }
    OutputValue(ctx, 0) = KX_RuntimeValue::Int(count_);
    return KX_ExecResult::Continue;
  }

 private:
  int count_ = 0;
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

void RegisterType(const std::string &type_id,
                  const KX_NodeSignature &signature,
                  KX_NodeRegistry::Factory factory)
{
  KX_NodeRegistry::Get().RegisterType(type_id, type_id + "Runtime", signature, factory);
}

void RegisterTestNodes()
{
  RegisterType("B61Trigger", TriggerOutSignature(),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new TriggerNode(0)); });
  RegisterType("B61TriggerOnce", TriggerOutSignature(),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new TriggerOnceNode()); });
  {
    KX_NodeSignature signature;
    signature.inputs.push_back(Port("In", IRPortKind::Trigger, KX_VALUE_NONE));
    signature.outputs.push_back(Port("Count", IRPortKind::Value, KX_VALUE_INT));
    RegisterType("B61TriggerCounter", signature,
                 []() { return std::unique_ptr<KX_GameNodeRuntime>(new TriggerCounterNode()); });
  }
  RegisterType("B61TriggerLate", TriggerOutSignature(),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new TriggerNode(2)); });
  RegisterType("B61ObjectA", OneOutput("Object", KX_VALUE_OBJECT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new ObjectNode(g_object_a)); });
  RegisterType("B61ObjectB", OneOutput("Object", KX_VALUE_OBJECT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new ObjectNode(g_object_b)); });
  RegisterType("B61VectorFrom", OneOutput("Value", KX_VALUE_VECTOR),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new VectorNode(0.0f, 0.0f, 0.0f)); });
  RegisterType("B61VectorTo", OneOutput("Value", KX_VALUE_VECTOR),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new VectorNode(6.0f, 0.0f, 0.0f)); });
  RegisterType("B61VectorBonePos", OneOutput("Value", KX_VALUE_VECTOR),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new VectorNode(1.0f, 2.0f, 3.0f)); });
  RegisterType("B61VectorBoneRot", OneOutput("Value", KX_VALUE_VECTOR),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new VectorNode(0.25f, 0.5f, 0.75f)); });
  RegisterType("B61FloatSteer", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(0.5f)); });
  RegisterType("B61FloatThrottle", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(1.0f)); });
  RegisterType("B61FloatBrake", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(0.25f)); });
  RegisterType("B61FloatDistance", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(10.0f)); });
  RegisterType("B61FloatBlendIn", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(0.3f)); });
  RegisterType("B61FloatFactor", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(0.4f)); });
  RegisterType("B61FloatSpeed", OneOutput("Value", KX_VALUE_FLOAT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new FloatNode(1.5f)); });
  RegisterType("B61IntLayer2", OneOutput("Value", KX_VALUE_INT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new IntNode(2)); });
  RegisterType("B61IntLayer0", OneOutput("Value", KX_VALUE_INT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new IntNode(0)); });
  RegisterType("B61IntEventFrame", OneOutput("Value", KX_VALUE_INT),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new IntNode(5)); });
  RegisterType("B61StringWheel", OneOutput("Value", KX_VALUE_STRING),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("wheel")); });
  RegisterType("B61StringActionA", OneOutput("Value", KX_VALUE_STRING),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("run")); });
  RegisterType("B61StringActionB", OneOutput("Value", KX_VALUE_STRING),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("walk")); });
  RegisterType("B61StringBone", OneOutput("Value", KX_VALUE_STRING),
               []() { return std::unique_ptr<KX_GameNodeRuntime>(new StringNode("spine")); });
}

/* ------------------------------------------------------------ graph helpers */

int AddNode(GraphIR &ir, const std::string &type_id, const std::string &name)
{
  const int index = static_cast<int>(ir.nodes.size());
  IRNode node;
  node.type_id = type_id;
  node.debug_name = name;
  const KX_NodeTypeId id = KX_NodeRegistry::Get().FindByEditorType(type_id);
  const KX_NodeSignature *signature = KX_NodeRegistry::Get().GetSignature(id);
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
  return index;
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
                                KX_MockAnimationAdapter &animation,
                                KX_MockPhysicsAdvancedAdapter &physics)
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
  instance->GetContext()->SetAdapters(nullptr, nullptr, nullptr, nullptr, &animation,
                                      nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr, &physics);
  return instance;
}

/* -------------------------------------------------------------- physics E2E */

struct PhysicsGraph {
  int create_vehicle = 0;
  int vehicle_control = 0;
  int add_constraint = 0;
  int remove_constraint = 0;
  int query_path = 0;
  int raycast_batch = 0;
};

GraphIR BuildPhysicsGraph(PhysicsGraph *out,
                          const std::string &wheel_names = "",
                          const std::string &suspension = "")
{
  PhysicsGraph ids;
  GraphIR ir;

  AddNode(ir, "B61Trigger", "trigger");
  const int chassis = AddNode(ir, "B61ObjectA", "chassis");
  const int other = AddNode(ir, "B61ObjectB", "other");
  const int wheel = AddNode(ir, "B61StringWheel", "wheel");

  ids.create_vehicle = AddNode(ir, "CreateVehicle", "create_vehicle");
  if (!wheel_names.empty() || !suspension.empty()) {
    std::string config = "{";
    if (!wheel_names.empty()) {
      config += "\"wheel_names\": \"" + wheel_names + "\"";
    }
    if (!suspension.empty()) {
      if (!wheel_names.empty()) {
        config += ", ";
      }
      /* P-3.5 per-wheel suspension: stiffness,damping,compression,rest_length */
      config += "\"suspension\": \"" + suspension + "\"";
    }
    config += "}";
    ir.nodes.back().config_json = config;
  }
  Connect(ir, 0, 0, ids.create_vehicle, 0);
  Connect(ir, chassis, 0, ids.create_vehicle, 1);
  Connect(ir, wheel, 0, ids.create_vehicle, 2);

  const int steer = AddNode(ir, "B61FloatSteer", "steer");
  const int throttle = AddNode(ir, "B61FloatThrottle", "throttle");
  const int brake = AddNode(ir, "B61FloatBrake", "brake");

  ids.vehicle_control = AddNode(ir, "VehicleControl", "vehicle_control");
  Connect(ir, ids.create_vehicle, 0, ids.vehicle_control, 0);
  Connect(ir, ids.create_vehicle, 2, ids.vehicle_control, 1);
  Connect(ir, steer, 0, ids.vehicle_control, 2);
  Connect(ir, throttle, 0, ids.vehicle_control, 3);
  Connect(ir, brake, 0, ids.vehicle_control, 4);

  ids.add_constraint = AddNode(ir, "AddConstraint", "add_constraint");
  ir.nodes.back().config_json =
      "{\"type\": 1, \"params\": \"1,0,0,0,0,0\", \"pivot\": \"1,2,3\", \"axis\": \"0,0,1\"}";
  Connect(ir, 0, 0, ids.add_constraint, 0);
  Connect(ir, chassis, 0, ids.add_constraint, 1);
  Connect(ir, other, 0, ids.add_constraint, 2);

  const int late = AddNode(ir, "B61TriggerLate", "late_trigger");
  ids.remove_constraint = AddNode(ir, "RemoveConstraint", "remove_constraint");
  Connect(ir, late, 0, ids.remove_constraint, 0);
  Connect(ir, chassis, 0, ids.remove_constraint, 1);

  const int from = AddNode(ir, "B61VectorFrom", "from");
  const int to = AddNode(ir, "B61VectorTo", "to");

  ids.query_path = AddNode(ir, "QueryPath", "query_path");
  Connect(ir, 0, 0, ids.query_path, 0);
  Connect(ir, from, 0, ids.query_path, 1);
  Connect(ir, to, 0, ids.query_path, 2);

  const int distance = AddNode(ir, "B61FloatDistance", "distance");

  ids.raycast_batch = AddNode(ir, "RaycastBatch", "raycast_batch");
  Connect(ir, 0, 0, ids.raycast_batch, 0);
  Connect(ir, ids.query_path, 2, ids.raycast_batch, 1);
  Connect(ir, ids.query_path, 2, ids.raycast_batch, 2);
  Connect(ir, distance, 0, ids.raycast_batch, 3);

  *out = ids;
  return ir;
}

/* ---------------------------------------------------- body / joint node E2E */

struct BodyGraph {
  int joint = 0;
  int mass = 0;
  int friction = 0;
  int restitution = 0;
  int linear_damping = 0;
  int angular_damping = 0;
  int group = 0;
  int navmesh = 0;
};

GraphIR BuildBodyGraph(BodyGraph *out)
{
  BodyGraph ids;
  GraphIR ir;

  AddNode(ir, "B61Trigger", "trigger");
  const int object = AddNode(ir, "B61ObjectA", "object");
  const int other = AddNode(ir, "B61ObjectB", "other");
  const int pivot = AddNode(ir, "B61VectorBonePos", "pivot");
  const int axis = AddNode(ir, "B61VectorBoneRot", "axis");
  const int mass_value = AddNode(ir, "B61FloatSteer", "mass");
  const int friction_value = AddNode(ir, "B61FloatThrottle", "friction");
  const int restitution_value = AddNode(ir, "B61FloatBrake", "restitution");
  const int damping_value = AddNode(ir, "B61FloatBlendIn", "damping");
  const int angular_value = AddNode(ir, "B61FloatFactor", "angular_damping");
  const int group_value = AddNode(ir, "B61IntLayer2", "group");
  const int mask_value = AddNode(ir, "B61IntLayer0", "mask");

  ids.joint = AddNode(ir, "AddJoint", "joint");
  Connect(ir, 0, 0, ids.joint, 0);
  Connect(ir, object, 0, ids.joint, 1);
  Connect(ir, other, 0, ids.joint, 2);
  Connect(ir, pivot, 0, ids.joint, 3);
  Connect(ir, axis, 0, ids.joint, 4);

  ids.mass = AddNode(ir, "SetPhysicsMass", "mass");
  Connect(ir, 0, 0, ids.mass, 0);
  Connect(ir, object, 0, ids.mass, 1);
  Connect(ir, mass_value, 0, ids.mass, 2);

  ids.friction = AddNode(ir, "SetPhysicsFriction", "friction");
  Connect(ir, 0, 0, ids.friction, 0);
  Connect(ir, object, 0, ids.friction, 1);
  Connect(ir, friction_value, 0, ids.friction, 2);

  ids.restitution = AddNode(ir, "SetPhysicsRestitution", "restitution");
  Connect(ir, 0, 0, ids.restitution, 0);
  Connect(ir, object, 0, ids.restitution, 1);
  Connect(ir, restitution_value, 0, ids.restitution, 2);

  ids.linear_damping = AddNode(ir, "SetLinearDamping", "linear_damping");
  Connect(ir, 0, 0, ids.linear_damping, 0);
  Connect(ir, object, 0, ids.linear_damping, 1);
  Connect(ir, damping_value, 0, ids.linear_damping, 2);

  ids.angular_damping = AddNode(ir, "SetAngularDamping", "angular_damping");
  Connect(ir, 0, 0, ids.angular_damping, 0);
  Connect(ir, object, 0, ids.angular_damping, 1);
  Connect(ir, angular_value, 0, ids.angular_damping, 2);

  ids.group = AddNode(ir, "SetCollisionGroup", "group");
  Connect(ir, 0, 0, ids.group, 0);
  Connect(ir, object, 0, ids.group, 1);
  Connect(ir, group_value, 0, ids.group, 2);
  Connect(ir, mask_value, 0, ids.group, 3);

  ids.navmesh = AddNode(ir, "BuildNavMesh", "navmesh");
  ir.nodes.back().config_json = "{\"agent_radius\": 0.3, \"agent_height\": 1.0}";
  Connect(ir, 0, 0, ids.navmesh, 0);

  *out = ids;
  return ir;
}

/* ------------------------------------------------------------ animation E2E */

struct PlayGraph {
  int play_layer = 0;
  int blend = 0;
  int bone_transform = 0;
};

GraphIR BuildPlayGraph(PlayGraph *out)
{
  PlayGraph ids;
  GraphIR ir;

  AddNode(ir, "B61Trigger", "trigger");
  const int object = AddNode(ir, "B61ObjectA", "object");
  const int action_a = AddNode(ir, "B61StringActionA", "action_a");
  const int action_b = AddNode(ir, "B61StringActionB", "action_b");
  const int bone = AddNode(ir, "B61StringBone", "bone");
  const int layer2 = AddNode(ir, "B61IntLayer2", "layer2");
  const int blend_in = AddNode(ir, "B61FloatBlendIn", "blend_in");
  const int factor = AddNode(ir, "B61FloatFactor", "factor");
  const int bone_pos = AddNode(ir, "B61VectorBonePos", "bone_pos");
  const int bone_rot = AddNode(ir, "B61VectorBoneRot", "bone_rot");

  ids.play_layer = AddNode(ir, "PlayActionByLayer", "play_layer");
  Connect(ir, 0, 0, ids.play_layer, 0);
  Connect(ir, object, 0, ids.play_layer, 1);
  Connect(ir, action_a, 0, ids.play_layer, 2);
  Connect(ir, layer2, 0, ids.play_layer, 3);
  Connect(ir, blend_in, 0, ids.play_layer, 4);

  ids.blend = AddNode(ir, "BlendActions", "blend");
  Connect(ir, 0, 0, ids.blend, 0);
  Connect(ir, object, 0, ids.blend, 1);
  Connect(ir, action_a, 0, ids.blend, 2);
  Connect(ir, action_b, 0, ids.blend, 3);
  Connect(ir, factor, 0, ids.blend, 4);

  ids.bone_transform = AddNode(ir, "SetBoneTransform", "bone_transform");
  Connect(ir, 0, 0, ids.bone_transform, 0);
  Connect(ir, object, 0, ids.bone_transform, 1);
  Connect(ir, bone, 0, ids.bone_transform, 2);
  Connect(ir, bone_pos, 0, ids.bone_transform, 3);
  Connect(ir, bone_rot, 0, ids.bone_transform, 4);

  *out = ids;
  return ir;
}

struct ClearGraph {
  int clear = 0;
};

/* B6.10: ClearBoneOverride must be reachable from the graph and remove the SetBoneTransform
 * override so the bone is driven by the action again. */
GraphIR BuildClearGraph(ClearGraph *out)
{
  ClearGraph ids;
  GraphIR ir;

  AddNode(ir, "B61Trigger", "trigger");
  const int object = AddNode(ir, "B61ObjectA", "object");
  const int bone = AddNode(ir, "B61StringBone", "bone");

  ids.clear = AddNode(ir, "ClearBoneOverride", "clear");
  Connect(ir, 0, 0, ids.clear, 0);
  Connect(ir, object, 0, ids.clear, 1);
  Connect(ir, bone, 0, ids.clear, 2);

  *out = ids;
  return ir;
}

struct SpeedGraph {
  int speed = 0;
  int get_frame = 0;
  int time_scale = 0;
  int normalized_time = 0;
  int bone_read = 0;
};

GraphIR BuildSpeedGraph(SpeedGraph *out)
{
  SpeedGraph ids;
  GraphIR ir;

  AddNode(ir, "B61Trigger", "trigger");
  const int object = AddNode(ir, "B61ObjectA", "object");
  const int layer0 = AddNode(ir, "B61IntLayer0", "layer0");
  const int speed_value = AddNode(ir, "B61FloatSpeed", "speed_value");

  ids.speed = AddNode(ir, "SetAnimationSpeed", "speed");
  Connect(ir, 0, 0, ids.speed, 0);
  Connect(ir, object, 0, ids.speed, 1);
  Connect(ir, layer0, 0, ids.speed, 2);
  Connect(ir, speed_value, 0, ids.speed, 3);

  ids.get_frame = AddNode(ir, "GetAnimationFrame", "get_frame");
  Connect(ir, object, 0, ids.get_frame, 0);
  Connect(ir, layer0, 0, ids.get_frame, 1);

  /* B6.9-A: explicit time-scale node must reach the adapter and fail with its reason,
   * while the normalized-time query reads the adapter's clamped playback position. */
  ids.time_scale = AddNode(ir, "SetActionTimeScale", "time_scale");
  Connect(ir, 0, 0, ids.time_scale, 0);
  Connect(ir, object, 0, ids.time_scale, 1);
  Connect(ir, layer0, 0, ids.time_scale, 2);
  Connect(ir, speed_value, 0, ids.time_scale, 3);

  ids.normalized_time = AddNode(ir, "GetActionNormalizedTime", "normalized_time");
  Connect(ir, object, 0, ids.normalized_time, 0);
  Connect(ir, layer0, 0, ids.normalized_time, 1);

  /* B6.9-A: pose read-back node, used by the runtime probe that proves a manual bone
   * transform survives a playing action. */
  const int bone = AddNode(ir, "B61StringBone", "bone");
  ids.bone_read = AddNode(ir, "GetBoneTransform", "bone_read");
  Connect(ir, object, 0, ids.bone_read, 0);
  Connect(ir, bone, 0, ids.bone_read, 1);

  *out = ids;
  return ir;
}

struct LimitedGraph {
  int bone_weight = 0;
  int bone_action = 0;
};

struct EventGraph {
  int event = 0;
  int counter = 0;
};

GraphIR BuildEventGraph(EventGraph *out)
{
  EventGraph ids;
  GraphIR ir;

  const int trigger = AddNode(ir, "B61TriggerOnce", "trigger_once");
  const int object = AddNode(ir, "B61ObjectA", "object");
  const int action = AddNode(ir, "B61StringActionA", "action");
  const int frame = AddNode(ir, "B61IntEventFrame", "event_frame");

  ids.event = AddNode(ir, "AddAnimationEvent", "event");
  ir.nodes[ids.event].config_json = "{\"event_name\": \"halfway\"}";
  Connect(ir, trigger, 0, ids.event, 0);
  Connect(ir, object, 0, ids.event, 1);
  Connect(ir, action, 0, ids.event, 2);
  Connect(ir, frame, 0, ids.event, 3);

  ids.counter = AddNode(ir, "B61TriggerCounter", "counter");
  Connect(ir, ids.event, 0, ids.counter, 0);

  *out = ids;
  return ir;
}

GraphIR BuildLimitedGraph(LimitedGraph *out)
{
  LimitedGraph ids;
  GraphIR ir;

  AddNode(ir, "B61Trigger", "trigger");
  const int object = AddNode(ir, "B61ObjectA", "object");
  const int bone = AddNode(ir, "B61StringBone", "bone");
  const int action = AddNode(ir, "B61StringActionA", "action");
  const int weight = AddNode(ir, "B61FloatFactor", "weight");

  ids.bone_weight = AddNode(ir, "SetBoneWeight", "bone_weight");
  Connect(ir, 0, 0, ids.bone_weight, 0);
  Connect(ir, object, 0, ids.bone_weight, 1);
  Connect(ir, bone, 0, ids.bone_weight, 2);
  Connect(ir, weight, 0, ids.bone_weight, 3);

  ids.bone_action = AddNode(ir, "PlayBoneAction", "bone_action");
  Connect(ir, 0, 0, ids.bone_action, 0);
  Connect(ir, object, 0, ids.bone_action, 1);
  Connect(ir, bone, 0, ids.bone_action, 2);
  Connect(ir, action, 0, ids.bone_action, 3);

  *out = ids;
  return ir;
}

}  // namespace

int main()
{
  RegisterB6AdvancedFunctionGameNodes();
  RegisterTestNodes();
  std::printf("INFO harness start\n");

  KX_MockAnimationAdapter animation;
  KX_MockPhysicsAdvancedAdapter physics;

  /* ------------------------------------------------------------- physics run */
  {
    physics.Reset();
    PhysicsGraph ids;
    GraphIR ir = BuildPhysicsGraph(&ids);
    KX_GraphInstance *instance = BuildInstance(ir, animation, physics);
    if (instance == nullptr) {
      std::printf("FAIL physics graph did not compile\n");
      return 1;
    }

    physics.path_waypoints = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f};
    KX_PhysicsBatchHit hit;
    hit.hit = true;
    hit.object = g_object_b;
    hit.point[0] = 1.0f;
    hit.point[1] = 2.0f;
    hit.point[2] = 3.0f;
    physics.batch_hits = {hit};

    instance->Tick(0.016f);

    const KX_RuntimeValue *handle = ReadOutput(*instance, ids.create_vehicle, 2);
    Check(physics.create_vehicle_calls == 1, "CreateVehicle reached the adapter",
          static_cast<double>(physics.create_vehicle_calls), 1.0);
    Check(physics.vehicle_chassis == g_object_a && physics.vehicle_template == "wheel",
          "CreateVehicle chassis/template");
    Check(physics.vehicle_wheels.empty(),
          "empty wheel_names keeps the legacy template path",
          static_cast<double>(physics.vehicle_wheels.size()), 0.0);
    Check(handle != nullptr && handle->AsInt() == 1, "CreateVehicle handle output",
          handle != nullptr ? handle->AsInt() : -1.0, 1.0);

    Check(physics.vehicle_control_calls == 1, "VehicleControl reached the adapter",
          static_cast<double>(physics.vehicle_control_calls), 1.0);
    Check(physics.last_vehicle == 1 && std::fabs(physics.last_steering - 0.5f) < 1e-5f &&
              std::fabs(physics.last_throttle - 1.0f) < 1e-5f &&
              std::fabs(physics.last_brake - 0.25f) < 1e-5f,
          "VehicleControl steering/throttle/brake");

    Check(physics.add_constraint_calls == 1, "AddConstraint reached the adapter",
          static_cast<double>(physics.add_constraint_calls), 1.0);
    Check(physics.constraints.size() == 1 && physics.constraints[0].type == 1 &&
              std::fabs(physics.constraints[0].params[0] - 1.0f) < 1e-5f &&
              physics.constraints[0].a == g_object_a && physics.constraints[0].b == g_object_b,
          "AddConstraint type/params/owners");
    Check(physics.constraints.size() == 1 && physics.constraints[0].has_frame &&
              std::fabs(physics.constraints[0].pivot[0] - 1.0f) < 1e-5f &&
              std::fabs(physics.constraints[0].pivot[1] - 2.0f) < 1e-5f &&
              std::fabs(physics.constraints[0].pivot[2] - 3.0f) < 1e-5f &&
              std::fabs(physics.constraints[0].axis[0]) < 1e-5f &&
              std::fabs(physics.constraints[0].axis[2] - 1.0f) < 1e-5f,
          "AddConstraint forwards the configured pivot/axis");
    Check(physics.remove_constraint_calls == 0, "RemoveConstraint waits for frame 1",
          static_cast<double>(physics.remove_constraint_calls), 0.0);

    const KX_RuntimeValue *waypoints = ReadOutput(*instance, ids.query_path, 2);
    const KX_ValueList *path = AsListOf(waypoints);
    Check(path != nullptr && path->size() == 3, "QueryPath returns a non empty waypoint list",
          path != nullptr ? static_cast<double>(path->size()) : 0.0, 3.0);

    instance->Tick(0.016f);

    Check(physics.remove_constraint_calls == 1 && physics.constraints.empty(),
          "RemoveConstraint empties the constraint list",
          static_cast<double>(physics.constraints.size()), 0.0);

    const KX_RuntimeValue *hits_value = ReadOutput(*instance, ids.raycast_batch, 0);
    const KX_RuntimeValue *points_value = ReadOutput(*instance, ids.raycast_batch, 1);
    const KX_ValueList *hits = AsListOf(hits_value);
    const KX_ValueList *points = AsListOf(points_value);
    Check(physics.raycast_batch_calls == 2, "RaycastBatch reached the adapter",
          static_cast<double>(physics.raycast_batch_calls), 2.0);
    Check(physics.last_batch_origins.size() == 9 && physics.last_batch_dirs.size() == 9,
          "RaycastBatch forwards three rays",
          static_cast<double>(physics.last_batch_origins.size()), 9.0);
    Check(std::fabs(physics.last_batch_distance - 10.0f) < 1e-5f,
          "RaycastBatch max distance",
          physics.last_batch_distance, 10.0);
    Check(hits != nullptr && hits->size() == 1, "RaycastBatch Hits output",
          hits != nullptr ? static_cast<double>(hits->size()) : 0.0, 1.0);
    Check(points != nullptr && points->size() == 1 &&
              std::fabs((*points)[0].data.vec[0] - 1.0f) < 1e-5f &&
              std::fabs((*points)[0].data.vec[1] - 2.0f) < 1e-5f &&
              std::fabs((*points)[0].data.vec[2] - 3.0f) < 1e-5f,
          "RaycastBatch Points output");

    /* Batch/single equivalence: the engine adapter runs one KX_RayCast::RayTest
     * per ray, so a one ray batch has to match a direct single ray call. */
    std::vector<KX_PhysicsBatchHit> single_hits;
    std::string error;
    const bool single_ok = physics.RaycastBatch(
        nullptr, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 10.0f, single_hits, &error);
    Check(single_ok && single_hits.size() == hits->size() &&
              single_hits[0].object == (*hits)[0].data.ptr,
          "single ray hit matches the batch hit object");

    delete instance;
  }

  /* C.1.2: explicit wheel names must win over the naming convention, and the
   * adapter must see all four names in order (no silent partial vehicles). */
  {
    KX_MockPhysicsAdvancedAdapter wheels_physics;
    PhysicsGraph wheels_ids;
    GraphIR wheels_ir = BuildPhysicsGraph(
        &wheels_ids,
        "wheelFL_A, wheelFR_A,wheelRL_A ,wheelRR_A",
        "3,0.8,0.4,0.25; 1,0.5,0.2,0.3; 2,0.6,0.3,0.15; 4,0.9,0.5,0.1");
    KX_GraphInstance *wheels_instance = BuildInstance(wheels_ir, animation, wheels_physics);
    if (wheels_instance == nullptr) {
      std::printf("FAIL wheel_names graph did not compile\n");
      return 1;
    }
    wheels_instance->Tick(0.016f);
    Check(wheels_physics.vehicle_wheels.size() == 4,
          "explicit wheel_names attaches four wheels",
          static_cast<double>(wheels_physics.vehicle_wheels.size()), 4.0);
    const bool wheel_names_ok =
        wheels_physics.vehicle_wheels.size() == 4 &&
        wheels_physics.vehicle_wheels[0] == "wheelFL_A" &&
        wheels_physics.vehicle_wheels[1] == "wheelFR_A" &&
        wheels_physics.vehicle_wheels[2] == "wheelRL_A" &&
        wheels_physics.vehicle_wheels[3] == "wheelRR_A";
    Check(wheel_names_ok, "explicit wheel_names preserves order and trims spaces");
    Check(wheels_physics.vehicle_suspension.size() == 16,
          "per-wheel suspension forwards one group per wheel",
          static_cast<double>(wheels_physics.vehicle_suspension.size()), 16.0);
    Check(wheels_physics.vehicle_suspension.size() == 16 &&
              std::fabs(wheels_physics.vehicle_suspension[0] - 3.0f) < 1e-5f &&
              std::fabs(wheels_physics.vehicle_suspension[1] - 0.8f) < 1e-5f &&
              std::fabs(wheels_physics.vehicle_suspension[2] - 0.4f) < 1e-5f &&
              std::fabs(wheels_physics.vehicle_suspension[3] - 0.25f) < 1e-5f &&
              std::fabs(wheels_physics.vehicle_suspension[12] - 4.0f) < 1e-5f &&
              std::fabs(wheels_physics.vehicle_suspension[15] - 0.1f) < 1e-5f,
          "per-wheel suspension preserves order and values");
    delete wheels_instance;
  }

  /* P-3.5 / rescore evidence: body and joint nodes that had no node level E2E. */
  {
    KX_MockPhysicsAdvancedAdapter body_physics;
    BodyGraph body;
    GraphIR body_ir = BuildBodyGraph(&body);
    KX_GraphInstance *body_instance = BuildInstance(body_ir, animation, body_physics);
    if (body_instance == nullptr) {
      std::printf("FAIL body graph did not compile\n");
      return 1;
    }
    body_instance->Tick(0.016f);

    Check(body_physics.add_joint_calls == 1 && body_physics.constraints.size() == 1,
          "AddJoint reaches the adapter",
          static_cast<double>(body_physics.add_joint_calls), 1.0);
    Check(body_physics.set_mass_calls == 1 && std::fabs(body_physics.last_mass - 0.5f) < 1e-5f,
          "SetPhysicsMass forwards the mass",
          body_physics.last_mass, 0.5);
    Check(body_physics.set_friction_calls == 1 &&
              std::fabs(body_physics.last_friction - 1.0f) < 1e-5f,
          "SetPhysicsFriction forwards the friction",
          body_physics.last_friction, 1.0);
    Check(body_physics.set_restitution_calls == 1 &&
              std::fabs(body_physics.last_restitution - 0.25f) < 1e-5f,
          "SetPhysicsRestitution forwards the restitution",
          body_physics.last_restitution, 0.25);
    Check(body_physics.set_linear_damping_calls == 1 &&
              std::fabs(body_physics.last_linear_damping - 0.3f) < 1e-5f,
          "SetLinearDamping forwards the damping",
          body_physics.last_linear_damping, 0.3);
    Check(body_physics.set_angular_damping_calls == 1 &&
              std::fabs(body_physics.last_angular_damping - 0.4f) < 1e-5f,
          "SetAngularDamping forwards the damping",
          body_physics.last_angular_damping, 0.4);
    Check(body_physics.set_collision_group_calls == 1 && body_physics.last_group == 2 &&
              body_physics.last_mask == 0,
          "SetCollisionGroup forwards group and mask",
          static_cast<double>(body_physics.last_group), 2.0);
    Check(body_physics.build_navmesh_calls == 1 &&
              std::fabs(body_physics.last_agent_radius - 0.3f) < 1e-5f &&
              std::fabs(body_physics.last_agent_height - 1.0f) < 1e-5f,
          "BuildNavMesh forwards the agent size",
          body_physics.last_agent_radius, 0.3);
    delete body_instance;
  }

  /* Malformed suspension config must fail before a vehicle is created. */
  {
    KX_MockPhysicsAdvancedAdapter bad_physics;
    PhysicsGraph bad_ids;
    GraphIR bad_ir = BuildPhysicsGraph(
        &bad_ids, "wheelFL_A, wheelFR_A,wheelRL_A ,wheelRR_A", "3,0.8");
    KX_GraphInstance *bad_instance = BuildInstance(bad_ir, animation, bad_physics);
    if (bad_instance == nullptr) {
      std::printf("FAIL bad suspension graph did not compile\n");
      return 1;
    }
    bad_instance->Tick(0.016f);
    Check(bad_physics.create_vehicle_calls == 0 && bad_physics.vehicle_suspension.empty(),
          "malformed suspension config fails before CreateVehicle",
          static_cast<double>(bad_physics.create_vehicle_calls), 0.0);
    delete bad_instance;
  }

  /* ----------------------------------------------------------- animation run */
  {
    animation = KX_MockAnimationAdapter();
    animation.frame = 42.0f;
    animation.normalized = 0.5f;
    animation.bone_supported = true;

    PlayGraph play;
    GraphIR play_ir = BuildPlayGraph(&play);
    KX_GraphInstance *play_instance = BuildInstance(play_ir, animation, physics);
    if (play_instance == nullptr) {
      std::printf("FAIL play graph did not compile\n");
      return 1;
    }
    play_instance->Tick(0.016f);

    Check(animation.play_layer_calls == 1 && animation.layer == 2 &&
              std::fabs(animation.blend_in - 0.3f) < 1e-5f,
          "PlayActionByLayer layer/blend_in");
    Check(animation.blend_calls == 1 && animation.blend_action_a == "run" &&
              animation.blend_action_b == "walk" &&
              std::fabs(animation.factor - 0.4f) < 1e-5f,
          "BlendActions actions/factor");
    Check(animation.bone_transform_calls == 1 && animation.bone == "spine" &&
              std::fabs(animation.bone_position[0] - 1.0f) < 1e-5f &&
              std::fabs(animation.bone_position[2] - 3.0f) < 1e-5f &&
              std::fabs(animation.bone_rotation[1] - 0.5f) < 1e-5f,
          "SetBoneTransform bone and transform");

    /* ------------------------------------------------- B6.10 additive/clear E2E */
    /* (1) mode omitted -> ConfigInt default 0: Override, so old graphs keep the legacy
     * behaviour (the adapter still receives exactly the same transform as before). */
    Check(animation.bone_mode == 0,
          "SetBoneTransform default blend mode is Override (legacy behaviour)",
          animation.bone_mode, 0.0);

    /* (2) mode=1 forwards Additive to the adapter without rewriting the delta. */
    {
      PlayGraph additive;
      GraphIR additive_ir = BuildPlayGraph(&additive);
      additive_ir.nodes[additive.bone_transform].config_json = "{\"mode\": 1}";
      animation.bone_transform_calls = 0;
      animation.bone_rotation[1] = 0.0f;
      animation.bone_mode = -1;
      KX_GraphInstance *instance = BuildInstance(additive_ir, animation, physics);
      if (instance == nullptr) {
        std::printf("FAIL additive graph did not compile\n");
        return 1;
      }
      instance->Tick(0.016f);
      Check(animation.bone_mode == 1 &&
                std::fabs(animation.bone_rotation[1] - 0.5f) < 1e-5f,
            "SetBoneTransform Additive mode forwards the delta quaternion",
            animation.bone_rotation[1], 0.5);

      /* (3) No accumulation: the override stores the user supplied delta only, so replaying
       * 60 frames must keep sending 0.5 rad instead of 30 rad (60 x 0.5). */
      animation.bone_transform_calls = 0;
      animation.bone_mode = -1;
      bool constant_delta = true;
      for (int frame = 0; frame < 60; ++frame) {
        instance->Tick(0.016f);
        if (std::fabs(animation.bone_rotation[1] - 0.5f) > 1e-5f) {
          constant_delta = false;
        }
      }
      Check(constant_delta && animation.bone_transform_calls == 60 &&
                animation.bone_mode == 1,
            "Additive delta stays 0.5 rad for 60 frames (no accumulation)",
            animation.bone_rotation[1], 0.5);
      delete instance;
    }

    /* (4) ClearBoneOverride reaches the adapter and removes the override. */
    {
      ClearGraph clear;
      GraphIR clear_ir = BuildClearGraph(&clear);
      animation.bone_clear_calls = 0;
      animation.bone_clear_result = false;
      animation.bone_clear_supported = true;
      KX_GraphInstance *instance = BuildInstance(clear_ir, animation, physics);
      if (instance == nullptr) {
        std::printf("FAIL clear graph did not compile\n");
        return 1;
      }
      instance->Tick(0.016f);
      Check(animation.bone_clear_calls == 1 && animation.bone_clear_name == "spine" &&
                animation.bone_clear_result,
            "ClearBoneOverride removes the pose override of the bone",
            static_cast<double>(animation.bone_clear_calls), 1.0);

      /* (5) Clearing a bone that has no override is a documented no-op: the node must not
       * crash and the adapter reports false. */
      animation.bone_clear_supported = false;
      animation.bone_clear_calls = 0;
      animation.bone_clear_result = true;
      instance->Tick(0.016f);
      Check(animation.bone_clear_calls == 1 && !animation.bone_clear_result,
            "clearing a bone without an override does not crash",
            static_cast<double>(animation.bone_clear_calls), 1.0);
      delete instance;
    }

    /* (6) The mode property is read as an int and forwarded verbatim (2 = Multiplicative is
     * reserved; the engine adapter rejects it with a diagnostic, the node does not rewrite it). */
    {
      PlayGraph multiplicative;
      GraphIR multiplicative_ir = BuildPlayGraph(&multiplicative);
      multiplicative_ir.nodes[multiplicative.bone_transform].config_json = "{\"mode\": 2}";
      animation.bone_transform_calls = 0;
      animation.bone_mode = -1;
      KX_GraphInstance *instance = BuildInstance(multiplicative_ir, animation, physics);
      if (instance == nullptr) {
        std::printf("FAIL multiplicative graph did not compile\n");
        return 1;
      }
      instance->Tick(0.016f);
      Check(animation.bone_mode == 2,
            "SetBoneTransform blend mode property round trips",
            animation.bone_mode, 2.0);
      delete instance;
    }

    SpeedGraph speed_graph;
    GraphIR speed_ir = BuildSpeedGraph(&speed_graph);
    KX_GraphInstance *speed_instance = BuildInstance(speed_ir, animation, physics);
    if (speed_instance == nullptr) {
      std::printf("FAIL speed graph did not compile\n");
      return 1;
    }
    animation.bone_read_rotation[0] = 0.25f;
    animation.bone_read_rotation[1] = 0.5f;
    animation.bone_read_rotation[2] = 0.75f;
    speed_instance->Tick(0.016f);

    Check(animation.speed_calls == 1 && std::fabs(animation.speed - 1.5f) < 1e-5f,
          "SetAnimationSpeed 1.5",
          animation.speed, 1.5);

    const KX_RuntimeValue *frame = ReadOutput(*speed_instance, speed_graph.get_frame, 0);
    const KX_RuntimeValue *normalized = ReadOutput(*speed_instance, speed_graph.get_frame, 1);
    Check(frame != nullptr && std::fabs(frame->AsFloat() - 42.0f) < 1e-5f,
          "GetAnimationFrame frame",
          frame != nullptr ? frame->AsFloat() : 0.0, 42.0);
    Check(normalized != nullptr && std::fabs(normalized->AsFloat() - 0.5f) < 1e-5f,
          "GetAnimationFrame normalized",
          normalized != nullptr ? normalized->AsFloat() : 0.0, 0.5);
    Check(animation.time_scale_calls == 1 &&
              std::fabs(animation.time_scale - 1.5f) < 1e-5f,
          "SetActionTimeScale reaches the adapter with its scale",
          animation.time_scale, 1.5);
    const KX_RuntimeValue *normalized_time =
        ReadOutput(*speed_instance, speed_graph.normalized_time, 0);
    Check(normalized_time != nullptr &&
              std::fabs(normalized_time->AsFloat() - 0.5f) < 1e-5f,
          "GetActionNormalizedTime reads the clamped playback position",
          normalized_time != nullptr ? normalized_time->AsFloat() : 0.0, 0.5);
    const KX_RuntimeValue *bone_rotation =
        ReadOutput(*speed_instance, speed_graph.bone_read, 1);
    Check(animation.bone_read_calls == 1 &&
              animation.bone_read_name == "spine" && bone_rotation != nullptr &&
              std::fabs(bone_rotation->AsFloat() - 0.25f) < 1e-5f,
          "GetBoneTransform reads back the pose channel rotation",
          bone_rotation != nullptr ? bone_rotation->AsFloat() : 0.0, 0.25);

    /* Engine limitation evidence: bone weight and per bone action are not
     * exposed by BL_ActionManager, the adapter must fail with a diagnostic
     * instead of faking success. */
    animation.bone_supported = false;
    LimitedGraph limited;
    GraphIR limited_ir = BuildLimitedGraph(&limited);
    KX_GraphInstance *limited_instance = BuildInstance(limited_ir, animation, physics);
    if (limited_instance == nullptr) {
      std::printf("FAIL limited graph did not compile\n");
      return 1;
    }
    animation.bone_weight_calls = 0;
    animation.bone_action_calls = 0;
    limited_instance->Tick(0.016f);
    Check(animation.bone_weight_calls == 1 && animation.bone_action_calls == 1,
          "unsupported bone weight/action still reach the adapter once",
          static_cast<double>(animation.bone_weight_calls), 1.0);
    std::printf("INFO unsupported paths printed engine limitation diagnostics above\n");

    /* C.4: the event must bind the action by name to its real layer and fire on the frame the
     * engine will render, not on the stale frame from the previous update. The same node must
     * re-arm when a looping action wraps. */
    animation.action_layers["run"] = 0;
    EventGraph event_graph;
    GraphIR event_ir = BuildEventGraph(&event_graph);
    KX_GraphInstance *event_instance = BuildInstance(event_ir, animation, physics);
    if (event_instance == nullptr) {
      std::printf("FAIL animation event graph did not compile\n");
      return 1;
    }
    auto event_count = [&]() -> int {
      const KX_RuntimeValue *value = ReadOutput(*event_instance, event_graph.counter, 0);
      return value != nullptr ? value->AsInt() : 0;
    };

    animation.frame_at_time = 0.0f;
    event_instance->Tick(0.016f);
    animation.frame_at_time = 2.0f;
    event_instance->Tick(0.016f);
    animation.frame_at_time = 4.0f;
    event_instance->Tick(0.016f);
    Check(event_count() == 0, "AddAnimationEvent does not fire before its frame",
          static_cast<double>(event_count()), 0.0);

    animation.frame_at_time = 5.0f;
    event_instance->Tick(0.016f);
    Check(event_count() == 1, "AddAnimationEvent fires on the event frame",
          static_cast<double>(event_count()), 1.0);

    animation.frame_at_time = 6.0f;
    event_instance->Tick(0.016f);
    animation.frame_at_time = 0.0f;
    event_instance->Tick(0.016f);
    animation.frame_at_time = 5.0f;
    event_instance->Tick(0.016f);
    Check(event_count() == 2, "AddAnimationEvent re-arms after a loop wraps",
          static_cast<double>(event_count()), 2.0);

    delete event_instance;
    delete limited_instance;
    delete speed_instance;
    delete play_instance;
  }

  std::printf("%s total failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
