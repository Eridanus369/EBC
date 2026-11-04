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

/** \file KX_GraphSceneManager.cpp
 *  \ingroup gamelogic
 */

#include "KX_GraphSceneManager.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_GraphInstance.h"
#include "KX_Scene.h"

KX_GraphSceneManager::KX_GraphSceneManager() = default;

KX_GraphSceneManager::~KX_GraphSceneManager()
{
  OnSceneUnload();
}

bool KX_GraphSceneManager::OnSceneLoad(KX_Scene *scene, const KX_SceneManifest &manifest)
{
  if (scene == nullptr) {
    return false;
  }

  OnSceneUnload();
  scene_ = scene;
  adapters_ = KX_CreateEngineAdapters(scene);

  for (const std::string &warning : manifest.warnings) {
    Warn(warning);
  }

  /* Stage 1: assets. The registry takes ownership, the manager only keeps the
   * handles so the unload path can drop the scene reference deterministically. */
  KX_AssetRegistry &registry = KX_AssetRegistry::Get();
  for (const KX_ManifestAsset &asset_def : manifest.assets) {
    std::unique_ptr<KX_GameDataAsset> asset =
        std::make_unique<KX_GameDataAsset>(asset_def.asset_id, asset_def.asset_type);
    for (const KX_ManifestAssetField &field : asset_def.fields) {
      std::string error;
      if (!asset->AddField(field.name, field.type, field.default_value, &error)) {
        Warn("Asset '" + asset_def.asset_id + "': " + error);
      }
    }

    const KX_AssetHandle handle = registry.Register(std::move(asset));
    if (handle == KX_INVALID_ASSET_HANDLE) {
      Warn("Asset '" + asset_def.asset_id + "' could not be registered");
      continue;
    }
    asset_handles_.push_back(handle);
  }

  /* Stage 2: graphs. They stay uncompiled until a mount needs them, so a graph
   * that is exported but not used never costs compile time. */
  for (const KX_ManifestGraph &graph_def : manifest.graphs) {
    graphs_[graph_def.graph_id] = std::make_unique<GraphIR>(graph_def.ir);
  }

  /* Stage 3: mounts. The order is the P3 determinism contract: lower priority
   * first, ties broken by object name, so "A writes asset, B reads asset" is
   * reproducible across runs and reloads. */
  std::vector<const KX_ManifestMount *> mounts;
  mounts.reserve(manifest.mounts.size());
  for (const KX_ManifestMount &mount : manifest.mounts) {
    if (!mount.scene.empty() && mount.scene != scene->GetName()) {
      continue;
    }
    mounts.push_back(&mount);
  }
  std::stable_sort(mounts.begin(),
                   mounts.end(),
                   [](const KX_ManifestMount *a, const KX_ManifestMount *b) {
                     if (a->priority != b->priority) {
                       return a->priority < b->priority;
                     }
                     return a->object < b->object;
                   });

  for (const KX_ManifestMount *mount : mounts) {
    KX_GameObject *object = FindObject(mount->object);
    if (object == nullptr) {
      Warn("Mount skipped: object '" + mount->object + "' is not in the scene");
      continue;
    }
    if (by_owner_.find(object) != by_owner_.end()) {
      Warn("Mount skipped: object '" + mount->object + "' already runs a graph");
      continue;
    }

    const auto graph_it = graphs_.find(mount->graph_id);
    if (graph_it == graphs_.end()) {
      Warn("Mount skipped: graph '" + mount->graph_id + "' was not exported");
      continue;
    }

    std::unique_ptr<KX_GraphInstance> instance =
        std::make_unique<KX_GraphInstance>(scene_, object, this);
    std::vector<std::string> compile_warnings;
    const bool loaded = instance->Load(*graph_it->second, &compile_warnings);
    for (const std::string &warning : compile_warnings) {
      Warn("Graph '" + mount->graph_id + "': " + warning);
    }
    if (!loaded) {
      Warn("Graph '" + mount->graph_id + "' failed to compile, the Python fallback stays active");
      continue;
    }

    if (adapters_) {
      instance->GetContext()->SetAdapters(adapters_->input.get(),
                                          adapters_->scene.get(),
                                          adapters_->physics.get(),
                                          adapters_->objects.get(),
                                          adapters_->animation.get(),
                                          adapters_->message.get(),
                                          adapters_->audio.get(),
                                          adapters_->scene_control.get(),
                                          adapters_->ui.get(),
                                          adapters_->file.get());
    }

    by_owner_[object] = instance.get();
    instances_.push_back(std::move(instance));
  }

  std::fprintf(stderr,
               "GameNode: scene '%s': %zu graph(s), %zu asset(s), %zu instance(s)\n",
               scene->GetName().c_str(),
               graphs_.size(),
               asset_handles_.size(),
               instances_.size());

  return true;
}

