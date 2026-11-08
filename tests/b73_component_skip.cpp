/*
 * B7.3 component-skip harness.
 *
 * Minimal "scene" at the object level: one KX_GameObject carrying
 *   - two legacy components whose PythonProxy module starts with "nl_"
 *   - one generic Python component (module "my_component")
 *
 * Calls the real KX_GameObject::Update() that ships in KX_GameObject.cpp and
 * checks that the legacy components are skipped (no Update callback) and the
 * generic one still runs. The warning is printed once for the object.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include <Python.h>

#include "EXP_ListValue.h"
#include "KX_GameObject.h"
#include "KX_PythonComponent.h"
#include "KX_PythonProxy.h"
#include "DNA_python_proxy_types.h"

using namespace blender;

class TrackingComponent : public KX_PythonComponent {
 public:
  explicit TrackingComponent(const std::string &name) : KX_PythonComponent(name) {}

  void Update() override
  {
    ++updates;
  }

  int updates = 0;
};

static PythonProxy MakePrototype(const char *module, const char *name)
{
  PythonProxy proto{};
  std::strncpy(proto.module, module, sizeof(proto.module) - 1);
  std::strncpy(proto.name, name, sizeof(proto.name) - 1);
  return proto;
}

int main()
{
  if (!Py_IsInitialized()) {
    Py_Initialize();
  }

  PythonProxy legacy_proto_a = MakePrototype("nl_legacytree", "LegacyTree");
  PythonProxy legacy_proto_b = MakePrototype("nl_othertree", "OtherTree");
  PythonProxy generic_proto = MakePrototype("my_component", "MyComponent");

  TrackingComponent *legacy_a = new TrackingComponent("LegacyTree");
  legacy_a->SetPrototype(&legacy_proto_a);
  TrackingComponent *legacy_b = new TrackingComponent("OtherTree");
  legacy_b->SetPrototype(&legacy_proto_b);
  TrackingComponent *generic = new TrackingComponent("MyComponent");
  generic->SetPrototype(&generic_proto);

  EXP_ListValue<KX_PythonComponent> *components = new EXP_ListValue<KX_PythonComponent>();
  components->Add(legacy_a);
  components->Add(generic);
  components->Add(legacy_b);

  KX_GameObject *object = new KX_GameObject();
  object->SetName("LegacyObject");
  object->SetComponents(components);

  object->Update();

  std::printf(
      "B73_RESULT legacy_a=%d legacy_b=%d generic=%d\n",
      legacy_a->updates,
      legacy_b->updates,
      generic->updates);

  const bool ok = (legacy_a->updates == 0) && (legacy_b->updates == 0) &&
                  (generic->updates == 1);
  std::printf("B73_OK %s\n", ok ? "true" : "false");
  return ok ? 0 : 1;
}
