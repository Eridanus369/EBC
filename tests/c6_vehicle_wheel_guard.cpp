/* C.6 vehicle wheel guard: no physics controller -> OnFail, never a crash.
 *
 * Two halves, both against the shipped code:
 *   1. KX_VehicleWheelIsUsable() is the admission check CreateVehicle runs
 *      before any wheel pointer reaches Bullet.  A wheel without a physics
 *      controller (No Collision, or a compound child that the converter never
 *      gave a controller) must be rejected with a diagnostic naming the
 *      object, the expected controller and what was actually found.
 *      CreateVehicle turns that rejection into -1, which the node turns into
 *      OnFail (KX_FunctionNodesB6.cpp, CreateVehicle handler).
 *   2. Real Bullet: the wheel motion state belongs to the wheel controller.
 *      The vehicle is torn down first (scene removal order); it must not free
 *      that motion state, otherwise the later ~CcdPhysicsController deletes
 *      it again (the Esc double free).
 */
#include <btBulletDynamicsCommon.h>
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "CcdPhysicsController.h"
#include "CcdPhysicsEnvironment.h"
#include "KX_PhysicsAdvancedAdapter.h"
#include "MT_Matrix3x3.h"
#include "MT_Vector3.h"
#include "PHY_IMotionState.h"
#include "PHY_IVehicle.h"

