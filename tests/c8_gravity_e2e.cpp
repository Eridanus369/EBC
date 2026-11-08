/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2026 EBC.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file c8_gravity_e2e.cpp
 *  \ingroup tests
 *
 *  Node level E2E for SetGravity. Runs the real node against a mock
 *  KX_PhysicsAdapter that implements only GetGravity/SetGravity, then checks
 *  both the accepted values and the documented rejection of invalid ones.
 *
 *  The mock lives in this harness only. The engine keeps using
 *  KX_EnginePhysicsAdapter; nothing here is compiled into the runtime path.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "KX_CompiledGraph.h"
#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GraphInstance.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_RuntimeValue.h"

void RegisterB5FunctionGameNodes()
{
}

int failures = 0;

namespace {

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  std::printf("%s %s (actual=%g expected=%g)\n", ok ? "PASS" : "FAIL", what, actual, expected);
  if (!ok) {
    ++failures;
  }
}

/** Physics mock limited to the gravity surface SetGravity needs. */
class GravityPhysics : public KX_PhysicsAdapter {
 public:
  int set_calls = 0;
  float gravity[3] = {0.0f, 0.0f, 0.0f};

  void SetGravity(const float value[3]) override
  {
    ++set_calls;
    std::memcpy(gravity, value, sizeof(gravity));
  }

  void GetGravity(float out[3]) const override
  {
    std::memcpy(out, gravity, sizeof(gravity));
  }
};

class VectorNode : public KX_GameNodeRuntime {
 public:
  explicit VectorNode(float x, float y, float z) : x_(x), y_(y), z_(z) {}
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    OutputValue(ctx, 0) = KX_RuntimeValue::Vector(x_, y_, z_);
    return KX_ExecResult::Continue;
  }

 private:
  float x_, y_, z_;
};

int g_done_count = 0;

/** Counts SetGravity OnDone pulses so the harness can assert the trigger fires. */
class DoneCounterNode : public KX_GameNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    if (InputTrigger(ctx, 0)) {
      ++g_done_count;
    }
    return KX_ExecResult::Continue;
  }
};

KX_NodePortSignature Port(const char *name, IRPortKind kind, KX_ValueType type)
{
  KX_NodePortSignature port;
  port.name = name;
  port.kind = kind;
  port.value_type = type;
  return port;
}

void RegisterVector(const std::string &type_id, float x, float y, float z)
{
  KX_NodeSignature signature;
  signature.outputs.push_back(Port("Value", IRPortKind::Value, KX_VALUE_VECTOR));
  KX_NodeRegistry::Get().RegisterType(type_id,
                                      type_id + "Runtime",
                                      signature,
                                      [x, y, z]() {
                                        return std::unique_ptr<KX_GameNodeRuntime>(
                                            new VectorNode(x, y, z));
                                      });
}

void RegisterTestNodes()
{
  RegisterVector("C8Valid", 3.0f, -2.0f, 7.0f);
  RegisterVector("C8Bound", 1000.0f, -1000.0f, 0.0f);
  RegisterVector("C8Over", 1001.0f, 0.0f, 0.0f);
  RegisterVector("C8NaN", std::nanf(""), 0.0f, 0.0f);
  RegisterVector("C8Inf", INFINITY, 0.0f, 0.0f);
  RegisterVector("C8Huge", 1e30f, 0.0f, 0.0f);

  KX_NodeSignature counter;
  counter.inputs.push_back(Port("In", IRPortKind::Trigger, KX_VALUE_NONE));
  KX_NodeRegistry::Get().RegisterType("C8DoneCounter",
                                      "C8DoneCounterRuntime",
                                      counter,
                                      []() {
                                        return std::unique_ptr<KX_GameNodeRuntime>(
                                            new DoneCounterNode());
                                      });
}

