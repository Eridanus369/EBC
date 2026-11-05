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

/** \file KX_FunctionNodesB2.cpp
 *  \ingroup gamelogic
 *
 *  B2 node family: math/vector, colour, string and list nodes.
 *
 *  Strings and lists reuse the per frame #KX_ValuePool: strings are interned,
 *  lists are recycled vectors of #KX_RuntimeValue. There is no SSO/refcount
 *  layer because values never own heap memory, and no per port destructor in
 *  #KX_RuntimeData because the pool releases everything in BeginFrame.
 *
 *  Formula and StringFormat resolve their ports from the IR (`ports_from_ir`),
 *  so the editor can generate one input port per variable/placeholder.
 */

#include "KX_FunctionNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "KX_NodeRegistry.h"
#include "KX_RuntimeValue.h"
#include "KX_ValuePool.h"

#define FP(name, type) KX_FuncPort {name, type, false}
#define TP(name) KX_FuncPort {name, KX_VALUE_NONE, true}

namespace {

/* ------------------------------------------------------------------------- */
/* Shared helpers                                                            */
/* ------------------------------------------------------------------------- */

std::string B2InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const KX_RuntimeValue value = node.In(ctx, index);
  const char *text = value.AsString();
  return text != nullptr ? std::string(text) : std::string();
}

KX_ValueList *B2InList(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  return static_cast<KX_ValueList *>(node.In(ctx, index).AsList());
}

float Dot3(const float a[3], const float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Length3(const float a[3])
{
  return std::sqrt(Dot3(a, a));
}

void Cross3(const float a[3], const float b[3], float out[3])
{
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

bool Normalize3(float v[3])
{
  const float length = Length3(v);
  if (length <= 1e-8f) {
    return false;
  }
  v[0] /= length;
  v[1] /= length;
  v[2] /= length;
  return true;
}

void RotateAroundAxis(const float v[3], const float axis[3], float angle, float out[3])
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  const float dot = Dot3(axis, v);
  float cross[3];
  Cross3(axis, v, cross);
  for (uint32_t i = 0; i < 3; ++i) {
    out[i] = v[i] * c + cross[i] * s + axis[i] * dot * (1.0f - c);
  }
}

uint32_t HashToUnit(uint32_t seed)
{
  seed ^= seed >> 16;
  seed *= 0x7feb352dU;
  seed ^= seed >> 15;
  seed *= 0x846ca68bU;
  seed ^= seed >> 16;
  return seed;
}

bool ValuesEqual(const KX_RuntimeValue &a, const KX_RuntimeValue &b)
{
  if (a.type == KX_VALUE_STRING && b.type == KX_VALUE_STRING) {
    const char *sa = a.AsString();
    const char *sb = b.AsString();
    return sa != nullptr && sb != nullptr && std::strcmp(sa, sb) == 0;
  }
  if (a.type == KX_VALUE_VECTOR || a.type == KX_VALUE_COLOR) {
    float av[4];
    float bv[4];
    a.ColorTo(av);
    b.ColorTo(bv);
    for (uint32_t i = 0; i < 4; ++i) {
      if (av[i] != bv[i]) {
        return false;
      }
    }
    return true;
  }
  return a.AsFloat() == b.AsFloat();
}

/* ------------------------------------------------------------------------- */
/* B2.1 Math / vector                                                        */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kRandomValueInputs[] = {
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};
const KX_FuncPort kFloatResultOutput[] = {FP("Result", KX_VALUE_FLOAT)};

void FuncRandomValue(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float minimum = node.InFloat(ctx, 0);
  const float maximum = node.InFloat(ctx, 1);
  const uint32_t hash = HashToUnit(static_cast<uint32_t>(ctx.Frame()) ^
                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&node)));
  const float unit = static_cast<float>(hash & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
  node.OutFloat(ctx, 0, minimum + (maximum - minimum) * unit);
}

const KX_FuncPort kClampValueInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("Range", KX_VALUE_VECTOR),
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};

void FuncClampValue(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const float minimum = node.InFloat(ctx, 2);
  const float maximum = node.InFloat(ctx, 3);
  const float lo = std::min(minimum, maximum);
  const float hi = std::max(minimum, maximum);
  node.OutFloat(ctx, 0, std::max(lo, std::min(hi, value)));
}

const KX_FuncPort kWrapInputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    FP("Min", KX_VALUE_FLOAT),
    FP("Max", KX_VALUE_FLOAT),
};

void FuncWrap(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const float minimum = node.InFloat(ctx, 1);
  const float maximum = node.InFloat(ctx, 2);
  const float span = maximum - minimum;
  if (span <= 0.0f) {
    node.OutFloat(ctx, 0, minimum);
    return;
  }
  float wrapped = std::fmod(value - minimum, span);
  if (wrapped < 0.0f) {
    wrapped += span;
  }
  node.OutFloat(ctx, 0, minimum + wrapped);
}

const KX_FuncPort kValueInput[] = {FP("Value", KX_VALUE_FLOAT)};

void FuncRound(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string mode = node.ConfigString("mode");
  const float value = node.InFloat(ctx, 0);
  float result = value;
  if (mode == "floor") {
    result = std::floor(value);
  }
  else if (mode == "ceil") {
    result = std::ceil(value);
  }
  else if (mode == "trunc") {
    result = std::trunc(value);
  }
  else {
    result = std::round(value);
  }
  node.OutFloat(ctx, 0, result);
}

void FuncSign(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  node.OutFloat(ctx, 0, value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f));
}

const KX_FuncPort kVectorPairInputs[] = {
    FP("A", KX_VALUE_VECTOR),
    FP("B", KX_VALUE_VECTOR),
};
const KX_FuncPort kVectorResultOutput[] = {FP("Result", KX_VALUE_VECTOR)};

