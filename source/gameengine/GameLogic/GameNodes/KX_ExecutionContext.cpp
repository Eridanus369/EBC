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

/** \file KX_ExecutionContext.cpp
 *  \ingroup gamelogic
 */

#include "KX_ExecutionContext.h"

#include <algorithm>

#include "KX_AssetRegistry.h"
#include "KX_CompiledGraph.h"
#include "KX_DebugTrace.h"
#include "KX_ExecutionPlan.h"
#include "KX_GameAdapters.h"
#include "KX_PropertyAccessor.h"

KX_ExecutionContext::KX_ExecutionContext(KX_Scene *scene, KX_GameObject *owner)
    : scene_(scene), owner_(owner)
{
}

void KX_ExecutionContext::Bind(const KX_ExecutionPlan *plan,
                               KX_Scene *scene,
                               KX_GameObject *owner)
{
  plan_ = plan;
  bound_data_ = nullptr;
  scene_ = scene;
  owner_ = owner;

  data_.Clear();
  trigger_slots_.clear();
  trigger_snapshot_stack_.clear();
  pending_triggers_.clear();
  pending_trigger_outputs_.clear();
  current_tick_node_ = 0;
  executed_nodes_.clear();
  duplicate_executions_ = 0;

  if (plan_ != nullptr) {
    data_.SetLayoutSize(plan_->runtime_data_size);
    trigger_slots_ = plan_->trigger_slots;
  }
}

void KX_ExecutionContext::BindGraph(KX_CompiledGraph *graph,
                                    KX_Scene *scene,
                                    KX_GameObject *owner)
{
  plan_ = graph != nullptr ? graph->Plan() : nullptr;
  bound_data_ = graph != nullptr ? &graph->Data() : nullptr;
  scene_ = scene;
  owner_ = owner;

  /* The compiled buffer already carries the migrated state, so only the frame
   * local scheduling tables are reset here. */
  trigger_slots_.clear();
  trigger_snapshot_stack_.clear();
  pending_triggers_.clear();
  pending_trigger_outputs_.clear();
  current_tick_node_ = 0;
  executed_nodes_.clear();
  duplicate_executions_ = 0;
  if (plan_ != nullptr) {
    trigger_slots_ = plan_->trigger_slots;
  }
}

const KX_ExecutionPlan *KX_ExecutionContext::Plan() const
{
  return plan_;
}

KX_RuntimeData &KX_ExecutionContext::Data()
{
  return bound_data_ != nullptr ? *bound_data_ : data_;
}

KX_RuntimeValue &KX_ExecutionContext::Value(uint32_t offset)
{
  return Data().Value(offset);
}

const KX_RuntimeValue &KX_ExecutionContext::Value(uint32_t offset) const
{
  /* Const reads still address the bound graph; the same fallback applies. */
  return const_cast<KX_ExecutionContext *>(this)->Data().Value(offset);
}

bool KX_ExecutionContext::IsTriggered(uint32_t offset) const
{
  return const_cast<KX_ExecutionContext *>(this)->Data().Trigger(offset);
}

void KX_ExecutionContext::SetTriggered(uint32_t offset, bool value)
{
  Data().SetTrigger(offset, value);
}

bool KX_ExecutionContext::ConsumeTrigger(uint32_t offset)
{
  KX_RuntimeData &data = Data();
  const bool triggered = data.Trigger(offset);
  if (triggered) {
    data.SetTrigger(offset, false);
  }
  return triggered;
}

void KX_ExecutionContext::PushTriggerSnapshot(const std::vector<bool> &bits)
{
  trigger_snapshot_stack_.push_back(bits);
}

void KX_ExecutionContext::PopTriggerSnapshot()
{
  if (!trigger_snapshot_stack_.empty()) {
    trigger_snapshot_stack_.pop_back();
  }
}

bool KX_ExecutionContext::TriggerSnapshot(size_t index) const
{
  if (trigger_snapshot_stack_.empty()) {
    return false;
  }
  const std::vector<bool> &bits = trigger_snapshot_stack_.back();
  return index < bits.size() && bits[index];
}

void KX_ExecutionContext::ClearTriggers()
{
  /* With a bound plan the trigger slots were allocated by the compiler, so the
   * allocator side bookkeeping of KX_RuntimeData is not populated. Clear the
   * compiled slot list instead. Standalone unit tests keep using the runtime
   * data allocation list. */
  if (plan_ != nullptr) {
    KX_RuntimeData &data = Data();
    for (const uint32_t offset : trigger_slots_) {
      data.SetTrigger(offset, false);
    }
    return;
  }
  Data().ClearTriggers();
}

