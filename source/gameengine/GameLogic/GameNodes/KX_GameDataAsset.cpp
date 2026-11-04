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

/** \file KX_GameDataAsset.cpp
 *  \ingroup gamelogic
 */

#include "KX_GameDataAsset.h"

#include <cassert>
#include <cstring>
#include <utility>

KX_ValueType KX_AssetFieldValueType(KX_AssetFieldType type)
{
  switch (type) {
    case KX_AssetFieldType::Bool:
      return KX_VALUE_BOOL;
    case KX_AssetFieldType::Int:
      return KX_VALUE_INT;
    case KX_AssetFieldType::Float:
      return KX_VALUE_FLOAT;
    case KX_AssetFieldType::Vector:
      return KX_VALUE_VECTOR;
    case KX_AssetFieldType::ObjectRef:
      return KX_VALUE_OBJECT;
    case KX_AssetFieldType::AssetRef:
      return KX_VALUE_ASSET;
    case KX_AssetFieldType::String:
      return KX_VALUE_STRING;
    case KX_AssetFieldType::List:
      return KX_VALUE_LIST;
  }
  return KX_VALUE_NONE;
}

const char *KX_AssetFieldTypeName(KX_AssetFieldType type)
{
  switch (type) {
    case KX_AssetFieldType::Bool:
      return "Bool";
    case KX_AssetFieldType::Int:
      return "Int";
    case KX_AssetFieldType::Float:
      return "Float";
    case KX_AssetFieldType::Vector:
      return "Vector";
    case KX_AssetFieldType::ObjectRef:
      return "Object";
    case KX_AssetFieldType::AssetRef:
      return "Asset";
    case KX_AssetFieldType::String:
      return "String";
    case KX_AssetFieldType::List:
      return "List";
  }
  return "None";
}

KX_RuntimeValue KX_MakeZeroValue(KX_ValueType type)
{
  switch (type) {
    case KX_VALUE_BOOL:
      return KX_RuntimeValue::Bool(false);
    case KX_VALUE_INT:
      return KX_RuntimeValue::Int(0);
    case KX_VALUE_FLOAT:
      return KX_RuntimeValue::Float(0.0f);
    case KX_VALUE_VECTOR:
      return KX_RuntimeValue::Vector(0.0f, 0.0f, 0.0f);
    case KX_VALUE_OBJECT:
      return KX_RuntimeValue::Object(nullptr);
    case KX_VALUE_ASSET:
      return KX_RuntimeValue::Asset(nullptr);
    case KX_VALUE_STRING:
      return KX_RuntimeValue::String(nullptr);
    case KX_VALUE_LIST:
      return KX_RuntimeValue::List(nullptr);
    default:
      return KX_RuntimeValue::None();
  }
}

KX_GameDataAsset::KX_GameDataAsset(std::string asset_id_value, std::string asset_type_value)
    : asset_id(std::move(asset_id_value)), asset_type(std::move(asset_type_value))
{
}

KX_GameDataAsset::~KX_GameDataAsset()
{
  for (uint32_t i = 0; i < fields_.size(); ++i) {
    if (fields_[i].type != KX_AssetFieldType::AssetRef) {
      continue;
    }

    KX_GameDataAsset *data_ref = reinterpret_cast<KX_GameDataAsset *>(Slot(i)->data.ptr);
    if (data_ref != nullptr && data_ref->refcount > 0) {
      --data_ref->refcount;
    }

    KX_GameDataAsset *default_ref = reinterpret_cast<KX_GameDataAsset *>(
        fields_[i].default_value.data.ptr);
    if (default_ref != nullptr && default_ref->refcount > 0) {
      --default_ref->refcount;
    }
  }
}

