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

/** \file KX_GraphSceneManager.h
 *  \ingroup gamelogic
 *
 *  Ownership and lifetime of every native graph instance inside one scene.
 *
 *  Loading a scene means: register the manifest assets, keep one #GraphIR per
 *  graph id, compile one #KX_GraphInstance per object mount and tick them in a
 *  deterministic order (mount priority, then object name).
 *
 *  The manager never touches bpy data. #KX_Scene hands it a scene pointer and
 *  the parsed manifest, and tells it when an object disappears.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "KX_AssetRegistry.h"
#include "KX_GraphIR.h"
#include "KX_SceneManifest.h"

class KX_GameObject;
class KX_GraphInstance;
class KX_Scene;
struct KX_AdapterSet;

class KX_GraphSceneManager {
 public:
  /* Defined in the .cpp so the unique_ptr<KX_GraphInstance> vector does not
   * need the complete type in every translation unit that includes this
   * header. */
  KX_GraphSceneManager();
  ~KX_GraphSceneManager();

  KX_GraphSceneManager(const KX_GraphSceneManager &) = delete;
  KX_GraphSceneManager &operator=(const KX_GraphSceneManager &) = delete;

  /**
   * Register assets, keep the graphs and mount the instances.
   *
   * A broken graph or a missing object only produces a warning: the scene keeps
   * loading and the legacy Python executor remains the fallback.
   */
  bool OnSceneLoad(KX_Scene *scene, const KX_SceneManifest &manifest);

  /**
   * Tear down in reverse creation order.
   *
   * Instances go first so their plans release the asset references they
   * acquired, then the registry drops the scene owned assets. After this call
   * #KX_AssetRegistry::AssetCount returns to the pre load baseline.
   */
  void OnSceneUnload();

  /** Tick every mounted graph in deterministic order. */
  void Tick(float delta_time);

  /**
   * Draw the scene HUD once.
   *
   * The engine calls this after the scene render finished, before the buffer
   * swap; it is never part of #Tick.
   */
  void DrawUI();

  /** Drop the instance of an object that is being destroyed. */
  void OnObjectRemoved(KX_GameObject *object);

  KX_GraphInstance *FindByOwner(KX_GameObject *object) const;

  /** Number of live graph instances, used by lifecycle tests. */
  size_t InstanceCount() const;
  /** Number of exported graphs kept for compilation, used by load tests. */
  size_t GraphCount() const;
  /** Number of scene owned asset registrations. */
  size_t AssetCount() const;
  /** Adapters shared by every graph of this scene, may be null before load. */
  KX_AdapterSet *Adapters() const;
  /** Diagnostics collected while loading, mirrored to stderr. */
  const std::vector<std::string> &Warnings() const;

  KX_Scene *Scene() const;

 private:
  void Warn(const std::string &message);
  KX_GameObject *FindObject(const std::string &name) const;

  KX_Scene *scene_ = nullptr;
  std::unique_ptr<KX_AdapterSet> adapters_;
  std::vector<std::unique_ptr<KX_GraphInstance>> instances_;
  std::unordered_map<std::string, std::unique_ptr<GraphIR>> graphs_;
  std::unordered_map<KX_GameObject *, KX_GraphInstance *> by_owner_;
  std::vector<KX_AssetHandle> asset_handles_;
  std::vector<std::string> warnings_;
};
