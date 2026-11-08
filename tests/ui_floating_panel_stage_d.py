"""Stage D verification: anchor states, expand/collapse animation, lock and per-panel alpha.

Run (GUI, not background, event simulation enabled):
  STAGE_D_OUT=/abs/out/dir EBC_FP_STATE_FILE=/tmp/fp_state.json \
  EBC_FP_TRACE_FILE=/tmp/fp_trace.txt EBC_FP_BLUR=1 \
  build_linux/bin/blender --factory-startup --enable-event-simulate \
  --python tests/ui_floating_panel_stage_d.py

The C++ panel writes a live state snapshot (`EBC_FP_STATE_FILE`, JSON, every frame) and a
per-frame trace (`EBC_FP_TRACE_FILE`) so all checks use real geometry and real timings.

Timing protocol: simulated input is queued in the window event queue. Every input step is
followed by a >=0.15 s pause without blocking draws, so the animation timers are not stalled.
The clean expand/collapse cycle contains no screenshots; the mid-frame screenshots are taken
in a separate cycle.

Milestones:
  1. anchor idle/hover pixel alpha, hover bbox ratio, hover ramp timing;
  2. expand/collapse duration + mid-animation frames with their live rect;
  3. per-panel alpha vs global env, lock (drag + slider) invariance.
"""

import json
import os
import time

import bpy
import numpy as np

OUT_DIR = os.environ.get("STAGE_D_OUT", "/tmp/ui_floating_stage_d")
os.makedirs(OUT_DIR, exist_ok=True)
LOG_FILE = open(os.path.join(OUT_DIR, "stage_d_log.txt"), "w", buffering=1)
RESULT = {}
SHOTS = {}

STATE_FILE = os.environ["EBC_FP_STATE_FILE"]
TRACE_FILE = os.environ["EBC_FP_TRACE_FILE"]

WIN = bpy.context.window
W, H = WIN.width, WIN.height

# Shader byte colours (mirror interface_region_floating_panel.cc).
TOP_BYTE = np.array([69.0, 61.0, 61.0])
BOTTOM_BYTE = np.array([11.0, 3.0, 3.0])
ANCHOR_BANDS = 12
ANCHOR_SIZE = 44


def log(msg):
    text = f"[STAGE-D] {msg}"
    print(text, flush=True)
    LOG_FILE.write(text + "\n")


def screenshot(name):
    path = os.path.join(OUT_DIR, name)
    with bpy.context.temp_override(window=WIN, screen=WIN.screen):
        bpy.ops.screen.screenshot(filepath=path)
    if not os.path.exists(path):
        log(f"screenshot MISSING {name}")
    SHOTS[name] = read_state()
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


def redraw(n=2):
    try:
        with bpy.context.temp_override(window=WIN, screen=WIN.screen):
            bpy.ops.wm.redraw_timer(type="DRAW_WIN_SWAP", iterations=n)
    except Exception as exc:  # noqa: BLE001
        log(f"redraw_timer failed: {exc}")


def read_state():
    for _ in range(8):
        try:
            with open(STATE_FILE) as fh:
                return json.loads(fh.read())
        except (OSError, json.JSONDecodeError):
            time.sleep(0.004)
    return None


def sim(kind, val, x, y):
    WIN.event_simulate(type=kind, value=val, x=int(x), y=int(y))


def move(x, y):
    sim("MOUSEMOVE", "NOTHING", x, y)


def click(x, y):
    sim("LEFTMOUSE", "PRESS", x, y)
    sim("LEFTMOUSE", "RELEASE", x, y)


def schedule(fn, delay):
    bpy.app.timers.register(fn, first_interval=delay)


def rect_slice(rect, pad=0, x_max=None):
    x0, x1, y0, y1 = rect
    x0 = max(0, x0 - pad)
    y0 = max(0, y0 - pad)
    x1 = min(W - 1 if x_max is None else x_max, x1 + pad)
    y1 = min(H - 1, y1 + pad)
    return slice(y0, y1 + 1), slice(x0, x1 + 1)


def bbox_of_diff(ref, cur, search, threshold=4.0):
    ry, rx = rect_slice(search, 0)
    diff = np.abs(cur[ry, rx] - ref[ry, rx]).max(axis=2)
    ys, xs = np.nonzero(diff > threshold)
    if len(xs) == 0:
        return None
    return [int(xs.min()) + rx.start, int(xs.max()) + rx.start,
            int(ys.min()) + ry.start, int(ys.max()) + ry.start]


