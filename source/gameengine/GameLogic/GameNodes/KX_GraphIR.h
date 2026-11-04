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

/** \file KX_GraphIR.h
 *  \ingroup gamelogic
 *
 *  Editor agnostic intermediate representation of a game node graph.
 *
 *  This is the only contract between the editor layer and the graph compiler.
 *  The compiler never reads bpy data: an editor (Python today, C++ later)
 *  translates its node tree into this structure and hands it to
 *  #CompileGraph.
 *
 *  Ports are declared by name and order. The compiler maps them to numeric
 *  offsets; the runtime node port signature registered in #KX_NodeRegistry
 *  guarantees that both orders agree.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "KX_RuntimeValue.h"

/** A port carries either a value slot or a trigger flag. */
enum class IRPortKind : uint8_t {
  Value = 0,
  Trigger = 1,
};

struct IRPort {
  /** Editor visible port name, also used as the debug symbol. */
  std::string name;
  IRPortKind kind = IRPortKind::Value;
  /** Only meaningful for value ports, trigger ports always use KX_VALUE_NONE. */
  KX_ValueType value_tag = KX_VALUE_NONE;
  /** The compiler reports an error when a required input is not connected. */
  bool required = false;
  /**
   * Optional asset type hint for ports carrying #KX_VALUE_ASSET values.
   * P2 compares hints as plain strings; P3 can introduce asset type inheritance.
   */
  std::string asset_type_hint;
};

struct IRNode {
  /** Registered editor type id, for example "FloatMath" or "Branch". */
  std::string type_id;
  /** Instance name copied into the execution plan debug symbol table. */
  std::string debug_name;
  std::vector<IRPort> inputs;
  std::vector<IRPort> outputs;
  /**
   * Node private configuration.
   *
   * P1 only needs flat values (FloatMath reads "op"), so a tiny quoted string
   * lookup is used instead of pulling in a JSON dependency. P2 replaces this
   * with the real asset/config parser.
   */
  std::string config_json;
};

struct IRConnection {
  int from_node = -1;
  int from_port = -1;
  int to_node = -1;
  int to_port = -1;
};

struct GraphIR {
  std::vector<IRNode> nodes;
  std::vector<IRConnection> connections;
};

/**
 * Read a quoted string field out of a flat JSON object.
 *
 * Returns false when the key is missing or malformed. The parser is deliberately
 * minimal: it accepts `{"op": "Mul"}` style input and leaves full JSON support
 * to the P2 asset layer.
 */
bool KX_IRConfigGetString(const std::string &config, const std::string &key, std::string *value);

/** Read a numeric field, returns false when the key is missing or not a number. */
bool KX_IRConfigGetFloat(const std::string &config, const std::string &key, float *value);
bool KX_IRConfigGetInt(const std::string &config, const std::string &key, int *value);
/** Read a boolean field, accepts true/false as well as 0/1. */
bool KX_IRConfigGetBool(const std::string &config, const std::string &key, bool *value);
/** Read a three component array such as `"v": [1, 2, 3]`. */
bool KX_IRConfigGetVector(const std::string &config, const std::string &key, float value[3]);