bool KX_GameDataAsset::AddField(const std::string &name,
                                KX_AssetFieldType type,
                                const KX_RuntimeValue &default_value,
                                std::string *error)
{
  if (name.empty()) {
    if (error != nullptr) {
      *error = "Asset field name must not be empty";
    }
    return false;
  }
  if (FindField(name) >= 0) {
    if (error != nullptr) {
      *error = "Duplicate asset field '" + name + "'";
    }
    return false;
  }

  const KX_ValueType expected = KX_AssetFieldValueType(type);
  if (default_value.type != KX_VALUE_NONE && default_value.type != expected) {
    if (error != nullptr) {
      *error = "Default value of field '" + name + "' is " +
               KX_ValueTypeName(default_value.type) + " but the field type is " +
               KX_AssetFieldTypeName(type);
    }
    return false;
  }

  KX_AssetFieldDesc desc;
  desc.name = name;
  desc.type = type;
  desc.offset = static_cast<uint32_t>(fields_.size() * sizeof(KX_RuntimeValue));
  desc.default_value = default_value.type == KX_VALUE_NONE
                           ? KX_MakeZeroValue(expected)
                           : default_value;

  /* The description owns one reference to a default AssetRef. The data slot
   * owns another one, so both are released by the destructor. */
  if (type == KX_AssetFieldType::AssetRef) {
    KX_GameDataAsset *default_asset = reinterpret_cast<KX_GameDataAsset *>(
        desc.default_value.data.ptr);
    if (default_asset != nullptr) {
      ++default_asset->refcount;
    }
  }

  fields_.push_back(std::move(desc));
  data_.resize(fields_.size() * sizeof(KX_RuntimeValue), 0);
  string_storage_.resize(fields_.size());
  list_storage_.resize(fields_.size());
  default_strings_.resize(fields_.size());
  default_lists_.resize(fields_.size());

  const uint32_t index = static_cast<uint32_t>(fields_.size() - 1);
  KX_RuntimeValue &slot = *Slot(index);
  slot = fields_[index].default_value;

  if (type == KX_AssetFieldType::String) {
    string_storage_[index] = std::make_unique<std::string>();
    default_strings_[index] = std::make_unique<std::string>();
    if (default_value.type == KX_VALUE_STRING && default_value.AsString() != nullptr) {
      *default_strings_[index] = default_value.AsString();
    }
    *string_storage_[index] = *default_strings_[index];
    slot.type = KX_VALUE_STRING;
    slot.data.ptr = const_cast<char *>(string_storage_[index]->c_str());
    /* Keep the description free of a pointer into the owned buffer, the owner
     * copies are restored by ResetToDefaults instead. */
    fields_[index].default_value = KX_RuntimeValue::String(nullptr);
  }
  else if (type == KX_AssetFieldType::List) {
    list_storage_[index] = std::make_unique<std::vector<KX_RuntimeValue>>();
    default_lists_[index] = std::make_unique<std::vector<KX_RuntimeValue>>();
    if (default_value.type == KX_VALUE_LIST) {
      const std::vector<KX_RuntimeValue> *source =
          static_cast<const std::vector<KX_RuntimeValue> *>(default_value.AsList());
      if (source != nullptr) {
        *default_lists_[index] = *source;
      }
    }
    *list_storage_[index] = *default_lists_[index];
    slot.type = KX_VALUE_LIST;
    slot.data.ptr = list_storage_[index].get();
    fields_[index].default_value = KX_RuntimeValue::List(nullptr);
  }
  else if (type == KX_AssetFieldType::AssetRef) {
    KX_GameDataAsset *default_asset = reinterpret_cast<KX_GameDataAsset *>(slot.data.ptr);
    if (default_asset != nullptr) {
      ++default_asset->refcount;
    }
  }

  ++generation;
  return true;
}