def anchor_glass_rgb(row_y, rect):
    h = rect[3] - rect[2] + 1
    pos = (row_y - rect[2] + 0.5) * ANCHOR_BANDS / h
    band = int(pos)
    t = (band + 0.5) / ANCHOR_BANDS
    return BOTTOM_BYTE + (TOP_BYTE - BOTTOM_BYTE) * t


def anchor_alpha_measure(arr, rect, x_edge=None):
    """out = a * glass + (1 - a) * backdrop. Backdrop is sampled right of the anchor, which is
    clean flat viewport background; the left side has stray viewport widgets.

    `x_edge` is the last usable viewport column (the bright 1px viewport/editor border sits at
    `x_edge + 1`); the backdrop sample must stay strictly left of it, otherwise the border crease
    contaminates the estimate. Rows where the glass colour and backdrop differ by less than 25
    bytes are skipped: there the byte quantisation dominates and the ratio is meaningless."""
    x0, x1, y0, y1 = rect
    h = y1 - y0 + 1
    if x_edge is None:
        x_edge = W - 1
    bg_x0, bg_x1 = x1 + 1, min(x_edge - 1, x1 + 4)
    if bg_x1 < bg_x0:
        return None, []
    alphas = []
    for y in range(y0 + 4, y1 - 3):
        pos = (y - y0 + 0.5) * ANCHOR_BANDS / h
        if not (0 <= pos < ANCHOR_BANDS) or abs(pos - round(pos)) < 0.2:
            continue
        c = anchor_glass_rgb(y, rect)
        bg = arr[y, bg_x0:bg_x1 + 1, :].mean(axis=0)
        out = arr[y, x0 + 4:x1 - 3, :].mean(axis=0)
        denom = c - bg
        ok = np.all(np.abs(denom) > 25.0)
        if not ok:
            continue
        alphas.append(float(np.mean((out - bg) / denom)))
    return (float(np.median(alphas)) if alphas else None), alphas


def panel_alpha_measure(cur, ref_bg, ref_opaque, rect):
    """Empirical two-point calibration: alpha 0 = backdrop, alpha 1 = glass colour."""
    band = (slice(rect[2] + 62, rect[2] + 120), slice(rect[0] + 20, rect[0] + 300))
    c = ref_opaque[band]
    bg = ref_bg[band]
    out = cur[band]
    denom = c - bg
    if np.abs(denom).max() < 10.0:
        return None
    ok = np.abs(denom) > 12.0
    if ok.sum() < 50:
        return None
    return float(np.median((out[ok] - bg[ok]) / denom[ok]))


def region_diff(a, b, rect, inset=0):
    ry, rx = rect_slice(rect, -inset)
    d = np.abs(a[ry, rx] - b[ry, rx])
    return {"max_abs": round(float(d.max()), 2),
            "changed_px": int((d.max(axis=2) > 0.5).sum()),
            "pixels": int(d.shape[0] * d.shape[1])}


def coverage(arr, ref, rect):
    """How much of the expected glass rect actually changed, and how much did not change in
    the ring right outside it (alignment check). Viewport-left half only, to avoid the dark
    properties editor where a 0.78 glass tint is nearly invisible."""
    x0, x1, y0, y1 = rect
    strip_x1 = min(x1 - 8, x0 + 200)
    ry, rx = rect_slice((x0 + 8, strip_x1, y0 + 8, y1 - 8))
    d = np.abs(arr[ry, rx] - ref[ry, rx]).max(axis=2)
    inside = float((d > 4.0).mean())
    out_rect = (x0 - 12, x1 + 12, y0 - 12, y1 + 12)
    ry2, rx2 = rect_slice(out_rect)
    ring = np.abs(arr[ry2, rx2] - ref[ry2, rx2]).max(axis=2)
    changed = ring > 4.0
    ring_frac = float(changed.mean())
    return {"inside_changed": round(inside, 3), "ring_changed": round(ring_frac, 3),
            "mean_delta": round(float(d.mean()), 2)}


# ---------------------------------------------------------------------------
# Step chain
# ---------------------------------------------------------------------------
def close_splash():
    log("closing splash")
    click(700, 500)
    sim("ESC", "PRESS", 700, 500)
    sim("ESC", "RELEASE", 700, 500)
    schedule(step_idle, 1.4)


def step_idle():
    os.environ["EBC_FP_ALPHA"] = "0.78"
    os.environ["EBC_FP_BLUR"] = "1"
    move(80, 40)
    schedule(step_idle_shot, 0.3)


