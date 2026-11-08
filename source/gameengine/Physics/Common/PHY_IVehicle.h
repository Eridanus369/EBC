
/** \file PHY_IVehicle.h
 *  \ingroup phys
 */

#pragma once

// PHY_IVehicle provides a generic interface for (raycast based) vehicles. Mostly targetting 4
// wheel cars and 2 wheel motorbikes.

#include "PHY_DynamicTypes.h"

#include "MT_Quaternion.h"

class PHY_IMotionState;

class PHY_IVehicle {
 public:
  virtual ~PHY_IVehicle(){};

  /**
   * @param ownsMotionState true when the vehicle owns @a motionState and must
   *        delete it (legacy Python addWheel creates a fresh one), false when
   *        the caller keeps ownership (e.g. the wheel controller's own motion
   *        state). Deleting a non-owned motion state double-frees it when the
   *        controller is destroyed later.
   */
  virtual void AddWheel(PHY_IMotionState *motionState,
                        MT_Vector3 connectionPoint,
                        MT_Vector3 downDirection,
                        MT_Vector3 axleDirection,
                        float suspensionRestLength,
                        float wheelRadius,
                        bool hasSteering,
                        bool ownsMotionState = true) = 0;

  virtual int GetNumWheels() const = 0;

  virtual MT_Vector3 GetWheelPosition(int wheelIndex) const = 0;
  virtual MT_Quaternion GetWheelOrientationQuaternion(int wheelIndex) const = 0;
  virtual float GetWheelRotation(int wheelIndex) const = 0;

  virtual int GetUserConstraintId() const = 0;
  virtual int GetUserConstraintType() const = 0;

  // some basic steering/braking/tuning/balancing (bikes)

  virtual void SetSteeringValue(float steering, int wheelIndex) = 0;

  virtual void ApplyEngineForce(float force, int wheelIndex) = 0;

  virtual void ApplyBraking(float braking, int wheelIndex) = 0;

  virtual void SetWheelFriction(float friction, int wheelIndex) = 0;

  virtual void SetSuspensionStiffness(float suspensionStiffness, int wheelIndex) = 0;

  virtual void SetSuspensionDamping(float suspensionStiffness, int wheelIndex) = 0;

  virtual void SetSuspensionCompression(float suspensionStiffness, int wheelIndex) = 0;

  /** Per-wheel suspension rest length (the ray length used by the vehicle). */
  virtual void SetSuspensionRestLength(float suspensionRestLength, int wheelIndex) = 0;

  virtual void SetRollInfluence(float rollInfluence, int wheelIndex) = 0;

  virtual void SetCoordinateSystem(int rightIndex, int upIndex, int forwardIndex) = 0;

  virtual void SetRayCastMask(short mask) = 0;
  virtual short GetRayCastMask() const = 0;
};
