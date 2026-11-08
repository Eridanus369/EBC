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

/** \file gameengine/Converter/BL_ArmatureObject.cpp
 *  \ingroup bgeconv
 */

#include "BL_ArmatureObject.h"

#include "ANIM_action.hh"
#include "BKE_armature.hh"
#include "BKE_constraint.h"
#include "BKE_context.hh"
#include "BKE_deform.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_mesh.hh"
#include "BKE_scene.hh"
#include "BLI_listbase.hh"
#include "BLI_math_matrix_c.hh"
#include "BLI_math_quaternion_types.hh"
#include "BLI_math_rotation_c.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "RNA_access.hh"

#include "BL_Action.h"
#include "BL_SceneConverter.h"
#include "KX_Globals.h"

using namespace blender;

// Only allowed for Poses with identical channels.
void BL_ArmatureObject::GameBlendPose(blender::bPose *dst, blender::bPose *src, float srcweight, short mode)
{
  float dstweight;

  if (mode == BL_Action::ACT_BLEND_BLEND) {
    dstweight = 1.0f - srcweight;
  }
  else if (mode == BL_Action::ACT_BLEND_ADD) {
    dstweight = 1.0f;
  }
  else {
    dstweight = 1.0f;
  }

  blender::bPoseChannel *schan = src->chanbase.first();
  for (blender::bPoseChannel *dchan = dst->chanbase.first(); dchan;
       dchan = dchan->next, schan = schan->next)
  {
    // always blend on all channels since we don't know which one has been set
    /* quat interpolation done separate */
    if (schan->rotmode == ROT_MODE_QUAT) {
      float dquat[4], squat[4];

      copy_qt_qt(dquat, dchan->quat);
      copy_qt_qt(squat, schan->quat);
      // Normalize quaternions so that interpolation/multiplication result is correct.
      normalize_qt(dquat);
      normalize_qt(squat);

      if (mode == BL_Action::ACT_BLEND_BLEND) {
        interp_qt_qtqt(dchan->quat, dquat, squat, srcweight);
      }
      else {
        pow_qt_fl_normalized(squat, srcweight);
        mul_qt_qtqt(dchan->quat, dquat, squat);
      }

      normalize_qt(dchan->quat);
    }

    for (unsigned short i = 0; i < 3; i++) {
      /* blending for loc and scale are pretty self-explanatory... */
      dchan->loc[i] = (dchan->loc[i] * dstweight) + (schan->loc[i] * srcweight);
      dchan->scale[i] = 1.0f + ((dchan->scale[i] - 1.0f) * dstweight) +
                        ((schan->scale[i] - 1.0f) * srcweight);

      /* euler-rotation interpolation done here instead... */
      // FIXME: are these results decent?
      if (schan->rotmode) {
        dchan->eul[i] = (dchan->eul[i] * dstweight) + (schan->eul[i] * srcweight);
      }
    }
    for (blender::bConstraint *dcon = dchan->constraints.first(),
                     *scon = schan->constraints.first();
         dcon && scon;
         dcon = dcon->next, scon = scon->next)
    {
      /* no 'add' option for constraint blending */
      dcon->enforce = dcon->enforce * (1.0f - srcweight) + scon->enforce * srcweight;
    }
  }

  /* this pose is now in src time */
  dst->ctime = src->ctime;
}

BL_ArmatureObject::BL_ArmatureObject()
    : KX_GameObject(), m_lastframe(0.0), m_drawDebug(false), m_lastapplyframe(0.0)
{
  m_controlledConstraints = new EXP_ListValue<BL_ArmatureConstraint>();
  m_poseChannels = nullptr;
  m_previousArmature = nullptr;
  // m_replicaMeshes default constructed
}

