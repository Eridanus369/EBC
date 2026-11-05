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

/** \file KX_CompiledGraph.cpp
 *  \ingroup gamelogic
 */

#include "KX_CompiledGraph.h"

#include <cstring>
#include <map>
#include <utility>

#include "KX_AssetRegistry.h"
#include "KX_FunctionNode.h"
#include "KX_GraphCompiler.h"
#include "KX_InlinePass.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeNodes.h"
#include "KX_UIElement.h"

namespace {

std::string FormatDiagnostic(const KX_CompileDiagnostic &diagnostic, const GraphIR &ir)
{
  std::string message;
  if (diagnostic.severity == KX_CompileDiagnostic::Severity::Warning) {
    message = "warning: ";
  }
  else {
    message = "error: ";
  }

  if (diagnostic.node_index >= 0 &&
      static_cast<size_t>(diagnostic.node_index) < ir.nodes.size())
  {
    const IRNode &node = ir.nodes[diagnostic.node_index];
    message += "node '" + node.debug_name + "' (" + node.type_id + "): ";
  }
  message += diagnostic.message;
  return message;
}

/** Migration key: runtime type id plus the node debug name. */
std::string MigrationKey(const std::string &runtime_type_id, const std::string &debug_name)
{
  return runtime_type_id + '\n' + debug_name;
}

/** UI creator nodes that keep their element handle across a rebuild. */
bool IsUIHandleNode(const std::string &runtime_type_id)
{
  return runtime_type_id == "KX_FuncB4UI_CreateUICanvas" ||
         runtime_type_id == "KX_FuncB4UI_CreateUILayout" ||
         runtime_type_id == "KX_FuncB4UI_CreateUILabel" ||
         runtime_type_id == "KX_FuncB4UI_CreateUIImage" ||
         runtime_type_id == "KX_FuncB4UI_CreateUIButton" ||
         runtime_type_id == "KX_FuncB4UI_MoveWidget";
}

bool IsUISliderNode(const std::string &runtime_type_id)
{
  return runtime_type_id == "KX_FuncB4UI_CreateUISliderWidget";
}

/**
 * Copy a private state block only when both graphs allocated the exact same
 * struct size, so a layout change between builds degrades to the default
 * instead of reading or writing past the block.
 */
template<typename T>
bool CopyStateBlock(const KX_RuntimeData &from,
                    uint32_t from_offset,
                    KX_RuntimeData &to,
                    uint32_t to_offset)
{
  if (from_offset == KX_INVALID_OFFSET || to_offset == KX_INVALID_OFFSET) {
    return false;
  }
  const T *source = from.State<T>(from_offset);
  T *target = to.State<T>(to_offset);
  if (source == nullptr || target == nullptr) {
    return false;
  }
  *target = *source;
  return true;
}

}  // namespace

KX_CompiledGraph::KX_CompiledGraph() = default;

KX_CompiledGraph::~KX_CompiledGraph() = default;

std::shared_ptr<KX_CompiledGraph> KX_CompiledGraph::Compile(
    const GraphIR &ir,
    KX_Scene *scene,
    KX_GameObject *owner,
    KX_GraphSceneManager *manager,
    std::vector<std::string> *diagnostics)
{
  KX_CompileResult result = CompileGraph(ir);
  if (diagnostics != nullptr) {
    for (const KX_CompileDiagnostic &diagnostic : result.diagnostics) {
      diagnostics->push_back(FormatDiagnostic(diagnostic, ir));
    }
  }

  if (!result.ok || !result.plan) {
    return nullptr;
  }

  /* Final compilation step: fold pure value chains into blocks. The pass only
   * rewrites plan.value_nodes and never touches trigger scheduling. */
  const KX_InlineStats inline_stats = KX_InlinePass::Run(*result.plan);
  if (inline_stats.blocks > 0 && diagnostics != nullptr) {
    diagnostics->push_back("inlined: " + std::to_string(inline_stats.blocks) + " blocks, " +
                           std::to_string(inline_stats.nodes) + " nodes -> " +
                           std::to_string(inline_stats.blocks) + " blocks");
  }

  std::shared_ptr<KX_CompiledGraph> graph(new KX_CompiledGraph());
  graph->plan_ = std::move(result.plan);
  graph->scene_ = scene;
  graph->owner_ = owner;
  graph->manager_ = manager;
  graph->data_.SetLayoutSize(graph->plan_->runtime_data_size);
  graph->asset_bindings_ = graph->plan_->asset_bindings;
  graph->BuildAssetSlots();
  return graph;
}

const KX_ExecutionPlan *KX_CompiledGraph::Plan() const
{
  return plan_.get();
}

KX_RuntimeData &KX_CompiledGraph::Data()
{
  return data_;
}

const KX_RuntimeData &KX_CompiledGraph::Data() const
{
  return data_;
}

const std::vector<KX_AssetBinding> &KX_CompiledGraph::AssetBindings() const
{
  return asset_bindings_;
}

KX_GameDataAsset *KX_CompiledGraph::AssetSlot(KX_AssetHandle handle) const
{
  const auto it = asset_slots_.find(handle);
  return it != asset_slots_.end() ? it->second : nullptr;
}

size_t KX_CompiledGraph::AssetSlotCount() const
{
  return asset_slots_.size();
}

const std::unordered_map<std::string, int> &KX_CompiledGraph::UIHandles() const
{
  return ui_handles_;
}

int KX_CompiledGraph::UIHandle(const std::string &debug_name) const
{
  const auto it = ui_handles_.find(debug_name);
  return it != ui_handles_.end() ? it->second : -1;
}

void KX_CompiledGraph::SetUIHandle(const std::string &debug_name, int handle)
{
  ui_handles_[debug_name] = handle;
}

