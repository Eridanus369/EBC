"""Stage E verification: native node editor host inside the floating panel + modal fallback.

Run (GUI, event simulation enabled):
  STAGE_E_OUT=/abs/out/dir EBC_FP_STATE_FILE=/tmp/fp_state_e.json \
  build_linux/bin/blender --factory-startup --enable-event-simulate \
  --python tests/ui_floating_panel_stage_e.py

The panel hosts a real offscreen `SpaceNode` area. Node drag / link / box-select stay native
operators. The C++ side writes a per-frame JSON snapshot with node rects, socket screen
positions, modal state and the signal-dot geometry/colour, so every check here is measured
on real screen geometry rather than guessed coordinates.

Milestones:
  1. node host alive in the panel: drag one node, create one link, selection state;
  2. modal fallback three states: dot appears, panel chrome pixel-identical while modal,
     interaction restored after release;
  3. stage-B regression: events outside the panel rectangle pass through (3D view orbits,
     node-editor box select), events inside move the panel and leave editor state untouched.
"""

import json
import os
import time

import bpy
import numpy as np

OUT_DIR = os.environ.get("STAGE_E_OUT", "/tmp/ui_floating_stage_e")
os.makedirs(OUT_DIR, exist_ok=True)
LOG_FILE = open(os.path.join(OUT_DIR, "stage_e_log.txt"), "w", buffering=1)
RESULT = {}

STATE_FILE = os.environ["EBC_FP_STATE_FILE"]
WIN = bpy.context.window
W, H = WIN.width, WIN.height

DOT_RGB = np.array([23.0, 141.0, 141.0])      # #178D8D (mirror of the C++ fill)
DOT_RING_RGB = np.array([133.0, 178.0, 178.0])  # #85B2B2


def log(msg):
    text = f"[STAGE-E] {msg}"
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