BL_ArmatureObject::~BL_ArmatureObject()
{
  m_poseChannels->Release();
  m_poseChannels = nullptr;
  m_controlledConstraints->Release();

  if (!m_replicaMeshes.empty() && m_isReplica) {
    blender::bContext *C = KX_GetActiveEngine()->GetContext();
    blender::Main *bmain = C ? CTX_data_main(C) : nullptr;

    /* Restore original data pointers first */
    for (auto &item : m_replacedOriginalData) {
      blender::Object *child_ob = item.first;
      blender::ID *orig_data = item.second;
      if (child_ob && orig_data) {
        child_ob->data = orig_data;
      }
    }

    /* Delete replicas meshes */
    for (auto &item : m_replicaMeshes) {
      blender::Mesh *replica = item.second;
      if (replica) {
        /* Delete the mesh from blender::Main database */
        BKE_id_delete(bmain, (blender::ID *)replica);
      }
    }

    m_replicaMeshes.clear();
    m_replacedOriginalData.clear();

    /* Update depsgraph after cleanup (same logic than in KX_GameObject::DiscardRenderedObject) */
    DEG_relations_tag_update(bmain);
  }
}

void BL_ArmatureObject::SetBlenderObject(blender::Object *obj)
{
  KX_GameObject::SetBlenderObject(obj);
  m_objArma = obj;

  if (m_objArma) {
    memcpy(m_object_to_world, m_objArma->object_to_world().ptr(), sizeof(m_object_to_world));
    LoadChannels();
  }
}

void BL_ArmatureObject::RemapParentChildren()
{
  /* When an armature is replicated, child objects that had an armature modifier
   * pointing to the original armature must be remapped to point to the new replica. */
  for (KX_GameObject *child : GetChildren()) {
    blender::Object *child_ob = child->GetBlenderObject();
    if (!child_ob) {
      continue;
    }
    for (ModifierData *md = child_ob->modifiers.first(); md; md = md->next) {
      if (md->type == eModifierType_Armature) {
        ArmatureModifierData *amd = (ArmatureModifierData *)md;
        if (amd && amd->object == m_previousArmature) {
          amd->object = m_objArma;
          bool is_using_gpu_deform = amd->deform_method & ARM_DEFORM_METHOD_GPU && !m_isUpbgeDupliInstance &&
                                     !child->IsUpbgeDupliInstance();
          if (child->IsReplica() && is_using_gpu_deform) {
            if (m_replicaMeshes.find(child_ob) == m_replicaMeshes.end()) {
              blender::bContext *C = KX_GetActiveEngine()->GetContext();
              blender::Main *bmain = CTX_data_main(C);

              /* Cache the replica mesh data to avoid multiple lookups. */
              blender::Mesh *orig = (blender::Mesh *)child_ob->data;
              blender::Mesh *replica = (blender::Mesh *)BKE_id_copy_ex(bmain, (blender::ID *)orig, nullptr, 0);

              child_ob->data = id_cast<ID *>(replica);

              /* Store original data pointer to restore it later. */
              if (m_replacedOriginalData.find(child_ob) == m_replacedOriginalData.end()) {
                m_replacedOriginalData[child_ob] = (blender::ID *)orig;
              }
              DEG_id_tag_update(&child_ob->id, ID_RECALC_GEOMETRY);
              DEG_relations_tag_update(bmain);

              m_replicaMeshes[child_ob] = replica;
            }
          }
        }
      }
    }
  }
}

