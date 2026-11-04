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

/** \file KX_AssetRegistry.cpp
 *  \ingroup gamelogic
 */

#include "KX_AssetRegistry.h"

#include <utility>

KX_AssetRegistry::KX_AssetRegistry()
{
  /* Slot zero is the invalid handle, so handles are 1 based. */
  entries_.push_back(Entry());
}

KX_AssetRegistry &KX_AssetRegistry::Get()
{
  static KX_AssetRegistry instance;
  return instance;
}

uint32_t KX_AssetRegistry::FindSlot(const std::string &asset_id) const
{
  const auto it = by_id_.find(asset_id);
  return it == by_id_.end() ? KX_INVALID_ASSET_HANDLE : it->second;
}

KX_AssetHandle KX_AssetRegistry::Register(std::unique_ptr<KX_GameDataAsset> asset)
{
  if (!asset || asset->asset_id.empty()) {
    return KX_INVALID_ASSET_HANDLE;
  }

  const uint32_t existing = FindSlot(asset->asset_id);
  if (existing != KX_INVALID_ASSET_HANDLE) {
    Entry &entry = entries_[existing];
    if (entry.asset && entry.asset->refcount > 1) {
      ++warning_count_;
      last_warning_ = "Asset '" + asset->asset_id +
                      "' is still referenced by a compiled graph and cannot be replaced";
      return static_cast<KX_AssetHandle>(existing);
    }

    asset->refcount = 1;
    entry.asset = std::move(asset);
    ++entry.generation;
    entry.registered = true;
    return static_cast<KX_AssetHandle>(existing);
  }

  asset->refcount = 1;
  Entry entry;
  entry.asset = std::move(asset);
  entry.generation = 1;
  entry.registered = true;
  entries_.push_back(std::move(entry));

  const KX_AssetHandle handle = static_cast<KX_AssetHandle>(entries_.size() - 1);
  by_id_[entries_[handle].asset->asset_id] = handle;
  return handle;
}

bool KX_AssetRegistry::Unregister(KX_AssetHandle handle)
{
  if (handle == KX_INVALID_ASSET_HANDLE || handle >= entries_.size()) {
    return false;
  }

  Entry &entry = entries_[handle];
  if (!entry.registered || !entry.asset) {
    return false;
  }

  entry.registered = false;
  by_id_.erase(entry.asset->asset_id);
  if (entry.asset->refcount > 0) {
    --entry.asset->refcount;
  }
  if (entry.asset->refcount == 0) {
    entry.asset.reset();
  }
  return true;
}

KX_AssetRegistry::LookupResult KX_AssetRegistry::Lookup(const std::string &asset_id) const
{
  LookupResult result;
  const uint32_t slot = FindSlot(asset_id);
  if (slot == KX_INVALID_ASSET_HANDLE) {
    return result;
  }

  const Entry &entry = entries_[slot];
  if (!entry.registered || !entry.asset) {
    return result;
  }

  result.asset = entry.asset.get();
  result.generation = entry.generation;
  result.handle = static_cast<KX_AssetHandle>(slot);
  return result;
}

KX_GameDataAsset *KX_AssetRegistry::Get(KX_AssetHandle handle) const
{
  if (handle == KX_INVALID_ASSET_HANDLE || handle >= entries_.size()) {
    return nullptr;
  }
  const Entry &entry = entries_[handle];
  return entry.registered ? entry.asset.get() : nullptr;
}

uint32_t KX_AssetRegistry::EntryGeneration(KX_AssetHandle handle) const
{
  if (handle == KX_INVALID_ASSET_HANDLE || handle >= entries_.size()) {
    return 0;
  }
  return entries_[handle].generation;
}

void KX_AssetRegistry::Acquire(KX_AssetHandle handle)
{
  KX_GameDataAsset *asset = Get(handle);
  if (asset != nullptr) {
    ++asset->refcount;
  }
}

void KX_AssetRegistry::Release(KX_AssetHandle handle)
{
  if (handle == KX_INVALID_ASSET_HANDLE || handle >= entries_.size()) {
    return;
  }

  Entry &entry = entries_[handle];
  if (!entry.asset) {
    return;
  }

  if (entry.asset->refcount > 0) {
    --entry.asset->refcount;
  }
  if (entry.asset->refcount == 0) {
    entry.asset.reset();
  }
}

size_t KX_AssetRegistry::AssetCount() const
{
  size_t count = 0;
  for (const Entry &entry : entries_) {
    if (entry.registered && entry.asset) {
      ++count;
    }
  }
  return count;
}

void KX_AssetRegistry::CollectHandles(std::vector<KX_AssetHandle> &out) const
{
  out.clear();
  for (size_t index = 1; index < entries_.size(); ++index) {
    const Entry &entry = entries_[index];
    if (entry.registered && entry.asset) {
      out.push_back(static_cast<KX_AssetHandle>(index));
    }
  }
}

uint32_t KX_AssetRegistry::WarningCount() const
{
  return warning_count_;
}

const std::string &KX_AssetRegistry::LastWarning() const
{
  return last_warning_;
}

void KX_AssetRegistry::Clear()
{
  entries_.clear();
  by_id_.clear();
  entries_.push_back(Entry());
}
