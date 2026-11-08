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

/** \file KX_GraphCompiler.cpp
 *  \ingroup gamelogic
 *
 *  Five stage compiler: Resolve -> ResolveAssets -> Analyze -> Allocate -> Emit.
 *
 *  ResolveAssets resolves string asset references into registry handles and
 *  finalizes the type of dynamic ports (asset field types, object property
 *  types). It runs before Analyze so type errors fail before topological
 *  sorting, which keeps diagnostics precise.
 *
 *  Determinism rules used throughout:
 *  - topological sorting uses a min heap keyed by IR node index
 *  - allocation walks that deterministic order
 *  - every table is built in IR declaration order
 *
 *  Delay is a legal cycle breaker: outgoing edges of a node whose signature
 *  sets breaks_trigger_cycles are left out of the dependency graph. The Delay
 *  value is still consumed on the next frame, so there is no same frame
 *  dependency to honor.
 */

#include "KX_GraphCompiler.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <string>
#include <utility>

#include "KX_AssetRegistry.h"
#include "KX_GameDataAsset.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeData.h"
#include "KX_RuntimeValue.h"

namespace {

bool IsImplicitConversionAllowed(KX_ValueType from, KX_ValueType to)
{
  if (from == to) {
    return true;
  }
  if (from == KX_VALUE_NONE || to == KX_VALUE_NONE) {
    return false;
  }

  switch (from) {
    case KX_VALUE_BOOL:
      return to == KX_VALUE_INT || to == KX_VALUE_FLOAT;
    case KX_VALUE_INT:
      return to == KX_VALUE_BOOL || to == KX_VALUE_FLOAT;
    case KX_VALUE_FLOAT:
      /* Float to Int is intentionally rejected; a Cast node arrives in P2. */
      return to == KX_VALUE_BOOL;
    case KX_VALUE_VECTOR:
      /* Explicitly marked narrowing: the scalar is the X component. */
      return to == KX_VALUE_FLOAT;
    case KX_VALUE_COLOR:
      /* Color to vector drops alpha, the reverse fills alpha with 1. */
      return to == KX_VALUE_VECTOR;
    case KX_VALUE_OBJECT:
    case KX_VALUE_ASSET:
    case KX_VALUE_STRING:
    case KX_VALUE_LIST:
    case KX_VALUE_DATABLOCK:
    case KX_VALUE_NONE:
      return false;
  }
  return false;
}

std::string NodeLabel(const GraphIR &ir, uint32_t node_index)
{
  const IRNode &node = ir.nodes[node_index];
  const std::string &name = node.debug_name.empty() ? node.type_id : node.debug_name;
  return name + "(" + std::to_string(node_index) + ")";
}

void AddDiagnostic(std::vector<KX_CompileDiagnostic> &diagnostics,
                   KX_CompileDiagnostic::Severity severity,
                   int node_index,
                   int port_index,
                   std::string message)
{
  KX_CompileDiagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.node_index = node_index;
  diagnostic.port_index = port_index;
  diagnostic.message = std::move(message);
  diagnostics.push_back(std::move(diagnostic));
}

bool HasErrors(const std::vector<KX_CompileDiagnostic> &diagnostics)
{
  for (const KX_CompileDiagnostic &diagnostic : diagnostics) {
    if (diagnostic.severity == KX_CompileDiagnostic::Severity::Error) {
      return true;
    }
  }
  return false;
}

bool IsNodeIndexValid(const GraphIR &ir, int index)
{
  return index >= 0 && static_cast<size_t>(index) < ir.nodes.size();
}

bool IsPortIndexValid(const IRNode &node, bool input, int index)
{
  const size_t count = input ? node.inputs.size() : node.outputs.size();
  return index >= 0 && static_cast<size_t>(index) < count;
}

/** Return the node sequence of the first cycle, or an empty vector. */
std::vector<uint32_t> FindCycle(const std::vector<std::vector<uint32_t>> &dependencies)
{
  std::vector<uint8_t> color(dependencies.size(), 0);
  std::vector<uint32_t> stack;
  std::vector<uint32_t> cycle;

  std::function<bool(uint32_t)> visit = [&](uint32_t node) -> bool {
    color[node] = 1;
    stack.push_back(node);

    for (const uint32_t next : dependencies[node]) {
      if (color[next] == 0) {
        if (visit(next)) {
          return true;
        }
      }
      else if (color[next] == 1) {
        const auto it = std::find(stack.begin(), stack.end(), next);
        cycle.assign(it, stack.end());
        cycle.push_back(next);
        return true;
      }
    }

    stack.pop_back();
    color[node] = 2;
    return false;
  };

  for (uint32_t node = 0; node < dependencies.size(); ++node) {
    if (color[node] == 0 && visit(node)) {
      return cycle;
    }
  }
  return {};
}

}  // namespace

