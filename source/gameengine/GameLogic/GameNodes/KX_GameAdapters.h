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

/** \file KX_GameAdapters.h
 *  \ingroup gamelogic
 *
 *  Engine services exposed to compiled game nodes.
 *
 *  Nodes never include Ketsji headers directly. They talk to these four
 *  interfaces, which keeps the node code testable (neutral implementations),
 *  keeps the engine coupling in one place and lets the graph manager compute
 *  input edges once per scene frame instead of once per graph.
 *
 *  Every method has a neutral default implementation, so the null adapters used
 *  by unit tests and headless builds are just empty instances.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_PhysicsAdvancedAdapter.h"
#include "KX_FileAdapter.h"
#include "KX_RuntimeValue.h"
#include "KX_UIElement.h"

class KX_GameObject;
class KX_ValuePool;
class KX_Scene;

/** Edge mode shared by keyboard, mouse and joystick queries. */
enum class KX_InputMode {
  Press = 0,   /* just activated this frame */
  Release = 1, /* just released this frame */
  Hold = 2,    /* active, including the activation frame */
};

class KX_InputAdapter {
 public:
  virtual ~KX_InputAdapter() = default;

  /** Compute this frame's edges; called once by the scene manager before Tick. */
  virtual void BeginFrame(float delta_time)
  {
    (void)delta_time;
  }
  virtual void EndFrame() {}

  virtual bool KeyActive(int code, KX_InputMode mode)
  {
    (void)code;
    (void)mode;
    return false;
  }
  virtual bool MouseButtonActive(int button, KX_InputMode mode)
  {
    (void)button;
    (void)mode;
    return false;
  }
  /** Mouse position in pixels, origin top left. */
  virtual void MousePosition(float &x, float &y) const
  {
    x = 0.0f;
    y = 0.0f;
  }
  /** Mouse position normalized to 0..1, origin bottom left (editor convention). */
  virtual void MouseNormalized(float &x, float &y) const
  {
    x = 0.0f;
    y = 0.0f;
  }
  /** Mouse movement since the previous frame, in pixels. */
  virtual void MouseDelta(float &dx, float &dy) const
  {
    dx = 0.0f;
    dy = 0.0f;
  }
  virtual int MouseWheel() const { return 0; }
  /** Move the cursor; coordinates normalized 0..1. */
  virtual void SetMousePosition(float nx, float ny)
  {
    (void)nx;
    (void)ny;
  }
  virtual void SetCursorVisible(bool visible) { (void)visible; }
  virtual bool JoystickButton(int index, KX_InputMode mode)
  {
    (void)index;
    (void)mode;
    return false;
  }
  virtual float JoystickAxis(int axis) const
  {
    (void)axis;
    return 0.0f;
  }

  /* ---------------------------------------------------------------------- */
  /* B6.2 input extensions. The virtuals above are kept in their original     */
  /* order; these are appended so existing adapters and tests stay valid.     */
  /* ---------------------------------------------------------------------- */

  /** Level state of gamepad #button on #pad; false when the pad is absent. */
  virtual bool IsGamepadButtonDown(int pad, int button)
  {
    (void)pad;
    (void)button;
    return false;
  }
  /** Analog stick pair: #axis 0 is the left stick, 1 the right stick. */
  virtual void GetGamepadAxis(int pad, int axis, float &x, float &y)
  {
    (void)pad;
    (void)axis;
    x = 0.0f;
    y = 0.0f;
  }
  /** Analog trigger: #trigger 0 is left, 1 is right. Value in 0..1. */
  virtual void GetGamepadTrigger(int pad, int trigger, float &value)
  {
    (void)pad;
    (void)trigger;
    value = 0.0f;
  }
  /** Touch point #index in normalized 0..1 screen space. */
  virtual void GetTouch(int index, float &x, float &y, bool &down)
  {
    (void)index;
    x = 0.0f;
    y = 0.0f;
    down = false;
  }
  virtual int GetTouchCount() { return 0; }
  /** True while every key named in a "ctrl+shift+a" style combo is held. */
  virtual bool IsKeyComboDown(const std::string &keys)
  {
    (void)keys;
    return false;
  }
  /** Register a named action binding, see ReadInputMap for the grammar. */
  virtual void SetInputMap(const std::string &name, const std::string &binding)
  {
    input_map_[name] = binding;
  }

  /**
   * Resolve a named action registered with #SetInputMap.
   *
   * Binding grammar:
   *   key:<code>                 keyboard level, value 1/0
   *   keycombo:<keys>            combo level, value 1/0
   *   mouse:<button>             mouse button level, value 1/0
   *   gamepad_button:<pad>:<n>   button level, value 1/0
   *   gamepad_axis:<pad>:<n>     stick, value is the X component
   *   gamepad_trigger:<pad>:<n>  trigger, value 0..1
   *
   * Unknown names or malformed bindings return false without diagnostics; the
   * node layer decides whether to warn.
   */
  virtual bool ReadInputMap(const std::string &name, float &value, bool &down);

  /** Start a vibration; returns false when the pad is absent/unsupported. */
  virtual bool VibrateGamepad(int pad, float intensity, float duration)
  {
    (void)pad;
    (void)intensity;
    (void)duration;
    return false;
  }

 protected:
  std::map<std::string, std::string> input_map_;
};

/**
 * Headless input backend with injectable state.
 *
 * Used by the B6 E2E harness and by headless builds. Keys are injected by
 * lower-case name (for combos), gamepads by (pad, index) pairs; touch points
 * are a sparse vector, and missing indices read as "not down".
 */
class KX_MockInputAdapter : public KX_InputAdapter {
 public:
  struct TouchPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
  };

  std::map<std::pair<int, int>, bool> gamepad_buttons;
  std::map<std::pair<int, int>, std::pair<float, float> > gamepad_axes;
  std::map<std::pair<int, int>, float> gamepad_triggers;
  std::vector<TouchPoint> touches;
  std::map<std::string, bool> pressed_keys;
  std::vector<std::string> vibration_requests;
  uint32_t vibrate_calls = 0;
  bool vibrate_result = true;

  void SetGamepadButton(int pad, int button, bool down);
  void SetGamepadAxis(int pad, int axis, float x, float y);
  void SetGamepadTrigger(int pad, int trigger, float value);
  void SetTouch(int index, float x, float y, bool down);
  void SetKeyDown(const std::string &key, bool down);

  bool IsGamepadButtonDown(int pad, int button) override;
  void GetGamepadAxis(int pad, int axis, float &x, float &y) override;
  void GetGamepadTrigger(int pad, int trigger, float &value) override;
  void GetTouch(int index, float &x, float &y, bool &down) override;
  int GetTouchCount() override;
  bool IsKeyComboDown(const std::string &keys) override;
  bool VibrateGamepad(int pad, float intensity, float duration) override;

  void Reset();
};

/** One object reported by a proximity query, sorted nearest first. */
struct KX_ProximityHit {
  KX_GameObject *object = nullptr;
  float distance = 0.0f;
  /** Angle between the query axis and the direction to the object, degrees. */
  float angle = 0.0f;
};

/** One physics ray hit under the mouse cursor. */
struct KX_RayHit {
  KX_GameObject *object = nullptr;
  float point[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 0.0f, 0.0f};
  float distance = 0.0f;
};

/** One object reported by an overlap query started from a ray hit. */
struct KX_OverlapHit {
  KX_GameObject *object = nullptr;
  /** Contact point, on or near the object surface. */
  float point[3] = {0.0f, 0.0f, 0.0f};
  /** Contact normal, pointing away from the object surface. */
  float normal[3] = {0.0f, 0.0f, 0.0f};
};

/** Callback invoked once per overlap hit. #user is the caller's context. */
using KX_OverlapCallback = void (*)(const KX_OverlapHit &hit, void *user);

class KX_SceneAdapter {
 public:
  virtual ~KX_SceneAdapter() = default;

  virtual KX_GameObject *FindObject(const std::string &name)
  {
    (void)name;
    return nullptr;
  }
  virtual void Objects(std::vector<KX_GameObject *> &out) { (void)out; }

