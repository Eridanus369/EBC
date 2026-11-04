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

/** \file KX_PropertyAccessorEngine.cpp
 *  \ingroup gamelogic
 *
 *  #EXP_Value backed property storage for the ObjectGet/ObjectSet nodes.
 *
 *  Game properties live in EXP_Value::m_properties, a name to pointer map.
 *  Writing an existing property goes through SetValue(), which converts in
 *  place and keeps the pointer stable, so the accessor cache survives every
 *  ordinary write. Structural changes bump the generation counted in
 *  EXP_Value::m_property_generation.
 */

#include "KX_PropertyAccessor.h"

#include "EXP_BoolValue.h"
#include "EXP_FloatValue.h"
#include "EXP_IntValue.h"
#include "EXP_StringValue.h"
#include "EXP_Vector3Value.h"

#include "KX_ValuePool.h"

namespace {

KX_ValueType MapEngineType(EXP_Value *value)
{
  switch (value->GetValueType()) {
    case VALUE_BOOL_TYPE:
      return KX_VALUE_BOOL;
    case VALUE_INT_TYPE:
      return KX_VALUE_INT;
    case VALUE_FLOAT_TYPE:
      return KX_VALUE_FLOAT;
    case VALUE_VECTOR3_TYPE:
      return KX_VALUE_VECTOR;
    case VALUE_STRING_TYPE:
      return KX_VALUE_STRING;
    default:
      return KX_VALUE_NONE;
  }
}

/** Build an EXP_Value of #type holding #value, nullptr for unsupported types. */
EXP_Value *MakeEngineValue(KX_ValueType type, const KX_RuntimeValue &value, const std::string &name)
{
  switch (type) {
    case KX_VALUE_BOOL:
      return new EXP_BoolValue(value.AsBool());
    case KX_VALUE_INT:
      return new EXP_IntValue(static_cast<cInt>(value.AsInt()));
    case KX_VALUE_FLOAT:
      return new EXP_FloatValue(value.AsFloat());
    case KX_VALUE_VECTOR: {
      float vector[3];
      value.VectorTo(vector);
      return new EXP_Vector3Value(vector[0], vector[1], vector[2]);
    }
    case KX_VALUE_STRING: {
      /* Only a String value may write to a String property; the node reports
       * a diagnostic for the nullptr case instead of storing an empty string. */
      const char *text = value.AsString();
      if (text == nullptr) {
        return nullptr;
      }
      return new EXP_StringValue(std::string(text), name);
    }
    default:
      return nullptr;
  }
}

class KX_EnginePropertyBackend : public KX_PropertyBackend {
 public:
  static KX_EnginePropertyBackend &Get()
  {
    static KX_EnginePropertyBackend backend;
    return backend;
  }

  uint32_t Generation(void *object) const override
  {
    return static_cast<EXP_Value *>(object)->GetPropertyGeneration();
  }

  KX_ValueType Type(void *object, const std::string &name) const override
  {
    EXP_Value *property = static_cast<EXP_Value *>(object)->GetProperty(name);
    return property != nullptr ? MapEngineType(property) : KX_VALUE_NONE;
  }

  KX_RuntimeValue Read(void *object,
                       const std::string &name,
                       KX_ValuePool *pool = nullptr) const override
  {
    EXP_Value *property = static_cast<EXP_Value *>(object)->GetProperty(name);
    if (property == nullptr) {
      return KX_RuntimeValue::None();
    }

    switch (MapEngineType(property)) {
      case KX_VALUE_BOOL:
        return KX_RuntimeValue::Bool(property->GetNumber() != 0.0);
      case KX_VALUE_INT:
        return KX_RuntimeValue::Int(static_cast<int>(property->GetNumber()));
      case KX_VALUE_FLOAT:
        return KX_RuntimeValue::Float(static_cast<float>(property->GetNumber()));
      case KX_VALUE_VECTOR: {
        const MT_Vector3 &vector = static_cast<EXP_Vector3Value *>(property)->GetVector3();
        return KX_RuntimeValue::Vector(static_cast<float>(vector[0]),
                                       static_cast<float>(vector[1]),
                                       static_cast<float>(vector[2]));
      }
      case KX_VALUE_STRING: {
        /* GetText() returns a copy, so intern it in the node pool to keep the
         * stored pointer valid after this call returns. */
        if (pool == nullptr) {
          return KX_RuntimeValue::String(nullptr);
        }
        return KX_RuntimeValue::String(pool->Intern(property->GetText()));
      }
      default:
        return KX_RuntimeValue::None();
    }
  }

  bool Write(void *object, const std::string &name, const KX_RuntimeValue &value) override
  {
    EXP_Value *property = static_cast<EXP_Value *>(object)->GetProperty(name);
    if (property == nullptr) {
      return false;
    }

    /* SetValue converts to the property own type and keeps the pointer, which
     * is what the generation cache relies on. */
    EXP_Value *new_value = MakeEngineValue(MapEngineType(property), value, name);
    if (new_value == nullptr) {
      return false;
    }
    property->SetValue(new_value);
    new_value->Release();
    return true;
  }

  bool Create(void *object,
              const std::string &name,
              KX_ValueType type,
              const KX_RuntimeValue &value) override
  {
    EXP_Value *target = static_cast<EXP_Value *>(object);
    if (target->GetProperty(name) != nullptr) {
      return Write(object, name, value);
    }

    EXP_Value *new_value = MakeEngineValue(type, value, name);
    if (new_value == nullptr) {
      return false;
    }
    target->SetProperty(name, new_value);
    new_value->Release();
    return true;
  }
};

}  // namespace

void KX_RegisterEnginePropertyBackend()
{
  KX_SetDefaultPropertyBackend(&KX_EnginePropertyBackend::Get());
}
