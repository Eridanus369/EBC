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

/** \file KX_SceneManifest.h
 *  \ingroup gamelogic
 *
 *  Editor side scene manifest, the P3 transport format.
 *
 *  The Python exporter walks the bpy data and writes one JSON object per .blend
 *  into the Scene custom property `game_graph_manifest`. The engine parses it
 *  back into this structure: asset definitions, one #GraphIR per game node tree
 *  and the object mount table. The parser never touches bpy data, so the same
 *  manifest can come from a file, a test fixture or a future C++ editor.
 */

#pragma once

#include <string>
#include <vector>

#include "KX_GameDataAsset.h"
#include "KX_GraphIR.h"
#include "KX_RuntimeValue.h"

struct KX_ManifestAssetField {
  std::string name;
  KX_AssetFieldType type = KX_AssetFieldType::Float;
  KX_RuntimeValue default_value;
};

struct KX_ManifestAsset {
  std::string asset_id;
  std::string asset_type;
  std::vector<KX_ManifestAssetField> fields;
};

struct KX_ManifestGraph {
  std::string graph_id;
  GraphIR ir;
};

struct KX_ManifestMount {
  /** Scene the mount belongs to, empty means "every scene". */
  std::string scene;
  std::string object;
  std::string graph_id;
  /** Lower values tick first, ties broken by object name. */
  int priority = 0;
};

struct KX_SceneManifest {
  std::vector<KX_ManifestAsset> assets;
  std::vector<KX_ManifestGraph> graphs;
  std::vector<KX_ManifestMount> mounts;
  /** Non fatal problems, for example an unknown node in a graph. */
  std::vector<std::string> warnings;
};

/**
 * Parse the JSON manifest emitted by the editor.
 *
 * Returns false only for malformed documents (bad JSON or a non object root).
 * Unknown node types, ports and connections are reported through
 * #KX_SceneManifest::warnings so a single broken tree does not stop the scene
 * from loading; the graph compiler turns them into diagnostics later.
 */
bool KX_ParseSceneManifestJson(const std::string &json_text, KX_SceneManifest *manifest);
