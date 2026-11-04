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

/** \file KX_Json.h
 *  \ingroup gamelogic
 *
 *  Minimal JSON reader and writer used by the B4 file nodes.
 *
 *  The runtime deliberately avoids a third party JSON dependency. This file
 *  supports the complete JSON value grammar (object, array, string, number,
 *  bool, null), rejects malformed input with a byte offset, escapes every
 *  character JSON requires and writes non-finite numbers as null.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace KXJson {

enum class Type {
  Null = 0,
  Bool,
  Number,
  String,
  Array,
  Object,
};

struct Value {
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Value> array;
  std::vector<std::pair<std::string, Value>> object;

  bool IsNull() const
  {
    return type == Type::Null;
  }
  bool IsNumber() const
  {
    return type == Type::Number;
  }
  bool IsString() const
  {
    return type == Type::String;
  }
  bool IsArray() const
  {
    return type == Type::Array;
  }
  bool IsObject() const
  {
    return type == Type::Object;
  }
  bool IsBool() const
  {
    return type == Type::Bool;
  }

  /** Object member lookup, nullptr when this is not an object or the key is absent. */
  const Value *Find(const std::string &key) const
  {
    if (type != Type::Object) {
      return nullptr;
    }
    for (const auto &member : object) {
      if (member.first == key) {
        return &member.second;
      }
    }
    return nullptr;
  }

  const Value *At(size_t index) const
  {
    if (type != Type::Array || index >= array.size()) {
      return nullptr;
    }
    return &array[index];
  }

  static Value MakeNull()
  {
    return Value();
  }

  static Value MakeBool(bool value)
  {
    Value result;
    result.type = Type::Bool;
    result.boolean = value;
    return result;
  }

  /** Non-finite numbers are written as null, matching the node contract. */
  static Value MakeNumber(double value)
  {
    Value result;
    if (!std::isfinite(value)) {
      return result;
    }
    result.type = Type::Number;
    result.number = value;
    return result;
  }

  static Value MakeString(std::string value)
  {
    Value result;
    result.type = Type::String;
    result.string = std::move(value);
    return result;
  }

  static Value MakeArray()
  {
    Value result;
    result.type = Type::Array;
    return result;
  }

  static Value MakeObject()
  {
    Value result;
    result.type = Type::Object;
    return result;
  }
};

namespace detail {

inline void AppendUtf8(std::string &out, unsigned int code)
{
  if (code <= 0x7f) {
    out.push_back(static_cast<char>(code));
  }
  else if (code <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
  }
  else if (code <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
  }
  else {
    out.push_back(static_cast<char>(0xf0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
  }
}

inline void AppendEscapedString(std::string &out, const std::string &text)
{
  out.push_back('"');
  for (char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(c));
          out += buffer;
        }
        else {
          out.push_back(raw);
        }
        break;
    }
  }
  out.push_back('"');
}

inline void AppendNumber(std::string &out, double value)
{
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  out += buffer;
}

class Parser {
 public:
  Parser(const std::string &text, std::string *error) : text_(text), error_(error)
  {
  }

  bool Run(Value *out)
  {
    if (!text_.empty() && static_cast<unsigned char>(text_[0]) == 0xef &&
        text_.size() >= 3 && static_cast<unsigned char>(text_[1]) == 0xbb &&
        static_cast<unsigned char>(text_[2]) == 0xbf)
    {
      pos_ = 3;
    }
    SkipSpace();
    if (!ParseValue(out, 0)) {
      return false;
    }
    SkipSpace();
    if (pos_ != text_.size()) {
      return Fail("unexpected trailing characters");
    }
    return true;
  }

 private:
  bool Fail(const std::string &message)
  {
    if (error_ != nullptr && error_->empty()) {
      char offset[32];
      std::snprintf(offset, sizeof(offset), " at byte %zu", pos_);
      *error_ = message + offset;
    }
    return false;
  }

