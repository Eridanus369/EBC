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
#include <vector>

#include "KX_ExecutionContext.h"
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

  void Reset();
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
  void DrawScreen(int width, int height) override;

  void Reset();
  KX_UIElement *Find(int handle);
  const KX_UIElement *Find(int handle) const;

 private:
  int CreateElement(KX_UIElementType type, int canvas, const std::string &text);
  int next_handle_ = 1;
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