void KX_ExecutionContext::BeginFrame(float delta_time)
{
  ++frame_;
  delta_time_ = delta_time;
  /* B6.9-Ms Ms-2: delayed messages are advanced once per logic frame, from
   * frame deltas only (no wall clock). */
  if (message_adapter_ != nullptr) {
    message_adapter_->TickDelayed(delta_time_);
  }
  ClearTriggers();

  /* Snapshots only live for one Execute call; drop leftovers defensively. */
  trigger_snapshot_stack_.clear();
  pending_triggers_.clear();
  pending_trigger_outputs_.clear();
  pool_.ResetFrame();
  duplicate_executions_ = 0;
  if (plan_ != nullptr) {
    executed_nodes_.assign(plan_->nodes.size(), 0);
  }

  /* Off by default; when on, the trace records only node indices. */
  KX_DebugTrace::BeginFrame(frame_, plan_ != nullptr ? plan_->nodes.size() : 0);
}

void KX_ExecutionContext::TickStatefulNodes()
{
  if (plan_ == nullptr) {
    return;
  }

  for (const uint32_t node_index : plan_->tick_nodes) {
    /* A tick may raise OnDone, so start from a clean output state and collect
     * the triggers it produced into the same frame pending queue. */
    current_tick_node_ = node_index;
    ClearNodeOutputTriggers(node_index);
    plan_->nodes[node_index]->Tick(*this, delta_time_);
    EnqueueNodeOutputs(node_index, pending_triggers_);
  }
}

void KX_ExecutionContext::ExecuteValueNodes()
{
  if (plan_ == nullptr) {
    return;
  }

  /* P1 has no lazy pull evaluation: pure value nodes are refreshed once per
   * frame, before any trigger runs, so trigger nodes read current values. */
  for (const uint32_t node_index : plan_->value_nodes) {
    plan_->nodes[node_index]->Execute(*this);
  }
}

void KX_ExecutionContext::PropagateTriggers()
{
  if (plan_ == nullptr) {
    return;
  }

  if (executed_nodes_.size() != plan_->nodes.size()) {
    executed_nodes_.assign(plan_->nodes.size(), 0);
  }

  std::vector<KX_TriggerTarget> queue;
  queue.swap(pending_triggers_);

  for (const KX_TriggerTarget &entry : plan_->trigger_starts) {
    queue.push_back(entry);
  }

  /* A trigger graph with a Delay feedback loop can in theory run forever inside
   * one frame. The step budget is deliberately generous but finite. Pending
   * element sequences (ForEach) grow it by their own fan out below, so a 100
   * element list is allowed to execute its whole subtree in one frame. */
  size_t max_steps = std::max<size_t>(64, plan_->nodes.size() * 8);
  size_t head = 0;

  auto process_entry = [&](auto &self) -> void {
    if (head >= queue.size() || head >= max_steps) {
      return;
    }

    const KX_TriggerTarget entry = queue[head++];
    if (entry.node_index >= plan_->nodes.size() || entry.consumed) {
      return;
    }

    if (entry.input_offset != KX_INVALID_OFFSET) {
      Data().SetTrigger(entry.input_offset, true);
    }

    if (executed_nodes_[entry.node_index] != 0) {
      ++duplicate_executions_;
    }
    executed_nodes_[entry.node_index] = 1;
    KX_DebugTrace::RecordActivated(entry.node_index);

    KX_GameNodeRuntime *node = plan_->nodes[entry.node_index].get();
    ClearNodeOutputTriggers(entry.node_index);

    if (node->NeedsTriggerSnapshot()) {
      /* Several trigger edges may reach the same node in one frame. Latch every
       * entry already queued for this node so the single Execute below sees all
       * of them; the duplicates are marked consumed instead of running again. */
      for (size_t index = head; index < queue.size(); ++index) {
        KX_TriggerTarget &pending = queue[index];
        if (pending.consumed || pending.node_index != entry.node_index) {
          continue;
        }
        if (pending.input_offset != KX_INVALID_OFFSET) {
          Data().SetTrigger(pending.input_offset, true);
        }
        pending.consumed = true;
      }

      const std::vector<uint32_t> &trigger_inputs =
          plan_->trigger_input_offsets[entry.node_index];
      std::vector<bool> bits;
      bits.reserve(trigger_inputs.size());
      for (const uint32_t offset : trigger_inputs) {
        bits.push_back(Data().Trigger(offset));
      }
      PushTriggerSnapshot(bits);
      node->Execute(*this);
      PopTriggerSnapshot();
    }
    else {
      node->Execute(*this);
    }

    ClearNodeInputTriggers(entry.node_index);
    EnqueueNodeOutputs(entry.node_index, queue);

    const uint32_t pending = node->CollectPendingTriggers(*this);
    if (pending == 0) {
      return;
    }

    /* Run the whole downstream subtree of one element before advancing, so
     * every consumer observes the Element value matching this iteration. The
     * budget grows by the sequence fan out to keep the loop from truncating. */
    max_steps += static_cast<size_t>(pending) * std::max<size_t>(1, plan_->nodes.size());
    for (uint32_t index = 0; index < pending; ++index) {
      node->PreparePendingTrigger(*this, index);
      EnqueueNodeOutputs(entry.node_index, queue);
      while (head < queue.size() && head < max_steps) {
        self(self);
      }
    }

    node->FinishPendingTriggers(*this);
    EnqueueNodeOutputs(entry.node_index, queue);
  };

  while (head < queue.size() && head < max_steps) {
    process_entry(process_entry);
  }

  pending_triggers_.clear();
}

