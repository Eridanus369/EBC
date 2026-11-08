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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file BL_ArmatureObject.h
 *  \ingroup bgeconv
 */

#pragma once

#include "BKE_modifier.hh"
#include "BLI_array.hh"

#include "BL_ArmatureChannel.h"
#include "BL_ArmatureConstraint.h"
#include "KX_GameObject.h"

#include <cstdint>
#include <unordered_map>

namespace blender::gpu {
class Shader;
class StorageBuf;
}  // namespace blender::gpu
namespace blender { struct AnimationEvalContext; }
namespace blender { struct ID; } /* forward declare blender::ID for storing original data pointers */
class MT_Matrix4x4;
class BL_SceneConverter;
class RAS_DebugDraw;

/**
 * How a per-bone pose override registered by SetBoneTransform combines with the pose the
 * action pipeline produced for the current frame.
 *
 * Semantics (fixed, do not change without re-checking the runtime E2E):
 * 1. Rotation math is a quaternion product, never an euler addition. Euler addition would
 *    introduce gimbal lock; the override stores a target quaternion (Override) or a delta
 *    quaternion (Additive) and combines them with Quaternion::operator* (Hamilton product).
 * 2. The base is the pose evaluated *after* fcurve evaluation and layer blending and *before*
 *    any override is applied (see ApplyPoseOverrides()). It is not the previous frame's pose.
 * 3. Overrides are never accumulated: the map holds the user supplied target/delta only, and
 *    every frame recomputes the result from the action-evaluated base, so a node that fires
 *    once in OnInit does not drift over time.
 * 4. Entries are removable with ClearBoneOverride / ClearPoseOverride(); without that path a
 *    triggered override could never be undone.
 */
enum class KX_PoseBlendMode : uint8_t {
  /** Replace the channel with the target transform. Default and legacy behaviour. */
  Override = 0,
  /** result = current * delta, quaternion product in bone-local space. */
  Additive = 1,
  /** Reserved (P2): not implemented this round, rejected by the adapter. */
  Multiplicative = 2,
};

class BL_ArmatureObject : public KX_GameObject {

  Py_Header

  protected :
  /// List element: BL_ArmatureConstraint.
  EXP_ListValue<BL_ArmatureConstraint> *m_controlledConstraints;
  /// List element: BL_ArmatureChannel.
  EXP_ListValue<BL_ArmatureChannel> *m_poseChannels;
  blender::Object *m_objArma;
  /* Used to do the remapping between Parent (armature) and children
   * after ProcessReplica */
  blender::Object *m_previousArmature;

  /* Map of replica meshes created for specific child objects. */
  std::unordered_map<blender::Object *, blender::Mesh *> m_replicaMeshes;
  /* Store the original data pointers for replaced children so they can be restored on cleanup. */
  std::unordered_map<blender::Object *, blender::ID *> m_replacedOriginalData;

  double m_lastframe;
  size_t m_constraintNumber;
  size_t m_channelNumber;
  /// Store the original armature object matrix.
  float m_object_to_world[4][4];
  /// Set to true to allow draw debug info for one frame, reset in DrawDebugArmature.
  bool m_drawDebug;

  double m_lastapplyframe;

  /**
   * Per-bone pose override registered by SetBoneTransform. The action pipeline reapplies it
   * after fcurve evaluation and layer blending, so an animated channel cannot win the final
   * pose. Entries persist until ClearPoseOverride() removes them. See KX_PoseBlendMode for the
   * four fixed semantics (quaternion product, action-evaluated base, no per-frame accumulation,
   * removable).
   */
  struct PoseOverride {
    KX_PoseBlendMode mode = KX_PoseBlendMode::Override;
    /** Bone-local location: absolute target (Override) or delta (Additive). */
    float loc[3];
    /** Bone-local rotation in Blender order (w, x, y, z): target or delta quaternion. */
    float quat[4];
  };
  std::unordered_map<std::string, PoseOverride> m_poseOverrides;

  /**
   * Pose of each overridden bone as evaluated by the action pipeline for the current frame,
   * captured right before ApplyPoseOverrides(). Additive overrides combine against this base
   * instead of the (already overridden) channel value, which makes the result independent of
   * how often the same frame is evaluated and prevents cross-frame accumulation.
   */
  struct PoseOverrideBase {
    float loc[3];
    float quat[4];
  };
  std::unordered_map<std::string, PoseOverrideBase> m_poseOverrideBases;

 public:
  BL_ArmatureObject();
  virtual ~BL_ArmatureObject();

  virtual KX_PythonProxy *NewInstance();
  virtual void ProcessReplica();
  virtual int GetGameObjectType() const;
  virtual void ReParentLogic();
  virtual void Relink(std::map<SCA_IObject *, SCA_IObject *> &obj_map);
  virtual bool UnlinkObject(SCA_IObject *clientobj);