def step_idle_shot():
    redraw(3)
    st = read_state()
    if st is None or st["view"] != "anchor":
        schedule(step_idle_shot, 0.4)
        return
    RESULT["initial_state"] = st
    log(f"state view={st['view']} anchor={st['anchor']} size={st['anchor_size']} win={st['win']}")
    screenshot("d_00_anchor_idle.png")
    schedule(step_hover_move, 0.3)


def step_hover_move():
    st = read_state()
    a = st["anchor"]
    cx, cy = (a[0] + a[1]) // 2, (a[2] + a[3]) // 2
    RESULT["anchor_center"] = [cx, cy]
    log(f"hover move to anchor centre {cx},{cy}")
    move(cx, cy)
    schedule(poll_hover_in, 0.02)


def poll_hover_in():
    st = read_state()
    if st and 1.0 <= st["hover_p"] <= 1.0001:
        log(f"hover settled: hover_p={st['hover_p']} hover_ms={st['hover_ms']}")
        RESULT["hover_settled_state"] = st
        redraw(1)
        screenshot("d_01_anchor_hover.png")
        schedule(step_hover_out, 0.2)
        return
    schedule(poll_hover_in, 0.02)


def step_hover_out():
    move(80, 40)
    schedule(poll_hover_out, 0.02)


def poll_hover_out(deadline=None):
    st = read_state()
    if st and st["hover_p"] <= 0.001:
        log(f"hover out settled: hover_p={st['hover_p']} hover_ms={st['hover_ms']}")
        RESULT["hover_out_state"] = st
        redraw(2)
        screenshot("d_02_anchor_idle_again.png")
        schedule(expand_clean_click, 0.3)
        return
    schedule(poll_hover_out, 0.02)


def expand_clean_click():
    st = read_state()
    a = st["anchor"]
    cx, cy = (a[0] + a[1]) // 2, (a[2] + a[3]) // 2
    log(f"[clean] expand click {cx},{cy}")
    RESULT["clean_expand_t0"] = time.time()
    click(cx, cy)
    schedule(poll_clean_expand, 0.05)


def poll_clean_expand():
    st = read_state()
    if st and st["view"] == "expanded" and not st["anim"]:
        RESULT["clean_expand_state"] = st
        log(f"[clean] expand done anim_ms={st['anim_ms']} (wall "
            f"{(time.time() - RESULT['clean_expand_t0']) * 1000:.0f} ms) rect={st['rect']}")
        schedule(expand_shot, 0.2)
        return
    schedule(poll_clean_expand, 0.03)


def expand_shot():
    redraw(3)
    st = read_state()
    RESULT["expanded_state"] = st
    screenshot("d_14_expanded.png")
    log(f"expanded rect={st['rect']} alpha={st['alpha']}")
    schedule(collapse_clean_click, 0.25)


def collapse_clean_click():
    st = read_state()
    mn = st["min"]
    cx, cy = (mn[0] + mn[1]) // 2, (mn[2] + mn[3]) // 2
    log(f"[clean] minimize click {cx},{cy}")
    RESULT["clean_collapse_t0"] = time.time()
    click(cx, cy)
    schedule(poll_clean_collapse, 0.05)


def poll_clean_collapse():
    st = read_state()
    if st and st["view"] == "anchor" and not st["anim"]:
        RESULT["clean_collapse_state"] = st
        log(f"[clean] collapse done anim_ms={st['anim_ms']} (wall "
            f"{(time.time() - RESULT['clean_collapse_t0']) * 1000:.0f} ms) rect={st['rect']}")
        schedule(anchor_back_shot, 0.2)
        return
    schedule(poll_clean_collapse, 0.03)


def anchor_back_shot():
    move(80, 40)
    redraw(3)
    screenshot("d_15_anchor_after_clean_collapse.png")
    schedule(expand_visual_click, 0.3)


MID_SHOTS = (("d_10_expand_050.png", 0.050), ("d_11_expand_110.png", 0.110),
             ("d_12_expand_170.png", 0.170), ("d_13_expand_240.png", 0.240))


def expand_visual_click():
    st = read_state()
    a = st["anchor"]
    cx, cy = (a[0] + a[1]) // 2, (a[2] + a[3]) // 2
    log(f"[visual] expand click {cx},{cy}")
    RESULT["visual_expand_t0"] = time.time()
    click(cx, cy)
    schedule(lambda: expand_mid(0), 0.0)


