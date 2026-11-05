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

/** \file KX_GameAdaptersEngine.cpp
 *  \ingroup gamelogic
 *
 *  Engine backed implementations of the four node adapters.
 */

#include "KX_GameAdapters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>

#include "BKE_action.hh"
#include "BKE_armature.hh"
#include "BKE_context.hh"
#include "BKE_global.hh"
#include "BKE_lib_id.hh"
#include "BKE_material.hh"
#include "BKE_sound.hh"
#include "DNA_action_types.h"
#include "DNA_armature_types.h"

#ifdef WITH_AUDASPACE
#  include <Exception.h>
#  include <devices/DeviceManager.h>
#  include <devices/I3DDevice.h>
#  include <devices/I3DHandle.h>
#  include <devices/IDevice.h>
#  include <devices/IHandle.h>
#  include <file/File.h>
#  include <util/Math3D.h>
#endif

#include "BL_ActionManager.h"
#include "BL_ArmatureObject.h"
#include "BL_SceneConverter.h"
#include "GPU_matrix.hh"
#include "KX_Camera.h"
#include "KX_ClientObjectInfo.h"
#include "KX_FontObject.h"
#include "KX_GameObject.h"
#include "KX_Globals.h"
#include "KX_KetsjiEngine.h"
#include "KX_RayCast.h"
#include "KX_Scene.h"
#include "KXNetworkMessage/KX_NetworkMessageScene.h"
#include "MT_Matrix3x3.h"
#include "MT_Vector3.h"
#include "KX_PropertyAccessor.h"
#include "KX_UIAdapterEngine.h"
#include "PHY_IPhysicsController.h"
#include "PHY_IPhysicsEnvironment.h"
#include "PHY_ICharacter.h"
#include "RAS_ICanvas.h"
#include "RAS_IDisplayArray.h"
#include "SCA_LogicManager.h"
#include "RAS_IVertex.h"
#include "RAS_MeshObject.h"
#include "DEV_Joystick.h"
#include "SCA_EventManager.h"
#include "SCA_IInputDevice.h"
#include "SCA_JoystickManager.h"
#include "SCA_KeyboardManager.h"
#include "SCA_LogicManager.h"
#include "SCA_MouseManager.h"

namespace {

class EngineRayCastCallback final : public KX_RayCast {
 public:
  explicit EngineRayCastCallback(PHY_IPhysicsController *ignore_controller)
      : KX_RayCast(ignore_controller, true, false)
  {
  }

  bool RayHit(KX_ClientObjectInfo *client) override
  {
    hit_object = client != nullptr ? client->m_gameobject : nullptr;
    return true;
  }

  KX_GameObject *hit_object = nullptr;
};

SCA_IInputDevice::SCA_EnumInputs ToInputCode(int code)
{
  const int clamped = std::max(0, std::min(int(SCA_IInputDevice::MAX_KEYS), code));
  return static_cast<SCA_IInputDevice::SCA_EnumInputs>(clamped);
}

SCA_InputEvent::SCA_EnumInputs ToEventMode(KX_InputMode mode)
{
  switch (mode) {
    case KX_InputMode::Press:
      return SCA_InputEvent::JUSTACTIVATED;
    case KX_InputMode::Release:
      return SCA_InputEvent::JUSTRELEASED;
    default:
      return SCA_InputEvent::ACTIVE;
  }
}

int LastValue(const SCA_InputEvent &event)
{
  return event.m_values.empty() ? 0 : event.m_values.back();
}

/** Resolve a gamepad by index through the scene's joystick event manager. */
DEV_Joystick *SceneJoystick(KX_Scene *scene, int pad)
{
  if (scene == nullptr || pad < 0) {
    return nullptr;
  }
  SCA_LogicManager *logic = scene->GetLogicManager();
  if (logic == nullptr) {
    return nullptr;
  }
  SCA_EventManager *manager = logic->FindEventManager(SCA_EventManager::JOY_EVENTMGR);
  if (manager == nullptr) {
    return nullptr;
  }
  return static_cast<SCA_JoystickManager *>(manager)->GetJoystickDevice(pad);
}

/** Minimal, documented key-name table used by "ctrl+shift+a" combos. */
int KeyCodeFromName(std::string name)
{
  for (char &c : name) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  if (name.size() == 1) {
    const char c = name[0];
    if (c >= 'a' && c <= 'z') {
      return SCA_IInputDevice::AKEY + (c - 'a');
    }
    if (c >= '0' && c <= '9') {
      return SCA_IInputDevice::ZEROKEY + (c - '0');
    }
  }
  if (name == "space") {
    return SCA_IInputDevice::SPACEKEY;
  }
  if (name == "enter" || name == "return") {
    return SCA_IInputDevice::RETKEY;
  }
  if (name == "esc" || name == "escape") {
    return SCA_IInputDevice::ESCKEY;
  }
  if (name == "tab") {
    return SCA_IInputDevice::TABKEY;
  }
  if (name == "shift") {
    return SCA_IInputDevice::LEFTSHIFTKEY;
  }
  if (name == "ctrl" || name == "control") {
    return SCA_IInputDevice::LEFTCTRLKEY;
  }
  if (name == "alt") {
    return SCA_IInputDevice::LEFTALTKEY;
  }
  if (name == "up") {
    return SCA_IInputDevice::UPARROWKEY;
  }
  if (name == "down") {
    return SCA_IInputDevice::DOWNARROWKEY;
  }
  if (name == "left") {
    return SCA_IInputDevice::LEFTARROWKEY;
  }
  if (name == "right") {
    return SCA_IInputDevice::RIGHTARROWKEY;
  }
  return SCA_IInputDevice::NOKEY;
}

class EngineInputAdapter final : public KX_InputAdapter {
 public:
  explicit EngineInputAdapter(KX_Scene *scene)
      : scene_(scene),
        keyboard_(scene != nullptr && scene->GetKeyboardManager() != nullptr
                      ? scene->GetKeyboardManager()->GetInputDevice()
                      : nullptr),
        mouse_(scene != nullptr && scene->GetMouseManager() != nullptr
                   ? scene->GetMouseManager()->GetInputDevice()
                   : nullptr),
        canvas_(scene != nullptr ? scene->GetCanvas() : nullptr)
  {
  }

  void BeginFrame(float delta_time) override
  {
    (void)delta_time;
    if (mouse_ == nullptr) {
      return;
    }

    const float x = float(LastValue(mouse_->GetInput(SCA_IInputDevice::MOUSEX)));
    const float y = float(LastValue(mouse_->GetInput(SCA_IInputDevice::MOUSEY)));
    delta_x_ = have_previous_ ? x - previous_x_ : 0.0f;
    delta_y_ = have_previous_ ? y - previous_y_ : 0.0f;
    previous_x_ = x;
    previous_y_ = y;
    have_previous_ = true;

    wheel_ = 0;
    if (mouse_->GetInput(SCA_IInputDevice::WHEELUPMOUSE).Find(SCA_InputEvent::JUSTACTIVATED)) {
      wheel_ += 1;
    }
    if (mouse_->GetInput(SCA_IInputDevice::WHEELDOWNMOUSE).Find(SCA_InputEvent::JUSTACTIVATED)) {
      wheel_ -= 1;
    }
  }

  bool KeyActive(int code, KX_InputMode mode) override
  {
    if (keyboard_ == nullptr) {
      return false;
    }
    const SCA_InputEvent &event = keyboard_->GetInput(ToInputCode(code));
    return event.Find(ToEventMode(mode));
  }

  bool MouseButtonActive(int button, KX_InputMode mode) override
  {
    if (mouse_ == nullptr) {
      return false;
    }
    SCA_IInputDevice::SCA_EnumInputs code = SCA_IInputDevice::LEFTMOUSE;
    switch (button) {
      case 1:
        code = SCA_IInputDevice::MIDDLEMOUSE;
        break;
      case 2:
        code = SCA_IInputDevice::RIGHTMOUSE;
        break;
      case 3:
        code = SCA_IInputDevice::BUTTON4MOUSE;
        break;
      case 4:
        code = SCA_IInputDevice::BUTTON5MOUSE;
        break;
      default:
        break;
    }
    return mouse_->GetInput(code).Find(ToEventMode(mode));
  }

  void MousePosition(float &x, float &y) const override
  {
    if (mouse_ == nullptr) {
      x = 0.0f;
      y = 0.0f;
      return;
    }
    x = float(LastValue(mouse_->GetInput(SCA_IInputDevice::MOUSEX)));
    y = float(LastValue(mouse_->GetInput(SCA_IInputDevice::MOUSEY)));
  }

  void MouseNormalized(float &x, float &y) const override
  {
    float pixel_x;
    float pixel_y;
    MousePosition(pixel_x, pixel_y);
    if (canvas_ == nullptr) {
      x = 0.0f;
      y = 0.0f;
      return;
    }
    x = canvas_->GetMouseNormalizedX(int(pixel_x));
    y = canvas_->GetMouseNormalizedY(int(pixel_y));
  }

  void MouseDelta(float &dx, float &dy) const override
  {
    dx = delta_x_;
    dy = delta_y_;
  }

  int MouseWheel() const override
  {
    return wheel_;
  }

  void SetMousePosition(float nx, float ny) override
  {
    if (canvas_ == nullptr) {
      return;
    }
    const int width = std::max(1, canvas_->GetWidth());
    const int height = std::max(1, canvas_->GetHeight());
    const int x = int(std::max(0.0f, std::min(1.0f, nx)) * float(width - 1));
    const int y = int(std::max(0.0f, std::min(1.0f, ny)) * float(height - 1));
    canvas_->SetMousePosition(x, y);
  }

  void SetCursorVisible(bool visible) override
  {
    if (canvas_ == nullptr) {
      return;
    }
    canvas_->SetMouseState(visible ? RAS_ICanvas::MOUSE_NORMAL : RAS_ICanvas::MOUSE_INVISIBLE);
  }

