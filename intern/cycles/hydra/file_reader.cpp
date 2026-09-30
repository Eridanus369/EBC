/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "hydra/file_reader.h"
#include "hydra/camera.h"
#include "hydra/render_delegate.h"

#include "util/log.h"
#include "util/path.h"
#include "util/unique_ptr.h"

#include "scene/scene.h"

#include <pxr/base/plug/registry.h>
#include <pxr/imaging/hd/flatteningSceneIndex.h>
#include <pxr/imaging/hd/mergingSceneIndex.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/extComputationPrimvarPruningSceneIndex.h>
#include <pxr/imaging/hdsi/legacyDisplayStyleOverrideSceneIndex.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usdImaging/usdImaging/flattenedDataSourceProviders.h>
#include <pxr/usdImaging/usdImaging/materialBindingsResolvingSceneIndex.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

HDCYCLES_NAMESPACE_OPEN_SCOPE

/* Dummy task whose only purpose is to provide render tag tokens to the render index. */
class DummyHdTask : public HdTask {
 public:
  DummyHdTask(HdSceneDelegate * /*delegate*/, const SdfPath &id)
      : HdTask(id), tags({HdRenderTagTokens->geometry, HdRenderTagTokens->render})
  {
  }

 protected:
  void Sync(HdSceneDelegate * /*delegate*/,
            HdTaskContext * /*ctx*/,
            HdDirtyBits * /*dirtyBits*/) override
  {
  }

  void Prepare(HdTaskContext * /*ctx*/, HdRenderIndex * /*render_index*/) override {}

  void Execute(HdTaskContext * /*ctx*/) override {}

  const TfTokenVector &GetRenderTags() const override
  {
    return tags;
  }

  TfTokenVector tags;
};

void HdCyclesFileReader::read(Session *session, const char *filepath, const bool use_camera)
{
  /* Initialize USD. */
  PlugRegistry::GetInstance().RegisterPlugins(path_get("usd"));

  /* Open Stage. */
  const UsdStageRefPtr stage = UsdStage::Open(filepath);
  if (!stage) {
    LOG_ERROR << "USD failed to read " << filepath;
    return;
  }

  /* Init paths. */
  const SdfPath root_path = SdfPath::AbsoluteRootPath();
  const SdfPath task_path("/_hdCycles/DummyHdTask");

  /* Create render delegate. */
  HdRenderSettingsMap settings_map;
  settings_map.insert(std::make_pair(HdCyclesRenderSettingsTokens->stageMetersPerUnit,
                                     VtValue(UsdGeomGetStageMetersPerUnit(stage))));

  HdCyclesDelegate render_delegate(settings_map, session, true);

  /* Create render index. */
  unique_ptr<HdRenderIndex> render_index(HdRenderIndex::New(&render_delegate, {}));

  /* Set up scene index from USD stage for easy consumption.
   * Do ext computation (e.g. skinning), flattening and resolve material bindings. */
  UsdImagingStageSceneIndexRefPtr stage_si = UsdImagingStageSceneIndex::New(nullptr);
  stage_si->SetStage(stage);
  stage_si->SetTime(UsdTimeCode::Default());

  HdSceneIndexBaseRefPtr filtered = stage_si;
  filtered = HdSiExtComputationPrimvarPruningSceneIndex::New(filtered);
  filtered = HdFlatteningSceneIndex::New(filtered, UsdImagingFlattenedDataSourceProviders());
  filtered = UsdImagingMaterialBindingsResolvingSceneIndex::New(filtered, nullptr);

  /* Create collection for syncing prims. */
  HdRprimCollection collection(HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));
  collection.SetRootPath(root_path);

  /* Insert the filtered scene index directly into the render index. */
  const bool needs_prefixing = false;
  render_index->InsertSceneIndex(filtered, root_path, needs_prefixing);

  /* Register the DummyHdTask directly with the render index.
   * USD 22+ removed the legacy task factory/schema API; tasks are now
   * registered via HdRenderIndex::InsertTask. The delegate parameter is
   * unused by DummyHdTask, so nullptr is safe. */
  render_index->InsertTask<DummyHdTask>(nullptr, task_path);

  stage_si->ApplyPendingUpdates();

  render_index->EnqueueCollectionToSync(collection);

  /* Sync prims. */
  HdTaskContext task_context;
  HdTaskSharedPtrVector tasks;
  if (HdTaskSharedPtr const &task = render_index->GetTask(task_path)) {
    tasks.push_back(task);
  }

  render_index->SyncAll(&tasks, &task_context);
  render_delegate.CommitResources(&render_index->GetChangeTracker());

  /* Use first camera in stage.
   * TODO: get camera from UsdRender if available. */
  if (use_camera) {
    for (const UsdPrim &prim : stage->Traverse()) {
      if (prim.IsA<UsdGeomCamera>()) {
        HdSprim *sprim = render_index->GetSprim(HdPrimTypeTokens->camera, prim.GetPath());
        if (sprim) {
          HdCyclesCamera *camera = dynamic_cast<HdCyclesCamera *>(sprim);
          camera->ApplyCameraSettings(render_delegate.GetRenderParam(), session->scene->camera);
          break;
        }
      }
    }
  }
}

HDCYCLES_NAMESPACE_CLOSE_SCOPE