void BL_ArmatureObject::LoadConstraints(BL_SceneConverter *converter)
{
  // first delete any existing constraint (should not have any)
  m_controlledConstraints->ReleaseAndRemoveAll();

  // list all the constraint and convert them to BL_ArmatureConstraint
  // get the persistent pose structure

  // and locate the constraint
  for (blender::bPoseChannel *pchan = m_objArma->pose->chanbase.first(); pchan;
       pchan = pchan->next) {
    for (blender::bConstraint *pcon = pchan->constraints.first(); pcon; pcon = pcon->next) {
      if (pcon->flag & CONSTRAINT_DISABLE) {
        continue;
      }
      // which constraint should we support?
      switch (pcon->type) {
        case CONSTRAINT_TYPE_TRACKTO:
        case CONSTRAINT_TYPE_DAMPTRACK:
        case CONSTRAINT_TYPE_KINEMATIC:
        case CONSTRAINT_TYPE_ROTLIKE:
        case CONSTRAINT_TYPE_LOCLIKE:
        case CONSTRAINT_TYPE_MINMAX:
        case CONSTRAINT_TYPE_SIZELIKE:
        case CONSTRAINT_TYPE_LOCKTRACK:
        case CONSTRAINT_TYPE_STRETCHTO:
        case CONSTRAINT_TYPE_CLAMPTO:
        case CONSTRAINT_TYPE_TRANSFORM:
        case CONSTRAINT_TYPE_DISTLIMIT:
        case CONSTRAINT_TYPE_TRANSLIKE: {
          const bConstraintTypeInfo *cti = BKE_constraint_typeinfo_get(pcon);
          KX_GameObject *gametarget = nullptr;
          KX_GameObject *gamesubtarget = nullptr;
          if (cti && cti->get_constraint_targets) {
            ListBaseT <bConstraintTarget> listb = {nullptr, nullptr};
            cti->get_constraint_targets(pcon, &listb);
            if (listb.first()) {
              bConstraintTarget *target = listb.first();
              if (target->tar && target->tar != m_objArma) {
                // only remember external objects, self target is handled automatically
                gametarget = converter->FindGameObject(target->tar);
              }
              if (target->next != nullptr) {
                // secondary target
                target = target->next;
                if (target->tar && target->tar != m_objArma) {
                  // only track external object
                  gamesubtarget = converter->FindGameObject(target->tar);
                }
              }
            }
            if (cti->flush_constraint_targets) {
              cti->flush_constraint_targets(pcon, &listb, 1);
            }
          }
          BL_ArmatureConstraint *constraint = new BL_ArmatureConstraint(
              this, pchan, pcon, gametarget, gamesubtarget);
          m_controlledConstraints->Add(constraint);
        }
        default:
          break;
      }
    }
  }

  // If we have constraints, make sure we get treated as an "animated" object
  if (m_controlledConstraints->GetCount() > 0) {
    GetActionManager();
  }
}

size_t BL_ArmatureObject::GetConstraintNumber() const
{
  return m_controlledConstraints->GetCount();
}

BL_ArmatureConstraint *BL_ArmatureObject::GetConstraint(const std::string &posechannel,
                                                        const std::string &constraintname)
{
  return m_controlledConstraints->FindIf(
      [&posechannel, &constraintname](BL_ArmatureConstraint *constraint) {
        return constraint->Match(posechannel, constraintname);
      });
}

BL_ArmatureConstraint *BL_ArmatureObject::GetConstraint(const std::string &posechannelconstraint)
{
  return static_cast<BL_ArmatureConstraint *>(
      //    m_controlledConstraints->FindValue(posechannelconstraint));
      m_controlledConstraints->FindValue(posechannelconstraint));
}

BL_ArmatureConstraint *BL_ArmatureObject::GetConstraint(int index)
{
  return static_cast<BL_ArmatureConstraint *>(m_controlledConstraints->GetValue(index));
}

/* this function is called to populate the m_poseChannels list */
void BL_ArmatureObject::LoadChannels()
{
  m_poseChannels = new EXP_ListValue<BL_ArmatureChannel>();
  for (blender::bPoseChannel *pchan = m_objArma->pose->chanbase.first(); pchan;
       pchan = pchan->next) {
    BL_ArmatureChannel *channel = new BL_ArmatureChannel(this, pchan);
    m_poseChannels->Add(channel);
  }
}

size_t BL_ArmatureObject::GetChannelNumber() const
{
  return m_poseChannels->GetCount();
}

BL_ArmatureChannel *BL_ArmatureObject::GetChannel(blender::bPoseChannel *pchan)
{
  return m_poseChannels->FindIf(
      [&pchan](BL_ArmatureChannel *channel) { return channel->m_posechannel == pchan; });
}