  /* B6.2 input extensions. */

  bool IsGamepadButtonDown(int pad, int button) override
  {
    DEV_Joystick *joystick = SceneJoystick(scene_, pad);
    if (joystick == nullptr || button < 0) {
      return false;
    }
    return joystick->aButtonPressIsPositive(button);
  }

  void GetGamepadAxis(int pad, int axis, float &x, float &y) override
  {
    x = 0.0f;
    y = 0.0f;
    DEV_Joystick *joystick = SceneJoystick(scene_, pad);
    if (joystick == nullptr || axis < 0 || axis > 2) {
      return;
    }
    const int base = axis * 2;
    x = float(joystick->GetAxisPosition(base)) / 32767.0f;
    y = float(joystick->GetAxisPosition(base + 1)) / 32767.0f;
  }

  void GetGamepadTrigger(int pad, int trigger, float &value) override
  {
    value = 0.0f;
    DEV_Joystick *joystick = SceneJoystick(scene_, pad);
    if (joystick == nullptr || trigger < 0 || trigger > 1) {
      return;
    }
    const int axis = 4 + trigger;
    value = std::max(0.0f, std::min(1.0f, float(joystick->GetAxisPosition(axis)) / 32767.0f));
  }

  void GetTouch(int index, float &x, float &y, bool &down) override
  {
    /* The Ketsji input layer has no touch device wired up in this build. */
    (void)index;
    x = 0.0f;
    y = 0.0f;
    down = false;
  }

  int GetTouchCount() override
  {
    return 0;
  }

  bool IsKeyComboDown(const std::string &keys) override
  {
    if (keyboard_ == nullptr) {
      return false;
    }
    size_t start = 0;
    while (start <= keys.size()) {
      const size_t plus = keys.find('+', start);
      std::string token = keys.substr(
          start, plus == std::string::npos ? std::string::npos : plus - start);
      while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
        token.erase(token.begin());
      }
      while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
        token.pop_back();
      }
      if (token.empty()) {
        return false;
      }
      const int code = KeyCodeFromName(token);
      if (code == SCA_IInputDevice::NOKEY) {
        std::fprintf(stderr, "GameNode: KeyCombo: unknown key name '%s'\n", token.c_str());
        return false;
      }
      if (!keyboard_->GetInput(ToInputCode(code)).Find(SCA_InputEvent::ACTIVE)) {
        return false;
      }
      if (plus == std::string::npos) {
        break;
      }
      start = plus + 1;
    }
    return true;
  }

  bool VibrateGamepad(int pad, float intensity, float duration) override
  {
    DEV_Joystick *joystick = SceneJoystick(scene_, pad);
    if (joystick == nullptr) {
      std::fprintf(stderr,
                   "GameNode: VibrateGamepad: no gamepad at index %d\n",
                   pad);
      return false;
    }
    if (!joystick->GetRumbleSupport()) {
      std::fprintf(stderr,
                   "GameNode: VibrateGamepad: gamepad %d has no rumble support\n",
                   pad);
      return false;
    }
    const float strength = std::max(0.0f, std::min(1.0f, intensity));
    const unsigned int milliseconds = static_cast<unsigned int>(
        std::max(0.0f, std::min(10000.0f, duration * 1000.0f)));
    return joystick->RumblePlay(strength, strength, milliseconds);
  }

 private:
  KX_Scene *scene_;
  SCA_IInputDevice *keyboard_;
  SCA_IInputDevice *mouse_;
  RAS_ICanvas *canvas_;
  bool have_previous_ = false;
  float previous_x_ = 0.0f;
  float previous_y_ = 0.0f;
  float delta_x_ = 0.0f;
  float delta_y_ = 0.0f;
  int wheel_ = 0;
};

class EngineSceneAdapter final : public KX_SceneAdapter {
 public:
  explicit EngineSceneAdapter(KX_Scene *scene) : scene_(scene) {}

  KX_GameObject *FindObject(const std::string &name) override
  {
    if (scene_ == nullptr) {
      return nullptr;
    }
    for (KX_GameObject *object : scene_->GetObjectList()) {
      if (object != nullptr && object->GetName() == name) {
        return object;
      }
    }
    return nullptr;
  }

  void Objects(std::vector<KX_GameObject *> &out) override
  {
    out.clear();
    if (scene_ == nullptr) {
      return;
    }
    for (KX_GameObject *object : scene_->GetObjectList()) {
      if (object != nullptr) {
        out.push_back(object);
      }
    }
  }

  /**
   * Geometric sphere/cone query over the scene object list.
   *
   * The legacy near/radar sensors rely on a hidden physics collider whose only
   * job is broad phase. Measuring world positions directly gives the same
   * result for the node use case without building a second physics controller,
   * and keeps the query usable on objects that have no physics at all.
   */
  void QueryProximity(KX_GameObject *origin,
                      const float local_axis[3],
                      float radius,
                      float angle_deg,
                      const std::string &property,
                      std::vector<KX_ProximityHit> &out) override
  {
    out.clear();
    if (scene_ == nullptr || origin == nullptr || radius <= 0.0f) {
      return;
    }

    const MT_Vector3 origin_position = origin->NodeGetWorldPosition();
    MT_Vector3 axis(0.0f, 0.0f, 1.0f);
    if (local_axis != nullptr) {
      axis = MT_Vector3(local_axis);
    }
    if (axis.length2() < 1e-12f) {
      axis = MT_Vector3(0.0f, 0.0f, 1.0f);
    }
    const MT_Vector3 world_axis =
        (origin->NodeGetWorldOrientation() * axis).safe_normalized();
    const bool use_cone = angle_deg > 0.0f && angle_deg < 360.0f;
    const float cos_half_angle = use_cone ?
                                     std::cos(angle_deg * 0.5f * float(MT_RADS_PER_DEG)) :
                                     -1.0f;
    KX_PropertyBackend *backend = KX_DefaultPropertyBackend();

    for (KX_GameObject *object : scene_->GetObjectList()) {
      if (object == nullptr || object == origin) {
        continue;
      }
      if (!property.empty() &&
          (backend == nullptr || backend->Type(object, property) == KX_VALUE_NONE))
      {
        continue;
      }

      const MT_Vector3 delta = object->NodeGetWorldPosition() - origin_position;
      const float distance = delta.length();
      if (distance > radius) {
        continue;
      }

      float angle = 0.0f;
      if (use_cone && distance > 1e-6f) {
        const float cosine = delta.safe_normalized().dot(world_axis);
        if (cosine < cos_half_angle) {
          continue;
        }
        angle = std::acos(std::min(1.0f, std::max(-1.0f, cosine))) *
                float(MT_DEGS_PER_RAD);
      }

      KX_ProximityHit hit;
      hit.object = object;
      hit.distance = distance;
      hit.angle = angle;
      out.push_back(hit);
    }

    std::sort(out.begin(),
              out.end(),
              [](const KX_ProximityHit &a, const KX_ProximityHit &b) {
                return a.distance < b.distance;
              });
  }

  /**
   * Physics ray through the mouse cursor. The ray is built by unprojecting the
   * cursor at the near and far planes, which works for both perspective and
   * orthographic cameras, then handed to KX_RayCast. This is an exact ray hit
   * against collision shapes, unlike the legacy mouse-over screen box test.
   */
  bool RayCastFromScreen(float nx, float ny, KX_GameObject *camera_object, KX_RayHit &out) override
  {
    out = KX_RayHit();
    if (scene_ == nullptr) {
      return false;
    }
    KX_Camera *camera = dynamic_cast<KX_Camera *>(camera_object);
    if (camera == nullptr) {
      camera = scene_->GetActiveCamera();
    }
    if (camera == nullptr) {
      return false;
    }
    RAS_ICanvas *canvas = scene_->GetCanvas();
    PHY_IPhysicsEnvironment *physics = scene_->GetPhysicsEnvironment();
    if (canvas == nullptr || physics == nullptr) {
      return false;
    }

    int viewport[4];
    canvas->GetViewportArea().Pack(viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
      return false;
    }

    const float pixel_x = nx * float(viewport[2]) + float(viewport[0]);
    const float pixel_y = (1.0f - ny) * float(viewport[3]) + float(viewport[1]);

    MT_Matrix4x4 world_to_camera(camera->GetWorldToCamera());
    MT_Matrix4x4 projection(camera->GetProjectionMatrix());
    float model_inverse[4][4];
    float projection_value[4][4];
    world_to_camera.inverse().getValue(static_cast<float *>(&model_inverse[0][0]));
    projection.getValue(static_cast<float *>(&projection_value[0][0]));

    float near_pixel[3] = {pixel_x, pixel_y, 0.0f};
    float far_pixel[3] = {pixel_x, pixel_y, 1.0f};
    float near_world[3];
    float far_world[3];
    blender::GPU_matrix_unproject_3fv(
        near_pixel, model_inverse, projection_value, viewport, near_world);
    blender::GPU_matrix_unproject_3fv(
        far_pixel, model_inverse, projection_value, viewport, far_world);

    const MT_Vector3 from(near_world);
    const MT_Vector3 to(far_world);
    EngineRayCastCallback callback(nullptr);
    if (!KX_RayCast::RayTest(physics, from, to, callback) || callback.hit_object == nullptr) {
      return false;
    }

    out.object = callback.hit_object;
    out.point[0] = float(callback.m_hitPoint.x());
    out.point[1] = float(callback.m_hitPoint.y());
    out.point[2] = float(callback.m_hitPoint.z());
    out.normal[0] = float(callback.m_hitNormal.x());
    out.normal[1] = float(callback.m_hitNormal.y());
    out.normal[2] = float(callback.m_hitNormal.z());
    out.distance = float((callback.m_hitPoint - from).length());
    return true;
  }