  void SkipSpace()
  {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      }
      else {
        break;
      }
    }
  }

  bool Literal(const char *literal)
  {
    const size_t length = std::string(literal).size();
    if (text_.compare(pos_, length, literal) != 0) {
      return false;
    }
    pos_ += length;
    return true;
  }

  bool ParseValue(Value *out, size_t depth)
  {
    if (depth > 64) {
      return Fail("nesting depth exceeds 64");
    }
    if (pos_ >= text_.size()) {
      return Fail("unexpected end of input");
    }
    const char c = text_[pos_];
    if (c == '{') {
      return ParseObject(out, depth);
    }
    if (c == '[') {
      return ParseArray(out, depth);
    }
    if (c == '"') {
      std::string text;
      if (!ParseString(&text)) {
        return false;
      }
      *out = Value::MakeString(std::move(text));
      return true;
    }
    if (c == 't') {
      if (!Literal("true")) {
        return Fail("invalid literal, expected true");
      }
      *out = Value::MakeBool(true);
      return true;
    }
    if (c == 'f') {
      if (!Literal("false")) {
        return Fail("invalid literal, expected false");
      }
      *out = Value::MakeBool(false);
      return true;
    }
    if (c == 'n') {
      if (!Literal("null")) {
        return Fail("invalid literal, expected null");
      }
      *out = Value();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return ParseNumber(out);
    }
    return Fail("unexpected character");
  }

  bool ParseNumber(Value *out)
  {
    const size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
      return Fail("invalid number");
    }
    if (text_[pos_] == '0') {
      ++pos_;
    }
    else {
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        return Fail("invalid fraction");
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        return Fail("invalid exponent");
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    const std::string token = text_.substr(start, pos_ - start);
    double value = 0.0;
    if (std::sscanf(token.c_str(), "%lf", &value) != 1) {
      return Fail("invalid number");
    }
    *out = Value::MakeNumber(value);
    return true;
  }

  bool ParseHex4(unsigned int *out)
  {
    if (pos_ + 4 > text_.size()) {
      return false;
    }
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_ + static_cast<size_t>(i)];
      unsigned int digit = 0;
      if (c >= '0' && c <= '9') {
        digit = static_cast<unsigned int>(c - '0');
      }
      else if (c >= 'a' && c <= 'f') {
        digit = static_cast<unsigned int>(c - 'a') + 10u;
      }
      else if (c >= 'A' && c <= 'F') {
        digit = static_cast<unsigned int>(c - 'A') + 10u;
      }
      else {
        return false;
      }
      value = (value << 4) | digit;
    }
    pos_ += 4;
    *out = value;
    return true;
  }

  bool ParseString(std::string *out)
  {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      return false;
    }
    ++pos_;
    out->clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        return Fail("unescaped control character in string");
      }
      if (c != '\\') {
        out->push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) {
        return Fail("unterminated escape");
      }
      const char escape = text_[pos_++];
      switch (escape) {
        case '"':
          out->push_back('"');
          break;
        case '\\':
          out->push_back('\\');
          break;
        case '/':
          out->push_back('/');
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'u': {
          unsigned int code = 0;
          if (!ParseHex4(&code)) {
            return Fail("invalid unicode escape");
          }
          if (code >= 0xd800 && code <= 0xdbff) {
            if (pos_ + 2 <= text_.size() && text_[pos_] == '\\' &&
                text_[pos_ + 1] == 'u')
            {
              pos_ += 2;
              unsigned int low = 0;
              if (!ParseHex4(&low)) {
                return Fail("invalid low surrogate escape");
              }
              if (low >= 0xdc00 && low <= 0xdfff) {
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
              }
              else {
                return Fail("invalid low surrogate");
              }
            }
            else {
              return Fail("missing low surrogate");
            }
          }
          else if (code >= 0xdc00 && code <= 0xdfff) {
            return Fail("unexpected low surrogate");
          }
          AppendUtf8(*out, code);
          break;
        }
        default:
          return Fail("invalid escape character");
      }
    }
    return Fail("unterminated string");
  }

  bool ParseArray(Value *out, size_t depth)
  {
    ++pos_; /* '[' */
    *out = Value::MakeArray();
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipSpace();
      Value element;
      if (!ParseValue(&element, depth + 1)) {
        return false;
      }
      out->array.push_back(std::move(element));
      SkipSpace();
      if (pos_ >= text_.size()) {
        return Fail("unterminated array");
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return Fail("expected ',' or ']' in array");
    }
  }

  bool ParseObject(Value *out, size_t depth)
  {
    ++pos_; /* '{' */
    *out = Value::MakeObject();
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipSpace();
      std::string key;
      if (!ParseString(&key)) {
        return Fail("expected object key");
      }
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return Fail("expected ':' after object key");
      }
      ++pos_;
      SkipSpace();
      Value member;
      if (!ParseValue(&member, depth + 1)) {
        return false;
      }
      out->object.emplace_back(std::move(key), std::move(member));
      SkipSpace();
      if (pos_ >= text_.size()) {
        return Fail("unterminated object");
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return Fail("expected ',' or '}' in object");
    }
  }

  const std::string &text_;
  std::string *error_;
  size_t pos_ = 0;
};

inline void StringifyValue(const Value &value, std::string &out)
{
  switch (value.type) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += value.boolean ? "true" : "false";
      break;
    case Type::Number:
      AppendNumber(out, value.number);
      break;
    case Type::String:
      AppendEscapedString(out, value.string);
      break;
    case Type::Array:
      out.push_back('[');
      for (size_t i = 0; i < value.array.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        StringifyValue(value.array[i], out);
      }
      out.push_back(']');
      break;
    case Type::Object:
      out.push_back('{');
      for (size_t i = 0; i < value.object.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        AppendEscapedString(out, value.object[i].first);
        out.push_back(':');
        StringifyValue(value.object[i].second, out);
      }
      out.push_back('}');
      break;
  }
}

}  // namespace detail

/** Serialize one value. Non-finite numbers become null. */
inline std::string Stringify(const Value &value)
{
  std::string out;
  detail::StringifyValue(value, out);
  return out;
}

/** Parse @p text. On failure returns false and fills @p error with a byte offset. */
inline bool Parse(const std::string &text, Value *out, std::string *error)
{
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "null output";
    }
    return false;
  }
  if (error != nullptr) {
    error->clear();
  }
  detail::Parser parser(text, error);
  return parser.Run(out);
}

}  // namespace KXJson