KX_CompileResult CompileGraph(const GraphIR &ir)
{
  KX_CompileResult result;

  const size_t node_count = ir.nodes.size();
  std::vector<KX_CompileDiagnostic> &diagnostics = result.diagnostics;

  if (node_count == 0) {
    result.plan = std::make_unique<KX_ExecutionPlan>();
    result.ok = true;
    return result;
  }

  /* ---------------------------------------------------------------------- */
  /* Stage 1: Resolve                                                       */
  /* ---------------------------------------------------------------------- */

  std::vector<std::unique_ptr<KX_GameNodeRuntime>> runtime_nodes;
  std::vector<KX_NodeSignature> signatures;
  runtime_nodes.reserve(node_count);
  signatures.reserve(node_count);

  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    const IRNode &ir_node = ir.nodes[node_index];

    const KX_NodeTypeId type_id = KX_NodeRegistry::Get().FindByEditorType(ir_node.type_id);
    if (type_id == KX_INVALID_NODE_TYPE_ID) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    static_cast<int>(node_index),
                    -1,
                    "Unknown node type '" + ir_node.type_id + "'");
      return result;
    }

    const KX_NodeSignature *registered = KX_NodeRegistry::Get().GetSignature(type_id);
    std::unique_ptr<KX_GameNodeRuntime> node = KX_NodeRegistry::Get().Create(type_id);
    if (registered == nullptr || !node) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    static_cast<int>(node_index),
                    -1,
                    "Node type '" + ir_node.type_id + "' cannot be instantiated");
      return result;
    }

    signatures.push_back(*registered);
    KX_NodeSignature &signature = signatures.back();

    /* Table driven node families do not repeat their port layout in the
     * registration. The IR already carries the concrete ports, so synthesize
     * the signature from them and keep the regular validation below. */
    if (signature.ports_from_ir) {
      signature.inputs.clear();
      signature.inputs.reserve(ir_node.inputs.size());
      for (const IRPort &port : ir_node.inputs) {
        KX_NodePortSignature sig;
        sig.name = port.name;
        sig.kind = port.kind;
        sig.value_type = port.kind == IRPortKind::Value ? port.value_tag : KX_VALUE_NONE;
        sig.asset_type_hint = port.asset_type_hint;
        signature.inputs.push_back(std::move(sig));
      }

      signature.outputs.clear();
      signature.outputs.reserve(ir_node.outputs.size());
      for (const IRPort &port : ir_node.outputs) {
        KX_NodePortSignature sig;
        sig.name = port.name;
        sig.kind = port.kind;
        sig.value_type = port.kind == IRPortKind::Value ? port.value_tag : KX_VALUE_NONE;
        sig.asset_type_hint = port.asset_type_hint;
        signature.outputs.push_back(std::move(sig));
      }
    }

    if (ir_node.inputs.size() > KX_MAX_NODE_INPUTS ||
        ir_node.outputs.size() > KX_MAX_NODE_OUTPUTS)
    {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    static_cast<int>(node_index),
                    -1,
                    "Node declares more than " + std::to_string(KX_MAX_NODE_INPUTS) +
                        " inputs or " + std::to_string(KX_MAX_NODE_OUTPUTS) + " outputs");
      return result;
    }

    bool ports_match = true;
    if (ir_node.inputs.size() != signature.inputs.size()) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    static_cast<int>(node_index),
                    -1,
                    "Input port count mismatch for '" + ir_node.type_id + "': IR declares " +
                        std::to_string(ir_node.inputs.size()) + ", registered signature has " +
                        std::to_string(signature.inputs.size()));
      ports_match = false;
    }
    /* A graph exported before a trailing output port extension only declares
     * the ports that existed then. Only signatures that opt in through
     * #KX_NodeSignature::allow_fewer_outputs accept that shorter prefix
     * (Collision today); every other node must declare exactly its outputs. */
    const bool output_count_ok =
        signature.allow_fewer_outputs ? ir_node.outputs.size() <= signature.outputs.size()
                                      : ir_node.outputs.size() == signature.outputs.size();
    if (!output_count_ok) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    static_cast<int>(node_index),
                    -1,
                    "Output port count mismatch for '" + ir_node.type_id + "': IR declares " +
                        std::to_string(ir_node.outputs.size()) + ", registered signature has " +
                        std::to_string(signature.outputs.size()));
      ports_match = false;
    }

    if (ports_match) {
      for (uint32_t port = 0; port < ir_node.inputs.size(); ++port) {
        const IRPort &ir_port = ir_node.inputs[port];
        const KX_NodePortSignature &sig_port = signature.inputs[port];
        if (ir_port.kind != sig_port.kind) {
          AddDiagnostic(diagnostics,
                        KX_CompileDiagnostic::Severity::Error,
                        static_cast<int>(node_index),
                        static_cast<int>(port),
                        "Input port '" + ir_port.name + "' kind mismatch: IR declares " +
                            (ir_port.kind == IRPortKind::Trigger ? "Trigger" : "Value") +
                            ", registered signature has " +
                            (sig_port.kind == IRPortKind::Trigger ? "Trigger" : "Value"));
          ports_match = false;
        }
        else if (ir_port.kind == IRPortKind::Value) {
          if (sig_port.dynamic_type) {
            if (ir_port.value_tag == KX_VALUE_NONE) {
              AddDiagnostic(diagnostics,
                            KX_CompileDiagnostic::Severity::Error,
                            static_cast<int>(node_index),
                            static_cast<int>(port),
                            "Dynamic input port '" + ir_port.name +
                                "' must declare a concrete value type in the IR");
              ports_match = false;
            }
          }
          else if (ir_port.value_tag != sig_port.value_type) {
            AddDiagnostic(diagnostics,
                          KX_CompileDiagnostic::Severity::Error,
                          static_cast<int>(node_index),
                          static_cast<int>(port),
                          "Input port '" + ir_port.name + "' type mismatch: IR declares " +
                              KX_ValueTypeName(ir_port.value_tag) +
                              ", registered signature expects " +
                              KX_ValueTypeName(sig_port.value_type));
            ports_match = false;
          }
        }
      }

      for (uint32_t port = 0; port < ir_node.outputs.size(); ++port) {
        const IRPort &ir_port = ir_node.outputs[port];
        const KX_NodePortSignature &sig_port = signature.outputs[port];
        if (ir_port.kind != sig_port.kind) {
          AddDiagnostic(diagnostics,
                        KX_CompileDiagnostic::Severity::Error,
                        static_cast<int>(node_index),
                        static_cast<int>(port),
                        "Output port '" + ir_port.name + "' kind mismatch: IR declares " +
                            (ir_port.kind == IRPortKind::Trigger ? "Trigger" : "Value") +
                            ", registered signature has " +
                            (sig_port.kind == IRPortKind::Trigger ? "Trigger" : "Value"));
          ports_match = false;
        }
        else if (ir_port.kind == IRPortKind::Value) {
          if (sig_port.dynamic_type) {
            if (ir_port.value_tag == KX_VALUE_NONE) {
              AddDiagnostic(diagnostics,
                            KX_CompileDiagnostic::Severity::Error,
                            static_cast<int>(node_index),
                            static_cast<int>(port),
                            "Dynamic output port '" + ir_port.name +
                                "' must declare a concrete value type in the IR");
              ports_match = false;
            }
          }
          else if (ir_port.value_tag != sig_port.value_type) {
            AddDiagnostic(diagnostics,
                          KX_CompileDiagnostic::Severity::Error,
                          static_cast<int>(node_index),
                          static_cast<int>(port),
                          "Output port '" + ir_port.name + "' type mismatch: IR declares " +
                              KX_ValueTypeName(ir_port.value_tag) +
                              ", registered signature expects " +
                              KX_ValueTypeName(sig_port.value_type));
            ports_match = false;
          }
        }
      }
    }

    if (!ports_match) {
      return result;
    }

    std::string config_error;
    if (!node->Configure(ir_node.config_json, &config_error)) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    static_cast<int>(node_index),
                    -1,
                    config_error);
      return result;
    }

    /* Bind port names to indices before the offset table is assigned, so a
     * table driven node can validate the ports it is going to read. */
    node->ConfigurePorts(ir_node.inputs, ir_node.outputs);

    runtime_nodes.push_back(std::move(node));
  }

  /* ---------------------------------------------------------------------- */
  /* Stage 2: ResolveAssets                                                 */
  /* ---------------------------------------------------------------------- */

  KX_AssetRegistry &asset_registry = KX_AssetRegistry::Get();

  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    KX_GameNodeRuntime &node = *runtime_nodes[node_index];
    const IRNode &ir_node = ir.nodes[node_index];
    KX_NodeSignature &signature = signatures[node_index];

    if (node.HasAssetBinding()) {
      std::string error;
      if (!node.ResolveAssetBinding(asset_registry, &error)) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      static_cast<int>(node_index),
                      -1,
                      error);
        continue;
      }
    }

    for (uint32_t port = 0; port < signature.inputs.size(); ++port) {
      KX_NodePortSignature &sig_port = signature.inputs[port];
      if (!sig_port.dynamic_type) {
        continue;
      }

      const KX_ValueType resolved = node.ResolvedPortType(true, port);
      if (resolved == KX_VALUE_NONE) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      static_cast<int>(node_index),
                      static_cast<int>(port),
                      "Node '" + NodeLabel(ir, node_index) +
                          "' did not resolve a type for dynamic input port '" +
                          sig_port.name + "'");
        continue;
      }
      if (ir_node.inputs[port].value_tag != resolved) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      static_cast<int>(node_index),
                      static_cast<int>(port),
                      "Dynamic input port '" + sig_port.name + "' is declared " +
                          KX_ValueTypeName(ir_node.inputs[port].value_tag) +
                          " but the node resolved it to " + KX_ValueTypeName(resolved));
        continue;
      }
      sig_port.value_type = resolved;
    }

    for (uint32_t port = 0; port < signature.outputs.size(); ++port) {
      KX_NodePortSignature &sig_port = signature.outputs[port];
      if (!sig_port.dynamic_type) {
        continue;
      }
      /* A legacy IR may omit trailing ports; there is nothing to resolve. */
      if (port >= ir_node.outputs.size()) {
        break;
      }

      const KX_ValueType resolved = node.ResolvedPortType(false, port);
      if (resolved == KX_VALUE_NONE) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      static_cast<int>(node_index),
                      static_cast<int>(port),
                      "Node '" + NodeLabel(ir, node_index) +
                          "' did not resolve a type for dynamic output port '" +
                          sig_port.name + "'");
        continue;
      }
      if (ir_node.outputs[port].value_tag != resolved) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      static_cast<int>(node_index),
                      static_cast<int>(port),
                      "Dynamic output port '" + sig_port.name + "' is declared " +
                          KX_ValueTypeName(ir_node.outputs[port].value_tag) +
                          " but the node resolved it to " + KX_ValueTypeName(resolved));
        continue;
      }
      sig_port.value_type = resolved;
    }
  }

  if (HasErrors(diagnostics)) {
    result.ok = false;
    return result;
  }

  /* ---------------------------------------------------------------------- */
  /* Stage 3: Analyze                                                       */
  /* ---------------------------------------------------------------------- */

  std::vector<std::vector<int>> value_source_node(node_count);
  std::vector<std::vector<int>> value_source_port(node_count);
  std::vector<std::vector<bool>> trigger_connected(node_count);
  std::vector<std::vector<KX_ValueType>> input_conversions(node_count);

  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    const size_t input_count = ir.nodes[node_index].inputs.size();
    value_source_node[node_index].assign(input_count, -1);
    value_source_port[node_index].assign(input_count, -1);
    trigger_connected[node_index].assign(input_count, false);
    input_conversions[node_index].assign(input_count, KX_VALUE_NONE);
  }

  for (size_t connection_index = 0; connection_index < ir.connections.size();
       ++connection_index)
  {
    const IRConnection &connection = ir.connections[connection_index];

    if (!IsNodeIndexValid(ir, connection.from_node) ||
        !IsNodeIndexValid(ir, connection.to_node))
    {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    -1,
                    -1,
                    "Connection " + std::to_string(connection_index) +
                        " references a node that does not exist");
      continue;
    }

    const IRNode &from_node = ir.nodes[connection.from_node];
    const IRNode &to_node = ir.nodes[connection.to_node];

    if (!IsPortIndexValid(from_node, false, connection.from_port)) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    connection.from_node,
                    -1,
                    "Connection " + std::to_string(connection_index) +
                        " references an output port that does not exist");
      continue;
    }
    if (!IsPortIndexValid(to_node, true, connection.to_port)) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    connection.to_node,
                    -1,
                    "Connection " + std::to_string(connection_index) +
                        " references an input port that does not exist");
      continue;
    }

    const IRPort &from_port = from_node.outputs[connection.from_port];
    const IRPort &to_port = to_node.inputs[connection.to_port];

    if (from_port.kind != to_port.kind) {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    connection.to_node,
                    connection.to_port,
                    "Cannot connect output '" + from_port.name + "' (" +
                        (from_port.kind == IRPortKind::Trigger ? "Trigger" : "Value") +
                        ") to input '" + to_port.name + "' (" +
                        (to_port.kind == IRPortKind::Trigger ? "Trigger" : "Value") + ")");
      continue;
    }

    if (from_port.kind == IRPortKind::Value && from_port.value_tag == KX_VALUE_ASSET &&
        !from_port.asset_type_hint.empty() && !to_port.asset_type_hint.empty() &&
        from_port.asset_type_hint != to_port.asset_type_hint)
    {
      AddDiagnostic(diagnostics,
                    KX_CompileDiagnostic::Severity::Error,
                    connection.to_node,
                    connection.to_port,
                    "Asset type mismatch: output '" + from_port.name + "' carries '" +
                        from_port.asset_type_hint + "' but input '" + to_port.name +
                        "' expects '" + to_port.asset_type_hint + "'");
      continue;
    }

    if (from_port.kind == IRPortKind::Value) {
      if (value_source_node[connection.to_node][connection.to_port] >= 0) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      connection.to_node,
                      connection.to_port,
                      "Value input '" + to_port.name + "' has more than one connection");
        continue;
      }

      if (!IsImplicitConversionAllowed(from_port.value_tag, to_port.value_tag)) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      connection.to_node,
                      connection.to_port,
                      "Type mismatch: output '" + from_port.name + "' is " +
                          KX_ValueTypeName(from_port.value_tag) + " but input '" +
                          to_port.name + "' expects " + KX_ValueTypeName(to_port.value_tag));
        continue;
      }

      value_source_node[connection.to_node][connection.to_port] = connection.from_node;
      value_source_port[connection.to_node][connection.to_port] = connection.from_port;
      if (from_port.value_tag != to_port.value_tag) {
        input_conversions[connection.to_node][connection.to_port] = to_port.value_tag;
      }
    }
    else {
      trigger_connected[connection.to_node][connection.to_port] = true;
    }
  }

  /* Required inputs are checked after all connections were registered. */
  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    const IRNode &ir_node = ir.nodes[node_index];
    for (uint32_t port = 0; port < ir_node.inputs.size(); ++port) {
      const IRPort &ir_port = ir_node.inputs[port];
      if (!ir_port.required) {
        continue;
      }

      const bool connected = ir_port.kind == IRPortKind::Trigger
                                 ? trigger_connected[node_index][port]
                                 : value_source_node[node_index][port] >= 0;
      if (!connected) {
        AddDiagnostic(diagnostics,
                      KX_CompileDiagnostic::Severity::Error,
                      static_cast<int>(node_index),
                      static_cast<int>(port),
                      "Required input '" + ir_port.name + "' is not connected");
      }
    }
  }

  /* Dependency graph and deterministic topological order. */
  std::vector<std::vector<uint32_t>> dependencies(node_count);
  std::vector<uint32_t> in_degree(node_count, 0);

  for (const IRConnection &connection : ir.connections) {
    if (!IsNodeIndexValid(ir, connection.from_node) ||
        !IsNodeIndexValid(ir, connection.to_node))
    {
      continue;
    }
    if (signatures[connection.from_node].breaks_trigger_cycles) {
      continue;
    }

    const uint32_t from = static_cast<uint32_t>(connection.from_node);
    const uint32_t to = static_cast<uint32_t>(connection.to_node);
    dependencies[from].push_back(to);
    ++in_degree[to];
  }

  std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    if (in_degree[node_index] == 0) {
      ready.push(node_index);
    }
  }

  std::vector<uint32_t> topological_order;
  topological_order.reserve(node_count);

  while (!ready.empty()) {
    const uint32_t node_index = ready.top();
    ready.pop();
    topological_order.push_back(node_index);

    for (const uint32_t next : dependencies[node_index]) {
      if (in_degree[next] == 0) {
        continue;
      }
      --in_degree[next];
      if (in_degree[next] == 0) {
        ready.push(next);
      }
    }
  }

  if (topological_order.size() != node_count) {
    const std::vector<uint32_t> cycle = FindCycle(dependencies);
    std::string message = "Trigger/value cycle detected";
    if (!cycle.empty()) {
      message += ": ";
      for (size_t i = 0; i < cycle.size(); ++i) {
        if (i != 0) {
          message += " -> ";
        }
        message += NodeLabel(ir, cycle[i]);
      }
    }
    AddDiagnostic(diagnostics,
                  KX_CompileDiagnostic::Severity::Error,
                  cycle.empty() ? -1 : static_cast<int>(cycle.front()),
                  -1,
                  message);
  }

  if (HasErrors(diagnostics)) {
    result.ok = false;
    return result;
  }

  /* ---------------------------------------------------------------------- */
  /* Stage 4: Allocate                                                      */
  /* ---------------------------------------------------------------------- */

  KX_RuntimeData layout;

  std::vector<std::vector<uint32_t>> output_offsets(node_count);
  std::vector<uint32_t> state_offsets(node_count, KX_INVALID_OFFSET);

  /* Outputs and state first, in the deterministic topological order. */
  for (const uint32_t node_index : topological_order) {
    const IRNode &ir_node = ir.nodes[node_index];
    output_offsets[node_index].resize(ir_node.outputs.size());

    for (uint32_t port = 0; port < ir_node.outputs.size(); ++port) {
      if (ir_node.outputs[port].kind == IRPortKind::Trigger) {
        output_offsets[node_index][port] = layout.AllocateTriggerPort();
      }
      else {
        output_offsets[node_index][port] = layout.AllocateValuePort();
      }
    }

    const KX_NodeSignature &signature = signatures[node_index];
    if (signature.state_size != 0) {
      state_offsets[node_index] = layout.AllocateState(signature.state_size,
                                                       signature.state_alignment);
    }
  }

  /* Inputs are wired to their source output offset. Unconnected optional value
   * inputs get a zeroed slot of their own so nodes can always read them. */
  std::vector<std::vector<uint32_t>> input_offsets(node_count);

  for (const uint32_t node_index : topological_order) {
    const IRNode &ir_node = ir.nodes[node_index];
    input_offsets[node_index].resize(ir_node.inputs.size());

    for (uint32_t port = 0; port < ir_node.inputs.size(); ++port) {
      if (ir_node.inputs[port].kind == IRPortKind::Trigger) {
        input_offsets[node_index][port] = layout.AllocateTriggerPort();
        continue;
      }

      const int source_node = value_source_node[node_index][port];
      if (source_node >= 0) {
        const uint32_t source_port = static_cast<uint32_t>(
            value_source_port[node_index][port]);
        input_offsets[node_index][port] = output_offsets[source_node][source_port];
      }
      else {
        input_offsets[node_index][port] = layout.AllocateValuePort();
      }
    }
  }

  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    KX_GameNodeRuntime &node = *runtime_nodes[node_index];
    const uint32_t input_count = static_cast<uint32_t>(input_offsets[node_index].size());
    const uint32_t output_count = static_cast<uint32_t>(output_offsets[node_index].size());

    node.ConfigureInputs(input_count, input_offsets[node_index].data());
    node.ConfigureInputConversions(input_count, input_conversions[node_index].data());
    node.ConfigureOutputs(output_count, output_offsets[node_index].data());
    node.SetStateOffset(state_offsets[node_index]);
  }

  /* ---------------------------------------------------------------------- */
  /* Stage 5: Emit                                                          */
  /* ---------------------------------------------------------------------- */

  std::unique_ptr<KX_ExecutionPlan> plan = std::make_unique<KX_ExecutionPlan>();
  plan->runtime_data_size = layout.UsedSize();
  plan->execution_order = topological_order;
  plan->nodes.reserve(node_count);
  plan->debug_names.resize(node_count);
  plan->trigger_edges.resize(node_count);
  plan->trigger_outgoing.resize(node_count);
  plan->trigger_input_offsets.resize(node_count);
  plan->trigger_output_offsets.resize(node_count);

  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    plan->nodes.push_back(std::move(runtime_nodes[node_index]));
    plan->debug_names[node_index] = ir.nodes[node_index].debug_name.empty()
                                        ? ir.nodes[node_index].type_id
                                        : ir.nodes[node_index].debug_name;

    const IRNode &ir_node = ir.nodes[node_index];
    for (uint32_t port = 0; port < ir_node.inputs.size(); ++port) {
      if (ir_node.inputs[port].kind != IRPortKind::Trigger) {
        continue;
      }
      plan->trigger_input_offsets[node_index].push_back(input_offsets[node_index][port]);
      plan->trigger_slots.push_back(input_offsets[node_index][port]);
    }
    for (uint32_t port = 0; port < ir_node.outputs.size(); ++port) {
      if (ir_node.outputs[port].kind != IRPortKind::Trigger) {
        continue;
      }
      plan->trigger_output_offsets[node_index].push_back(output_offsets[node_index][port]);
      plan->trigger_slots.push_back(output_offsets[node_index][port]);
    }
  }

  for (const IRConnection &connection : ir.connections) {
    if (!IsNodeIndexValid(ir, connection.from_node) ||
        !IsNodeIndexValid(ir, connection.to_node))
    {
      continue;
    }
    const IRPort &from_port = ir.nodes[connection.from_node].outputs[connection.from_port];
    if (from_port.kind != IRPortKind::Trigger) {
      continue;
    }

    KX_TriggerEdge edge;
    edge.from_node = static_cast<uint32_t>(connection.from_node);
    edge.from_port = static_cast<uint32_t>(connection.from_port);
    edge.from_offset = output_offsets[connection.from_node][connection.from_port];
    edge.to_node = static_cast<uint32_t>(connection.to_node);
    edge.to_port = static_cast<uint32_t>(connection.to_port);
    edge.to_offset = input_offsets[connection.to_node][connection.to_port];

    plan->trigger_edges[edge.to_node].push_back(edge);
    plan->trigger_outgoing[edge.from_node].push_back(edge);
  }

  for (const uint32_t node_index : topological_order) {
    const KX_NodeSignature &signature = signatures[node_index];

    if (signature.needs_tick) {
      plan->tick_nodes.push_back(node_index);
    }

    const IRNode &ir_node = ir.nodes[node_index];
    bool has_trigger_input = false;
    bool has_trigger_output = false;
    std::vector<uint32_t> unconnected_trigger_inputs;

    for (uint32_t port = 0; port < ir_node.inputs.size(); ++port) {
      if (ir_node.inputs[port].kind != IRPortKind::Trigger) {
        continue;
      }
      has_trigger_input = true;
      if (!trigger_connected[node_index][port]) {
        unconnected_trigger_inputs.push_back(port);
      }
    }
    for (const IRPort &port : ir_node.outputs) {
      if (port.kind == IRPortKind::Trigger) {
        has_trigger_output = true;
      }
    }

    /* Pure value nodes are refreshed once per frame before trigger runs.
     * Poll mode asset/object nodes join this list even though they declare
     * trigger ports for their trigger mode variants. */
    const bool is_value_node = plan->nodes[node_index]->IsValueNode();
    if (is_value_node || (!has_trigger_input && !has_trigger_output)) {
      plan->value_nodes.push_back(node_index);
    }

    if (is_value_node) {
      /* A poll node is an entry point of its own through the value list, so its
       * unconnected trigger input must not create a second per frame entry. */
      continue;
    }

    /* Root detection: an unconnected trigger input is an entry point. A node
     * with trigger outputs but no trigger inputs at all is also an entry. */
    if (!unconnected_trigger_inputs.empty()) {
      plan->trigger_roots.push_back(node_index);
      for (const uint32_t port : unconnected_trigger_inputs) {
        KX_TriggerTarget entry;
        entry.node_index = node_index;
        entry.input_offset = input_offsets[node_index][port];
        plan->trigger_starts.push_back(entry);
      }
    }
    else if (!has_trigger_input && has_trigger_output) {
      plan->trigger_roots.push_back(node_index);
      KX_TriggerTarget entry;
      entry.node_index = node_index;
      entry.input_offset = KX_INVALID_OFFSET;
      plan->trigger_starts.push_back(entry);
    }
  }

  /* Asset references in node index order, deterministic across compiles. */
  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    KX_ResolvedAssetBinding binding;
    if (!plan->nodes[node_index]->GetResolvedAssetBinding(&binding)) {
      continue;
    }

    KX_AssetBinding plan_binding;
    plan_binding.node_index = node_index;
    plan_binding.asset_id = binding.asset_id;
    plan_binding.asset_type = binding.asset_type;
    plan_binding.handle = binding.handle;
    plan_binding.generation = binding.generation;
    plan_binding.field_name = binding.field_name;
    plan->asset_bindings.push_back(std::move(plan_binding));
  }

  /* Flat debug table, kept in IR declaration order. */
  for (uint32_t node_index = 0; node_index < node_count; ++node_index) {
    const IRNode &ir_node = ir.nodes[node_index];

    for (uint32_t port = 0; port < ir_node.inputs.size(); ++port) {
      KX_PortOffsetInfo info;
      info.node_index = node_index;
      info.port_index = port;
      info.input = true;
      info.trigger = ir_node.inputs[port].kind == IRPortKind::Trigger;
      info.offset = input_offsets[node_index][port];
      info.name = ir_node.inputs[port].name;
      plan->port_offsets.push_back(std::move(info));
    }
    for (uint32_t port = 0; port < ir_node.outputs.size(); ++port) {
      KX_PortOffsetInfo info;
      info.node_index = node_index;
      info.port_index = port;
      info.input = false;
      info.trigger = ir_node.outputs[port].kind == IRPortKind::Trigger;
      info.offset = output_offsets[node_index][port];
      info.name = ir_node.outputs[port].name;
      plan->port_offsets.push_back(std::move(info));
    }
  }

  result.ok = true;
  result.plan = std::move(plan);
  return result;
}