  /**
   * Nearest-first objects inside a sphere or cone around #origin.
   *
   * #local_axis is a direction in the origin's local space; #angle_deg is the
   * full apex angle of the cone and 0 means "no cone, plain sphere". The
   * optional #property filters candidates the same way the legacy sensors
   * filter by game property. Implementations append to #out.
   */
  virtual void QueryProximity(KX_GameObject *origin,
                              const float local_axis[3],
                              float radius,
                              float angle_deg,
                              const std::string &property,
                              std::vector<KX_ProximityHit> &out)
  {
    (void)origin;
    (void)local_axis;
    (void)radius;
    (void)angle_deg;
    (void)property;
    (void)out;
  }
  virtual bool AddObject(const std::string &name)
  {
    (void)name;
    return false;
  }

  /**
   * Physics ray cast through the mouse cursor, matching the legacy
   * camera.getScreenRay path. #nx/#ny are normalized 0..1 with origin bottom
   * left. #camera may be null to use the scene's active camera. This is a
   * real ray hit, not a screen space bounding box approximation, and is
   * therefore distinct from the collision overlap sensors.
   */
  virtual bool RayCastFromScreen(float nx,
                                 float ny,
                                 KX_GameObject *camera,
                                 KX_RayHit &out)
  {
    (void)nx;
    (void)ny;
    (void)camera;
    (void)out;
    return false;
  }

  /**
   * World position on the camera view ray through a screen pixel, at an
   * arbitrary depth. #sx/#sy are pixels with the origin bottom left (the same
   * convention as `KX_SceneAdapter::ScreenWidth/Height`); #depth is in world
   * units measured from the camera plane along the view direction, so 0 gives
   * the camera position and negative values land behind the camera.
   *
   * This is a pure inverse projection: unlike `RayCastFromScreen` it neither
   * uses the physics world nor reports an object. It exists for the legacy
   * WorldPosition node, which asks "what world point does this pixel represent
   * at depth N" rather than "what did the cursor hit".
   */
  virtual bool RayCastFromScreenAnyDepth(float sx, float sy, float depth, float out[3])
  {
    (void)sx;
    (void)sy;
    (void)depth;
    (void)out;
    return false;
  }

  /**
   * Objects overlapping the ray hit #hit. Implementations call #fn once per
   * object, nearest first, with a contact point and normal. #hit.object is
   * always reported first with the ray hit point/normal. Other objects are
   * reported when they lie within #threshold of the hit point.
   *
   * This is the legacy Touch/TARGET sibling of the Collision sensor: it is a
   * point query started from a screen ray, not the physics contact stream, and
   * the two must not be conflated.
   */
  virtual void OverlapQuery(const KX_RayHit &hit,
                            float threshold,
                            KX_OverlapCallback fn,
                            void *user)
  {
    (void)hit;
    (void)threshold;
    (void)fn;
    (void)user;
  }
  virtual void RemoveObject(KX_GameObject *object) { (void)object; }
  virtual bool LoadScene(const std::string &name)
  {
    (void)name;
    return false;
  }
  virtual void SceneNames(std::vector<std::string> &out) { (void)out; }
  virtual std::string SceneName() const { return std::string(); }
  virtual float ScreenWidth() const { return 0.0f; }
  virtual float ScreenHeight() const { return 0.0f; }
  virtual void SetActiveCamera(KX_GameObject *camera) { (void)camera; }
  virtual void SendMessage(KX_GameObject *to, const std::string &subject, const std::string &body)
  {
    (void)to;
    (void)subject;
    (void)body;
  }
  virtual bool FindMessages(KX_GameObject *receiver,
                            const std::string &subject,
                            std::vector<std::string> &out)
  {
    (void)receiver;
    (void)subject;
    (void)out;
    return false;
  }
  virtual void RequestQuit() {}
  virtual void RequestRestart() {}
  virtual void SetTimeScale(float scale) { (void)scale; }
  virtual float TimeScale() const { return 1.0f; }
  virtual void SetResolution(int width, int height)
  {
    (void)width;
    (void)height;
  }
  virtual void SetFullScreen(bool enable) { (void)enable; }
  virtual bool FullScreen() const { return false; }

  /** Canvas size in pixels. */
  virtual void Resolution(int &width, int &height) const
  {
    width = int(ScreenWidth());
    height = int(ScreenHeight());
  }
  virtual bool Vsync() const { return false; }
  virtual void SetVsync(bool enabled) { (void)enabled; }
  /** Active camera of the scene, nullptr when there is none. */
  virtual KX_GameObject *ActiveCamera() { return nullptr; }
  virtual void SetCameraFov(KX_GameObject *camera, float fov)
  {
    (void)camera;
    (void)fov;
  }
  virtual void SetCameraOrthoScale(KX_GameObject *camera, float scale)
  {
    (void)camera;
    (void)scale;
  }
};

class KX_PhysicsAdapter {
 public:
  virtual ~KX_PhysicsAdapter() = default;

  virtual KX_RaycastResult Raycast(const float origin[3],
                                   const float direction[3],
                                   float max_dist,
                                   const KX_GameObject *ignore)
  {
    (void)origin;
    (void)direction;
    (void)max_dist;
    (void)ignore;
    return KX_RaycastResult();
  }
  virtual void SetLinearVelocity(KX_GameObject *object, const float velocity[3])
  {
    (void)object;
    (void)velocity;
  }
  virtual void GetVelocity(KX_GameObject *object, float out[3])
  {
    (void)object;
    out[0] = out[1] = out[2] = 0.0f;
  }
  virtual void ApplyImpulse(KX_GameObject *object,
                            const float point[3],
                            const float impulse[3],
                            bool local)
  {
    (void)object;
    (void)point;
    (void)impulse;
    (void)local;
  }
  virtual void ApplyForce(KX_GameObject *object,
                          const float point[3],
                          const float force[3],
                          bool local)
  {
    (void)object;
    (void)point;
    (void)force;
    (void)local;
  }
  virtual void ApplyTorque(KX_GameObject *object, const float torque[3], bool local)
  {
    (void)object;
    (void)torque;
    (void)local;
  }
  virtual void SetGravity(const float gravity[3]) { (void)gravity; }
  virtual void GetGravity(float out[3]) const { out[0] = out[1] = out[2] = 0.0f; }
  virtual bool WorldBounds(KX_GameObject *object, float min[3], float max[3])
  {
    (void)object;
    (void)min;
    (void)max;
    return false;
  }
  virtual void SetDynamics(KX_GameObject *object, bool enable)
  {
    (void)object;
    (void)enable;
  }
  virtual void SetRigidBody(KX_GameObject *object, bool enable)
  {
    (void)object;
    (void)enable;
  }
  virtual void SetCollisionMask(KX_GameObject *object, int mask)
  {
    (void)object;
    (void)mask;
  }
  virtual bool CharacterJump(KX_GameObject *object, const float velocity[3])
  {
    (void)object;
    (void)velocity;
    return false;
  }
  virtual void SetCharacterWalkDirection(KX_GameObject *object, const float direction[3])
  {
    (void)object;
    (void)direction;
  }
  virtual void SetCharacterVelocity(KX_GameObject *object, const float velocity[3])
  {
    (void)object;
    (void)velocity;
  }
  virtual void SetCharacterGravity(KX_GameObject *object, const float gravity[3])
  {
    (void)object;
    (void)gravity;
  }
  virtual void SetCharacterMaxJumps(KX_GameObject *object, int jumps)
  {
    (void)object;
    (void)jumps;
  }
  virtual void SetCharacterJumpSpeed(KX_GameObject *object, float speed)
  {
    (void)object;
    (void)speed;
  }
};

class KX_ObjectAdapter {
 public:
  virtual ~KX_ObjectAdapter() = default;

  virtual bool GetProperty(KX_GameObject *object,
                           const std::string &name,
                           KX_RuntimeValue *out,
                           KX_ValuePool *pool = nullptr)
  {
    (void)object;
    (void)name;
    (void)out;
    (void)pool;
    return false;
  }
  virtual bool SetProperty(KX_GameObject *object,
                           const std::string &name,
                           const KX_RuntimeValue &value)
  {
    (void)object;
    (void)name;
    (void)value;
    return false;
  }
  virtual bool HasProperty(KX_GameObject *object, const std::string &name)
  {
    (void)object;
    (void)name;
    return false;
  }

