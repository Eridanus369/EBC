/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Spectral rendering shared data between host and device.
 */

#pragma once

#include "GPU_shader_shared_utils.hh"

#ifndef GPU_SHADER
namespace blender::eevee {
#endif

/* Number of discrete wavelengths used to represent a spectrum. 8 is the
 * quality/perf balance point recommended in the design doc. */
#define SPECTRAL_SAMPLES 8

/* Visible range in nanometers. */
#define SPECTRAL_LAMBDA_MIN 380.0f
#define SPECTRAL_LAMBDA_MAX 780.0f

struct [[host_shared]] SpectralConfigData {
  /* Number of bounces for photon tracing. */
  int max_bounces;
  /* Total photons to emit per frame. */
  int photon_count;
  /* Spectral sample count (must equal SPECTRAL_SAMPLES). */
  int spectral_samples;
  /* Whether to use wavelength-dependent IOR (dispersion). */
  int dispersion_enabled;
};

struct [[host_shared]] CauchyCoeffs {
  /* Cauchy equation: n(λ) = A + B/λ² + C/λ⁴ */
  float A;
  float B;
  float C;
  float pad;
};

#ifndef GPU_SHADER
}  // namespace blender::eevee
#endif
