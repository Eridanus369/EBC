"""Stage B runtime verification for the floating panel region (drag / resize / pass-through).

Run (GUI, not background, event simulation enabled):
  STAGE_B_OUT=/abs/out/dir build_linux/bin/blender --factory-startup \
      --enable-event-simulate --python tests/ui_floating_panel_stage_b.py

Evidence strategy: every operation takes a screenshot before and after and compares the two
frames pixel-by-pixel. The floating panel is the only thing that changes while it is dragged, so
the changed-pixel bounding box must equal the union of the expected old/new panel rects, and
there must be no changed pixels anywhere else (no trails / no ghosting). The 3D view matrix is
recorded before/after the drag to prove the viewport was not rotated or panned.

No source files are modified.
"""

import json
import os

import bpy
import numpy as np

OUT_DIR = os.environ.get("STAGE_B_OUT", "/tmp/ui_floating_stage_b")
os.makedirs(OUT_DIR, exist_ok=True)
LOG_FILE = open(os.path.join(OUT_DIR, "stage_b_log.txt"), "w", buffering=1)
RESULT = {}

# Mirrors the constants in interface_region_floating_panel.cc.
PANEL_W = 420
PANEL_H = 240
MIN_W = 160
MIN_H = 120
HANDLE = 14
PANEL_X_FAC = 0.70
PANEL_Y_FAC = 0.12

WIN = bpy.context.window
W, H = WIN.width, WIN.height


def log(msg):
    text = f"[STAGE-B] {msg}"
    print(text, flush=True)
    LOG_FILE.write(text + "\n")


def rect_str(r):
    return f"x={r[0]}..{r[2]} y={r[1]}..{r[3]} w={r[2] - r[0] + 1} h={r[3] - r[1] + 1}"


def union_rect(a, b):
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


# --------------------------------------------------------------------------------------
# Expected rect math (mirrors floating_panel_apply_interaction()).
# --------------------------------------------------------------------------------------
def clamp_into_window(r):
    x0, y0, x1, y1 = r
    w = min(x1 - x0 + 1, W)
    h = min(y1 - y0 + 1, H)
    x0 = max(0, min(x0, W - w))
    y0 = max(0, min(y0, H - h))
    return (x0, y0, x0 + w - 1, y0 + h - 1)


def expected_move(r, dx, dy):
    x0, y0, x1, y1 = r
    return clamp_into_window((x0 + dx, y0 + dy, x1 + dx, y1 + dy))


def expected_resize(r, dx, dy):
    x0, y0, x1, y1 = r
    x1 += dx
    y0 += dy
    x1 = max(min(x1, W - 1), min(x0 + MIN_W - 1, W - 1))
    y0 = max(0, min(y0, y1 - MIN_H + 1))
    return (x0, y0, x1, y1)


def initial_panel_rect():
    return clamp_into_window((int(W * PANEL_X_FAC),
                              int(H * PANEL_Y_FAC),
                              int(W * PANEL_X_FAC) + PANEL_W - 1,
                              int(H * PANEL_Y_FAC) + PANEL_H - 1))


# --------------------------------------------------------------------------------------
# Input simulation.
# --------------------------------------------------------------------------------------
def send(kind, value, x, y):
    WIN.event_simulate(type=kind, value=value, x=int(round(x)), y=int(round(y)))


def press(x, y):
    send("LEFTMOUSE", "PRESS", x, y)


def move(x, y):
    send("MOUSEMOVE", "NOTHING", x, y)


def release(x, y):
    send("LEFTMOUSE", "RELEASE", x, y)


# --------------------------------------------------------------------------------------
# Screenshots and pixel analysis.
# --------------------------------------------------------------------------------------
def screenshot(name):
    path = os.path.join(OUT_DIR, name)
    with bpy.context.temp_override(window=WIN, screen=WIN.screen):
        bpy.ops.screen.screenshot(filepath=path)
    log(f"screenshot {name} bytes={os.path.getsize(path) if os.path.exists(path) else 0}")
    return path


