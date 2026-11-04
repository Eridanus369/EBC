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

/** \file KX_PropertyAccessor.cpp
 *  \ingroup gamelogic
 */

#include "KX_PropertyAccessor.h"

#include "KX_GameDataAsset.h"

namespace {

KX_PropertyBackend *&DefaultBackendSlot()
{
  static KX_PropertyBackend *backend = nullptr;
  return backend;
}

}  // namespace

KX_PropertyBackend *KX_DefaultPropertyBackend()
{
  return DefaultBackendSlot();
}

void KX_SetDefaultPropertyBackend(KX_PropertyBackend *backend)
{
  DefaultBackendSlot() = backend;
}

KX_PropertyAccessor::KX_PropertyAccessor(KX_PropertyBackend *backend) : backend_(backend)
{
}

void KX_PropertyAccessor::SetBackend(KX_PropertyBackend *backend)
{
  backend_ = backend;
}

KX_PropertyBackend *KX_PropertyAccessor::Backend() const
{
  return EffectiveBackend();
}

KX_PropertyBackend *KX_PropertyAccessor::EffectiveBackend() const
{
  return backend_ != nullptr ? backend_ : KX_DefaultPropertyBackend();
}

KX_ValueType KX_PropertyAccessor::Resolve(void *object,
                                          const std::string &name,
                                          KX_PropertyCacheEntry *entry)
{
  KX_PropertyBackend *backend = EffectiveBackend();
  if (entry == nullptr || backend == nullptr || object == nullptr) {
    if (entry != nullptr) {
      entry->valid = false;
    }
    return KX_VALUE_NONE;
  }

  const uint32_t generation = backend->Generation(object);
  if (entry->valid && entry->object == object && entry->generation == generation) {
    return entry->type;
  }

  entry->object = object;
  entry->generation = generation;
  entry->type = backend->Type(object, name);
  entry->valid = true;
  return entry->type;
}

bool KX_PropertyAccessor::Read(void *object,
                               const std::string &name,
                               KX_PropertyCacheEntry *entry,
                               KX_ValueType fallback_type,
                               KX_RuntimeValue *out)
{
  if (out == nullptr) {
    return false;
  }

  if (Resolve(object, name, entry) == KX_VALUE_NONE) {
    *out = KX_MakeZeroValue(fallback_type);
    return false;
  }

  *out = EffectiveBackend()->Read(object, name);
  if (fallback_type != KX_VALUE_NONE) {
    *out = out->ConvertTo(fallback_type);
  }
  return true;
}

KX_PropertyAccessor::WriteResult KX_PropertyAccessor::Write(
    void *object,
    const std::string &name,
    const KX_RuntimeValue &value,
    bool create_missing,
    KX_PropertyCacheEntry *entry)
{
  KX_PropertyBackend *backend = EffectiveBackend();
  if (backend == nullptr || object == nullptr) {
    return WriteResult::NoBackend;
  }

  const KX_ValueType type = Resolve(object, name, entry);
  if (type != KX_VALUE_NONE) {
    if (!backend->Write(object, name, value)) {
      /* The property vanished between the resolve and the write. */
      Invalidate(entry);
      return WriteResult::Missing;
    }
    return WriteResult::Written;
  }

  if (!create_missing) {
    return WriteResult::Missing;
  }

  if (!backend->Create(object, name, value.type, value)) {
    Invalidate(entry);
    return WriteResult::Missing;
  }

  /* The create changed the property table, refresh the slot for the next call. */
  entry->valid = false;
  Resolve(object, name, entry);
  return WriteResult::Created;
}

void KX_PropertyAccessor::Invalidate(KX_PropertyCacheEntry *entry) const
{
  if (entry != nullptr) {
    entry->valid = false;
  }
}