  /* ---------------------------------------------------------------------- */
  /* B6.3 sensor extensions (appended; existing virtuals unchanged).          */
  /* ---------------------------------------------------------------------- */

  /** Bullet collision group bits; 0 when the object has no physics body. */
  virtual unsigned short GetCollisionGroup(KX_GameObject *object) const
  {
    (void)object;
    return 0;
  }
  /** World space linear velocity; false when the object has no physics body. */
  virtual bool GetLinearVelocity(KX_GameObject *object, float out[3]) const
  {
    (void)object;
    out[0] = out[1] = out[2] = 0.0f;
    return false;
  }

  virtual bool GetWorldPosition(KX_GameObject *object, float out[3])
  {
    (void)object;
    out[0] = out[1] = out[2] = 0.0f;
    return false;
  }
  virtual bool GetWorldOrientation(KX_GameObject *object, float out[3])
  {
    (void)object;
    out[0] = out[1] = out[2] = 0.0f;
    return false;
  }
  virtual bool GetWorldScale(KX_GameObject *object, float out[3])
  {
    (void)object;
    out[0] = out[1] = out[2] = 0.0f;
    return false;
  }
  virtual bool SetLocalPosition(KX_GameObject *object, const float value[3])
  {
    (void)object;
    (void)value;
    return false;
  }
  virtual bool SetLocalOrientation(KX_GameObject *object, const float value[3])
  {
    (void)object;
    (void)value;
    return false;
  }
  virtual bool SetLocalScale(KX_GameObject *object, const float value[3])
  {
    (void)object;
    (void)value;
    return false;
  }
  /** World-space position; #value is xyz. */
  virtual bool SetWorldPosition(KX_GameObject *object, const float value[3])
  {
    (void)object;
    (void)value;
    return false;
  }
  /** World-space orientation; #value is Euler x, y, z in radians. */
  virtual bool SetWorldOrientation(KX_GameObject *object, const float value[3])
  {
    (void)object;
    (void)value;
    return false;
  }
  /**
   * Replace the material in #slot (0-based) by the datablock named #name.
   * Implementations report a missing material on stderr and return false.
   */
  virtual bool SetMaterial(KX_GameObject *object, const std::string &name, int slot)
  {
    (void)object;
    (void)name;
    (void)slot;
    return false;
  }

  /** Mesh vertices in world space, capped at #max_count entries. */
  virtual bool GetMeshVertices(KX_GameObject *object,
                               std::vector<KX_RuntimeValue> &out,
                               size_t max_count)
  {
    (void)object;
    (void)out;
    (void)max_count;
    return false;
  }

  virtual bool GetVisible(KX_GameObject *object)
  {
    (void)object;
    return false;
  }
  virtual void SetVisible(KX_GameObject *object, bool visible)
  {
    (void)object;
    (void)visible;
  }
  virtual bool ReplaceMesh(KX_GameObject *object, const std::string &mesh)
  {
    (void)object;
    (void)mesh;
    return false;
  }

  virtual KX_GameObject *GetParent(KX_GameObject *object)
  {
    (void)object;
    return nullptr;
  }
  virtual bool SetParent(KX_GameObject *object, KX_GameObject *parent)
  {
    (void)object;
    (void)parent;
    return false;
  }
  virtual void ClearParent(KX_GameObject *object) { (void)object; }

  virtual bool PlayAction(KX_GameObject *object, const std::string &name, float blend)
  {
    (void)object;
    (void)name;
    (void)blend;
    return false;
  }
  virtual bool SetActionFrame(KX_GameObject *object, float frame)
  {
    (void)object;
    (void)frame;
    return false;
  }
  virtual void StopAction(KX_GameObject *object) { (void)object; }
  virtual std::string GetActionName(KX_GameObject *object)
  {
    (void)object;
    return std::string();
  }
  virtual float GetActionFrame(KX_GameObject *object)
  {
    (void)object;
    return 0.0f;
  }

  virtual bool SetFontText(KX_GameObject *object, const std::string &text)
  {
    (void)object;
    (void)text;
    return false;
  }

  /** Object name, empty for a null object. */
  virtual std::string GetName(KX_GameObject *object)
  {
    (void)object;
    return std::string();
  }
  /** Direct children lookups. */
  virtual KX_GameObject *FindChildByName(KX_GameObject *object, const std::string &name)
  {
    (void)object;
    (void)name;
    return nullptr;
  }
  virtual KX_GameObject *FindChildByIndex(KX_GameObject *object, int index)
  {
    (void)object;
    (void)index;
    return nullptr;
  }
};

/** How a newly started action advances its frame. */
enum class KX_AnimationPlayMode : int {
  Play = 0,
  Loop = 1,
  PingPong = 2,
};

/**
 * Animation playback services for the B3 node family.
 *
 * Actions are addressed by layer, like the legacy animation nodes and the
 * BL_ActionManager. A node only talks to this interface, the Ketsji specific
 * calls live in KX_GameAdaptersEngine.cpp.
 */
class KX_AnimationAdapter {
 public:
  virtual ~KX_AnimationAdapter() = default;

  virtual bool PlayAction(KX_GameObject *object,
                          const std::string &name,
                          int layer,
                          KX_AnimationPlayMode mode,
                          float speed,
                          float start_frame,
                          float end_frame,
                          float weight)
  {
    (void)object;
    (void)name;
    (void)layer;
    (void)mode;
    (void)speed;
    (void)start_frame;
    (void)end_frame;
    (void)weight;
    return false;
  }

  virtual void StopAction(KX_GameObject *object, int layer)
  {
    (void)object;
    (void)layer;
  }

  virtual bool SetActionFrame(KX_GameObject *object, int layer, float frame)
  {
    (void)object;
    (void)layer;
    (void)frame;
    return false;
  }

  virtual bool SetActionSpeed(KX_GameObject *object, int layer, float speed)
  {
    (void)object;
    (void)layer;
    (void)speed;
    return false;
  }

  virtual bool SetLayerWeight(KX_GameObject *object, int layer, float weight)
  {
    (void)object;
    (void)layer;
    (void)weight;
    return false;
  }

  virtual float GetActionFrame(KX_GameObject *object, int layer)
  {
    (void)object;
    (void)layer;
    return 0.0f;
  }

  virtual bool IsActionDone(KX_GameObject *object, int layer)
  {
    (void)object;
    (void)layer;
    return true;
  }

  virtual std::string GetActionName(KX_GameObject *object, int layer)
  {
    (void)object;
    (void)layer;
    return std::string();
  }

  /* --- B6.1 animation advanced (appended, existing order preserved) --- */

  virtual bool PlayActionByLayer(KX_GameObject *object,
                                 const std::string &name,
                                 int layer,
                                 float blend_in)
  {
    (void)object;
    (void)name;
    (void)layer;
    (void)blend_in;
    return false;
  }

  virtual bool BlendActions(KX_GameObject *object,
                            const std::string &action_a,
                            const std::string &action_b,
                            float factor)
  {
    (void)object;
    (void)action_a;
    (void)action_b;
    (void)factor;
    return false;
  }

  virtual bool SetBoneTransform(KX_GameObject *object,
                                const std::string &bone,
                                const float position[3],
                                const float rotation[3])
  {
    (void)object;
    (void)bone;
    (void)position;
    (void)rotation;
    return false;
  }

  virtual bool SetBoneWeight(KX_GameObject *object, const std::string &bone, float weight, std::string *error)
  {
    (void)object;
    (void)bone;
    (void)weight;
    if (error != nullptr) {
      *error = "bone weight is not exposed by the engine";
    }
    return false;
  }

  virtual bool PlayBoneAction(KX_GameObject *object,
                              const std::string &bone,
                              const std::string &action,
                              std::string *error)
  {
    (void)object;
    (void)bone;
    (void)action;
    if (error != nullptr) {
      *error = "per bone action playback is not exposed by the engine";
    }
    return false;
  }

  /** 0..1 position inside the action range of @a layer, 0 when unknown. */
  virtual float GetActionNormalized(KX_GameObject *object, int layer)
  {
    (void)object;
    (void)layer;
    return 0.0f;
  }
};

/**
 * Deterministic animation backend used by the headless tests.
 *
 * It records the last call so a test can assert that a node reached the
 * adapter with the expected layer, action and frame.
 */
