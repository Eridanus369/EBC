/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "eevee_spectral.hh"
#include "eevee_spectral_lut.hh"

#include <cmath>

namespace blender::eevee {

/* Meng et al. 2015: "Physically-based upsampling for efficient off-line rendering
 * of complex scenes." Simplified variant using equal-energy sampling. */
Spectrum Spectrum::from_rgb(float3 rgb)
{
  Spectrum s;
  const float step = (SPECTRAL_LAMBDA_MAX - SPECTRAL_LAMBDA_MIN) / float(SPECTRAL_SAMPLES - 1);
  for (int i = 0; i < SPECTRAL_SAMPLES; i++) {
    s.wavelengths[i] = SPECTRAL_LAMBDA_MIN + step * float(i);
    /* Equal energy: distribute the RGB energy uniformly across wavelengths.
     * A proper implementation would weight by the CIE locus. */
    s.values[i] = (rgb.x + rgb.y + rgb.z) / float(SPECTRAL_SAMPLES);
  }
  return s;
}

float3 Spectrum::to_xyz() const
{
  float3 xyz = float3(0.0f);
  const float step = (SPECTRAL_LAMBDA_MAX - SPECTRAL_LAMBDA_MIN) / float(SPECTRAL_SAMPLES - 1);
  for (int i = 0; i < SPECTRAL_SAMPLES; i++) {
    /* Map wavelength to LUT index (5 nm steps). */
    float idx_f = (wavelengths[i] - SPECTRAL_LAMBDA_MIN) / 5.0f;
    int idx = int(idx_f);
    idx = (idx < 0) ? 0 : ((idx > 80) ? 80 : idx);
    const auto &cmf = CIE_1931_TABLE[idx];
    xyz.x += values[i] * cmf[0] * step;
    xyz.y += values[i] * cmf[1] * step;
    xyz.z += values[i] * cmf[2] * step;
  }
  return xyz;
}

float3 Spectrum::to_rgb() const
{
  float3 xyz = to_xyz();
  /* Normalize so Y=1 corresponds to white. */
  const float3 white_xyz = float3(0.95047f, 1.0f, 1.08883f);
  xyz = float3(xyz.x / white_xyz.x, xyz.y / white_xyz.y, xyz.z / white_xyz.z);
  /* XYZ -> linear sRGB (IEC 61966-2-1). */
  float3 rgb;
  rgb.x = 3.2406f * xyz.x - 1.5372f * xyz.y - 0.4986f * xyz.z;
  rgb.y = -0.9689f * xyz.x + 1.8758f * xyz.y + 0.0415f * xyz.z;
  rgb.z = 0.0557f * xyz.x - 0.2040f * xyz.y + 1.0570f * xyz.z;
  return rgb;
}

float ior_at_wavelength(float lambda_nm, const CauchyCoeffs &coeffs)
{
  /* Convert nm to micrometers for the standard Cauchy form. */
  const float lambda_um = lambda_nm * 0.001f;
  const float l2 = lambda_um * lambda_um;
  return coeffs.A + coeffs.B / l2 + coeffs.C / (l2 * l2);
}

}  // namespace blender::eevee