def expand_mid(i):
    if i >= len(MID_SHOTS):
        schedule(visual_expanded, 0.4)
        return
    name, delay = MID_SHOTS[i]
    target = RESULT["visual_expand_t0"] + delay
    now = time.time()
    if now < target - 0.004:
        schedule(lambda: expand_mid(i), target - now)
        return
    before = read_state()
    redraw(1)
    screenshot(name)
    after = read_state()
    RESULT.setdefault("expand_mid", []).append(
        {"name": name, "offset_ms": round((time.time() - RESULT["visual_expand_t0"]) * 1000, 1),
         "state_before": before, "state_after": after})
    log(f"mid {name}: offset={RESULT['expand_mid'][-1]['offset_ms']} "
        f"anim_ms before={before and before['anim_ms']} after={after and after['anim_ms']}")
    schedule(lambda: expand_mid(i + 1), 0.0)


def visual_expanded():
    st = read_state()
    if st and st["view"] == "expanded" and not st["anim"]:
        redraw(2)
        RESULT["expanded_state"] = st
        screenshot("d_16_expanded_visual.png")
        schedule(calibration, 0.2)
        return
    schedule(visual_expanded, 0.1)


def calibration():
    st = read_state()
    RESULT["cal_rect"] = st["rect"]
    os.environ["EBC_FP_ALPHA"] = "1.0"
    redraw(3)
    RESULT["cal_opaque_state"] = read_state()
    screenshot("d_20_cal_alpha100.png")
    os.environ["EBC_FP_ALPHA"] = "0.0"
    redraw(3)
    RESULT["cal_bg_state"] = read_state()
    screenshot("d_21_cal_alpha000.png")
    os.environ["EBC_FP_ALPHA"] = "0.78"
    redraw(3)
    schedule(slider_hi_click, 0.2)


def slider_track_x(st, frac):
    x0 = st["rect"][0] + 16
    x1 = st["rect"][1] - 16
    return int(x0 + (x1 - x0) * frac), (st["slider"][2] + st["slider"][3]) // 2


def slider_hi_click():
    st = read_state()
    x, y = slider_track_x(st, 0.82)
    log(f"slider click 82% at {x},{y}")
    click(x, y)
    schedule(slider_hi_check, 0.2)


def slider_hi_check():
    st = read_state()
    RESULT["slider_hi_state"] = st
    log(f"slider hi state alpha={st['alpha']} override={st['override']}")
    redraw(3)
    screenshot("d_22_slider_hi.png")
    os.environ["EBC_FP_ALPHA"] = "0.05"
    schedule(slider_hi_probe, 0.15)


def slider_hi_probe():
    st = read_state()
    RESULT["slider_hi_global_low_state"] = st
    log(f"global env 0.05 under slider override: alpha={st['alpha']} override={st['override']}")
    redraw(3)
    screenshot("d_23_slider_hi_global_low.png")
    os.environ["EBC_FP_ALPHA"] = "0.78"
    schedule(slider_lo_click, 0.15)


def slider_lo_click():
    st = read_state()
    x, y = slider_track_x(st, 0.18)
    log(f"slider click 18% at {x},{y}")
    click(x, y)
    schedule(slider_lo_check, 0.2)


def slider_lo_check():
    st = read_state()
    RESULT["slider_lo_state"] = st
    log(f"slider lo state alpha={st['alpha']} override={st['override']}")
    redraw(3)
    screenshot("d_24_slider_lo.png")
    st = read_state()
    x, y = slider_track_x(st, 1.0)
    log(f"slider click 100% at {x},{y}")
    click(x, y)
    schedule(slider_max_check, 0.2)


def slider_max_check():
    st = read_state()
    RESULT["slider_max_state"] = st
    log(f"slider max state alpha={st['alpha']}")
    redraw(3)
    screenshot("d_25_slider_max.png")
    move(80, 40)
    schedule(hover_ref, 0.25)


def hover_ref():
    redraw(3)
    RESULT["hover_ref_state"] = read_state()
    screenshot("d_26_hover_ref.png")
    st = read_state()
    lk = st["lock"]
    cx, cy = (lk[0] + lk[1]) // 2, (lk[2] + lk[3]) // 2
    log(f"hover onto lock button {cx},{cy}")
    move(cx, cy)
    schedule(hover_button_shot, 0.25)


def hover_button_shot():
    st = read_state()
    RESULT["hover_button_state"] = st
    redraw(2)
    screenshot("d_27_hover_lock_button.png")
    schedule(drag_gate_press, 0.2)