void FuncVectorReflect(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float incident[3];
  float normal[3];
  node.InVector(ctx, 0, incident);
  node.InVector(ctx, 1, normal);
  const float denominator = Dot3(normal, normal);
  float result[3] = {incident[0], incident[1], incident[2]};
  if (denominator > 1e-8f) {
    const float factor = 2.0f * Dot3(incident, normal) / denominator;
    for (uint32_t i = 0; i < 3; ++i) {
      result[i] = incident[i] - factor * normal[i];
    }
  }
  node.OutVector(ctx, 0, result[0], result[1], result[2]);
}

void FuncVectorProject(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float a[3];
  float b[3];
  node.InVector(ctx, 0, a);
  node.InVector(ctx, 1, b);
  const float denominator = Dot3(b, b);
  float result[3] = {0.0f, 0.0f, 0.0f};
  if (denominator > 1e-8f) {
    const float factor = Dot3(a, b) / denominator;
    for (uint32_t i = 0; i < 3; ++i) {
      result[i] = b[i] * factor;
    }
  }
  node.OutVector(ctx, 0, result[0], result[1], result[2]);
}

const KX_FuncPort kRotateTowardInputs[] = {
    FP("From", KX_VALUE_VECTOR),
    FP("To", KX_VALUE_VECTOR),
    FP("MaxRadians", KX_VALUE_FLOAT),
};

void FuncVectorRotateToward(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float from[3];
  float to[3];
  node.InVector(ctx, 0, from);
  node.InVector(ctx, 1, to);
  const float max_radians = node.InFloat(ctx, 2);

  float from_dir[3] = {from[0], from[1], from[2]};
  float to_dir[3] = {to[0], to[1], to[2]};
  if (!Normalize3(from_dir) || !Normalize3(to_dir)) {
    node.OutVector(ctx, 0, from[0], from[1], from[2]);
    return;
  }

  const float dot = std::max(-1.0f, std::min(1.0f, Dot3(from_dir, to_dir)));
  const float angle = std::acos(dot);
  if (angle <= max_radians) {
    node.OutVector(ctx, 0, to[0], to[1], to[2]);
    return;
  }

  float axis[3];
  Cross3(from_dir, to_dir, axis);
  if (Length3(axis) <= 1e-6f) {
    /* Opposed vectors have no unique axis, pick any perpendicular one. */
    axis[0] = 0.0f;
    axis[1] = 0.0f;
    axis[2] = 1.0f;
    if (std::fabs(from_dir[2]) > 0.9f) {
      axis[1] = 1.0f;
    }
  }
  Normalize3(axis);

  float rotated[3];
  RotateAroundAxis(from_dir, axis, max_radians, rotated);
  const float length = Length3(from);
  float result[3];
  for (uint32_t i = 0; i < 3; ++i) {
    result[i] = rotated[i] * length;
  }
  node.OutVector(ctx, 0, result[0], result[1], result[2]);
}

void FuncVectorAngle(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float a[3];
  float b[3];
  node.InVector(ctx, 0, a);
  node.InVector(ctx, 1, b);
  const float lengths = Length3(a) * Length3(b);
  if (lengths <= 1e-8f) {
    node.OutFloat(ctx, 0, 0.0f);
    return;
  }
  const float dot = std::max(-1.0f, std::min(1.0f, Dot3(a, b) / lengths));
  node.OutFloat(ctx, 0, std::acos(dot));
}

void FuncVectorDistance(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float a[3];
  float b[3];
  node.InVector(ctx, 0, a);
  node.InVector(ctx, 1, b);
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  node.OutFloat(ctx, 0, Length3(d));
}

#undef Dot3

/* ------------------------------------------------------------------------- */
/* B2.2 Colour                                                               */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kColorMixInputs[] = {
    FP("A", KX_VALUE_COLOR),
    FP("B", KX_VALUE_COLOR),
    FP("Factor", KX_VALUE_FLOAT),
};
const KX_FuncPort kColorResultOutput[] = {FP("Result", KX_VALUE_COLOR)};

void FuncColorMix(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float a[4];
  float b[4];
  node.In(ctx, 0).ColorTo(a);
  node.In(ctx, 1).ColorTo(b);
  const float factor = node.InFloat(ctx, 2);
  node.OutColor(ctx,
                0,
                a[0] + (b[0] - a[0]) * factor,
                a[1] + (b[1] - a[1]) * factor,
                a[2] + (b[2] - a[2]) * factor,
                a[3] + (b[3] - a[3]) * factor);
}

const KX_FuncPort kColorLerpInputs[] = {
    FP("A", KX_VALUE_COLOR),
    FP("B", KX_VALUE_COLOR),
    FP("T", KX_VALUE_FLOAT),
};

void FuncColorLerp(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float a[4];
  float b[4];
  node.In(ctx, 0).ColorTo(a);
  node.In(ctx, 1).ColorTo(b);
  const float t = std::max(0.0f, std::min(1.0f, node.InFloat(ctx, 2)));
  node.OutColor(ctx,
                0,
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t,
                a[3] + (b[3] - a[3]) * t);
}

const KX_FuncPort kColorInput[] = {FP("Color", KX_VALUE_COLOR)};
const KX_FuncPort kColorToRGBAOutputs[] = {
    FP("R", KX_VALUE_FLOAT),
    FP("G", KX_VALUE_FLOAT),
    FP("B", KX_VALUE_FLOAT),
    FP("A", KX_VALUE_FLOAT),
};

void FuncColorToRGBA(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float color[4];
  node.In(ctx, 0).ColorTo(color);
  node.OutFloat(ctx, 0, color[0]);
  node.OutFloat(ctx, 1, color[1]);
  node.OutFloat(ctx, 2, color[2]);
  node.OutFloat(ctx, 3, color[3]);
}

