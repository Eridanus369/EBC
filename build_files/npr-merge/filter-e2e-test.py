#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

# NPR F3d: end-to-end Eevee filter graph validation.
#
# Renders the same scene three times:
#   1. baseline (no filter graph)
#   2. filter material A: Pass Input -> Image Sample (new node) -> Invert -> Filter Output,
#      invoked by a Filter Pass node fed from the graph Scene Color node
#   3. filter material B: material-domain Scene Color node -> Filter Output (passthrough)
#
# A successful run means (2) visibly differs from (1) and (3) matches (1).

import bpy
import os
import sys

OUT = "/tmp/npr-f3d"
os.makedirs(OUT, exist_ok=True)

failures = []


def check(condition, message):
    print(f"[{'PASS' if condition else 'FAIL'}] {message}")
    if not condition:
        failures.append(message)


bpy.ops.wm.read_factory_settings(use_empty=True)

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 64
scene.render.resolution_y = 64
scene.render.image_settings.file_format = "PNG"

bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.active_object

bpy.ops.object.camera_add(location=(3, -3, 3))
cam = bpy.context.active_object
cam.rotation_euler = (1.1, 0.0, 0.785)
scene.camera = cam

bpy.ops.object.light_add(type="SUN", location=(3, -3, 5))
bpy.context.active_object.data.energy = 3.0

# Opaque gray surface material on the cube so the inverted filter result is obvious.
surf_mat = bpy.data.materials.new("Surface")
surf_mat.use_nodes = True
cube.data.materials.append(surf_mat)


def render(name):
    path = f"{OUT}/{name}.png"
    scene.render.filepath = path
    print(f"BEFORE_RENDER {name}", flush=True)
    bpy.ops.render.render(write_still=True)
    print(f"AFTER_RENDER {name}", flush=True)
    check(os.path.exists(path), f"render output written: {name}")
    return path


def load_pixels(path):
    image = bpy.data.images.load(path, check_existing=True)
    return list(image.pixels), image.size[0], image.size[1]


def mean_rgb_diff(pixels_a, pixels_b):
    total = 0.0
    count = 0
    # RGBA float pixels.
    for i in range(0, len(pixels_a), 4):
        total += abs(pixels_a[i] - pixels_b[i])
        total += abs(pixels_a[i + 1] - pixels_b[i + 1])
        total += abs(pixels_a[i + 2] - pixels_b[i + 2])
        count += 3
    return total / max(count, 1)


# ------------------------------------------------------------------ baseline
render("01_baseline")

# ------------------------------------------- filter material A: image sample
mat_a = bpy.data.materials.new("Filter A")
mat_a.use_fake_user = True
mat_a.use_nodes = True
mat_a.eevee_domain = "FILTER"
nt_a = mat_a.node_tree
nt_a.nodes.clear()

pass_in = nt_a.nodes.new("ShaderNodeFilterGraphInput")
pass_in.interface_items.new("Color")

sample = nt_a.nodes.new("ShaderNodeNPRImageSample")

invert = nt_a.nodes.new("ShaderNodeInvert")
invert.inputs["Fac"].default_value = 1.0

out_a = nt_a.nodes.new("ShaderNodeOutputFilter")

nt_a.links.new(pass_in.outputs["Image_0"], sample.inputs["Image"])
nt_a.links.new(sample.outputs["Color"], invert.inputs["Color"])
nt_a.links.new(invert.outputs["Color"], out_a.inputs["Color"])

graph = bpy.data.node_groups.new("Filter Graph", "EeveeFilterGraphNodeTree")
graph_scene_color = graph.nodes.new("EeveeFilterGraphNodeSceneColor")
filter_pass_a = graph.nodes.new("EeveeFilterGraphNodeFilterMaterial")
filter_pass_a.material = mat_a
stage_output = graph.nodes.new("EeveeFilterGraphNodeStageOutput")

# Assigning the material is supposed to sync the pass input sockets; create the
# item manually if the update callback did not run in background mode.
if "Color" not in filter_pass_a.inputs:
    filter_pass_a.input_items.new("Color")

graph.links.new(graph_scene_color.outputs["Color Image"], filter_pass_a.inputs["Color"])
graph.links.new(filter_pass_a.outputs["Image"], stage_output.inputs["Image"])
scene.eevee.filter_graph = graph

render("02_image_sample_invert")

# ------------------------------------------- filter material B: scene color
# Rewire the same graph to invoke material B.
mat_b = bpy.data.materials.new("Filter B")
mat_b.use_fake_user = True
mat_b.use_nodes = True
mat_b.eevee_domain = "FILTER"
nt_b = mat_b.node_tree
nt_b.nodes.clear()

scene_color_mat = nt_b.nodes.new("ShaderNodeSceneColor")
out_b = nt_b.nodes.new("ShaderNodeOutputFilter")
nt_b.links.new(scene_color_mat.outputs["Color"], out_b.inputs["Color"])

filter_pass_b = graph.nodes.new("EeveeFilterGraphNodeFilterMaterial")
filter_pass_b.material = mat_b

graph.links.remove(graph_scene_color.outputs["Color Image"].links[0])
graph.links.remove(filter_pass_a.outputs["Image"].links[0])
graph.links.new(filter_pass_b.outputs["Image"], stage_output.inputs["Image"])
graph.nodes.remove(filter_pass_a)

render("03_scene_color_passthrough")

# ----------------------------------------------------------------- compare
base_px, _, _ = load_pixels(f"{OUT}/01_baseline.png")
invert_px, _, _ = load_pixels(f"{OUT}/02_image_sample_invert.png")
pass_px, _, _ = load_pixels(f"{OUT}/03_scene_color_passthrough.png")

diff_invert = mean_rgb_diff(base_px, invert_px)
diff_passthrough = mean_rgb_diff(base_px, pass_px)
print(f"[STAT] mean RGB diff baseline vs inverted   = {diff_invert:.4f}")
print(f"[STAT] mean RGB diff baseline vs passthrough = {diff_passthrough:.4f}")

check(diff_invert > 0.05, "Image Sample filter graph output differs from baseline (invert applied)")
check(diff_passthrough < 0.10, "Scene Color passthrough matches baseline")

if failures:
    print(f"F3D FAIL: {len(failures)} check(s) failed")
    sys.exit(1)

print("F3D OK: end-to-end filter graph validation passed")
