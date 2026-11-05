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

/** \file KX_FunctionNodesB4File.cpp
 *  \ingroup gamelogic
 *
 *  B4 file domain: save games, generic file IO and JSON conversion.
 *
 *  SaveGame serializes every KX_GameDataAsset field table plus its values into
 *  the lightweight #KXJson format and hands the blob to #KX_FileAdapter.
 *  LoadGame parses the blob and writes the values back into the matching
 *  assets. Object property saving stays in B5.
 *
 *  JSONStringify and JSONParse build their ports from the IR (`ports_from_ir`):
 *  the editor derives one socket per schema key, the runtime reads them by
 *  index in schema order. Their schema config is part of the export contract.
 *
 *  Every failure path raises OnFail and prints a stderr diagnostic. No IO or
 *  parse error is reported as success.
 */

#include "KX_FunctionNode.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_B4AssetIO.h"
#include "KX_AssetRegistry.h"
#include "KX_FileAdapter.h"
#include "KX_GameAdapters.h"
#include "KX_GameDataAsset.h"
#include "KX_GraphIR.h"
#include "KX_Json.h"
#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"
#include "KX_SaveGameBinary.h"
#include "KX_ValuePool.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

std::string B4InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

void B4Warn(const char *node_name, const std::string &message)
{
  std::fprintf(stderr, "GameNode: %s: %s\n", node_name, message.c_str());
}

/** Guard dynamic trigger indexes against a graph whose IR ports were rebuilt. */
bool B4Trigger(KX_FunctionNode &node,
               KX_ExecutionContext &ctx,
               uint32_t index,
               const char *node_name)
{
  if (index >= node.OutputCount()) {
    B4Warn(node_name, "trigger output index is out of range");
    return false;
  }
  node.OutTrigger(ctx, index, true);
  return true;
}

/* ------------------------------------------------------------------------- */
/* Schema                                                                    */
/* ------------------------------------------------------------------------- */

struct B4SchemaField {
  std::string name;
  KX_ValueType type = KX_VALUE_FLOAT;
};

