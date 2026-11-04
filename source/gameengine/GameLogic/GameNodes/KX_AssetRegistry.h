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

/** \file KX_AssetRegistry.h
 *  \ingroup gamelogic
 *
 *  Global registry of shared game data assets.
 *
 *  Handles are stable numeric slots. The registry owns one reference per
 *  registered asset; runtime nodes acquire one more while their compiled plan
 *  is alive. Replacing an asset with an identical string id only happens when
 *  no node references the old instance, which keeps handles safe.
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "KX_GameDataAsset.h"

class KX_AssetRegistry {
 public:
  struct LookupResult {
    KX_GameDataAsset *asset = nullptr;
    uint32_t generation = 0;
    KX_AssetHandle handle = KX_INVALID_ASSET_HANDLE;
  };

  static KX_AssetRegistry &Get();

  /**
   * Register an asset and return its stable handle.
   *
   * A duplicate id with no external references is replaced in place and the
   * entry generation is increased. A duplicate id that nodes still reference is
   * rejected with a warning and the existing handle is returned.
   */
  KX_AssetHandle Register(std::unique_ptr<KX_GameDataAsset> asset);

  /** Drop the registry reference. Existing node references keep the asset alive. */
  bool Unregister(KX_AssetHandle handle);

  /** Cold path lookup by string id, used by the compiler. */
  LookupResult Lookup(const std::string &asset_id) const;

  /** Hot path lookup by handle, one array access. */
  KX_GameDataAsset *Get(KX_AssetHandle handle) const;

  uint32_t EntryGeneration(KX_AssetHandle handle) const;

  /** Add one node reference. */
  void Acquire(KX_AssetHandle handle);
  /** Drop one node reference, destroying the asset when the count reaches zero. */
  void Release(KX_AssetHandle handle);

  size_t AssetCount() const;
  /** Append every registered asset handle, in registry slot order. */
  void CollectHandles(std::vector<KX_AssetHandle> &out) const;
  uint32_t WarningCount() const;
  const std::string &LastWarning() const;

  /** Remove every asset. Only meant for tests and scene teardown. */
  void Clear();

 private:
  struct Entry {
    std::unique_ptr<KX_GameDataAsset> asset;
    uint32_t generation = 1;
    bool registered = false;
  };

  KX_AssetRegistry();
  uint32_t FindSlot(const std::string &asset_id) const;

  std::vector<Entry> entries_;
  std::unordered_map<std::string, KX_AssetHandle> by_id_;
  uint32_t warning_count_ = 0;
  std::string last_warning_;
};