void KX_GraphSceneManager::OnSceneUnload()
{
  /* Reverse creation order: destroying the instances releases the asset
   * references taken by their plans, then the registry can drop the scene
   * owned references without leaving a dangling asset behind. */
  for (std::unique_ptr<KX_GraphInstance> &instance : instances_) {
    instance->Shutdown();
  }
  instances_.clear();
  by_owner_.clear();
  graphs_.clear();

  KX_AssetRegistry &registry = KX_AssetRegistry::Get();
  for (const KX_AssetHandle handle : asset_handles_) {
    registry.Unregister(handle);
  }
  asset_handles_.clear();

  adapters_.reset();
  scene_ = nullptr;
}

void KX_GraphSceneManager::Tick(float delta_time)
{
  /* Input edges are computed once for the whole scene, so every graph sees the
   * same Press/Release/Hold state in the same frame. */
  if (adapters_) {
    adapters_->BeginFrame(delta_time);
  }
  for (std::unique_ptr<KX_GraphInstance> &instance : instances_) {
    instance->Tick(delta_time);
  }
  if (adapters_) {
    adapters_->EndFrame();
  }
}

void KX_GraphSceneManager::DrawUI()
{
  /* Called from the engine frame end, after the scene composition, so the HUD
   * lands in the backbuffer that is swapped next (D2). */
  if (adapters_ && scene_ != nullptr) {
    adapters_->DrawUI();
  }
}

void KX_GraphSceneManager::OnObjectRemoved(KX_GameObject *object)
{
  if (object == nullptr) {
    return;
  }

  auto owner_it = by_owner_.find(object);
  if (owner_it == by_owner_.end()) {
    return;
  }

  KX_GraphInstance *instance = owner_it->second;
  by_owner_.erase(owner_it);

  for (auto instance_it = instances_.begin(); instance_it != instances_.end(); ++instance_it) {
    if (instance_it->get() != instance) {
      continue;
    }
    /* Shutdown before the erase so the plan is gone while the owner pointer is
     * still only being compared, never dereferenced. */
    (*instance_it)->Shutdown();
    instances_.erase(instance_it);
    return;
  }
}

KX_GraphInstance *KX_GraphSceneManager::FindByOwner(KX_GameObject *object) const
{
  const auto it = by_owner_.find(object);
  return it != by_owner_.end() ? it->second : nullptr;
}

size_t KX_GraphSceneManager::InstanceCount() const
{
  return instances_.size();
}

size_t KX_GraphSceneManager::GraphCount() const
{
  return graphs_.size();
}

size_t KX_GraphSceneManager::AssetCount() const
{
  return asset_handles_.size();
}

const std::vector<std::string> &KX_GraphSceneManager::Warnings() const
{
  return warnings_;
}

KX_Scene *KX_GraphSceneManager::Scene() const
{
  return scene_;
}

KX_AdapterSet *KX_GraphSceneManager::Adapters() const
{
  return adapters_.get();
}

void KX_GraphSceneManager::Warn(const std::string &message)
{
  warnings_.push_back(message);
  std::fprintf(stderr, "GameNode: %s\n", message.c_str());
}

KX_GameObject *KX_GraphSceneManager::FindObject(const std::string &name) const
{
  if (scene_ == nullptr) {
    return nullptr;
  }

  for (KX_GameObject *object : scene_->GetObjectList()) {
    if (object != nullptr && object->GetName() == name) {
      return object;
    }
  }
  return nullptr;
}