  /**
   * Pure inverse projection for the legacy WorldPosition node.
   *
   * The cursor pixel is unprojected to the near and far planes with the same
   * view/projection inverse used by #RayCastFromScreen, but the result is never
   * tested against physics. The pixel's lateral offset from the camera center
   * line is preserved, then the point is advanced along the view direction by
   * #depth, so depth 0 returns the camera plane and a negative depth returns a
   * point behind the camera.
   */
  bool RayCastFromScreenAnyDepth(float sx, float sy, float depth, float out[3]) override
  {
    out[0] = out[1] = out[2] = 0.0f;
    if (scene_ == nullptr) {
      return false;
    }
    KX_Camera *camera = scene_->GetActiveCamera();
    if (camera == nullptr) {
      return false;
    }
    RAS_ICanvas *canvas = scene_->GetCanvas();
    if (canvas == nullptr) {
      return false;
    }

    int viewport[4];
    canvas->GetViewportArea().Pack(viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
      return false;
    }

    MT_Matrix4x4 world_to_camera(camera->GetWorldToCamera());
    MT_Matrix4x4 projection(camera->GetProjectionMatrix());
    float model_inverse[4][4];
    float projection_value[4][4];
    world_to_camera.inverse().getValue(static_cast<float *>(&model_inverse[0][0]));
    projection.getValue(static_cast<float *>(&projection_value[0][0]));

    /* Pixels are already in viewport space with the origin at the bottom left,
     * which is exactly what GPU_matrix_unproject_3fv expects. */
    float near_pixel[3] = {sx, sy, 0.0f};
    float far_pixel[3] = {sx, sy, 1.0f};
    float near_world[3];
    float far_world[3];
    blender::GPU_matrix_unproject_3fv(
        near_pixel, model_inverse, projection_value, viewport, near_world);
    blender::GPU_matrix_unproject_3fv(
        far_pixel, model_inverse, projection_value, viewport, far_world);

    const MT_Vector3 near_point(near_world);
    const MT_Vector3 direction = (MT_Vector3(far_world) - near_point).safe_normalized();
    if (direction.length2() < 1e-12f) {
      return false;
    }

    const MT_Vector3 eye = camera->NodeGetWorldPosition();
    const MT_Vector3 offset = near_point - eye;
    const MT_Vector3 lateral = offset - direction * offset.dot(direction);
    const MT_Vector3 position = eye + lateral + direction * depth;
    position.getValue(out);
    return true;
  }

  /**
   * Point query behind the legacy Touch node.
   *
   * The physics world has no "overlap at a point" entry point, so this walks the
   * scene object list and treats every object as a sphere centered on its world
   * position with the largest world scale component as radius. This is an
   * approximation of the collider, deliberately kept separate from the physical
   * Collision sensor: Touch asks "which objects sit around the ray hit point",
   * not "which bodies are in contact this frame". The hit object itself is
   * always reported first with its exact ray point and normal.
   */
  void OverlapQuery(const KX_RayHit &hit,
                    float threshold,
                    KX_OverlapCallback fn,
                    void *user) override
  {
    if (fn == nullptr || scene_ == nullptr) {
      return;
    }
    if (hit.object != nullptr) {
      KX_OverlapHit own;
      own.object = hit.object;
      own.point[0] = hit.point[0];
      own.point[1] = hit.point[1];
      own.point[2] = hit.point[2];
      own.normal[0] = hit.normal[0];
      own.normal[1] = hit.normal[1];
      own.normal[2] = hit.normal[2];
      fn(own, user);
    }

    if (threshold <= 0.0f) {
      return;
    }

    const MT_Vector3 point(hit.point);
    for (KX_GameObject *object : scene_->GetObjectList()) {
      if (object == nullptr || object == hit.object) {
        continue;
      }
      const MT_Vector3 center = object->NodeGetWorldPosition();
      const MT_Vector3 scaling = object->NodeGetWorldScaling();
      const float radius = std::max(std::fabs(float(scaling.x())),
                                    std::max(std::fabs(float(scaling.y())),
                                             std::fabs(float(scaling.z()))));
      const MT_Vector3 delta = point - center;
      const float distance = delta.length();
      if (distance > threshold + radius) {
        continue;
      }

      KX_OverlapHit overlap;
      overlap.object = object;
      if (distance > 1e-6f) {
        const MT_Vector3 normal = delta.safe_normalized();
        /* Closest point on the approximating sphere toward the ray hit. */
        const MT_Vector3 contact = center + normal * std::min(distance, radius);
        contact.getValue(overlap.point);
        normal.getValue(overlap.normal);
      }
      else {
        point.getValue(overlap.point);
      }
      fn(overlap, user);
    }
  }

  void RemoveObject(KX_GameObject *object) override
  {
    if (scene_ != nullptr && object != nullptr) {
      if (audio_ != nullptr) {
        /* Drop audio ownership while the pointer is still valid. */
        audio_->ForgetObject(object);
      }
      scene_->DelayedRemoveObject(object);
    }
  }

  bool LoadScene(const std::string &name) override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine == nullptr || scene_ == nullptr || name.empty()) {
      return false;
    }
    if (name == scene_->GetName()) {
      return false;
    }
    engine->ReplaceScene(scene_->GetName(), name);
    return true;
  }

  void SceneNames(std::vector<std::string> &out) override
  {
    out.clear();
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine == nullptr) {
      return;
    }
    for (KX_Scene *scene : engine->CurrentScenes()) {
      if (scene != nullptr) {
        out.push_back(scene->GetName());
      }
    }
  }

  std::string SceneName() const override
  {
    return scene_ != nullptr ? scene_->GetName() : std::string();
  }

  float ScreenWidth() const override
  {
    RAS_ICanvas *canvas = scene_ != nullptr ? scene_->GetCanvas() : nullptr;
    return canvas != nullptr ? float(canvas->GetWidth()) : 0.0f;
  }

  float ScreenHeight() const override
  {
    RAS_ICanvas *canvas = scene_ != nullptr ? scene_->GetCanvas() : nullptr;
    return canvas != nullptr ? float(canvas->GetHeight()) : 0.0f;
  }

  void SetActiveCamera(KX_GameObject *camera) override
  {
    if (scene_ == nullptr || camera == nullptr) {
      return;
    }
    if (camera->GetGameObjectType() != SCA_IObject::OBJ_CAMERA) {
      return;
    }
    scene_->SetActiveCamera(static_cast<KX_Camera *>(camera));
  }

  void SendMessage(KX_GameObject *to, const std::string &subject, const std::string &body) override
  {
    if (scene_ == nullptr) {
      return;
    }
    KX_NetworkMessageScene *network = scene_->GetNetworkMessageScene();
    if (network == nullptr) {
      return;
    }
    /* A null target broadcasts to every receiver listening on the subject. */
    network->SendMessage(to != nullptr ? to->GetName() : std::string(), nullptr, subject, body);
  }

  bool FindMessages(KX_GameObject *receiver,
                    const std::string &subject,
                    std::vector<std::string> &out) override
  {
    out.clear();
    if (scene_ == nullptr || receiver == nullptr) {
      return false;
    }
    KX_NetworkMessageScene *network = scene_->GetNetworkMessageScene();
    if (network == nullptr) {
      return false;
    }
    const auto messages = network->FindMessages(receiver->GetName(), subject);
    for (const auto &message : messages) {
      out.push_back(message.body);
    }
    return !out.empty();
  }

  void RequestQuit() override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine != nullptr) {
      engine->RequestExit(KX_ExitRequest::QUIT_GAME);
    }
  }

  void RequestRestart() override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine != nullptr) {
      engine->RequestExit(KX_ExitRequest::RESTART_GAME);
    }
  }

  void SetTimeScale(float scale) override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine != nullptr) {
      engine->SetTimeScale(double(scale));
    }
  }

  float TimeScale() const override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    return engine != nullptr ? float(engine->GetTimeScale()) : 1.0f;
  }

  void Resolution(int &width, int &height) const override
  {
    RAS_ICanvas *canvas = scene_ != nullptr ? scene_->GetCanvas() : nullptr;
    width = canvas != nullptr ? canvas->GetWidth() : 0;
    height = canvas != nullptr ? canvas->GetHeight() : 0;
  }

  bool Vsync() const override
  {
    RAS_ICanvas *canvas = scene_ != nullptr ? scene_->GetCanvas() : nullptr;
    if (canvas == nullptr) {
      return false;
    }
    int interval = 0;
    canvas->GetSwapInterval(interval);
    return interval > 0;
  }

  void SetVsync(bool enabled) override
  {
    RAS_ICanvas *canvas = scene_ != nullptr ? scene_->GetCanvas() : nullptr;
    if (canvas != nullptr) {
      canvas->SetSwapInterval(enabled ? 1 : 0);
    }
  }

  KX_GameObject *ActiveCamera() override
  {
    return scene_ != nullptr ? static_cast<KX_GameObject *>(scene_->GetActiveCamera()) : nullptr;
  }

  void SetCameraFov(KX_GameObject *camera, float fov) override
  {
    if (camera == nullptr || camera->GetGameObjectType() != SCA_IObject::OBJ_CAMERA) {
      return;
    }
    KX_Camera *cam = static_cast<KX_Camera *>(camera);
    RAS_CameraData *data = cam->GetCameraData();
    if (data == nullptr || data->m_sensor_x <= 0.0f || fov <= 0.0f) {
      return;
    }
    RAS_CameraData updated = *data;
    updated.m_lens = data->m_sensor_x / (2.0f * std::tan(fov * MT_RADS_PER_DEG * 0.5f));
    cam->SetCameraData(updated);
    cam->InvalidateProjectionMatrix(false);
  }

  void SetCameraOrthoScale(KX_GameObject *camera, float scale) override
  {
    if (camera == nullptr || camera->GetGameObjectType() != SCA_IObject::OBJ_CAMERA) {
      return;
    }
    KX_Camera *cam = static_cast<KX_Camera *>(camera);
    RAS_CameraData *data = cam->GetCameraData();
    if (data == nullptr || scale <= 0.0f) {
      return;
    }
    RAS_CameraData updated = *data;
    updated.m_scale = scale;
    cam->SetCameraData(updated);
    cam->InvalidateProjectionMatrix(false);
  }

  /** Wire the audio adapter so object removal can drop audio ownership. */
  void SetAudioAdapter(KX_AudioAdapter *audio) { audio_ = audio; }

 private:
  KX_Scene *scene_;
  KX_AudioAdapter *audio_ = nullptr;
};