  double GetLastFrame();

  void GetPose(blender::bPose **pose) const;
  /// Never edit this, only for accessing names.
  blender::bPose *GetPose() const;
  void ApplyPose();
  void GameBlendPose(blender::bPose *dst, blender::bPose *src, float srcweight, short mode);
  void RemapParentChildren();
  void ApplyAction(blender::bAction *action, const blender::AnimationEvalContext &evalCtx, bool gpu_skinning);
  void BlendInPose(blender::bPose *blend_pose, float weight, short mode);

  bool UpdateTimestep(double curtime);

  blender::Object *GetArmatureObject();
  blender::Object *GetOrigArmatureObject();
  bool GetDrawDebug() const;
  void DrawDebug(RAS_DebugDraw &debugDraw);

  // for constraint python API
  void LoadConstraints(BL_SceneConverter *converter);
  size_t GetConstraintNumber() const;
  BL_ArmatureConstraint *GetConstraint(const std::string &posechannel,
                                       const std::string &constraint);
  BL_ArmatureConstraint *GetConstraint(const std::string &posechannelconstraint);
  BL_ArmatureConstraint *GetConstraint(int index);

  // for pose channel python API
  void LoadChannels();
  size_t GetChannelNumber() const;
  BL_ArmatureChannel *GetChannel(blender::bPoseChannel *channel);
  BL_ArmatureChannel *GetChannel(const std::string &channel);
  BL_ArmatureChannel *GetChannel(int index);

  /// Retrieve the pose matrix for the specified bone.
  /// Returns true on success.
  bool GetBoneMatrix(blender::Bone *bone, MT_Matrix4x4 &matrix);

  /// Returns the bone length. The end of the bone is in the local y direction.
  float GetBoneLength(blender::Bone *bone) const;

  virtual void SetBlenderObject(blender::Object *obj);

  /**
   * Recompute the armature pose for the current frame immediately and tag it for depsgraph
   * update. Unlike ApplyPose(), this bypasses the once-per-frame guard; called after a
   * GameNode writes a pose channel so the result can be used in the same frame.
   * Appended for the GameNode SetBoneTransform bridge.
   */
  void ApplyPoseImmediate();

  /**
   * Register/update a persistent pose override for @a bone. @a rotation is an XYZ euler as
   * authored by the node; it is converted to a quaternion immediately so the apply layer never
   * adds eulers. For KX_PoseBlendMode::Additive @a position/@a rotation are deltas relative to
   * the action-evaluated pose, otherwise they are absolute targets.
   * Appended for the GameNode SetBoneTransform bridge.
   */
  void SetPoseOverride(const std::string &bone,
                       KX_PoseBlendMode mode,
                       const float position[3],
                       const float rotation[3]);

  /**
   * Snapshot the action-evaluated pose of every overridden bone. Called by the action pipeline
   * after ApplyAction()/ProcessArmatureBlending() and before ApplyPoseOverrides(), so the
   * additive base is fixed per frame.
   */
  void CapturePoseOverrideBases();

  /**
   * Drop the override (and its captured base) of @a bone, restoring the pose channel to the
   * action-evaluated base so the bone is driven by the action again. Returns false when the
   * bone had no override (unknown bone / unknown object), which is a no-op and never crashes.
   */
  bool ClearPoseOverride(const std::string &bone);

  /**
   * Reapply every stored pose override and recompute the armature pose for this frame.
   * Returns false when no override matched a pose channel, so the caller can fall back to
   * ApplyPose().
   */
  bool ApplyPoseOverrides();

  /**
   * Read back the local transform of @a bone currently stored in the pose channel
   * (location + XYZ euler rotation). Used by the GameNode GetBoneTransform bridge and by the
   * runtime E2E checks that prove a manual pose survives a playing action.
   * Appended for the GameNode pose read-back bridge.
   */
  bool GetPoseChannelTransform(const std::string &bone, float position[3], float rotation[3]);

#ifdef WITH_PYTHON

  // PYTHON
  static PyObject *game_object_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

  static PyObject *pyattr_get_constraints(EXP_PyObjectPlus *self_v,
                                          const EXP_PYATTRIBUTE_DEF *attrdef);
  static PyObject *pyattr_get_channels(EXP_PyObjectPlus *self_v,
                                       const EXP_PYATTRIBUTE_DEF *attrdef);
  EXP_PYMETHOD_DOC_NOARGS(BL_ArmatureObject, update);
  EXP_PYMETHOD_DOC_NOARGS(BL_ArmatureObject, draw);

#endif /* WITH_PYTHON */
};