const KX_FuncPort kRGBAToColorInputs[] = {
    FP("R", KX_VALUE_FLOAT),
    FP("G", KX_VALUE_FLOAT),
    FP("B", KX_VALUE_FLOAT),
    FP("A", KX_VALUE_FLOAT),
};

void FuncRGBAToColor(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  node.OutColor(ctx, 0, node.InFloat(ctx, 0), node.InFloat(ctx, 1), node.InFloat(ctx, 2),
                node.InFloat(ctx, 3));
}

const KX_FuncPort kHSVInputs[] = {
    FP("H", KX_VALUE_FLOAT),
    FP("S", KX_VALUE_FLOAT),
    FP("V", KX_VALUE_FLOAT),
};

void FuncHSVToRGB(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float h = node.InFloat(ctx, 0);
  const float s = node.InFloat(ctx, 1);
  const float v = node.InFloat(ctx, 2);

  const float hue = (h - std::floor(h / 360.0f) * 360.0f) / 60.0f;
  const float saturation = std::max(0.0f, std::min(1.0f, s));
  const float value = std::max(0.0f, std::min(1.0f, v));

  const int sector = static_cast<int>(std::floor(hue));
  const float fraction = hue - static_cast<float>(sector);
  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * fraction);
  const float t = value * (1.0f - saturation * (1.0f - fraction));

  float r = value;
  float g = t;
  float b = p;
  switch ((sector % 6 + 6) % 6) {
    case 0:
      r = value;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = value;
      b = p;
      break;
    case 2:
      r = p;
      g = value;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = value;
      break;
    case 4:
      r = t;
      g = p;
      b = value;
      break;
    default:
      r = value;
      g = p;
      b = q;
      break;
  }
  node.OutColor(ctx, 0, r, g, b, 1.0f);
}

const KX_FuncPort kRGBToHSVOutputs[] = {
    FP("H", KX_VALUE_FLOAT),
    FP("S", KX_VALUE_FLOAT),
    FP("V", KX_VALUE_FLOAT),
};

void FuncRGBToHSV(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  float color[4];
  node.In(ctx, 0).ColorTo(color);
  const float r = color[0];
  const float g = color[1];
  const float b = color[2];
  const float maximum = std::max(r, std::max(g, b));
  const float minimum = std::min(r, std::min(g, b));
  const float delta = maximum - minimum;

  float hue = 0.0f;
  if (delta > 1e-8f) {
    if (maximum == r) {
      hue = 60.0f * std::fmod((g - b) / delta, 6.0f);
    }
    else if (maximum == g) {
      hue = 60.0f * ((b - r) / delta + 2.0f);
    }
    else {
      hue = 60.0f * ((r - g) / delta + 4.0f);
    }
    if (hue < 0.0f) {
      hue += 360.0f;
    }
  }

  const float saturation = maximum > 1e-8f ? delta / maximum : 0.0f;
  node.OutFloat(ctx, 0, hue);
  node.OutFloat(ctx, 1, saturation);
  node.OutFloat(ctx, 2, maximum);
}

/* ------------------------------------------------------------------------- */
/* B2.3 String                                                               */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kStringPairInputs[] = {
    FP("A", KX_VALUE_STRING),
    FP("B", KX_VALUE_STRING),
};
const KX_FuncPort kStringResultOutput[] = {FP("Result", KX_VALUE_STRING)};
const KX_FuncPort kBoolResultOutput[] = {FP("Result", KX_VALUE_BOOL)};

void FuncStringCompare(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string mode = node.ConfigString("mode");
  const std::string a = B2InString(node, ctx, 0);
  const std::string b = B2InString(node, ctx, 1);
  bool result = false;
  if (mode == "ne") {
    result = a != b;
  }
  else if (mode == "lt") {
    result = a < b;
  }
  else if (mode == "gt") {
    result = a > b;
  }
  else {
    result = a == b;
  }
  node.OutBool(ctx, 0, result);
}

const KX_FuncPort kStringFindInputs[] = {
    FP("Haystack", KX_VALUE_STRING),
    FP("Needle", KX_VALUE_STRING),
};
const KX_FuncPort kIntResultOutput[] = {FP("Index", KX_VALUE_INT)};

void FuncStringFind(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string haystack = B2InString(node, ctx, 0);
  const std::string needle = B2InString(node, ctx, 1);
  if (needle.empty()) {
    node.OutInt(ctx, 0, -1);
    return;
  }
  const std::size_t position = haystack.find(needle);
  node.OutInt(ctx, 0, position == std::string::npos ? -1 : static_cast<int>(position));
}

const KX_FuncPort kStringSourceInput[] = {FP("Source", KX_VALUE_STRING)};

void FuncStringToUpper(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  std::string text = B2InString(node, ctx, 0);
  for (char &c : text) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  node.OutString(ctx, 0, text);
}

void FuncStringToLower(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  std::string text = B2InString(node, ctx, 0);
  for (char &c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  node.OutString(ctx, 0, text);
}

const KX_FuncPort kStringSubstrInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Start", KX_VALUE_INT),
    FP("Length", KX_VALUE_INT),
};

void FuncStringSubstr(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string source = B2InString(node, ctx, 0);
  int start = node.InInt(ctx, 1);
  int length = node.InInt(ctx, 2);
  const int size = static_cast<int>(source.size());
  if (start < 0) {
    start = size + start;
  }
  start = std::max(0, std::min(start, size));
  if (length <= 0) {
    node.OutString(ctx, 0, std::string());
    return;
  }
  const int end = std::min(size, start + length);
  node.OutString(ctx, 0, source.substr(static_cast<std::size_t>(start),
                                       static_cast<std::size_t>(end - start)));
}