namespace {

int failures = 0;
int assertions = 0;

void Check(const char *what, bool pass)
{
  ++assertions;
  if (!pass) {
    ++failures;
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", what);
}

void CheckNear(const char *what, double actual, double expected)
{
  ++assertions;
  const bool pass = std::fabs(actual - expected) <= 1e-3;
  if (!pass) {
    ++failures;
  }
  std::printf("%s %s (actual=%.3f expected=%.3f)\n", pass ? "PASS" : "FAIL", what, actual, expected);
}

bool Contains(const std::string &text, const char *needle)
{
  return text.find(needle) != std::string::npos;
}

class TestMotionState : public PHY_IMotionState {
 public:
  MT_Vector3 GetWorldPosition() const override
  {
    return position_;
  }
  MT_Vector3 GetWorldScaling() const override
  {
    return MT_Vector3(1.0, 1.0, 1.0);
  }
  MT_Matrix3x3 GetWorldOrientation() const override
  {
    return orientation_;
  }
  void SetWorldPosition(const MT_Vector3 &pos) override
  {
    position_ = pos;
  }
  void SetWorldOrientation(const MT_Matrix3x3 &ori) override
  {
    orientation_ = ori;
  }
  void SetWorldOrientation(const MT_Quaternion &quat) override
  {
    orientation_.setRotation(quat);
  }
  void CalculateWorldTransformations() override
  {
  }

 private:
  MT_Vector3 position_ = MT_Vector3(0.0, 0.0, 0.0);
  MT_Matrix3x3 orientation_;
};

class TestController : public CcdPhysicsController {
 public:
  explicit TestController(const CcdConstructionInfo &ci) : CcdPhysicsController(ci)
  {
  }

  void Place(const btVector3 &position)
  {
    ForceWorldTransform(btMatrix3x3(btQuaternion(0, 0, 0, 1)), position);
  }
};

CcdPhysicsController *AddBody(CcdPhysicsEnvironment &env,
                              btCollisionShape *shape,
                              float mass,
                              const btVector3 &position)
{
  CcdConstructionInfo ci;
  ci.m_collisionShape = shape; /* the controller owns and deletes it */
  ci.m_MotionState = new TestMotionState();
  ci.m_mass = mass;
  ci.m_bDyna = (mass > 0.0f);
  ci.m_bRigid = true;
  /* BL_DataConversion sets this from Object.inertia, default 1.0. */
  ci.m_radius = 1.0f;
  ci.m_physicsEnv = &env;
  TestController *controller = new TestController(ci);
  controller->Place(position);
  env.AddCcdPhysicsController(controller);
  return controller;
}

/* Same setup as BL_SceneConverter: the body carries the shape info the engine
 * built it from, which is what the analytic shape override rebuilds from. */
CcdPhysicsController *AddShapeInfoBody(CcdPhysicsEnvironment &env,
                                       CcdShapeConstructionInfo *shape_info,
                                       float mass,
                                       const btVector3 &position)
{
  CcdConstructionInfo ci;
  ci.m_margin = 0.0f; /* exact half extents for the assertions below */
  ci.m_shapeInfo = shape_info;
  ci.m_collisionShape = shape_info->CreateBulletShape(ci.m_margin);
  ci.m_MotionState = new TestMotionState();
  ci.m_mass = mass;
  ci.m_bDyna = (mass > 0.0f);
  ci.m_bRigid = true;
  ci.m_radius = 1.0f;
  ci.m_physicsEnv = &env;
  TestController *controller = new TestController(ci);
  controller->Place(position);
  env.AddCcdPhysicsController(controller);
  return controller;
}

void GuardCases()
{
  KX_VehicleWheelInput valid;
  valid.name = "Wheel_FL";
  valid.parent_name = "Chassis";
  valid.source = "wheel_names";
  valid.has_controller = true;
  valid.has_motion_state = true;
  valid.controller_is_dynamic = true;
  valid.radius = 0.44f;
  valid.local[0] = -0.5f;
  valid.local[1] = 0.8f;
  valid.local[2] = -0.44f;

  std::string error;
  Check("a complete wheel is admitted", KX_VehicleWheelIsUsable(valid, &error));
  Check("an admitted wheel leaves the error empty", error.empty());

  /* The reported case: No Collision or a compound child has no controller. */
  KX_VehicleWheelInput missing = valid;
  missing.has_controller = false;
  missing.has_motion_state = false;
  missing.controller_is_dynamic = false;
  missing.radius = 0.0f;
  missing.is_compound_child = true;
  error.clear();
  Check("a wheel without a controller is rejected", !KX_VehicleWheelIsUsable(missing, &error));
  Check("the diagnostic names the wheel object", Contains(error, "'Wheel_FL'"));
  Check("the diagnostic states the expected type", Contains(error, "expected: physics controller"));
  Check("the diagnostic states what was found", Contains(error, "actual: none"));
  Check("the diagnostic explains the compound child", Contains(error, "compound_child=1"));
  Check("the diagnostic names the parent", Contains(error, "parent='Chassis'"));

  KX_VehicleWheelInput no_motion = valid;
  no_motion.has_motion_state = false;
  error.clear();
  Check("a controller without a motion state is rejected",
        !KX_VehicleWheelIsUsable(no_motion, &error) && Contains(error, "no motion state"));

  KX_VehicleWheelInput no_radius = valid;
  no_radius.radius = 0.0f;
  error.clear();
  Check("a wheel without a collision radius is rejected",
        !KX_VehicleWheelIsUsable(no_radius, &error) && Contains(error, "collision radius"));

  KX_VehicleWheelInput nan_local = valid;
  nan_local.local[1] = std::nanf("");
  error.clear();
  Check("a non-finite suspension anchor is rejected",
        !KX_VehicleWheelIsUsable(nan_local, &error) && Contains(error, "non-finite suspension anchor"));

  KX_VehicleWheelInput zero_down = valid;
  zero_down.down[0] = zero_down.down[1] = zero_down.down[2] = 0.0f;
  error.clear();
  Check("a zero suspension direction is rejected",
        !KX_VehicleWheelIsUsable(zero_down, &error) &&
            Contains(error, "degenerate suspension direction"));

  KX_VehicleWheelInput zero_axle = valid;
  zero_axle.axle[0] = zero_axle.axle[1] = zero_axle.axle[2] = 0.0f;
  error.clear();
  Check("a zero axle is rejected",
        !KX_VehicleWheelIsUsable(zero_axle, &error) && Contains(error, "degenerate axle"));
}

void TeardownCases()
{
  CcdPhysicsEnvironment env{PHY_SOLVER_SEQUENTIAL};
  CcdPhysicsController *chassis = AddBody(
      env, new btBoxShape(btVector3(0.5, 1.0, 0.2)), 3.0f, btVector3(0, 0, 0.6));
  CcdPhysicsController *wheel = AddBody(
      env, new btBoxShape(btVector3(0.4, 0.4, 0.4)), 0.5f, btVector3(-0.5, -0.8, 0.16));
  TestMotionState *wheel_ms = static_cast<TestMotionState *>(wheel->GetMotionState());

  /* The old source of the flying vehicle: Object.radius defaults to 1.0
   * regardless of the box size. The shape radius is the real 0.4. */
  CheckNear("legacy GetRadius() is the unrelated Object.radius default",
            chassis->GetRadius(),
            1.0);
  CheckNear("GetShapeBoundingRadius() reports the box half extent", wheel->GetShapeBoundingRadius(), 0.40);

  PHY_IVehicle *vehicle = env.CreateVehicle(chassis);
  Check("the environment creates a vehicle", vehicle != nullptr);
  if (vehicle != nullptr) {
    vehicle->AddWheel(wheel->GetMotionState(),
                      MT_Vector3(-0.5, -0.8, -0.44),
                      MT_Vector3(0, 0, -1),
                      MT_Vector3(1, 0, 0),
                      0.5f * wheel->GetShapeBoundingRadius(),
                      wheel->GetShapeBoundingRadius(),
                      true,
                      false);
  }
  Check("the vehicle accepted one wheel", vehicle != nullptr && vehicle->GetNumWheels() == 1);

  /* Scene destruction order: the chassis (and its vehicle) goes first. */
  delete chassis;
  Check("the vehicle constraint is gone with the chassis",
        env.GetDynamicsWorld()->getNumConstraints() == 0);

  /* The wheel motion state survived: the vehicle did not own it. */
  wheel_ms->SetWorldPosition(MT_Vector3(1.0, 2.0, 3.0));
  CheckNear("wheel motion state is still writable after teardown",
            wheel_ms->GetWorldPosition().x(),
            1.0);

  /* The wheel controller still owns (and now frees) its motion state once. */
  delete wheel;
}

/* The vehicle node defaults: a body the converter left on its unit-sphere
 * fallback must be replaceable in place by the box/cylinder that matches the
 * mesh, which is what keeps the chassis on the ground and the suspension ray
 * at the real wheel length. */
void ShapeOverrideCases()
{
  CcdPhysicsEnvironment env{PHY_SOLVER_SEQUENTIAL};

  CcdShapeConstructionInfo *chassis_info = new CcdShapeConstructionInfo();
  chassis_info->m_shapeType = PHY_SHAPE_SPHERE;
  chassis_info->m_radius = 1.0f;
  CcdPhysicsController *chassis = AddShapeInfoBody(
      env, chassis_info, 800.0f, btVector3(0.0, 0.0, 1.0));
  chassis_info->Release(); /* the controller holds its own reference */

  Check("the fallback chassis starts as a sphere",
        chassis->GetRigidBody()->getCollisionShape()->getShapeType() ==
            SPHERE_SHAPE_PROXYTYPE);
  const float chassis_half_extents[3] = {1.0f, 0.5f, 0.5f};
  Check("the node can replace the fallback with a box",
        chassis->SetAnalyticCollisionShape(PHY_SHAPE_BOX, chassis_half_extents));
  Check("the chassis shape is now a box",
        chassis->GetRigidBody()->getCollisionShape()->getShapeType() ==
            BOX_SHAPE_PROXYTYPE);
  /* Sphere inertia (2/5 m r^2 = 320) vs box inertia (m/3 (hy^2 + hz^2) =
   * 133.3): the override must refresh the tensor, not just the shape. */
  const btVector3 chassis_inv_inertia = chassis->GetRigidBody()->getInvInertiaDiagLocal();
  CheckNear("the box inertia replaced the sphere inertia",
            chassis_inv_inertia.x(),
            3.0 / (800.0 * (0.25 + 0.25)));

  CcdShapeConstructionInfo *wheel_info = new CcdShapeConstructionInfo();
  wheel_info->m_shapeType = PHY_SHAPE_SPHERE;
  wheel_info->m_radius = 1.0f;
  CcdPhysicsController *wheel = AddShapeInfoBody(
      env, wheel_info, 40.0f, btVector3(-0.5, -0.8, 0.16));
  wheel_info->Release();

  const float wheel_half_extents[3] = {0.35f, 0.35f, 0.1f};
  Check("the node can replace a wheel sphere with a cylinder",
        wheel->SetAnalyticCollisionShape(PHY_SHAPE_CYLINDER, wheel_half_extents));
  Check("the wheel shape is now a cylinder",
        wheel->GetRigidBody()->getCollisionShape()->getShapeType() ==
            CYLINDER_SHAPE_PROXYTYPE);
  CheckNear("the wheel bounds report the 0.35 m mesh radius, not the 1.0 m fallback",
            wheel->GetShapeBoundingRadius(),
            0.35);

  delete chassis;
  delete wheel;
}

}  // namespace

int main()
{
  GuardCases();
  TeardownCases();
  ShapeOverrideCases();

  std::printf("\n%s assertions=%d failures=%d\n",
              failures == 0 ? "ALL PASS" : "FAILURES",
              assertions,
              failures);
  return failures == 0 ? 0 : 1;
}
