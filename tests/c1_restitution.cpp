/* C.1.1 E2E: runtime restitution must change the rebound of the same falling
 * body without rebuilding it. Numeric assertions only, no eyeballing.
 * Bullet combines restitution as a product, so the ground uses 1.0 and the
 * ball value alone decides the rebound. */
#include <btBulletDynamicsCommon.h>

#include <cmath>
#include <cstdio>

namespace {

struct DropResult {
  double peak_after_contact;
  double runtime_value_used;
};

DropResult DropBall(double initial_restitution, bool set_runtime, double runtime_value, int frames)
{
  btDefaultCollisionConfiguration config;
  btCollisionDispatcher dispatcher(&config);
  btDbvtBroadphase broadphase;
  btSequentialImpulseConstraintSolver solver;
  btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &config);
  world.setGravity(btVector3(0.0, -10.0, 0.0));

  btBoxShape ground_shape(btVector3(10.0, 0.5, 10.0));
  btRigidBody::btRigidBodyConstructionInfo ground_ci(0.0, nullptr, &ground_shape);
  ground_ci.m_restitution = 1.0;
  ground_ci.m_friction = 0.5;
  btRigidBody ground(ground_ci);
  ground.setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, -0.5, 0)));
  world.addRigidBody(&ground);

  btSphereShape ball_shape(0.5);
  btRigidBody::btRigidBodyConstructionInfo ball_ci(1.0, nullptr, &ball_shape);
  ball_ci.m_restitution = initial_restitution;
  ball_ci.m_friction = 0.5;
  btRigidBody ball(ball_ci);
  ball.setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 5, 0)));
  ball.setActivationState(DISABLE_DEACTIVATION);
  world.addRigidBody(&ball);

  double peak = -1.0e9;
  bool touched = false;
  const double dt = 1.0 / 120.0;
  for (int i = 0; i < frames; ++i) {
    /* Write the new value on the live body, well before the first impact. */
    if (set_runtime && i == 20) {
      ball.setRestitution(runtime_value);
    }
    world.stepSimulation(dt, 1);
    const double height = ball.getWorldTransform().getOrigin().y();
    if (!touched && height <= 0.51) {
      touched = true;
    }
    if (touched && height > peak) {
      peak = height;
    }
  }
  return {peak, set_runtime ? ball.getRestitution() : initial_restitution};
}

int Fail(const char *what, double got, double expected)
{
  std::printf("FAIL %s: got %.4f expected %.4f\n", what, got, expected);
  return 1;
}

}  // namespace

int main()
{
  constexpr int kFrames = 400;
  const DropResult dead = DropBall(0.0, false, 0.0, kFrames);
  const DropResult bouncy = DropBall(0.9, false, 0.0, kFrames);
  const DropResult runtime = DropBall(0.0, true, 0.9, kFrames);

  std::printf("INFO dead peak=%.4f bouncy peak=%.4f runtime peak=%.4f runtime value=%.4f\n",
              dead.peak_after_contact,
              bouncy.peak_after_contact,
              runtime.peak_after_contact,
              runtime.runtime_value_used);

  if (!(dead.peak_after_contact > 0.40 && dead.peak_after_contact < 0.70)) {
    return Fail("zero restitution should rest on the ground", dead.peak_after_contact, 0.5);
  }
  if (!(bouncy.peak_after_contact > dead.peak_after_contact + 1.0)) {
    return Fail("0.9 restitution should rebound clearly higher",
                bouncy.peak_after_contact,
                dead.peak_after_contact + 1.0);
  }
  if (!(std::fabs(runtime.peak_after_contact - bouncy.peak_after_contact) < 0.15)) {
    return Fail("runtime setRestitution must match a body built with 0.9",
                runtime.peak_after_contact,
                bouncy.peak_after_contact);
  }
  if (!(runtime.runtime_value_used > 0.89 && runtime.runtime_value_used < 0.91)) {
    return Fail("runtime restitution readback", runtime.runtime_value_used, 0.9);
  }
  std::printf("PASS C.1.1 restitution runtime change is measurable and in place\n");
  return 0;
}
