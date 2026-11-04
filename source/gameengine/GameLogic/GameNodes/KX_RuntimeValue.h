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

/** \file KX_RuntimeValue.h
 *  \ingroup gamelogic
 *
 *  Value stored inside a #KX_RuntimeData slot.
 *
 *  Ports of a compiled game node graph are plain #KX_RuntimeValue slots in a
 *  flat memory buffer. Nodes read and write them through the offsets resolved
 *  at compile time, no dictionaries or Python objects are involved.
 */

#pragma once

#include <cstdint>

/** Types supported by the native game node runtime. */
enum KX_ValueType : uint8_t {
  KX_VALUE_NONE = 0,
  KX_VALUE_BOOL,
  KX_VALUE_INT,
  KX_VALUE_FLOAT,
  /* Three component vector (x, y, z), stored in the first three vec slots. */
  KX_VALUE_VECTOR,
  /* KX_GameObject pointer, never owned by the value. */
  KX_VALUE_OBJECT,
  /* KX_GameDataAsset pointer, never owned by the value. */
  KX_VALUE_ASSET,
  /* RGBA color, four components. */
  KX_VALUE_COLOR,
  /* Interned C string owned by #KX_ValuePool, valid for the current frame. */
  KX_VALUE_STRING,
  /* #KX_ValueList pointer owned by #KX_ValuePool, valid for the current frame. */
  KX_VALUE_LIST,
  /* Generic datablock reference (sound, image, font, material, ...). */
  KX_VALUE_DATABLOCK,
};

/** Human readable type name, used by compiler diagnostics. */
inline const char *KX_ValueTypeName(KX_ValueType type)
{
  switch (type) {
    case KX_VALUE_BOOL:
      return "Bool";
    case KX_VALUE_INT:
      return "Int";
    case KX_VALUE_FLOAT:
      return "Float";
    case KX_VALUE_VECTOR:
      return "Vector";
    case KX_VALUE_OBJECT:
      return "Object";
    case KX_VALUE_ASSET:
      return "Asset";
    case KX_VALUE_COLOR:
      return "Color";
    case KX_VALUE_STRING:
      return "String";
    case KX_VALUE_LIST:
      return "List";
    case KX_VALUE_DATABLOCK:
      return "Datablock";
    default:
      return "None";
  }
}

/**
 * A single typed value stored in the runtime data buffer.
 *
 * The struct is a POD so it can live inside a raw memory block. All slots are
 * zero initialized, which makes the zeroed state the "empty" value.
 */
struct KX_RuntimeValue {
  KX_ValueType type = KX_VALUE_NONE;

  union {
    bool b;
    int i;
    float f;
    float vec[4];
    void *ptr;
  } data = {};

  static KX_RuntimeValue None()
  {
    return KX_RuntimeValue();
  }

