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

/** \file KX_InlinePass.cpp
 *  \ingroup gamelogic
 */

#include "KX_InlinePass.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "KX_ExecutionContext.h"
#include "KX_ExecutionPlan.h"
#include "KX_RuntimeData.h"

namespace {

/** A chain shorter than this is not worth the extra block node. */
constexpr size_t kMinimumRun = 3;

bool IsInlineCandidate(const KX_ExecutionPlan &plan, uint32_t node_index)
{
  if (node_index >= plan.nodes.size() || plan.nodes[node_index] == nullptr) {
    return false;
  }
  const KX_LogicNodeRuntime *node = plan.nodes[node_index].get();
  /* Value only means: no trigger ports and no per node state. Anything else,
   * including Branch, Delay, Timer, ForEach, OnEnter and the trigger based
   * LogicGate, keeps its original scheduling path. */
  if (node->TypeId() == KX_INVALID_NODE_TYPE_ID) {
    return false;
  }
  if (node->StateOffset() != KX_INVALID_OFFSET) {
    return false;
  }
  if (!plan.trigger_input_offsets[node_index].empty() ||
      !plan.trigger_output_offsets[node_index].empty()) {
    return false;
  }
  return true;
}

}  // namespace

KX_InlinedBlock::KX_InlinedBlock(std::vector<KX_LogicNodeRuntime *> nodes)
    : nodes_(std::move(nodes))
{
}

KX_ExecResult KX_InlinedBlock::Execute(KX_ExecutionContext &ctx)
{
  ExecuteValueOnly(ctx);
  return KX_ExecResult::Continue;
}

bool KX_InlinedBlock::ExecuteValueOnly(KX_ExecutionContext &ctx)
{
  for (KX_LogicNodeRuntime *node : nodes_) {
    /* Every wrapped node passed the pure value filter, so #Execute is the value
     * path for it. Calling it directly costs one dispatch per node instead of
     * the two a nested #ExecuteValueOnly chain would spend. */
    node->Execute(ctx);
  }
  return true;
}

KX_InlineStats KX_InlinePass::Run(KX_ExecutionPlan &plan)
{
  KX_InlineStats stats;
  if (!Enabled() || plan.value_nodes.size() < kMinimumRun) {
    return stats;
  }

  std::vector<uint32_t> rewritten;
  rewritten.reserve(plan.value_nodes.size());

  size_t index = 0;
  while (index < plan.value_nodes.size()) {
    if (!IsInlineCandidate(plan, plan.value_nodes[index])) {
      rewritten.push_back(plan.value_nodes[index]);
      ++index;
      continue;
    }

    size_t end = index;
    while (end < plan.value_nodes.size() && IsInlineCandidate(plan, plan.value_nodes[end])) {
      ++end;
    }

    const size_t run_size = end - index;
    if (run_size < kMinimumRun) {
      for (size_t copy = index; copy < end; ++copy) {
        rewritten.push_back(plan.value_nodes[copy]);
      }
      index = end;
      continue;
    }

    std::vector<KX_LogicNodeRuntime *> inlined;
    inlined.reserve(run_size);
    for (size_t copy = index; copy < end; ++copy) {
      inlined.push_back(plan.nodes[plan.value_nodes[copy]].get());
    }

    const uint32_t block_index = static_cast<uint32_t>(plan.nodes.size());
    plan.nodes.push_back(std::make_unique<KX_InlinedBlock>(std::move(inlined)));
    plan.debug_names.push_back("inline_" + std::to_string(stats.blocks));
    /* Keep the per node tables index aligned; a block has no trigger ports. */
    plan.trigger_edges.emplace_back();
    plan.trigger_outgoing.emplace_back();
    plan.trigger_input_offsets.emplace_back();
    plan.trigger_output_offsets.emplace_back();
    plan.execution_order.push_back(block_index);

    rewritten.push_back(block_index);
    ++stats.blocks;
    stats.nodes += static_cast<uint32_t>(run_size);
    index = end;
  }

  plan.value_nodes.swap(rewritten);
  return stats;
}

bool &KX_InlinePass::EnabledFlag()
{
  static bool enabled = []() {
    const char *setting = std::getenv("BGE_GAMENODE_INLINE");
    if (setting == nullptr) {
      return true;
    }
    return std::strcmp(setting, "0") != 0 && std::strcmp(setting, "false") != 0 &&
           std::strcmp(setting, "off") != 0;
  }();
  return enabled;
}

bool KX_InlinePass::Enabled()
{
  return EnabledFlag();
}

void KX_InlinePass::SetEnabled(bool enabled)
{
  EnabledFlag() = enabled;
}
