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

/** \file KX_ExecutionPlan.h
 *  \ingroup gamelogic
 *
 *  Compiled form of a #GraphIR.
 *
 *  It owns the runtime node instances and every scheduling table the frame
 *  driver needs: trigger roots, trigger edges, stateful tick nodes and the flat
 *  port offset table. The plan is immutable after compilation, which is what
 *  makes deterministic recompilation and P4 hot reload diffs possible.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "KX_GameDataAsset.h"
#include "KX_GameNodeRuntime.h"

/** One trigger connection, resolved to runtime offsets. */
struct KX_TriggerEdge {
  uint32_t from_node = 0;
  uint32_t from_port = 0;
  /** Trigger output slot of #from_node, shared by every outgoing edge. */
  uint32_t from_offset = 0;
  uint32_t to_node = 0;
  uint32_t to_port = 0;
  /** Trigger input slot of #to_node. */
  uint32_t to_offset = 0;
};

/** Debug row of the flat port offset table. */
struct KX_PortOffsetInfo {
  uint32_t node_index = 0;
  uint32_t port_index = 0;
  bool input = true;
  bool trigger = false;
  uint32_t offset = 0;
  std::string name;
};

/** Asset reference resolved during compilation, kept for hot reload diagnostics. */
struct KX_AssetBinding {
  uint32_t node_index = 0;
  std::string asset_id;
  std::string asset_type;
  KX_AssetHandle handle = KX_INVALID_ASSET_HANDLE;
  /** Registry entry generation seen at compile time. */
  uint32_t generation = 0;
  std::string field_name;
};

/**
 * Everything the per frame driver needs after compilation.
 *
 * Memory offsets are absolute and only depend on the IR content plus the
 * deterministic topological order. Compiling the same IR twice yields the same
 * runtime_data_size and the same #port_offsets bytes.
 */
struct KX_ExecutionPlan {
  std::vector<std::unique_ptr<KX_GameNodeRuntime>> nodes;
  std::vector<std::string> debug_names;

  /** Nodes whose trigger inputs are entry points. */
  std::vector<uint32_t> trigger_roots;
  /** Concrete entry points, one per unconnected trigger input (or none). */
  std::vector<KX_TriggerTarget> trigger_starts;

  /** trigger_edges[node] holds every incoming trigger edge of that node. */
  std::vector<std::vector<KX_TriggerEdge>> trigger_edges;
  /** trigger_outgoing[node] holds every outgoing trigger edge of that node. */
  std::vector<std::vector<KX_TriggerEdge>> trigger_outgoing;

  /** Trigger input / output slots, used to clear flags at frame start. */
  std::vector<std::vector<uint32_t>> trigger_input_offsets;
  std::vector<std::vector<uint32_t>> trigger_output_offsets;
  /** Flat list of every trigger slot, also used by the frame driver. */
  std::vector<uint32_t> trigger_slots;

  /** Stateful nodes ticked before trigger propagation, in topological order. */
  std::vector<uint32_t> tick_nodes;
  /** Pure value nodes executed once per frame, in topological order. */
  std::vector<uint32_t> value_nodes;
  /** Deterministic topological order of every node. */
  std::vector<uint32_t> execution_order;

  size_t runtime_data_size = 0;
  /** Flat debug table: node_index, port_index, offset, port name. */
  std::vector<KX_PortOffsetInfo> port_offsets;
  /** Asset references in node index order, deterministic across compiles. */
  std::vector<KX_AssetBinding> asset_bindings;
};
