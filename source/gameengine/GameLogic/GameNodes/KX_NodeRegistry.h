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

/** \file KX_NodeRegistry.h
 *  \ingroup gamelogic
 *
 *  Bridge between the editor node definitions and the runtime node classes.
 *
 *  A node type is registered once with two string ids: the editor side
 *  identifier (the type used by the node tree) and the runtime identifier
 *  (the implementation name). Both map to the same numeric id, factory and
 *  port signature. The compiler resolves a serialized graph from the editor
 *  identifier without touching editor data.
 *
 *  The port signature is the order contract between #IRNode ports and the
 *  runtime node input_offsets[] arrays. Registration validates it so P2 can
 *  add fields without silently shifting offsets.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "KX_GraphIR.h"
#include "KX_LogicNodeRuntime.h"

/** Declared port of a runtime node, used to validate an #IRNode. */
struct KX_NodePortSignature {
  std::string name;
  IRPortKind kind = IRPortKind::Value;
  KX_ValueType value_type = KX_VALUE_NONE;
  /**
   * Port type is resolved by the node from its configuration during the
   * ResolveAssets pass. The IR still declares the concrete type.
   */
  bool dynamic_type = false;
  /** Optional type hint for ports carrying #KX_VALUE_ASSET values. */
  std::string asset_type_hint;
};

struct KX_NodeSignature {
  std::vector<KX_NodePortSignature> inputs;
  std::vector<KX_NodePortSignature> outputs;
  /** Size of the private state block allocated per node instance. */
  size_t state_size = 0;
  size_t state_alignment = KX_RUNTIME_DATA_ALIGNMENT;
  /** Stateful nodes are ticked before trigger propagation. */
  bool needs_tick = false;
  /** The graph compiler ignores outgoing edges of this node during sorting. */
  bool breaks_trigger_cycles = false;
  /**
   * Port layout is taken from the IR node instead of the registration.
   *
   * Table driven node families use this to avoid repeating a port signature
   * per editor node. The compiler still validates count, kind and value type
   * against the IR, and the runtime node resolves ports by name through
   * #KX_LogicNodeRuntime::ConfigurePorts.
   */
  bool ports_from_ir = false;
};

class KX_NodeRegistry {
 public:
  using Factory = std::function<std::unique_ptr<KX_LogicNodeRuntime>()>;

  static KX_NodeRegistry &Get();

  /**
   * Register a node type. Returns the numeric id, or the already registered id
   * when one of the identifiers exists. Never returns
   * #KX_INVALID_NODE_TYPE_ID for a valid pair.
   */
  KX_NodeTypeId RegisterType(const std::string &editor_type_id,
                             const std::string &runtime_type_id,
                             KX_NodeSignature signature,
                             Factory factory);

  KX_NodeTypeId FindByEditorType(const std::string &editor_type_id) const;
  KX_NodeTypeId FindByRuntimeType(const std::string &runtime_type_id) const;

  /** Editor identifier of a registered type, nullptr for an unknown id. */
  const char *EditorTypeId(KX_NodeTypeId type_id) const;
  /** Runtime identifier of a registered type, nullptr for an unknown id. */
  const char *RuntimeTypeId(KX_NodeTypeId type_id) const;
  /** Declared ports/flags of a registered type, nullptr for an unknown id. */
  const KX_NodeSignature *GetSignature(KX_NodeTypeId type_id) const;

  /** Create the runtime instance of a registered type, nullptr when unknown. */
  std::unique_ptr<KX_LogicNodeRuntime> Create(KX_NodeTypeId type_id) const;
  /** Create from either the editor or the runtime identifier. */
  std::unique_ptr<KX_LogicNodeRuntime> Create(const std::string &type_id) const;

  size_t TypeCount() const;

  /** Remove every registration. Only meant for tests and reloads. */
  void Clear();

 private:
  struct Entry {
    std::string editor_type_id;
    std::string runtime_type_id;
    KX_NodeSignature signature;
    Factory factory;
  };

  KX_NodeRegistry() = default;

  std::vector<Entry> entries_;
  std::unordered_map<std::string, KX_NodeTypeId> editor_type_ids_;
  std::unordered_map<std::string, KX_NodeTypeId> runtime_type_ids_;
};

/** Convenience helper: register a runtime class under an editor/runtime id pair. */
template<typename T>
KX_NodeTypeId RegisterGameNodeType(const std::string &editor_type_id,
                                   const std::string &runtime_type_id,
                                   const KX_NodeSignature &signature)
{
  return KX_NodeRegistry::Get().RegisterType(
      editor_type_id,
      runtime_type_id,
      signature,
      []() { return std::unique_ptr<KX_LogicNodeRuntime>(new T()); });
}