class KX_MockAnimationAdapter : public KX_AnimationAdapter {
 public:
  KX_GameObject *object = nullptr;
  std::string action;
  int layer = 0;
  KX_AnimationPlayMode mode = KX_AnimationPlayMode::Play;
  float frame = 0.0f;
  float speed = 1.0f;
  float weight = 1.0f;
  bool playing = false;
  bool done = true;

  uint32_t play_calls = 0;
  uint32_t stop_calls = 0;
  uint32_t frame_calls = 0;
  uint32_t speed_calls = 0;
  uint32_t weight_calls = 0;
  /* B6.1 */
  float blend_in = 0.0f;
  std::string blend_action_a;
  std::string blend_action_b;
  float factor = 0.0f;
  std::string bone;
  float bone_position[3] = {0.0f, 0.0f, 0.0f};
  float bone_rotation[3] = {0.0f, 0.0f, 0.0f};
  float bone_weight = 1.0f;
  float normalized = 0.0f;
  bool bone_supported = true;
  uint32_t play_layer_calls = 0;
  uint32_t blend_calls = 0;
  uint32_t bone_transform_calls = 0;
  uint32_t bone_weight_calls = 0;
  uint32_t bone_action_calls = 0;

  bool PlayAction(KX_GameObject *new_object,
                  const std::string &name,
                  int new_layer,
                  KX_AnimationPlayMode new_mode,
                  float new_speed,
                  float start_frame,
                  float end_frame,
                  float new_weight) override
  {
    (void)start_frame;
    (void)end_frame;
    object = new_object;
    action = name;
    layer = new_layer;
    mode = new_mode;
    speed = new_speed;
    weight = new_weight;
    playing = true;
    done = false;
    frame = 0.0f;
    ++play_calls;
    return true;
  }

  void StopAction(KX_GameObject *new_object, int new_layer) override
  {
    object = new_object;
    layer = new_layer;
    playing = false;
    done = true;
    ++stop_calls;
  }

  bool SetActionFrame(KX_GameObject *new_object, int new_layer, float new_frame) override
  {
    object = new_object;
    layer = new_layer;
    frame = new_frame;
    ++frame_calls;
    return true;
  }

  bool SetActionSpeed(KX_GameObject *new_object, int new_layer, float new_speed) override
  {
    object = new_object;
    layer = new_layer;
    speed = new_speed;
    ++speed_calls;
    return true;
  }

  bool SetLayerWeight(KX_GameObject *new_object, int new_layer, float new_weight) override
  {
    object = new_object;
    layer = new_layer;
    weight = new_weight;
    ++weight_calls;
    return true;
  }

  float GetActionFrame(KX_GameObject *new_object, int new_layer) override
  {
    object = new_object;
    layer = new_layer;
    return frame;
  }

  bool IsActionDone(KX_GameObject *new_object, int new_layer) override
  {
    object = new_object;
    layer = new_layer;
    return done;
  }

  std::string GetActionName(KX_GameObject *new_object, int new_layer) override
  {
    object = new_object;
    layer = new_layer;
    return action;
  }

  bool PlayActionByLayer(KX_GameObject *new_object,
                         const std::string &name,
                         int new_layer,
                         float blend_in) override
  {
    object = new_object;
    action = name;
    layer = new_layer;
    this->blend_in = blend_in;
    playing = true;
    done = false;
    ++play_layer_calls;
    return true;
  }

  bool BlendActions(KX_GameObject *new_object,
                    const std::string &action_a,
                    const std::string &action_b,
                    float new_factor) override
  {
    object = new_object;
    blend_action_a = action_a;
    blend_action_b = action_b;
    factor = new_factor;
    ++blend_calls;
    return true;
  }

  bool SetBoneTransform(KX_GameObject *new_object,
                        const std::string &new_bone,
                        const float new_position[3],
                        const float new_rotation[3]) override
  {
    object = new_object;
    bone = new_bone;
    for (int i = 0; i < 3; ++i) {
      bone_position[i] = new_position[i];
      bone_rotation[i] = new_rotation[i];
    }
    ++bone_transform_calls;
    return bone_supported;
  }

  bool SetBoneWeight(KX_GameObject *new_object,
                     const std::string &new_bone,
                     float new_weight,
                     std::string *error) override
  {
    object = new_object;
    bone = new_bone;
    bone_weight = new_weight;
    ++bone_weight_calls;
    if (!bone_supported && error != nullptr) {
      *error = "bone weight is not exposed by the engine";
    }
    return bone_supported;
  }

  bool PlayBoneAction(KX_GameObject *new_object,
                      const std::string &new_bone,
                      const std::string &new_action,
                      std::string *error) override
  {
    object = new_object;
    bone = new_bone;
    action = new_action;
    ++bone_action_calls;
    if (!bone_supported && error != nullptr) {
      *error = "per bone action playback is not exposed by the engine";
    }
    return bone_supported;
  }

  float GetActionNormalized(KX_GameObject *new_object, int new_layer) override
  {
    object = new_object;
    layer = new_layer;
    return normalized;
  }

  void Reset()
  {
    object = nullptr;
    action.clear();
    layer = 0;
    mode = KX_AnimationPlayMode::Play;
    frame = 0.0f;
    speed = 1.0f;
    weight = 1.0f;
    playing = false;
    done = true;
    play_calls = stop_calls = frame_calls = speed_calls = weight_calls = 0;
    blend_in = 0.0f;
    blend_action_a.clear();
    blend_action_b.clear();
    factor = 0.0f;
    bone.clear();
    bone_position[0] = bone_position[1] = bone_position[2] = 0.0f;
    bone_rotation[0] = bone_rotation[1] = bone_rotation[2] = 0.0f;
    bone_weight = 1.0f;
    normalized = 0.0f;
    bone_supported = true;
    play_layer_calls = blend_calls = bone_transform_calls = bone_weight_calls = bone_action_calls = 0;
  }
};

/**
 * Message bus service for the B3 message nodes.
 *
 * The engine implementation wraps KX_NetworkMessageScene, which double buffers
 * the queue: a message sent on frame N is readable on frame N+1 and is cleared
 * at the end of that frame. Messages are addressed to an object name, `to`
 * empty means "every receiver", and the subject is a filter that only the
 * receiving side applies. Delivery is broadcast, never consuming, so every
 * receiver with a matching name sees the same message.
 */
struct KX_Message {
  std::string to;
  std::string from;
  std::string subject;
  std::string body;
};

class KX_MessageAdapter {
 public:
  virtual ~KX_MessageAdapter() = default;

  virtual void Send(const std::string &to,
                    KX_GameObject *from,
                    const std::string &subject,
                    const std::string &body)
  {
    (void)to;
    (void)from;
    (void)subject;
    (void)body;
  }

  virtual std::vector<KX_Message> Receive(const std::string &to, const std::string &subject)
  {
    (void)to;
    (void)subject;
    return std::vector<KX_Message>();
  }
};

/** Headless message bus: records sends and exposes them through a mailbox. */
class KX_MockMessageAdapter : public KX_MessageAdapter {
 public:
  std::vector<KX_Message> sent;
  std::vector<KX_Message> mailbox;
  std::vector<KX_Message> delivered;
  uint32_t send_calls = 0;
  uint32_t receive_calls = 0;

  void Send(const std::string &to,
            KX_GameObject *from,
            const std::string &subject,
            const std::string &body) override;

  std::vector<KX_Message> Receive(const std::string &to, const std::string &subject) override;

  void Reset();
};

/**
 * Audio playback service for the B3 sound nodes.
 *
 * Handles are opaque values owned by the adapter; 0 means "nothing playing".
 * The engine implementation wraps audaspace exactly like SCA_SoundActuator, so
 * the legacy sound actuator and the native nodes share the same device path.
 */
class KX_AudioAdapter {
 public:
  virtual ~KX_AudioAdapter() = default;

  /* ---------------------------------------------------------------------- */
  /* B6.2 object ownership (appended; no existing signature changed).        */
  /* ---------------------------------------------------------------------- */

  /**
   * Register that #handle is produced for #object.
   *
   * A handle can only belong to one object; registering it again moves it. A
   * null object means "unowned" and removes the handle from any previous owner.
   * The map stores borrowed pointers: the owner must call #ForgetObject before
   * the object is destroyed.
   */
  void RegisterSoundOwner(KX_GameObject *object, uint64_t handle)
  {
    ReleaseSoundOwner(handle);
    if (object == nullptr || handle == 0) {
      return;
    }
    handle_owner_[handle] = object;
    object_handles_[object].push_back(handle);
  }