BL_ArmatureChannel *BL_ArmatureObject::GetChannel(const std::string &str)
{
  return static_cast<BL_ArmatureChannel *>(m_poseChannels->FindValue(str));
}

BL_ArmatureChannel *BL_ArmatureObject::GetChannel(int index)
{
  if (index < 0 || index >= m_poseChannels->GetCount()) {
    return nullptr;
  }
  return static_cast<BL_ArmatureChannel *>(m_poseChannels->GetValue(index));
}

KX_PythonProxy *BL_ArmatureObject::NewInstance()
{
  return new BL_ArmatureObject(*this);
}

void BL_ArmatureObject::ProcessReplica()
{
  m_previousArmature = m_objArma;

  KX_GameObject::ProcessReplica();

  // Replicate each constraints.
  m_controlledConstraints = static_cast<EXP_ListValue<BL_ArmatureConstraint> *>(
  // m_controlledConstraints->GetReplica());
  m_controlledConstraints->GetReplica());

  m_objArma = m_pBlenderObject;

  LoadChannels();
}

int BL_ArmatureObject::GetGameObjectType() const
{
  return OBJ_ARMATURE;
}

void BL_ArmatureObject::ReParentLogic()
{
  for (BL_ArmatureConstraint *constraint : m_controlledConstraints) {
    constraint->ReParent(this);
  }
  KX_GameObject::ReParentLogic();
}

void BL_ArmatureObject::Relink(std::map<SCA_IObject *, SCA_IObject *> &obj_map)
{
  for (BL_ArmatureConstraint *constraint : m_controlledConstraints) {
    constraint->Relink(obj_map);
  }
  KX_GameObject::Relink(obj_map);
}

bool BL_ArmatureObject::UnlinkObject(SCA_IObject *clientobj)
{
  // clientobj is being deleted, make sure we don't hold any reference to it
  bool res = false;
  for (BL_ArmatureConstraint *constraint : m_controlledConstraints) {
    res |= constraint->UnlinkObject(clientobj);
  }

  return res;
}

void BL_ArmatureObject::ApplyPose()
{
  if (m_lastapplyframe != m_lastframe) {
    // update the constraint if any, first put them all off so that only the active ones will be
    // updated
    for (BL_ArmatureConstraint *constraint : m_controlledConstraints) {
      constraint->UpdateTarget();
    }
    blender::bContext *C = KX_GetActiveEngine()->GetContext();
    blender::Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
    BKE_pose_where_is(depsgraph, GetScene()->GetBlenderScene(), m_objArma);

    m_lastapplyframe = m_lastframe;
  }
}

void BL_ArmatureObject::ApplyPoseImmediate()
{
  if (m_objArma == nullptr || m_objArma->pose == nullptr) {
    return;
  }

  UpdateTimestep(KX_GetActiveEngine()->GetFrameTime());
  if (!ApplyPoseOverrides()) {
    for (BL_ArmatureConstraint *constraint : m_controlledConstraints) {
      constraint->UpdateTarget();
    }
    blender::bContext *C = KX_GetActiveEngine()->GetContext();
    blender::Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
    BKE_pose_where_is(depsgraph, GetScene()->GetBlenderScene(), m_objArma);
  }

  /* Tagging makes the current pose visible to the depsgraph for this frame. With an active
   * action the same-frame action update later reapplies the stored override after its fcurve
   * evaluation, see ApplyPoseOverrides(). */
  GetScene()->AppendToIdsToUpdate(&m_objArma->id,
                                  ID_RECALC_TRANSFORM,
                                  m_objArma->gameflag & OB_OVERLAY_COLLECTION);
}

