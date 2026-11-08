"""Stage A runtime verification for the floating panel region.

Run (GUI, not background):
  STAGE_A_OUT=/abs/out/dir build_linux/bin/blender --factory-startup \
      --python tests/ui_floating_panel_stage_a.py

It dumps the window/area layout, takes screenshots before/after a workspace
switch, and exits. No source files are modified.
"""

import os

import bpy

OUT_DIR = os.environ.get("STAGE_A_OUT", "/tmp/ui_floating_stage_a")
os.makedirs(OUT_DIR, exist_ok=True)

# Mirrors the constants in interface_region_floating_panel.cc.
PANEL_W = 420
PANEL_H = 240
PANEL_X_FAC = 0.70
PANEL_Y_FAC = 0.12

STATE = {"phase": 0}


def log(msg):
    print(f"[STAGE-A] {msg}", flush=True)


def report_layout(tag):
    win = bpy.context.window
    screen = win.screen
    w, h = win.width, win.height
    log(f"{tag}: window={w}x{h} workspace='{win.workspace.name}' screen='{screen.name}'")

    px, py = int(w * PANEL_X_FAC), int(h * PANEL_Y_FAC)
    px2, py2 = px + PANEL_W - 1, py + PANEL_H - 1
    log(f"{tag}: expected panel rect x={px}..{px2} y={py}..{py2}")

    for area in screen.areas:
        ax, ay, aw, ah = area.x, area.y, area.width, area.height
        ix = max(0, min(ax + aw, px2 + 1) - max(ax, px)) if aw > 0 else 0
        iy = max(0, min(ay + ah, py2 + 1) - max(ay, py)) if ah > 0 else 0
        hit = "OVERLAP" if (ix > 0 and iy > 0) else "-"
        log(
            f"{tag}: area {area.type:16s} x={ax:5d} y={ay:5d} w={aw:5d} h={ah:5d} "
            f"intersect=({ix}x{iy}) {hit}"
        )


def screenshot(name):
    path = os.path.join(OUT_DIR, name)
    win = bpy.context.window
    with bpy.context.temp_override(window=win, screen=win.screen):
        bpy.ops.screen.screenshot(filepath=path)
    ok = os.path.exists(path)
    log(f"screenshot '{path}' exists={ok} bytes={os.path.getsize(path) if ok else 0}")


def step():
    phase = STATE["phase"]

    if phase == 0:
        report_layout("layout")
        screenshot("stage_a_layout.png")
        STATE["phase"] = 1
        return 0.8

    if phase == 1:
        modeling = bpy.data.workspaces.get("Modeling")
        log(f"switch workspace Layout -> Modeling (found={modeling is not None})")
        if modeling is not None:
            bpy.context.window.workspace = modeling
        STATE["phase"] = 2
        return 1.2

    if phase == 2:
        report_layout("workspace-modeling")
        screenshot("stage_a_workspace_modeling.png")
        layout = bpy.data.workspaces.get("Layout")
        log(f"switch workspace Modeling -> Layout (found={layout is not None})")
        if layout is not None:
            bpy.context.window.workspace = layout
        STATE["phase"] = 3
        return 1.2

    if phase == 3:
        report_layout("back-layout")
        screenshot("stage_a_back_layout.png")
        log("done, quitting")
        bpy.ops.wm.quit_blender()
        return None

    return None


bpy.app.timers.register(step, first_interval=1.0)