  /** Drop the ownership record of a handle (stop, finish or destroy). */
  void ReleaseSoundOwner(uint64_t handle)
  {
    std::unordered_map<uint64_t, KX_GameObject *>::iterator found = handle_owner_.find(handle);
    if (found == handle_owner_.end()) {
      return;
    }
    KX_GameObject *object = found->second;
    handle_owner_.erase(found);
    std::unordered_map<KX_GameObject *, std::vector<uint64_t>>::iterator list =
        object_handles_.find(object);
    if (list == object_handles_.end()) {
      return;
    }
    std::vector<uint64_t> &handles = list->second;
    for (std::vector<uint64_t>::iterator it = handles.begin(); it != handles.end(); ++it) {
      if (*it == handle) {
        handles.erase(it);
        break;
      }
    }
    if (handles.empty()) {
      object_handles_.erase(list);
    }
  }

  /**
   * Drop every handle owned by #object.
   *
   * Called before the object is destroyed. Erasing by pointer value is safe for
   * an already dangling key (the map never dereferences it); after this call no
   * lookup can hit that object again.
   */
  virtual void ForgetObject(KX_GameObject *object)
  {
    if (object == nullptr) {
      return;
    }
    std::unordered_map<KX_GameObject *, std::vector<uint64_t>>::iterator found =
        object_handles_.find(object);
    if (found == object_handles_.end()) {
      return;
    }
    for (uint64_t handle : found->second) {
      handle_owner_.erase(handle);
    }
    object_handles_.erase(found);
  }

  /** Handles currently owned by #object; empty for null or unknown objects. */
  std::vector<uint64_t> HandlesFor(KX_GameObject *object) const
  {
    if (object == nullptr) {
      return std::vector<uint64_t>();
    }
    std::unordered_map<KX_GameObject *, std::vector<uint64_t>>::const_iterator found =
        object_handles_.find(object);
    return found == object_handles_.end() ? std::vector<uint64_t>() : found->second;
  }

  /** Owner of a handle, or null when it is unknown or unowned. */
  KX_GameObject *OwnerOf(uint64_t handle) const
  {
    std::unordered_map<uint64_t, KX_GameObject *>::const_iterator found =
        handle_owner_.find(handle);
    return found == handle_owner_.end() ? nullptr : found->second;
  }

  /** Forget every ownership record; used when the device is torn down. */
  void ClearSoundOwners()
  {
    object_handles_.clear();
    handle_owner_.clear();
  }

  virtual uint64_t PlaySound(const std::string &filepath,
                             bool loop,
                             float volume,
                             float pitch)
  {
    (void)filepath;
    (void)loop;
    (void)volume;
    (void)pitch;
    return 0;
  }

  /**
   * Start a sound by its bpy datablock name (the name shown in the Blender
   * sound list), instead of a file path. Returns 0 when the name is unknown.
   */
  virtual uint64_t PlaySoundByName(const std::string &name,
                                   bool loop,
                                   float volume,
                                   float pitch)
  {
    (void)name;
    (void)loop;
    (void)volume;
    (void)pitch;
    return 0;
  }

  /**
   * Listener transform for spatial sounds. Position is world space, and
   * orientation is the forward direction of the listener. The engine backend
   * feeds the device's 3D listener; headless backends record it so the wiring
   * can be asserted.
   */
  virtual void Set3DListener(const float position[3], const float orientation[3])
  {
    (void)position;
    (void)orientation;
  }

  virtual void StopSound(uint64_t handle)
  {
    (void)handle;
  }

  virtual void SetVolume(uint64_t handle, float volume)
  {
    (void)handle;
    (void)volume;
  }

  virtual void SetPitch(uint64_t handle, float pitch)
  {
    (void)handle;
    (void)pitch;
  }

  virtual bool IsPlaying(uint64_t handle)
  {
    (void)handle;
    return false;
  }

  virtual bool IsFinished(uint64_t handle)
  {
    (void)handle;
    return true;
  }

  /* ---------------------------------------------------------------------- */
  /* B6.2 audio extensions (appended, existing virtuals unchanged).           */
  /* ---------------------------------------------------------------------- */

  /**
   * Distance attenuation for sounds spawned by #PlaySound3D.
   *
   * #model uses the node enum: 0 inverse, 1 inverse clamped, 2 linear,
   * 3 linear clamped, 4 exponent, 5 exponent clamped.
   */
  virtual void SetAttenuation(KX_GameObject *object,
                              int model,
                              float reference_distance,
                              float maximum_distance)
  {
    (void)object;
    (void)model;
    (void)reference_distance;
    (void)maximum_distance;
  }
  virtual void SetDopplerFactor(KX_GameObject *object, float factor)
  {
    (void)object;
    (void)factor;
  }
  /**
   * Ambient zone sound at the listener position.
   *
   * audaspace has no zone concept, so this is an equivalent implementation: a
   * named looping ambient source at the listener. Returns the new handle.
   */
  virtual uint64_t SetAudioZone(const std::string &zone_name,
                                const std::string &sound,
                                float volume)
  {
    (void)zone_name;
    (void)sound;
    (void)volume;
    return 0;
  }
  /**
   * Named gain group. audaspace has no native bus, so the adapter implements
   * buses as named volume multipliers applied to the handles on that bus.
   */
  virtual void SetAudioBus(const std::string &bus, float volume)
  {
    (void)bus;
    (void)volume;
  }
  /** Positional one-shot sound; returns 0 when the name is unknown. */
  virtual uint64_t PlaySound3D(const std::string &sound,
                               const float position[3],
                               float volume,
                               float pitch)
  {
    (void)sound;
    (void)position;
    (void)volume;
    (void)pitch;
    return 0;
  }

  /**
   * #PlaySoundByName plus ownership registration.
   *
   * A null #object keeps the sound unowned, which is the backward compatible
   * behaviour for every pre B6.2 node.
   */
  virtual uint64_t PlaySoundByNameFor(KX_GameObject *object,
                                      const std::string &name,
                                      bool loop,
                                      float volume,
                                      float pitch)
  {
    const uint64_t handle = PlaySoundByName(name, loop, volume, pitch);
    RegisterSoundOwner(object, handle);
    return handle;
  }

  /** #PlaySound3D plus ownership registration; null #object means unowned. */
  virtual uint64_t PlaySound3DFor(KX_GameObject *object,
                                  const std::string &sound,
                                  const float position[3],
                                  float volume,
                                  float pitch)
  {
    const uint64_t handle = PlaySound3D(sound, position, volume, pitch);
    RegisterSoundOwner(object, handle);
    return handle;
  }

 protected:
  /** Object -> handles it spawned. Borrowed keys, see #ForgetObject. */
  std::unordered_map<KX_GameObject *, std::vector<uint64_t>> object_handles_;
  /** Reverse map used by stop/finish cleanup. */
  std::unordered_map<uint64_t, KX_GameObject *> handle_owner_;
};

/** Headless audio backend that records the handle lifecycle without a device. */
class KX_MockAudioAdapter : public KX_AudioAdapter {
 public:
  struct Entry {
    std::string filepath;
    bool loop = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool playing = false;
    bool finished = false;
  };

  uint64_t next_handle = 1;
  std::map<uint64_t, Entry> entries;
  std::vector<std::string> by_name_requests;
  std::vector<std::string> missing_names;
  float listener_position[3] = {0.0f, 0.0f, 0.0f};
  float listener_orientation[3] = {0.0f, 0.0f, 0.0f};
  uint32_t play_calls = 0;
  uint32_t play_by_name_calls = 0;
  uint32_t listener_calls = 0;
  uint32_t stop_calls = 0;
  uint32_t volume_calls = 0;
  uint32_t pitch_calls = 0;

