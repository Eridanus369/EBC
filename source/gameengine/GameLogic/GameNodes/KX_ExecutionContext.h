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

/** \file KX_ExecutionContext.h
 *  \ingroup gamelogic
 *
 *  Execution state handed to every #KX_GameNodeRuntime while a graph runs.
 *
 *  It owns the per graph #KX_RuntimeData buffer and the engine side context
 *  (scene, owner object, frame counters). It also implements the fixed frame
 *  order used by the trigger scheduler:
 *
 *    1. clear last frame trigger flags, advance the frame counter
 *    2. tick stateful nodes (Delay may emit OnDone here)
 *    3. execute pure value nodes in topological order
 *    4. propagate triggers from roots plus the tick produced queue
 *
 *  This order is a hard contract: a Delay OnDone must be able to join the same
 *  frame trigger propagation. Keep it encoded here and nowhere else.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "KX_GameDataAsset.h"
#include "KX_RuntimeData.h"
#include "KX_ValuePool.h"

class KX_AnimationAdapter;
class KX_CompiledGraph;
class KX_MessageAdapter;
class KX_NetworkAdapter;
class KX_AudioAdapter;
class KX_AssetRegistry;
class KX_ExecutionPlan;
class KX_FileAdapter;
class KX_GameObject;
class KX_InputAdapter;
class KX_ObjectAdapter;
class KX_PhysicsAdapter;
class KX_PhysicsAdvancedAdapter;
class KX_PropertyBackend;
class KX_Scene;
class KX_SceneAdapter;
class KX_SceneControlAdapter;
class KX_UIAdapter;

/** A pending trigger target consumed by the next propagation step. */
struct KX_TriggerTarget {
  uint32_t node_index = 0;
  /** Trigger input slot, KX_INVALID_OFFSET for nodes without trigger inputs. */
  uint32_t input_offset = KX_INVALID_OFFSET;
  /** Set when a snapshot node already consumed this entry in the same frame. */
  bool consumed = false;
};

/** A trigger output a stateful node raised during #Tick. */
struct KX_PendingTrigger {
  uint32_t node_index = 0;
  uint32_t output_offset = KX_INVALID_OFFSET;
};

struct KX_RaycastResult {
  bool hit = false;
  /** Hit object, never owned by the result. */
  KX_GameObject *object = nullptr;
  float point[3] = {0.0f, 0.0f, 0.0f};
  float normal[3] = {0.0f, 0.0f, 0.0f};
};

class KX_ExecutionContext {
 public:
  KX_ExecutionContext() = default;
  KX_ExecutionContext(KX_Scene *scene, KX_GameObject *owner);

  /**
   * Attach a compiled plan and adopt its absolute offset layout.
   *
   * The runtime data buffer is resized and zeroed once; every later frame only
   * touches the slots listed in the plan.
   */
  void Bind(const KX_ExecutionPlan *plan, KX_Scene *scene, KX_GameObject *owner);

  /**
   * Attach a compiled graph and address *its* runtime data buffer.
   *
   * Unlike #Bind this never clears the data block: the P4.1 hot reload path
   * migrates state into the new graph first and only then binds it. Passing
   * nullptr restores the internal buffer and drops the plan.
   */
  void BindGraph(KX_CompiledGraph *graph, KX_Scene *scene, KX_GameObject *owner);

  const KX_ExecutionPlan *Plan() const;

  KX_RuntimeData &Data();

  KX_RuntimeValue &Value(uint32_t offset);
  const KX_RuntimeValue &Value(uint32_t offset) const;

  bool IsTriggered(uint32_t offset) const;
  void SetTriggered(uint32_t offset, bool value);
  /** Read a trigger flag and clear it, used by node trigger inputs. */
  bool ConsumeTrigger(uint32_t offset);
  void ClearTriggers();

  /**
   * Push one per-node trigger snapshot while a snapshot node executes.
   *
   * #bits holds the trigger state of the node trigger inputs in port order,
   * captured by #PropagateTriggers before the node clears its inputs.
   */
  void PushTriggerSnapshot(const std::vector<bool> &bits);
  void PopTriggerSnapshot();
  /** Bit #index of the innermost snapshot, false without a live snapshot. */
  bool TriggerSnapshot(size_t index) const;

  void BeginFrame(float delta_time);
  void TickStatefulNodes();
  void ExecuteValueNodes();
  void PropagateTriggers();
  void EndFrame();
  /** Run the full fixed order for one frame. */
  void RunFrame(float delta_time);

  uint64_t Frame() const;
  float DeltaTime() const;

  KX_Scene *Scene() const;
  void SetScene(KX_Scene *scene);

  KX_GameObject *Owner() const;
  void SetOwner(KX_GameObject *owner);

  bool Paused() const;
  void SetPaused(bool paused);

  /** Asset registry used by AssetGet/AssetSet. Defaults to the global singleton. */
  KX_AssetRegistry &Assets();
  void SetAssetRegistry(KX_AssetRegistry *registry);
  /** Hot path asset lookup, one registry array access. */
  KX_GameDataAsset *ResolveAsset(KX_AssetHandle handle);

  /**
   * Property storage used by ObjectGet/ObjectSet.
   *
   * Defaults to #KX_DefaultPropertyBackend, which the engine build installs
   * from KX_PropertyAccessorEngine.cpp. Tests inject their own backend.
   */
  KX_PropertyBackend *PropertyBackend();
  void SetPropertyBackend(KX_PropertyBackend *backend);