class EnginePhysicsAdapter final : public KX_PhysicsAdapter {
 public:
  explicit EnginePhysicsAdapter(KX_Scene *scene) : scene_(scene) {}

  KX_RaycastResult Raycast(const float origin_data[3],
                           const float direction_data[3],
                           float max_dist,
                           const KX_GameObject *ignore) override
  {
    KX_RaycastResult result;
    if (scene_ == nullptr) {
      return result;
    }
    PHY_IPhysicsEnvironment *environment = scene_->GetPhysicsEnvironment();
    if (environment == nullptr || max_dist <= 0.0f) {
      return result;
    }

    const MT_Vector3 origin(origin_data[0], origin_data[1], origin_data[2]);
    const MT_Vector3 direction(direction_data[0], direction_data[1], direction_data[2]);
    if (direction.length2() < 1e-12f) {
      return result;
    }
    const MT_Vector3 target = origin + direction.safe_normalized() * max_dist;

    PHY_IPhysicsController *ignore_controller = ignore != nullptr
                                                    ? const_cast<KX_GameObject *>(ignore)->GetPhysicsController()
                                                    : nullptr;
    EngineRayCastCallback callback(ignore_controller);
    if (!KX_RayCast::RayTest(environment, origin, target, callback) ||
        !callback.m_hitFound || callback.hit_object == nullptr)
    {
      return result;
    }

    result.hit = true;
    result.object = callback.hit_object;
    for (int i = 0; i < 3; ++i) {
      result.point[i] = callback.m_hitPoint[i];
      result.normal[i] = callback.m_hitNormal[i];
    }
    return result;
  }

  void SetLinearVelocity(KX_GameObject *object, const float velocity[3]) override
  {
    PHY_IPhysicsController *controller = Controller(object);
    if (controller != nullptr) {
      controller->SetLinearVelocity(MT_Vector3(velocity), false);
    }
  }

  void GetVelocity(KX_GameObject *object, float out[3]) override
  {
    out[0] = out[1] = out[2] = 0.0f;
    PHY_IPhysicsController *controller = Controller(object);
    if (controller == nullptr) {
      return;
    }
    const MT_Vector3 velocity = controller->GetVelocity(object->NodeGetWorldPosition());
    velocity.getValue(out);
  }

  void ApplyImpulse(KX_GameObject *object,
                    const float point[3],
                    const float impulse[3],
                    bool local) override
  {
    PHY_IPhysicsController *controller = Controller(object);
    if (controller != nullptr) {
      controller->ApplyImpulse(MT_Vector3(point), MT_Vector3(impulse), local);
    }
  }

  void ApplyForce(KX_GameObject *object,
                  const float point[3],
                  const float force[3],
                  bool local) override
  {
    (void)point;
    PHY_IPhysicsController *controller = Controller(object);
    if (controller != nullptr) {
      controller->ApplyForce(MT_Vector3(force), local);
    }
  }

  void ApplyTorque(KX_GameObject *object, const float torque[3], bool local) override
  {
    PHY_IPhysicsController *controller = Controller(object);
    if (controller != nullptr) {
      controller->ApplyTorque(MT_Vector3(torque), local);
    }
  }

  void SetGravity(const float gravity[3]) override
  {
    if (scene_ != nullptr) {
      scene_->SetGravity(MT_Vector3(gravity));
    }
  }

  void GetGravity(float out[3]) const override
  {
    if (scene_ == nullptr) {
      out[0] = out[1] = out[2] = 0.0f;
      return;
    }
    scene_->GetGravity().getValue(out);
  }

