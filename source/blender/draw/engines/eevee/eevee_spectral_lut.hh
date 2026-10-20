/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#pragma once

#include <array>

namespace blender::eevee {

/* 81 entries at 5nm steps from 380nm to 780nm. */
extern const std::array<std::array<float, 3>, 81> CIE_1931_TABLE;

}  // namespace blender::eevee
