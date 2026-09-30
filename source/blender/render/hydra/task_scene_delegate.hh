/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

namespace blender::render::hydra {

/* Minimal HdSceneDelegate that serves task parameters from a container data
 * source. Replaces the HdLegacyTaskSchema mechanism removed in USD 22. */
class TaskSceneDelegate : public pxr::HdSceneDelegate {
 public:
  TaskSceneDelegate(pxr::HdRenderIndex *render_index,
                    const pxr::SdfPath &id,
                    pxr::HdContainerDataSourceHandle task_ds)
      : pxr::HdSceneDelegate(render_index, id), task_ds_(std::move(task_ds))
  {
  }

  pxr::VtValue Get(const pxr::SdfPath &id, const pxr::TfToken &key) override
  {
    if (id == GetDelegateID() && task_ds_) {
      const pxr::HdDataSourceBaseHandle ds = task_ds_->Get(key);
      if (auto sampled = pxr::HdSampledDataSource::Cast(ds)) {
        return sampled->GetValue(0.0f);
      }
    }
    return pxr::HdSceneDelegate::Get(id, key);
  }

  pxr::TfTokenVector GetTaskRenderTags(const pxr::SdfPath &task_id) override
  {
    if (task_id == GetDelegateID() && task_ds_) {
      const pxr::HdDataSourceBaseHandle ds = task_ds_->Get(pxr::HdTokens->renderTags);
      if (auto typed = pxr::HdTypedSampledDataSource<pxr::TfTokenVector>::Cast(ds)) {
        return typed->GetTypedValue(0.0f);
      }
    }
    return pxr::TfTokenVector();
  }

 private:
  pxr::HdContainerDataSourceHandle task_ds_;
};

}  // namespace blender::render::hydra