std::string B4Trim(const std::string &text)
{
  size_t begin = 0;
  size_t end = text.size();
  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool B4ParseSchema(const std::string &schema,
                   std::vector<B4SchemaField> *fields,
                   std::string *error)
{
  fields->clear();
  std::string remaining = schema;
  size_t start = 0;
  while (start <= remaining.size()) {
    const size_t comma = remaining.find(',', start);
    const std::string part = B4Trim(
        remaining.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (!part.empty()) {
      const size_t colon = part.find(':');
      if (colon == std::string::npos) {
        if (error != nullptr) {
          *error = "schema entry '" + part + "' is not key:type";
        }
        return false;
      }
      const std::string key = B4Trim(part.substr(0, colon));
      const std::string type = B4Trim(part.substr(colon + 1));
      if (key.empty()) {
        if (error != nullptr) {
          *error = "schema entry has an empty key";
        }
        return false;
      }
      for (const B4SchemaField &existing : *fields) {
        if (existing.name == key) {
          if (error != nullptr) {
            *error = "duplicate schema key '" + key + "'";
          }
          return false;
        }
      }
      KX_ValueType value_type = KX_VALUE_NONE;
      if (type == "string") {
        value_type = KX_VALUE_STRING;
      }
      else if (type == "float") {
        value_type = KX_VALUE_FLOAT;
      }
      else if (type == "int") {
        value_type = KX_VALUE_INT;
      }
      else if (type == "bool") {
        value_type = KX_VALUE_BOOL;
      }
      else {
        if (error != nullptr) {
          *error = "unknown schema type '" + type + "' for key '" + key +
                   "' (expected string, float, int or bool)";
        }
        return false;
      }
      B4SchemaField field;
      field.name = key;
      field.type = value_type;
      fields->push_back(field);
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (fields->empty()) {
    if (error != nullptr) {
      *error = "schema is empty";
    }
    return false;
  }
  return true;
}

/* ------------------------------------------------------------------------- */
/* JSON value conversion                                                     */
/* ------------------------------------------------------------------------- */

KXJson::Value B4ScalarToJson(const KX_RuntimeValue &value)
{
  switch (value.type) {
    case KX_VALUE_BOOL:
      return KXJson::Value::MakeBool(value.data.b);
    case KX_VALUE_INT:
      return KXJson::Value::MakeNumber(static_cast<double>(value.data.i));
    case KX_VALUE_FLOAT:
      return KXJson::Value::MakeNumber(static_cast<double>(value.data.f));
    case KX_VALUE_VECTOR: {
      KXJson::Value array = KXJson::Value::MakeArray();
      array.array.push_back(KXJson::Value::MakeNumber(static_cast<double>(value.data.vec[0])));
      array.array.push_back(KXJson::Value::MakeNumber(static_cast<double>(value.data.vec[1])));
      array.array.push_back(KXJson::Value::MakeNumber(static_cast<double>(value.data.vec[2])));
      return array;
    }
    case KX_VALUE_STRING: {
      const char *text = value.AsString();
      return KXJson::Value::MakeString(text != nullptr ? text : "");
    }
    default:
      return KXJson::Value::MakeNull();
  }
}

KXJson::Value B4FieldToJson(const KX_RuntimeValue &value, KX_AssetFieldType field_type)
{
  if (field_type == KX_AssetFieldType::List) {
    KXJson::Value array = KXJson::Value::MakeArray();
    const void *raw = value.AsList();
    if (raw != nullptr) {
      const KX_ValueList *list = static_cast<const KX_ValueList *>(raw);
      array.array.reserve(list->size());
      for (const KX_RuntimeValue &element : *list) {
        array.array.push_back(B4ScalarToJson(element));
      }
    }
    return array;
  }
  if (field_type == KX_AssetFieldType::String) {
    const char *text = value.AsString();
    return KXJson::Value::MakeString(text != nullptr ? text : "");
  }
  if (field_type == KX_AssetFieldType::ObjectRef || field_type == KX_AssetFieldType::AssetRef) {
    /* B5 saves object properties; references are not part of the B4 blob. */
    return KXJson::Value::MakeNull();
  }
  return B4ScalarToJson(value);
}

KX_RuntimeValue B4ReadScalar(const KXJson::Value &value,
                             KX_ValueType type,
                             KX_ValuePool &pool,
                             bool *ok,
                             std::string *error,
                             const std::string &field_name)
{
  *ok = true;
  if (value.IsNull()) {
    return KX_MakeZeroValue(type);
  }
  switch (type) {
    case KX_VALUE_BOOL:
      if (value.IsBool()) {
        return KX_RuntimeValue::Bool(value.boolean);
      }
      break;
    case KX_VALUE_INT:
      if (value.IsNumber()) {
        return KX_RuntimeValue::Int(static_cast<int>(value.number));
      }
      if (value.IsBool()) {
        return KX_RuntimeValue::Int(value.boolean ? 1 : 0);
      }
      break;
    case KX_VALUE_FLOAT:
      if (value.IsNumber()) {
        return KX_RuntimeValue::Float(static_cast<float>(value.number));
      }
      if (value.IsBool()) {
        return KX_RuntimeValue::Float(value.boolean ? 1.0f : 0.0f);
      }
      break;
    case KX_VALUE_VECTOR:
      if (value.IsArray() && value.array.size() >= 3 && value.array[0].IsNumber() &&
          value.array[1].IsNumber() && value.array[2].IsNumber())
      {
        return KX_RuntimeValue::Vector(static_cast<float>(value.array[0].number),
                                       static_cast<float>(value.array[1].number),
                                       static_cast<float>(value.array[2].number));
      }
      break;
    case KX_VALUE_STRING:
      if (value.IsString()) {
        return KX_RuntimeValue::String(pool.Intern(value.string));
      }
      break;
    default:
      break;
  }
  *ok = false;
  if (error != nullptr) {
    *error = "field '" + field_name + "' expects " + KX_ValueTypeName(type);
  }
  return KX_MakeZeroValue(type);
}

KX_RuntimeValue B4ReadFieldValue(const KXJson::Value &value,
                                 KX_AssetFieldType field_type,
                                 KX_ValuePool &pool,
                                 bool *ok,
                                 std::string *error,
                                 const std::string &field_name)
{
  *ok = true;
  switch (field_type) {
    case KX_AssetFieldType::Bool:
      return B4ReadScalar(value, KX_VALUE_BOOL, pool, ok, error, field_name);
    case KX_AssetFieldType::Int:
      return B4ReadScalar(value, KX_VALUE_INT, pool, ok, error, field_name);
    case KX_AssetFieldType::Float:
      return B4ReadScalar(value, KX_VALUE_FLOAT, pool, ok, error, field_name);
    case KX_AssetFieldType::Vector:
      return B4ReadScalar(value, KX_VALUE_VECTOR, pool, ok, error, field_name);
    case KX_AssetFieldType::String:
      return B4ReadScalar(value, KX_VALUE_STRING, pool, ok, error, field_name);
    case KX_AssetFieldType::List: {
      if (!value.IsArray()) {
        *ok = false;
        if (error != nullptr) {
          *error = "field '" + field_name + "' expects an array";
        }
        return KX_MakeZeroValue(KX_VALUE_LIST);
      }
      KX_ValueList &list = pool.AllocList(value.array.size());
      for (size_t i = 0; i < value.array.size(); ++i) {
        const KXJson::Value &element = value.array[i];
        KX_RuntimeValue element_value;
        if (element.IsNull()) {
          element_value = KX_RuntimeValue::None();
        }
        else if (element.IsBool()) {
          element_value = KX_RuntimeValue::Bool(element.boolean);
        }
        else if (element.IsNumber()) {
          element_value = KX_RuntimeValue::Float(static_cast<float>(element.number));
        }
        else if (element.IsString()) {
          element_value = KX_RuntimeValue::String(pool.Intern(element.string));
        }
        else if (element.IsArray() && element.array.size() >= 3 && element.array[0].IsNumber() &&
                 element.array[1].IsNumber() && element.array[2].IsNumber())
        {
          element_value = KX_RuntimeValue::Vector(static_cast<float>(element.array[0].number),
                                                  static_cast<float>(element.array[1].number),
                                                  static_cast<float>(element.array[2].number));
        }
        else {
          *ok = false;
          if (error != nullptr) {
            *error = "field '" + field_name + "': unsupported list element type";
          }
          return KX_MakeZeroValue(KX_VALUE_LIST);
        }
        list[i] = element_value;
      }
      return KX_RuntimeValue::List(&list);
    }
    case KX_AssetFieldType::ObjectRef:
    case KX_AssetFieldType::AssetRef:
      /* References are not serialized in B4. */
      return KX_MakeZeroValue(KX_ValueType::KX_VALUE_ASSET);
  }
  *ok = false;
  return KX_MakeZeroValue(KX_VALUE_NONE);
}

/* ------------------------------------------------------------------------- */
/* Asset blob                                                                */
/* ------------------------------------------------------------------------- */

}  // namespace

/* Shared with the B6.4 binary save node (see KX_B4AssetIO.h). */
bool B4SerializeAssetsValue(KX_ExecutionContext &ctx,
                            KXJson::Value *out,
                            std::string *error)
{
  KX_AssetRegistry &registry = ctx.Assets();
  std::vector<KX_AssetHandle> handles;
  registry.CollectHandles(handles);

  KXJson::Value root = KXJson::Value::MakeObject();
  root.object.emplace_back("version", KXJson::Value::MakeNumber(1.0));
  KXJson::Value assets = KXJson::Value::MakeArray();
  for (KX_AssetHandle handle : handles) {
    KX_GameDataAsset *asset = registry.Get(handle);
    if (asset == nullptr) {
      continue;
    }
    KXJson::Value entry = KXJson::Value::MakeObject();
    entry.object.emplace_back("id", KXJson::Value::MakeString(asset->asset_id));
    entry.object.emplace_back("type", KXJson::Value::MakeString(asset->asset_type));
    KXJson::Value fields = KXJson::Value::MakeObject();
    const size_t field_count = asset->FieldCount();
    for (uint32_t i = 0; i < field_count; ++i) {
      const KX_AssetFieldDesc &description = asset->Field(i);
      fields.object.emplace_back(description.name,
                                 B4FieldToJson(asset->ReadField(i), description.type));
    }
    entry.object.emplace_back("fields", std::move(fields));
    assets.array.push_back(std::move(entry));
  }
  root.object.emplace_back("assets", std::move(assets));
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "asset serialization needs an output value";
    }
    return false;
  }
  *out = std::move(root);
  return true;
}

bool B4SerializeAssets(KX_ExecutionContext &ctx, std::string *out, std::string *error)
{
  KXJson::Value root;
  if (!B4SerializeAssetsValue(ctx, &root, error)) {
    return false;
  }
  *out = KXJson::Stringify(root);
  if (out->empty()) {
    if (error != nullptr) {
      *error = "asset serialization produced an empty blob";
    }
    return false;
  }
  return true;
}

bool B4ApplyAssets(KX_ExecutionContext &ctx,
                   const KXJson::Value &root,
                   std::string *error)
{
  if (!root.IsObject()) {
    if (error != nullptr) {
      *error = "save blob root is not an object";
    }
    return false;
  }
  const KXJson::Value *assets = root.Find("assets");
  if (assets == nullptr || !assets->IsArray()) {
    if (error != nullptr) {
      *error = "save blob has no 'assets' array";
    }
    return false;
  }
  KX_AssetRegistry &registry = ctx.Assets();
  for (const KXJson::Value &entry : assets->array) {
    if (!entry.IsObject()) {
      continue;
    }
    const KXJson::Value *id = entry.Find("id");
    if (id == nullptr || !id->IsString()) {
      continue;
    }
    KX_AssetRegistry::LookupResult lookup = registry.Lookup(id->string);
    if (lookup.asset == nullptr) {
      B4Warn("LoadGame", "asset '" + id->string + "' is not registered, skipping");
      continue;
    }
    const KXJson::Value *fields = entry.Find("fields");
    if (fields == nullptr || !fields->IsObject()) {
      B4Warn("LoadGame", "asset '" + id->string + "' has no fields object, skipping");
      continue;
    }
    KX_GameDataAsset *asset = lookup.asset;
    const size_t field_count = asset->FieldCount();
    for (uint32_t i = 0; i < field_count; ++i) {
      const KX_AssetFieldDesc &description = asset->Field(i);
      const KXJson::Value *value = fields->Find(description.name);
      if (value == nullptr) {
        continue;
      }
      if (description.type == KX_AssetFieldType::ObjectRef ||
          description.type == KX_AssetFieldType::AssetRef)
      {
        continue;
      }
      bool ok = true;
      std::string read_error;
      KX_RuntimeValue converted = B4ReadFieldValue(
          *value, description.type, ctx.Pool(), &ok, &read_error, description.name);
      if (!ok) {
        if (error != nullptr) {
          *error = "asset '" + asset->asset_id + "': " + read_error;
        }
        return false;
      }
      asset->WriteField(i, converted);
    }
  }
  return true;
}

/* ------------------------------------------------------------------------- */
/* Save / load                                                               */
/* ------------------------------------------------------------------------- */

namespace {

const KX_FuncPort kSaveGameInputs[] = {
    TP("OnEnter"),
    FP("Slot", KX_VALUE_STRING),
};
const KX_FuncPort kSaveLoadOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncSaveGame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string slot = B4InString(node, ctx, 1);
  if (slot.empty()) {
    B4Warn("SaveGame", "empty save slot");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string format = node.ConfigString("format");
  if (format.empty()) {
    format = "json";
  }
  if (format != "json") {
    B4Warn("SaveGame", "save format '" + format + "' is not implemented, use json");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string blob;
  std::string error;
  if (!B4SerializeAssets(ctx, &blob, &error)) {
    B4Warn("SaveGame", error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  if (!ctx.File().SaveGame(slot, blob)) {
    B4Warn("SaveGame", "adapter failed to write slot '" + slot + "'");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kLoadGameInputs[] = {
    TP("OnEnter"),
    FP("Slot", KX_VALUE_STRING),
};

void FuncLoadGame(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string slot = B4InString(node, ctx, 1);
  if (slot.empty()) {
    B4Warn("LoadGame", "empty save slot");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string format = node.ConfigString("format");
  if (format.empty()) {
    format = "json";
  }
  if (format != "json" && format != "binary") {
    B4Warn("LoadGame", "save format '" + format + "' is not implemented, use json or binary");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string blob;
  if (format == "binary") {
    if (!ctx.File().LoadGameBinary(slot, &blob)) {
      B4Warn("LoadGame", "adapter failed to read binary slot '" + slot + "'");
      node.OutTrigger(ctx, 1, true);
      return;
    }
    KXJson::Value root;
    std::string decode_error;
    if (!KXSaveGameBinary::Decode(blob, &root, &decode_error)) {
      B4Warn("LoadGame", "binary save rejected: " + decode_error);
      node.OutTrigger(ctx, 1, true);
      return;
    }
    std::string apply_error;
    if (!B4ApplyAssets(ctx, root, &apply_error)) {
      B4Warn("LoadGame", apply_error);
      node.OutTrigger(ctx, 1, true);
      return;
    }
    node.OutTrigger(ctx, 0, true);
    return;
  }
  if (!ctx.File().LoadGame(slot, &blob)) {
    B4Warn("LoadGame", "adapter failed to read slot '" + slot + "'");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  KXJson::Value root;
  std::string parse_error;
  if (!KXJson::Parse(blob, &root, &parse_error)) {
    B4Warn("LoadGame", "JSON parse failed: " + parse_error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  std::string apply_error;
  if (!B4ApplyAssets(ctx, root, &apply_error)) {
    B4Warn("LoadGame", apply_error);
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

/* ------------------------------------------------------------------------- */
/* Generic file IO                                                           */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kReadFileInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};
const KX_FuncPort kReadFileOutputs[] = {
    FP("Content", KX_VALUE_STRING),
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncReadFile(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B4InString(node, ctx, 1);
  if (path.empty()) {
    B4Warn("ReadFile", "empty path");
    node.OutTrigger(ctx, 2, true);
    return;
  }
  std::string content;
  if (!ctx.File().ReadFile(path, node.ConfigBool("binary", false), &content)) {
    B4Warn("ReadFile", "adapter failed to read '" + path + "'");
    node.OutTrigger(ctx, 2, true);
    return;
  }
  node.OutString(ctx, 0, content);
  node.OutTrigger(ctx, 1, true);
}

const KX_FuncPort kWriteFileInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
    FP("Content", KX_VALUE_STRING),
};

void FuncWriteFile(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B4InString(node, ctx, 1);
  if (path.empty()) {
    B4Warn("WriteFile", "empty path");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  const std::string content = B4InString(node, ctx, 2);
  if (!ctx.File().WriteFile(
          path, node.ConfigBool("binary", false), content.data(), content.size()))
  {
    B4Warn("WriteFile", "adapter failed to write '" + path + "'");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kDeleteFileInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};

void FuncDeleteFile(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B4InString(node, ctx, 1);
  if (path.empty()) {
    B4Warn("DeleteFile", "empty path");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  if (!ctx.File().DeleteFile(path)) {
    B4Warn("DeleteFile", "adapter failed to delete '" + path + "'");
    node.OutTrigger(ctx, 1, true);
    return;
  }
  node.OutTrigger(ctx, 0, true);
}

const KX_FuncPort kFileExistsInputs[] = {
    TP("OnEnter"),
    FP("Path", KX_VALUE_STRING),
};
const KX_FuncPort kFileExistsOutputs[] = {
    FP("Exists", KX_VALUE_BOOL),
};

void FuncFileExists(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string path = B4InString(node, ctx, 1);
  node.OutBool(ctx, 0, ctx.File().FileExists(path));
}

/* ------------------------------------------------------------------------- */
/* JSON nodes (ports derived from the IR)                                    */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kJsonStringifyInputs[] = {
    TP("OnEnter"),
};
const KX_FuncPort kJsonStringifyOutputs[] = {
    FP("Result", KX_VALUE_STRING),
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncJsonStringify(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::vector<B4SchemaField> fields;
  std::string error;
  if (!B4ParseSchema(node.ConfigString("schema"), &fields, &error)) {
    B4Warn("JSONStringify", error);
    node.OutTrigger(ctx, 2, true);
    return;
  }
  if (fields.size() > KX_MAX_NODE_INPUTS - 1) {
    B4Warn("JSONStringify", "schema has more keys than available inputs");
    node.OutTrigger(ctx, 2, true);
    return;
  }
  KXJson::Value root = KXJson::Value::MakeObject();
  for (size_t i = 0; i < fields.size(); ++i) {
    const KX_RuntimeValue value = node.In(ctx, static_cast<uint32_t>(i + 1));
    KXJson::Value converted = KXJson::Value::MakeNull();
    switch (fields[i].type) {
      case KX_VALUE_STRING: {
        const char *text = value.AsString();
        converted = KXJson::Value::MakeString(text != nullptr ? text : "");
        break;
      }
      case KX_VALUE_FLOAT:
        converted = KXJson::Value::MakeNumber(static_cast<double>(value.AsFloat()));
        break;
      case KX_VALUE_INT:
        converted = KXJson::Value::MakeNumber(static_cast<double>(value.AsInt()));
        break;
      case KX_VALUE_BOOL:
        converted = KXJson::Value::MakeBool(value.AsBool());
        break;
      default:
        break;
    }
    root.object.emplace_back(fields[i].name, std::move(converted));
  }
  node.OutString(ctx, 0, KXJson::Stringify(root));
  node.OutTrigger(ctx, 1, true);
}

const KX_FuncPort kJsonParseInputs[] = {
    TP("OnEnter"),
    FP("Source", KX_VALUE_STRING),
};
const KX_FuncPort kJsonParseOutputs[] = {
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncJsonParse(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  std::vector<B4SchemaField> fields;
  std::string error;
  if (!B4ParseSchema(node.ConfigString("schema"), &fields, &error)) {
    B4Warn("JSONParse", error);
    B4Trigger(node, ctx, static_cast<uint32_t>(fields.size() + 1), "JSONParse");
    return;
  }
  if (fields.size() + 2 > KX_MAX_NODE_OUTPUTS) {
    B4Warn("JSONParse", "schema has more keys than available outputs");
    return;
  }
  const std::string source = B4InString(node, ctx, 1);
  KXJson::Value root;
  std::string parse_error;
  const uint32_t fail_index = static_cast<uint32_t>(fields.size() + 1);
  const uint32_t success_index = static_cast<uint32_t>(fields.size());
  if (!KXJson::Parse(source, &root, &parse_error)) {
    B4Warn("JSONParse", "JSON parse failed: " + parse_error);
    B4Trigger(node, ctx, fail_index, "JSONParse");
    return;
  }
  if (!root.IsObject()) {
    B4Warn("JSONParse", "source is not a JSON object");
    B4Trigger(node, ctx, fail_index, "JSONParse");
    return;
  }
  for (size_t i = 0; i < fields.size(); ++i) {
    const KXJson::Value *value = root.Find(fields[i].name);
    KXJson::Value missing;
    if (value == nullptr) {
      value = &missing;
    }
    bool ok = true;
    std::string read_error;
    const KX_RuntimeValue converted = B4ReadScalar(
        *value, fields[i].type, ctx.Pool(), &ok, &read_error, fields[i].name);
    if (!ok) {
      B4Warn("JSONParse", read_error);
      B4Trigger(node, ctx, fail_index, "JSONParse");
      return;
    }
    switch (fields[i].type) {
      case KX_VALUE_STRING: {
        const char *text = converted.AsString();
        node.OutString(ctx, static_cast<uint32_t>(i), text != nullptr ? text : "");
        break;
      }
      case KX_VALUE_FLOAT:
        node.OutFloat(ctx, static_cast<uint32_t>(i), converted.AsFloat());
        break;
      case KX_VALUE_INT:
        node.OutInt(ctx, static_cast<uint32_t>(i), converted.AsInt());
        break;
      case KX_VALUE_BOOL:
        node.OutBool(ctx, static_cast<uint32_t>(i), converted.AsBool());
        break;
      default:
        break;
    }
  }
  B4Trigger(node, ctx, success_index, "JSONParse");
}

}  // namespace

namespace {

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define B4_ACTION(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

/* Ports come from the IR; runtime reads them by schema order. */
#define B4_DYNAMIC(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false, nullptr, nullptr, nullptr, true}

const KX_FunctionSpec kB4FileSpecs[] = {
    B4_ACTION("SaveGame", kSaveGameInputs, kSaveLoadOutputs, FuncSaveGame),
    B4_ACTION("LoadGame", kLoadGameInputs, kSaveLoadOutputs, FuncLoadGame),
    B4_ACTION("ReadFile", kReadFileInputs, kReadFileOutputs, FuncReadFile),
    B4_ACTION("WriteFile", kWriteFileInputs, kSaveLoadOutputs, FuncWriteFile),
    B4_ACTION("FileExists", kFileExistsInputs, kFileExistsOutputs, FuncFileExists),
    B4_ACTION("DeleteFile", kDeleteFileInputs, kSaveLoadOutputs, FuncDeleteFile),
    B4_DYNAMIC("JSONStringify", kJsonStringifyInputs, kJsonStringifyOutputs, FuncJsonStringify),
    B4_DYNAMIC("JSONParse", kJsonParseInputs, kJsonParseOutputs, FuncJsonParse),
};

#undef PORT_COUNT

}  // namespace

namespace {

KX_NodeSignature MakeB4Signature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    if (spec.inputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.inputs[i].type;
    }
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    if (spec.outputs[i].trigger) {
      port.kind = IRPortKind::Trigger;
      port.value_type = KX_VALUE_NONE;
    }
    else {
      port.kind = IRPortKind::Value;
      port.value_type = spec.outputs[i].type;
    }
    signature.outputs.push_back(port);
  }
  signature.state_size = spec.state_size;
  signature.state_alignment = spec.state_alignment;
  signature.needs_tick = spec.needs_tick || spec.tick != nullptr;
  signature.breaks_trigger_cycles = spec.breaks_trigger_cycles;
  signature.ports_from_ir = spec.ports_from_ir;
  return signature;
}

}  // namespace

void RegisterB4FileFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB4FileSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB4_") + spec.type_id,
        MakeB4Signature(*entry),
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
