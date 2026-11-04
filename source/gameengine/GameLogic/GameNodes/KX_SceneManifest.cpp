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

/** \file KX_SceneManifest.cpp
 *  \ingroup gamelogic
 */

#include "KX_SceneManifest.h"

#include "json.hpp"

namespace {

bool ParseValueTypeName(const std::string &name, KX_ValueType *type)
{
  if (name == "Bool") {
    *type = KX_VALUE_BOOL;
  }
  else if (name == "Int") {
    *type = KX_VALUE_INT;
  }
  else if (name == "Float") {
    *type = KX_VALUE_FLOAT;
  }
  else if (name == "Vector") {
    *type = KX_VALUE_VECTOR;
  }
  else if (name == "Object") {
    *type = KX_VALUE_OBJECT;
  }
  else if (name == "Asset") {
    *type = KX_VALUE_ASSET;
  }
  else if (name == "Color") {
    *type = KX_VALUE_COLOR;
  }
  else if (name == "String") {
    *type = KX_VALUE_STRING;
  }
  else if (name == "List") {
    *type = KX_VALUE_LIST;
  }
  else if (name == "DataBlock" || name == "Datablock") {
    *type = KX_VALUE_DATABLOCK;
  }
  else if (name == "None" || name.empty()) {
    *type = KX_VALUE_NONE;
  }
  else {
    return false;
  }
  return true;
}

bool ParseFieldTypeName(const std::string &name, KX_AssetFieldType *type)
{
  if (name == "Bool") {
    *type = KX_AssetFieldType::Bool;
  }
  else if (name == "Int") {
    *type = KX_AssetFieldType::Int;
  }
  else if (name == "Float") {
    *type = KX_AssetFieldType::Float;
  }
  else if (name == "Vector") {
    *type = KX_AssetFieldType::Vector;
  }
  else if (name == "ObjectRef") {
    *type = KX_AssetFieldType::ObjectRef;
  }
  else if (name == "AssetRef") {
    *type = KX_AssetFieldType::AssetRef;
  }
  else if (name == "String") {
    *type = KX_AssetFieldType::String;
  }
  else if (name == "List") {
    *type = KX_AssetFieldType::List;
  }
  else {
    return false;
  }
  return true;
}

KX_RuntimeValue ParseDefaultValue(const nlohmann::json &value, KX_AssetFieldType type)
{
  switch (type) {
    case KX_AssetFieldType::Bool:
      return KX_RuntimeValue::Bool(value.is_boolean() && value.get<bool>());
    case KX_AssetFieldType::Int:
      return KX_RuntimeValue::Int(value.is_number() ? value.get<int>() : 0);
    case KX_AssetFieldType::Float:
      return KX_RuntimeValue::Float(value.is_number() ? value.get<float>() : 0.0f);
    case KX_AssetFieldType::Vector: {
      if (!value.is_array() || value.size() < 3) {
        return KX_RuntimeValue::Vector(0.0f, 0.0f, 0.0f);
      }
      return KX_RuntimeValue::Vector(
          value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    }
    default:
      return KX_RuntimeValue::None();
  }
}

void ParsePorts(const nlohmann::json &array,
                std::vector<IRPort> *ports,
                const std::string &graph_id,
                const std::string &node_name,
                std::vector<std::string> *warnings)
{
  if (!array.is_array()) {
    return;
  }

  for (const nlohmann::json &entry : array) {
    IRPort port;
    if (entry.contains("name") && entry["name"].is_string()) {
      port.name = entry["name"].get<std::string>();
    }

    std::string kind = "value";
    if (entry.contains("kind") && entry["kind"].is_string()) {
      kind = entry["kind"].get<std::string>();
    }
    if (kind == "trigger") {
      port.kind = IRPortKind::Trigger;
      port.value_tag = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      std::string tag = "None";
      if (entry.contains("value_tag") && entry["value_tag"].is_string()) {
        tag = entry["value_tag"].get<std::string>();
      }
      if (!ParseValueTypeName(tag, &port.value_tag)) {
        warnings->push_back("Graph '" + graph_id + "', node '" + node_name +
                            "': unknown port type '" + tag + "'");
        port.value_tag = KX_VALUE_NONE;
      }
    }

    if (entry.contains("required") && entry["required"].is_boolean()) {
      port.required = entry["required"].get<bool>();
    }
    if (entry.contains("asset_type_hint") && entry["asset_type_hint"].is_string()) {
      port.asset_type_hint = entry["asset_type_hint"].get<std::string>();
    }
    ports->push_back(std::move(port));
  }
}

void ParseAssets(const nlohmann::json &array,
                 std::vector<KX_ManifestAsset> *assets,
                 std::vector<std::string> *warnings)
{
  if (!array.is_array()) {
    return;
  }

  for (const nlohmann::json &entry : array) {
    if (!entry.is_object()) {
      continue;
    }
    KX_ManifestAsset asset;
    if (entry.contains("asset_id") && entry["asset_id"].is_string()) {
      asset.asset_id = entry["asset_id"].get<std::string>();
    }
    if (entry.contains("asset_type") && entry["asset_type"].is_string()) {
      asset.asset_type = entry["asset_type"].get<std::string>();
    }
    if (asset.asset_type.empty()) {
      asset.asset_type = asset.asset_id;
    }
    if (asset.asset_id.empty()) {
      warnings->push_back("Scene manifest contains an asset without an id");
      continue;
    }

    if (entry.contains("fields") && entry["fields"].is_array()) {
      for (const nlohmann::json &field_entry : entry["fields"]) {
        if (!field_entry.is_object()) {
          continue;
        }
        KX_ManifestAssetField field;
        if (field_entry.contains("name") && field_entry["name"].is_string()) {
          field.name = field_entry["name"].get<std::string>();
        }
        if (field.name.empty()) {
          continue;
        }

        std::string type_name = "Float";
        if (field_entry.contains("type") && field_entry["type"].is_string()) {
          type_name = field_entry["type"].get<std::string>();
        }
        if (!ParseFieldTypeName(type_name, &field.type)) {
          warnings->push_back("Asset '" + asset.asset_id + "': unknown field type '" +
                              type_name + "'");
          continue;
        }

        if (field_entry.contains("default")) {
          field.default_value = ParseDefaultValue(field_entry["default"], field.type);
        }
        else {
          field.default_value = KX_MakeZeroValue(KX_AssetFieldValueType(field.type));
        }
        asset.fields.push_back(std::move(field));
      }
    }
    assets->push_back(std::move(asset));
  }
}

void ParseGraphs(const nlohmann::json &array,
                 std::vector<KX_ManifestGraph> *graphs,
                 std::vector<std::string> *warnings)
{
  if (!array.is_array()) {
    return;
  }

  for (const nlohmann::json &entry : array) {
    if (!entry.is_object()) {
      continue;
    }
    KX_ManifestGraph graph;
    if (entry.contains("graph_id") && entry["graph_id"].is_string()) {
      graph.graph_id = entry["graph_id"].get<std::string>();
    }
    if (graph.graph_id.empty()) {
      warnings->push_back("Scene manifest contains a graph without an id");
      continue;
    }

    if (entry.contains("nodes") && entry["nodes"].is_array()) {
      for (const nlohmann::json &node_entry : entry["nodes"]) {
        if (!node_entry.is_object()) {
          continue;
        }
        IRNode node;
        if (node_entry.contains("type_id") && node_entry["type_id"].is_string()) {
          node.type_id = node_entry["type_id"].get<std::string>();
        }
        if (node_entry.contains("debug_name") && node_entry["debug_name"].is_string()) {
          node.debug_name = node_entry["debug_name"].get<std::string>();
        }
        if (node_entry.contains("inputs")) {
          ParsePorts(node_entry["inputs"],
                     &node.inputs,
                     graph.graph_id,
                     node.debug_name,
                     warnings);
        }
        if (node_entry.contains("outputs")) {
          ParsePorts(node_entry["outputs"],
                     &node.outputs,
                     graph.graph_id,
                     node.debug_name,
                     warnings);
        }
        if (node_entry.contains("config")) {
          node.config_json = node_entry["config"].dump();
        }
        if (node.type_id.empty()) {
          warnings->push_back("Graph '" + graph.graph_id + "', node '" + node.debug_name +
                              "': missing type id");
        }
        graph.ir.nodes.push_back(std::move(node));
      }
    }

    if (entry.contains("connections") && entry["connections"].is_array()) {
      for (const nlohmann::json &connection_entry : entry["connections"]) {
        if (!connection_entry.is_object()) {
          continue;
        }
        IRConnection connection;
        connection.from_node = connection_entry.value("from_node", -1);
        connection.from_port = connection_entry.value("from_port", -1);
        connection.to_node = connection_entry.value("to_node", -1);
        connection.to_port = connection_entry.value("to_port", -1);
        graph.ir.connections.push_back(connection);
      }
    }

    graphs->push_back(std::move(graph));
  }
}

void ParseMounts(const nlohmann::json &array,
                 std::vector<KX_ManifestMount> *mounts,
                 std::vector<std::string> *warnings)
{
  if (!array.is_array()) {
    return;
  }

  for (const nlohmann::json &entry : array) {
    if (!entry.is_object()) {
      continue;
    }
    KX_ManifestMount mount;
    if (entry.contains("scene") && entry["scene"].is_string()) {
      mount.scene = entry["scene"].get<std::string>();
    }
    if (entry.contains("object") && entry["object"].is_string()) {
      mount.object = entry["object"].get<std::string>();
    }
    if (entry.contains("graph_id") && entry["graph_id"].is_string()) {
      mount.graph_id = entry["graph_id"].get<std::string>();
    }
    mount.priority = entry.value("priority", 0);

    if (mount.object.empty() || mount.graph_id.empty()) {
      warnings->push_back("Scene manifest contains a mount without object or graph");
      continue;
    }
    mounts->push_back(std::move(mount));
  }
}

}  // namespace

bool KX_ParseSceneManifestJson(const std::string &json_text, KX_SceneManifest *manifest)
{
  if (manifest == nullptr) {
    return false;
  }

  /* A .blend must never crash the engine because of a stale custom property:
   * malformed JSON is reported as a plain parse failure by the caller. */
  const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    return false;
  }

  if (root.contains("assets")) {
    ParseAssets(root["assets"], &manifest->assets, &manifest->warnings);
  }
  if (root.contains("graphs")) {
    ParseGraphs(root["graphs"], &manifest->graphs, &manifest->warnings);
  }
  if (root.contains("mounts")) {
    ParseMounts(root["mounts"], &manifest->mounts, &manifest->warnings);
  }
  return true;
}