def drag_gate_press():
    st = read_state()
    r = st["rect"]
    start = (r[0] + 60, r[3] - 8)
    lk = st["lock"]
    target = ((lk[0] + lk[1]) // 2, (lk[2] + lk[3]) // 2)
    log(f"drag gate: press {start} drag onto {target}")
    sim("LEFTMOUSE", "PRESS", start[0], start[1])
    move(start[0] + 4, start[1] + 2)
    move(target[0], target[1])
    schedule(drag_gate_shot, 0.25)


def drag_gate_shot():
    st = read_state()
    RESULT["drag_gate_state"] = st
    redraw(2)
    screenshot("d_28_dragging_over_button.png")
    log(f"drag gate state action={st['action']} hover={st['hover']} rect={st['rect']}")
    sim("LEFTMOUSE", "RELEASE", st["rect"][0] + 60, st["rect"][1] - 8)
    schedule(drag_gate_release, 0.25)


def drag_gate_release():
    move(80, 40)
    redraw(3)
    st = read_state()
    RESULT["after_drag_gate_state"] = st
    click((st["lock"][0] + st["lock"][1]) // 2, (st["lock"][2] + st["lock"][3]) // 2)
    schedule(lock_check, 0.3)


def lock_check():
    st = read_state()
    RESULT["locked_state"] = st
    log(f"locked={st['locked']} alpha={st['alpha']} override={st['override']} rect={st['rect']}")
    move(80, 40)
    schedule(lock_before, 0.2)


def lock_before():
    redraw(3)
    screenshot("d_30_locked.png")
    schedule(lock_control, 0.3)


def lock_control():
    redraw(3)
    screenshot("d_31_locked_control.png")
    schedule(lock_drag_attempt, 0.2)


def lock_drag_attempt():
    st = read_state()
    r = st["rect"]
    x, y = r[0] + 60, r[3] - 8
    log(f"locked drag attempt from {x},{y}")
    sim("LEFTMOUSE", "PRESS", x, y)
    for i in range(1, 7):
        move(x + 8 * i, y + 3 * i)
    sim("LEFTMOUSE", "RELEASE", x + 48, y + 18)
    move(80, 40)
    schedule(lock_drag_shot, 0.3)


def lock_drag_shot():
    redraw(3)
    RESULT["after_lock_drag_state"] = read_state()
    screenshot("d_32_locked_after_drag.png")
    st = read_state()
    x = st["slider"][1] - 4
    y = (st["slider"][2] + st["slider"][3]) // 2
    log(f"locked slider attempt at {x},{y}")
    click(x, y)
    move(80, 40)
    schedule(lock_slider_shot, 0.3)


def lock_slider_shot():
    redraw(3)
    RESULT["after_lock_slider_state"] = read_state()
    screenshot("d_33_locked_after_slider.png")
    st = read_state()
    mn = st["min"]
    cx, cy = (mn[0] + mn[1]) // 2, (mn[2] + mn[3]) // 2
    log(f"minimize click {cx},{cy}")
    RESULT["final_collapse_t0"] = time.time()
    click(cx, cy)
    schedule(lambda: collapse_mid(0), 0.0)


COLL_MID = (("d_34_collapse_060.png", 0.060), ("d_35_collapse_150.png", 0.150))


def collapse_mid(i):
    if i >= len(COLL_MID):
        schedule(poll_collapsed, 0.35)
        return
    name, delay = COLL_MID[i]
    target = RESULT["final_collapse_t0"] + delay
    now = time.time()
    if now < target - 0.004:
        schedule(lambda: collapse_mid(i), target - now)
        return
    before = read_state()
    redraw(1)
    screenshot(name)
    after = read_state()
    RESULT.setdefault("collapse_mid", []).append(
        {"name": name, "state_before": before, "state_after": after})
    log(f"collapse mid {name}: before={before and before['rect']} after={after and after['rect']}")
    schedule(lambda: collapse_mid(i + 1), 0.0)


def poll_collapsed():
    st = read_state()
    if st and st["view"] == "anchor" and not st["anim"]:
        RESULT["collapsed_state"] = st
        log(f"final collapsed anim_ms={st['anim_ms']} rect={st['rect']}")
        move(80, 40)
        schedule(final_shot, 0.25)
        return
    schedule(poll_collapsed, 0.05)


def final_shot():
    redraw(3)
    screenshot("d_40_anchor_back.png")
    schedule(analyze, 0.2)


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------
def do_analyze():
    global W, H
    idle = load_rgb(os.path.join(OUT_DIR, "d_00_anchor_idle.png"))
    hover = load_rgb(os.path.join(OUT_DIR, "d_01_anchor_hover.png"))
    idle2 = load_rgb(os.path.join(OUT_DIR, "d_02_anchor_idle_again.png"))
    H, W = idle.shape[0], idle.shape[1]
    log(f"image {W}x{H} window {WIN.width}x{WIN.height}")

    st_idle = RESULT["initial_state"]
    a_idle_r = st_idle["anchor"]
    st_hov = RESULT["hover_settled_state"]
    a_hov_r = st_hov["anchor"]

    # --- milestone 1: anchor alpha + bbox ratio -----------------------------
    # The 1px viewport/editor border sits 6 px right of the idle anchor's right edge; it must be
    # excluded from both the backdrop sample and the hover bbox search window.
    x_edge = a_idle_r[1] + 6
    a_idle, rows_idle = anchor_alpha_measure(idle, a_idle_r, x_edge)
    a_hov, rows_hov = anchor_alpha_measure(hover, a_hov_r, x_edge)
    a_idle2, _ = anchor_alpha_measure(idle2, a_idle_r, x_edge)
    log(f"anchor alpha idle={a_idle} hover={a_hov} idle_again={a_idle2} "
        f"rows n_idle={len(rows_idle)} n_hover={len(rows_hov)}")
    RESULT["anchor_alpha"] = {
        "idle": round(a_idle, 4) if a_idle else None,
        "idle_again": round(a_idle2, 4) if a_idle2 else None,
        "hover": round(a_hov, 4) if a_hov else None,
        "row_samples_idle": [round(v, 3) for v in rows_idle],
        "row_samples_hover": [round(v, 3) for v in rows_hov],
    }

    search = (a_idle_r[0] - 20, min(x_edge - 1, a_idle_r[1] + 20),
              a_idle_r[2] - 20, a_idle_r[3] + 20)
    bb = bbox_of_diff(idle, hover, search, 4.0)
    ratio = None
    if bb:
        bw, bh = bb[1] - bb[0] + 1, bb[3] - bb[2] + 1
        ratio = max(bw, bh) / float(ANCHOR_SIZE)
        log(f"hover bbox {bb} = {bw}x{bh}, ratio {ratio:.4f}; "
            f"state anchor {a_idle_r} -> {a_hov_r}")
    RESULT["anchor_hover_bbox"] = {"bbox": bb, "ratio": ratio,
                                   "state_idle": a_idle_r, "state_hover": a_hov_r}

    trace = []
    if os.path.exists(TRACE_FILE):
        with open(TRACE_FILE) as fh:
            for line in fh:
                parts = line.split()
                if len(parts) >= 11:
                    trace.append([float(v) for v in parts])
    log(f"trace lines {len(trace)}")

    ramp = RESULT.get("hover_ramp") or {}
    ramp_in = [row for row in trace if 0.001 < row[1] < 0.999]
    ramp_out = [row for row in trace if row[1] > 0.0001 and row[4] > 0]
    RESULT["hover_ramp"] = {
        "cpp_in_ms": st_hov["hover_ms"],
        "cpp_out_ms": RESULT["hover_out_state"]["hover_ms"],
        "rising_frames": len(ramp_in),
        "trace_first_last_ms": (round((max(r[0] for r in ramp_in) -
                                      min(r[0] for r in ramp_in)) * 1000, 1)
                                if ramp_in else None),
        "samples_in": [[round(r[4], 1), round(r[1], 3)] for r in ramp_in][:16],
        "samples_out": [[round(r[4], 1), round(r[1], 3)] for r in ramp_out][:16],
    }
    log(f"hover ramp cpp in={st_hov['hover_ms']} out={RESULT['hover_out_state']['hover_ms']} "
        f"rising frames={len(ramp_in)}")

    # --- milestone 2: expand/collapse --------------------------------------
    exp = RESULT["clean_expand_state"]
    col = RESULT["clean_collapse_state"]
    anim_rows = [row for row in trace if row[2] == 1]
    shrinking = [row for row in anim_rows if row[10] < 240]
    RESULT["expand_anim"] = {
        "cpp_ms": exp["anim_ms"], "rect": exp["rect"],
        "frames": [[round(r[3], 1), int(r[9]), int(r[10])] for r in anim_rows
                   if r[10] >= 44][:30],
        "total_frames": len(anim_rows),
    }
    RESULT["collapse_anim"] = {
        "cpp_ms": col["anim_ms"], "rect": col["rect"],
        "frames": [[round(r[3], 1), int(r[9]), int(r[10])] for r in shrinking][:30],
        "total_frames": len(shrinking),
    }
    log(f"expand anim_ms={exp['anim_ms']} rect={exp['rect']}; "
        f"collapse anim_ms={col['anim_ms']} rect={col['rect']}")
    log(f"expand frames {RESULT['expand_anim']['frames']}")

    ref_rect = RESULT["expanded_state"]["rect"]
    mid = []
    for entry in RESULT.get("expand_mid", []):
        arr = load_rgb(os.path.join(OUT_DIR, entry["name"]))
        st = entry["state_after"] or entry["state_before"]
        cov = coverage(arr, idle, st["rect"])
        bb2 = bbox_of_diff(idle, arr, st["rect"], 4.0)
        mid.append({"name": entry["name"], "offset_ms": entry["offset_ms"],
                    "anim_ms": st["anim_ms"], "rect": st["rect"],
                    "image_bbox": bb2, "coverage": cov,
                    "rect_w": st["rect"][1] - st["rect"][0] + 1,
                    "rect_h": st["rect"][3] - st["rect"][2] + 1})
        log(f"mid {entry['name']}: anim_ms={st['anim_ms']} rect={st['rect']} "
            f"bbox={bb2} coverage={cov}")
    RESULT["expand_mid_frames"] = mid

    cmid = []
    for entry in RESULT.get("collapse_mid", []):
        st = entry["state_after"] or entry["state_before"]
        cmid.append({"name": entry["name"], "anim_ms": st["anim_ms"], "rect": st["rect"]})
    RESULT["collapse_mid_frames"] = cmid
    log(f"collapse mid frames {cmid}")

    # --- milestone 3: slider vs global -------------------------------------
    cal = RESULT["cal_rect"]
    opaque = load_rgb(os.path.join(OUT_DIR, "d_20_cal_alpha100.png"))
    bgimg = load_rgb(os.path.join(OUT_DIR, "d_21_cal_alpha000.png"))
    hi = load_rgb(os.path.join(OUT_DIR, "d_22_slider_hi.png"))
    hig = load_rgb(os.path.join(OUT_DIR, "d_23_slider_hi_global_low.png"))
    lo = load_rgb(os.path.join(OUT_DIR, "d_24_slider_lo.png"))

    band = (slice(cal[2] + 62, cal[2] + 120), slice(cal[0] + 20, cal[0] + 300))
    log(f"opaque calibration median={float(np.median(opaque[band])):.1f} (byte space)")
    a_hi = panel_alpha_measure(hi, bgimg, opaque, cal)
    a_lo = panel_alpha_measure(lo, bgimg, opaque, cal)
    a_hig = panel_alpha_measure(hig, bgimg, opaque, cal)
    log(f"pixel alpha hi={a_hi} lo={a_lo} hi_under_global0.05={a_hig}")
    RESULT["slider"] = {
        "cpp_hi": RESULT["slider_hi_state"]["alpha"],
        "cpp_lo": RESULT["slider_lo_state"]["alpha"],
        "pixel_hi": None if a_hi is None else round(a_hi, 4),
        "pixel_lo": None if a_lo is None else round(a_lo, 4),
        "pixel_hi_under_global_low": None if a_hig is None else round(a_hig, 4),
        "transmission_hi": None if a_hi is None else round(1.0 - a_hi, 4),
        "transmission_lo": None if a_lo is None else round(1.0 - a_lo, 4),
        "env_global": 0.78,
        "env_global_during_probe": 0.05,
        "global_probe_diff": region_diff(hi, hig, cal),
    }

    # --- hover gate (hover only with action == None) -----------------------
    st_ref = RESULT["hover_ref_state"]
    st_on = RESULT["hover_button_state"]
    st_drag = RESULT["drag_gate_state"]
    ref = load_rgb(os.path.join(OUT_DIR, "d_26_hover_ref.png"))
    on = load_rgb(os.path.join(OUT_DIR, "d_27_hover_lock_button.png"))
    drag = load_rgb(os.path.join(OUT_DIR, "d_28_dragging_over_button.png"))
    d_hover = region_diff(ref, on, st_ref["lock"])
    d_drag = region_diff(ref, drag, st_ref["lock"])
    log(f"hover gate: hover icon diff={d_hover} ; during drag action={st_drag['action']} "
        f"hover={st_drag['hover']} icon diff={d_drag}")

    # --- lock invariance ---------------------------------------------------
    locked = load_rgb(os.path.join(OUT_DIR, "d_30_locked.png"))
    control = load_rgb(os.path.join(OUT_DIR, "d_31_locked_control.png"))
    after_drag = load_rgb(os.path.join(OUT_DIR, "d_32_locked_after_drag.png"))
    after_slider = load_rgb(os.path.join(OUT_DIR, "d_33_locked_after_slider.png"))
    lr = RESULT["locked_state"]["rect"]
    d_ctrl = region_diff(locked, control, lr, inset=2)
    d_drag = region_diff(locked, after_drag, lr, inset=2)
    d_slider = region_diff(locked, after_slider, lr, inset=2)
    s1 = RESULT["locked_state"]
    s2 = RESULT["after_lock_drag_state"]
    s3 = RESULT["after_lock_slider_state"]
    log(f"lock control diff={d_ctrl}")
    log(f"lock drag diff={d_drag} rect {s1['rect']}->{s2['rect']} alpha {s1['alpha']}->{s2['alpha']}")
    log(f"lock slider diff={d_slider} rect {s2['rect']}->{s3['rect']} alpha {s2['alpha']}->{s3['alpha']}")
    RESULT["lock"] = {
        "locked": s1["locked"],
        "control_diff": d_ctrl, "drag_diff": d_drag, "slider_diff": d_slider,
        "rect_before": s1["rect"], "rect_after_drag": s2["rect"],
        "rect_after_slider": s3["rect"],
        "alpha_before": s1["alpha"], "alpha_after_drag": s2["alpha"],
        "alpha_after_slider": s3["alpha"],
        "state_unchanged": (s1["rect"] == s2["rect"] == s3["rect"] and
                            s1["alpha"] == s2["alpha"] == s3["alpha"]),
    }

    # --- collapse back to the anchor ---------------------------------------
    back = load_rgb(os.path.join(OUT_DIR, "d_40_anchor_back.png"))
    stc = RESULT["collapsed_state"]
    d_back = region_diff(idle, back, a_idle_r)
    log(f"final collapse: anim_ms={stc['anim_ms']} rect={stc['rect']} expected={a_idle_r} "
        f"pixel diff={d_back}")
    RESULT["collapse"] = {"cpp_anim_ms": stc["anim_ms"], "rect": stc["rect"],
                          "anchor_rect_idle": a_idle_r,
                          "rect_matches": stc["rect"] == a_idle_r,
                          "pixel_diff": d_back}

    verdicts = {
        "anchor_idle_15_20": a_idle not in (None,) and 0.15 <= a_idle <= 0.20,
        "anchor_hover_about_60": a_hov is not None and 0.55 <= a_hov <= 0.65,
        "hover_scale_1_06": ratio is not None and 1.03 <= ratio <= 1.09,
        "hover_ramp_150_200": bool(st_hov["hover_ms"]) and 150 <= st_hov["hover_ms"] <= 200,
        "expand_300ms": 285 <= exp["anim_ms"] <= 330,
        "collapse_300ms": 285 <= col["anim_ms"] <= 330,
        "slider_overrides_global": (
            a_hi is not None and a_hig is not None and abs(a_hi - a_hig) < 0.02 and
            s1["locked"] and 0.7 <= RESULT["slider_hi_state"]["alpha"] <= 1.0),
        "lock_drag_noop": (s1["rect"] == s2["rect"] and s1["alpha"] == s2["alpha"] and
                           d_drag["max_abs"] <= 1.0),
        "lock_slider_noop": (s2["rect"] == s3["rect"] and s2["alpha"] == s3["alpha"] and
                             d_slider["max_abs"] <= 1.0),
        "collapse_returns_anchor": stc["rect"] == a_idle_r and d_back["max_abs"] <= 1.0,
    }
    RESULT["verdicts"] = verdicts
    log(f"verdicts {verdicts}")
    RESULT["status"] = "ok"


def analyze():
    try:
        do_analyze()
    except Exception as exc:  # noqa: BLE001
        RESULT["status"] = "failed"
        RESULT["error"] = repr(exc)
        log(f"ANALYSIS FAILED: {exc!r}")
    finally:
        with open(os.path.join(OUT_DIR, "stage_d_result.json"), "w") as fh:
            json.dump(RESULT, fh, indent=2, default=str)
        log("result written")
        bpy.ops.wm.quit_blender()


log("stage D script loaded")
bpy.app.timers.register(close_splash, first_interval=1.6)
