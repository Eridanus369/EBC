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

/** \file KX_GameDataAsset.h
 *  \ingroup gamelogic
 *
 *  Shared game data asset, the P2 replacement for ad hoc object properties.
 *
 *  An asset is a field description table plus one compact data block. Field
 *  byte offsets are fixed when the field is added, so every read and write at
 *  runtime is a single offset access. Assets are owned by #KX_AssetRegistry and
 *  referenced by nodes through #KX_AssetHandle.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "KX_RuntimeData.h"
#include "KX_RuntimeValue.h"

/** Numeric handle of an asset inside #KX_AssetRegistry. Zero is never valid. */
using KX_AssetHandle = uint32_t;
static constexpr KX_AssetHandle KX_INVALID_ASSET_HANDLE = 0;

/** Field types supported by a data asset. */
enum class KX_AssetFieldType : uint8_t {
  Bool = 0,
  Int,
  Float,
  Vector,
  /** Borrowed KX_GameObject pointer, never owned by the asset. */
  ObjectRef,
  /** KX_GameDataAsset pointer, reference counted on write. */
  AssetRef,
  /**
   * Owned text. The data slot points into the asset owned string, so a value
   * written in frame N stays readable in every later frame. Writing copies the
   * source text, the pool which produced it keeps no ownership here.
   */
  String,
  /**
   * Owned list copy. Only the element array is copied; element values which are
   * themselves pooled strings keep the pool lifetime rule.
   */
  List,
};

/** Runtime value type corresponding to an asset field type. */
KX_ValueType KX_AssetFieldValueType(KX_AssetFieldType type);
/** Human readable field type name, used by compiler diagnostics. */
const char *KX_AssetFieldTypeName(KX_AssetFieldType type);

/** A zero value of the requested runtime type. */
KX_RuntimeValue KX_MakeZeroValue(KX_ValueType type);

struct KX_AssetFieldDesc {
  std::string name;
  KX_AssetFieldType type = KX_AssetFieldType::Float;
  /** Byte offset inside the asset data block. Fixed at definition time. */
  uint32_t offset = KX_INVALID_OFFSET;
  /** Restored by #KX_GameDataAsset::ResetToDefaults. */
  KX_RuntimeValue default_value;
};

/** Resolved asset reference recorded by a node after the ResolveAssets pass. */
struct KX_ResolvedAssetBinding {
  std::string asset_id;
  std::string asset_type;
  KX_AssetHandle handle = KX_INVALID_ASSET_HANDLE;
  /** Registry entry generation seen at compile time. */
  uint32_t generation = 0;
  std::string field_name;
};

class KX_GameDataAsset {
 public:
  KX_GameDataAsset(std::string asset_id, std::string asset_type);
  ~KX_GameDataAsset();

  KX_GameDataAsset(const KX_GameDataAsset &) = delete;
  KX_GameDataAsset &operator=(const KX_GameDataAsset &) = delete;

  /**
   * Append a field and fix its offset.
   *
   * Returns false and fills #error for duplicate names or a default value whose
   * type does not match the field type. Field structure must not change after a
   * graph referenced the asset; P3 adds dynamic fields.
   */
  bool AddField(const std::string &name,
                KX_AssetFieldType type,
                const KX_RuntimeValue &default_value,
                std::string *error);

  /** Cold path lookup used by the compiler. Returns -1 when not found. */
  int FindField(const std::string &name) const;

  /** Hot path read, one offset access. */
  KX_RuntimeValue ReadField(uint32_t field_index) const;
  /** Hot path write, converted to the field type before storing. */
  void WriteField(uint32_t field_index, const KX_RuntimeValue &value);

  void ResetToDefaults();

  size_t FieldCount() const;
  const KX_AssetFieldDesc &Field(uint32_t field_index) const;

  /** Stable string id, for example "PlayerStats". */
  std::string asset_id;
  /** Type tag, for example "CharacterStats". */
  std::string asset_type;
  /** Registry ownership plus one per node reference. */
  uint32_t refcount = 1;
  /** Increased whenever the field structure changes. */
  uint32_t generation = 1;

 private:
  KX_RuntimeValue *Slot(uint32_t field_index);

  std::vector<KX_AssetFieldDesc> fields_;
  /**
   * Compact field block. Every field occupies one #KX_RuntimeValue, so the
   * byte offset is always a multiple of its size and the vector allocation is
   * suitably aligned.
   */
  std::vector<uint8_t> data_;

  /** Owning storage behind the String/List data slots. */
  std::vector<std::unique_ptr<std::string>> string_storage_;
  std::vector<std::unique_ptr<std::vector<KX_RuntimeValue>>> list_storage_;
  /** Pristine copies restored by ResetToDefaults. */
  std::vector<std::unique_ptr<std::string>> default_strings_;
  std::vector<std::unique_ptr<std::vector<KX_RuntimeValue>>> default_lists_;
};
