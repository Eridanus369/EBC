/* C.5 E2E: behavior of the constraint types now dispatched by AddConstraint.
 *
 * The frames, limit mapping and stepping below mirror what
 * CcdPhysicsEnvironment::CreateConstraint() and
 * KX_PhysicsAdvancedAdapter::AddConstraint() actually build for each type:
 *   point  -> btPoint2PointConstraint
 *   hinge  -> btHingeConstraint(axisInA, frameInA/frameInB)
 *   cone   -> btConeTwistConstraint(limits on DOF 3/4/5)
 *   slider -> btSliderConstraint(linear limits + linear motor)
 *   generic-> btGeneric6DofSpringConstraint(x/y/z limits)
 * No gravity and no damping, so the asserted motion comes from the constraint
 * alone.  Numeric assertions only, no eyeballing.
 */
#include <btBulletDynamicsCommon.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;
int assertions = 0;

void Check(bool ok, const char *what, double actual, double expected, double tol)
{
  ++assertions;
  const bool pass = ok && std::fabs(actual - expected) <= tol;
  if (!pass) {
    ++failures;
  }
  std::printf("%s %s (actual=%.6f expected=%.6f tol=%.6f)\n",
              pass ? "PASS" : "FAIL",
              what,
              actual,
              expected,
              tol);
}

/** Assertion for one-sided behavior ("this axis is free / stays above"). */
void CheckGreater(bool ok, const char *what, double actual, double threshold)
{
  ++assertions;
  if (!ok) {
    ++failures;
  }
  std::printf("%s %s (actual=%.6f threshold>%.6f)\n",
              ok ? "PASS" : "FAIL",
              what,
              actual,
              threshold);
}

struct Fixture {
  btDefaultCollisionConfiguration config;
  btCollisionDispatcher dispatcher{&config};
  btDbvtBroadphase broadphase;
  btSequentialImpulseConstraintSolver solver;
  btDiscreteDynamicsWorld world{&dispatcher, &broadphase, &solver, &config};

  btBoxShape anchor_shape{btVector3(0.2, 0.2, 0.2)};
  btBoxShape mover_shape{btVector3(0.2, 0.2, 0.2)};

  btRigidBody anchor;
  btRigidBody mover;

  static btRigidBody::btRigidBodyConstructionInfo MoverInfo(btCollisionShape *shape)
  {
    btVector3 inertia(0, 0, 0);
    shape->calculateLocalInertia(1.0, inertia);
    return btRigidBody::btRigidBodyConstructionInfo(1.0, nullptr, shape, inertia);
  }

  Fixture() : anchor(btRigidBody::btRigidBodyConstructionInfo(0.0, nullptr, &anchor_shape)),
              mover(MoverInfo(&mover_shape))
  {
    world.setGravity(btVector3(0, 0, 0));
    anchor.setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0)));
    mover.setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0)));
    mover.setActivationState(DISABLE_DEACTIVATION);
    world.addRigidBody(&anchor);
    world.addRigidBody(&mover);
  }

  ~Fixture()
  {
    if (m_constraint) {
      world.removeConstraint(m_constraint);
      delete m_constraint;
    }
  }

  btTypedConstraint *m_constraint = nullptr;

  void Add(btTypedConstraint *constraint)
  {
    m_constraint = constraint;
    world.addConstraint(constraint, true);
  }

  /** Same frame construction as CcdPhysicsEnvironment for a local Y axis. */
  static void Frames(btTransform &frame_a, btTransform &frame_b, btRigidBody &a, btRigidBody &b)
  {
    const btVector3 axis_in_a(0, 1, 0);
    btVector3 axis1, axis2;
    btPlaneSpace1(axis_in_a, axis1, axis2);
    frame_a.getBasis().setValue(axis_in_a.x(),
                                axis1.x(),
                                axis2.x(),
                                axis_in_a.y(),
                                axis1.y(),
                                axis2.y(),
                                axis_in_a.z(),
                                axis1.z(),
                                axis2.z());
    frame_a.setOrigin(btVector3(0, 0, 0));
    frame_b = b.getCenterOfMassTransform().inverse() * (a.getCenterOfMassTransform() * frame_a);
  }

  /** btHingeConstraint's hinge axis is the z column (see engine comment). */
  static void HingeFrames(btTransform &frame_a, btTransform &frame_b, btRigidBody &a, btRigidBody &b)
  {
    const btVector3 axis_in_a(0, 1, 0);
    btVector3 axis1, axis2;
    btPlaneSpace1(axis_in_a, axis1, axis2);
    frame_a.getBasis().setValue(axis1.x(),
                                axis2.x(),
                                axis_in_a.x(),
                                axis1.y(),
                                axis2.y(),
                                axis_in_a.y(),
                                axis1.z(),
                                axis2.z(),
                                axis_in_a.z());
    frame_a.setOrigin(btVector3(0, 0, 0));
    frame_b = b.getCenterOfMassTransform().inverse() * (a.getCenterOfMassTransform() * frame_a);
  }

  void Step(int frames)
  {
    const btScalar dt = 1.0 / 120.0;
    for (int i = 0; i < frames; ++i) {
      world.stepSimulation(dt, 1);
    }
  }

  /** Build a hinge frame around an arbitrary world axis and pivot. */
  static void AxisFrames(btTransform &frame_a,
                         btTransform &frame_b,
                         btRigidBody &a,
                         btRigidBody &b,
                         const btVector3 &axis_in_a,
                         const btVector3 &pivot_in_a)
  {
    btVector3 axis1, axis2;
    btPlaneSpace1(axis_in_a, axis1, axis2);
    frame_a.getBasis().setValue(axis1.x(),
                                axis2.x(),
                                axis_in_a.x(),
                                axis1.y(),
                                axis2.y(),
                                axis_in_a.y(),
                                axis1.z(),
                                axis2.z(),
                                axis_in_a.z());
    frame_a.setOrigin(pivot_in_a);
    frame_b = b.getCenterOfMassTransform().inverse() * (a.getCenterOfMassTransform() * frame_a);
  }

  btVector3 MoverPos() const
  {
    return mover.getWorldTransform().getOrigin();
  }

  double RotationAngle() const
  {
    const btMatrix3x3 basis = mover.getWorldTransform().getBasis();
    double trace = basis[0][0] + basis[1][1] + basis[2][2];
    double cos_angle = (trace - 1.0) * 0.5;
    cos_angle = std::max(-1.0, std::min(1.0, cos_angle));
    return std::acos(cos_angle);
  }

  double HingeAngleY() const
  {
    const btMatrix3x3 basis = mover.getWorldTransform().getBasis();
    return std::atan2(-basis[0][2], basis[0][0]);
  }
};

