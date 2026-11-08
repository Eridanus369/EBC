"""Stage C blur verification: capture, gaussian blur, frame cost, drag degradation.

Run (GUI, event simulation enabled):
  build_linux/bin/blender --factory-startup --enable-event-simulate \
      --python tests/ui_floating_panel_stage_c_blur.py

Produces screenshots + `stage_c_blur_result.json`. State transitions and timing are logged by
the C++ side as `[FP-BLUR] ...` lines on stderr.
"""

import json
import os

import bpy
import numpy as np

OUT_DIR = os.environ.get("STAGE_C_OUT", "/tmp/ui_floating_stage_c")
os.makedirs(OUT_DIR, exist_ok=True)
LOG_FILE = open(os.path.join(OUT_DIR, "stage_c_blur_log.txt"), "w", buffering=1)
RESULT = {}

PANEL_W = 420
PANEL_H = 240
PANEL_X_FAC = 0.70
PANEL_Y_FAC = 0.12

WIN = bpy.context.window
W, H = WIN.width, WIN.height
X0 = int(W * PANEL_X_FAC)
Y0 = int(H * PANEL_Y_FAC)


def log(msg):
    print(f"[STAGE-C] {msg}", flush=True)
    LOG_FILE.write(f"[STAGE-C] {msg}\n")


def screenshot(name):
    path = os.path.join(OUT_DIR, name)
    with bpy.context.temp_override(window=WIN, screen=WIN.screen):
        bpy.ops.screen.screenshot(filepath=path)
    log(f"screenshot {name}")
    return path


def load_rgb(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    arr = buf.reshape(h, w, 4)[:, :, :3]
    bpy.data.images.remove(img)
    return arr


def redraw(n=2):
    try:
        with bpy.context.temp_override(window=WIN, screen=WIN.screen):
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=n)
    except Exception as exc:  # noqa: BLE001
        log(f"redraw_timer failed: {exc}")


def set_env(**kwargs):
    for key, value in kwargs.items():
        os.environ[key] = f"{value}"
        log(f"{key}={value}")


def sharpness(arr, rect):
    x0, y0, x1, y1 = rect
    x0, y0 = max(0, x0 + 4), max(0, y0 + 4)
    x1, y1 = min(W - 1, x1 - 3), min(H - 1, y1 - 3)
    sub = arr[y0:y1, x0:x1, :].mean(axis=2)
    if sub.size == 0:
        return 0.0
    return float(np.abs(np.diff(sub, axis=1)).mean() + np.abs(np.diff(sub, axis=0)).mean())


def save_crop(arr, name, rect, pad=40):
    x0, y0, x1, y1 = rect
    cx0, cy0 = max(0, x0 - pad), max(0, y0 - pad)
    cx1, cy1 = min(W - 1, x1 + pad), min(H - 1, y1 + pad)
    crop = arr[cy0:cy1 + 1, cx0:cx1 + 1, :]
    h, w, _ = crop.shape
    img = bpy.data.images.new(name, width=w, height=h, alpha=False)
    buf = np.ones((h, w, 4), dtype=np.float32)
    buf[:, :, :3] = crop
    img.pixels.foreach_set(buf.ravel())
    img.file_format = "PNG"
    img.filepath_raw = os.path.join(OUT_DIR, name)
    img.save()
    bpy.data.images.remove(img)
    log(f"saved crop {name}")


# ---------------------------------------------------------------------------
# Step chain (timers, so the main loop dispatches simulated events between steps)
# ---------------------------------------------------------------------------
def schedule(fn, delay=0.6):
    bpy.app.timers.register(fn, first_interval=delay)


def close_splash():
    log("closing splash")
    WIN.event_simulate(type="LEFTMOUSE", value="PRESS", x=700, y=500)
    WIN.event_simulate(type="LEFTMOUSE", value="RELEASE", x=700, y=500)
    schedule(step_off, 1.2)


def step_off():
    # alpha=0: the glass layer draws nothing, so the panel shows the raw backdrop (blur off) or
    # the pure blurred backdrop (blur on). This makes the sharpness comparison direct.
    set_env(EBC_FP_ALPHA=0.0, EBC_FP_BLUR=0)
    redraw(3)
    screenshot("c_10_pair_off_alpha0.png")
    schedule(step_on)


def step_on():
    set_env(EBC_FP_BLUR=1)
    redraw(3)
    screenshot("c_11_pair_on_alpha0.png")
    # Visual variant with the normal glass alpha.
    set_env(EBC_FP_ALPHA=0.78)
    redraw(3)
    screenshot("c_12_blur_visual.png")
    schedule(step_analyze_blur)


