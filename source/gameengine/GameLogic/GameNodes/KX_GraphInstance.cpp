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

#include "KX_GraphCompiler.h"

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

}  // namespace

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
  KX_CompileResult result = CompileGraph(ir);
  if (warnings != nullptr) {
    for (const KX_CompileDiagnostic &diagnostic : result.diagnostics) {
      warnings->push_back(FormatDiagnostic(diagnostic, ir));
    }
  }

  if (!result.ok || !result.plan) {
    return false;
  }

  plan_ = std::move(result.plan);
  context_.Bind(plan_.get(), scene_, owner_);
  return true;
}

bool KX_GraphInstance::Rebuild(const GraphIR &ir, std::vector<std::string> *warnings)
{
  /* Compile into a temporary plan so a failed hot reload keeps the graph
   * running with its previous behavior instead of going dark. */
  KX_CompileResult result = CompileGraph(ir);
  if (warnings != nullptr) {
    for (const KX_CompileDiagnostic &diagnostic : result.diagnostics) {
      warnings->push_back(FormatDiagnostic(diagnostic, ir));
    }
  }

  if (!result.ok || !result.plan) {
    return false;
  }

  plan_ = std::move(result.plan);
  context_.Bind(plan_.get(), scene_, owner_);
  return true;
}

void KX_GraphInstance::Tick(float delta_time)
{
  if (plan_ == nullptr) {
    return;
  }
  context_.RunFrame(delta_time);
}

void KX_GraphInstance::Shutdown()
{
  /* Destroying the plan releases the asset references its nodes acquired, so
   * this must happen before the manager unregisters the assets. */
  plan_.reset();
  context_.Bind(nullptr, nullptr, nullptr);
}

const KX_ExecutionPlan *KX_GraphInstance::GetPlan() const
{
  return plan_.get();
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
