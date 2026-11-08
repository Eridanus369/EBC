"""Stage F milestone 1: left-click inside the floating panel must not reach the editor below.

Setup: the area under the floating panel is turned into a real 3D viewport with the default
selection (Cube, Light, Camera). The panel hosts the native node editor, so the blank canvas
inside the panel is the exact spot where `LEFTMOUSE` is passed through on purpose (to keep the
window-level `event_queue_check_drag` alive for native node drag/link/box-select).

Checks:
  1. select-all three objects, then click blank node canvas inside the panel: object selection
     must stay unchanged (before the fix the 3D viewport below runs `view3d.select` and
     deselects/reselects).
  2. dragging the panel title bar still moves the panel (window-level drag detection kept).
  3. native node link drag still works (`node.link` is a `CLICK_DRAG` binding, i.e. it needs the
     same window-level drag flag that check 1 must not break).

Run (GUI, event simulation enabled):
  STAGE_F_OUT=/abs/out/dir EBC_FP_STATE_FILE=/tmp/fp_state_f.json \
  build_linux/bin/blender --factory-startup --enable-event-simulate \
  --python tests/ui_floating_panel_stage_f_m1.py
"""

import json
import os
import time

import bpy
import numpy as np
from bpy_extras.view3d_utils import location_3d_to_region_2d, region_2d_to_location_3d

OUT_DIR = os.environ.get("STAGE_F_OUT", "/tmp/ui_floating_stage_f")
os.makedirs(OUT_DIR, exist_ok=True)
LOG_FILE = open(os.path.join(OUT_DIR, "stage_f_m1_log.txt"), "w", buffering=1)
RESULT = {}

STATE_FILE = os.environ["EBC_FP_STATE_FILE"]
WIN = bpy.context.window
W, H = WIN.width, WIN.height


def log(msg):
    text = f"[STAGE-F-M1] {msg}"
    print(text, flush=True)
    LOG_FILE.write(text + "\n")


def read_state():
    for _ in range(10):
        try:
            with open(STATE_FILE) as fh:
                return json.loads(fh.read())
        except (OSError, json.JSONDecodeError):
            time.sleep(0.004)
    return None


def redraw(n=2):
    try:
        with bpy.context.temp_override(window=WIN, screen=WIN.screen):
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=n)
    except Exception as exc:  # noqa: BLE001
        log(f"redraw_timer failed: {exc}")


def sim(kind, val, x, y):
    WIN.event_simulate(type=kind, value=val, x=int(x), y=int(y))


def move(x, y):
    sim("MOUSEMOVE", "NOTHING", x, y)


def click(x, y):
    sim("LEFTMOUSE", "PRESS", x, y)
    sim("LEFTMOUSE", "RELEASE", x, y)


def schedule(fn, delay):
    bpy.app.timers.register(fn, first_interval=delay)


def screenshot(name):
    path = os.path.join(OUT_DIR, name)
    with bpy.context.temp_override(window=WIN, screen=WIN.screen):
        bpy.ops.screen.screenshot(filepath=path)
    if not os.path.exists(path):
        log(f"screenshot MISSING {name}")
    return path


def inside(x, y, rect):
    return rect[0] <= x <= rect[1] and rect[2] <= y <= rect[3]


def view3d_region_under(x, y):
    """(area, region, rv3d) of the non-node 3D viewport under a window point."""
    for area in bpy.context.screen.areas:
        if area.type != "VIEW_3D":
            continue
        if not (area.x <= x < area.x + area.width and area.y <= y < area.y + area.height):
            continue
        for region in area.regions:
            if region.type != "WINDOW":
                continue
            if not (region.x <= x < region.x + region.width and
                    region.y <= y < region.y + region.height):
                continue
            return area, region, area.spaces.active.region_3d
    return None, None, None


