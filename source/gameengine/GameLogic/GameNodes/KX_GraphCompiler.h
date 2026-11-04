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

/** \file KX_GraphCompiler.h
 *  \ingroup gamelogic
 *
 *  The only public entry point of the game node compiler.
 *
 *  A #GraphIR goes in, a deterministic #KX_ExecutionPlan or a list of
 *  diagnostics comes out. The compiler never touches editor data, which keeps
 *  P2 asset parsing and P4 hot reload outside of this file's contract.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "KX_ExecutionPlan.h"
#include "KX_GraphIR.h"

struct KX_CompileDiagnostic {
  enum class Severity : uint8_t {
    Error = 0,
    Warning = 1,
  };

  Severity severity = Severity::Error;
  /** Index into GraphIR::nodes, -1 for graph level errors. */
  int node_index = -1;
  /** Index of the offending port, -1 when the whole node is at fault. */
  int port_index = -1;
  std::string message;
};

struct KX_CompileResult {
  bool ok = false;
  std::unique_ptr<KX_ExecutionPlan> plan;
  std::vector<KX_CompileDiagnostic> diagnostics;
};

/**
 * Compile a graph: Resolve -> ResolveAssets -> Analyze -> Allocate -> Emit.
 *
 * The returned plan is null when any Error diagnostic was emitted. Warnings do
 * not block compilation.
 */
KX_CompileResult CompileGraph(const GraphIR &ir);