int KX_GameDataAsset::FindField(const std::string &name) const
{
  for (uint32_t i = 0; i < fields_.size(); ++i) {
    if (fields_[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

KX_RuntimeValue KX_GameDataAsset::ReadField(uint32_t field_index) const
{
  assert(field_index < fields_.size());
  return *reinterpret_cast<const KX_RuntimeValue *>(data_.data() +
                                                    fields_[field_index].offset);
}

void KX_GameDataAsset::WriteField(uint32_t field_index, const KX_RuntimeValue &value)
{
  assert(field_index < fields_.size());
  const KX_AssetFieldDesc &desc = fields_[field_index];

  /* String and List copy into asset owned storage, so a pooled value produced
   * in this frame stays readable after #KX_ValuePool reclaims its generation. */
  if (desc.type == KX_AssetFieldType::String) {
    std::unique_ptr<std::string> &owned = string_storage_[field_index];
    if (!owned) {
      owned = std::make_unique<std::string>();
    }
    std::string copy;
    if (value.type == KX_VALUE_STRING && value.AsString() != nullptr) {
      copy.assign(value.AsString());
    }
    *owned = std::move(copy);

    KX_RuntimeValue &slot = *Slot(field_index);
    slot.type = KX_VALUE_STRING;
    slot.data.ptr = const_cast<char *>(owned->c_str());
    return;
  }

  if (desc.type == KX_AssetFieldType::List) {
    std::unique_ptr<std::vector<KX_RuntimeValue>> &owned = list_storage_[field_index];
    if (!owned) {
      owned = std::make_unique<std::vector<KX_RuntimeValue>>();
    }
    std::vector<KX_RuntimeValue> copy;
    if (value.type == KX_VALUE_LIST) {
      const std::vector<KX_RuntimeValue> *source =
          static_cast<const std::vector<KX_RuntimeValue> *>(value.AsList());
      if (source != nullptr) {
        copy = *source;
      }
    }
    *owned = std::move(copy);

    KX_RuntimeValue &slot = *Slot(field_index);
    slot.type = KX_VALUE_LIST;
    slot.data.ptr = owned.get();
    return;
  }

  const KX_ValueType expected = KX_AssetFieldValueType(desc.type);

  KX_RuntimeValue converted;
  if (expected == KX_VALUE_ASSET || expected == KX_VALUE_OBJECT) {
    converted = value.type == expected ? value : KX_MakeZeroValue(expected);
  }
  else {
    converted = value.type == KX_VALUE_NONE ? KX_MakeZeroValue(expected)
                                            : value.ConvertTo(expected);
  }

  KX_RuntimeValue &slot = *Slot(field_index);
  if (desc.type == KX_AssetFieldType::AssetRef) {
    KX_GameDataAsset *old_asset = reinterpret_cast<KX_GameDataAsset *>(slot.data.ptr);
    KX_GameDataAsset *new_asset = reinterpret_cast<KX_GameDataAsset *>(converted.data.ptr);
    if (old_asset != new_asset) {
      if (old_asset != nullptr && old_asset->refcount > 0) {
        --old_asset->refcount;
      }
      if (new_asset != nullptr) {
        ++new_asset->refcount;
      }
    }
  }
  slot = converted;
}

void KX_GameDataAsset::ResetToDefaults()
{
  for (uint32_t i = 0; i < fields_.size(); ++i) {
    if (fields_[i].type == KX_AssetFieldType::String) {
      KX_RuntimeValue value = KX_RuntimeValue::String(
          default_strings_[i] != nullptr ? default_strings_[i]->c_str() : nullptr);
      WriteField(i, value);
      continue;
    }
    if (fields_[i].type == KX_AssetFieldType::List) {
      KX_RuntimeValue value = KX_RuntimeValue::List(default_lists_[i].get());
      WriteField(i, value);
      continue;
    }
    WriteField(i, fields_[i].default_value);
  }
}

size_t KX_GameDataAsset::FieldCount() const
{
  return fields_.size();
}

const KX_AssetFieldDesc &KX_GameDataAsset::Field(uint32_t field_index) const
{
  assert(field_index < fields_.size());
  return fields_[field_index];
}

KX_RuntimeValue *KX_GameDataAsset::Slot(uint32_t field_index)
{
  return reinterpret_cast<KX_RuntimeValue *>(data_.data() +
                                             fields_[field_index].offset);
}