def make_probe(point):
    """Small cube placed exactly under `point` in the viewport below the panel, so a leaked
    press hits it and changes the object selection."""
    area, region, rv3d = view3d_region_under(point[0], point[1])
    if region is None or rv3d is None:
        log("FATAL: no 3D viewport under the click point")
        return None
    depth = bpy.data.objects.get("Cube")
    depth_loc = tuple(depth.matrix_world.translation) if depth else (0.0, 0.0, 0.0)
    world = region_2d_to_location_3d(
        region, rv3d, (point[0] - region.x, point[1] - region.y), depth_loc)
    s = 0.35
    verts = [(-s, -s, -s), (s, -s, -s), (s, s, -s), (-s, s, -s),
             (-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s)]
    faces = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 4, 7, 3), (1, 5, 6, 2), (0, 1, 5, 4), (3, 2, 6, 7)]
    mesh = bpy.data.meshes.new("FProbeMesh")
    mesh.from_pydata(verts, [], faces)
    obj = bpy.data.objects.new("FLeakProbe", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = world
    bpy.context.view_layer.update()
    proj = location_3d_to_region_2d(region, rv3d, world)
    screen = None if proj is None else [int(region.x + proj[0]), int(region.y + proj[1])]
    RESULT["probe_world"] = list(world)
    RESULT["probe_screen"] = screen
    log(f"probe world={world} screen={screen} target={point}")
    return obj


def select_only(name):
    for obj in bpy.data.objects:
        obj.select_set(obj.name == name)


def object_selection():
    return sorted(o.name for o in bpy.data.objects if o.select_get())


def select_all_objects():
    for obj in bpy.data.objects:
        obj.select_set(True)


def find_node(st, name):
    for node in (st or {}).get("nodes", []):
        if node.get("name") == name:
            return node
    return None


def find_socket(node, direction, name):
    for sock in (node or {}).get("sockets", []):
        if sock.get("dir") == direction and sock.get("name") == name:
            return sock
    return None


def area_type_at(x, y):
    """Type of the non-node (lower) area under a window point; the panel's node host is a
    NODE_EDITOR area and is skipped."""
    for area in bpy.context.screen.areas:
        if area.type == "NODE_EDITOR":
            continue
        if area.x <= x < area.x + area.width and area.y <= y < area.y + area.height:
            return area.type
    return None


def blank_canvas_point(st):
    """A point inside the canvas hole that does not hit any node rectangle."""
    nx0, nx1, ny0, ny1 = st["node"]
    node_rects = [n["rect"] for n in st.get("nodes", []) if n.get("rect")]
    candidates = [
        (nx1 - 30, ny0 + 20),
        (nx1 - 30, ny0 + 60),
        (nx0 + 40, ny1 - 25),
        (nx1 - 60, ny1 - 30),
    ]
    for x, y in candidates:
        if not inside(x, y, st["node"]):
            continue
        if any(inside(x, y, r) for r in node_rects):
            continue
        return x, y
    return None


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------
def setup_tree():
    ng = bpy.data.node_groups.new("StageF_Geometry", "GeometryNodeTree")
    ng.interface.new_socket(name="Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
    ng.interface.new_socket(name="Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    gi = ng.nodes.new("NodeGroupInput")
    sp = ng.nodes.new("GeometryNodeSetPosition")
    go = ng.nodes.new("NodeGroupOutput")
    gi.location = (-220.0, 60.0)
    sp.location = (-10.0, -10.0)
    go.location = (170.0, -110.0)
    ng.links.new(gi.outputs["Geometry"], sp.inputs["Geometry"])
    RESULT["tree_nodes"] = [[n.name, list(n.location)] for n in ng.nodes]
    log(f"created tree {ng.name} nodes={RESULT['tree_nodes']} links={len(ng.links)}")


def close_splash():
    log("closing splash")
    click(700, 500)
    sim("ESC", "PRESS", 700, 500)
    sim("ESC", "RELEASE", 700, 500)
    schedule(setup, 1.2)


def setup():
    try:
        setup_tree()
    except Exception as exc:  # noqa: BLE001
        RESULT["setup_error"] = str(exc)
        log(f"setup failed: {exc}")
    move(80, 40)
    schedule(idle, 0.6)


def idle():
    redraw(2)
    st = read_state()
    if st is None or st.get("view") != "anchor":
        schedule(idle, 0.4)
        return
    RESULT["initial_state"] = st
    log(f"state view={st['view']} anchor={st['anchor']}")
    schedule(expand_click, 0.25)


def expand_click():
    st = read_state()
    a = st["anchor"]
    cx, cy = (a[0] + a[1]) // 2, (a[2] + a[3]) // 2
    log(f"expand click {cx},{cy}")
    click(cx, cy)
    schedule(poll_expanded, 0.05)


def poll_expanded():
    st = read_state()
    if st and st.get("view") == "expanded" and not st.get("anim"):
        RESULT["expanded_state"] = st
        log(f"expanded rect={st['rect']} host={st['host']} node={st['node']}")
        schedule(poll_host, 0.15)
        return
    schedule(poll_expanded, 0.04)


def poll_host():
    st = read_state()
    if st is None:
        schedule(poll_host, 0.1)
        return
    nodes = [n for n in st.get("nodes", []) if n.get("rect") and
             n["rect"][1] - n["rect"][0] > 20 and n["rect"][3] - n["rect"][2] > 10]
    if st.get("host") and len(nodes) >= 3:
        RESULT["host_state"] = st
        RESULT["node_rects"] = {n["name"]: n["rect"] for n in nodes}
        log(f"host live tree={st.get('tree')} nodes={RESULT['node_rects']} "
            f"links={st.get('links')}")
        move(80, 40)
        redraw(2)
        schedule(title_drag_start, 0.2)
        return
    schedule(poll_host, 0.1)


# ---------------------------------------------------------------------------
# Check 1: click blank canvas inside the panel
# ---------------------------------------------------------------------------
def install_leak_marker():
    """Test-only keymap probe: bind LEFTMOUSE PRESS to `view3d.select` so a press that leaks
    through the panel selects immediately. The default map only selects on CLICK, and the
    release is swallowed by the blocker even in the old build, so the default map cannot expose
    whether the press reached the lower editor. With this binding "lower editor received the
    press" becomes directly observable (selection changes)."""
    try:
        kc = bpy.context.window_manager.keyconfigs.active
        km = kc.keymaps.get("3D View")
        item = km.keymap_items.new("view3d.select", 'LEFTMOUSE', 'PRESS')
        item.properties.deselect_all = True
        RESULT["leak_marker"] = True
        log("installed LEFTMOUSE PRESS -> view3d.select probe binding")
    except Exception as exc:  # noqa: BLE001
        RESULT["leak_marker_error"] = str(exc)
        log(f"leak marker failed: {exc}")


def click_through_before():
    st = read_state()
    point = blank_canvas_point(st)
    if point is None:
        log("FATAL: no blank canvas point found")
        finish()
        return
    install_leak_marker()
    # Nothing selected in the node tree: the native `node.select` operator on an empty canvas
    # then returns PASS_THROUGH (so that a CLICK_DRAG can box-select), which is exactly the
    # path where the press can reach the editor below.
    tree = bpy.data.node_groups.get("StageF_Geometry")
    if tree is not None:
        for node in tree.nodes:
            node.select = False
    # Put a selectable object exactly under the canvas hole so a leaked press selects it.
    probe = make_probe(point)
    select_only("Camera")
    # Make sure the selection is really applied and the viewport drew once.
    redraw(2)
    st = read_state()
    RESULT["click_point"] = list(point)
    RESULT["click_area_type"] = area_type_at(point[0], point[1])
    RESULT["selection_before"] = object_selection()
    RESULT["node_sel_before"] = sorted(n["name"] for n in st.get("nodes", []) if n.get("sel"))
    RESULT["rect_before_click"] = st["rect"]
    RESULT["node_rect_at_click"] = st["node"]
    RESULT["click_rel_to_rect"] = [point[0] - st["rect"][0], point[1] - st["rect"][2]]
    log(f"click blank canvas at {point} node={st['node']} "
        f"probe={probe.name if probe else None} "
        f"selection_before={RESULT['selection_before']} "
        f"node_sel_before={RESULT['node_sel_before']}")
    screenshot("f_m1_01_before_click.png")
    sim("LEFTMOUSE", "PRESS", point[0], point[1])
    # What owns the press while it is down? A running node-editor modal operator would be the
    # reason the lower viewport never sees the press.
    redraw(1)
    RESULT["modal_after_press"] = (read_state() or {}).get("modal")
    RESULT["window_modal_ops"] = ([op.name for op in WIN.modal_operators]
                                  if hasattr(WIN, "modal_operators") else None)
    log(f"after press modal={RESULT['modal_after_press']} "
        f"modal_ops={RESULT['window_modal_ops']}")
    schedule(click_through_release, 0.12)


def click_through_release():
    point = RESULT["click_point"]
    sim("LEFTMOUSE", "RELEASE", point[0], point[1])
    schedule(click_through_after, 0.35)


def click_through_after():
    st = read_state()
    after = object_selection()
    node_after = sorted(n["name"] for n in (st or {}).get("nodes", []) if n.get("sel"))
    RESULT["selection_after"] = after
    RESULT["node_sel_after"] = node_after
    RESULT["rect_after_click"] = (st or {}).get("rect")
    changed = RESULT["selection_before"] != after
    RESULT["click_through_leak"] = bool(changed)
    RESULT["click_through_ok"] = bool(not changed)
    log(f"selection_after={after} changed={changed} node_sel_after={node_after} "
        f"rect={RESULT['rect_after_click']}")
    # Control: can the same point be picked by `view3d.select` when invoked directly? If yes,
    # the 3D viewport really has a selectable object under the canvas hole.
    point = RESULT["click_point"]
    area, region, _rv3d = view3d_region_under(point[0], point[1])
    if region is not None:
        try:
            with bpy.context.temp_override(window=WIN, screen=WIN.screen,
                                           area=area, region=region):
                ret = bpy.ops.view3d.select(
                    location=(point[0] - region.x, point[1] - region.y), deselect_all=True)
            RESULT["direct_select_ret"] = list(ret)
            RESULT["direct_select_selection"] = object_selection()
        except Exception as exc:  # noqa: BLE001
            RESULT["direct_select_error"] = str(exc)
        log(f"direct view3d.select {RESULT.get('direct_select_ret')} -> "
            f"{RESULT.get('direct_select_selection')} "
            f"{RESULT.get('direct_select_error', '')}")
    redraw(2)
    screenshot("f_m1_02_after_click.png")
    schedule(control_click_start, 0.2)


# ---------------------------------------------------------------------------
# Control: a click in the 3D view outside the panel really does change selection
# ---------------------------------------------------------------------------
def control_click_start():
    RESULT["control_selection_before"] = object_selection()
    log(f"control click 300,900 selection={RESULT['control_selection_before']}")
    click(300, 900)
    schedule(control_click_check, 0.35)


def control_click_check():
    after = object_selection()
    RESULT["control_selection_after"] = after
    RESULT["control_ok"] = bool(RESULT["control_selection_before"] != after)
    log(f"control selection {RESULT['control_selection_before']} -> {after} "
        f"ok={RESULT['control_ok']}")
    redraw(2)
    screenshot("f_m1_05_control_click.png")
    schedule(box_select_start, 0.2)


# ---------------------------------------------------------------------------
# Regression: native node box-select is a CLICK_DRAG binding too
# ---------------------------------------------------------------------------
def box_select_start():
    st = read_state()
    point = blank_canvas_point(st)
    if point is None:
        log("FATAL: no blank canvas point for box select")
        finish()
        return
    nx0, _nx1, _ny0, ny1 = st["node"]
    target = (nx0 + 30, ny1 - 30)
    tree = bpy.data.node_groups.get("StageF_Geometry")
    if tree is not None:
        for node in tree.nodes:
            node.select = False
    RESULT["box_start"] = list(point)
    RESULT["box_target"] = list(target)
    log(f"box select {point} -> {target}")
    sim("LEFTMOUSE", "PRESS", point[0], point[1])
    for i in range(1, 11):
        move(point[0] + (target[0] - point[0]) * i / 10,
             point[1] + (target[1] - point[1]) * i / 10)
    sim("LEFTMOUSE", "RELEASE", target[0], target[1])
    schedule(box_select_check, 0.35)


def box_select_check():
    st = read_state()
    selected = sorted(n["name"] for n in (st or {}).get("nodes", []) if n.get("sel"))
    RESULT["box_selected_nodes"] = selected
    RESULT["box_sel_ok"] = bool(len(selected) > 0)
    log(f"box selected {selected} ok={RESULT['box_sel_ok']}")
    redraw(2)
    screenshot("f_m1_06_box_select.png")
    schedule(link_drag_start, 0.25)


# ---------------------------------------------------------------------------
# Check 2: title-bar drag still moves the panel
# ---------------------------------------------------------------------------
def title_drag_start():
    """Drag the title bar far enough to the left that the panel canvas sits over the main 3D
    viewport. Doubles as the "title-bar drag still works" check."""
    st = read_state()
    p = st["rect"]
    x, y = p[0] + 100, p[3] - 8
    target_x = 604
    RESULT["title_drag_before"] = p
    log(f"title drag press {x},{y} rect={p}")
    sim("LEFTMOUSE", "PRESS", x, y)
    steps = 10
    for i in range(1, steps + 1):
        move(x + (target_x - x) * i / steps, y)
    sim("LEFTMOUSE", "RELEASE", target_x, y)
    schedule(title_drag_check, 0.35)


def title_drag_check():
    st = read_state()
    before = RESULT["title_drag_before"]
    after = st["rect"]
    RESULT["title_drag_after"] = after
    RESULT["node_rect_after_drag"] = st["node"]
    moved = before[0] != after[0] or before[2] != after[2]
    RESULT["title_drag_ok"] = bool(moved)
    log(f"title drag {before} -> {after} moved={moved} node={st['node']}")
    redraw(2)
    screenshot("f_m1_03_title_drag.png")
    schedule(resize_start, 0.25)


def resize_start():
    st = read_state()
    p = st["rect"]
    x, y = p[1] - 7, p[2] + 7
    RESULT["resize_before"] = p
    log(f"resize press {x},{y} rect={p}")
    sim("LEFTMOUSE", "PRESS", x, y)
    for i in range(1, 6):
        move(x + 6 * i, y - 4 * i)
    sim("LEFTMOUSE", "RELEASE", x + 30, y - 20)
    schedule(resize_check, 0.3)


def resize_check():
    st = read_state()
    before = RESULT["resize_before"]
    after = st["rect"]
    RESULT["resize_after"] = after
    grown = (after[1] - after[0]) > (before[1] - before[0]) and \
        (after[3] - after[2]) > (before[3] - before[2])
    RESULT["resize_ok"] = bool(grown)
    log(f"resize {before} -> {after} grown={grown}")
    redraw(2)
    screenshot("f_m1_04_resize.png")
    schedule(click_through_before, 0.25)


# ---------------------------------------------------------------------------
# Check 3: native node link drag (CLICK_DRAG) still works
# ---------------------------------------------------------------------------
def link_drag_start():
    st = read_state()
    sp = find_node(st, "Set Position")
    go = find_node(st, "Group Output")
    out = find_socket(sp, "out", "Geometry")
    inp = find_socket(go, "in", "Geometry")
    if out is None or inp is None:
        log(f"FATAL: link sockets missing out={out} in={inp}")
        finish()
        return
    a = out["pos"]
    b = inp["pos"]
    RESULT["link_before"] = st.get("links")
    RESULT["link_socket_out"] = a
    RESULT["link_socket_in"] = b
    log(f"link drag {a} -> {b} links_before={RESULT['link_before']}")
    move(80, 40)
    redraw(2)
    sim("LEFTMOUSE", "PRESS", a[0], a[1])
    RESULT["link_modal_after_press"] = ([op.name for op in WIN.modal_operators]
                                        if hasattr(WIN, "modal_operators") else None)
    dx, dy = (b[0] - a[0]) / 6.0, (b[1] - a[1]) / 6.0
    for i in range(1, 7):
        move(a[0] + dx * i, a[1] + dy * i)
    RESULT["link_modal_during"] = ([op.name for op in WIN.modal_operators]
                                   if hasattr(WIN, "modal_operators") else None)
    sim("LEFTMOUSE", "RELEASE", b[0], b[1])
    schedule(link_drag_check, 0.35)


def link_drag_check():
    st = read_state()
    after = st.get("links")
    RESULT["link_after"] = after
    RESULT["link_ok"] = bool(after is not None and RESULT["link_before"] is not None and
                            after > RESULT["link_before"])
    log(f"links {RESULT['link_before']} -> {after} ok={RESULT['link_ok']}")
    redraw(2)
    screenshot("f_m1_07_link.png")
    schedule(analyze, 0.2)


def analyze():
    RESULT["checks"] = {
        "selection_not_leaked": RESULT.get("click_through_ok"),
        "title_drag_works": RESULT.get("title_drag_ok"),
        "resize_works": RESULT.get("resize_ok"),
        "node_box_select_works": RESULT.get("box_sel_ok"),
        "node_link_works": RESULT.get("link_ok"),
        "viewport_control_works": RESULT.get("control_ok"),
    }
    log(f"checks {RESULT['checks']}")
    finish()


def finish():
    try:
        with open(os.path.join(OUT_DIR, "stage_f_m1_result.json"), "w") as fh:
            json.dump(RESULT, fh, indent=2)
    except OSError:
        pass
    log("quitting")
    LOG_FILE.close()
    bpy.ops.wm.quit_blender()


schedule(close_splash, 1.0)
