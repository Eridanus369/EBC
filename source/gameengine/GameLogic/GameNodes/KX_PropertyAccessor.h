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

/** \file KX_PropertyAccessor.h
 *  \ingroup gamelogic
 *
 *  Generation counted cache for game object properties.
 *
 *  ObjectGet/ObjectSet address a property by name, so a naive implementation
 *  would run a string hash on every frame. #KX_PropertyAccessor keeps the
 *  resolved type of the last lookup plus the object property generation and
 *  only re-resolves when the property table changed shape. Property values
 *  written in place keep their pointer, so the cached type stays valid across
 *  ordinary writes.
 *
 *  The engine storage (#EXP_Value) is not visible here: nodes talk to a
 *  #KX_PropertyBackend. This keeps the runtime nodes compilable without the
 *  Blender expression headers and makes the cache testable with a fake backend.
 */

#pragma once

#include <cstdint>
#include <string>

#include "KX_RuntimeValue.h"

class KX_ValuePool;

/** Storage interface implemented by the engine and by test doubles. */
class KX_PropertyBackend {
 public:
  virtual ~KX_PropertyBackend() = default;

  /** Monotonic counter bumped whenever the property table of #object changes shape. */
  virtual uint32_t Generation(void *object) const = 0;
  /** Type of the named property, KX_VALUE_NONE when it does not exist. */
  virtual KX_ValueType Type(void *object, const std::string &name) const = 0;
  /** Current value of an existing property; never called for a missing property.
   *  String properties need #pool to copy their text into stable storage. */
  virtual KX_RuntimeValue Read(void *object,
                               const std::string &name,
                               KX_ValuePool *pool = nullptr) const = 0;
  /** Write an existing property, false when it disappeared in the meantime. */
  virtual bool Write(void *object,
                     const std::string &name,
                     const KX_RuntimeValue &value) = 0;
  /** Create the property with #value as its initial value. */
  virtual bool Create(void *object,
                      const std::string &name,
                      KX_ValueType type,
                      const KX_RuntimeValue &value) = 0;
};

/** Backend used when a context has none; the engine installs the EXP_Value one. */
KX_PropertyBackend *KX_DefaultPropertyBackend();
void KX_SetDefaultPropertyBackend(KX_PropertyBackend *backend);

#ifdef KX_GAME_NODES_ENGINE
/**
 * Engine only: install the EXP_Value backend as the default.
 *
 * Called by #RegisterBuiltinGameNodes. It is an explicit call rather than a
 * static initializer because ge_logic_bricks is a static library and an
 * unreferenced object file would never run its initializer.
 */
void KX_RegisterEnginePropertyBackend();
#endif

/**
 * Compiled cache slot, stored in the node state block.
 *
 * #valid is false in the zeroed state block, #object changes when the node is
 * pointed at another object, #generation detects structural changes and #type
 * is the property type seen at the last resolve.
 */
struct KX_PropertyCacheEntry {
  void *object = nullptr;
  uint32_t generation = 0;
  KX_ValueType type = KX_VALUE_NONE;
  bool valid = false;
};

class KX_PropertyAccessor {
 public:
  enum class WriteResult : uint8_t {
    /** Existing property updated in place. */
    Written = 0,
    /** Missing property created on demand (config "on_missing": "create"). */
    Created,
    /** Property missing and creation disabled. */
    Missing,
    /** No backend or no object, nothing was touched. */
    NoBackend,
  };

  explicit KX_PropertyAccessor(KX_PropertyBackend *backend = nullptr);

  void SetBackend(KX_PropertyBackend *backend);
  KX_PropertyBackend *Backend() const;

  /**
   * Refresh #entry and return the property type, KX_VALUE_NONE when missing.
   *
   * A cache hit is one generation comparison; a miss costs one property table
   * lookup plus the type query.
   */
  KX_ValueType Resolve(void *object, const std::string &name, KX_PropertyCacheEntry *entry);

  /**
   * Read a property into #out.
   *
   * Missing objects and missing properties yield a zero value of
   * #fallback_type and return false. Successful reads are converted to
   * #fallback_type so the output port always carries the declared type.
   */
  bool Read(void *object,
            const std::string &name,
            KX_PropertyCacheEntry *entry,
            KX_ValueType fallback_type,
            KX_RuntimeValue *out);

  /**
   * Write a property.
   *
   * When it is missing, #create_missing selects between creating it (#Created)
   * and skipping the write (#Missing). Existing properties are updated in place
   * so cached pointers and generations stay valid.
   */
  WriteResult Write(void *object,
                    const std::string &name,
                    const KX_RuntimeValue &value,
                    bool create_missing,
                    KX_PropertyCacheEntry *entry);

  /** Drop a cache slot, for example when the node loses its object. */
  void Invalidate(KX_PropertyCacheEntry *entry) const;

 private:
  KX_PropertyBackend *EffectiveBackend() const;

  KX_PropertyBackend *backend_ = nullptr;
};