  /**
   * Engine adapters used by the bulk node families.
   *
   * Passing nullptr restores the neutral adapter, so unit tests and headless
   * builds never dereference a missing scene service. The execution context
   * never owns the adapters.
   */
  void SetAdapters(KX_InputAdapter *input,
                   KX_SceneAdapter *scene_query,
                   KX_PhysicsAdapter *physics,
                   KX_ObjectAdapter *objects,
                   KX_AnimationAdapter *animation = nullptr,
                   KX_MessageAdapter *message = nullptr,
                   KX_AudioAdapter *audio = nullptr,
                   KX_SceneControlAdapter *scene_control = nullptr,
                   KX_UIAdapter *ui = nullptr,
                   KX_FileAdapter *file = nullptr,
                   KX_NetworkAdapter *network = nullptr,
                   KX_PhysicsAdvancedAdapter *physics_advanced = nullptr);
  KX_InputAdapter &Input();
  KX_SceneAdapter &SceneQuery();
  KX_PhysicsAdapter &Physics();
  KX_ObjectAdapter &Objects();
  KX_AnimationAdapter &Animation();
  KX_MessageAdapter &Message();
  KX_AudioAdapter &Audio();
  KX_SceneControlAdapter &SceneControl();
  KX_UIAdapter &UI();
  KX_FileAdapter &File();
  KX_NetworkAdapter &Network();
  KX_PhysicsAdvancedAdapter &PhysicsAdvanced();

  /** Per frame string and list storage used by the extended value types. */
  KX_ValuePool &Pool();

  /**
   * Queue a trigger output raised outside the regular output scan.
   *
   * Called by #KX_GameNodeRuntime::EmitPendingTrigger from a Tick callback.
   * The current tick node is recorded by #TickStatefulNodes.
   */
  void EnqueuePendingOutput(uint32_t output_offset);

  /**
   * Number of nodes that executed more than once during the last frame.
   *
   * P1 allows duplicate same frame triggers on purpose. The counter exists so
   * P3 can warn about it and add per-node execution stamps without changing
   * the scheduler contract.
   */
  uint32_t DuplicateExecutions() const;

  /**
   * Opaque per-instance slot shared by cooperating UI nodes.
   *
   * The screen stack needs one state object per graph instance so two mounted
   * graphs never share a stack. The slot lives on the context, which survives
   * #KX_GraphInstance::Rebuild, so a hot reload keeps the stack contents.
   */
  std::shared_ptr<void> &UISharedState();

  /**
   * Physics query used by the Raycast node.
   *
   * Defined in KX_ExecutionContextPhysics.cpp for the engine build. Tests
   * provide their own definition or override it through a derived context.
   * A miss always returns hit=false and a null object, never a dangling
   * controller.
   */
  virtual KX_RaycastResult Raycast(const float origin[3],
                                   const float direction[3],
                                   float max_dist);

 private:
  void ClearNodeInputTriggers(uint32_t node_index);
  void ClearNodeOutputTriggers(uint32_t node_index);
  void EnqueueNodeOutputs(uint32_t node_index, std::vector<KX_TriggerTarget> &queue);

  KX_RuntimeData data_;
  /** Borrowed buffer of a #KX_CompiledGraph; null means #data_ is used. */
  KX_RuntimeData *bound_data_ = nullptr;
  const KX_ExecutionPlan *plan_ = nullptr;
  KX_Scene *scene_ = nullptr;
  KX_GameObject *owner_ = nullptr;
  KX_AssetRegistry *asset_registry_ = nullptr;
  KX_PropertyBackend *property_backend_ = nullptr;
  uint64_t frame_ = 0;
  float delta_time_ = 0.0f;
  bool paused_ = false;

  std::vector<uint32_t> trigger_slots_;
  /** Innermost last; only populated while a snapshot node is executing. */
  std::vector<std::vector<bool>> trigger_snapshot_stack_;
  std::vector<KX_TriggerTarget> pending_triggers_;
  std::vector<KX_PendingTrigger> pending_trigger_outputs_;
  uint32_t current_tick_node_ = 0;
  KX_ValuePool pool_;
  KX_InputAdapter *input_adapter_ = nullptr;
  KX_SceneAdapter *scene_adapter_ = nullptr;
  KX_PhysicsAdapter *physics_adapter_ = nullptr;
  KX_ObjectAdapter *object_adapter_ = nullptr;
  KX_AnimationAdapter *animation_adapter_ = nullptr;
  KX_MessageAdapter *message_adapter_ = nullptr;
  KX_AudioAdapter *audio_adapter_ = nullptr;
  KX_SceneControlAdapter *scene_control_adapter_ = nullptr;
  KX_UIAdapter *ui_adapter_ = nullptr;
  KX_FileAdapter *file_adapter_ = nullptr;
  KX_NetworkAdapter *network_adapter_ = nullptr;
  KX_PhysicsAdvancedAdapter *physics_advanced_adapter_ = nullptr;
  std::vector<uint8_t> executed_nodes_;
  uint32_t duplicate_executions_ = 0;
  /** Instance-scoped UI state (see #UISharedState). */
  std::shared_ptr<void> ui_shared_state_;
};