namespace {

/* Write a quaternion into a pose channel using the channel's own rotation representation.
 * The quaternion is authoritative (the override layer never adds eulers); this only adapts the
 * storage so the action fcurves keep driving the representation they were authored in. */
void write_pose_channel_quat(blender::bPoseChannel &channel, const float quat[4])
{
  BKE_pchan_quat_to_rot(channel, blender::float4(quat[0], quat[1], quat[2], quat[3]));
}

/* Snapshot a channel's current local transform; used as the additive base. */
void read_pose_channel_base(const blender::bPoseChannel &channel, float loc[3], float quat[4])
{
  for (int i = 0; i < 3; ++i) {
    loc[i] = channel.loc[i];
  }
  const blender::float4 q = BKE_pchan_rot_to_quat(channel);
  for (int i = 0; i < 4; ++i) {
    quat[i] = q[i];
  }
}

}  // namespace

void BL_ArmatureObject::SetPoseOverride(const std::string &bone,
                                        KX_PoseBlendMode mode,
                                        const float position[3],
                                        const float rotation[3])
{
  PoseOverride &entry = m_poseOverrides[bone];
  entry.mode = mode;
  for (int i = 0; i < 3; ++i) {
    entry.loc[i] = position[i];
  }
  /* Store the user rotation as a quaternion immediately; never keep an euler offset because
   * adding eulers would introduce gimbal lock. Additive holds the delta quaternion, Override
   * holds the absolute target quaternion. */
  eulO_to_quat(entry.quat, rotation, ROT_MODE_XYZ);

  /* First registration: remember the pose this bone had before any override was applied, so
   * the graph-tick ApplyPoseImmediate() path already combines against a stable base. The
   * action pipeline refreshes it every frame through CapturePoseOverrideBases(). */
  if (m_poseOverrideBases.find(bone) == m_poseOverrideBases.end()) {
    blender::bPoseChannel *channel =
        (m_objArma != nullptr && m_objArma->pose != nullptr) ?
            BKE_pose_channel_find_name(m_objArma->pose, bone.c_str()) :
            nullptr;
    if (channel != nullptr) {
      PoseOverrideBase &base = m_poseOverrideBases[bone];
      read_pose_channel_base(*channel, base.loc, base.quat);
    }
  }
}

void BL_ArmatureObject::CapturePoseOverrideBases()
{
  if (m_poseOverrides.empty() || m_objArma == nullptr || m_objArma->pose == nullptr) {
    return;
  }
  for (const auto &item : m_poseOverrides) {
    blender::bPoseChannel *channel = BKE_pose_channel_find_name(m_objArma->pose, item.first.c_str());
    if (channel == nullptr) {
      continue;
    }
    PoseOverrideBase &base = m_poseOverrideBases[item.first];
    read_pose_channel_base(*channel, base.loc, base.quat);
  }
}

bool BL_ArmatureObject::ClearPoseOverride(const std::string &bone)
{
  auto found = m_poseOverrides.find(bone);
  if (found == m_poseOverrides.end()) {
    /* Unknown bone (or an object without an armature): documented no-op, never a crash. */
    return false;
  }
  auto base = m_poseOverrideBases.find(bone);
  blender::bPoseChannel *channel =
      (m_objArma != nullptr && m_objArma->pose != nullptr) ?
          BKE_pose_channel_find_name(m_objArma->pose, bone.c_str()) :
          nullptr;
  if (channel != nullptr && base != m_poseOverrideBases.end()) {
    /* Restore the action-evaluated pose so a same-frame read-back already sees the bone
     * driven by the action again instead of the last overridden value. */
    for (int i = 0; i < 3; ++i) {
      channel->loc[i] = base->second.loc[i];
    }
    write_pose_channel_quat(*channel, base->second.quat);
  }
  m_poseOverrides.erase(found);
  if (base != m_poseOverrideBases.end()) {
    m_poseOverrideBases.erase(base);
  }
  return true;
}