  /* B6.2 recordings. */
  struct AttenuationRecord {
    KX_GameObject *object = nullptr;
    int model = 0;
    float reference_distance = 0.0f;
    float maximum_distance = 0.0f;
  };
  struct DopplerRecord {
    KX_GameObject *object = nullptr;
    float factor = 1.0f;
  };
  struct ZoneRecord {
    std::string zone;
    std::string sound;
    float volume = 1.0f;
  };
  struct BusRecord {
    std::string bus;
    float volume = 1.0f;
  };
  struct SpatialRecord {
    std::string sound;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float volume = 1.0f;
    float pitch = 1.0f;
  };
  std::vector<AttenuationRecord> attenuation_requests;
  std::vector<DopplerRecord> doppler_requests;
  std::vector<ZoneRecord> zone_requests;
  std::vector<BusRecord> bus_requests;
  std::vector<SpatialRecord> spatial_requests;
  uint32_t attenuation_calls = 0;
  uint32_t doppler_calls = 0;
  uint32_t zone_calls = 0;
  uint32_t bus_calls = 0;
  uint32_t spatial_calls = 0;

  /** Per handle settings, used to prove object scoped isolation in tests. */
  struct HandleSettings {
    int model = 0;
    float reference_distance = 0.0f;
    float maximum_distance = 0.0f;
    float doppler = 1.0f;
    float attenuation = 1.0f;
    float volume = 1.0f;
  };
  std::map<uint64_t, HandleSettings> handle_settings;
  /** Values inherited by handles spawned after a global (null object) call. */
  HandleSettings global_settings;
  /** Settings of #handle, or the default when unknown. */
  const HandleSettings &SettingsOf(uint64_t handle) const;

  uint64_t PlaySound(const std::string &filepath, bool loop, float volume, float pitch) override;
  uint64_t PlaySoundByName(const std::string &name,
                           bool loop,
                           float volume,
                           float pitch) override;
  void Set3DListener(const float position[3], const float orientation[3]) override;
  void StopSound(uint64_t handle) override;
  void SetVolume(uint64_t handle, float volume) override;
  void SetPitch(uint64_t handle, float pitch) override;
  bool IsPlaying(uint64_t handle) override;
  bool IsFinished(uint64_t handle) override;
  void SetAttenuation(KX_GameObject *object,
                      int model,
                      float reference_distance,
                      float maximum_distance) override;
  void SetDopplerFactor(KX_GameObject *object, float factor) override;
  uint64_t SetAudioZone(const std::string &zone_name,
                        const std::string &sound,
                        float volume) override;
  void SetAudioBus(const std::string &bus, float volume) override;
  uint64_t PlaySound3D(const std::string &sound,
                       const float position[3],
                       float volume,
                       float pitch) override;

  void Reset();
};

/** Outcome of a scene unload request (B6.3 UnloadScene node). */
enum class KX_SceneUnloadResult {
  Success = 0,
  SceneActive = 1, /* the scene is currently running */
  Unsupported = 2, /* the engine cannot remove scenes */
  Missing = 3,     /* no scene with that name */
};

/**
 * Scene lifecycle service for the B3 scene control nodes.
 *
 * Suspend stops this scene's logic tick and physics step while rendering keeps
 * running; Resume restores both. AddObject spawns an object that lives in an
 * inactive layer, exactly like the legacy Add Object actuator.
 */
class KX_SceneControlAdapter {
 public:
  virtual ~KX_SceneControlAdapter() = default;

  virtual bool SuspendScene() { return false; }
  virtual bool ResumeScene() { return false; }
  virtual bool IsSuspended() const { return false; }

  /** Spawn an inactive-layer object by name; nullptr when not found. */
  virtual KX_GameObject *AddObject(const std::string &name)
  {
    (void)name;
    return nullptr;
  }

  /* ---------------------------------------------------------------------- */
  /* B6.3 advanced scene extensions (appended; existing virtuals unchanged).  */
  /* ---------------------------------------------------------------------- */

  virtual bool HasScene(const std::string &name)
  {
    (void)name;
    return false;
  }
  /** Remove a non-active scene; false when the engine declines or it is active. */
  virtual bool UnloadScene(const std::string &name)
  {
    (void)name;
    return false;
  }
  /**
   * Why an unload succeeded or failed. The default mapping keeps older
   * adapters working: a positive bool is Success, anything else Unsupported.
   */
  virtual KX_SceneUnloadResult UnloadSceneDetailed(const std::string &name, std::string *reason)
  {
    (void)reason;
    return UnloadScene(name) ? KX_SceneUnloadResult::Success
                             : KX_SceneUnloadResult::Unsupported;
  }
  virtual void ActiveSceneNames(std::vector<std::string> &out) { (void)out; }
  /**
   * Apply the requested time scale to #name. The current engine only has a
   * global time scale, so implementations report false when they cannot honour
   * a per-scene value.
   */
  virtual bool SetSceneTimeScale(const std::string &name, float scale)
  {
    (void)name;
    (void)scale;
    return false;
  }
};

/** Headless scene control backend that records every request. */
class KX_MockSceneControlAdapter : public KX_SceneControlAdapter {
 public:
  bool suspended = false;
  bool suspend_result = true;
  bool resume_result = true;
  std::vector<std::string> add_requests;
  KX_GameObject *next_add_result = nullptr;
  uint32_t suspend_calls = 0;
  uint32_t resume_calls = 0;
  uint32_t add_calls = 0;

  bool SuspendScene() override;
  bool ResumeScene() override;
  bool IsSuspended() const override;
  KX_GameObject *AddObject(const std::string &name) override;

  void Reset();
};

/**
 * Screen space UI service used by the B4.1 UI nodes.
 *
 * Elements are created and updated during the logic frame. The HUD itself is
 * drawn once from #EndFrame, which the engine calls after the scene render
 * finished (decision D2), never from the logic tick. Handles are plain ints;
 * a negative handle means "no element".
 *
 * The interface is deliberately per canvas: every creation call takes the
 * canvas handle it belongs to, so a future per viewport HUD only changes the
 * call site, never these signatures.
 */
class KX_UIAdapter {
 public:
  virtual ~KX_UIAdapter() = default;

  /**
   * Frame start. Mouse position in pixels with origin at the top left, plus
   * whether the left button is held this frame.
   */
  virtual void BeginFrame(float delta_time, float mouse_x, float mouse_y, bool mouse_down)
  {
    (void)delta_time;
    (void)mouse_x;
    (void)mouse_y;
    (void)mouse_down;
  }

  /** Draw the HUD once. Called after the scene render, not from the logic tick. */
  virtual void EndFrame() {}

  /** Create a full screen canvas. @return handle, -1 when creation fails. */
  virtual int CreateCanvas(const std::string &name)
  {
    (void)name;
    return -1;
  }

  /**
   * B6.7: load an extra font face and expose it under `name`, which is what
   * CreateUILabel's Font port now refers to. A label whose name was never
   * loaded keeps the default face. Returns false when the face cannot be
   * loaded.
   */
  virtual bool LoadFont(const std::string &name, const std::string &path)
  {
    (void)name;
    (void)path;
    return false;
  }
  virtual int CreateLabel(int canvas, const std::string &text)
  {
    (void)canvas;
    (void)text;
    return -1;
  }
  virtual int CreateImage(int canvas, const std::string &image)
  {
    (void)canvas;
    (void)image;
    return -1;
  }
  virtual int CreateButton(int canvas, const std::string &text)
  {
    (void)canvas;
    (void)text;
    return -1;
  }
  virtual int CreateLayout(int canvas, int layout_type)
  {
    (void)canvas;
    (void)layout_type;
    return -1;
  }
  /** A slider starts at #initial_value and snaps to #steps when non zero. */
  virtual int CreateSlider(int canvas, float initial_value, int steps, bool allow_bar_click)
  {
    (void)canvas;
    (void)initial_value;
    (void)steps;
    (void)allow_bar_click;
    return -1;
  }
  virtual bool DestroyElement(int handle)
  {
    (void)handle;
    return false;
  }

  /** Set one typed attribute; false when the handle or key is unknown. */
  virtual bool SetAttribute(int handle, const std::string &key, const KX_RuntimeValue &value)
  {
    (void)handle;
    (void)key;
    (void)value;
    return false;
  }

  /** Current text of a label or button. */
  virtual bool GetText(int handle, std::string &out) const
  {
    (void)handle;
    (void)out;
    return false;
  }

