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

/** \file KX_InlinePass.h
 *  \ingroup gamelogic
 *
 *  Post compile pass that folds consecutive pure value nodes into a single
 *  runtime block.
 *
 *  Every inlined node stays in the plan untouched: the block only executes
 *  them back to back in the same topological order the value phase used
 *  before. State migration, asset bindings, trigger scheduling and hot reload
 *  therefore keep working without special cases.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "KX_LogicNodeRuntime.h"

struct KX_ExecutionContext;
struct KX_ExecutionPlan;

/** Result counters reported to the compiler diagnostics. */
struct KX_InlineStats {
  uint32_t blocks = 0;
  uint32_t nodes = 0;
};

/**
 * Runs several pure value nodes in one scheduler call.
 *
 * The wrapped nodes are owned by the execution plan; the block only keeps raw
 * pointers to them. Nodes without a dedicated value path fall back to
 * #KX_LogicNodeRuntime::Execute so behavior never changes.
 */
class KX_InlinedBlock : public KX_LogicNodeRuntime {
 public:
  explicit KX_InlinedBlock(std::vector<KX_LogicNodeRuntime *> nodes);

  KX_ExecResult Execute(KX_ExecutionContext &ctx) override;
  bool ExecuteValueOnly(KX_ExecutionContext &ctx) override;

  const std::vector<KX_LogicNodeRuntime *> &Nodes() const
  {
    return nodes_;
  }

 private:
  std::vector<KX_LogicNodeRuntime *> nodes_;
};

class KX_InlinePass {
 public:
  /**
   * Rewrites plan.value_nodes, appending one block per eligible run.
   * Returns the number of blocks and the number of nodes they absorbed.
   */
  static KX_InlineStats Run(KX_ExecutionPlan &plan);

  /** A/B switch; defaults to the BGE_GAMENODE_INLINE environment setting. */
  static bool Enabled();
  static void SetEnabled(bool enabled);

 private:
  static bool &EnabledFlag();
};