KX_Scene *KX_CompiledGraph::Scene() const
{
  return scene_;
}

KX_GameObject *KX_CompiledGraph::Owner() const
{
  return owner_;
}

void KX_CompiledGraph::BuildAssetSlots()
{
  asset_slots_.clear();
  KX_AssetRegistry &registry = KX_AssetRegistry::Get();
  for (const KX_AssetBinding &binding : asset_bindings_) {
    if (binding.handle == KX_INVALID_ASSET_HANDLE) {
      continue;
    }
    asset_slots_[binding.handle] = registry.Get(binding.handle);
  }
}

void KX_CompiledGraph::RefreshUIHandlesFromState()
{
  if (plan_ == nullptr) {
    return;
  }
  KX_NodeRegistry &registry = KX_NodeRegistry::Get();
  for (uint32_t index = 0; index < plan_->nodes.size(); ++index) {
    const KX_LogicNodeRuntime *node = plan_->nodes[index].get();
    const char *runtime_id = registry.RuntimeTypeId(node->TypeId());
    if (runtime_id == nullptr) {
      continue;
    }
    const uint32_t offset = node->StateOffset();
    if (offset == KX_INVALID_OFFSET) {
      continue;
    }
    const std::string &debug_name = index < plan_->debug_names.size()
                                        ? plan_->debug_names[index]
                                        : std::string();
    if (IsUISliderNode(runtime_id)) {
      const KX_UISliderState *state = data_.State<KX_UISliderState>(offset);
      if (state != nullptr && state->created && state->handle >= 0) {
        ui_handles_[debug_name] = state->handle;
      }
    }
    else if (IsUIHandleNode(runtime_id)) {
      const KX_UIHandleState *state = data_.State<KX_UIHandleState>(offset);
      if (state != nullptr && state->created && state->handle >= 0) {
        ui_handles_[debug_name] = state->handle;
      }
    }
  }
}

void KX_CompiledGraph::MigrateStateFrom(const KX_CompiledGraph &previous)
{
  if (plan_ == nullptr) {
    return;
  }

  /* Asset pointer slots are keyed by the registry handle, so a handle that the
   * new compile did not re-resolve (for example a scene asset that is only
   * read by an unchanged node) keeps pointing at the live registry entry. */
  for (const auto &slot : previous.asset_slots_) {
    if (asset_slots_.find(slot.first) == asset_slots_.end()) {
      asset_slots_[slot.first] = slot.second;
    }
  }

  const KX_ExecutionPlan *previous_plan = previous.Plan();
  if (previous_plan == nullptr) {
    return;
  }

  KX_NodeRegistry &registry = KX_NodeRegistry::Get();

  /* Previous nodes indexed by the migration key. Multimap keeps insertion
   * order for equal keys, so duplicate debug names migrate in node order. */
  std::multimap<std::string, uint32_t> previous_nodes;
  for (uint32_t index = 0; index < previous_plan->nodes.size(); ++index) {
    const char *runtime_id = registry.RuntimeTypeId(previous_plan->nodes[index]->TypeId());
    if (runtime_id == nullptr) {
      continue;
    }
    const std::string &debug_name = index < previous_plan->debug_names.size()
                                        ? previous_plan->debug_names[index]
                                        : std::string();
    previous_nodes.emplace(MigrationKey(runtime_id, debug_name), index);
  }

  for (uint32_t index = 0; index < plan_->nodes.size(); ++index) {
    const KX_LogicNodeRuntime *node = plan_->nodes[index].get();
    const char *runtime_id = registry.RuntimeTypeId(node->TypeId());
    if (runtime_id == nullptr) {
      continue;
    }
    const std::string &debug_name = index < plan_->debug_names.size()
                                        ? plan_->debug_names[index]
                                        : std::string();

    const auto range = previous_nodes.equal_range(MigrationKey(runtime_id, debug_name));
    if (range.first == range.second) {
      /* No counterpart: every private field keeps its zero default. */
      continue;
    }
    const uint32_t previous_index = range.first->second;
    previous_nodes.erase(range.first);

    const KX_LogicNodeRuntime *previous_node = previous_plan->nodes[previous_index].get();
    const uint32_t from_offset = previous_node->StateOffset();
    const uint32_t to_offset = node->StateOffset();
    if (from_offset == KX_INVALID_OFFSET || to_offset == KX_INVALID_OFFSET) {
      continue;
    }

    if (IsUISliderNode(runtime_id)) {
      if (CopyStateBlock<KX_UISliderState>(previous.Data(), from_offset, data_, to_offset)) {
        const KX_UISliderState *state = data_.State<KX_UISliderState>(to_offset);
        if (state->handle >= 0) {
          ui_handles_[debug_name] = state->handle;
        }
      }
    }
    else if (IsUIHandleNode(runtime_id)) {
      if (CopyStateBlock<KX_UIHandleState>(previous.Data(), from_offset, data_, to_offset)) {
        const KX_UIHandleState *state = data_.State<KX_UIHandleState>(to_offset);
        if (state->handle >= 0) {
          ui_handles_[debug_name] = state->handle;
        }
      }
    }
    else if (std::strcmp(runtime_id, "KX_Node_Delay") == 0) {
      CopyStateBlock<KX_DelayState>(previous.Data(), from_offset, data_, to_offset);
    }
    else if (std::strcmp(runtime_id, "KX_FuncB2_ForEach") == 0) {
      /* The cursor lives in node state; the element list pointer is frame local
       * and is rebuilt by the next Execute, so it is deliberately not copied. */
      KX_ForEachCursor cursor;
      if (KX_ReadForEachCursor(from_offset, previous.Data(), &cursor)) {
        KX_WriteForEachCursor(to_offset, data_, cursor);
      }
    }
  }
}