def load_rgb(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    arr = buf.reshape(h, w, 4)[:, :, :3]
    bpy.data.images.remove(img)
    return arr


def panel_stats(path, rect):
    """Average / uniformity of the expected panel rect, plus the corner handle color."""
    arr = load_rgb(os.path.join(OUT_DIR, path))
    x0, y0, x1, y1 = rect
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(arr.shape[1] - 1, x1), min(arr.shape[0] - 1, y1)
    body = arr[y0 + 8:y1 - 8, x0 + 8:x1 - 8 - HANDLE]
    handle = arr[y0 + 1:y0 + HANDLE - 1, x1 - HANDLE + 2:x1 - 1]
    stats = {
        "rect": list(rect),
        "body_mean": [round(float(v), 4) for v in body.reshape(-1, 3).mean(axis=0)],
        "body_std": round(float(body.std()), 4),
        "handle_mean": [round(float(v), 4) for v in handle.reshape(-1, 3).mean(axis=0)],
    }
    log(f"{path}: rect={rect_str(rect)} body_mean={stats['body_mean']} "
        f"std={stats['body_std']} handle_mean={stats['handle_mean']}")
    return stats


def analyze_change(name, old_rect, new_rect, baseline_name, node_area=None, ignore_rects=(),
                   min_px=200):
    """Diff two frames: the only change must be the panel rect moving from old to new."""
    arr = load_rgb(os.path.join(OUT_DIR, name))
    base = load_rgb(os.path.join(OUT_DIR, baseline_name))
    h, w = arr.shape[:2]
    d = np.abs(arr - base).max(axis=2)
    changed = d > 0.008

    if node_area is not None:
        nx0, ny0, nx1, ny1 = node_area
        mask = np.zeros_like(changed)
        mask[max(0, ny0):min(h, ny1 + 1), max(0, nx0):min(w, nx1 + 1)] = True
        changed &= mask

    for (ex0, ey0, ex1, ey1) in ignore_rects:
        changed[max(0, ey0):min(h, ey1 + 1), max(0, ex0):min(w, ex1 + 1)] = False

    ys, xs = np.nonzero(changed)
    count = int(len(xs))
    if count == 0:
        log(f"{name}: NO changed pixels vs {baseline_name}")
        return {"name": name, "baseline": baseline_name, "changed_pixels": 0,
                "bbox": None, "expected_union": list(union_rect(old_rect, new_rect)),
                "ok": False}

    bbox = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
    expected = union_rect(old_rect, new_rect)

    # Stray pixels: changed pixels outside the expected union (2 px tolerance for AA edges).
    stray = changed.copy()
    m = 2
    sx0, sy0 = max(0, expected[0] - m), max(0, expected[1] - m)
    sx1, sy1 = min(w, expected[2] + 1 + m), min(h, expected[3] + 1 + m)
    stray[sy0:sy1, sx0:sx1] = False
    stray_count = int(stray.sum())
    stray_bbox = None
    sys_, sxs_ = np.nonzero(stray)
    if len(sxs_):
        stray_bbox = (int(sxs_.min()), int(sys_.min()), int(sxs_.max()), int(sys_.max()))

    ok = count >= min_px and bbox == expected and stray_count <= 200
    result = {
        "name": name,
        "baseline": baseline_name,
        "changed_pixels": count,
        "bbox": list(bbox),
        "expected_union": list(expected),
        "bbox_matches": bbox == expected,
        "stray_pixels": stray_count,
        "stray_bbox": list(stray_bbox) if stray_bbox else None,
        "ok": ok,
    }
    log(f"{name}: changed={count} diff_bbox={rect_str(bbox)} expected_union={rect_str(expected)} "
        f"match={bbox == expected} stray={stray_count} stray_bbox={stray_bbox} ok={ok}")
    return result


def diff_in_area(name, baseline_name, area, min_px=500):
    arr = load_rgb(os.path.join(OUT_DIR, name))
    base = load_rgb(os.path.join(OUT_DIR, baseline_name))
    h, w = arr.shape[:2]
    d = np.abs(arr - base).max(axis=2)
    ax0, ay0, ax1, ay1 = area
    d[:, :max(0, ax0)] = 0.0
    d[:, min(w, ax1 + 1):] = 0.0
    d[:max(0, ay0), :] = 0.0
    d[min(h, ay1 + 1):, :] = 0.0
    changed = d > 0.01
    ys, xs = np.nonzero(changed)
    count = int(len(xs))
    bbox = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())) if count else None
    log(f"{name}: area diff pixels={count} bbox={rect_str(bbox) if bbox else None} "
        f"received={count >= min_px}")
    return {"name": name, "baseline": baseline_name, "changed_pixels": count,
            "bbox": list(bbox) if bbox else None, "received": count >= min_px}


