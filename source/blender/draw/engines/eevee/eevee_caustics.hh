/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Spectral caustics module.
 *
 * Photon-based caustics with spectral dispersion. Photons carry a wavelength
 * sampled from the CIE 1931 locus; glass interfaces refract per-wavelength via
 * Cauchy's equation. Results are splatted into cascaded caustic maps and queried
 * by surface and volume shading.
 */

#pragma once

#include "draw_pass.hh"
#include "eevee_caustic_map.hh"

namespace blender::eevee {

using namespace draw;

class Instance;

/* -------------------------------------------------------------------- */
/** \name CausticModule
 * \{ */

class CausticModule {
 private:
  Instance &inst_;

  bool enabled_ = false;
  int photon_count_ = 0;
  int spectral_samples_ = 0;

  CausticMap caustic_map_;

  /* Persistent GPU buffers to avoid CPU round-trips. */
  StorageArrayBuffer<Photon, 131072> photon_pool_ = {"photon_pool"};
  
  PassSimple photon_emit_ps_ = {"PhotonEmit"};
  PassSimple photon_trace_ps_ = {"PhotonTrace"};
  PassSimple photon_splat_ps_ = {"PhotonSplat"};

 public:
  CausticModule(Instance &inst);
  ~CausticModule() {};

  void init();
  void sync();
  void render();
  Texture &color_texture();

  bool enabled() const
  {
    return enabled_;
  }
};

/** \} */

}  // namespace blender::eevee
