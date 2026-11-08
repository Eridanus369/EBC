"""Stage C runtime verification for the floating panel glass (fallback + blur).

Run (GUI, not background, event simulation enabled):
  STAGE_C_OUT=/abs/out/dir build_linux/bin/blender --factory-startup \
      --enable-event-simulate --python tests/ui_floating_panel_stage_c.py

Milestone 1 (fallback glass) evidence:
  - background-only reference screenshot with EBC_FP_ALPHA=0.0 (panel draws nothing),
  - panel-on screenshots at EBC_FP_ALPHA=0.78 and 0.35,
  - pixel statistics: changed area, gradient direction, and a measured transmission
    coefficient across an area boundary inside the panel (bg difference vs panel
    difference at the same row) proving the panel is semi-transparent, not opaque.

No source files are modified.
"""

import json
import os

import bpy
import numpy as np

OUT_DIR = os.environ.get("STAGE_C_OUT", "/tmp/ui_floating_stage_c")
os.makedirs(OUT_DIR, exist_ok=True)
LOG_FILE = open(os.path.join(OUT_DIR, "stage_c_log.txt"), "w", buffering=1)
RESULT = {}

# Mirrors the constants in interface_region_floating_panel.cc.
PANEL_W = 420
PANEL_H = 240
HANDLE = 14
PANEL_X_FAC = 0.70
PANEL_Y_FAC = 0.12
PANEL_TOP = np.array([69.0, 61.0, 61.0]) / 255.0
PANEL_BOTTOM = np.array([11.0, 3.0, 3.0]) / 255.0

WIN = bpy.context.window
W, H = WIN.width, WIN.height


def log(msg):
    text = f"[STAGE-C] {msg}"
    print(text, flush=True)
    LOG_FILE.write(text + "\n")


def screenshot(name):
    path = os.path.join(OUT_DIR, name)
    with bpy.context.temp_override(window=WIN, screen=WIN.screen):
        bpy.ops.screen.screenshot(filepath=path)
    log(f"screenshot {name} bytes={os.path.getsize(path) if os.path.exists(path) else 0}")
    return path


def save_crop(src_arr, name, x0, y0, x1, y1):
    """Save a window-space crop of an already loaded pixel array (row 0 = bottom)."""
    pad = 40
    cx0, cy0 = max(0, x0 - pad), max(0, y0 - pad)
    cx1, cy1 = min(W - 1, x1 + pad), min(H - 1, y1 + pad)
    crop = src_arr[cy0:cy1 + 1, cx0:cx1 + 1, :]
    h, w, _ = crop.shape
    img = bpy.data.images.new(name, width=w, height=h, alpha=False)
    buf = np.ones((h, w, 4), dtype=np.float32)
    buf[:, :, :3] = crop
    img.pixels.foreach_set(buf.ravel())
    img.file_format = "PNG"
    path = os.path.join(OUT_DIR, name)
    img.filepath_raw = path
    img.save()
    bpy.data.images.remove(img)
    log(f"saved crop {name} ({w}x{h})")


def load_rgb(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    arr = buf.reshape(h, w, 4)[:, :, :3]
    bpy.data.images.remove(img)
    # Blender image row 0 is the bottom row; window coords are bottom-left.
    return arr


def panel_rect():
    x0 = int(W * PANEL_X_FAC)
    y0 = int(H * PANEL_Y_FAC)
    return (x0, y0, x0 + PANEL_W - 1, y0 + PANEL_H - 1)


def clamp_rect(r):
    x0, y0, x1, y1 = r
    return (max(0, x0), max(0, y0), min(W - 1, x1), min(H - 1, y1))


def force_redraw():
    try:
        with bpy.context.temp_override(window=WIN, screen=WIN.screen):
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=2)
    except Exception as exc:  # noqa: BLE001
        log(f"redraw_timer failed: {exc}")


def set_alpha(value):
    os.environ["EBC_FP_ALPHA"] = f"{value:.3f}"
    log(f"EBC_FP_ALPHA -> {value:.3f}")
    force_redraw()


