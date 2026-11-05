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

/** \file KX_GraphInstance.cpp
 *  \ingroup gamelogic
 */

#include "KX_GraphInstance.h"

#include <cstdio>

#include "KX_CompiledGraph.h"

KX_GraphInstance::KX_GraphInstance(KX_Scene *scene,
                                   KX_GameObject *owner,
                                   KX_GraphSceneManager *manager)
    : scene_(scene), owner_(owner), manager_(manager)
{
}

KX_GraphInstance::~KX_GraphInstance()
{
  Shutdown();
}

bool KX_GraphInstance::Load(const GraphIR &ir, std::vector<std::string> *warnings)
{
  std::vector<std::string> diagnostics;
  std::shared_ptr<KX_CompiledGraph> graph =
      KX_CompiledGraph::Compile(ir, scene_, owner_, manager_, &diagnostics);
  if (warnings != nullptr) {
    warnings->insert(warnings->end(), diagnostics.begin(), diagnostics.end());
  }
  if (!graph) {
    return false;
  }

  graph_ = std::move(graph);
  context_.BindGraph(graph_.get(), scene_, owner_);
  return true;
}

bool KX_GraphInstance::Rebuild(const GraphIR &ir, std::vector<std::string> *warnings)
{
  std::vector<std::string> diagnostics;
  std::shared_ptr<KX_CompiledGraph> next =
      KX_CompiledGraph::Compile(ir, scene_, owner_, manager_, &diagnostics);
  if (warnings != nullptr) {
    warnings->insert(warnings->end(), diagnostics.begin(), diagnostics.end());
  }

  if (!next) {
    /* Keep the old graph running: a broken hot reload must never stop the
     * scene or fall back to the Python executor mid frame. */
    const std::string reason = diagnostics.empty() ? std::string("unknown error")
                                                   : diagnostics.front();
    std::fprintf(stderr, "rebuild failed, keeping old graph: %s\n", reason.c_str());
    return false;
  }

  /* Migrate while both graphs are alive and nothing is executing. The new
   * artifact stays in pending_ until the next Tick boundary. */
  if (graph_) {
    next->MigrateStateFrom(*graph_);
  }
  pending_ = std::move(next);
  return true;
}

void KX_GraphInstance::Tick(float delta_time)
{
  CommitPendingGraph();
  if (graph_ == nullptr) {
    return;
  }
  context_.RunFrame(delta_time);
  graph_->RefreshUIHandlesFromState();
}

void KX_GraphInstance::CommitPendingGraph()
{
  if (!pending_) {
    return;
  }

  /* Double buffering: both artifacts are complete, the swap is one pointer
   * exchange at the frame boundary, then the context re-binds to the buffer of
   * the new graph. Nothing outside this function observes a mixed state. */
  graph_.swap(pending_);
  pending_.reset();
  context_.BindGraph(graph_.get(), scene_, owner_);
}

void KX_GraphInstance::Shutdown()
{
  /* Destroying the graph releases the asset references its nodes acquired, so
   * this must happen before the manager unregisters the assets. */
  pending_.reset();
  graph_.reset();
  context_.Bind(nullptr, nullptr, nullptr);
}

bool KX_GraphInstance::HasPendingRebuild() const
{
  return pending_ != nullptr;
}

const KX_ExecutionPlan *KX_GraphInstance::GetPlan() const
{
  return graph_ != nullptr ? graph_->Plan() : nullptr;
}

const KX_CompiledGraph *KX_GraphInstance::GetGraph() const
{
  return graph_.get();
}

const KX_CompiledGraph *KX_GraphInstance::GetPendingGraph() const
{
  return pending_.get();
}

void KX_GraphInstance::SetGraphId(const std::string &graph_id)
{
  graph_id_ = graph_id;
}

const std::string &KX_GraphInstance::GraphId() const
{
  return graph_id_;
}

KX_ExecutionContext *KX_GraphInstance::GetContext()
{
  return &context_;
}

KX_GameObject *KX_GraphInstance::Owner() const
{
  return owner_;
}

KX_Scene *KX_GraphInstance::Scene() const
{
  return scene_;
}