def load_rgb(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    arr = (buf.reshape(h, w, 4)[:, :, :3] * 255.0).astype(np.float32)
    bpy.data.images.remove(img)
    # Loaded rows are bottom-first: `arr[y, x]` uses window coordinates (origin bottom-left).
    return arr


def pixel(arr, x, y):
    return arr[int(y), int(x), :]


def inside(x, y, rect):
    return rect[0] <= x <= rect[1] and rect[2] <= y <= rect[3]


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


def chrome_mask(panel, node):
    """Four chrome bands around the canvas hole, in window coordinates."""
    mask = np.zeros((H, W), dtype=bool)
    x0, x1, y0, y1 = panel
    nx0, nx1, ny0, ny1 = node
    nx0 = max(x0, nx0)
    nx1 = min(x1, nx1)
    ny0 = max(y0, ny0)
    ny1 = min(y1, ny1)
    mask[y0:ny0, x0:x1 + 1] = True          # bottom band
    mask[ny1 + 1:y1 + 1, x0:x1 + 1] = True  # top band
    mask[ny0:ny1 + 1, x0:nx0] = True        # left band
    mask[ny0:ny1 + 1, nx1 + 1:x1 + 1] = True  # right band
    return mask


def view3d_rotation():
    for area in bpy.context.screen.areas:
        if area.type == "VIEW_3D":
            for region in area.regions:
                if region.type == "WINDOW":
                    quat = region.view_rotation if hasattr(region, "view_rotation") else None
                    if quat is None and area.spaces.active.region_3d:
                        quat = area.spaces.active.region_3d.view_rotation
                    return tuple(quat) if quat else None
    return None


def pass_through_point(panel):
    """A point inside the 3D view window region but outside the panel rectangle."""
    for area in bpy.context.screen.areas:
        if area.type != "VIEW_3D":
            continue
        for region in area.regions:
            if region.type != "WINDOW":
                continue
            xs = (region.x + 70, region.x + region.width - 70)
            ys = (region.y + 70, region.y + region.height - 90)
            for y in ys:
                for x in xs:
                    if 0 <= x < W and 0 <= y < H and not inside(x, y, panel):
                        return x, y
    return None


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------
def setup_tree():
    ng = bpy.data.node_groups.new("StageE_Geometry", "GeometryNodeTree")
    ng.interface.new_socket(name="Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
    ng.interface.new_socket(name="Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    gi = ng.nodes.new("NodeGroupInput")
    sp = ng.nodes.new("GeometryNodeSetPosition")
    go = ng.nodes.new("NodeGroupOutput")
    gi.location = (-200.0, 40.0)
    sp.location = (-20.0, -20.0)
    go.location = (130.0, -90.0)
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
    schedule(idle, 0.4)


def idle():
    redraw(2)
    st = read_state()
    if st is None or st.get("view") != "anchor":
        schedule(idle, 0.4)
        return
    RESULT["initial_state"] = st
    log(f"state view={st['view']} anchor={st['anchor']} win={st['win']}")
    schedule(expand_click, 0.2)


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
        log(f"expanded rect={st['rect']} host={st['host']} modal={st['modal']}")
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
            f"links={st.get('links')} nregion={st['nregion']}")
        move(80, 40)
        redraw(2)
        screenshot("e_00_host_expanded.png")
        schedule(node_drag_start, 0.2)
        return
    schedule(poll_host, 0.1)


# ---------------------------------------------------------------------------
# Milestone 1: native node drag + native link
# ---------------------------------------------------------------------------
def node_drag_start():
    st = read_state()
    node = find_node(st, "Set Position")
    if node is None:
        log("FATAL: Set Position node missing from state dump")
        finish()
        return
    n = node["rect"]
    x, y = (n[0] + n[1]) // 2, n[3] - 12
    RESULT["node_before"] = node
    RESULT["node_press"] = [x, y]
    log(f"node drag press {x},{y} rect={n} links={st.get('links')}")
    move(80, 40)
    redraw(2)
    screenshot("e_01_node_before.png")
    sim("LEFTMOUSE", "PRESS", x, y)
    move(x + 8, y + 3)
    schedule(node_drag_modal, 0.15)


def node_drag_modal():
    st = read_state()
    RESULT["node_drag_modal_state"] = st
    log(f"during node drag modal={st.get('modal')} action={st.get('action')}")
    if st.get("modal"):
        redraw(1)
        screenshot("e_10_modal_node_drag.png")
    x, y = RESULT["node_press"]
    for i in range(1, 7):
        move(x + 10 * i, y - 3 * i)
    schedule(node_drag_release, 0.2)


def node_drag_release():
    x, y = RESULT["node_press"]
    sim("LEFTMOUSE", "RELEASE", x + 60, y - 18)
    schedule(node_drag_check, 0.3)


def node_drag_check():
    st = read_state()
    node = find_node(st, "Set Position")
    RESULT["node_after"] = node
    ok = False
    if node:
        before = RESULT["node_before"]["rect"]
        after = node["rect"]
        ok = after[0] > before[0] + 25 and abs(after[2] - before[2]) >= 0 and node.get("sel") == 1
        log(f"node drag check before={before} after={after} sel={node.get('sel')} ok={ok}")
    RESULT["node_drag_ok"] = bool(ok)
    if node:
        log(f"after drag rect={node['rect']} modal={st.get('modal')}")
    schedule(link_drag_start, 0.2)


def link_drag_start():
    st = read_state()
    src_node = find_node(st, "Set Position")
    dst_node = find_node(st, "Group Output")
    src = find_socket(src_node, "out", "Geometry")
    dst = find_socket(dst_node, "in", "Geometry")
    if src is None or dst is None:
        log(f"FATAL: sockets missing src={src} dst={dst}")
        finish()
        return
    RESULT["link_before"] = st.get("links")
    RESULT["link_src"] = src
    RESULT["link_dst"] = dst
    sx, sy = src["pos"]
    dx, dy = dst["pos"]
    log(f"link drag {sx},{sy} -> {dx},{dy} links_before={st.get('links')}")
    move(80, 40)
    redraw(2)
    sim("LEFTMOUSE", "PRESS", sx, sy)
    for i in range(1, 7):
        move(sx + (dx - sx) * i // 6, sy + (dy - sy) * i // 6)
    sim("LEFTMOUSE", "RELEASE", dx, dy)
    schedule(link_check, 0.35)


def link_check():
    st = read_state()
    RESULT["link_after_state"] = st
    links = st.get("links", 0)
    ok = links >= 2
    RESULT["link_ok"] = bool(ok)
    log(f"link check links={links} ok={ok} nodes={[n['name'] for n in st.get('nodes', [])]}")
    move(80, 40)
    redraw(2)
    screenshot("e_02_link_created.png")
    schedule(modal_pre, 0.25)


# ---------------------------------------------------------------------------
# Milestone 2: modal fallback three states
# ---------------------------------------------------------------------------
def modal_pre():
    st = read_state()
    RESULT["modal_pre_state"] = st
    node = find_node(st, "Set Position")
    if node is None:
        log("FATAL: node missing before modal test")
        finish()
        return
    n = node["rect"]
    RESULT["modal_node"] = n
    RESULT["modal_press"] = [(n[0] + n[1]) // 2, n[3] - 12]
    redraw(2)
    screenshot("e_20_pre_modal.png")
    log(f"pre-modal rect={st['rect']} node={n} modal={st['modal']} dot={st.get('dot')}")
    x, y = RESULT["modal_press"]
    sim("LEFTMOUSE", "PRESS", x, y)
    move(x + 4, y + 2)
    schedule(modal_enter, 0.2)


def modal_enter():
    st = read_state()
    if st is None or not st.get("modal"):
        schedule(modal_enter, 0.1)
        return
    RESULT["modal_enter_state"] = st
    RESULT["modal_rect"] = st["rect"]
    redraw(2)
    screenshot("e_21_modal_dot.png")
    log(f"modal entered rect={st['rect']} dot={st.get('dot')} modal={st['modal']}")
    x, y = RESULT["modal_press"]
    # Try to drag the panel while the node operator is modal: the title bar, then far away.
    p = st["rect"]
    move((p[0] + p[1]) // 2, p[3] - 8)
    move((p[0] + p[1]) // 2 + 40, p[3] - 8 + 15)
    move((p[0] + p[1]) // 2 + 90, p[3] - 8 + 35)
    schedule(modal_drag_attempt, 0.25)


def modal_drag_attempt():
    st = read_state()
    RESULT["modal_drag_state"] = st
    redraw(2)
    screenshot("e_22_modal_panel_drag_attempt.png")
    log(f"modal drag attempt rect={st['rect']} modal={st['modal']} dot={st.get('dot')}")
    x, y = RESULT["modal_press"]
    sim("LEFTMOUSE", "RELEASE", x + 60, y - 18)
    schedule(modal_exit, 0.35)


def modal_exit():
    st = read_state()
    if st is None or st.get("modal"):
        schedule(modal_exit, 0.1)
        return
    RESULT["modal_exit_state"] = st
    redraw(2)
    screenshot("e_23_modal_exit.png")
    log(f"modal exit rect={st['rect']} modal={st['modal']} dot={st.get('dot')}")
    schedule(panel_restore_press, 0.2)


def panel_restore_press():
    st = read_state()
    p = st["rect"]
    RESULT["restore_before"] = p
    x, y = p[0] + 100, p[3] - 8
    log(f"panel drag after modal from {x},{y} rect={p}")
    sim("LEFTMOUSE", "PRESS", x, y)
    for i in range(1, 6):
        move(x + 8 * i, y + 5 * i)
    sim("LEFTMOUSE", "RELEASE", x + 40, y + 25)
    schedule(panel_restore_check, 0.35)


def panel_restore_check():
    st = read_state()
    RESULT["restore_after"] = st["rect"]
    ok = st["rect"][0] != RESULT["restore_before"][0] or st["rect"][2] != RESULT["restore_before"][2]
    RESULT["restore_ok"] = bool(ok)
    log(f"restore drag {RESULT['restore_before']} -> {st['rect']} ok={ok}")
    RESULT["hover_before_probe"] = st.get("hover")
    move(80, 40)
    redraw(2)
    screenshot("e_24_panel_drag_restored.png")
    schedule(hover_probe, 0.2)


def hover_probe():
    """After the modal operator exits, hovering the chrome must work again (stage D rule)."""
    st = read_state()
    if st is None:
        schedule(hover_probe, 0.1)
        return
    p = st["rect"]
    # Minimise button: BUTTON_MARGIN 10, BUTTON_SIZE 20 -> centre 19 px in from the top-right.
    x, y = p[1] - 19, p[3] - 19
    move(x, y)
    schedule(hover_probe_check, 0.25)


def hover_probe_check():
    st = read_state()
    RESULT["hover_restore_state"] = st
    RESULT["hover_restore_ok"] = bool(st.get("hover"))
    log(f"hover restore hover={st.get('hover')} (0=None,2=Lock,3=Minimise,4=Slider) "
        f"ok={RESULT['hover_restore_ok']}")
    redraw(2)
    screenshot("e_25_hover_restored.png")
    move(80, 40)
    redraw(2)
    schedule(pass_through_setup, 0.2)


# ---------------------------------------------------------------------------
# Milestone 3: pass-through regression (stage B criterion)
# ---------------------------------------------------------------------------
def pass_through_setup():
    st = read_state()
    if st is None or not st.get("host"):
        log("pass-through skipped: host not active")
        finish()
        return
    point = pass_through_point(st["rect"])
    if point is None:
        log("pass-through skipped: no 3D view point outside panel")
        finish()
        return
    RESULT["pass_through_point"] = list(point)
    RESULT["panel_before_pass"] = st["rect"]
    RESULT["rotation_before"] = view3d_rotation()
    x, y = point
    log(f"pass-through MMB orbit at {x},{y} rot_before={RESULT['rotation_before']}")
    sim("MIDDLEMOUSE", "PRESS", x, y)
    for i in range(1, 9):
        move(x + 10 * i, y + 3 * i)
    sim("MIDDLEMOUSE", "RELEASE", x + 80, y + 24)
    schedule(pass_through_check, 0.4)


def pass_through_check():
    st = read_state()
    RESULT["pass_through_state"] = st
    RESULT["rotation_after"] = view3d_rotation()
    before = RESULT.get("rotation_before")
    after = RESULT.get("rotation_after")
    rot_ok = False
    if before and after:
        rot_ok = sum(abs(a - b) for a, b in zip(before, after)) > 1e-4
    rect_ok = st is not None and st.get("rect") == RESULT.get("panel_before_pass")
    RESULT["pass_through_ok"] = bool(rot_ok and rect_ok)
    log(f"pass-through rot {before} -> {after} orbit_ok={rot_ok} panel_rect_stable={rect_ok} "
        f"host={st.get('host') if st else None}")
    redraw(2)
    screenshot("e_30_pass_through.png")
    schedule(object_inside_control, 0.25)


def object_selection():
    return sorted(o.name for o in bpy.data.objects if o.select_get())


def object_inside_control():
    """Control case for the 3D orbit: an inside-panel drag must move the panel only."""
    st = read_state()
    p = st["rect"]
    RESULT["object_inside_before"] = {
        "rect": p,
        "rotation": view3d_rotation(),
        "selected": object_selection(),
    }
    x, y = p[0] + 100, p[3] - 8
    log(f"object inside drag press {x},{y} rect={p}")
    sim("LEFTMOUSE", "PRESS", x, y)
    for i in range(1, 6):
        move(x + 8 * i, y - 4 * i)
    sim("LEFTMOUSE", "RELEASE", x + 40, y - 20)
    schedule(object_inside_check, 0.35)


def object_inside_check():
    st = read_state()
    before = RESULT["object_inside_before"]
    after = {
        "rect": st["rect"],
        "rotation": view3d_rotation(),
        "selected": object_selection(),
    }
    RESULT["object_inside_after"] = after
    moved = after["rect"][0] != before["rect"][0] or after["rect"][2] != before["rect"][2]
    rot_same = True
    if before["rotation"] is not None and after["rotation"] is not None:
        rot_same = sum(abs(a - b) for a, b in zip(before["rotation"], after["rotation"])) < 1e-6
    sel_same = before["selected"] == after["selected"]
    RESULT["object_inside_ok"] = bool(moved and rot_same and sel_same)
    log(f"object inside drag {before['rect']} -> {after['rect']} moved={moved} "
        f"rot_same={rot_same} sel_same={sel_same} ok={RESULT['object_inside_ok']}")
    redraw(2)
    screenshot("e_31_object_inside_drag.png")
    schedule(node_editor_setup, 0.2)


def node_editor_area():
    """The real node-editor area, not the panel's offscreen host (which is also in areabase)."""
    candidates = []
    for area in bpy.context.screen.areas:
        if area.type == "NODE_EDITOR":
            region = next((r for r in area.regions if r.type == "WINDOW"), None)
            if region is not None:
                candidates.append((area, region))
    if not candidates:
        return None, None
    return min(candidates, key=lambda ar: ar[0].x)


def node_editor_rect(region):
    return [region.x, region.x + region.width - 1,
            region.y, region.y + region.height - 1]


def tree_nodes(tree):
    return {n.name: n for n in tree.nodes}


def selected_tree(tree):
    return sorted(n.name for n in tree.nodes if n.select)


def node_editor_setup():
    """Create a real node editor outside the panel, then frame the stage-E tree in it."""
    tree = bpy.data.node_groups.get("StageE_Geometry")
    if tree is None:
        log("FATAL: node editor test needs StageE_Geometry")
        finish()
        return
    src = next((a for a in bpy.context.screen.areas if a.type == "VIEW_3D"), None)
    if src is not None:
        region = next((r for r in src.regions if r.type == "WINDOW"), None)
        try:
            with bpy.context.temp_override(window=WIN, screen=WIN.screen,
                                           area=src, region=region):
                bpy.ops.screen.area_split(direction="VERTICAL", factor=0.5)
        except Exception as exc:  # noqa: BLE001
            log(f"area split failed: {exc} (fallback: properties area)")
    schedule(node_editor_configure, 0.35)


def node_editor_configure():
    tree = bpy.data.node_groups.get("StageE_Geometry")
    views = [a for a in bpy.context.screen.areas if a.type == "VIEW_3D"]
    target = min(views, key=lambda a: a.x) if len(views) >= 2 else None
    if target is None:
        target = next((a for a in bpy.context.screen.areas if a.type == "PROPERTIES"), None)
    if target is None:
        log("FATAL: no area available for the node editor")
        finish()
        return
    try:
        target.type = "NODE_EDITOR"
        space = target.spaces.active
        # 5.3: `SpaceNodeEditor` binds its tree from the context (Cube's modifier) unless pinned.
        space.tree_type = "GeometryNodeTree"
        space.node_tree = tree
        space.pin = True
    except Exception as exc:  # noqa: BLE001
        log(f"node editor conversion failed: {exc}")
        finish()
        return
    log(f"node editor area x={target.x} w={target.width} tree={space.node_tree.name} "
        f"tree_type={space.tree_type} edit_tree={space.edit_tree.name if space.edit_tree else None}")
    RESULT["node_editor_tree"] = space.node_tree.name
    schedule(node_editor_frame, 0.3)


def node_editor_frame():
    area, region = node_editor_area()
    tree = bpy.data.node_groups.get("StageE_Geometry")
    if area is None or tree is None:
        log("FATAL: node editor window region missing")
        finish()
        return
    r = node_editor_rect(region)
    RESULT["node_editor_rect"] = r
    redraw(2)
    log(f"node editor draw: edit_tree={area.spaces.active.edit_tree.name if area.spaces.active.edit_tree else None}")
    try:
        with bpy.context.temp_override(window=WIN, screen=WIN.screen, area=area, region=region):
            bpy.ops.node.view_all()
    except Exception as exc:  # noqa: BLE001
        log(f"node view_all failed: {exc}")
    for node in tree.nodes:
        node.select = False
    RESULT["node_sel_before_box"] = selected_tree(tree)
    log(f"node editor rect={r} sel_before={RESULT['node_sel_before_box']}")
    move(80, 40)
    redraw(2)
    schedule(node_box_drag, 0.25)


def box_corners(r):
    return [(r[0] + 25, r[2] + 25), (r[1] - 25, r[2] + 25),
            (r[0] + 25, r[3] - 25), (r[1] - 25, r[3] - 25)]


def node_box_drag():
    """Box select in the node editor outside the panel rectangle."""
    st = read_state()
    r = RESULT["node_editor_rect"]
    p = st["rect"]
    start = next((c for c in box_corners(r) if not inside(c[0], c[1], p)), None)
    if start is None:
        log("FATAL: no node-editor corner outside the panel")
        finish()
        return
    end = (r[1] - 25, r[3] - 25) if start[1] < p[2] + (p[3] - p[2]) // 2 else (r[1] - 25, r[2] + 25)
    if inside(end[0], end[1], p):
        end = (r[1] - 25, r[2] + 25) if end[1] > p[2] else (r[1] - 25, r[3] - 25)
    RESULT["node_box_points"] = [list(start), list(end)]
    log(f"box select {start} -> {end} panel={p}")
    move(start[0], start[1])
    sim("LEFTMOUSE", "PRESS", start[0], start[1])
    for i in range(1, 5):
        move(start[0] + (end[0] - start[0]) * i // 8,
             start[1] + (end[1] - start[1]) * i // 8)
    schedule(node_box_mid, 0.15)


def node_box_mid():
    st = read_state()
    RESULT["node_box_modal"] = bool(st and st.get("modal"))
    redraw(1)
    screenshot("e_32_node_box_drag.png")
    start, end = RESULT["node_box_points"]
    for i in range(5, 9):
        move(start[0] + (end[0] - start[0]) * i // 8,
             start[1] + (end[1] - start[1]) * i // 8)
    sim("LEFTMOUSE", "RELEASE", end[0], end[1])
    schedule(node_box_check, 0.35)


def node_box_check():
    tree = bpy.data.node_groups.get("StageE_Geometry")
    st = read_state()
    RESULT["node_box_selected"] = selected_tree(tree) if tree else []
    RESULT["node_box_state_selected"] = sorted(
        n.get("name") for n in (st.get("nodes") or []) if n.get("sel"))
    RESULT["node_box_ok"] = bool(RESULT["node_box_selected"])
    log(f"box selected {RESULT['node_box_selected']} "
        f"(state={RESULT['node_box_state_selected']}) ok={RESULT['node_box_ok']} "
        f"modal_during={RESULT.get('node_box_modal')}")
    redraw(2)
    screenshot("e_33_node_box_selected.png")
    schedule(panel_onto_node_editor, 0.2)


def panel_onto_node_editor():
    """Move the panel over the node editor so an inside drag can be attempted there."""
    st = read_state()
    p = st["rect"]
    area, region = node_editor_area()
    if region is None:
        log("FATAL: node editor missing for inside-drag control")
        finish()
        return
    r = node_editor_rect(region)
    target_x0 = r[0] + 20
    delta = target_x0 - p[0]
    x, y = p[0] + 100, p[3] - 8
    log(f"panel onto editor: {p} -> x0={target_x0} delta={delta}")
    sim("LEFTMOUSE", "PRESS", x, y)
    for i in range(1, 9):
        move(x + delta * i // 8, y)
    sim("LEFTMOUSE", "RELEASE", x + delta, y)
    RESULT["node_sel_before_inside"] = selected_tree(bpy.data.node_groups["StageE_Geometry"])
    schedule(panel_over_check, 0.35)


def panel_over_check():
    st = read_state()
    p = st["rect"]
    r = RESULT["node_editor_rect"]
    overlaps = p[0] <= r[1] and p[1] >= r[0] and p[2] <= r[3] and p[3] >= r[2]
    RESULT["panel_over_editor_rect"] = p
    RESULT["panel_over_editor"] = bool(overlaps)
    log(f"panel over editor rect={p} editor={r} overlaps={overlaps}")
    redraw(2)
    screenshot("e_34_panel_over_node_editor.png")
    schedule(node_inside_drag, 0.2)


def node_inside_drag():
    """Press-drag on the panel chrome while it floats over the node editor."""
    st = read_state()
    p = st["rect"]
    r = RESULT["node_editor_rect"]
    ix0, ix1 = max(p[0], r[0]), min(p[1], r[1])
    iy0, iy1 = max(p[2], r[2]), min(p[3], r[3])
    if ix1 - ix0 < 60 or iy1 - iy0 < 60:
        log(f"FATAL: panel/editor overlap too small {p} {r}")
        finish()
        return
    x, y = (ix0 + ix1) // 2, p[3] - 8
    RESULT["node_inside_before"] = {
        "rect": p,
        "selected": selected_tree(bpy.data.node_groups["StageE_Geometry"]),
    }
    log(f"node inside drag press {x},{y} rect={p}")
    sim("LEFTMOUSE", "PRESS", x, y)
    for i in range(1, 6):
        move(x + 7 * i, y + 4 * i)
    sim("LEFTMOUSE", "RELEASE", x + 35, y + 20)
    schedule(node_inside_check, 0.35)


def node_inside_check():
    st = read_state()
    tree = bpy.data.node_groups.get("StageE_Geometry")
    after = {"rect": st["rect"], "selected": selected_tree(tree)}
    before = RESULT["node_inside_before"]
    RESULT["node_inside_after"] = after
    moved = after["rect"][0] != before["rect"][0] or after["rect"][2] != before["rect"][2]
    sel_same = after["selected"] == before["selected"]
    RESULT["node_inside_ok"] = bool(moved and sel_same and RESULT.get("panel_over_editor"))
    log(f"node inside drag {before['rect']} -> {after['rect']} moved={moved} "
        f"sel {before['selected']} == {after['selected']} sel_same={sel_same} "
        f"ok={RESULT['node_inside_ok']}")
    redraw(2)
    screenshot("e_35_node_inside_drag.png")
    schedule(analyze, 0.25)


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------
def analyze():
    log("analyzing screenshots")
    pre = load_rgb(os.path.join(OUT_DIR, "e_20_pre_modal.png"))
    dot = load_rgb(os.path.join(OUT_DIR, "e_21_modal_dot.png"))
    drag = load_rgb(os.path.join(OUT_DIR, "e_22_modal_panel_drag_attempt.png"))
    exit_img = load_rgb(os.path.join(OUT_DIR, "e_23_modal_exit.png"))
    after_drag = load_rgb(os.path.join(OUT_DIR, "e_02_link_created.png"))

    dot_info = (RESULT.get("modal_enter_state") or {}).get("dot")
    if dot_info:
        cx, cy, radius = dot_info
        centre = pixel(dot, cx, cy)
        ring = pixel(dot, cx + radius - 1, cy)
        RESULT["dot_sample"] = {
            "centre_xy": [cx, cy], "radius": radius,
            "centre_rgb": [round(float(v), 1) for v in centre],
            "ring_rgb": [round(float(v), 1) for v in ring],
            "centre_dist": round(float(np.abs(centre - DOT_RGB).max()), 2),
            "ring_dist": round(float(np.abs(ring - DOT_RING_RGB).max()), 2),
        }
        RESULT["dot_ok"] = bool(np.abs(centre - DOT_RGB).max() <= 6.0)
        log(f"dot centre={RESULT['dot_sample']['centre_rgb']} "
            f"ring={RESULT['dot_sample']['ring_rgb']} ok={RESULT['dot_ok']}")

        pre_px = pixel(pre, cx, cy)
        exit_px = pixel(exit_img, cx, cy)
        RESULT["dot_exit_sample"] = {
            "pre_rgb": [round(float(v), 1) for v in pre_px],
            "exit_rgb": [round(float(v), 1) for v in exit_px],
            "exit_dist_to_dot": round(float(np.abs(exit_px - DOT_RGB).max()), 2),
        }
        RESULT["dot_removed_ok"] = bool(np.abs(exit_px - DOT_RGB).max() > 12.0)
        log(f"dot removed pre={RESULT['dot_exit_sample']['pre_rgb']} "
            f"exit={RESULT['dot_exit_sample']['exit_rgb']} "
            f"ok={RESULT['dot_removed_ok']}")

        panel = (RESULT.get("modal_rect") or [0, 0, 0, 0])
        node = (RESULT.get("modal_drag_state") or {}).get("node") or [0, 0, 0, 0]
        mask = chrome_mask(panel, node)
        diff = np.abs(drag - dot).max(axis=2)
        changed = diff[mask]
        RESULT["modal_chrome_diff"] = {
            "pixels": int(mask.sum()),
            "changed_pixels": int((changed > 2.0).sum()),
            "max_diff": round(float(changed.max()) if changed.size else 0.0, 2),
        }
        RESULT["modal_chrome_stable_ok"] = bool((changed > 2.0).sum() == 0)
        log(f"modal chrome diff={RESULT['modal_chrome_diff']} "
            f"stable={RESULT['modal_chrome_stable_ok']}")

    st = RESULT.get("link_after_state") or {}
    nodes = {n["name"]: n for n in st.get("nodes", [])}
    RESULT["links"] = st.get("links")
    RESULT["selected_nodes"] = [name for name, n in nodes.items() if n.get("sel")]

    checks = {
        "host_active": bool((RESULT.get("host_state") or {}).get("host")),
        "node_drag": RESULT.get("node_drag_ok"),
        "node_selected": bool(RESULT.get("selected_nodes")),
        "link_created": RESULT.get("link_ok"),
        "dot_visible_modal": RESULT.get("dot_ok"),
        "panel_survives_modal": bool((RESULT.get("modal_enter_state") or {}).get("host")),
        "modal_panel_immovable": RESULT.get("modal_chrome_stable_ok"),
        "dot_removed_on_exit": RESULT.get("dot_removed_ok"),
        "interaction_restored": RESULT.get("restore_ok"),
        "hover_restored": RESULT.get("hover_restore_ok"),
        "outside_passthrough": RESULT.get("pass_through_ok"),
        "object_inside_panel_stable": RESULT.get("object_inside_ok"),
        "node_box_select": RESULT.get("node_box_ok"),
        "node_panel_drag_selection_stable": RESULT.get("node_inside_ok"),
    }
    RESULT["checks"] = checks
    log(f"checks {checks}")
    with open(os.path.join(OUT_DIR, "stage_e_result.json"), "w") as fh:
        json.dump(RESULT, fh, indent=2)
    log("result written")
    finish()


def finish():
    try:
        with open(os.path.join(OUT_DIR, "stage_e_result.json"), "w") as fh:
            json.dump(RESULT, fh, indent=2)
    except OSError:
        pass
    log("quitting")
    LOG_FILE.close()
    bpy.ops.wm.quit_blender()


schedule(close_splash, 1.0)
