/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_material_interface.bsl.hh"

/* NPR: screen-space outline control.
 * The node has no output sockets; the synthetic `Closure &dummy` output exists so the generated
 * node link can be captured by the material codegen and serialized into the surface graph. */
[[node]]
void node_output_outline(
    [[maybe_unused]] float4 line_color,
    [[maybe_unused]] float line_alpha,
    [[maybe_unused]] float line_width,
    [[maybe_unused]] float depth_threshold,
    [[maybe_unused]] float depth_threshold_range,
    [[maybe_unused]] float depth_edge_width,
    [[maybe_unused]] float normal_threshold,
    [[maybe_unused]] float normal_threshold_range,
    [[maybe_unused]] float normal_edge_width,
    [[maybe_unused]] int outline_id,
    [[maybe_unused]] bool id_edge,
    [[maybe_unused]] float id_edge_width,
    [[maybe_unused]] bool freestyle_edge,
    Closure &dummy)
{
  dummy = Closure(0);
  line_color.a = saturate(line_color.a) * saturate(line_alpha);
  output_outline(line_color,
                 line_width,
                 depth_threshold,
                 depth_threshold_range,
                 depth_edge_width,
                 normal_threshold,
                 normal_threshold_range,
                 normal_edge_width,
                 float(outline_id),
                 id_edge,
                 id_edge_width,
                 freestyle_edge);
}