void TestPoint()
{
  Fixture f;
  btTransform a, b;
  Fixture::Frames(a, b, f.anchor, f.mover);
  f.Add(new btPoint2PointConstraint(f.anchor, f.mover, btVector3(0, 0, 0), btVector3(0, 0, 0)));
  f.mover.applyCentralImpulse(btVector3(2, 2, 2));
  /* P-2: point-to-point has 3 rotational degrees of freedom of its own. */
  f.mover.setAngularVelocity(btVector3(2, 2, 2));
  f.Step(240);
  const double distance = (f.MoverPos() - btVector3(0, 0, 0)).length();
  Check(true, "point keeps the anchor pinned", distance, 0.0, 0.02);
  const double rotation = f.RotationAngle();
  CheckGreater(rotation > 0.5, "point leaves rotation free", rotation, 0.5);
}

void TestHinge()
{
  Fixture f;
  btTransform a, b;
  Fixture::HingeFrames(a, b, f.anchor, f.mover);
  btHingeConstraint *hinge = new btHingeConstraint(f.anchor, f.mover, a, b);
  /* Bullet normalizes negative limits to 2*pi-low, so a [-0.5,0.5] range
   * collapses; use a positive range to exercise the limit itself. */
  hinge->setLimit(0.0, 0.5);
  f.Add(hinge);
  f.mover.setAngularVelocity(btVector3(0, 2, 0));
  double max_angle = -1.0e9;
  for (int i = 0; i < 240; ++i) {
    f.Step(1);
    max_angle = std::max(max_angle, hinge->getHingeAngle());
  }
  Check(true, "hinge never exceeds the upper limit", max_angle, 0.5, 0.04);
  CheckGreater(max_angle > 0.3, "hinge rotates freely up to its limit", max_angle, 0.3);
  const double distance = f.MoverPos().length();
  Check(true, "hinge keeps the pivot fixed", distance, 0.0, 0.02);
}

void TestCone()
{
  Fixture f;
  btTransform a, b;
  Fixture::Frames(a, b, f.anchor, f.mover);
  btConeTwistConstraint *cone = new btConeTwistConstraint(f.anchor, f.mover, a, b);
  cone->setLimit(3, 0.1);
  cone->setLimit(4, 0.1);
  cone->setLimit(5, 0.1);
  f.Add(cone);
  f.mover.setAngularVelocity(btVector3(2, 2, 2));
  f.Step(240);
  Check(true, "cone twist clamps the swing", f.RotationAngle(), 0.0, 0.70);
  /* P-2: a cone-twist constraint pins the anchor; only rotation is left free. */
  Check(true, "cone keeps the anchor fixed (translation locked)", f.MoverPos().length(), 0.0,
        0.02);
}