int AddNode(GraphIR &ir, const std::string &type_id)
{
  const int index = static_cast<int>(ir.nodes.size());
  IRNode node;
  node.type_id = type_id;
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

void Connect(GraphIR &ir, int from, int from_port, int to, int to_port)
{
  IRConnection connection;
  connection.from_node = from;
  connection.from_port = from_port;
  connection.to_node = to;
  connection.to_port = to_port;
  ir.connections.push_back(connection);
}

/** Runs OnInit -> SetGravity with @a type_id feeding the Gravity port.
 *  When @a done is non-null, SetGravity.OnDone is wired to a counter and the
 *  pulse count is returned through it. */
void RunCase(const char *test_type, GravityPhysics *physics, int *done = nullptr)
{
  g_done_count = 0;
  GraphIR ir;
  const int on_init = AddNode(ir, "OnInit");
  const int set_gravity = AddNode(ir, "SetGravity");
  const int value = AddNode(ir, test_type);
  Connect(ir, on_init, 0, set_gravity, 0);
  Connect(ir, value, 0, set_gravity, 1);
  int counter = -1;
  if (done != nullptr) {
    counter = AddNode(ir, "C8DoneCounter");
    Connect(ir, set_gravity, 0, counter, 0);
  }

  KX_GraphInstance instance(nullptr, nullptr, nullptr);
  std::vector<std::string> warnings;
  if (!instance.Load(ir, &warnings)) {
    std::printf("FAIL %s could not load (warnings=%zu)\n", test_type, warnings.size());
    ++failures;
    return;
  }
  instance.GetContext()->SetAdapters(nullptr, nullptr, physics, nullptr, nullptr, nullptr,
                                     nullptr, nullptr, nullptr, nullptr, nullptr,
                                     nullptr);
  instance.Tick(0.01f);
  instance.Tick(0.01f);
  if (done != nullptr) {
    *done = g_done_count;
  }
}

void ExpectGravity(GravityPhysics &physics, float x, float y, float z, const char *what)
{
  float out[3];
  physics.GetGravity(out);
  Check(std::fabs(out[0] - x) < 1e-6f, what, out[0], x);
  Check(std::fabs(out[1] - y) < 1e-6f, what, out[1], y);
  Check(std::fabs(out[2] - z) < 1e-6f, what, out[2], z);
}

}  // namespace

int main()
{
  RegisterBuiltinGameNodes();
  RegisterFunctionGameNodes();
  RegisterTestNodes();

  {
    GravityPhysics physics;
    RunCase("C8Valid", &physics);
    Check(physics.set_calls == 1, "SetGravity called once", physics.set_calls, 1);
    ExpectGravity(physics, 3.0f, -2.0f, 7.0f, "valid gravity component");
    std::printf("GRAVITY valid = (%g,%g,%g)\n",
                double(physics.gravity[0]),
                double(physics.gravity[1]),
                double(physics.gravity[2]));
  }

  {
    GravityPhysics physics;
    RunCase("C8Bound", &physics);
    Check(physics.set_calls == 1, "boundary gravity accepted", physics.set_calls, 1);
    ExpectGravity(physics, 1000.0f, -1000.0f, 0.0f, "boundary gravity component");
    std::printf("GRAVITY bound = (%g,%g,%g)\n",
                double(physics.gravity[0]),
                double(physics.gravity[1]),
                double(physics.gravity[2]));
  }

  {
    GravityPhysics physics;
    physics.gravity[0] = 3.0f;
    physics.gravity[1] = -2.0f;
    physics.gravity[2] = 7.0f;
    int done = 0;
    RunCase("C8Over", &physics, &done);
    Check(physics.set_calls == 0, "1001 gravity rejected", physics.set_calls, 0);
    ExpectGravity(physics, 3.0f, -2.0f, 7.0f, "1001 keeps previous gravity");
    Check(done == 1, "rejected gravity still fires OnDone", done, 1);
  }

  {
    GravityPhysics physics;
    RunCase("C8NaN", &physics);
    Check(physics.set_calls == 0, "NaN gravity rejected", physics.set_calls, 0);
    ExpectGravity(physics, 0.0f, 0.0f, 0.0f, "NaN leaves gravity unchanged");
  }

  {
    GravityPhysics physics;
    RunCase("C8Inf", &physics);
    Check(physics.set_calls == 0, "inf gravity rejected", physics.set_calls, 0);
    ExpectGravity(physics, 0.0f, 0.0f, 0.0f, "inf leaves gravity unchanged");
  }

  {
    GravityPhysics physics;
    RunCase("C8Huge", &physics);
    Check(physics.set_calls == 0, "1e30 gravity rejected", physics.set_calls, 0);
    ExpectGravity(physics, 0.0f, 0.0f, 0.0f, "1e30 leaves gravity unchanged");
  }

  std::printf("%s total failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