void KX_ExecutionContext::EndFrame()
{
  if (plan_ != nullptr) {
    KX_DebugTrace::EndFrame(*plan_);
  }
}

void KX_ExecutionContext::RunFrame(float delta_time)
{
  if (paused_) {
    return;
  }

  /* Fixed frame order, see the contract at the top of this header. */
  BeginFrame(delta_time);
  TickStatefulNodes();
  ExecuteValueNodes();
  PropagateTriggers();
  EndFrame();
}

uint64_t KX_ExecutionContext::Frame() const
{
  return frame_;
}

float KX_ExecutionContext::DeltaTime() const
{
  return delta_time_;
}

KX_Scene *KX_ExecutionContext::Scene() const
{
  return scene_;
}

void KX_ExecutionContext::SetScene(KX_Scene *scene)
{
  scene_ = scene;
}

KX_GameObject *KX_ExecutionContext::Owner() const
{
  return owner_;
}

void KX_ExecutionContext::SetOwner(KX_GameObject *owner)
{
  owner_ = owner;
}

bool KX_ExecutionContext::Paused() const
{
  return paused_;
}

void KX_ExecutionContext::SetPaused(bool paused)
{
  paused_ = paused;
}

uint32_t KX_ExecutionContext::DuplicateExecutions() const
{
  return duplicate_executions_;
}

std::shared_ptr<void> &KX_ExecutionContext::UISharedState()
{
  return ui_shared_state_;
}

KX_AssetRegistry &KX_ExecutionContext::Assets()
{
  return asset_registry_ != nullptr ? *asset_registry_ : KX_AssetRegistry::Get();
}

void KX_ExecutionContext::SetAssetRegistry(KX_AssetRegistry *registry)
{
  asset_registry_ = registry;
}

KX_GameDataAsset *KX_ExecutionContext::ResolveAsset(KX_AssetHandle handle)
{
  return Assets().Get(handle);
}

KX_PropertyBackend *KX_ExecutionContext::PropertyBackend()
{
  return property_backend_ != nullptr ? property_backend_ : KX_DefaultPropertyBackend();
}

void KX_ExecutionContext::SetPropertyBackend(KX_PropertyBackend *backend)
{
  property_backend_ = backend;
}

void KX_ExecutionContext::SetAdapters(KX_InputAdapter *input,
                                     KX_SceneAdapter *scene_query,
                                     KX_PhysicsAdapter *physics,
                                     KX_ObjectAdapter *objects,
                                     KX_AnimationAdapter *animation,
                                     KX_MessageAdapter *message,
                                     KX_AudioAdapter *audio,
                                     KX_SceneControlAdapter *scene_control,
                                     KX_UIAdapter *ui,
                                     KX_FileAdapter *file,
                                     KX_NetworkAdapter *network,
                                     KX_PhysicsAdvancedAdapter *physics_advanced)
{
  input_adapter_ = input;
  scene_adapter_ = scene_query;
  physics_adapter_ = physics;
  object_adapter_ = objects;
  animation_adapter_ = animation;
  message_adapter_ = message;
  audio_adapter_ = audio;
  scene_control_adapter_ = scene_control;
  ui_adapter_ = ui;
  file_adapter_ = file;
  network_adapter_ = network;
  physics_advanced_adapter_ = physics_advanced;
}

