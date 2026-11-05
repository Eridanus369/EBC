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

/** \file KX_GraphInstance.h
 *  \ingroup gamelogic
 *
 *  One mounted game node tree, compiled and running on one game object.
 *
 *  The instance owns the compiled #KX_ExecutionPlan and the execution state of
 *  that graph. It never owns the scene or the owner object: the scene manager
 *  calls #Shutdown when the object is destroyed, which keeps the raw owner
 *  pointer free of dangling states.
 *
 *  #Rebuild is the seam reserved for the P4 hot reload path: it swaps the plan
 *  while preserving the object lifetime contract.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_ExecutionPlan.h"
#include "KX_GraphIR.h"

class KX_CompiledGraph;
class KX_ExecutionContext;
class KX_GameObject;
class KX_GraphSceneManager;
class KX_Scene;

class KX_GraphInstance {
 public:
  KX_GraphInstance(KX_Scene *scene, KX_GameObject *owner, KX_GraphSceneManager *manager);
  ~KX_GraphInstance();

  KX_GraphInstance(const KX_GraphInstance &) = delete;
  KX_GraphInstance &operator=(const KX_GraphInstance &) = delete;

  /**
   * Compile #ir and attach the resulting plan.
   *
   * Returns false when the graph does not compile; the caller keeps the scene
   * running through the legacy Python executor and reports the diagnostics.
   */
  bool Load(const GraphIR &ir, std::vector<std::string> *warnings);

  /**
   * Compile #ir into a pending graph and stage it for the next frame boundary.
   *
   * The running graph keeps executing until #Tick starts, so a caller never
   * observes a half replaced plan. A failed compile keeps the old graph and
   * reports `rebuild failed, keeping old graph: <diagnostic>` on stderr.
   */
  bool Rebuild(const GraphIR &ir, std::vector<std::string> *warnings);

  /** Run one frame of the fixed P1 order (clear, tick, values, triggers). */
  void Tick(float delta_time);

  /** True while a hot reload waits for the next #Tick. */
  bool HasPendingRebuild() const;

  /** Release the plan before the owner disappears. Idempotent. */
  void Shutdown();

  const KX_ExecutionPlan *GetPlan() const;
  /** Currently bound compiled artifact, null after Shutdown. */
  const KX_CompiledGraph *GetGraph() const;
  const KX_CompiledGraph *GetPendingGraph() const;

  /** Manifest graph id this instance was mounted from, used by hot reload. */
  void SetGraphId(const std::string &graph_id);
  const std::string &GraphId() const;
  KX_ExecutionContext *GetContext();
  KX_GameObject *Owner() const;
  KX_Scene *Scene() const;

 private:
  /** Installed at the start of the next Tick after a rebuild. */
  void CommitPendingGraph();

  std::shared_ptr<KX_CompiledGraph> graph_;
  std::shared_ptr<KX_CompiledGraph> pending_;
  std::string graph_id_;
  KX_ExecutionContext context_;
  KX_Scene *scene_ = nullptr;
  KX_GameObject *owner_ = nullptr;
  KX_GraphSceneManager *manager_ = nullptr;
};
