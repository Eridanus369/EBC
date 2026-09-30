/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "camera_delegate.hh"

#include "DNA_ID.h"
#include "DNA_camera_types.h"
#include "DNA_object_types.h"

#include "BKE_idprop.hh"

#include "BLI_listbase_iterator.hh"

#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>

namespace blender::io::hydra {

class BlenderCameraIDPropertiesDataSource : public pxr::HdContainerDataSource {
 public:
  HD_DECLARE_DATASOURCE(BlenderCameraIDPropertiesDataSource);

  void set_camera(const Camera *camera)
  {
    camera_ = camera;
  }

  pxr::TfTokenVector GetNames() override
  {
    pxr::TfTokenVector result;
    if (camera_ && camera_->id.properties) {
      for (const IDProperty &prop : camera_->id.properties->data.group) {
        result.emplace_back(prop.name);
      }
    }
    return result;
  }

  pxr::HdDataSourceBaseHandle Get(const pxr::TfToken &name) override
  {
    if (!camera_ || !camera_->id.properties) {
      return nullptr;
    }
    const IDProperty *prop = IDP_GetPropertyFromGroup(camera_->id.properties, name.GetText());
    if (!prop) {
      return nullptr;
    }
    switch (prop->type) {
      case IDP_INT:
        return pxr::HdRetainedTypedSampledDataSource<int>::New(IDP_int_get(prop));
      case IDP_FLOAT:
        return pxr::HdRetainedTypedSampledDataSource<float>::New(IDP_float_get(prop));
      case IDP_DOUBLE:
        return pxr::HdRetainedTypedSampledDataSource<double>::New(IDP_double_get(prop));
      case IDP_BOOLEAN:
        return pxr::HdRetainedTypedSampledDataSource<bool>::New(bool(IDP_bool_get(prop)));
      default:
        return nullptr;
    }
  }

 private:
  const Camera *camera_ = nullptr;
};

static pxr::HdContainerDataSourceHandle build_camera_ds(const pxr::GfCamera &camera)
{
  using namespace pxr;

  const VtArray<GfVec4f> planes(camera.GetClippingPlanes().begin(),
                                camera.GetClippingPlanes().end());

  const TfToken names[] = {
      HdCameraSchemaTokens->projection,
      HdCameraSchemaTokens->focalLength,
      HdCameraSchemaTokens->horizontalAperture,
      HdCameraSchemaTokens->verticalAperture,
      HdCameraSchemaTokens->horizontalApertureOffset,
      HdCameraSchemaTokens->verticalApertureOffset,
      HdCameraSchemaTokens->clippingRange,
      HdCameraSchemaTokens->clippingPlanes,
      HdCameraSchemaTokens->focusDistance,
      HdCameraSchemaTokens->fStop,
  };
  const HdDataSourceBaseHandle values[] = {
      HdRetainedTypedSampledDataSource<TfToken>::New(
          camera.GetProjection() == GfCamera::Perspective
              ? HdCameraSchemaTokens->perspective
              : HdCameraSchemaTokens->orthographic),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetFocalLength()),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetHorizontalAperture()),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetVerticalAperture()),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetHorizontalApertureOffset()),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetVerticalApertureOffset()),
      HdRetainedTypedSampledDataSource<GfRange1f>::New(camera.GetClippingRange()),
      HdRetainedTypedSampledDataSource<VtArray<GfVec4f>>::New(planes),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetFocusDistance()),
      HdRetainedTypedSampledDataSource<float>::New(camera.GetFStop()),
  };

  return HdRetainedContainerDataSource::New(10, names, values);
}

CameraDelegate::CameraDelegate(pxr::HdRenderIndex *render_index, pxr::SdfPath const &camera_id)
    : camera_scene_index_(pxr::HdRetainedSceneIndex::New()),
      camera_id_(camera_id),
      free_camera_ds_(build_camera_ds(pxr::GfCamera())),
      id_properties_ds_(BlenderCameraIDPropertiesDataSource::New())
{
  pxr::HdContainerDataSourceHandle camera_ds = pxr::HdOverlayContainerDataSource::New(
      id_properties_ds_, free_camera_ds_);

  camera_scene_index_->AddPrims({{camera_id_, pxr::HdPrimTypeTokens->camera, camera_ds}});

  render_index->InsertSceneIndex(
      camera_scene_index_, pxr::SdfPath::AbsoluteRootPath(), /*needsPrefixing=*/false);
}

void CameraDelegate::sync(const Object *camera_object)
{
  const Camera *camera = (camera_object && camera_object->type == OB_CAMERA) ?
                             id_cast<const Camera *>(camera_object->data) :
                             nullptr;
  id_properties_ds_->set_camera(camera);
  camera_scene_index_->DirtyPrims({{camera_id_, pxr::HdDataSourceLocator::EmptyLocator()}});
}

void CameraDelegate::SetCamera(pxr::GfCamera const &camera)
{
  free_camera_ds_ = build_camera_ds(camera);
  pxr::HdContainerDataSourceHandle camera_ds = pxr::HdOverlayContainerDataSource::New(
      id_properties_ds_, free_camera_ds_);
  camera_scene_index_->AddPrims({{camera_id_, pxr::HdPrimTypeTokens->camera, camera_ds}});
  camera_scene_index_->DirtyPrims({{camera_id_, pxr::HdDataSourceLocator::EmptyLocator()}});
}

}  // namespace blender::io::hydra