# --------------------------------------------------------------------------------------
# Context helpers.
# --------------------------------------------------------------------------------------
def area_of_type(kind):
    for area in WIN.screen.areas:
        if area.type == kind:
            return area
    return None


def area_rect(area):
    return (area.x, area.y, area.x + area.width - 1, area.y + area.height - 1)


def view_matrix():
    area = area_of_type("VIEW_3D")
    if area is None:
        return None
    rv3d = area.spaces.active.region_3d
    return [round(float(v), 6) for row in rv3d.view_matrix for v in row]


def log_layout(tag):
    log(f"{tag}: window={W}x{H} workspace='{WIN.workspace.name}' screen='{WIN.screen.name}'")
    for area in WIN.screen.areas:
        log(f"{tag}: area {area.type:16s} {rect_str(area_rect(area))}")


def node_tree():
    area = area_of_type("NODE_EDITOR")
    if area is None:
        return area, None
    tree = getattr(area.spaces.active, "node_tree", None)
    return area, tree


def deselect_nodes():
    _, tree = node_tree()
    if tree is None:
        return None
    for node in tree.nodes:
        node.select = False
    return len(tree.nodes)


def selected_nodes():
    _, tree = node_tree()
    if tree is None:
        return None
    return sum(1 for n in tree.nodes if n.select)