KX_InputAdapter &KX_ExecutionContext::Input()
{
  return input_adapter_ != nullptr ? *input_adapter_ : KX_NullInputAdapter();
}

KX_SceneAdapter &KX_ExecutionContext::SceneQuery()
{
  return scene_adapter_ != nullptr ? *scene_adapter_ : KX_NullSceneAdapter();
}

KX_PhysicsAdapter &KX_ExecutionContext::Physics()
{
  return physics_adapter_ != nullptr ? *physics_adapter_ : KX_NullPhysicsAdapter();
}

KX_ObjectAdapter &KX_ExecutionContext::Objects()
{
  return object_adapter_ != nullptr ? *object_adapter_ : KX_NullObjectAdapter();
}

KX_AnimationAdapter &KX_ExecutionContext::Animation()
{
  return animation_adapter_ != nullptr ? *animation_adapter_ : KX_NullAnimationAdapter();
}

KX_MessageAdapter &KX_ExecutionContext::Message()
{
  return message_adapter_ != nullptr ? *message_adapter_ : KX_NullMessageAdapter();
}

KX_AudioAdapter &KX_ExecutionContext::Audio()
{
  return audio_adapter_ != nullptr ? *audio_adapter_ : KX_NullAudioAdapter();
}

KX_SceneControlAdapter &KX_ExecutionContext::SceneControl()
{
  return scene_control_adapter_ != nullptr ? *scene_control_adapter_ :
                                             KX_NullSceneControlAdapter();
}

KX_UIAdapter &KX_ExecutionContext::UI()
{
  return ui_adapter_ != nullptr ? *ui_adapter_ : KX_NullUIAdapter();
}

KX_FileAdapter &KX_ExecutionContext::File()
{
  return file_adapter_ != nullptr ? *file_adapter_ : KX_NullFileAdapter();
}

KX_NetworkAdapter &KX_ExecutionContext::Network()
{
  return network_adapter_ != nullptr ? *network_adapter_ : KX_NullNetworkAdapter();
}

KX_PhysicsAdvancedAdapter &KX_ExecutionContext::PhysicsAdvanced()
{
  return physics_advanced_adapter_ != nullptr ? *physics_advanced_adapter_
                                              : KX_NullPhysicsAdvancedAdapter();
}

KX_ValuePool &KX_ExecutionContext::Pool()
{
  return pool_;
}

void KX_ExecutionContext::EnqueuePendingOutput(uint32_t output_offset)
{
  if (plan_ == nullptr || output_offset == KX_INVALID_OFFSET) {
    return;
  }
  /* The flag is scanned by the regular output walk after Tick returns, so a
   * pending output and a directly raised one share one propagation path. */
  Data().SetTrigger(output_offset, true);
  pending_trigger_outputs_.push_back({current_tick_node_, output_offset});
}

void KX_ExecutionContext::ClearNodeInputTriggers(uint32_t node_index)
{
  KX_RuntimeData &data = Data();
  for (const uint32_t offset : plan_->trigger_input_offsets[node_index]) {
    data.SetTrigger(offset, false);
  }
}

void KX_ExecutionContext::ClearNodeOutputTriggers(uint32_t node_index)
{
  KX_RuntimeData &data = Data();
  for (const uint32_t offset : plan_->trigger_output_offsets[node_index]) {
    data.SetTrigger(offset, false);
  }
}

void KX_ExecutionContext::EnqueueNodeOutputs(uint32_t node_index,
                                             std::vector<KX_TriggerTarget> &queue)
{
  KX_RuntimeData &data = Data();
  for (const KX_TriggerEdge &edge : plan_->trigger_outgoing[node_index]) {
    if (!data.Trigger(edge.from_offset)) {
      continue;
    }
    KX_TriggerTarget target;
    target.node_index = edge.to_node;
    target.input_offset = edge.to_offset;
    queue.push_back(target);
  }

  /* Clear after scanning so several edges from the same output port all see the
   * flag. The node's next execution starts from a clean output state. */
  ClearNodeOutputTriggers(node_index);
}