bool BL_ArmatureObject::ApplyPoseOverrides()
{
  if (m_poseOverrides.empty() || m_objArma == nullptr || m_objArma->pose == nullptr) {
    return false;
  }

  bool applied = false;
  for (const auto &item : m_poseOverrides) {
    blender::bPoseChannel *channel = BKE_pose_channel_find_name(m_objArma->pose, item.first.c_str());
    if (channel == nullptr) {
      continue;
    }
    const PoseOverride &override = item.second;

    /* Base = the pose after fcurve evaluation + layer blending and before this override. That
     * is the pose of the current frame, not the previous frame's final pose, which is what
     * keeps additive entries from accumulating. Falling back to the channel value keeps the
     * graph-tick ApplyPoseImmediate() path working before the action pipeline refreshed it. */
    float base_loc[3];
    float base_quat[4];
    auto base_it = m_poseOverrideBases.find(item.first);
    if (base_it != m_poseOverrideBases.end()) {
      for (int i = 0; i < 3; ++i) {
        base_loc[i] = base_it->second.loc[i];
      }
      for (int i = 0; i < 4; ++i) {
        base_quat[i] = base_it->second.quat[i];
      }
    }
    else {
      read_pose_channel_base(*channel, base_loc, base_quat);
    }

    float result_loc[3];
    float result_quat[4];
    if (override.mode == KX_PoseBlendMode::Additive) {
      /* result = current * delta: a Hamilton quaternion product, never an euler addition (see
       * KX_PoseBlendMode). Components are (w, x, y, z), the same convention used by
       * BKE_pchan_rot_to_quat / BKE_pchan_quat_to_rot. */
      const blender::math::Quaternion current{blender::float4(base_quat)};
      const blender::math::Quaternion delta{blender::float4(override.quat)};
      const blender::math::Quaternion result = current * delta;
      result_quat[0] = result.w;
      result_quat[1] = result.x;
      result_quat[2] = result.y;
      result_quat[3] = result.z;
      for (int i = 0; i < 3; ++i) {
        result_loc[i] = base_loc[i] + override.loc[i];
      }
    }
    else {
      for (int i = 0; i < 4; ++i) {
        result_quat[i] = override.quat[i];
      }
      for (int i = 0; i < 3; ++i) {
        result_loc[i] = override.loc[i];
      }
    }

    for (int i = 0; i < 3; ++i) {
      channel->loc[i] = result_loc[i];
    }
    write_pose_channel_quat(*channel, result_quat);
    applied = true;
  }
  if (!applied) {
    return false;
  }

  for (BL_ArmatureConstraint *constraint : m_controlledConstraints) {
    constraint->UpdateTarget();
  }
  blender::bContext *C = KX_GetActiveEngine()->GetContext();
  blender::Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  BKE_pose_where_is(depsgraph, GetScene()->GetBlenderScene(), m_objArma);
  /* The pose for this frame is fully evaluated now; the regular ApplyPose() later in the
   * action pipeline must not duplicate the work. */
  m_lastapplyframe = m_lastframe;
  return true;
}

bool BL_ArmatureObject::GetPoseChannelTransform(const std::string &bone,
                                                float position[3],
                                                float rotation[3])
{
  if (m_objArma == nullptr || m_objArma->pose == nullptr) {
    return false;
  }
  /* Evaluate the frame first, so the read reflects the same pose the renderer will use. With
   * an override pending, ApplyPoseOverrides() wins over the fcurve values. */
  ApplyPose();
  blender::bPoseChannel *channel = BKE_pose_channel_find_name(m_objArma->pose, bone.c_str());
  if (channel == nullptr) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    position[i] = channel->loc[i];
  }
  if (channel->rotmode == ROT_MODE_XYZ) {
    for (int i = 0; i < 3; ++i) {
      rotation[i] = channel->eul[i];
    }
  }
  else if (channel->rotmode == ROT_MODE_QUAT) {
    quat_to_eulO(rotation, ROT_MODE_XYZ, channel->quat);
  }
  else if (channel->rotmode == ROT_MODE_AXISANGLE) {
    axis_angle_to_eulO(rotation, ROT_MODE_XYZ, channel->rotAxis, channel->rotAngle);
  }
  else {
    /* Euler order differs from XYZ: normalise through the channel matrix. */
    mat4_to_eulO(rotation, ROT_MODE_XYZ, channel->chan_mat);
  }
  return true;
}