const KX_FuncPort kStringToNumberInputs[] = {
    TP("OnEnter"),
    FP("Source", KX_VALUE_STRING),
};
const KX_FuncPort kStringToNumberOutputs[] = {
    FP("Result", KX_VALUE_FLOAT),
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncStringToNumber(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  const std::string text = B2InString(node, ctx, 1);
  bool success = false;
  float result = 0.0f;
  if (!text.empty()) {
    char *end = nullptr;
    result = std::strtof(text.c_str(), &end);
    success = end != nullptr && *end == '\0';
  }
  node.OutFloat(ctx, 0, success ? result : 0.0f);
  node.OutTrigger(ctx, 1, success);
  node.OutTrigger(ctx, 2, !success);
}

const KX_FuncPort kNumberToStringInputs[] = {FP("Value", KX_VALUE_FLOAT)};

void FuncNumberToString(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const float value = node.InFloat(ctx, 0);
  const int precision = std::max(0, std::min(9, node.ConfigInt("precision", 3)));
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", precision, static_cast<double>(value));
  node.OutString(ctx, 0, std::string(buffer));
}

/* ------------------------------------------------------------------------- */
/* B2.4 List                                                                 */
/* ------------------------------------------------------------------------- */

const KX_FuncPort kListInput[] = {FP("List", KX_VALUE_LIST)};
const KX_FuncPort kListAddInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kListIndexInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Index", KX_VALUE_INT),
};
const KX_FuncPort kListSetInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Index", KX_VALUE_INT),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kListContainsInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kListResultOutput[] = {FP("List", KX_VALUE_LIST)};

KX_ValueList &CopyIntoPoolList(KX_ExecutionContext &ctx,
                               const KX_ValueList *source,
                               size_t extra)
{
  KX_ValueList &destination = ctx.Pool().AllocList((source != nullptr ? source->size() : 0) + extra);
  if (source != nullptr) {
    destination.assign(source->begin(), source->end());
  }
  return destination;
}

bool ListAccepts(const KX_ValueList &list, const KX_RuntimeValue &value)
{
  return list.empty() || list.front().type == value.type;
}

