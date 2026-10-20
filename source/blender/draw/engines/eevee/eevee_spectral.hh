/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 *
 * Spectral types and wavelength utilities.
 */

#pragma once

#include "eevee_spectral_shared.hh"

#include "BLI_math_vector_types.hh"

namespace blender::eevee {

struct Spectrum {
  float wavelengths[SPECTRAL_SAMPLES];
  float values[SPECTRAL_SAMPLES];

  /* Upsample an RGB color to a spectral power distribution. */
  static Spectrum from_rgb(float3 rgb);
  /* Integrate the SPD against the CIE 1931 CMFs to get XYZ. */
  float3 to_xyz() const;
  /* XYZ -> linear sRGB. */
  float3 to_rgb() const;
};

/* Cauchy equation: n(λ) = A + B/λ² + C/λ⁴, with λ in micrometers. */
float ior_at_wavelength(float lambda_nm, const CauchyCoeffs &coeffs);

}  // namespace blender::eevee