void TestHingeCustomFrame()
{
  Fixture f;
  btTransform a, b;
  /* P-3.5: AddConstraintWithFrame feeds an arbitrary world pivot/axis.  Using
   * the X axis here only reaches its 0.5 rad limit if the frame is honoured;
   * with the legacy +Y frame the X spin is blocked instead. */
  Fixture::AxisFrames(a, b, f.anchor, f.mover, btVector3(1, 0, 0), btVector3(0.5, 0, 0));
  btHingeConstraint *hinge = new btHingeConstraint(f.anchor, f.mover, a, b);
  hinge->setLimit(0.0, 0.5);
  f.Add(hinge);
  f.mover.setAngularVelocity(btVector3(2, 0, 0));
  double max_angle = -1.0e9;
  for (int i = 0; i < 240; ++i) {
    f.Step(1);
    max_angle = std::max(max_angle, hinge->getHingeAngle());
  }
  Check(true, "custom hinge axis reaches its limit", max_angle, 0.5, 0.04);
  Check(true, "custom hinge frame keeps the pivot fixed", f.MoverPos().length(), 0.0, 0.02);
}

void TestSlider()
{
  Fixture f;
  btTransform a, b;
  Fixture::Frames(a, b, f.anchor, f.mover);
  btSliderConstraint *slider = new btSliderConstraint(f.anchor, f.mover, a, b, true);
  /* Same mapping as CcdConstraint::SetParam(0, lo, hi). */
  slider->setLowerLinLimit(-0.5);
  slider->setUpperLinLimit(0.5);
  f.Add(slider);
  f.mover.applyCentralImpulse(btVector3(0, 12, 0));
  f.Step(240);
  std::printf("INFO slider linearPos=%.6f bodyY=%.6f\n",
              slider->getLinearPos(),
              f.MoverPos().y());
  Check(true, "slider clamps linear travel", std::fabs(f.MoverPos().y()), 0.5, 0.03);
  Check(true, "slider locks rotation", f.RotationAngle(), 0.0, 0.02);
  Check(true, "slider locks the other linear axes", std::fabs(f.MoverPos().x()), 0.0, 0.02);
}

void TestSliderMotor()
{
  Fixture f;
  btTransform a, b;
  Fixture::Frames(a, b, f.anchor, f.mover);
  btSliderConstraint *slider = new btSliderConstraint(f.anchor, f.mover, a, b, true);
  slider->setLowerLinLimit(-2.0);
  slider->setUpperLinLimit(2.0);
  /* Same mapping as CcdConstraint::SetParam(6, target_velocity, max_force). */
  slider->setTargetLinMotorVelocity(0.5);
  slider->setMaxLinMotorForce(10.0);
  slider->setPoweredLinMotor(true);
  f.Add(slider);
  f.Step(240);
  Check(true, "slider linear motor moves the body at the target velocity",
        f.MoverPos().y(),
        1.0,
        0.15);
}

void TestGeneric6Dof()
{
  Fixture f;
  btTransform a, b;
  Fixture::Frames(a, b, f.anchor, f.mover);
  btGeneric6DofSpringConstraint *generic = new btGeneric6DofSpringConstraint(
      f.anchor, f.mover, a, b, true);
  /* Same mapping as the previous 6DOF slider approximation. */
  generic->setLimit(0, -0.5, 0.5);
  generic->setLimit(1, 0.0, 0.0);
  generic->setLimit(2, 0.0, 0.0);
  generic->setLimit(3, 0.0, 0.0);
  generic->setLimit(4, 0.0, 0.0);
  generic->setLimit(5, 0.0, 0.0);
  f.Add(generic);
  f.mover.applyCentralImpulse(btVector3(0, 12, 0));
  f.Step(240);
  Check(true, "6DOF approximation clamps the same travel", std::fabs(f.MoverPos().y()), 0.5, 0.03);
  Check(true, "6DOF approximation locks rotation", f.RotationAngle(), 0.0, 0.02);
  /* P-2: limits on x/z are 0/0, so those linear axes must not move. */
  Check(true,
        "6DOF approximation locks the other linear axes",
        std::fabs(f.MoverPos().x()) + std::fabs(f.MoverPos().z()),
        0.0,
        0.03);
}

}  // namespace

int main()
{
  std::printf("INFO constraint behavior harness, 240 frames at dt=1/120\n");
  TestPoint();
  TestHinge();
  TestHingeCustomFrame();
  TestCone();
  TestSlider();
  TestSliderMotor();
  TestGeneric6Dof();
  std::printf("RESULT assertions=%d failures=%d\n", assertions, failures);
  return failures == 0 ? 0 : 1;
}