def step_analyze_blur():
    rect = (X0, Y0, X0 + PANEL_W - 1, Y0 + PANEL_H - 1)
    off = load_rgb(os.path.join(OUT_DIR, "c_10_pair_off_alpha0.png"))
    on = load_rgb(os.path.join(OUT_DIR, "c_11_pair_on_alpha0.png"))
    vis = load_rgb(os.path.join(OUT_DIR, "c_12_blur_visual.png"))
    panel_off = sharpness(off, rect)
    panel_on = sharpness(on, rect)
    ctrl_rect = (max(0, X0 - 260), Y0, max(0, X0 - 30), Y0 + PANEL_H - 1)
    ctrl_off = sharpness(off, ctrl_rect)
    ctrl_on = sharpness(on, ctrl_rect)
    log(f"panel sharpness blur_off={panel_off:.4f} blur_on={panel_on:.4f} "
        f"ratio={panel_on / panel_off if panel_off else 0:.3f}")
    log(f"control sharpness (outside panel) off={ctrl_off:.4f} on={ctrl_on:.4f} "
        f"ratio={ctrl_on / ctrl_off if ctrl_off else 0:.3f}")
    RESULT["blur_metrics"] = {
        "panel_sharpness_off": panel_off,
        "panel_sharpness_on": panel_on,
        "panel_ratio": panel_on / panel_off if panel_off else None,
        "control_sharpness_off": ctrl_off,
        "control_sharpness_on": ctrl_on,
        "control_ratio": ctrl_on / ctrl_off if ctrl_off else None,
        "panel_rect": list(rect),
    }
    save_crop(vis, "c_13_blur_crop.png", rect)
    RESULT["panel_sharpness_reduced"] = bool(panel_off > 0 and panel_on / panel_off < 0.75)
    schedule(step_timing_off)


def step_timing_off():
    set_env(EBC_FP_BLUR=0)
    redraw(3)
    log("timing: blur off, 60 redraw+swap iterations")
    redraw(60)
    schedule(step_timing_on)


def step_timing_on():
    set_env(EBC_FP_BLUR=1)
    redraw(3)
    log("timing: blur on, 60 redraw+swap iterations")
    redraw(60)
    schedule(step_drag_press)


# --- drag degradation -------------------------------------------------------
DRAG_START = (X0 + 80, Y0 + PANEL_H - 12)
DRAG_STEPS = 40
DRAG_DX = -3


def step_drag_press():
    set_env(EBC_FP_ALPHA=0.78, EBC_FP_BLUR=1)
    redraw(3)
    log(f"drag press at {DRAG_START}")
    WIN.event_simulate(type="LEFTMOUSE", value="PRESS", x=DRAG_START[0], y=DRAG_START[1])
    schedule(lambda: drag_motion(1))


def drag_motion(i):
    x = DRAG_START[0] + DRAG_DX * i
    WIN.event_simulate(type="MOUSEMOVE", value="NOTHING", x=x, y=DRAG_START[1])
    if i == DRAG_STEPS // 2:
        screenshot("c_20_drag_mid.png")
        RESULT["drag_mid_rect"] = [X0 + DRAG_DX * i, Y0, X0 + PANEL_W - 1 + DRAG_DX * i,
                                   Y0 + PANEL_H - 1]
    if i < DRAG_STEPS:
        schedule(lambda: drag_motion(i + 1), 0.05)
    else:
        schedule(drag_release, 0.0)


def drag_release():
    x = DRAG_START[0] + DRAG_DX * DRAG_STEPS
    WIN.event_simulate(type="LEFTMOUSE", value="RELEASE", x=x, y=DRAG_START[1])
    log("drag released")
    schedule(step_after_release, 0.7)


def step_after_release():
    screenshot("c_21_after_release.png")
    rect = (X0 + DRAG_DX * DRAG_STEPS,
            Y0,
            X0 + DRAG_DX * DRAG_STEPS + PANEL_W - 1,
            Y0 + PANEL_H - 1)
    mid = load_rgb(os.path.join(OUT_DIR, "c_20_drag_mid.png"))
    after = load_rgb(os.path.join(OUT_DIR, "c_21_after_release.png"))
    mid_rect = RESULT.get("drag_mid_rect", rect)
    mid_sharp = sharpness(mid, mid_rect)
    after_sharp = sharpness(after, rect)
    log(f"drag sharpness mid={mid_sharp:.4f} after_release={after_sharp:.4f}")
    RESULT["drag_metrics"] = {
        "mid_sharpness": mid_sharp,
        "after_release_sharpness": after_sharp,
        "mid_rect": mid_rect,
        "after_rect": list(rect),
    }
    save_crop(mid, "c_22_drag_mid_crop.png", mid_rect)
    save_crop(after, "c_23_after_release_crop.png", rect)
    schedule(finish, 0.3)


def finish():
    RESULT["status"] = "ok"
    with open(os.path.join(OUT_DIR, "stage_c_blur_result.json"), "w") as fh:
        json.dump(RESULT, fh, indent=2)
    log("result written")
    bpy.ops.wm.quit_blender()


log("stage C blur script loaded")
bpy.app.timers.register(close_splash, first_interval=1.5)
