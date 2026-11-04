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

/** \file KX_GraphIR.cpp
 *  \ingroup gamelogic
 */

#include "KX_GraphIR.h"

#include <cctype>
#include <cstdlib>

namespace {

/** Position of the first character of the value belonging to @a key. */
size_t FindValueStart(const std::string &config, const std::string &key)
{
  const std::string quoted_key = "\"" + key + "\"";
  size_t pos = config.find(quoted_key);
  if (pos == std::string::npos) {
    return std::string::npos;
  }

  pos = config.find(':', pos + quoted_key.size());
  if (pos == std::string::npos) {
    return std::string::npos;
  }
  ++pos;

  while (pos < config.size() && std::isspace(static_cast<unsigned char>(config[pos])) != 0) {
    ++pos;
  }
  return pos < config.size() ? pos : std::string::npos;
}

/** Raw token of @a key up to the next `,` or `}`. */
std::string FindRawToken(const std::string &config, const std::string &key)
{
  const size_t start = FindValueStart(config, key);
  if (start == std::string::npos) {
    return std::string();
  }

  size_t end = start;
  int depth = 0;
  while (end < config.size()) {
    const char c = config[end];
    if (c == '"') {
      ++end;
      while (end < config.size() && config[end] != '"') {
        if (config[end] == '\\' && end + 1 < config.size()) {
          ++end;
        }
        ++end;
      }
    }
    else if (c == '[' || c == '{') {
      ++depth;
    }
    else if (c == ']' || c == '}') {
      if (depth == 0) {
        break;
      }
      --depth;
    }
    else if (c == ',' && depth == 0) {
      break;
    }

    if (end >= config.size()) {
      break;
    }
    ++end;
  }
  return config.substr(start, end - start);
}

}  // namespace

bool KX_IRConfigGetString(const std::string &config, const std::string &key, std::string *value)
{
  const size_t pos = FindValueStart(config, key);
  if (pos == std::string::npos || config[pos] != '"') {
    return false;
  }

  size_t cursor = pos + 1;
  std::string result;
  while (cursor < config.size() && config[cursor] != '"') {
    if (config[cursor] == '\\' && cursor + 1 < config.size()) {
      ++cursor;
    }
    result.push_back(config[cursor]);
    ++cursor;
  }
  if (cursor >= config.size()) {
    return false;
  }

  if (value != nullptr) {
    *value = result;
  }
  return true;
}

bool KX_IRConfigGetFloat(const std::string &config, const std::string &key, float *value)
{
  const std::string token = FindRawToken(config, key);
  if (token.empty()) {
    return false;
  }

  char *end = nullptr;
  const double parsed = std::strtod(token.c_str(), &end);
  if (end == token.c_str()) {
    return false;
  }
  if (value != nullptr) {
    *value = float(parsed);
  }
  return true;
}

bool KX_IRConfigGetInt(const std::string &config, const std::string &key, int *value)
{
  const std::string token = FindRawToken(config, key);
  if (token.empty()) {
    return false;
  }

  char *end = nullptr;
  const long parsed = std::strtol(token.c_str(), &end, 10);
  if (end == token.c_str()) {
    return false;
  }
  if (value != nullptr) {
    *value = int(parsed);
  }
  return true;
}

bool KX_IRConfigGetBool(const std::string &config, const std::string &key, bool *value)
{
  const std::string token = FindRawToken(config, key);
  if (token.empty()) {
    return false;
  }

  if (token == "true" || token == "1") {
    if (value != nullptr) {
      *value = true;
    }
    return true;
  }
  if (token == "false" || token == "0") {
    if (value != nullptr) {
      *value = false;
    }
    return true;
  }
  return false;
}

bool KX_IRConfigGetVector(const std::string &config, const std::string &key, float value[3])
{
  const std::string token = FindRawToken(config, key);
  if (token.empty() || token[0] != '[') {
    return false;
  }

  float components[3] = {0.0f, 0.0f, 0.0f};
  size_t cursor = 1;
  for (int component = 0; component < 3; ++component) {
    while (cursor < token.size() &&
           (token[cursor] == ' ' || token[cursor] == ',' || token[cursor] == '\t'))
    {
      ++cursor;
    }
    if (cursor >= token.size() || token[cursor] == ']') {
      break;
    }

    char *end = nullptr;
    components[component] = float(std::strtod(token.c_str() + cursor, &end));
    if (end == token.c_str() + cursor) {
      return false;
    }
    cursor = size_t(end - token.c_str());
  }

  if (value != nullptr) {
    value[0] = components[0];
    value[1] = components[1];
    value[2] = components[2];
  }
  return true;
}