  static KX_RuntimeValue Bool(bool value)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_BOOL;
    result.data.b = value;
    return result;
  }

  static KX_RuntimeValue Int(int value)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_INT;
    result.data.i = value;
    return result;
  }

  static KX_RuntimeValue Float(float value)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_FLOAT;
    result.data.f = value;
    return result;
  }

  static KX_RuntimeValue Vector(float x, float y, float z)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_VECTOR;
    result.data.vec[0] = x;
    result.data.vec[1] = y;
    result.data.vec[2] = z;
    return result;
  }

  static KX_RuntimeValue Object(void *object)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_OBJECT;
    result.data.ptr = object;
    return result;
  }

  static KX_RuntimeValue Asset(void *asset)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_ASSET;
    result.data.ptr = asset;
    return result;
  }

  static KX_RuntimeValue Color(float r, float g, float b, float a)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_COLOR;
    result.data.vec[0] = r;
    result.data.vec[1] = g;
    result.data.vec[2] = b;
    result.data.vec[3] = a;
    return result;
  }

  /** @param text must stay alive for as long as the value is read. */
  static KX_RuntimeValue String(const char *text)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_STRING;
    result.data.ptr = const_cast<char *>(text);
    return result;
  }

  /** @param list #KX_ValueList owned by the execution context pool. */
  static KX_RuntimeValue List(void *list)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_LIST;
    result.data.ptr = list;
    return result;
  }

  static KX_RuntimeValue DataBlock(void *datablock)
  {
    KX_RuntimeValue result;
    result.type = KX_VALUE_DATABLOCK;
    result.data.ptr = datablock;
    return result;
  }

  bool IsNone() const
  {
    return type == KX_VALUE_NONE;
  }

  /** Read the value as a float, applying the implicit conversions of the type system. */
  float AsFloat() const
  {
    switch (type) {
      case KX_VALUE_BOOL:
        return data.b ? 1.0f : 0.0f;
      case KX_VALUE_INT:
        return float(data.i);
      case KX_VALUE_FLOAT:
        return data.f;
      case KX_VALUE_VECTOR:
        return data.vec[0];
      default:
        return 0.0f;
    }
  }

  /** Read the value as an int, applying the implicit conversions of the type system. */
  int AsInt() const
  {
    switch (type) {
      case KX_VALUE_BOOL:
        return data.b ? 1 : 0;
      case KX_VALUE_INT:
        return data.i;
      case KX_VALUE_FLOAT:
        return int(data.f);
      case KX_VALUE_VECTOR:
        return int(data.vec[0]);
      default:
        return 0;
    }
  }

  /** Read the value as a bool. Used for trigger sockets and boolean inputs. */
  bool AsBool() const
  {
    switch (type) {
      case KX_VALUE_BOOL:
        return data.b;
      case KX_VALUE_INT:
        return data.i != 0;
      case KX_VALUE_FLOAT:
        return data.f != 0.0f;
      case KX_VALUE_VECTOR:
        return data.vec[0] != 0.0f || data.vec[1] != 0.0f || data.vec[2] != 0.0f;
      case KX_VALUE_COLOR:
        return data.vec[0] != 0.0f || data.vec[1] != 0.0f || data.vec[2] != 0.0f ||
               data.vec[3] != 0.0f;
      case KX_VALUE_STRING:
        return data.ptr != nullptr;
      case KX_VALUE_LIST:
        return data.ptr != nullptr;
      default:
        return false;
    }
  }

  /** Copy the value into a three component vector. Non vector values map to (x, 0, 0). */
  void VectorTo(float out[3]) const
  {
    if (type == KX_VALUE_VECTOR || type == KX_VALUE_COLOR) {
      out[0] = data.vec[0];
      out[1] = data.vec[1];
      out[2] = data.vec[2];
    }
    else {
      out[0] = AsFloat();
      out[1] = 0.0f;
      out[2] = 0.0f;
    }
  }

  /** Copy the value into a four component color. Missing components use the identity default. */
  void ColorTo(float out[4]) const
  {
    if (type == KX_VALUE_COLOR) {
      out[0] = data.vec[0];
      out[1] = data.vec[1];
      out[2] = data.vec[2];
      out[3] = data.vec[3];
      return;
    }
    float vector[3];
    VectorTo(vector);
    out[0] = vector[0];
    out[1] = vector[1];
    out[2] = vector[2];
    out[3] = 1.0f;
  }

  /** Interned string for #KX_VALUE_STRING, nullptr for every other type. */
  const char *AsString() const
  {
    return type == KX_VALUE_STRING ? static_cast<const char *>(data.ptr) : nullptr;
  }

  /** #KX_ValueList pointer for #KX_VALUE_LIST, nullptr for every other type. */
  void *AsList() const
  {
    return type == KX_VALUE_LIST ? data.ptr : nullptr;
  }

  /** Object or asset pointer stored in the value, nullptr for every other type. */
  void *AsPointer() const
  {
    if (type == KX_VALUE_OBJECT || type == KX_VALUE_ASSET || type == KX_VALUE_DATABLOCK ||
        type == KX_VALUE_LIST)
    {
      return data.ptr;
    }
    return nullptr;
  }

  /**
   * Convert this value to #target using the implicit conversion rules.
   *
   * Numeric types can always widen to Bool/Int/Float, Vector only converts to
   * scalar through its X component and pointer types are preserved. The graph
   * compiler only marks conversions it has validated as lossless.
   */
  KX_RuntimeValue ConvertTo(KX_ValueType target) const
  {
    if (target == KX_VALUE_NONE || type == target) {
      return *this;
    }
    switch (target) {
      case KX_VALUE_BOOL:
        return Bool(AsBool());
      case KX_VALUE_INT:
        return Int(AsInt());
      case KX_VALUE_FLOAT:
        return Float(AsFloat());
      case KX_VALUE_VECTOR: {
        float vector[3];
        VectorTo(vector);
        return Vector(vector[0], vector[1], vector[2]);
      }
      default:
        return *this;
    }
  }
};