  /** Read one typed attribute; false when the handle or key is unknown. */
  virtual bool GetAttribute(int handle, const std::string &key, KX_RuntimeValue &out) const
  {
    (void)handle;
    (void)key;
    (void)out;
    return false;
  }

  /**
   * Frame event of one element.
   *
   * The adapter derives the edges in #BeginFrame from the mouse position and
   * button state, so the nodes never sample the mouse themselves.
   */
  virtual bool Event(int handle, KX_UIEvent event) const
  {
    (void)handle;
    (void)event;
    return false;
  }

  /** Current slider value and knob position (normalized 0..1). */
  virtual float SliderValue(int handle) const
  {
    (void)handle;
    return 0.0f;
  }
  virtual bool KnobPosition(int handle, float &x, float &y) const
  {
    (void)handle;
    x = 0.0f;
    y = 0.0f;
    return false;
  }

  /** Reorder an element inside its parent (MoveWidget). */
  virtual bool MoveElement(int handle, int mode)
  {
    (void)handle;
    (void)mode;
    return false;
  }
  
  /** Full screen draw pass, called once per frame after the scene render. */
  virtual void DrawScreen(int width, int height) { (void)width; (void)height; }

  /** True when the point (pixels, top left origin) is inside the element. */
  virtual bool HitTest(int handle, float x, float y) const
  {
    (void)handle;
    (void)x;
    (void)y;
    return false;
  }

  /** Topmost element under the point, -1 when nothing is hit. */
  virtual int HitTestPoint(float x, float y) const
  {
    (void)x;
    (void)y;
    return -1;
  }

  /**
   * B6.4 read-only frame state.
   *
   * Scroll/drag and list selection need the cursor position, and anchor nodes
   * need the resolved screen size. Everything is already sampled in
   * #BeginFrame / #DrawScreen, so exposing it keeps nodes off the input
   * adapter and avoids a second source of truth.
   */
  virtual float MouseX() const
  {
    return 0.0f;
  }
  virtual float MouseY() const
  {
    return 0.0f;
  }
  virtual bool MouseDown() const
  {
    return false;
  }
  virtual int ScreenWidth() const
  {
    return 0;
  }
  virtual int ScreenHeight() const
  {
    return 0;
  }
};

/** Headless UI backend that records every call for contract tests. */
class KX_MockUIAdapter : public KX_UIAdapter {
 public:
  struct AttributeCall {
    int handle = -1;
    std::string key;
    KX_RuntimeValue value;
    bool accepted = false;
  };
  struct HitCall {
    int handle = -1;
    float x = 0.0f;
    float y = 0.0f;
    bool result = false;
  };

  std::vector<KX_UIElement> elements;
  std::map<int, size_t> by_handle;
  std::vector<AttributeCall> attribute_calls;
  /** HitTest is const on the interface, so the recording side is mutable. */
  mutable std::vector<HitCall> hit_calls;
  std::vector<std::string> canvas_names;
  std::vector<std::string> image_names;
  /** B6.7: name -> path of every font accepted by #LoadFont. */
  std::map<std::string, std::string> fonts;
  uint32_t load_font_calls = 0;

  uint32_t begin_frame_calls = 0;
  uint32_t end_frame_calls = 0;
  /** One increment per element drawn during #EndFrame. */
  uint32_t draw_calls = 0;
  uint32_t create_canvas_calls = 0;
  uint32_t create_label_calls = 0;
  uint32_t create_image_calls = 0;
  uint32_t create_button_calls = 0;
  uint32_t create_layout_calls = 0;
  uint32_t create_slider_calls = 0;
  uint32_t set_attribute_calls = 0;
  mutable uint32_t get_attribute_calls = 0;
  mutable uint32_t event_calls = 0;
  uint32_t move_calls = 0;
  mutable uint32_t hit_test_calls = 0;
  mutable uint32_t hit_point_calls = 0;
  uint32_t destroy_calls = 0;
  /** Event edges derived in BeginFrame, keyed by handle. */
  std::map<int, uint8_t> event_flags;

  float last_delta_time = 0.0f;
  float last_mouse_x = 0.0f;
  float last_mouse_y = 0.0f;
  bool last_mouse_down = false;
  bool previous_mouse_down = false;
  int pressed_handle = -1;
  int hovered_handle = -1;

  void BeginFrame(float delta_time, float mouse_x, float mouse_y, bool mouse_down) override;
  void EndFrame() override;
  int CreateCanvas(const std::string &name) override;
  bool LoadFont(const std::string &name, const std::string &path) override;
  int CreateLabel(int canvas, const std::string &text) override;
  int CreateImage(int canvas, const std::string &image) override;
  int CreateButton(int canvas, const std::string &text) override;
  int CreateLayout(int canvas, int layout_type) override;
  int CreateSlider(int canvas, float initial_value, int steps, bool allow_bar_click) override;
  bool DestroyElement(int handle) override;
  bool SetAttribute(int handle, const std::string &key, const KX_RuntimeValue &value) override;
  bool GetText(int handle, std::string &out) const override;
  bool GetAttribute(int handle, const std::string &key, KX_RuntimeValue &out) const override;
  bool Event(int handle, KX_UIEvent event) const override;
  float SliderValue(int handle) const override;
  bool KnobPosition(int handle, float &x, float &y) const override;
  bool MoveElement(int handle, int mode) override;
  bool HitTest(int handle, float x, float y) const override;
  int HitTestPoint(float x, float y) const override;
  float MouseX() const override
  {
    return last_mouse_x;
  }
  float MouseY() const override
  {
    return last_mouse_y;
  }
  bool MouseDown() const override
  {
    return last_mouse_down;
  }
  int ScreenWidth() const override
  {
    return screen_w_;
  }
  int ScreenHeight() const override
  {
    return screen_h_;
  }
  void DrawScreen(int width, int height) override;

  void Reset();
  KX_UIElement *Find(int handle);
  const KX_UIElement *Find(int handle) const;

 private:
  int CreateElement(KX_UIElementType type, int canvas, const std::string &text);
  int next_handle_ = 1;
  int screen_w_ = 0;
  int screen_h_ = 0;
};

/* ------------------------------------------------------------------------- */
/* B6.3 network adapter                                                      */
/* ------------------------------------------------------------------------- */

/** One event produced by the LAN network adapter. */
struct KX_NetworkEvent {
  enum Type : int {
    None = 0,
    PlayerJoin = 1,
    PlayerLeave = 2,
    Message = 3,
    Disconnect = 4,
  };

  Type type = None;
  /** Sender or affected player; -1 when unknown. */
  int player_id = -1;
  /** Message payload for #Message, empty for the other events. */
  std::string data;
  /** True for SendReliable / BroadcastReliable payloads. */
  bool reliable = true;
};

/** One room advertised by a host's room table (B6.7 C.3.2). */
struct KX_RoomInfo {
  std::string name;
  /** Host address the room was queried through. */
  std::string host;
  int port = 0;
  int players = 0;
  int max_players = 0;
};

/**
 * LAN network service used by the B6.3 network nodes.
 *
 * Scope is deliberately small: a star topology over raw BSD sockets on one
 * local network. There is no NAT traversal, no rollback and no client
 * prediction. Reliable messages use TCP; "unreliable" messages use a separate
 * non-blocking UDP socket (protocol v2), so they can be lost or reordered and
 * are never retransmitted. The adapter never blocks the logic thread:
 * connections and receives are non-blocking and the join call only waits for
 * a bounded, short welcome timeout.
 */
class KX_NetworkAdapter {
 public:
  enum class Role {
    None = 0,
    Host = 1,
    Client = 2,
  };

  virtual ~KX_NetworkAdapter() = default;

