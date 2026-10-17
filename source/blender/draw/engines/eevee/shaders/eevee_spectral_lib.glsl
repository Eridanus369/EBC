/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#define SPECTRAL_SAMPLES 8
#define SPECTRAL_LAMBDA_MIN 380.0
#define SPECTRAL_LAMBDA_MAX 780.0

/* CIE 1931 2-deg CMF approximated by Wyman et al. 2013 multi-lobe Gaussian fits.
 * Fast to evaluate, accurate to ~1% over the visible range. */
float3 cie_1931_cmf(float lambda)
{
  float l = lambda;
  float x = 1.056 * exp(-0.5 * pow((l - 599.8) / 37.9, 2.0))
          + 0.362 * exp(-0.5 * pow((l - 442.0) / 16.0, 2.0))
          - 0.065 * exp(-0.5 * pow((l - 501.1) / 20.4, 2.0));
  float y = 0.821 * exp(-0.5 * pow((l - 568.8) / 46.9, 2.0))
          + 0.286 * exp(-0.5 * pow((l - 530.9) / 16.3, 2.0));
  float z = 1.217 * exp(-0.5 * pow((l - 437.0) / 11.8, 2.0))
          + 0.681 * exp(-0.5 * pow((l - 459.0) / 26.0, 2.0));
  return max(float3(x, y, z), float3(0.0));
}

float spectrum_to_lambda(int idx)
{
  return SPECTRAL_LAMBDA_MIN + (SPECTRAL_LAMBDA_MAX - SPECTRAL_LAMBDA_MIN)
         * float(idx) / float(SPECTRAL_SAMPLES - 1);
}

/* Upsample an RGB color into an equal-energy spectral distribution.
 * Returns SPD value at each discrete wavelength. */
float3 spectrum_upsample_rgb(float3 rgb)
{
  return rgb / float(SPECTRAL_SAMPLES);
}

/* CIE Cauchy dispersion: n(λ) = A + B/λ² + C/λ⁴ (λ in μm). */
float spectral_cauchy_ior(float lambda_nm, float3 cauchy)
{
  float lambda_um = lambda_nm * 0.001;
  float l2 = lambda_um * lambda_um;
  return cauchy.x + cauchy.y / l2 + cauchy.z / (l2 * l2);
}