def area_boundaries():
    """Vertical split x positions of the current screen areas (window pixels)."""
    xs = []
    for area in WIN.screen.areas:
        xs.append(int(area.x))
        xs.append(int(area.x + area.width))
    return sorted(set(xs))


def analyze():
    rect = clamp_rect(panel_rect())
    x0, y0, x1, y1 = rect
    bg = load_rgb(os.path.join(OUT_DIR, "c_00_bg_alpha0.png"))
    a78 = load_rgb(os.path.join(OUT_DIR, "c_01_alpha078.png"))
    a35 = load_rgb(os.path.join(OUT_DIR, "c_02_alpha035.png"))

    log(f"image size {bg.shape[1]}x{bg.shape[0]} window {W}x{H} panel {rect}")
    assert bg.shape[1] == W and bg.shape[0] == H, "screenshot size != window size"

    # Interior region: inset border (1px) and the bottom-right resize handle.
    ix0, iy0 = x0 + 3, y0 + 3
    ix1, iy1 = x1 - 3, y1 - 3
    interior = np.zeros((H, W), dtype=bool)
    interior[iy0:iy1 + 1, ix0:ix1 + 1] = True
    interior[iy0:iy0 + HANDLE + 4, x1 - HANDLE - 4:x1 + 1] = False

    changed = np.abs(a78 - bg).max(axis=2) > 0.02
    ys, xs = np.nonzero(changed)
    if len(xs):
        bbox = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
        log(f"alpha0.78 changed bbox {bbox} expected {rect}")
        assert bbox[0] >= x0 and bbox[1] >= y0 and bbox[2] <= x1 and bbox[3] <= y1, \
            "panel changed pixels outside the panel rect"
        outside = changed.copy()
        outside[y0:y1 + 1, x0:x1 + 1] = False
        stray = int(outside.sum())
        log(f"changed pixels outside panel rect: {stray}")
        assert stray == 0, f"{stray} pixels changed outside the panel"
    else:
        raise AssertionError("panel produced no pixel change at alpha 0.78")

    inside_changed = int(changed[interior].sum())
    log(f"changed interior pixels {inside_changed} / {int(interior.sum())}")

    # Gradient direction: mean (panel - background) must be brighter at the top.
    delta78 = a78 - bg
    top_band = delta78[iy1 - 30:iy1 - 9, ix0 + 40:ix1 - 60].mean(axis=(0, 1))
    bottom_band = delta78[iy0 + 10:iy0 + 31, ix0 + 40:ix1 - 60].mean(axis=(0, 1))
    log(f"delta(top)={np.round(top_band, 4).tolist()} delta(bottom)="
        f"{np.round(bottom_band, 4).tolist()}")
    lum_top = float(top_band.mean())
    lum_bottom = float(bottom_band.mean())
    log(f"gradient delta luminance top={lum_top:.4f} bottom={lum_bottom:.4f}")
    assert lum_top > lum_bottom, "gradient is not brighter at the top"

    # Transmission measurement across an area boundary inside the panel.
    boundaries = area_boundaries()
    mid_y = (y0 + y1) // 2
    picks = [b for b in boundaries if x0 + 20 < b < x1 - 20]
    log(f"area x boundaries {boundaries}; usable inside panel: {picks}")
    transmission = None
    if picks:
        b = picks[0]
        cols = [(b - 4, b - 4), (b + 4, b + 4)]
        bg_pair = bg[mid_y, [c[0] for c in cols], :]
        p_pair = a78[mid_y, [c[0] for c in cols], :]
        log(f"bg across boundary  x={cols[0][0]},{cols[1][0]} -> {np.round(bg_pair, 4).tolist()}")
        log(f"panel across boundary -> {np.round(p_pair, 4).tolist()}")
        rgb_diff = bg_pair[0] - bg_pair[1]
        panel_diff = p_pair[0] - p_pair[1]
        strong = np.abs(rgb_diff) > 0.03
        if strong.any():
            transmission = float(np.mean(panel_diff[strong] / rgb_diff[strong]))
            log(f"measured transmission coefficient = {transmission:.4f} "
                f"(expected ~{1.0 - 0.78:.2f} for alpha 0.78)")
    # Fallback / complementary check: differences scale linearly with alpha.
    ratio = None
    if np.abs(a78 - a35)[interior].mean() > 1e-4:
        num = np.abs(a78 - bg).mean()
        den = np.abs(a78 - a35).mean()
        ratio = float(num / den)
    log(f"mean |a0.78-bg|={np.abs(a78 - bg)[interior].mean():.4f} "
        f"mean |a0.78-a0.35|={np.abs(a78 - a35)[interior].mean():.4f} "
        f"|a0.78-bg|/|a0.78-a0.35|={ratio if ratio is None else round(ratio, 3)} "
        f"(expected ~{0.78 / (0.78 - 0.35):.2f})")

    # Sample a few concrete pixels for the report.
    samples = {}
    for label, (sx, sy) in {
        "center": ((x0 + x1) // 2, (y0 + y1) // 2),
        "top": ((x0 + x1) // 2, y1 - 20),
        "bottom": ((x0 + x1) // 2, y0 + 20),
    }.items():
        samples[label] = {
            "xy": [sx, sy],
            "bg": [round(float(v) * 255, 1) for v in bg[sy, sx]],
            "alpha0.78": [round(float(v) * 255, 1) for v in a78[sy, sx]],
            "alpha0.35": [round(float(v) * 255, 1) for v in a35[sy, sx]],
        }
        log(f"sample {label} {samples[label]}")

    RESULT.update({
        "window": [W, H],
        "panel_rect": list(rect),
        "changed_bbox": bbox,
        "changed_pixels_outside_panel": stray,
        "changed_interior_pixels": inside_changed,
        "delta_luminance_top": lum_top,
        "delta_luminance_bottom": lum_bottom,
        "gradient_top_brighter": bool(lum_top > lum_bottom),
        "area_x_boundaries": boundaries,
        "transmission_across_boundary": transmission,
        "alpha_ratio_a078_over_a078_minus_a035": ratio,
        "pixel_samples": samples,
    })
    save_crop(a78, "c_03_panel_crop_alpha078.png", x0, y0, x1, y1)


def tick_close_splash():
    """First timer tick: post the closing events, then let the main loop dispatch them."""
    try:
        log("closing splash (click + ESC)")
        WIN.event_simulate(type="LEFTMOUSE", value="PRESS", x=700, y=500)
        WIN.event_simulate(type="LEFTMOUSE", value="RELEASE", x=700, y=500)
        WIN.event_simulate(type="ESC", value="PRESS", x=700, y=500)
        WIN.event_simulate(type="ESC", value="RELEASE", x=700, y=500)
    except Exception as exc:  # noqa: BLE001
        log(f"event_simulate failed: {exc}")
    bpy.app.timers.register(tick_screenshots, first_interval=1.0)
    return None


def tick_screenshots():
    try:
        force_redraw()
        screenshot("c_00_bg_alpha0.png")
        set_alpha(0.78)
        screenshot("c_01_alpha078.png")
        set_alpha(0.35)
        screenshot("c_02_alpha035.png")
        analyze()
        RESULT["status"] = "ok"
    except Exception as exc:  # noqa: BLE001
        RESULT["status"] = "failed"
        RESULT["error"] = repr(exc)
        log(f"FAILED: {exc!r}")
    finally:
        with open(os.path.join(OUT_DIR, "stage_c_result.json"), "w") as fh:
            json.dump(RESULT, fh, indent=2)
        log(f"result written to stage_c_result.json status={RESULT.get('status')}")
        bpy.ops.wm.quit_blender()
    return None


log("stage C script loaded")
bpy.app.timers.register(tick_close_splash, first_interval=1.5)