  /** Start listening; @a max_players includes the host itself. */
  virtual bool HostRoom(int port, int max_players, std::string *error = nullptr)
  {
    (void)port;
    (void)max_players;
    (void)error;
    return false;
  }
  /** Connect to a host on the same LAN. */
  virtual bool JoinRoom(const std::string &host, int port, std::string *error = nullptr)
  {
    (void)host;
    (void)port;
    (void)error;
    return false;
  }
  virtual void LeaveRoom() {}
  /** Number of players in the room, including the local player. */
  virtual int PlayerCount() const { return 0; }
  /** Host is 0; clients receive their id from the host. -1 while unknown. */
  virtual int LocalPlayerId() const { return -1; }
  virtual bool SendReliable(int target_id, const void *data, size_t len)
  {
    (void)target_id;
    (void)data;
    (void)len;
    return false;
  }
  virtual bool SendUnreliable(int target_id, const void *data, size_t len)
  {
    (void)target_id;
    (void)data;
    (void)len;
    return false;
  }
  virtual bool BroadcastReliable(const void *data, size_t len)
  {
    (void)data;
    (void)len;
    return false;
  }
  /** Store and broadcast one synchronised key/value pair. */
  virtual void SetSyncProperty(const std::string &key, const KX_RuntimeValue &value)
  {
    (void)key;
    (void)value;
  }
  virtual bool GetRemoteProperty(int player_id,
                                 const std::string &key,
                                 KX_RuntimeValue *out) const
  {
    (void)player_id;
    (void)key;
    (void)out;
    return false;
  }
  /** Pop one queued event; false when the queue is empty. */
  virtual bool PollEvent(KX_NetworkEvent &out) { (void)out; return false; }

  /** Drive the non-blocking sockets; called once per logic frame. */
  virtual void Tick() {}
  virtual Role GetRole() const { return Role::None; }
  virtual bool IsConnected() const { return false; }
  virtual std::string LastError() const { return std::string(); }

  /* ---------------------------------------------------------------------- */
  /* B6.7 C.3.2 room table and reconnect (appended; earlier methods stay).    */
  /* ---------------------------------------------------------------------- */

  /** Name advertised to room-list clients; server side only. */
  virtual void SetRoomName(const std::string &name) { (void)name; }
  virtual std::string RoomName() const { return std::string(); }

  /**
   * Query the room table of a host over UDP.
   *
   * There is deliberately no discovery protocol: the caller supplies the host
   * address typed by the user. The implementation replaces #out and returns
   * false with a diagnostic in #error when the host does not answer within the
   * bounded window.
   */
  virtual bool ListRooms(const std::string &host,
                         int port,
                         std::vector<KX_RoomInfo> &out,
                         std::string *error = nullptr)
  {
    (void)host;
    (void)port;
    (void)out;
    (void)error;
    return false;
  }

  /** Re-query the same host; default is just ListRooms again. */
  virtual bool RefreshRooms(const std::string &host,
                            int port,
                            std::vector<KX_RoomInfo> &out,
                            std::string *error = nullptr)
  {
    return ListRooms(host, port, out, error);
  }

  /**
   * Retry the last JoinRoom with exponential backoff.
   *
   * #max_attempts counts join attempts, not sleeps; #backoff_ms is the first
   * delay and doubles per failure, capped at 4000 ms. #attempts receives the
   * number of attempts actually made (0 when already connected). A failure
   * always fills #error with a status line, never returns silently.
   */
  virtual bool Reconnect(int max_attempts,
                         int backoff_ms,
                         int *attempts = nullptr,
                         std::string *error = nullptr)
  {
    (void)max_attempts;
    (void)backoff_ms;
    if (attempts != nullptr) {
      *attempts = 0;
    }
    if (error != nullptr) {
      *error = "Reconnect: not supported";
    }
    return false;
  }
};

/** Headless network backend that records requests for contract tests. */
class KX_MockNetworkAdapter : public KX_NetworkAdapter {
 public:
  bool host_result = true;
  bool join_result = true;
  int local_player = 0;
  int player_count = 1;
  Role role = Role::None;
  bool connected = false;
  std::string last_error;

  bool host_called = false;
  bool join_called = false;
  bool leave_called = false;
  int host_port = 0;
  int host_max_players = 0;
  std::string join_host;
  int join_port = 0;

  /* B6.7 C.3.2 programming surface. */
  bool list_result = true;
  bool refresh_result = true;
  bool reconnect_result = true;
  std::string room_name;
  std::vector<KX_RoomInfo> room_list;
  bool list_called = false;
  bool refresh_called = false;
  std::string list_host;
  int list_port = 0;
  int reconnect_calls = 0;
  int reconnect_attempts = 0;
  int reconnect_max_attempts = 0;
  int reconnect_backoff_ms = 0;
  std::string reconnect_status;

  struct SentPacket {
    int target = -1;
    std::string data;
    bool reliable = true;
    bool broadcast = false;
  };

  std::vector<SentPacket> sent;
  std::vector<KX_NetworkEvent> events;
  std::map<std::string, KX_RuntimeValue> local_props;
  std::map<int, std::map<std::string, KX_RuntimeValue>> remote_props;

  bool HostRoom(int port, int max_players, std::string *error = nullptr) override;
  bool JoinRoom(const std::string &host, int port, std::string *error = nullptr) override;
  void LeaveRoom() override;
  int PlayerCount() const override { return player_count; }
  int LocalPlayerId() const override { return local_player; }
  bool SendReliable(int target_id, const void *data, size_t len) override;
  bool SendUnreliable(int target_id, const void *data, size_t len) override;
  bool BroadcastReliable(const void *data, size_t len) override;
  void SetSyncProperty(const std::string &key, const KX_RuntimeValue &value) override;
  bool GetRemoteProperty(int player_id,
                         const std::string &key,
                         KX_RuntimeValue *out) const override;
  bool PollEvent(KX_NetworkEvent &out) override;
  Role GetRole() const override { return role; }
  bool IsConnected() const override { return connected; }
  std::string LastError() const override { return last_error; }

  /* B6.7 C.3.2 mocks. */
  void SetRoomName(const std::string &name) override { room_name = name; }
  std::string RoomName() const override { return room_name; }
  bool ListRooms(const std::string &host,
                 int port,
                 std::vector<KX_RoomInfo> &out,
                 std::string *error = nullptr) override;
  bool RefreshRooms(const std::string &host,
                    int port,
                    std::vector<KX_RoomInfo> &out,
                    std::string *error = nullptr) override;
  bool Reconnect(int max_attempts,
                 int backoff_ms,
                 int *attempts = nullptr,
                 std::string *error = nullptr) override;

  void Reset();
};

/** The adapters owned by #KX_GraphSceneManager for one scene. */
struct KX_AdapterSet {
  std::unique_ptr<KX_InputAdapter> input;
  std::unique_ptr<KX_SceneAdapter> scene;
  std::unique_ptr<KX_PhysicsAdapter> physics;
  std::unique_ptr<KX_ObjectAdapter> objects;
  std::unique_ptr<KX_AnimationAdapter> animation;
  std::unique_ptr<KX_MessageAdapter> message;
  std::unique_ptr<KX_AudioAdapter> audio;
  std::unique_ptr<KX_SceneControlAdapter> scene_control;
  std::unique_ptr<KX_UIAdapter> ui;
  std::unique_ptr<KX_FileAdapter> file;
  std::unique_ptr<KX_NetworkAdapter> network;
  std::unique_ptr<KX_PhysicsAdvancedAdapter> physics_advanced;

  /** Input edges, then the UI frame start with the mouse state. */
  void BeginFrame(float delta_time);
  /** Logic side frame end. Drawing happens in #DrawUI. */
  void EndFrame();
  /** Render side HUD pass, called once after the scene composition (D2). */
  void DrawUI();
};

/** Engine backed adapters; falls back to neutral when the scene has no canvas. */
std::unique_ptr<KX_AdapterSet> KX_CreateEngineAdapters(KX_Scene *scene);
/** Neutral adapters used by tests and headless exports. */
std::unique_ptr<KX_AdapterSet> KX_CreateNullAdapters();

KX_InputAdapter &KX_NullInputAdapter();
KX_SceneAdapter &KX_NullSceneAdapter();
KX_PhysicsAdapter &KX_NullPhysicsAdapter();
KX_ObjectAdapter &KX_NullObjectAdapter();
KX_AnimationAdapter &KX_NullAnimationAdapter();
KX_MessageAdapter &KX_NullMessageAdapter();
KX_AudioAdapter &KX_NullAudioAdapter();
KX_SceneControlAdapter &KX_NullSceneControlAdapter();
KX_UIAdapter &KX_NullUIAdapter();
KX_NetworkAdapter &KX_NullNetworkAdapter();
/** Engine backed LAN network backend (raw non-blocking sockets). */
std::unique_ptr<KX_NetworkAdapter> KX_CreateEngineNetworkAdapter();