void FuncListAdd(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ValueList *source = B2InList(node, ctx, 0);
  KX_ValueList &result = CopyIntoPoolList(ctx, source, 1);
  const KX_RuntimeValue value = node.In(ctx, 1);
  if (ListAccepts(result, value) && result.size() < KX_ValuePool::kMaxListElements) {
    result.push_back(value);
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

void FuncListRemove(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ValueList *source = B2InList(node, ctx, 0);
  KX_ValueList &result = CopyIntoPoolList(ctx, source, 0);
  const int index = node.InInt(ctx, 1);
  if (source != nullptr && index >= 0 && static_cast<size_t>(index) < source->size()) {
    result.erase(result.begin() + index);
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

void FuncListSet(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ValueList *source = B2InList(node, ctx, 0);
  KX_ValueList &result = CopyIntoPoolList(ctx, source, 0);
  const int index = node.InInt(ctx, 1);
  const KX_RuntimeValue value = node.In(ctx, 2);
  if (index >= 0 && static_cast<size_t>(index) < result.size() && ListAccepts(result, value)) {
    result[static_cast<size_t>(index)] = value;
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListGetInputs[] = {
    TP("OnEnter"),
    FP("List", KX_VALUE_LIST),
    FP("Index", KX_VALUE_INT),
};
const KX_FuncPort kListGetOutputs[] = {
    FP("Value", KX_VALUE_FLOAT),
    TP("OnSuccess"),
    TP("OnFail"),
};

void FuncListGet(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  if (!node.InTrigger(ctx, 0)) {
    return;
  }
  KX_ValueList *list = B2InList(node, ctx, 1);
  const int index = node.InInt(ctx, 2);
  const bool valid = list != nullptr && index >= 0 && static_cast<size_t>(index) < list->size();
  node.OutFloat(ctx, 0, valid ? (*list)[static_cast<size_t>(index)].AsFloat() : 0.0f);
  node.OutTrigger(ctx, 1, valid);
  node.OutTrigger(ctx, 2, !valid);
}

void FuncListContains(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ValueList *list = B2InList(node, ctx, 0);
  const KX_RuntimeValue value = node.In(ctx, 1);
  bool found = false;
  if (list != nullptr) {
    for (const KX_RuntimeValue &element : *list) {
      if (ValuesEqual(element, value)) {
        found = true;
        break;
      }
    }
  }
  node.OutBool(ctx, 0, found);
}

void FuncListClear(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  (void)node.InPointer(ctx, 0);
  KX_ValueList &result = ctx.Pool().AllocList(0);
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

/* --- ForEach ------------------------------------------------------------- */

/* ForEach keeps a pointer to the list instead of a copy: the whole sequence is
 * expanded inside the frame which produced it, so the pooled list cannot be
 * reclaimed while the downstream subtree runs. */
struct KX_ForEachState {
  const KX_ValueList *list = nullptr;
  uint32_t count = 0;
  /** Element cursor, only meaningful while the sequence is fanning out. */
  uint32_t index = 0;
};

const KX_FuncPort kForEachInputs[] = {
    FP("List", KX_VALUE_LIST),
    TP("OnEnter"),
};
const KX_FuncPort kForEachOutputs[] = {
    TP("OnEach"),
    FP("Element", KX_VALUE_FLOAT),
    TP("OnDone"),
};

void FuncForEachExec(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ForEachState *state = node.StateBlock<KX_ForEachState>(ctx);
  state->list = nullptr;
  state->count = 0;
  state->index = 0;

  if (!node.InTrigger(ctx, 1)) {
    return;
  }

  KX_ValueList *list = B2InList(node, ctx, 0);
  if (list != nullptr && !list->empty()) {
    state->list = list;
    state->count = static_cast<uint32_t>(
        std::min(list->size(), KX_ValuePool::kMaxListElements));
  }

  node.OutFloat(ctx, 1, 0.0f);
  if (state->count == 0) {
    node.OutTrigger(ctx, 2, true);
  }
}

uint32_t FuncForEachPending(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ForEachState *state = node.StateBlock<KX_ForEachState>(ctx);
  return state->list != nullptr ? state->count : 0;
}

void FuncForEachElement(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  KX_ForEachState *state = node.StateBlock<KX_ForEachState>(ctx);
  if (state->list == nullptr || index >= state->count) {
    return;
  }
  state->index = index;
  node.Out(ctx, 1, (*state->list)[index]);
  node.OutTrigger(ctx, 0, true);
}

void FuncForEachFinish(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  KX_ForEachState *state = node.StateBlock<KX_ForEachState>(ctx);
  state->list = nullptr;
  state->count = 0;
  node.OutTrigger(ctx, 2, true);
}

/* ------------------------------------------------------------------------- */
/* Formula node (RPN)                                                        */
/* ------------------------------------------------------------------------- */

enum class B2FormulaOp : uint8_t {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Pow,
  Lt,
  Gt,
  Le,
  Ge,
  Eq,
  Ne,
  And,
  Or,
  Not,
  Neg,
  Select,
};

struct B2FormulaToken {
  enum class Kind : uint8_t { Number, Variable, Op } kind = Kind::Number;
  float number = 0.0f;
  B2FormulaOp op = B2FormulaOp::Add;
  std::string variable;
};

uint32_t B2FormulaArity(B2FormulaOp op)
{
  switch (op) {
    case B2FormulaOp::Not:
    case B2FormulaOp::Neg:
      return 1;
    case B2FormulaOp::Select:
      return 3;
    default:
      return 2;
  }
}

class B2FormulaParser {
 public:
  B2FormulaParser(const std::string &text, std::vector<B2FormulaToken> *out)
      : text_(text), out_(out)
  {
  }

  bool Parse(std::string *error)
  {
    ParseTernary();
    SkipSpace();
    if (index_ < text_.size()) {
      return Fail(error, "unexpected character");
    }
    if (depth_ != 1) {
      return Fail(error, "malformed expression");
    }
    return true;
  }

 private:
  bool Fail(std::string *error, const char *message)
  {
    if (error != nullptr && error->empty()) {
      *error = std::string("Formula: ") + message + " at offset " + std::to_string(index_);
    }
    return false;
  }

  void SkipSpace()
  {
    while (index_ < text_.size() &&
           (text_[index_] == ' ' || text_[index_] == '\t' || text_[index_] == '\n' ||
            text_[index_] == '\r'))
    {
      ++index_;
    }
  }

  bool Match(const char *token)
  {
    const size_t length = std::strlen(token);
    if (index_ + length > text_.size()) {
      return false;
    }
    if (text_.compare(index_, length, token) != 0) {
      return false;
    }
    index_ += length;
    return true;
  }

  void EmitNumber(float value)
  {
    B2FormulaToken token;
    token.kind = B2FormulaToken::Kind::Number;
    token.number = value;
    out_->push_back(token);
    ++depth_;
    if (depth_ > max_depth_) {
      max_depth_ = depth_;
    }
  }

  void EmitVariable(const std::string &name)
  {
    B2FormulaToken token;
    token.kind = B2FormulaToken::Kind::Variable;
    token.variable = name;
    out_->push_back(token);
    ++depth_;
    if (depth_ > max_depth_) {
      max_depth_ = depth_;
    }
  }

  void EmitOp(B2FormulaOp op)
  {
    const uint32_t arity = B2FormulaArity(op);
    if (depth_ < arity) {
      failed_ = true;
      return;
    }
    B2FormulaToken token;
    token.kind = B2FormulaToken::Kind::Op;
    token.op = op;
    out_->push_back(token);
    depth_ -= arity - 1;
  }

  void ParseTernary()
  {
    ParseOr();
    if (failed_) {
      return;
    }
    SkipSpace();
    if (index_ < text_.size() && text_[index_] == '?') {
      ++index_;
      ParseTernary();
      SkipSpace();
      if (failed_ || index_ >= text_.size() || text_[index_] != ':') {
        failed_ = true;
        index_ = text_.size();
        return;
      }
      ++index_;
      ParseTernary();
      EmitOp(B2FormulaOp::Select);
    }
  }

  void ParseOr()
  {
    ParseAnd();
    while (!failed_) {
      SkipSpace();
      if (!Match("||")) {
        return;
      }
      ParseAnd();
      EmitOp(B2FormulaOp::Or);
    }
  }

  void ParseAnd()
  {
    ParseCompare();
    while (!failed_) {
      SkipSpace();
      if (!Match("&&")) {
        return;
      }
      ParseCompare();
      EmitOp(B2FormulaOp::And);
    }
  }

  void ParseCompare()
  {
    ParseAdd();
    while (!failed_) {
      SkipSpace();
      B2FormulaOp op;
      if (Match("<=")) {
        op = B2FormulaOp::Le;
      }
      else if (Match(">=")) {
        op = B2FormulaOp::Ge;
      }
      else if (Match("==")) {
        op = B2FormulaOp::Eq;
      }
      else if (Match("!=")) {
        op = B2FormulaOp::Ne;
      }
      else if (index_ < text_.size() && text_[index_] == '<') {
        ++index_;
        op = B2FormulaOp::Lt;
      }
      else if (index_ < text_.size() && text_[index_] == '>') {
        ++index_;
        op = B2FormulaOp::Gt;
      }
      else {
        return;
      }
      ParseAdd();
      EmitOp(op);
    }
  }

  void ParseAdd()
  {
    ParseMul();
    while (!failed_) {
      SkipSpace();
      if (index_ >= text_.size() || (text_[index_] != '+' && text_[index_] != '-')) {
        return;
      }
      const char op = text_[index_++];
      ParseMul();
      EmitOp(op == '+' ? B2FormulaOp::Add : B2FormulaOp::Sub);
    }
  }

  void ParseMul()
  {
    ParseUnary();
    while (!failed_) {
      SkipSpace();
      if (index_ >= text_.size() ||
          (text_[index_] != '*' && text_[index_] != '/' && text_[index_] != '%'))
      {
        return;
      }
      const char op = text_[index_++];
      ParseUnary();
      EmitOp(op == '*' ? B2FormulaOp::Mul : (op == '/' ? B2FormulaOp::Div : B2FormulaOp::Mod));
    }
  }

  void ParseUnary()
  {
    SkipSpace();
    if (index_ < text_.size() && text_[index_] == '-') {
      ++index_;
      ParseUnary();
      EmitOp(B2FormulaOp::Neg);
      return;
    }
    if (index_ < text_.size() && text_[index_] == '!') {
      ++index_;
      ParseUnary();
      EmitOp(B2FormulaOp::Not);
      return;
    }
    ParsePower();
  }

  void ParsePower()
  {
    ParsePrimary();
    if (failed_) {
      return;
    }
    SkipSpace();
    if (index_ < text_.size() && text_[index_] == '^') {
      ++index_;
      ParseUnary();
      EmitOp(B2FormulaOp::Pow);
    }
  }

  void ParsePrimary()
  {
    SkipSpace();
    if (index_ >= text_.size()) {
      failed_ = true;
      return;
    }
    if (text_[index_] == '(') {
      ++index_;
      ParseTernary();
      SkipSpace();
      if (index_ >= text_.size() || text_[index_] != ')') {
        failed_ = true;
        index_ = text_.size();
        return;
      }
      ++index_;
      return;
    }
    if ((text_[index_] >= '0' && text_[index_] <= '9') || text_[index_] == '.') {
      char *end = nullptr;
      const float value = std::strtof(text_.c_str() + index_, &end);
      const size_t consumed = static_cast<size_t>(end - (text_.c_str() + index_));
      if (consumed == 0) {
        failed_ = true;
        return;
      }
      index_ += consumed;
      EmitNumber(value);
      return;
    }
    if ((text_[index_] >= 'a' && text_[index_] <= 'z') ||
        (text_[index_] >= 'A' && text_[index_] <= 'Z') || text_[index_] == '_')
    {
      const size_t start = index_;
      while (index_ < text_.size()) {
        const char c = text_[index_];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_')
        {
          ++index_;
          continue;
        }
        break;
      }
      EmitVariable(text_.substr(start, index_ - start));
      return;
    }
    failed_ = true;
  }

  const std::string &text_;
  std::vector<B2FormulaToken> *out_;
  size_t index_ = 0;
  uint32_t depth_ = 0;
  uint32_t max_depth_ = 0;
  bool failed_ = false;
};

class KX_FormulaNode final : public KX_LogicNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    float stack[32];
    uint32_t top = 0;
    for (const B2FormulaToken &token : rpn_) {
      if (token.kind == B2FormulaToken::Kind::Number) {
        if (top >= 32) {
          break;
        }
        stack[top++] = token.number;
        continue;
      }
      if (token.kind == B2FormulaToken::Kind::Variable) {
        if (top >= 32) {
          break;
        }
        float value = 0.0f;
        const auto found = var_to_index_.find(token.variable);
        if (found != var_to_index_.end()) {
          value = ReadInputFloat(ctx, found->second);
        }
        stack[top++] = value;
        continue;
      }
      ApplyOp(token.op, stack, top);
    }
    OutputValue(ctx, 0) = KX_RuntimeValue::Float(top > 0 ? stack[0] : 0.0f);
    return KX_ExecResult::Continue;
  }

  bool Configure(const std::string &config, std::string *error) override
  {
    std::string expression;
    if (!KX_IRConfigGetString(config, "expression", &expression) || expression.empty()) {
      KX_IRConfigGetString(config, "formula", &expression);
    }
    if (expression.empty()) {
      if (error != nullptr) {
        *error = "Formula: empty expression";
      }
      return false;
    }

    std::string declared;
    KX_IRConfigGetString(config, "vars", &declared);

    rpn_.clear();
    B2FormulaParser parser(expression, &rpn_);
    std::string parse_error;
    if (!parser.Parse(&parse_error)) {
      if (error != nullptr) {
        *error = parse_error;
      }
      return false;
    }

    if (!declared.empty()) {
      for (const B2FormulaToken &token : rpn_) {
        if (token.kind != B2FormulaToken::Kind::Variable) {
          continue;
        }
        bool found = false;
        std::size_t start = 0;
        while (start <= declared.size()) {
          const std::size_t end = declared.find(',', start);
          const std::string name = declared.substr(
              start, end == std::string::npos ? std::string::npos : end - start);
          if (name == token.variable) {
            found = true;
            break;
          }
          if (end == std::string::npos) {
            break;
          }
          start = end + 1;
        }
        if (!found) {
          if (error != nullptr) {
            *error = "Formula: unknown variable '" + token.variable + "'";
          }
          return false;
        }
      }
    }
    return true;
  }

  void ConfigurePorts(const std::vector<IRPort> &inputs,
                      const std::vector<IRPort> &outputs) override
  {
    (void)outputs;
    var_to_index_.clear();
    for (uint32_t i = 0; i < inputs.size(); ++i) {
      if (inputs[i].kind == IRPortKind::Value) {
        var_to_index_[inputs[i].name] = i;
      }
    }
  }

  bool IsValueNode() const override
  {
    return true;
  }

 private:
  static void ApplyOp(B2FormulaOp op, float *stack, uint32_t &top)
  {
    auto pop = [&](float &value) {
      if (top == 0) {
        value = 0.0f;
        return false;
      }
      value = stack[--top];
      return true;
    };

    if (op == B2FormulaOp::Not) {
      float a = 0.0f;
      pop(a);
      stack[top++] = a == 0.0f ? 1.0f : 0.0f;
      return;
    }
    if (op == B2FormulaOp::Neg) {
      float a = 0.0f;
      pop(a);
      stack[top++] = -a;
      return;
    }
    if (op == B2FormulaOp::Select) {
      float b = 0.0f;
      float a = 0.0f;
      float condition = 0.0f;
      pop(b);
      pop(a);
      pop(condition);
      stack[top++] = condition != 0.0f ? a : b;
      return;
    }

    float b = 0.0f;
    float a = 0.0f;
    pop(b);
    pop(a);
    float result = 0.0f;
    switch (op) {
      case B2FormulaOp::Add:
        result = a + b;
        break;
      case B2FormulaOp::Sub:
        result = a - b;
        break;
      case B2FormulaOp::Mul:
        result = a * b;
        break;
      case B2FormulaOp::Div:
        result = b != 0.0f ? a / b : 0.0f;
        break;
      case B2FormulaOp::Mod:
        result = b != 0.0f ? std::fmod(a, b) : 0.0f;
        break;
      case B2FormulaOp::Pow:
        result = std::pow(a, b);
        break;
      case B2FormulaOp::Lt:
        result = a < b ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::Gt:
        result = a > b ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::Le:
        result = a <= b ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::Ge:
        result = a >= b ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::Eq:
        result = a == b ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::Ne:
        result = a != b ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::And:
        result = (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f;
        break;
      case B2FormulaOp::Or:
        result = (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f;
        break;
      default:
        result = 0.0f;
        break;
    }
    stack[top++] = result;
  }

  std::vector<B2FormulaToken> rpn_;
  std::unordered_map<std::string, uint32_t> var_to_index_;
};

/* ------------------------------------------------------------------------- */
/* StringFormat node                                                         */
/* ------------------------------------------------------------------------- */

class KX_StringFormatNode final : public KX_LogicNodeRuntime {
 public:
  KX_ExecResult Execute(KX_ExecutionContext &ctx) override
  {
    std::string result;
    for (std::size_t i = 0; i < format_.size();) {
      if (format_[i] == '{') {
        const std::size_t close = format_.find('}', i + 1);
        if (close != std::string::npos) {
          bool numeric = close > i + 1;
          for (std::size_t k = i + 1; k < close; ++k) {
            if (format_[k] < '0' || format_[k] > '9') {
              numeric = false;
              break;
            }
          }
          if (numeric) {
            const int index = std::atoi(format_.substr(i + 1, close - i - 1).c_str());
            if (index >= 0 && static_cast<uint32_t>(index) < InputCount()) {
              const KX_RuntimeValue value = ReadInput(ctx, static_cast<uint32_t>(index));
              const char *text = value.AsString();
              if (text != nullptr) {
                result += text;
              }
              else {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value.AsFloat()));
                result += buffer;
              }
            }
            i = close + 1;
            continue;
          }
        }
      }
      result.push_back(format_[i]);
      ++i;
    }
    OutputValue(ctx, 0) = KX_RuntimeValue::String(ctx.Pool().Intern(result));
    return KX_ExecResult::Continue;
  }

  bool Configure(const std::string &config, std::string *error) override
  {
    if (!KX_IRConfigGetString(config, "format", &format_)) {
      format_.clear();
    }

    int declared = -1;
    KX_IRConfigGetInt(config, "arg_count", &declared);

    int required = 0;
    for (std::size_t i = 0; i < format_.size();) {
      if (format_[i] == '{') {
        const std::size_t close = format_.find('}', i + 1);
        if (close != std::string::npos) {
          const std::string inner = format_.substr(i + 1, close - i - 1);
          bool numeric = !inner.empty();
          for (char c : inner) {
            if (c < '0' || c > '9') {
              numeric = false;
              break;
            }
          }
          if (numeric) {
            required = std::max(required, std::atoi(inner.c_str()) + 1);
            i = close + 1;
            continue;
          }
        }
      }
      ++i;
    }

    if (declared >= 0 && declared != required) {
      if (error != nullptr) {
        *error = "StringFormat: format needs " + std::to_string(required) +
                 " inputs but the node declares " + std::to_string(declared);
      }
      return false;
    }
    return true;
  }

  bool IsValueNode() const override
  {
    return true;
  }

 private:
  std::string format_;
};

/* ------------------------------------------------------------------------- */
/* Table                                                                     */
/* ------------------------------------------------------------------------- */

#define PORT_COUNT(array) static_cast<uint32_t>(sizeof(array) / sizeof(array[0]))

#define B2_VALUE(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, true, false, false}

#define B2_ACTION(id, ins, outs, exec) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, 0, \
                   KX_RUNTIME_DATA_ALIGNMENT, false, false, false}

#define B2_TICK_STATE(id, ins, outs, exec, tick_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, tick_fn, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, true, true}

/* Stateful node which expands a synchronous per-element sequence after Execute. */
#define B2_SEQUENCE(id, ins, outs, exec, count_fn, element_fn, finish_fn, state_type) \
  KX_FunctionSpec {id, ins, PORT_COUNT(ins), outs, PORT_COUNT(outs), exec, nullptr, nullptr, \
                   static_cast<uint32_t>(sizeof(state_type)), alignof(state_type), false, false, \
                   false, count_fn, element_fn, finish_fn}

const KX_FunctionSpec kB2Specs[] = {
    /* B2.1 math / vector */
    B2_VALUE("RandomValue", kRandomValueInputs, kFloatResultOutput, FuncRandomValue),
    B2_VALUE("ClampValue", kClampValueInputs, kFloatResultOutput, FuncClampValue),
    B2_VALUE("Wrap", kWrapInputs, kFloatResultOutput, FuncWrap),
    B2_VALUE("Round", kValueInput, kFloatResultOutput, FuncRound),
    B2_VALUE("Sign", kValueInput, kFloatResultOutput, FuncSign),
    B2_VALUE("VectorReflect", kVectorPairInputs, kVectorResultOutput, FuncVectorReflect),
    B2_VALUE("VectorProject", kVectorPairInputs, kVectorResultOutput, FuncVectorProject),
    B2_VALUE("VectorRotateToward", kRotateTowardInputs, kVectorResultOutput, FuncVectorRotateToward),
    B2_VALUE("VectorAngle", kVectorPairInputs, kFloatResultOutput, FuncVectorAngle),
    B2_VALUE("VectorDistance", kVectorPairInputs, kFloatResultOutput, FuncVectorDistance),

    /* B2.2 colour */
    B2_VALUE("ColorMix", kColorMixInputs, kColorResultOutput, FuncColorMix),
    B2_VALUE("ColorLerp", kColorLerpInputs, kColorResultOutput, FuncColorLerp),
    B2_VALUE("ColorToRGBA", kColorInput, kColorToRGBAOutputs, FuncColorToRGBA),
    B2_VALUE("RGBAToColor", kRGBAToColorInputs, kColorResultOutput, FuncRGBAToColor),
    B2_VALUE("HSVToRGB", kHSVInputs, kColorResultOutput, FuncHSVToRGB),
    B2_VALUE("RGBToHSV", kColorInput, kRGBToHSVOutputs, FuncRGBToHSV),

    /* B2.3 string */
    B2_VALUE("StringCompare", kStringPairInputs, kBoolResultOutput, FuncStringCompare),
    B2_VALUE("StringFind", kStringFindInputs, kIntResultOutput, FuncStringFind),
    B2_VALUE("StringToUpper", kStringSourceInput, kStringResultOutput, FuncStringToUpper),
    B2_VALUE("StringToLower", kStringSourceInput, kStringResultOutput, FuncStringToLower),
    B2_VALUE("StringSubstr", kStringSubstrInputs, kStringResultOutput, FuncStringSubstr),
    B2_ACTION("StringToNumber", kStringToNumberInputs, kStringToNumberOutputs, FuncStringToNumber),
    B2_VALUE("NumberToString", kNumberToStringInputs, kStringResultOutput, FuncNumberToString),

    /* B2.4 list */
    B2_VALUE("ListAdd", kListAddInputs, kListResultOutput, FuncListAdd),
    B2_VALUE("ListRemove", kListIndexInputs, kListResultOutput, FuncListRemove),
    B2_VALUE("ListSet", kListSetInputs, kListResultOutput, FuncListSet),
    B2_ACTION("ListGet", kListGetInputs, kListGetOutputs, FuncListGet),
    B2_VALUE("ListContains", kListContainsInputs, kBoolResultOutput, FuncListContains),
    B2_VALUE("ListClear", kListInput, kListResultOutput, FuncListClear),
    B2_SEQUENCE("ForEach",
                kForEachInputs,
                kForEachOutputs,
                FuncForEachExec,
                FuncForEachPending,
                FuncForEachElement,
                FuncForEachFinish,
                KX_ForEachState),
};

#undef PORT_COUNT

}  // namespace

namespace {

KX_NodeSignature MakeB2Signature(const KX_FunctionSpec &spec)
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
  return signature;
}

}  // namespace

void RegisterB2FunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB2Specs) {
    const KX_FunctionSpec *entry = &spec;
    const KX_NodeSignature signature = MakeB2Signature(*entry);
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB2_") + spec.type_id,
        signature,
        [entry]() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FunctionNode(entry)); });
  }

  {
    KX_NodeSignature signature;
    signature.ports_from_ir = true;
    KX_NodeRegistry::Get().RegisterType(
        "Formula",
        "KX_FuncB2_Formula",
        signature,
        []() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_FormulaNode()); });
  }

  {
    KX_NodeSignature signature;
    signature.ports_from_ir = true;
    KX_NodeRegistry::Get().RegisterType(
        "StringFormat",
        "KX_FuncB2_StringFormat",
        signature,
        []() { return std::unique_ptr<KX_LogicNodeRuntime>(new KX_StringFormatNode()); });
  }
}

bool KX_ReadForEachCursor(uint32_t state_offset,
                          const KX_RuntimeData &data,
                          KX_ForEachCursor *cursor)
{
  if (cursor == nullptr || state_offset == KX_INVALID_OFFSET) {
    return false;
  }
  const KX_ForEachState *state = data.State<KX_ForEachState>(state_offset);
  if (state == nullptr) {
    return false;
  }
  cursor->index = state->index;
  cursor->count = state->count;
  return true;
}

void KX_WriteForEachCursor(uint32_t state_offset,
                           KX_RuntimeData &data,
                           const KX_ForEachCursor &cursor)
{
  if (state_offset == KX_INVALID_OFFSET) {
    return;
  }
  KX_ForEachState *state = data.State<KX_ForEachState>(state_offset);
  if (state == nullptr) {
    return;
  }
  /* The list pointer stays null: a rebuild happens between frames and the next
   * Execute re-reads the list input before any element runs. */
  state->index = cursor.index;
  state->count = cursor.count;
}