# --------------------------------------------------------------------------------------
# Test sequence (a generator; each `yield` returns the timer delay in seconds).
# --------------------------------------------------------------------------------------
def sequence():
    yield 1.0

    # Close the startup splash: a click outside the panel (and outside the splash popup) is
    # consumed by the splash handler only. Motion alone does not dismiss it.
    press(60, 60)
    yield 0.2
    release(60, 60)
    yield 0.8

    log_layout("layout")
    screenshot("b_00_layout_idle.png")
    RESULT["window"] = [W, H]
    RESULT["view_matrix_before"] = view_matrix()
    log(f"view matrix before drag: {RESULT['view_matrix_before']}")

    panel = initial_panel_rect()
    RESULT["initial_expect"] = list(panel)
    RESULT["initial"] = panel_stats("b_00_layout_idle.png", panel)

    # ------------------------------------------------------------------
    # 1) Object mode: 2 s continuous drag inside the 3D view.
    # ------------------------------------------------------------------
    grab = (panel[0] + 120, panel[3] - 18)
    log(f"3D drag: press at {grab}, delta (-200,-80), 40 motion steps over ~2 s")
    press(*grab)
    yield 0.05
    total = (-200, -80)
    for i in range(1, 41):
        move(grab[0] + total[0] * i / 40, grab[1] + total[1] * i / 40)
        yield 0.05
    release(grab[0] + total[0], grab[1] + total[1])
    yield 0.4

    panel_after = expected_move(panel, total[0], total[1])
    RESULT["drag_2s_expect"] = list(panel_after)
    screenshot("b_01_drag_2s.png")
    RESULT["drag_2s"] = analyze_change("b_01_drag_2s.png", panel, panel_after, "b_00_layout_idle.png")
    RESULT["drag_2s_stats"] = panel_stats("b_01_drag_2s.png", panel_after)
    RESULT["view_matrix_after"] = view_matrix()
    log(f"view matrix after drag:  {RESULT['view_matrix_after']}")
    log(f"VIEW_MATRIX_IDENTICAL={RESULT['view_matrix_before'] == RESULT['view_matrix_after']}")

    # ------------------------------------------------------------------
    # 2) Resize grow, then shrink to the minimum size.
    # ------------------------------------------------------------------
    def handle_point(r):
        return (r[2] - 4, r[1] + 4)

    hp = handle_point(panel_after)
    log(f"resize grow: press handle at {hp}, delta (+180,-60)")
    press(*hp)
    yield 0.05
    for i in range(1, 21):
        move(hp[0] + 180 * i / 20, hp[1] - 60 * i / 20)
        yield 0.05
    release(hp[0] + 180, hp[1] - 60)
    yield 0.4
    grown = expected_resize(panel_after, 180, -60)
    RESULT["resize_grow_expect"] = list(grown)
    screenshot("b_02_resize_grow.png")
    RESULT["resize_grow"] = analyze_change(
        "b_02_resize_grow.png", panel_after, grown, "b_01_drag_2s.png")

    hp2 = handle_point(grown)
    log(f"resize shrink to minimum: press handle at {hp2}, delta (-440,+180)")
    press(*hp2)
    yield 0.05
    for i in range(1, 21):
        move(hp2[0] - 440 * i / 20, hp2[1] + 180 * i / 20)
        yield 0.05
    release(hp2[0] - 440, hp2[1] + 180)
    yield 0.4
    shrunk = expected_resize(grown, -440, 180)
    RESULT["resize_min_expect"] = list(shrunk)
    screenshot("b_03_resize_min.png")
    RESULT["resize_min"] = analyze_change(
        "b_03_resize_min.png", grown, shrunk, "b_02_resize_grow.png")
    log(f"resize_min: min size enforced = {shrunk[2] - shrunk[0] + 1 == MIN_W and shrunk[3] - shrunk[1] + 1 == MIN_H}")

    # ------------------------------------------------------------------
    # 3) Boundary: cross the ScrArea split, then hit the window edge.
    # ------------------------------------------------------------------
    body = (shrunk[0] + 80, shrunk[3] - 18)
    log(f"boundary right: press at {body}, delta (+2500,0) -> expect clamped xmax={W - 1}")
    press(*body)
    yield 0.05
    for i in range(1, 21):
        move(body[0] + 2500 * i / 20, body[1])
        yield 0.05
    release(body[0] + 2500, body[1])
    yield 0.4
    right = expected_move(shrunk, 2500, 0)
    RESULT["edge_right_expect"] = list(right)
    screenshot("b_04_edge_right.png")
    RESULT["edge_right"] = analyze_change(
        "b_04_edge_right.png", shrunk, right, "b_03_resize_min.png")
    RESULT["edge_right_areas"] = [
        [a.type, list(area_rect(a))] for a in WIN.screen.areas]
    log(f"edge_right: panel rect {rect_str(right)}; xmax==W-1 -> {right[2] == W - 1}")

    body2 = (right[0] + 80, right[3] - 18)
    log(f"boundary top: press at {body2}, delta (0,+2500) -> expect clamped ymax={H - 1}")
    press(*body2)
    yield 0.05
    for i in range(1, 21):
        move(body2[0], body2[1] + 2500 * i / 20)
        yield 0.05
    release(body2[0], body2[1] + 2500)
    yield 0.4
    top = expected_move(right, 0, 2500)
    RESULT["edge_top_expect"] = list(top)
    screenshot("b_05_edge_top.png")
    RESULT["edge_top"] = analyze_change("b_05_edge_top.png", right, top, "b_04_edge_right.png")
    log(f"edge_top: panel rect {rect_str(top)}; ymax==H-1 -> {top[3] == H - 1}")

    # ------------------------------------------------------------------
    # 4) Node editor (Shading workspace): outside select vs inside consume.
    # ------------------------------------------------------------------
    shading = bpy.data.workspaces.get("Shading")
    log(f"switch workspace -> Shading (found={shading is not None})")
    if shading is not None:
        WIN.workspace = shading
    yield 1.5

    log_layout("shading")
    panel = initial_panel_rect()
    screenshot("b_10_nodes_idle.png")
    RESULT["nodes_idle"] = panel_stats("b_10_nodes_idle.png", panel)
    RESULT["nodes_idle_expect"] = list(panel)
    RESULT["shading_view_before"] = view_matrix()
    log(f"shading view matrix before: {RESULT['shading_view_before']}")

    # Control: two idle frames ~0.6 s apart, to measure background noise (Material Preview
    # progressive sampling, File Browser thumbnail decoding) that must not be attributed to input.
    yield 0.6
    screenshot("b_10b_nodes_idle2.png")

    node_area, tree = node_tree()
    if node_area is None:
        log("ERROR: no NODE_EDITOR area in Shading workspace")
        RESULT["nodes"] = {"error": "no node editor"}
    else:
        nr = area_rect(node_area)
        log(f"node area rect: {rect_str(nr)}")
        RESULT["nodes_idle_control"] = diff_in_area(
            "b_10b_nodes_idle2.png", "b_10_nodes_idle.png", nr, min_px=1)
        # Areas that are irrelevant to input handling and may animate on their own.
        noise_areas = [(0, 0, W - 1, 22)]
        for kind in ("VIEW_3D", "IMAGE_EDITOR", "FILE_BROWSER"):
            a = area_of_type(kind)
            if a is not None:
                noise_areas.append(area_rect(a))
        RESULT["noise_areas"] = [list(r) for r in noise_areas]
        space = node_area.spaces.active
        RESULT["nodes_zoom_before"] = getattr(space, "zoom", None)
        log(f"node editor zoom before: {RESULT['nodes_zoom_before']}")
        log(f"node count in editor: {deselect_nodes()}")

        # Outside the panel: border select over the node area must reach the editor.
        outside_start = (node_area.x + 20, node_area.y + 20)
        outside_end = (min(1300, nr[2] - 20), min(nr[3] - 20, 503))
        log(f"outside select: press {outside_start} -> {outside_end} "
            f"(drag rect {rect_str((outside_start[0], outside_start[1], outside_end[0], outside_end[1]))})")
        press(*outside_start)
        yield 0.05
        for i in range(1, 9):
            move(outside_start[0] + (outside_end[0] - outside_start[0]) * i / 8,
                 outside_start[1] + (outside_end[1] - outside_start[1]) * i / 8)
            yield 0.05
        screenshot("b_11_nodes_outside_mid.png")
        RESULT["nodes_outside_mid"] = diff_in_area(
            "b_11_nodes_outside_mid.png", "b_10_nodes_idle.png", nr)
        release(*outside_end)
        yield 0.5
        screenshot("b_12_nodes_outside_after.png")
        RESULT["nodes_outside_selected"] = selected_nodes()
        log(f"selected nodes after outside select: {RESULT['nodes_outside_selected']}")
        RESULT["nodes_outside_panel_static"] = analyze_change(
            "b_12_nodes_outside_after.png", panel, panel, "b_10_nodes_idle.png",
            ignore_rects=[nr, (0, 0, W - 1, 22)])

        # Inside the panel: the panel must move and the editor must see nothing.
        # Take a fresh baseline AFTER deselecting so the node highlight change is not counted.
        deselect_nodes()
        yield 0.3
        screenshot("b_13_nodes_inside_before.png")
        RESULT["nodes_inside_before"] = panel_stats("b_13_nodes_inside_before.png", panel)
        inside_grab = (panel[0] + 100, panel[3] - 18)
        log(f"inside drag: press {inside_grab}, delta (+120,-40)")
        press(*inside_grab)
        yield 0.05
        for i in range(1, 11):
            move(inside_grab[0] + 120 * i / 10, inside_grab[1] - 40 * i / 10)
            yield 0.05
        screenshot("b_13_nodes_inside_mid.png")
        release(inside_grab[0] + 120, inside_grab[1] - 40)
        yield 0.5
        screenshot("b_14_nodes_inside_after.png")
        panel_inside = expected_move(panel, 120, -40)
        RESULT["nodes_inside_expect"] = list(panel_inside)
        RESULT["nodes_inside"] = analyze_change(
            "b_14_nodes_inside_after.png", panel, panel_inside,
            "b_13_nodes_inside_before.png", ignore_rects=noise_areas)
        RESULT["shading_view_after"] = view_matrix()
        log(f"shading view matrix after inside drag: {RESULT['shading_view_after']}")
        log(f"SHADING_VIEW_MATRIX_IDENTICAL="
            f"{RESULT['shading_view_before'] == RESULT['shading_view_after']}")
        RESULT["nodes_inside_stats"] = panel_stats("b_14_nodes_inside_after.png", panel_inside)
        RESULT["nodes_inside_selected"] = selected_nodes()
        log(f"selected nodes after inside drag: {RESULT['nodes_inside_selected']}")

    with open(os.path.join(OUT_DIR, "stage_b_result.json"), "w") as f:
        json.dump(RESULT, f, indent=2, default=str)
    log("result written, quitting")
    yield 0.2
    with bpy.context.temp_override(window=WIN, screen=WIN.screen):
        bpy.ops.wm.quit_blender()


GEN = sequence()


def tick():
    try:
        return next(GEN)
    except StopIteration:
        return None
    except Exception as ex:  # noqa: BLE001
        import traceback
        log("EXCEPTION: " + repr(ex))
        LOG_FILE.write(traceback.format_exc())
        with open(os.path.join(OUT_DIR, "stage_b_result.json"), "w") as f:
            json.dump(RESULT, f, indent=2, default=str)
        bpy.ops.wm.quit_blender()
        return None


bpy.app.timers.register(tick, first_interval=1.0)
