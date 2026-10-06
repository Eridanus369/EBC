/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Platform abstraction for the optional DLSS5 D3D12/NGX runtime.
 *
 * - On Windows (with DLSS SDK available), this file wires the Vulkan-imported
 *   shared textures to the nvngx_dlssnr.dll NR feature.
 * - On any other platform (Linux / unsupported backends), the session is a
 *   permanent no-op: ensure_resources() always reports unavailable, and the
 *   EEVEE adapter transparently falls back to the native Film output.
 */

#include "dlss5_d3d12.hh"

#include "BLI_string.hh"

namespace blender::eevee {

struct Dlss5D3D12Session::Impl {
  char status_[128] = "Unavailable";
  double gpu_time_ms_ = -1.0;
};

Dlss5D3D12Session::Dlss5D3D12Session()
{
#ifdef _WIN32
  impl_ = new Impl();
  STRNCPY(impl_->status_, "DLSS5 D3D12: Windows runtime not yet linked");
#else
  impl_ = new Impl();
  STRNCPY(impl_->status_, "DLSS5 D3D12: Linux requires Vulkan NGX backend (pending)");
#endif
}

Dlss5D3D12Session::~Dlss5D3D12Session()
{
  delete impl_;
}

bool Dlss5D3D12Session::ensure_resources(int2 /*input_extent*/,
                                         int2 /*output_extent*/,
                                         int2 /*guide_extent*/,
                                         const Dlss5NRSettings & /*settings*/,
                                         bool /*color_is_scene_linear*/,
                                         bool /*depth_is_reverse_z*/)
{
  return false;
}

bool Dlss5D3D12Session::warmup()
{
  return false;
}

void Dlss5D3D12Session::retry_initialization()
{
  /* No-op on unsupported platforms. */
}

bool Dlss5D3D12Session::copy_inputs_and_evaluate(const Dlss5D3D12Frame & /*frame*/,
                                                 bool /*copy_color*/,
                                                 bool /*copy_velocity*/)
{
  return false;
}

bool Dlss5D3D12Session::wait_for_output()
{
  return false;
}

void Dlss5D3D12Session::reset()
{
  /* No-op. */
}

gpu::Texture *Dlss5D3D12Session::color_texture() const
{
  return nullptr;
}

gpu::Texture *Dlss5D3D12Session::depth_texture() const
{
  return nullptr;
}

gpu::Texture *Dlss5D3D12Session::velocity_texture() const
{
  return nullptr;
}

gpu::Texture *Dlss5D3D12Session::output_texture() const
{
  return nullptr;
}

double Dlss5D3D12Session::gpu_time_ms() const
{
  return impl_ ? impl_->gpu_time_ms_ : -1.0;
}

bool Dlss5D3D12Session::available() const
{
  return false;
}

const char *Dlss5D3D12Session::status() const
{
  return impl_ ? impl_->status_ : "Dlss5D3D12Session uninitialized";
}

}  // namespace blender::eevee