static void compute_bendy_bones_matrices(blender::Object *armOb)
{
  /* ============================================================
   * UPDATE BENDY BONES SEGMENTS CACHE FOR BGE
   * ============================================================
   * When depsgraph is not tagged, it's needed to do it manually.
   */
  if (armOb->pose) {
    for (blender::bPoseChannel &pchan : armOb->pose->chanbase) {
      /* Only update bendy bones (segments > 1) */
      Bone *bone = pchan.bone_get(*armOb);
      if (bone != nullptr && bone->segments > 1) {
        BKE_pchan_bbone_segments_cache_compute({&pchan, bone}, *(bArmature *)(armOb->data));
      }
    }
  }
}

void BL_ArmatureObject::ApplyAction(blender::bAction *action, const blender::AnimationEvalContext &evalCtx, bool gpu_skinning)
{
  if (!m_objArma || !action) {
    return;
  }
  blender::PointerRNA ptrrna = RNA_id_pointer_create(&m_objArma->id);
  const blender::animrig::slot_handle_t slot_handle = blender::animrig::first_slot_handle(*action);
  animsys_evaluate_action(&ptrrna, action, slot_handle, &evalCtx, false);
  if (gpu_skinning) {
    // Partial support of bbones.
    compute_bendy_bones_matrices(m_objArma);
  }
}

void BL_ArmatureObject::BlendInPose(blender::bPose *blend_pose, float weight, short mode)
{
  GameBlendPose(m_objArma->pose, blend_pose, weight, mode);
}

bool BL_ArmatureObject::UpdateTimestep(double curtime)
{
  if (curtime != m_lastframe) {
    /* Compute the timestep for the underlying IK algorithm,
     * in the GE, we use ctime to store the timestep.
     */
    m_objArma->pose->ctime = (float)(curtime - m_lastframe);
    m_lastframe = curtime;
  }

  return false;
}

blender::Object *BL_ArmatureObject::GetArmatureObject()
{
  return m_objArma;
}
blender::Object *BL_ArmatureObject::GetOrigArmatureObject()
{
  return m_objArma;
}

void BL_ArmatureObject::GetPose(blender::bPose **pose) const
{
  /* If the caller supplies a null pose, create a new one. */
  /* Otherwise, copy the armature's pose channels into the caller-supplied pose */

  if (!*pose) {
    /* probably not to good of an idea to
     * duplicate everything, but it clears up
     * a crash and memory leakage when
     * &SCA_ActionActuator::m_pose is freed
     */
    BKE_pose_copy_data(pose, m_objArma->pose, 1);
  }
  else {
    if (*pose == m_objArma->pose) {
      // no need to copy if the pointers are the same
      return;
    }

    extract_pose_from_pose(*pose, m_objArma->pose);
  }
}

blender::bPose *BL_ArmatureObject::GetPose() const
{
  return m_objArma->pose;
}

double BL_ArmatureObject::GetLastFrame()
{
  return m_lastframe;
}

bool BL_ArmatureObject::GetBoneMatrix(blender::Bone *bone, MT_Matrix4x4 &matrix)
{
  ApplyPose();
  blender::bPoseChannel *pchan = BKE_pose_channel_find_name(m_objArma->pose, bone->name);
  if (pchan) {
    matrix.setValue(&pchan->pose_mat[0][0]);
  }

  return (pchan != nullptr);
}

bool BL_ArmatureObject::GetDrawDebug() const
{
  return m_drawDebug;
}

void BL_ArmatureObject::DrawDebug(RAS_DebugDraw &debugDraw)
{
  const MT_Vector3 &scale = NodeGetWorldScaling();
  const MT_Matrix3x3 &rot = NodeGetWorldOrientation();
  const MT_Vector3 &pos = NodeGetWorldPosition();

  for (blender::bPoseChannel *pchan = m_objArma->pose->chanbase.first(); pchan;
       pchan = pchan->next)
  {
    MT_Vector3 head = rot * (MT_Vector3(pchan->pose_head) * scale) + pos;
    MT_Vector3 tail = rot * (MT_Vector3(pchan->pose_tail) * scale) + pos;
    debugDraw.DrawLine(tail, head, MT_Vector4(1.0f, 0.0f, 0.0f, 1.0f));
  }
  m_drawDebug = false;
}