  bool WorldBounds(KX_GameObject *object, float min[3], float max[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    const MT_Vector3 position = object->NodeGetWorldPosition();
    const MT_Vector3 scale = object->NodeGetWorldScaling();
    float extent[3] = {1.0f, 1.0f, 1.0f};
    RAS_MeshObject *mesh = object->GetMesh(0);
    if (mesh != nullptr) {
      RAS_IDisplayArray *array = mesh->GetDisplayArray(0);
      if (array != nullptr && array->GetVertexCount() != 0) {
        float bounds_min[3] = {std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
        float bounds_max[3] = {-std::numeric_limits<float>::max(),
                               -std::numeric_limits<float>::max(),
                               -std::numeric_limits<float>::max()};
        const unsigned int count = array->GetVertexCount();
        for (unsigned int i = 0; i < count; ++i) {
          RAS_IVertex *vertex = array->GetVertex(i);
          const float *xyz = vertex != nullptr ? vertex->getXYZ() : nullptr;
          if (xyz == nullptr) {
            continue;
          }
          for (int axis = 0; axis < 3; ++axis) {
            bounds_min[axis] = std::min(bounds_min[axis], xyz[axis]);
            bounds_max[axis] = std::max(bounds_max[axis], xyz[axis]);
          }
        }
        for (int axis = 0; axis < 3; ++axis) {
          extent[axis] = (bounds_max[axis] - bounds_min[axis]) * 0.5f * scale[axis];
        }
      }
    }
    for (int axis = 0; axis < 3; ++axis) {
      min[axis] = position[axis] - std::abs(extent[axis]);
      max[axis] = position[axis] + std::abs(extent[axis]);
    }
    return true;
  }

  void SetDynamics(KX_GameObject *object, bool enable) override
  {
    PHY_IPhysicsController *controller = Controller(object);
    if (controller == nullptr) {
      return;
    }
    if (enable) {
      controller->RestoreDynamics();
    }
    else {
      controller->SuspendDynamics();
    }
  }

  void SetRigidBody(KX_GameObject *object, bool enable) override
  {
    PHY_IPhysicsController *controller = Controller(object);
    if (controller != nullptr) {
      controller->SetRigidBody(enable);
    }
  }

  void SetCollisionMask(KX_GameObject *object, int mask) override
  {
    if (object != nullptr) {
      object->SetCollisionMask(static_cast<unsigned short>(mask));
    }
  }

  bool CharacterJump(KX_GameObject *object, const float velocity[3]) override
  {
    PHY_ICharacter *character = Character(object);
    if (character == nullptr) {
      return false;
    }
    (void)velocity;
    character->Jump();
    return true;
  }

  void SetCharacterWalkDirection(KX_GameObject *object, const float direction[3]) override
  {
    PHY_ICharacter *character = Character(object);
    if (character != nullptr) {
      character->SetWalkDirection(MT_Vector3(direction));
    }
  }

  void SetCharacterVelocity(KX_GameObject *object, const float velocity[3]) override
  {
    PHY_ICharacter *character = Character(object);
    if (character != nullptr) {
      character->SetVelocity(MT_Vector3(velocity), 0.0f, false);
    }
  }

  void SetCharacterGravity(KX_GameObject *object, const float gravity[3]) override
  {
    PHY_ICharacter *character = Character(object);
    if (character != nullptr) {
      character->SetGravity(MT_Vector3(gravity));
    }
  }

  void SetCharacterMaxJumps(KX_GameObject *object, int jumps) override
  {
    PHY_ICharacter *character = Character(object);
    if (character != nullptr) {
      character->SetMaxJumps(jumps);
    }
  }

  void SetCharacterJumpSpeed(KX_GameObject *object, float speed) override
  {
    PHY_ICharacter *character = Character(object);
    if (character != nullptr) {
      character->SetJumpSpeed(speed);
    }
  }

 private:
  static PHY_ICharacter *Character(KX_GameObject *object)
  {
    KX_Scene *scene = object != nullptr ? object->GetScene() : nullptr;
    if (scene == nullptr) {
      return nullptr;
    }
    PHY_IPhysicsEnvironment *environment = scene->GetPhysicsEnvironment();
    return environment != nullptr ? environment->GetCharacterController(object) : nullptr;
  }

  static PHY_IPhysicsController *Controller(KX_GameObject *object)
  {
    return object != nullptr ? object->GetPhysicsController() : nullptr;
  }

  KX_Scene *scene_;
};

class EngineObjectAdapter final : public KX_ObjectAdapter {
 public:
  bool GetProperty(KX_GameObject *object,
                   const std::string &name,
                   KX_RuntimeValue *out,
                   KX_ValuePool *pool = nullptr) override
  {
    if (object == nullptr || out == nullptr) {
      return false;
    }
    KX_PropertyBackend *backend = KX_DefaultPropertyBackend();
    if (backend == nullptr || backend->Type(object, name) == KX_VALUE_NONE) {
      return false;
    }
    *out = backend->Read(object, name, pool);
    return true;
  }

  bool SetProperty(KX_GameObject *object,
                   const std::string &name,
                   const KX_RuntimeValue &value) override
  {
    if (object == nullptr) {
      return false;
    }
    KX_PropertyBackend *backend = KX_DefaultPropertyBackend();
    if (backend == nullptr) {
      return false;
    }
    if (backend->Type(object, name) == KX_VALUE_NONE) {
      return backend->Create(object, name, value.type, value);
    }
    return backend->Write(object, name, value);
  }

  bool HasProperty(KX_GameObject *object, const std::string &name) override
  {
    return object != nullptr && KX_DefaultPropertyBackend() != nullptr &&
           KX_DefaultPropertyBackend()->Type(object, name) != KX_VALUE_NONE;
  }

  unsigned short GetCollisionGroup(KX_GameObject *object) const override
  {
    return object != nullptr ? object->GetCollisionGroup() : 0;
  }

  bool GetLinearVelocity(KX_GameObject *object, float out[3]) const override
  {
    if (object == nullptr) {
      out[0] = out[1] = out[2] = 0.0f;
      return false;
    }
    MT_Vector3 velocity = object->GetLinearVelocity(false);
    velocity.getValue(out);
    return true;
  }

  bool GetWorldPosition(KX_GameObject *object, float out[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeGetWorldPosition().getValue(out);
    return true;
  }

  bool GetWorldOrientation(KX_GameObject *object, float out[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    float yaw;
    float pitch;
    float roll;
    object->NodeGetWorldOrientation().getEuler(yaw, pitch, roll);
    /* Match SetWorldOrientation's Euler order (x, y, z). */
    out[0] = yaw;
    out[1] = pitch;
    out[2] = roll;
    return true;
  }

  bool GetWorldScale(KX_GameObject *object, float out[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeGetWorldScaling().getValue(out);
    return true;
  }

  bool SetLocalPosition(KX_GameObject *object, const float value[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeSetLocalPosition(MT_Vector3(value));
    object->NodeUpdateGS(0.0);
    return true;
  }

  bool SetLocalOrientation(KX_GameObject *object, const float value[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeSetLocalOrientation(MT_Matrix3x3(MT_Vector3(value)));
    object->NodeUpdateGS(0.0);
    return true;
  }

  bool SetLocalScale(KX_GameObject *object, const float value[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeSetLocalScale(MT_Vector3(value));
    object->NodeUpdateGS(0.0);
    return true;
  }

  bool SetWorldPosition(KX_GameObject *object, const float value[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeSetWorldPosition(MT_Vector3(value));
    object->NodeUpdateGS(0.0);
    return true;
  }

  bool SetWorldOrientation(KX_GameObject *object, const float value[3]) override
  {
    if (object == nullptr) {
      return false;
    }
    object->NodeSetGlobalOrientation(MT_Matrix3x3(MT_Vector3(value)));
    object->NodeUpdateGS(0.0);
    return true;
  }

  bool GetMeshVertices(KX_GameObject *object,
                       std::vector<KX_RuntimeValue> &out,
                       size_t max_count) override
  {
    out.clear();
    if (object == nullptr) {
      return false;
    }
    const MT_Vector3 position = object->NodeGetWorldPosition();
    const MT_Matrix3x3 orientation = object->NodeGetWorldOrientation();
    const MT_Vector3 scale = object->NodeGetWorldScaling();

    const int mesh_count = object->GetMeshCount();
    for (int mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
      RAS_MeshObject *mesh = object->GetMesh(mesh_index);
      if (mesh == nullptr) {
        continue;
      }
      RAS_IDisplayArray *array = mesh->GetDisplayArray(0);
      if (array == nullptr) {
        continue;
      }
      const unsigned int count = array->GetVertexCount();
      for (unsigned int i = 0; i < count && out.size() < max_count; ++i) {
        RAS_IVertex *vertex = array->GetVertex(i);
        const float *xyz = vertex != nullptr ? vertex->getXYZ() : nullptr;
        if (xyz == nullptr) {
          continue;
        }
        MT_Vector3 local(xyz[0] * scale[0], xyz[1] * scale[1], xyz[2] * scale[2]);
        MT_Vector3 world = position + orientation * local;
        float data[3];
        world.getValue(data);
        out.push_back(KX_RuntimeValue::Vector(data[0], data[1], data[2]));
      }
    }
    return true;
  }

  bool GetVisible(KX_GameObject *object) override
  {
    return object != nullptr && object->GetVisible();
  }

  void SetVisible(KX_GameObject *object, bool visible) override
  {
    if (object != nullptr) {
      object->SetVisible(visible, false);
    }
  }

  bool ReplaceMesh(KX_GameObject *object, const std::string &mesh) override
  {
    (void)object;
    (void)mesh;
    /* Mesh replacement stays deferred: it needs the converter level material
     * swap path, which is not part of the node runtime yet. */
    return false;
  }

  bool SetMaterial(KX_GameObject *object, const std::string &name, int slot) override
  {
    if (object == nullptr) {
      return false;
    }
    blender::Object *blender_object = object->GetBlenderObject();
    if (blender_object == nullptr) {
      return false;
    }
    blender::Material *material = reinterpret_cast<blender::Material *>(
        blender::BKE_libblock_find_name(blender::G.main, blender::ID_MA, name.c_str()));
    if (material == nullptr) {
      std::fprintf(stderr,
                   "[SetMaterial] material datablock '%s' not found on object '%s'\n",
                   name.c_str(),
                   object->GetName().c_str());
      return false;
    }
    if (slot < 0) {
      slot = 0;
    }
    blender::BKE_object_material_assign_single_obdata(
        blender::G.main, blender_object, material, static_cast<short>(slot + 1));
    object->UpdateBlenderObjectMatrix(blender_object);
    return true;
  }

  KX_GameObject *GetParent(KX_GameObject *object) override
  {
    return object != nullptr ? object->GetParent() : nullptr;
  }

  bool SetParent(KX_GameObject *object, KX_GameObject *parent) override
  {
    if (object == nullptr || parent == nullptr) {
      return false;
    }
    object->SetParent(parent);
    return true;
  }

  void ClearParent(KX_GameObject *object) override
  {
    if (object != nullptr) {
      object->RemoveParent();
    }
  }

  bool PlayAction(KX_GameObject *object, const std::string &name, float blend) override
  {
    if (object == nullptr) {
      return false;
    }
    return object->PlayAction(name, 0.0f, -1.0f, 0, 0, blend);
  }

  bool SetActionFrame(KX_GameObject *object, float frame) override
  {
    if (object == nullptr) {
      return false;
    }
    object->SetActionFrame(0, frame);
    return true;
  }

  void StopAction(KX_GameObject *object) override
  {
    if (object != nullptr) {
      object->StopAction(0);
    }
  }

  std::string GetActionName(KX_GameObject *object) override
  {
    return object != nullptr ? object->GetActionName(0) : std::string();
  }

  float GetActionFrame(KX_GameObject *object) override
  {
    return object != nullptr ? object->GetActionFrame(0) : 0.0f;
  }

  bool SetFontText(KX_GameObject *object, const std::string &text) override
  {
    if (object == nullptr || object->GetGameObjectType() != SCA_IObject::OBJ_TEXT) {
      return false;
    }
    static_cast<KX_FontObject *>(object)->SetText(text);
    return true;
  }

  std::string GetName(KX_GameObject *object) override
  {
    return object != nullptr ? object->GetName() : std::string();
  }

  KX_GameObject *FindChildByName(KX_GameObject *object, const std::string &name) override
  {
    if (object == nullptr) {
      return nullptr;
    }
    for (KX_GameObject *child : object->GetChildren()) {
      if (child != nullptr && child->GetName() == name) {
        return child;
      }
    }
    return nullptr;
  }

  KX_GameObject *FindChildByIndex(KX_GameObject *object, int index) override
  {
    if (object == nullptr || index < 0) {
      return nullptr;
    }
    const std::vector<KX_GameObject *> children = object->GetChildren();
    if (static_cast<size_t>(index) >= children.size()) {
      return nullptr;
    }
    return children[static_cast<size_t>(index)];
  }
};

class EngineAnimationAdapter final : public KX_AnimationAdapter {
 public:
  bool PlayAction(KX_GameObject *object,
                  const std::string &name,
                  int layer,
                  KX_AnimationPlayMode mode,
                  float speed,
                  float start_frame,
                  float end_frame,
                  float weight) override
  {
    if (object == nullptr || name.empty()) {
      return false;
    }
    return object->PlayAction(name,
                              start_frame,
                              end_frame,
                              static_cast<short>(layer),
                              0,
                              1.0f,
                              static_cast<short>(mode),
                              weight,
                              0,
                              speed,
                              0);
  }

  void StopAction(KX_GameObject *object, int layer) override
  {
    if (object != nullptr) {
      object->StopAction(static_cast<short>(layer));
    }
  }

  bool SetActionFrame(KX_GameObject *object, int layer, float frame) override
  {
    if (object == nullptr) {
      return false;
    }
    object->SetActionFrame(static_cast<short>(layer), frame);
    return true;
  }

  bool SetActionSpeed(KX_GameObject *object, int layer, float speed) override
  {
    if (object == nullptr) {
      return false;
    }
    object->SetActionSpeed(static_cast<short>(layer), speed);
    return true;
  }

  bool SetLayerWeight(KX_GameObject *object, int layer, float weight) override
  {
    if (object == nullptr) {
      return false;
    }
    object->SetLayerWeight(static_cast<short>(layer), weight);
    return true;
  }

  float GetActionFrame(KX_GameObject *object, int layer) override
  {
    return object != nullptr ? object->GetActionFrame(static_cast<short>(layer)) : 0.0f;
  }

  bool IsActionDone(KX_GameObject *object, int layer) override
  {
    return object != nullptr ? object->IsActionDone(static_cast<short>(layer)) : true;
  }

  std::string GetActionName(KX_GameObject *object, int layer) override
  {
    return object != nullptr ? object->GetActionName(static_cast<short>(layer)) : std::string();
  }

  /* --- B6.1 animation advanced --- */

  bool PlayActionByLayer(KX_GameObject *object,
                         const std::string &name,
                         int layer,
                         float blend_in) override
  {
    if (object == nullptr || name.empty()) {
      return false;
    }
    float start = 0.0f;
    float end = 0.0f;
    if (!ActionRange(object, name, start, end)) {
      fprintf(stderr,
              "[PlayActionByLayer] action '%s' has no frame range, cannot play it\n",
              name.c_str());
      return false;
    }
    return object->PlayAction(name,
                              start,
                              end,
                              static_cast<short>(layer),
                              0,
                              blend_in,
                              0,
                              1.0f,
                              0,
                              1.0f,
                              0);
  }

  bool BlendActions(KX_GameObject *object,
                    const std::string &action_a,
                    const std::string &action_b,
                    float factor) override
  {
    if (object == nullptr || action_a.empty() || action_b.empty()) {
      return false;
    }
    if (!(factor >= 0.0f && factor <= 1.0f)) {
      fprintf(stderr, "[BlendActions] factor %.3f is outside [0, 1]\n", factor);
      return false;
    }
    float a_start = 0.0f, a_end = 0.0f, b_start = 0.0f, b_end = 0.0f;
    if (!ActionRange(object, action_a, a_start, a_end) ||
        !ActionRange(object, action_b, b_start, b_end)) {
      fprintf(stderr, "[BlendActions] both actions need a frame range\n");
      return false;
    }
    /* Real layer weight blending: A on layer 0, B on layer 1. BL_Action applies
     * the layer weight through BL_ArmatureObject::BlendInPose (dst = 1 - src),
     * so the visible result is (1 - factor) A + factor B. Layer 0 ignores its
     * weight (BL_ActionManager forces it to -1), B alone drives the blend.
     * Fixed occupancy: this overwrites whatever was playing on layers 0 and 1,
     * and only two actions can be mixed (no N-way weighted blending). */
    return object->PlayAction(action_a, a_start, a_end, 0, 0, 0.0f, 0, 1.0f - factor, 0, 1.0f, 0) &&
           object->PlayAction(action_b, b_start, b_end, 1, 0, 0.0f, 0, factor, 0, 1.0f, 0);
  }

  bool SetBoneTransform(KX_GameObject *object,
                        const std::string &bone,
                        const float position[3],
                        const float rotation[3]) override
  {
    if (object == nullptr || bone.empty()) {
      return false;
    }
    if (object->GetGameObjectType() != SCA_IObject::OBJ_ARMATURE) {
      fprintf(stderr, "[SetBoneTransform] object '%s' is not an armature\n", object->GetName().c_str());
      return false;
    }
    BL_ArmatureObject *armature = static_cast<BL_ArmatureObject *>(object);
    blender::bPose *pose = nullptr;
    armature->GetPose(&pose);
    if (pose == nullptr) {
      fprintf(stderr, "[SetBoneTransform] armature '%s' has no pose\n", object->GetName().c_str());
      return false;
    }
    blender::bPoseChannel *channel = blender::BKE_pose_channel_find_name(pose, bone.c_str());
    if (channel == nullptr) {
      fprintf(stderr,
              "[SetBoneTransform] bone '%s' not found on armature '%s'\n",
              bone.c_str(),
              object->GetName().c_str());
      return false;
    }
    channel->loc[0] = position[0];
    channel->loc[1] = position[1];
    channel->loc[2] = position[2];
    channel->rotmode = blender::ROT_MODE_XYZ;
    channel->eul[0] = rotation[0];
    channel->eul[1] = rotation[1];
    channel->eul[2] = rotation[2];
    /* The pose is recomputed on the next armature update; the engine exposes
     * no immediate re-evaluation hook. */
    armature->ApplyPose();
    return true;
  }

  bool SetBoneWeight(KX_GameObject *object, const std::string &bone, float weight, std::string *error) override
  {
    (void)object;
    (void)bone;
    (void)weight;
    if (error != nullptr) {
      *error =
          "vertex group weights are evaluated at conversion time; the runtime has no setter";
    }
    return false;
  }

  bool PlayBoneAction(KX_GameObject *object,
                      const std::string &bone,
                      const std::string &action,
                      std::string *error) override
  {
    (void)object;
    (void)bone;
    (void)action;
    if (error != nullptr) {
      *error = "BL_ActionManager plays whole object actions; per bone masking is not exposed";
    }
    return false;
  }

  float GetActionNormalized(KX_GameObject *object, int layer) override
  {
    if (object == nullptr) {
      return 0.0f;
    }
    BL_ActionManager *manager = object->GetActionManagerNoCreate();
    if (manager == nullptr) {
      return 0.0f;
    }
    blender::bAction *action = manager->GetCurrentAction(static_cast<short>(layer));
    if (action == nullptr) {
      return 0.0f;
    }
    const float start = action->frame_start;
    const float end = action->frame_end;
    if (!(end > start)) {
      return 0.0f;
    }
    const float frame = object->GetActionFrame(static_cast<short>(layer));
    const float normalized = (frame - start) / (end - start);
    return std::max(0.0f, std::min(1.0f, normalized));
  }

 private:
  static bool ActionRange(KX_GameObject *object, const std::string &name, float &start, float &end)
  {
    if (object == nullptr || object->GetScene() == nullptr) {
      return false;
    }
    SCA_LogicManager *manager = object->GetScene()->GetLogicManager();
    if (manager == nullptr) {
      return false;
    }
    blender::bAction *action = static_cast<blender::bAction *>(manager->GetActionByName(name));
    if (action == nullptr) {
      return false;
    }
    start = action->frame_start;
    end = action->frame_end;
    return end > start;
  }
};

class EngineMessageAdapter final : public KX_MessageAdapter {
 public:
  explicit EngineMessageAdapter(KX_Scene *scene) : scene_(scene)
  {
  }

  void Send(const std::string &to,
            KX_GameObject *from,
            const std::string &subject,
            const std::string &body) override
  {
    if (scene_ == nullptr || scene_->GetNetworkMessageScene() == nullptr) {
      return;
    }
    scene_->GetNetworkMessageScene()->SendMessage(
        to, static_cast<SCA_IObject *>(from), subject, body);
  }

  std::vector<KX_Message> Receive(const std::string &to, const std::string &subject) override
  {
    std::vector<KX_Message> result;
    if (scene_ == nullptr || scene_->GetNetworkMessageScene() == nullptr) {
      return result;
    }
    const std::vector<KX_NetworkMessageManager::Message> found =
        scene_->GetNetworkMessageScene()->FindMessages(to, subject);
    result.reserve(found.size());
    for (const KX_NetworkMessageManager::Message &message : found) {
      KX_Message out;
      out.to = message.to;
      out.from = message.from != nullptr ? static_cast<KX_GameObject *>(message.from)->GetName() :
                                          std::string();
      out.subject = message.subject;
      out.body = message.body;
      result.push_back(out);
    }
    return result;
  }

 private:
  KX_Scene *scene_;
};

/**
 * Scene lifecycle controls used by Suspend/Resume/AddObject.
 *
 * Suspension is a scene wide flag: the engine skips the logic tick and physics
 * step of a suspended scene while rendering keeps running. Spawning reuses the
 * same replica path as the legacy Add Object actuator, so the new object gets
 * its logic, properties and layers from the inactive-layer original.
 */
class EngineSceneControlAdapter final : public KX_SceneControlAdapter {
 public:
  explicit EngineSceneControlAdapter(KX_Scene *scene) : scene_(scene) {}

  bool SuspendScene() override
  {
    if (scene_ == nullptr) {
      return false;
    }
    scene_->SetSuspended(true);
    return true;
  }

  bool ResumeScene() override
  {
    if (scene_ == nullptr) {
      return false;
    }
    scene_->SetSuspended(false);
    return true;
  }

  bool IsSuspended() const override
  {
    return scene_ != nullptr && scene_->IsSuspended();
  }

  KX_GameObject *AddObject(const std::string &name) override
  {
    if (scene_ == nullptr || name.empty()) {
      return nullptr;
    }
    EXP_ListValue<KX_GameObject> *inactive = scene_->GetInactiveList();
    if (inactive == nullptr) {
      std::fprintf(stderr,
                   "GameNode: AddObject '%s' requires the object to sit in an inactive layer\n",
                   name.c_str());
      return nullptr;
    }
    KX_GameObject *original = nullptr;
    for (KX_GameObject *object : *inactive) {
      if (object != nullptr && object->GetName() == name) {
        original = object;
        break;
      }
    }
    if (original == nullptr) {
      std::fprintf(stderr,
                   "GameNode: AddObject '%s' not found in an inactive layer\n",
                   name.c_str());
      return nullptr;
    }
    KX_GameObject *replica = scene_->AddReplicaObject(original, nullptr, 0.0f);
    if (replica == nullptr) {
      return nullptr;
    }
    /* AddReplicaObject takes its own reference; drop the conversion reference. */
    replica->Release();
    return replica;
  }

  bool HasScene(const std::string &name) override
  {
    if (name.empty()) {
      return false;
    }
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine == nullptr) {
      return false;
    }
    for (KX_Scene *scene : engine->CurrentScenes()) {
      if (scene != nullptr && scene->GetName() == name) {
        return true;
      }
    }
    return false;
  }

  bool UnloadScene(const std::string &name) override
  {
    return UnloadSceneDetailed(name, nullptr) == KX_SceneUnloadResult::Success;
  }

  KX_SceneUnloadResult UnloadSceneDetailed(const std::string &name,
                                           std::string *reason) override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine == nullptr) {
      if (reason != nullptr) {
        *reason = "no active engine";
      }
      return KX_SceneUnloadResult::Unsupported;
    }
    if (name.empty() || !HasScene(name)) {
      if (reason != nullptr) {
        *reason = "scene not found";
      }
      return KX_SceneUnloadResult::Missing;
    }
    /* A scene in the running list owns live graphs and objects; freeing it
     * would invalidate nodes mid-frame. Refuse instead of crashing. */
    for (KX_Scene *active : engine->CurrentScenes()) {
      if (active != nullptr && active->GetName() == name) {
        if (reason != nullptr) {
          *reason = "scene is active";
        }
        return KX_SceneUnloadResult::SceneActive;
      }
    }
    /* KX_KetsjiEngine::RemoveScene is disabled in this build (see the early
     * return in KX_KetsjiEngine.cpp around line 1343), so removal cannot be
     * honoured even for a non-active scene. */
    if (reason != nullptr) {
      *reason = "engine RemoveScene is disabled";
    }
    return KX_SceneUnloadResult::Unsupported;
  }

  void ActiveSceneNames(std::vector<std::string> &out) override
  {
    out.clear();
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine == nullptr) {
      return;
    }
    for (KX_Scene *scene : engine->CurrentScenes()) {
      if (scene != nullptr) {
        out.push_back(scene->GetName());
      }
    }
  }

  bool SetSceneTimeScale(const std::string &name, float scale) override
  {
    KX_KetsjiEngine *engine = KX_GetActiveEngine();
    if (engine == nullptr || !HasScene(name) || scale < 0.0f) {
      return false;
    }
    /* The engine only exposes one global time scale; the scene name is
     * validated, then the global scale is applied. Per-scene scaling is a
     * documented limitation, not a fake per-scene result. */
    engine->SetTimeScale(double(scale));
    return true;
  }

 private:
  KX_Scene *scene_ = nullptr;
};

#ifdef WITH_AUDASPACE
/** Shortest-arc rotation mapping audaspace's default listening direction (-Z)
 *  onto #forward. A zero length vector keeps the default orientation. */
aud::Quaternion ListenerOrientationFromForward(const float forward[3])
{
  const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] +
                                 forward[2] * forward[2]);
  if (length < 1e-6f) {
    return aud::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
  }
  const float from[3] = {0.0f, 0.0f, -1.0f};
  const float to[3] = {forward[0] / length, forward[1] / length, forward[2] / length};
  const float dot = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
  const float cross[3] = {from[1] * to[2] - from[2] * to[1],
                          from[2] * to[0] - from[0] * to[2],
                          from[0] * to[1] - from[1] * to[0]};
  const float sine = std::sqrt(
      cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
  if (sine < 1e-6f) {
    /* Parallel or opposite: identity, or a half turn around a stable axis. */
    return dot > 0.0f ? aud::Quaternion(1.0f, 0.0f, 0.0f, 0.0f) :
                        aud::Quaternion(0.0f, 0.0f, 1.0f, 0.0f);
  }
  const float half_angle = std::atan2(sine, dot) * 0.5f;
  const float scale = std::sin(half_angle) / sine;
  return aud::Quaternion(std::cos(half_angle),
                         cross[0] * scale,
                         cross[1] * scale,
                         cross[2] * scale);
}
#endif

/**
 * Audio playback through the same audaspace device used by SCA_SoundActuator.
 * Handles are kept in a map so the nodes can address a sound by an integer id.
 */
class EngineAudioAdapter final : public KX_AudioAdapter {
 public:
  explicit EngineAudioAdapter(KX_Scene *scene) : scene_(scene) {}

  ~EngineAudioAdapter() override
  {
    for (auto &entry : handles_) {
      if (entry.second) {
        entry.second->stop();
      }
    }
    ClearSoundOwners();
  }

  uint64_t PlaySound(const std::string &filepath, bool loop, float volume, float pitch) override
  {
#ifdef WITH_AUDASPACE
    if (filepath.empty()) {
      return 0;
    }
    try {
      return StartHandle(std::make_shared<aud::File>(filepath), loop, volume, pitch);
    }
    catch (aud::Exception &) {
      return 0;
    }
#else
    (void)filepath;
    (void)loop;
    (void)volume;
    (void)pitch;
    return 0;
#endif
  }

  /** Play a bSound datablock of the loaded blend, addressed by its bpy name. */
  uint64_t PlaySoundByName(const std::string &name, bool loop, float volume, float pitch) override
  {
#ifdef WITH_AUDASPACE
    if (name.empty()) {
      std::fprintf(stderr, "GameNode: PlaySoundByName: empty sound name\n");
      return 0;
    }
    BL_SceneConverter *converter = scene_ != nullptr ? scene_->GetBlenderSceneConverter() :
                                                       nullptr;
    blender::bSound *sound = converter != nullptr ? converter->FindSound(name) : nullptr;
    if (sound == nullptr) {
      std::fprintf(stderr,
                   "GameNode: PlaySoundByName: sound datablock '%s' not found\n",
                   name.c_str());
      return 0;
    }
    blender::bContext *context = KX_GetActiveEngine() != nullptr ?
                                     KX_GetActiveEngine()->GetContext() :
                                     nullptr;
    if (context == nullptr) {
      std::fprintf(stderr,
                   "GameNode: PlaySoundByName: no active context for '%s'\n",
                   name.c_str());
      return 0;
    }
    BKE_sound_load_no_assert(CTX_data_main(context), sound);
    AUD_Sound loaded = BKE_sound_playback_handle_get(sound);
    if (!loaded) {
      std::fprintf(stderr,
                   "GameNode: PlaySoundByName: sound '%s' has no playback handle\n",
                   name.c_str());
      return 0;
    }
    return StartHandle(loaded, loop, volume, pitch);
#else
    (void)name;
    (void)loop;
    (void)volume;
    (void)pitch;
    return 0;
#endif
  }

  /** Set the 3D listener transform; applied when the device supports 3D audio. */
  void Set3DListener(const float position[3], const float orientation[3]) override
  {
    if (position != nullptr) {
      std::memcpy(listener_position_, position, sizeof(listener_position_));
    }
    if (orientation != nullptr) {
      std::memcpy(listener_orientation_, orientation, sizeof(listener_orientation_));
    }
#ifdef WITH_AUDASPACE
    auto device = aud::DeviceManager::getDevice();
    if (!device) {
      return;
    }
    auto device3d = std::dynamic_pointer_cast<aud::I3DDevice>(device);
    if (!device3d) {
      return;
    }
    device3d->setListenerLocation(
        aud::Vector3(listener_position_[0], listener_position_[1], listener_position_[2]));
    device3d->setListenerOrientation(ListenerOrientationFromForward(listener_orientation_));
#endif
  }

  void StopSound(uint64_t handle) override
  {
    ReleaseSoundOwner(handle);
    std::map<uint64_t, std::shared_ptr<aud::IHandle>>::iterator it = handles_.find(handle);
    if (it == handles_.end()) {
      return;
    }
    if (it->second) {
      it->second->stop();
    }
    handles_.erase(it);
    base_volume_.erase(handle);
    handle_bus_.erase(handle);
  }

  void SetVolume(uint64_t handle, float volume) override
  {
    auto it = handles_.find(handle);
    if (it != handles_.end() && it->second) {
      it->second->setVolume(volume);
    }
  }

  void SetPitch(uint64_t handle, float pitch) override
  {
    auto it = handles_.find(handle);
    if (it != handles_.end() && it->second) {
      it->second->setPitch(pitch);
    }
  }

  bool IsPlaying(uint64_t handle) override
  {
    auto it = handles_.find(handle);
    return it != handles_.end() && it->second && it->second->getStatus() == aud::STATUS_PLAYING;
  }

  bool IsFinished(uint64_t handle) override
  {
    auto it = handles_.find(handle);
    if (it == handles_.end()) {
      return true;
    }
    if (!it->second) {
      return true;
    }
    const aud::Status status = it->second->getStatus();
    return status != aud::STATUS_PLAYING && status != aud::STATUS_PAUSED;
  }

  /* B6.2 audio extensions. */

  void SetAttenuation(KX_GameObject *object,
                      int model,
                      float reference_distance,
                      float maximum_distance) override
  {
    /* Null object keeps the pre B6.2 global behaviour; a concrete object only
     * touches the handles it owns. */
    if (object == nullptr) {
      last_attenuation_.model = model;
      last_attenuation_.reference_distance = reference_distance;
      last_attenuation_.maximum_distance = maximum_distance;
    }
    else {
      AttenuationSettings settings;
      settings.model = model;
      settings.reference_distance = reference_distance;
      settings.maximum_distance = maximum_distance;
      object_attenuation_[object] = settings;
    }
#ifdef WITH_AUDASPACE
    auto device3d = AudDevice3D();
    if (!device3d) {
      std::fprintf(stderr,
                   "GameNode: SetAudioAttenuation: audio device has no 3D support\n");

      return;
    }
    if (object == nullptr) {
      if (model >= 0 && model <= 5) {
        /* Node ids 0..5 are Inverse..ExponentClamped; audaspace uses 1..6. */
        device3d->setDistanceModel(static_cast<aud::DistanceModel>(model + 1));
      }
      for (std::map<uint64_t, std::shared_ptr<aud::IHandle>>::iterator it = handles_.begin();
           it != handles_.end();
           ++it)
      {
        std::shared_ptr<aud::I3DHandle> handle = std::dynamic_pointer_cast<aud::I3DHandle>(
            it->second);
        if (!handle) {
          continue;
        }
        handle->setDistanceReference(reference_distance);
        handle->setDistanceMaximum(maximum_distance);
        handle->setAttenuation(1.0f);
      }
      return;
    }

    /* audaspace models are device wide, so an object scoped model cannot be
     * isolated; the per handle reference/maximum/attenuation can. */
    if ((model >= 0 && model <= 5) && !attenuation_model_warned_) {
      std::fprintf(stderr,
                   "GameNode: SetAudioAttenuation: distance model is device wide; "
                   "the object scoped model value is not applied per object\n");
      attenuation_model_warned_ = true;
    }
    const std::vector<uint64_t> owned = HandlesFor(object);
    for (uint64_t owned_handle : owned) {
      std::map<uint64_t, std::shared_ptr<aud::IHandle>>::iterator it = handles_.find(
          owned_handle);
      if (it == handles_.end()) {
        continue;
      }
      std::shared_ptr<aud::I3DHandle> handle = std::dynamic_pointer_cast<aud::I3DHandle>(
          it->second);
      if (!handle) {
        continue;
      }
      handle->setDistanceReference(reference_distance);
      handle->setDistanceMaximum(maximum_distance);
      handle->setAttenuation(1.0f);
    }
#else
    (void)reference_distance;
    (void)maximum_distance;
#endif
  }

  void SetDopplerFactor(KX_GameObject *object, float factor) override
  {
    const float clamped = std::max(0.0f, factor);
    if (object == nullptr) {
      last_doppler_ = clamped;
#ifdef WITH_AUDASPACE
      auto device3d = AudDevice3D();
      if (!device3d) {
        std::fprintf(stderr, "GameNode: SetAudioDoppler: audio device has no 3D support\n");
        return;
      }
      device3d->setDopplerFactor(clamped);
#endif
      return;
    }

    object_doppler_[object] = clamped;
#ifdef WITH_AUDASPACE
    /* audaspace exposes a single factor per device. Apply it only when this
     * object owns every registered handle, otherwise keep the previous device
     * value so other objects are not affected. */
    if (handle_owner_.size() != HandlesFor(object).size()) {
      if (!doppler_object_warned_) {
        std::fprintf(stderr,
                     "GameNode: SetAudioDoppler: audaspace doppler is device wide; "
                     "the object scoped value is stored but not applied while other "
                     "objects own handles\n");
        doppler_object_warned_ = true;
      }
      return;
    }
    auto device3d = AudDevice3D();
    if (!device3d) {
      std::fprintf(stderr, "GameNode: SetAudioDoppler: audio device has no 3D support\n");
      return;
    }
    device3d->setDopplerFactor(clamped);
#endif
  }

  /** Object destruction hook: drop ownership and per object settings. */
  void ForgetObject(KX_GameObject *object) override
  {
    object_attenuation_.erase(object);
    object_doppler_.erase(object);
    KX_AudioAdapter::ForgetObject(object);
  }

  uint64_t SetAudioZone(const std::string &zone_name,
                        const std::string &sound,
                        float volume) override
  {
    if (zone_name.empty() || sound.empty()) {
      std::fprintf(stderr, "GameNode: AudioZone: empty zone or sound name\n");
      return 0;
    }
    /* Equivalent implementation: audaspace has no zone concept, so the zone is
     * a looping ambient source at the listener position. Re-registering the
     * same zone replaces (stops) the previous ambient. */
    std::map<std::string, uint64_t>::iterator it = zones_.find(zone_name);
    if (it != zones_.end()) {
      StopSound(it->second);
    }
    const uint64_t handle = PlaySoundByName(sound, true, volume, 1.0f);
    if (handle != 0) {
      zones_[zone_name] = handle;
      base_volume_[handle] = volume;
      handle_bus_[handle] = "master";
    }
    return handle;
  }

  void SetAudioBus(const std::string &bus, float volume) override
  {
    const float gain = std::max(0.0f, std::min(1.0f, volume));
    bus_gain_[bus] = gain;
    /* Equivalent implementation: there is no native bus, so named gain groups
     * multiply the base volume of every handle they own. */
    for (std::map<uint64_t, std::shared_ptr<aud::IHandle>>::iterator it = handles_.begin();
         it != handles_.end();
         ++it)
    {
      std::map<uint64_t, std::string>::const_iterator bus_it = handle_bus_.find(it->first);
      const std::string owner = bus_it != handle_bus_.end() ? bus_it->second : std::string("master");
      if (owner != bus || !it->second) {
        continue;
      }
      std::map<uint64_t, float>::const_iterator base_it = base_volume_.find(it->first);
      const float base = base_it != base_volume_.end() ? base_it->second : 1.0f;
      it->second->setVolume(base * gain);
    }
  }

  uint64_t PlaySound3D(const std::string &sound,
                       const float position[3],
                       float volume,
                       float pitch) override
  {
    const uint64_t handle = PlaySoundByName(sound, false, volume, pitch);
    if (handle == 0) {
      return 0;
    }
    base_volume_[handle] = volume;
    handle_bus_[handle] = "master";
#ifdef WITH_AUDASPACE
    std::map<uint64_t, std::shared_ptr<aud::IHandle>>::iterator it = handles_.find(handle);
    if (it == handles_.end() || !it->second) {
      return handle;
    }
    std::shared_ptr<aud::I3DHandle> handle3d = std::dynamic_pointer_cast<aud::I3DHandle>(
        it->second);
    if (!handle3d) {
      std::fprintf(stderr,
                   "GameNode: PlaySound3D: audio device does not expose a 3D handle\n");
      return handle;
    }
    const float *origin = position != nullptr ? position : listener_position_;
    handle3d->setRelative(false);
    handle3d->setLocation(aud::Vector3(origin[0], origin[1], origin[2]));
    if (last_attenuation_.reference_distance > 0.0f) {
      handle3d->setDistanceReference(last_attenuation_.reference_distance);
    }
    if (last_attenuation_.maximum_distance > 0.0f) {
      handle3d->setDistanceMaximum(last_attenuation_.maximum_distance);
    }
#else
    (void)position;
#endif
    return handle;
  }

  /** PlaySound3D plus ownership and any object scoped attenuation. */
  uint64_t PlaySound3DFor(KX_GameObject *object,
                          const std::string &sound,
                          const float position[3],
                          float volume,
                          float pitch) override
  {
    const uint64_t handle = PlaySound3D(sound, position, volume, pitch);
    RegisterSoundOwner(object, handle);
    if (object == nullptr || handle == 0) {
      return handle;
    }
    std::map<KX_GameObject *, AttenuationSettings>::const_iterator found =
        object_attenuation_.find(object);
    if (found == object_attenuation_.end()) {
      return handle;
    }
#ifdef WITH_AUDASPACE
    std::map<uint64_t, std::shared_ptr<aud::IHandle>>::iterator it = handles_.find(handle);
    if (it == handles_.end()) {
      return handle;
    }
    std::shared_ptr<aud::I3DHandle> handle3d = std::dynamic_pointer_cast<aud::I3DHandle>(
        it->second);
    if (!handle3d) {
      return handle;
    }
    handle3d->setDistanceReference(found->second.reference_distance);
    handle3d->setDistanceMaximum(found->second.maximum_distance);
    handle3d->setAttenuation(1.0f);
#endif
    return handle;
  }

 private:
#ifdef WITH_AUDASPACE
  std::shared_ptr<aud::I3DDevice> AudDevice3D()
  {
    auto device = aud::DeviceManager::getDevice();
    if (!device) {
      return nullptr;
    }
    return std::dynamic_pointer_cast<aud::I3DDevice>(device);
  }
#endif


#ifdef WITH_AUDASPACE
  /**
   * Allocate the next node visible handle.
   *
   * Node ports carry handles as 32-bit integers (#KX_VALUE_INT), so ids stay in
   * 1..INT32_MAX; a wider id could not survive the node boundary and would be
   * rejected there. Ids that are still in #handles_ are skipped.
   */
  uint64_t AllocateHandle()
  {
    const uint64_t limit = 0x7fffffffu;
    for (size_t attempt = 0; attempt <= handles_.size(); ++attempt) {
      const uint64_t id = next_handle_;
      next_handle_ = id >= limit ? 1u : id + 1u;
      if (handles_.find(id) == handles_.end()) {
        return id;
      }
    }
    return 0;
  }

  uint64_t StartHandle(AUD_Sound sound, bool loop, float volume, float pitch)
  {
    if (!sound) {
      return 0;
    }
    try {
      auto device = aud::DeviceManager::getDevice();
      if (!device) {
        return 0;
      }
      std::shared_ptr<aud::IHandle> handle = device->play(sound, false);
      if (!handle) {
        return 0;
      }
      if (loop) {
        handle->setLoopCount(-1);
      }
      handle->setVolume(volume);
      handle->setPitch(pitch);
      const uint64_t id = AllocateHandle();
      if (id == 0u) {
        /* No free id left in the node visible range; the shared handle above
         * stops itself when this scope ends. */
        return 0;
      }
      handles_[id] = handle;
      base_volume_[id] = volume;
      handle_bus_[id] = "master";
      return id;
    }
    catch (aud::Exception &) {
      return 0;
    }
  }
#endif

  struct AttenuationSettings {
    int model = 0;
    float reference_distance = 0.0f;
    float maximum_distance = 0.0f;
  };

  uint64_t next_handle_ = 1;
  std::map<uint64_t, std::shared_ptr<aud::IHandle>> handles_;
  std::map<uint64_t, float> base_volume_;
  std::map<uint64_t, std::string> handle_bus_;
  std::map<std::string, float> bus_gain_;
  std::map<std::string, uint64_t> zones_;
  std::map<KX_GameObject *, AttenuationSettings> object_attenuation_;
  std::map<KX_GameObject *, float> object_doppler_;
  AttenuationSettings last_attenuation_;
  float last_doppler_ = 1.0f;
  bool attenuation_model_warned_ = false;
  bool doppler_object_warned_ = false;
  KX_Scene *scene_ = nullptr;
  float listener_position_[3] = {0.0f, 0.0f, 0.0f};
  float listener_orientation_[3] = {0.0f, 0.0f, -1.0f};
};

}  // namespace

std::unique_ptr<KX_AdapterSet> KX_CreateEngineAdapters(KX_Scene *scene)
{
  if (scene == nullptr) {
    return KX_CreateNullAdapters();
  }

  std::unique_ptr<KX_AdapterSet> set = std::make_unique<KX_AdapterSet>();
  set->input = std::make_unique<EngineInputAdapter>(scene);
  set->scene = std::make_unique<EngineSceneAdapter>(scene);
  set->physics = std::make_unique<EnginePhysicsAdapter>(scene);
  set->objects = std::make_unique<EngineObjectAdapter>();
  set->animation = std::make_unique<EngineAnimationAdapter>();
  set->message = std::make_unique<EngineMessageAdapter>(scene);
  set->audio = std::make_unique<EngineAudioAdapter>(scene);
  /* B6.2: object removal must release audio ownership before the pointer dies. */
  static_cast<EngineSceneAdapter *>(set->scene.get())->SetAudioAdapter(set->audio.get());
  set->scene_control = std::make_unique<EngineSceneControlAdapter>(scene);
  set->ui = KX_CreateEngineUIAdapter(scene);
  set->file = KX_CreateEngineFileAdapter(scene);
  set->network = KX_CreateEngineNetworkAdapter();
  set->physics_advanced = std::unique_ptr<KX_PhysicsAdvancedAdapter>(
      KX_CreateEnginePhysicsAdvancedAdapter(scene));
  return set;
}
