/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include "eevee_caustics.hh"
#include "eevee_instance.hh"

namespace blender::eevee {

void CausticModule::init()
{
  /* TODO: allocate photon buffers, cascaded caustic maps, CIE LUT. */
}

void CausticModule::sync()
{
  /* TODO: read scene settings, set enabled_ and photon_count_. */
}

void CausticModule::render()
{
  if (!enabled_) {
    return;
  }
  /* TODO: emit photons, trace, splat into caustic maps. */
}

}  // namespace blender::eevee