float BL_ArmatureObject::GetBoneLength(blender::Bone *bone) const
{
  return (float)(MT_Vector3(bone->head) - MT_Vector3(bone->tail)).length();
}

#ifdef WITH_PYTHON

// PYTHON
PyObject *BL_ArmatureObject::game_object_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  /* Legacy subclassing: if an existing object is passed as argument, delegate to py_base_new. */
  if (PyTuple_GET_SIZE(args) > 0) {
    return py_base_new(type, args, kwds);
  }

  BL_ArmatureObject *obj = new BL_ArmatureObject();

  PyObject *proxy = py_base_new(type, PyTuple_Pack(1, obj->GetProxy()), kwds);
  if (!proxy) {
    delete obj;
    return nullptr;
  }

  return proxy;
}

PyTypeObject BL_ArmatureObject::Type = {PyVarObject_HEAD_INIT(nullptr, 0) "BL_ArmatureObject",
                                        sizeof(EXP_PyObjectPlus_Proxy),
                                        0,
                                        py_base_dealloc,
                                        0,
                                        0,
                                        0,
                                        0,
                                        py_base_repr,
                                        0,
                                        &KX_GameObject::Sequence,
                                        &KX_GameObject::Mapping,
                                        0,
                                        0,
                                        0,
                                        nullptr,
                                        nullptr,
                                        0,
                                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        Methods,
                                        0,
                                        0,
                                        &KX_GameObject::Type,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        game_object_new};

PyMethodDef BL_ArmatureObject::Methods[] = {
    EXP_PYMETHODTABLE_NOARGS(BL_ArmatureObject, update),
    EXP_PYMETHODTABLE_NOARGS(BL_ArmatureObject, draw),
    {nullptr, nullptr}  // Sentinel
};

PyAttributeDef BL_ArmatureObject::Attributes[] = {

    EXP_PYATTRIBUTE_RO_FUNCTION("constraints", BL_ArmatureObject, pyattr_get_constraints),
    EXP_PYATTRIBUTE_RO_FUNCTION("channels", BL_ArmatureObject, pyattr_get_channels),
    EXP_PYATTRIBUTE_NULL  // Sentinel
};

PyObject *BL_ArmatureObject::pyattr_get_constraints(EXP_PyObjectPlus *self_v,
                                                    const EXP_PYATTRIBUTE_DEF *attrdef)
{
  BL_ArmatureObject *self = static_cast<BL_ArmatureObject *>(self_v);
  return self->m_controlledConstraints->GetProxy();
}

PyObject *BL_ArmatureObject::pyattr_get_channels(EXP_PyObjectPlus *self_v,
                                                 const EXP_PYATTRIBUTE_DEF *attrdef)
{
  BL_ArmatureObject *self = static_cast<BL_ArmatureObject *>(self_v);
  return self->m_poseChannels->GetProxy();
}

EXP_PYMETHODDEF_DOC_NOARGS(
    BL_ArmatureObject,
    update,
    "update()\n"
    "Make sure that the armature will be updated on next graphic frame.\n"
    "This is automatically done if a KX_ArmatureActuator with mode run is active\n"
    "or if an action is playing. This function is useful in other cases.\n")
{
  UpdateTimestep(KX_GetActiveEngine()->GetFrameTime());
  Py_RETURN_NONE;
}

EXP_PYMETHODDEF_DOC_NOARGS(BL_ArmatureObject, draw, "Draw Debug Armature")
{
  /* Armature bones are updated later, so we only set to true a flag
   * to request a debug draw later in ApplyPose after updating bones. */
  m_drawDebug = true;
  Py_RETURN_NONE;
}

#endif  // WITH_PYTHON
