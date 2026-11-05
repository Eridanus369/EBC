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

/** \file KX_CompiledGraph.h
 *  \ingroup gamelogic
 *
 *  Immutable result of compiling one #GraphIR for one mounted instance.
 *
 *  P4.1 splits the graph into a *compiled artifact* and a *live execution
 *  context*. The artifact owns the #KX_ExecutionPlan, the #KX_RuntimeData
 *  buffer the plan addresses, the resolved asset pointer slots and the node
 *  name -> UI handle table. A hot reload compiles a new artifact next to the
 *  running one and only swaps the pointer at the next frame boundary, which is
 *  why a partially replaced graph can never be observed from a frame.
 *
 *  #MigrateStateFrom implements the migration contract: only the state listed
 *  here survives a rebuild, everything else starts from zero.
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "KX_ExecutionPlan.h"
#include "KX_GraphIR.h"
#include "KX_RuntimeData.h"

class KX_GameDataAsset;
class KX_GameObject;
class KX_GraphSceneManager;
class KX_Scene;

class KX_CompiledGraph {
 public:
  KX_CompiledGraph();
  ~KX_CompiledGraph();

  KX_CompiledGraph(const KX_CompiledGraph &) = delete;
  KX_CompiledGraph &operator=(const KX_CompiledGraph &) = delete;

  /**
   * Compile #ir in the context of a mounted instance.
   *
   * Returns nullptr when any stage reports an error; #diagnostics receives one
   * formatted "error: node 'x' (Type): message" record per diagnostic. Warnings
   * are included too so the caller can mirror them to stderr.
   */
  static std::shared_ptr<KX_CompiledGraph> Compile(const GraphIR &ir,
                                                   KX_Scene *scene,
                                                   KX_GameObject *owner,
                                                   KX_GraphSceneManager *manager,
                                                   std::vector<std::string> *diagnostics);

  const KX_ExecutionPlan *Plan() const;
  KX_RuntimeData &Data();
  const KX_RuntimeData &Data() const;

  /** Resolved asset references this graph acquired, in node index order. */
  const std::vector<KX_AssetBinding> &AssetBindings() const;
  /** Pointer slot keyed by registry handle; null for unknown handles. */
  KX_GameDataAsset *AssetSlot(KX_AssetHandle handle) const;
  size_t AssetSlotCount() const;

  /** Node debug name -> UI handle, populated after migration and creation. */
  const std::unordered_map<std::string, int> &UIHandles() const;
  int UIHandle(const std::string &debug_name) const;
  void SetUIHandle(const std::string &debug_name, int handle);
  /**
   * Re-read the element handle of every UI creator from its state block.
   *
   * Called once per frame after node execution, so the map tracks elements the
   * creators made on their first OnEnter rather than only migrated ones.
   */
  void RefreshUIHandlesFromState();

  /**
   * Copy the migratable private state of #previous into this graph.
   *
   * Matching key is `runtime_type_id + debug_name`. Migrated fields:
   *   - asset pointer slots, matched by asset handle,
   *   - UI element handles (and the slider value) of the B4.1 creators,
   *   - Delay `remaining_sec` / `pending`,
   *   - ForEach cursor,
   * Every other private field keeps its zero default.
   */
  void MigrateStateFrom(const KX_CompiledGraph &previous);

  KX_Scene *Scene() const;
  KX_GameObject *Owner() const;

 private:
  void BuildAssetSlots();

  std::unique_ptr<KX_ExecutionPlan> plan_;
  KX_RuntimeData data_;
  KX_Scene *scene_ = nullptr;
  KX_GameObject *owner_ = nullptr;
  KX_GraphSceneManager *manager_ = nullptr;
  std::vector<KX_AssetBinding> asset_bindings_;
  std::unordered_map<KX_AssetHandle, KX_GameDataAsset *> asset_slots_;
  std::unordered_map<std::string, int> ui_handles_;
};
