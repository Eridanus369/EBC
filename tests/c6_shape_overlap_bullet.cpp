/* C.6 shape overlap: real Bullet contact tests.
 *
 * The node-level harness (c6_shape_overlap.cpp) verifies the adapter contract
 * with an exact reference backend.  This harness covers the other half:
 * CcdPhysicsEnvironment::TestShapeContact / TestControllerContact against a
 * real btCollisionWorld, without going through KX_GameAdaptersEngine.cpp.
 *
 * The two rods below are the cases the sensor change exists for:
 *   - origins 3.0 apart, 4 x 0.2 x 0.2 boxes that overlap -> contact
 *   - origins 0.5 apart, shifted in z so the boxes are apart -> no contact
 * If only one of them passes, the implementation merely changed a threshold
 * instead of testing shapes.
 */
#include <btBulletDynamicsCommon.h>
#include <BulletSoftBody/btSoftRigidDynamicsWorld.h>

#include <cmath>
#include <cstdio>

#include "CcdPhysicsController.h"
#include "CcdPhysicsEnvironment.h"
#include "MT_Matrix3x3.h"
#include "MT_Vector3.h"
#include "PHY_IMotionState.h"

namespace {

int failures = 0;
int assertions = 0;

void Check(const char *what, double actual, double expected)
{
  ++assertions;
  const bool pass = std::fabs(actual - expected) <= 1e-6;
  if (!pass) {
    ++failures;
  }
  std::printf("%s %s (actual=%.0f expected=%.0f)\n",
              pass ? "PASS" : "FAIL",
              what,
              actual,
              expected);
}

/** Minimal motion state so CcdPhysicsController never dereferences null. */
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

/** ForceWorldTransform is protected, so place bodies through a subclass. */
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

struct Fixture {
  CcdPhysicsEnvironment env{PHY_SOLVER_SEQUENTIAL};
  CcdPhysicsController *controller_a = nullptr;
  CcdPhysicsController *controller_b = nullptr;

  CcdPhysicsController *AddBody(btCollisionShape *shape, const btVector3 &position)
  {
    CcdConstructionInfo ci;
    ci.m_collisionShape = shape; /* the controller owns and deletes it */
    ci.m_MotionState = new TestMotionState();
    ci.m_mass = 0.0f;
    ci.m_bRigid = true;
    ci.m_physicsEnv = &env;
    TestController *controller = new TestController(ci);
    controller->Place(position);
    env.AddCcdPhysicsController(controller);
    return controller;
  }
};

int ManifoldCount(CcdPhysicsEnvironment &env)
{
  return env.GetDynamicsWorld()->getDispatcher()->getNumManifolds();
}

/** Box query at `position` against an existing controller (or nullptr). */
bool BoxQuery(CcdPhysicsEnvironment &env,
              const btVector3 &half,
              const btVector3 &position,
              PHY_IPhysicsController *other)
{
  PHY_ShapeDesc desc;
  desc.kind = PHY_ShapeDesc::PHY_SHAPE_BOX;
  desc.half_extents[0] = half.getX();
  desc.half_extents[1] = half.getY();
  desc.half_extents[2] = half.getZ();
  MT_Vector3 origin(position.getX(), position.getY(), position.getZ());
  MT_Matrix3x3 orientation;
  orientation.setIdentity();
  return env.TestShapeContact(desc, origin, orientation, other);
}

bool SphereQuery(CcdPhysicsEnvironment &env,
                 float radius,
                 const btVector3 &position,
                 PHY_IPhysicsController *other)
{
  PHY_ShapeDesc desc;
  desc.kind = PHY_ShapeDesc::PHY_SHAPE_SPHERE;
  desc.radius = radius;
  MT_Vector3 origin(position.getX(), position.getY(), position.getZ());
  MT_Matrix3x3 orientation;
  orientation.setIdentity();
  return env.TestShapeContact(desc, origin, orientation, other);
}

}  // namespace

int main()
{
  const btVector3 rod_half(2.0, 0.1, 0.1);

  /* Scenario 1: origins 3.0 apart, shapes overlap on x ([1, 2]). */
  Fixture rods;
  rods.controller_a = rods.AddBody(new btBoxShape(rod_half), btVector3(0, 0, 0));
  rods.controller_b = rods.AddBody(new btBoxShape(rod_half), btVector3(3, 0, 0));
  Check("long rods overlapping by shape report contact",
        BoxQuery(rods.env, rod_half, btVector3(3, 0, 0), rods.controller_a) ? 1 : 0,
        1);
  Check("controller pair with overlapping shapes reports contact",
        rods.env.TestControllerContact(rods.controller_a, rods.controller_b) ? 1 : 0,
        1);

  /* Scenario 2: origins 0.5 apart, z shifted by 0.5 so there is a gap. */
  Fixture thin;
  thin.controller_a = thin.AddBody(new btBoxShape(rod_half), btVector3(0, 0, 0));
  thin.controller_b = thin.AddBody(new btBoxShape(rod_half), btVector3(0.5, 0, 0.5));
  Check("thin rods with separated shapes report no contact",
        BoxQuery(thin.env, rod_half, btVector3(0.5, 0, 0.5), thin.controller_a) ? 1 : 0,
        0);
  Check("controller pair with separated shapes report no contact",
        thin.env.TestControllerContact(thin.controller_a, thin.controller_b) ? 1 : 0,
        0);

  /* Scenario 3/4: spheres, radius sum 1.0. */
  Fixture spheres;
  spheres.controller_a = spheres.AddBody(new btSphereShape(0.5), btVector3(0, 0, 0));
  spheres.controller_b = spheres.AddBody(new btSphereShape(0.5), btVector3(1.1, 0, 0));
  Check("sphere pair beyond the radius sum reports no contact",
        SphereQuery(spheres.env, 0.5f, btVector3(1.1, 0, 0), spheres.controller_a) ? 1 : 0,
        0);
  Check("sphere pair inside the radius sum reports contact",
        SphereQuery(spheres.env, 0.5f, btVector3(0.9, 0, 0), spheres.controller_a) ? 1 : 0,
        1);

  /* Scenario 5: no collider -> false, the caller falls back to origin distance. */
  Check("shape query without a collider returns no contact",
        SphereQuery(spheres.env, 0.5f, btVector3(0.9, 0, 0), nullptr) ? 1 : 0,
        0);
  Check("controller query without a collider returns no contact",
        rods.env.TestControllerContact(nullptr, rods.controller_a) ? 1 : 0,
        0);

  /* Scenario 6: repeated calls stay stable and leave no manifolds behind. */
  const bool first = rods.env.TestControllerContact(rods.controller_a, rods.controller_b);
  const bool second = rods.env.TestControllerContact(rods.controller_a, rods.controller_b);
  Check("repeated contact calls on the same world stay stable",
        (first ? 1 : 0) + (second ? 1 : 0),
        2);
  Check("contact pair tests leave no persistent manifolds",
        ManifoldCount(rods.env),
        0);

  std::printf("\n%s assertions=%d failures=%d\n",
              failures == 0 ? "ALL PASS" : "FAILURES",
              assertions,
              failures);
  return failures == 0 ? 0 : 1;
}
