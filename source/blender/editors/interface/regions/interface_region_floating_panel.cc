/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edinterface
 *
 * Floating UI panel: a real #ARegion in #bScreen::regionbase (the same container used by menus),
 * not an overlay painted by Python. Screen-level regions are free of #ScrArea layout constraints,
 * are composited on top of every area (see `wm_draw_window_onscreen`) and never become the event
 * context region, so input passes through to the editor below by default.
 *
 * Stage A: static, non-interactive rectangle.
 * Stage B: title-bar/body drag move + one bottom-right corner resize handle, with input
 * interception strictly limited to the panel rectangle.
 *
 * How input is taken over without touching the global event dispatch:
 * - A UI handler is registered in `win->runtime->modalhandlers`, which `wm_event_do_handlers`
 *   processes before area/region handlers. Inside the panel rect it returns `WM_UI_HANDLER_BREAK`
 *   (the editor below never sees the press/motion/release), outside it returns
 *   `WM_UI_HANDLER_CONTINUE` so the existing chain is untouched.
 * - The handler is registered with a null context (no area/region), so no keymap or active-region
 *   state is changed. Popups/menus are added later and therefore prepended to the same list, so
 *   they always get input first. Modal operators are also prepended; they run first as well.
 *
 * Stage C: optional backdrop blur (see `docs/UI_FLOATING_PANEL_STAGE_C.md`).
 * Stage D: anchor (collapsed) state, expand/collapse animation, hover, lock, per-panel alpha
 * slider and the full colour system (see `docs/UI_FLOATING_PANEL_STAGE_D.md`).
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.hh"
#include "BLI_rect.hh"
#include "BLI_string.hh"
#include "BLI_time.hh"
#include "BLI_utildefines.hh"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_screen.hh"

#include "BLF_api.hh"

#include "GPU_context.hh"
#include "GPU_framebuffer.hh"
#include "GPU_immediate.hh"
#include "GPU_immediate_util.hh"
#include "GPU_matrix.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "GPU_viewport.hh"

#include "UI_interface_c.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "ED_node_c.hh"
#include "ED_screen.hh"

#include "BKE_context.hh"
#include "BKE_main.hh"
#include "BKE_node_runtime.hh"

#include "DNA_node_types.h"

#include "UI_view2d.hh"

/* Handler list internals (`win->runtime->modalhandlers`), needed to avoid double registration and
 * to detect running modal operators. Only read, never modified here. */
#include "wm_draw.hh"
#include "wm_event_system.hh"

namespace blender::ui {

/** \name Panel constants
 * \{ */

/** Initial panel size in window pixels. Enlarged in stage E so the embedded node-editor host
 * remains usable (the content rect is roughly 540 x 308 px). */
static constexpr int PANEL_WIDTH = 560;
static constexpr int PANEL_HEIGHT = 380;
/** Hard lower bound for resize. */
static constexpr int PANEL_MIN_WIDTH = 160;
static constexpr int PANEL_MIN_HEIGHT = 120;
/** Size of the single bottom-right resize handle (window pixels). */
static constexpr int RESIZE_HANDLE_SIZE = 14;
/** Horizontal/vertical anchor as a fraction of the window. The default layout splits the 3D view
 * from the properties editor around x=0.72 of the window, so this position straddles that split.
 * Kept below the startup splash as well. */
static constexpr float PANEL_X_FAC = 0.70f;
static constexpr float PANEL_Y_FAC = 0.12f;
/** Glass body gradient, top -> bottom: `#453D3D` -> `#0B0303`. This is the stage-C fallback
 * (no blur) and stays usable on its own. Alpha is read from `EBC_FP_ALPHA` at runtime. */
static constexpr float PANEL_TOP_COLOR[3] = {69.0f / 255.0f, 61.0f / 255.0f, 61.0f / 255.0f};
static constexpr float PANEL_BOTTOM_COLOR[3] = {11.0f / 255.0f, 3.0f / 255.0f, 3.0f / 255.0f};
/** Default glass alpha, adjustable through `EBC_FP_ALPHA` (0.0 - 1.0). */
static constexpr float PANEL_ALPHA_DEFAULT = 0.78f;
/** 1px outline `#85B2B2` at low opacity (15% - 25% range from the design brief). */
static constexpr float PANEL_BORDER_COLOR[3] = {133.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f};
static constexpr float PANEL_BORDER_ALPHA = 0.20f;
/** Resize handle color `#85B2B2` (temporary placeholder, not the final color system). */
static constexpr float HANDLE_COLOR[4] = {
    133.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f, 0.55f};

/* --------------------------------------------------------------------------------------
 * Stage D design system.
 * -------------------------------------------------------------------------------------- */

/** Signal gradient, left -> right: `#178D8D` -> `#85B2B2`. Only used on small signal areas
 * (hover, selection, slider fill, active button, lock indicator). */
static constexpr float SIGNAL_DARK_COLOR[3] = {23.0f / 255.0f, 141.0f / 255.0f, 141.0f / 255.0f};
static constexpr float SIGNAL_LIGHT_COLOR[3] = {133.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f};
/** Body text `#E8E8E2` (warm off-white), secondary text `#85B2B2` at ~60% opacity. */
static constexpr float TEXT_BODY_COLOR[4] = {232.0f / 255.0f, 232.0f / 255.0f, 226.0f / 255.0f, 1.0f};
static constexpr float TEXT_SECONDARY_COLOR[4] = {133.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f, 0.60f};
/** Icons are neutral warm grey; they only turn signal teal on hover / active. */
static constexpr float ICON_NEUTRAL_COLOR[4] = {181.0f / 255.0f, 175.0f / 255.0f, 165.0f / 255.0f, 0.85f};
static constexpr float ICON_ACTIVE_COLOR[4] = {133.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f, 1.0f};

/** Collapsed anchor: square, docked to the right edge of the 3D viewport. */
static constexpr int ANCHOR_SIZE = 44;
static constexpr int ANCHOR_INSET = 6;
static constexpr float ANCHOR_ALPHA_IDLE = 0.17f;
static constexpr float ANCHOR_ALPHA_HOVER = 0.60f;
static constexpr float ANCHOR_HOVER_SCALE = 1.06f;
/** Hover ramp must settle inside 150 - 200 ms (brief). */
static constexpr double ANCHOR_HOVER_SECONDS = 0.175;
/** Expand / collapse duration and its cubic-bezier(0.4, 0, 0.2, 1) easing. */
static constexpr double PANEL_ANIM_SECONDS = 0.300;

/** Stage E: node-editor host content rect inside the expanded panel. The host area is a real,
 * runtime-only #ScrArea (SPACE_NODE) that is added to the active screen's area base and covers
 * exactly this rectangle, so node dragging, linking and box-select keep using the native node
 * editor operators and keymap. The panel itself only owns the surrounding chrome. */
static constexpr int NODE_HOST_INSET = 10;
static constexpr int NODE_HOST_TOP_GAP = 6;
static constexpr int NODE_HOST_BOTTOM = 56;
/** Small status dot shown on the right chrome strip while a modal operator is running. */
static constexpr int MODAL_DOT_RADIUS = 4;

/** Expanded panel layout (window pixels). */
static constexpr int TITLE_BAR_HEIGHT = 34;
static constexpr int BUTTON_SIZE = 20;
static constexpr int BUTTON_MARGIN = 10;
static constexpr int SLIDER_TRACK_Y = 27;
static constexpr int SLIDER_MARGIN = 16;

/** Glass body alpha (`EBC_FP_ALPHA`, default #PANEL_ALPHA_DEFAULT). Re-read every draw so the
 * value can be tuned at runtime (and for A/B pixel evidence) without restarting Blender. */
static float floating_panel_alpha_get()
{
  const char *env = std::getenv("EBC_FP_ALPHA");
  float value = PANEL_ALPHA_DEFAULT;
  if (env != nullptr && env[0] != '\0') {
    value = float(std::atof(env));
  }
  if (!std::isfinite(value)) {
    value = PANEL_ALPHA_DEFAULT;
  }
  CLAMP(value, 0.0f, 1.0f);
  return value;
}

/** \} */

/** \name Panel state
 * \{ */

/**
 * Per-region runtime state.
 *
 * Interactions: `None` -> `Move`/`Resize`/`Slider` on a left press inside the expanded panel,
 * back to `None` on release. Hover is only updated while `action == None` (stage B rule).
 *
 * View: the panel is an anchor (collapsed square docked to the viewport edge) or expanded.
 * Expand/collapse is a 300 ms rect interpolation; while `animating` the panel draws glass only
 * (no content) and the blur overlay stays off.
 */
struct FloatingPanelData {
  enum class Action : uint8_t {
    None,
    Move,
    Resize,
    Slider,
  };
  enum class View : uint8_t {
    Anchor,
    Expanded,
  };
  enum class Hover : uint8_t {
    None,
    Anchor,
    Lock,
    Minimize,
    Slider,
  };

  Action action = Action::None;
  View view = View::Anchor;
  Hover hover = Hover::None;

  /** Mouse position at press time (window pixels). */
  int2 grab_mouse = {0, 0};
  /** Panel rect at press time (window pixels). */
  rcti grab_rect = {};
  /** Screen the interaction started on (never dereferenced, only compared). */
  const bScreen *grab_screen = nullptr;

  /** Locked panels ignore move, resize, slider and global alpha changes (stage D). */
  bool locked = false;
  /** Per-panel alpha; `< 0` means "follow the global `EBC_FP_ALPHA`". Once the slider is used
   * (or the panel is locked) this overrides the global value. */
  float alpha_override = -1.0f;

  /** Anchor centre in window pixels (right edge of the 3D viewport, vertically centred). */
  int2 anchor_center = {0, 0};
  /** Expanded rect stashed across collapse so re-expanding returns to the same place. */
  rcti expanded_rect = {};
  bool has_expanded_rect = false;

  /* Stage E: runtime-only node-editor host area. It is created when the expand animation
   * finishes and removed as soon as a collapse starts. */
  ScrArea *node_area = nullptr;
  ARegion *node_region = nullptr;
  bool node_host_active = false;
  /** Content rect in window coordinates, last synced to the host area geometry. */
  rcti node_rect = {};
  /** A modal operator (node drag/link, sculpt stroke, ...) currently owns input. The panel
   * keeps drawing but stops hover/drag and shows the status dot. */
  bool modal_active = false;

  /* Expand / collapse animation. */
  bool animating = false;
  View anim_target_view = View::Expanded;
  double anim_start = 0.0;
  rcti anim_from = {};
  rcti anim_to = {};
  /** Duration of the last completed animation in ms (also written to the state dump). */
  double anim_last_ms = 0.0;

  /* Anchor hover ramp (150 - 200 ms). */
  float hover_progress = 0.0f;
  float hover_from = 0.0f;
  float hover_to = 0.0f;
  bool hover_ramp_active = false;
  double hover_start = 0.0;
  double hover_last_ms = 0.0;

  /** 60 Hz timer driving the hover ramp and the expand/collapse animation. */
  wmTimer *timer = nullptr;

  /* Stage C blur cache: one window-sized copy of the composited frame. Re-created when the
   * window size changes. */
  gpu::Texture *blur_capture_tex = nullptr;
  gpu::FrameBuffer *blur_capture_fb = nullptr;
  int blur_cache_size[2] = {0, 0};
};

/** Effective glass alpha for one panel: slider override > global `EBC_FP_ALPHA`. */
static float floating_panel_effective_alpha(const FloatingPanelData &data)
{
  if (data.alpha_override >= 0.0f) {
    float value = data.alpha_override;
    CLAMP(value, 0.0f, 1.0f);
    return value;
  }
  return floating_panel_alpha_get();
}

/** \} */

/** \name Node editor host (stage E)
 *
 * The panel hosts a real node editor instead of drawing one: a runtime-only #ScrArea of type
 * #SPACE_NODE is added to the active screen's area base with its four screen verts on the panel
 * content rect. It is not part of the tiling layout (no edges, no azones), but because it lives
 * in `screen->areabase` the normal draw and event paths handle it:
 * - its window region is composited in the overlapping-area pass, on top of the 3D view;
 * - `wm_event_do_handlers` finds it under the mouse and runs the regular node-editor keymap,
 *   so node dragging, linking and box-select are 100% native operators;
 * - the panel leaves a transparent hole over this rect and only draws the surrounding chrome.
 *
 * The area is created when the expand animation finishes and thrown away when a collapse starts.
 * \{ */

/** Blocks events under the panel in the areas *below* the host, so a click that the node editor
 * consumes is never also handled by the 3D view (define below, after the region lookup). */
static int floating_panel_background_blocker(bContext *C, const wmEvent *event, void *user_data);
static void floating_panel_background_blockers_update(bContext *C,
                                                     wmWindow *win,
                                                     bScreen *screen,
                                                     const ScrArea *host,
                                                     bool enable);

static rcti floating_panel_node_rect(const rcti &panel)
{
  rcti rect;
  rect.xmin = panel.xmin + NODE_HOST_INSET;
  rect.xmax = std::max(rect.xmin + 40, panel.xmax - NODE_HOST_INSET);
  rect.ymin = panel.ymin + NODE_HOST_BOTTOM;
  rect.ymax = std::max(rect.ymin + 40, panel.ymax - TITLE_BAR_HEIGHT - NODE_HOST_TOP_GAP);
  return rect;
}

static ARegion *floating_panel_node_window_region(ScrArea *area)
{
  if (area == nullptr) {
    return nullptr;
  }
  for (ARegion &region : area->regionbase) {
    if (region.regiontype == RGN_TYPE_WINDOW) {
      return &region;
    }
  }
  return nullptr;
}

/** Position the host area and its canvas region. Uses the same 1px border inset that
 * `area_calc_totrct` applies, without rebuilding the screen layout. */
static void floating_panel_node_host_geometry_set(ScrArea *area, const rcti &rect)
{
  if (area == nullptr || area->v1 == nullptr) {
    return;
  }
  area->v1->vec = {short(rect.xmin), short(rect.ymin)};
  area->v2->vec = {short(rect.xmin), short(rect.ymax + 1)};
  area->v3->vec = {short(rect.xmax + 1), short(rect.ymax + 1)};
  area->v4->vec = {short(rect.xmax + 1), short(rect.ymin)};

  /* The canvas region covers the panel hole exactly: the panel draws no chrome inside it. */
  area->totrct.xmin = rect.xmin;
  area->totrct.xmax = rect.xmax;
  area->totrct.ymin = rect.ymin;
  area->totrct.ymax = rect.ymax;
  area->winx = short(BLI_rcti_size_x(&area->totrct) + 1);
  area->winy = short(BLI_rcti_size_y(&area->totrct) + 1);

  ARegion *region = floating_panel_node_window_region(area);
  if (region == nullptr) {
    return;
  }
  region->winrct = area->totrct;
  region->runtime->visible = true;
  ED_region_update_rect(region);
  /* The node editor region is a `GPUViewport` region (its UI lands in the overlay framebuffer).
   * It must stay non-overlap so `wm_draw_window_onscreen` composites it with
   * `wm_draw_region_blit`, which resolves the overlay too. Overlap regions are blended from the
   * colour texture only, which shows an empty canvas. */
  region->overlap = false;
}

/** Centre the node view on the tree origin at 1:1 pixel scale. */
static void floating_panel_node_view_reset(ARegion *region)
{
  const float w = float(std::max(int(region->winx), 1));
  const float h = float(std::max(int(region->winy), 1));
  rctf cur;
  cur.xmin = -w * 0.5f;
  cur.xmax = w * 0.5f;
  cur.ymin = -h * 0.5f;
  cur.ymax = h * 0.5f;
  region->v2d.tot = cur;
  region->v2d.cur = cur;
}

static bNodeTree *floating_panel_node_tree_find(bContext *C)
{
  if (C == nullptr) {
    return nullptr;
  }
  for (bNodeTree &tree : CTX_data_main(C)->nodetrees.items_mutable()) {
    if (tree.type == NTREE_GEOMETRY) {
      return &tree;
    }
  }
  return nullptr;
}

/** Bind the host canvas to a tree and pin it: the area refresh callback (`snode_set_context`) */
/** would otherwise re-derive the tree from context and clear it again (this runtime-only area
 * has no active object/modifier of its own). */
static void floating_panel_node_host_bind(bContext *C, FloatingPanelData &data)
{
  if (data.node_area == nullptr || data.node_region == nullptr) {
    return;
  }
  bNodeTree *ntree = floating_panel_node_tree_find(C);
  if (ntree == nullptr) {
    return;
  }
  SpaceNode *snode = data.node_area->spacedata.first_as<SpaceNode>();
  if (snode == nullptr) {
    return;
  }
  /* The space subtype must match the tree, otherwise `snode_set_context` (run on every draw)
   * treats the tree as foreign, re-derives it from context and clears it. */
  BLI_strncpy(snode->tree_idname, ntree->idname, sizeof(snode->tree_idname));
  ED_node_tree_start(data.node_region, snode, ntree, nullptr, nullptr);
  snode->flag |= SNODE_PIN;
  floating_panel_node_view_reset(data.node_region);
  ED_region_tag_redraw(data.node_region);
}

static ScrArea *floating_panel_node_host_create(bContext *C,
                                                 wmWindow *win,
                                                 FloatingPanelData &data,
                                                 const rcti &rect)
{
  if (C == nullptr) {
    return nullptr;
  }
  bNodeTree *ntree = floating_panel_node_tree_find(C);
  if (ntree == nullptr) {
    return nullptr;
  }

  bScreen *screen = WM_window_get_active_screen(win);
  ScrArea *area = ED_area_offscreen_create(win, SPACE_NODE);
  if (area == nullptr) {
    return nullptr;
  }

  /* Four isolated screen verts define the floating rectangle. No screen edges are added, so no
   * area border is drawn and the tiling layout stays untouched. */
  ScrVert *v1 = MEM_new<ScrVert>(__func__);
  ScrVert *v2 = MEM_new<ScrVert>(__func__);
  ScrVert *v3 = MEM_new<ScrVert>(__func__);
  ScrVert *v4 = MEM_new<ScrVert>(__func__);
  BLI_addtail(&screen->vertbase, v1);
  BLI_addtail(&screen->vertbase, v2);
  BLI_addtail(&screen->vertbase, v3);
  BLI_addtail(&screen->vertbase, v4);
  area->v1 = v1;
  area->v2 = v2;
  area->v3 = v3;
  area->v4 = v4;

  /* Only the canvas is visible (no header, no sidebar), and it is composited on top of the
   * 3D view instead of being tiled next to it. */
  for (ARegion &region : area->regionbase) {
    if (region.regiontype == RGN_TYPE_WINDOW) {
      region.overlap = true;
      data.node_region = &region;
    }
    else {
      region.flag |= RGN_FLAG_HIDDEN;
    }
  }

  /* Prepended: area-under-mouse lookups and event dispatch hit the host before the 3D view. */
  BLI_addhead(&screen->areabase, area);

  floating_panel_node_host_geometry_set(area, rect);

  /* Sets region types, keymap handlers and the node view2d (normal area refresh path). */
  ED_area_init(C, win, area);

  /* `area_calc_totrct` insets by the user border width: pin the canvas back onto the hole. */
  floating_panel_node_host_geometry_set(area, rect);

  /* The area is not part of the screen layout, so its corner/edge split widgets are removed. */
  BLI_freelistN(&area->actionzones);

  if (data.node_region != nullptr) {
    SpaceNode *snode = area->spacedata.first_as<SpaceNode>();
    /* The space subtype must match the tree, otherwise `snode_set_context` (run on every draw)
     * treats the tree as foreign, re-derives it from context and clears it. */
    BLI_strncpy(snode->tree_idname, ntree->idname, sizeof(snode->tree_idname));
    ED_node_tree_start(data.node_region, snode, ntree, nullptr, nullptr);
    snode->flag |= SNODE_PIN;
    floating_panel_node_view_reset(data.node_region);
    ED_region_tag_redraw(data.node_region);
  }

  data.node_area = area;
  data.node_host_active = true;
  data.node_rect = rect;
  return area;
}

static void floating_panel_node_host_destroy(bContext *C, wmWindow *win, FloatingPanelData &data)
{
  if (data.node_area == nullptr || C == nullptr) {
    return;
  }
  wmWindowManager *wm = CTX_wm_manager(C);
  bScreen *screen = WM_window_get_active_screen(win);
  ScrArea *area = data.node_area;

  BLI_remlink(&screen->areabase, area);
  for (ScrVert *vert : {area->v1, area->v2, area->v3, area->v4}) {
    if (vert != nullptr) {
      BLI_remlink(&screen->vertbase, vert);
      MEM_delete(vert);
    }
  }
  area->v1 = area->v2 = area->v3 = area->v4 = nullptr;
  ED_area_offscreen_free(wm, win, area);

  data.node_area = nullptr;
  data.node_region = nullptr;
  data.node_host_active = false;
  data.node_rect = {};

  /* Re-open the areas below the panel for input. */
  floating_panel_background_blockers_update(C, win, screen, nullptr, false);
}

/** Keep the host rect glued to the panel (move / resize / animation-end). */
static void floating_panel_node_host_sync(const FloatingPanelData &data, const rcti &rect)
{
  if (data.node_area == nullptr) {
    return;
  }
  floating_panel_node_host_geometry_set(data.node_area, rect);
  ED_region_tag_redraw(data.node_region);
}

/** \} */

/** \name Region callbacks
 * \{ */

static rcti floating_panel_handle_rect(const ARegion *region)
{
  rcti rect;
  rect.xmin = region->winrct.xmax - (RESIZE_HANDLE_SIZE - 1);
  rect.xmax = region->winrct.xmax;
  rect.ymin = region->winrct.ymin;
  rect.ymax = region->winrct.ymin + (RESIZE_HANDLE_SIZE - 1);
  return rect;
}

/**
 * Glass layer, independent of the blur: vertical gradient `#453D3D` (top) -> `#0B0303` (bottom)
 * at `alpha`, a 1px `#85B2B2` outline at low opacity, and the resize handle.
 * `x0/y0/w/h` are in the coordinate space currently set by the caller (region-local pixels for
 * the offscreen region draw, window pixels for the onscreen overlay).
 */
static void floating_panel_draw_glass(float x0, float y0, float w, float h, float alpha)
{
  if (alpha <= 0.0f) {
    /* Fully transparent: nothing at all, including the border and handle. Used as the
     * background-only reference when verifying the blend. */
    return;
  }
  constexpr int GRADIENT_BANDS = 32;

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  GPU_blend(GPU_BLEND_ALPHA);

  /* Body gradient, drawn as horizontal bands. Band 0 is the darkest one (bottom). */
  for (int i = 0; i < GRADIENT_BANDS; i++) {
    const float t0 = float(i) / float(GRADIENT_BANDS);
    const float t1 = float(i + 1) / float(GRADIENT_BANDS);
    const float t = (t0 + t1) * 0.5f;
    const float r = PANEL_BOTTOM_COLOR[0] + (PANEL_TOP_COLOR[0] - PANEL_BOTTOM_COLOR[0]) * t;
    const float g = PANEL_BOTTOM_COLOR[1] + (PANEL_TOP_COLOR[1] - PANEL_BOTTOM_COLOR[1]) * t;
    const float b = PANEL_BOTTOM_COLOR[2] + (PANEL_TOP_COLOR[2] - PANEL_BOTTOM_COLOR[2]) * t;
    immUniformColor4f(r, g, b, alpha);
    immRectf(pos, x0, y0 + h * t0, x0 + w, y0 + h * t1);
  }

  /* 1px outline, drawn inside the panel edges. */
  immUniformColor4f(
      PANEL_BORDER_COLOR[0], PANEL_BORDER_COLOR[1], PANEL_BORDER_COLOR[2], PANEL_BORDER_ALPHA);
  immRectf(pos, x0, y0 + h - 1.0f, x0 + w, y0 + h); /* top */
  immRectf(pos, x0, y0, x0 + w, y0 + 1.0f);         /* bottom */
  immRectf(pos, x0, y0, x0 + 1.0f, y0 + h);         /* left */
  immRectf(pos, x0 + w - 1.0f, y0, x0 + w, y0 + h); /* right */

  /* Bottom-right resize handle. */
  immUniformColor4fv(HANDLE_COLOR);
  immRectf(pos,
           x0 + w - float(RESIZE_HANDLE_SIZE),
           y0,
           x0 + w,
           y0 + float(RESIZE_HANDLE_SIZE));

  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();
}

/* -------------------------------------------------------------------- */
/** \name Stage D: easing, anchor geometry, animation and content
 * \{ */

static float floating_panel_ease_out_cubic(float t)
{
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

/** cubic-bezier(0.4, 0, 0.2, 1): control points P1=(0.4, 0), P2=(0.2, 1). */
static float floating_panel_ease_bezier(float t)
{
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  /* x(s) = 3(1-s)^2 s * 0.4 + 3(1-s) s^2 * 0.2 + s^3, solved by bisection. */
  float lo = 0.0f, hi = 1.0f;
  for (int i = 0; i < 14; i++) {
    const float s = 0.5f * (lo + hi);
    const float x = 3.0f * (1.0f - s) * (1.0f - s) * s * 0.4f +
                    3.0f * (1.0f - s) * s * s * 0.2f + s * s * s;
    if (x < t) {
      lo = s;
    }
    else {
      hi = s;
    }
  }
  const float s = 0.5f * (lo + hi);
  /* y(s) = 3(1-s) s^2 * 1 + s^3. */
  return 3.0f * (1.0f - s) * s * s + s * s * s;
}

static rcti floating_panel_rect_lerp(const rcti &a, const rcti &b, float t)
{
  rcti r;
  r.xmin = int(std::round(float(a.xmin) + float(b.xmin - a.xmin) * t));
  r.xmax = int(std::round(float(a.xmax) + float(b.xmax - a.xmax) * t));
  r.ymin = int(std::round(float(a.ymin) + float(b.ymin - a.ymin) * t));
  r.ymax = int(std::round(float(a.ymax) + float(b.ymax - a.ymax) * t));
  if (r.xmax < r.xmin) {
    r.xmax = r.xmin;
  }
  if (r.ymax < r.ymin) {
    r.ymax = r.ymin;
  }
  return r;
}

/** Anchor edge length for the current hover progress (44 px -> ~47 px, ratio 1.06). */
static int floating_panel_anchor_size_px(const FloatingPanelData &data)
{
  const float scale = 1.0f + (ANCHOR_HOVER_SCALE - 1.0f) * data.hover_progress;
  return int(std::round(float(ANCHOR_SIZE) * scale));
}

static rcti floating_panel_anchor_rect_px(const FloatingPanelData &data, int size_px)
{
  rcti r;
  r.xmin = data.anchor_center[0] - size_px / 2;
  r.xmax = r.xmin + size_px - 1;
  r.ymin = data.anchor_center[1] - size_px / 2;
  r.ymax = r.ymin + size_px - 1;
  return r;
}

/** Expanded-panel title button rects, window pixels. */
static rcti floating_panel_button_rect_px(const rcti &panel, bool lock)
{
  const int xmax = panel.xmax - BUTTON_MARGIN;
  const int ymax = panel.ymax - BUTTON_MARGIN;
  rcti r;
  r.ymax = ymax;
  r.ymin = ymax - (BUTTON_SIZE - 1);
  if (lock) {
    r.xmax = xmax;
    r.xmin = xmax - (BUTTON_SIZE - 1);
  }
  else {
    r.xmax = xmax - BUTTON_SIZE - 6;
    r.xmin = r.xmax - (BUTTON_SIZE - 1);
  }
  return r;
}

static rcti floating_panel_slider_rect_px(const rcti &panel)
{
  rcti r;
  r.xmin = panel.xmin + 10;
  r.xmax = panel.xmax - 10;
  r.ymin = panel.ymin + 14;
  r.ymax = panel.ymin + 46;
  return r;
}

static void floating_panel_slider_track_px(const rcti &panel, float &x0, float &x1, float &y)
{
  x0 = float(panel.xmin + SLIDER_MARGIN);
  x1 = float(panel.xmax - SLIDER_MARGIN);
  y = float(panel.ymin + SLIDER_TRACK_Y);
}

/* ------------------------------------------------------------------ */
/* Timer driving the hover ramp and the expand/collapse animation. */

static wmTimer *g_floating_panel_timer = nullptr;
static wmWindow *g_floating_panel_timer_window = nullptr;

/** Stage F: window whose left press was consumed inside the node canvas hole while the node host
 * is active. `wm_handlers_do` clears `event_queue_check_drag` on any handled press, so the flag is
 * re-armed exactly once on the following motion event to keep the native node editor `CLICK_DRAG`
 * bindings (link, box-select) working. The marker is cleared by that first re-arm (and on release /
 * window deactivate), which also prevents a stale re-arm once a modal operator consumed the release.
 */
static wmWindow *g_panel_press_block_window = nullptr;

static void floating_panel_timer_stop(bContext *C, FloatingPanelData &data)
{
  if (data.timer == nullptr) {
    return;
  }
  wmWindowManager *wm = (C != nullptr) ? CTX_wm_manager(C) : nullptr;
  if (wm != nullptr) {
    WM_event_timer_remove(wm, g_floating_panel_timer_window, data.timer);
  }
  data.timer = nullptr;
  g_floating_panel_timer = nullptr;
  g_floating_panel_timer_window = nullptr;
}

static void floating_panel_timer_ensure(bContext *C, wmWindow *win, FloatingPanelData &data)
{
  if (data.timer != nullptr) {
    return;
  }
  wmWindowManager *wm = (C != nullptr) ? CTX_wm_manager(C) : nullptr;
  if (wm == nullptr) {
    return;
  }
  data.timer = WM_event_timer_add(wm, win, TIMER, 1.0 / 60.0);
  g_floating_panel_timer = data.timer;
  g_floating_panel_timer_window = win;
}

/**
 * Optional per-frame trace (`EBC_FP_TRACE_FILE`), one line per timer tick:
 * `time hover_progress animating anim_elapsed_ms hover_elapsed_ms rect`.
 * Written only when the env var is set; used to reconstruct the hover ramp and the
 * expand/collapse curve from real frame timestamps.
 */
static void floating_panel_trace(const FloatingPanelData &data,
                                 const rcti &rect,
                                 double anim_elapsed_ms,
                                 double hover_elapsed_ms)
{
  const char *path = std::getenv("EBC_FP_TRACE_FILE");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  FILE *fp = fopen(path, "a");
  if (fp == nullptr) {
    return;
  }
  fprintf(fp,
          "%.6f %.4f %d %.2f %.2f %d %d %d %d %d %d\n",
          BLI_time_now_seconds(),
          data.hover_progress,
          int(data.animating),
          anim_elapsed_ms,
          hover_elapsed_ms,
          rect.xmin,
          rect.xmax,
          rect.ymin,
          rect.ymax,
          rect.xmax - rect.xmin + 1,
          rect.ymax - rect.ymin + 1);
  fclose(fp);
}

static void floating_panel_anim_start(FloatingPanelData &data,
                                      FloatingPanelData::View target,
                                      const rcti &from,
                                      const rcti &to)
{
  data.animating = true;
  data.anim_target_view = target;
  data.anim_from = from;
  data.anim_to = to;
  data.anim_start = BLI_time_now_seconds();
  fprintf(stderr, "[FP-ANIM] %s start\n", (target == FloatingPanelData::View::Expanded) ? "expand" : "collapse");
}

static void floating_panel_anim_tick(bContext *C,
                                     wmWindow * /*win*/,
                                     ARegion *region,
                                     FloatingPanelData &data)
{
  const double now = BLI_time_now_seconds();
  bool active = false;

  if (data.animating) {
    active = true;
    double t = (now - data.anim_start) / PANEL_ANIM_SECONDS;
    CLAMP(t, 0.0, 1.0);
    const float eased = floating_panel_ease_bezier(float(t));
    const rcti r = floating_panel_rect_lerp(data.anim_from, data.anim_to, eased);
    BLI_rcti_init(&region->winrct, r.xmin, r.xmax, r.ymin, r.ymax);
    ED_region_update_rect(region);
    ED_region_tag_redraw(region);
    floating_panel_trace(data, region->winrct, (now - data.anim_start) * 1000.0, 0.0);

    if (t >= 1.0) {
      data.animating = false;
      data.view = data.anim_target_view;
      data.anim_last_ms = (now - data.anim_start) * 1000.0;
      if (data.view == FloatingPanelData::View::Expanded) {
        data.expanded_rect = data.anim_to;
        data.has_expanded_rect = true;
        data.hover = FloatingPanelData::Hover::None;
      }
      else {
        data.hover_progress = 0.0f;
        data.hover_from = 0.0f;
        data.hover_to = 0.0f;
        data.hover_ramp_active = false;
        data.hover = FloatingPanelData::Hover::None;
      }
      fprintf(stderr,
              "[FP-ANIM] %s done %.1f ms\n",
              (data.view == FloatingPanelData::View::Expanded) ? "expand" : "collapse",
              data.anim_last_ms);
    }
  }

  if (!data.animating && data.hover_ramp_active) {
    active = true;
    double t = (now - data.hover_start) / ANCHOR_HOVER_SECONDS;
    CLAMP(t, 0.0, 1.0);
    const float p = data.hover_from +
                    (data.hover_to - data.hover_from) * floating_panel_ease_out_cubic(float(t));
    data.hover_progress = p;
    if (data.view == FloatingPanelData::View::Anchor) {
      const int size = floating_panel_anchor_size_px(data);
      const rcti r = floating_panel_anchor_rect_px(data, size);
      BLI_rcti_init(&region->winrct, r.xmin, r.xmax, r.ymin, r.ymax);
      ED_region_update_rect(region);
    }
    ED_region_tag_redraw(region);
    floating_panel_trace(data, region->winrct, 0.0, (now - data.hover_start) * 1000.0);

    if (t >= 1.0) {
      data.hover_progress = data.hover_to;
      data.hover_ramp_active = false;
      data.hover_last_ms = (now - data.hover_start) * 1000.0;
      fprintf(stderr,
              "[FP-ANIM] hover %s done %.1f ms\n",
              (data.hover_to > 0.0f) ? "in" : "out",
              data.hover_last_ms);
    }
  }

  if (!active) {
    floating_panel_timer_stop(C, data);
  }
}

/* ------------------------------------------------------------------ */
/* Anchor (collapsed) drawing. */

static void floating_panel_draw_anchor(float w, float h, const FloatingPanelData &data)
{
  const float p = data.hover_progress;
  const float alpha = ANCHOR_ALPHA_IDLE + (ANCHOR_ALPHA_HOVER - ANCHOR_ALPHA_IDLE) * p;
  constexpr int BANDS = 12;

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  GPU_blend(GPU_BLEND_ALPHA);

  for (int i = 0; i < BANDS; i++) {
    const float t0 = float(i) / float(BANDS);
    const float t1 = float(i + 1) / float(BANDS);
    const float t = (t0 + t1) * 0.5f;
    const float r = PANEL_BOTTOM_COLOR[0] + (PANEL_TOP_COLOR[0] - PANEL_BOTTOM_COLOR[0]) * t;
    const float g = PANEL_BOTTOM_COLOR[1] + (PANEL_TOP_COLOR[1] - PANEL_BOTTOM_COLOR[1]) * t;
    const float b = PANEL_BOTTOM_COLOR[2] + (PANEL_TOP_COLOR[2] - PANEL_BOTTOM_COLOR[2]) * t;
    immUniformColor4f(r, g, b, alpha);
    immRectf(pos, 0.0f, h * t0, w, h * t1);
  }

  /* Inner signal halo (micro glow), kept inside the region rect. */
  const float halo_alpha = 0.04f + 0.10f * p;
  immUniformColor4f(SIGNAL_LIGHT_COLOR[0], SIGNAL_LIGHT_COLOR[1], SIGNAL_LIGHT_COLOR[2], halo_alpha);
  immRectf(pos, 0.0f, h - 2.0f, w, h);
  immRectf(pos, 0.0f, 0.0f, w, 2.0f);
  immRectf(pos, 0.0f, 0.0f, 2.0f, h);
  immRectf(pos, w - 2.0f, 0.0f, w, h);

  /* 1px signal outline `#178D8D`. */
  const float border_alpha = 0.55f + 0.35f * p;
  immUniformColor4f(SIGNAL_DARK_COLOR[0], SIGNAL_DARK_COLOR[1], SIGNAL_DARK_COLOR[2], border_alpha);
  immRectf(pos, 0.0f, h - 1.0f, w, h);
  immRectf(pos, 0.0f, 0.0f, w, 1.0f);
  immRectf(pos, 0.0f, 0.0f, 1.0f, h);
  immRectf(pos, w - 1.0f, 0.0f, w, h);

  /* Corner bracket, all right angles. */
  const float arm = std::min(14.0f, std::min(w, h) * 0.34f);
  immUniformColor4f(SIGNAL_LIGHT_COLOR[0], SIGNAL_LIGHT_COLOR[1], SIGNAL_LIGHT_COLOR[2], 0.55f + 0.35f * p);
  immRectf(pos, w - arm, h - 2.0f, w - 2.0f, h - 1.0f);
  immRectf(pos, w - 2.0f, h - arm, w - 1.0f, h - 2.0f);
  immRectf(pos, 2.0f, 1.0f, 2.0f + arm, 2.0f);
  immRectf(pos, 1.0f, 2.0f, 2.0f, 2.0f + arm);

  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();
}

/* ------------------------------------------------------------------ */
/* Expanded-panel content: title, divider, icons, readouts, slider. */

static void floating_panel_stroke_rect(uint pos, float x0, float y0, float x1, float y1)
{
  immRectf(pos, x0, y1 - 1.0f, x1, y1);
  immRectf(pos, x0, y0, x1, y0 + 1.0f);
  immRectf(pos, x0, y0, x0 + 1.0f, y1);
  immRectf(pos, x1 - 1.0f, y0, x1, y1);
}

static void floating_panel_text(const int font,
                                const float rgba[4],
                                float size,
                                float x,
                                float y,
                                const char *str)
{
  BLF_color4fv(font, rgba);
  BLF_size(font, size);
  BLF_position(font, x, y, 0.0f);
  BLF_draw(font, str, strlen(str));
}

static void floating_panel_text_mono(const int font,
                                     const float rgba[4],
                                     float size,
                                     int char_width,
                                     float x,
                                     float y,
                                     const char *str)
{
  BLF_color4fv(font, rgba);
  BLF_size(font, size);
  BLF_position(font, x, y, 0.0f);
  BLF_draw_mono(font, str, strlen(str), char_width, 0);
}

/** `panel` is the window-space rect of the region; drawing happens in region-local pixels. */
static float floating_panel_blur_radius_get();

/**
 * Expanded-panel chrome content (window pixels through `panel`).
 *
 * `host_mode` skips the text items that sit in the middle of the panel, because that middle is a
 * transparent hole for the node-editor canvas in stage E. Everything that remains (title bar,
 * buttons, divider, slider) lies inside the top/bottom chrome bands.
 */
static void floating_panel_draw_content(const FloatingPanelData &data,
                                        const rcti &panel,
                                        bool host_mode = false)
{
  const float w = float(BLI_rcti_size_x(&panel) + 1);
  const float h = float(BLI_rcti_size_y(&panel) + 1);
  const float ox = float(panel.xmin);
  const float oy = float(panel.ymin);
  const float scale = UI_SCALE_FAC;
  const float alpha = floating_panel_effective_alpha(data);

  const rcti lock_r = floating_panel_button_rect_px(panel, true);
  const rcti min_r = floating_panel_button_rect_px(panel, false);
  const rcti slider_r = floating_panel_slider_rect_px(panel);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  GPU_blend(GPU_BLEND_ALPHA);

  /* Title divider, thin muted signal line. */
  immUniformColor4f(PANEL_BORDER_COLOR[0], PANEL_BORDER_COLOR[1], PANEL_BORDER_COLOR[2], 0.25f);
  immRectf(pos, 12.0f, h - float(TITLE_BAR_HEIGHT), w - 12.0f, h - float(TITLE_BAR_HEIGHT) + 1.0f);

  /* Buttons: right-angle 1px outline + icon. Icons are neutral until hover / active. */
  const bool lock_hot = (data.hover == FloatingPanelData::Hover::Lock);
  const bool min_hot = (data.hover == FloatingPanelData::Hover::Minimize);
  const float *lock_col = (data.locked || lock_hot) ? ICON_ACTIVE_COLOR : ICON_NEUTRAL_COLOR;
  const float *min_col = min_hot ? ICON_ACTIVE_COLOR : ICON_NEUTRAL_COLOR;
  const float *lock_outline = lock_hot ? ICON_ACTIVE_COLOR : PANEL_BORDER_COLOR;
  immUniformColor4f(lock_outline[0], lock_outline[1], lock_outline[2], lock_hot ? 0.80f : 0.30f);
  floating_panel_stroke_rect(pos,
                             float(lock_r.xmin) - ox,
                             float(lock_r.ymin) - oy,
                             float(lock_r.xmax - ox + 1),
                             float(lock_r.ymax - oy + 1));
  immUniformColor4f(min_col[0], min_col[1], min_col[2], min_col[3]);
  floating_panel_stroke_rect(pos,
                             float(min_r.xmin) - ox,
                             float(min_r.ymin) - oy,
                             float(min_r.xmax - ox + 1),
                             float(min_r.ymax - oy + 1));

  /* Lock icon: body + shackle (right angles only). */
  {
    const float cx = float(lock_r.xmin - ox) + float(BUTTON_SIZE) * 0.5f;
    const float cy = float(lock_r.ymin - oy) + float(BUTTON_SIZE) * 0.5f;
    immUniformColor4fv(lock_col);
    floating_panel_stroke_rect(pos, cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 1.0f);
    immRectf(pos, cx - 3.0f, cy + 1.0f, cx - 2.0f, cy + 4.0f);
    immRectf(pos, cx - 3.0f, cy + 3.0f, cx + 3.0f, cy + 4.0f);
    immRectf(pos, cx + 2.0f, cy + 1.0f, cx + 3.0f, cy + 4.0f);
  }
  /* Minimize icon: single horizontal bar. */
  {
    const float cx = float(min_r.xmin - ox) + float(BUTTON_SIZE) * 0.5f;
    const float cy = float(min_r.ymin - oy) + float(BUTTON_SIZE) * 0.5f;
    immUniformColor4fv(min_col);
    immRectf(pos, cx - 5.0f, cy - 1.0f, cx + 5.0f, cy);
  }

  /* Slider track and fill. */
  {
    const float x0 = float(slider_r.xmin - ox) + 6.0f;
    const float x1 = float(slider_r.xmax - ox) - 6.0f;
    const float y = float(panel.ymin + SLIDER_TRACK_Y) - oy;
    float frac = (alpha - 0.05f) / 0.95f;
    CLAMP(frac, 0.0f, 1.0f);
    const float knob_x = x0 + (x1 - x0) * frac;

    immUniformColor4f(0.72f, 0.70f, 0.66f, 0.30f);
    immRectf(pos, x0, y - 1.0f, x1, y + 1.0f);

    if (!data.locked) {
      constexpr int SEGMENTS = 24;
      for (int i = 0; i < SEGMENTS; i++) {
        const float t0 = float(i) / float(SEGMENTS);
        const float t1 = float(i + 1) / float(SEGMENTS);
        if (x0 + (knob_x - x0) * t0 >= knob_x) {
          break;
        }
        const float r = SIGNAL_DARK_COLOR[0] + (SIGNAL_LIGHT_COLOR[0] - SIGNAL_DARK_COLOR[0]) * t0;
        const float g = SIGNAL_DARK_COLOR[1] + (SIGNAL_LIGHT_COLOR[1] - SIGNAL_DARK_COLOR[1]) * t0;
        const float b = SIGNAL_DARK_COLOR[2] + (SIGNAL_LIGHT_COLOR[2] - SIGNAL_DARK_COLOR[2]) * t0;
        immUniformColor4f(r, g, b, 1.0f);
        immRectf(pos, x0 + (knob_x - x0) * t0, y - 1.0f, x0 + (knob_x - x0) * t1, y + 1.0f);
      }
    }

    const bool slider_hot = (data.hover == FloatingPanelData::Hover::Slider);
    if (data.locked) {
      immUniformColor4f(0.72f, 0.70f, 0.66f, 0.35f);
    }
    else if (slider_hot) {
      immUniformColor4fv(ICON_ACTIVE_COLOR);
    }
    else {
      immUniformColor4f(SIGNAL_LIGHT_COLOR[0], SIGNAL_LIGHT_COLOR[1], SIGNAL_LIGHT_COLOR[2], 0.85f);
    }
    immRectf(pos, knob_x - 3.0f, y - 6.0f, knob_x + 3.0f, y + 6.0f);
    if (slider_hot && !data.locked) {
      immUniformColor4fv(ICON_ACTIVE_COLOR);
      floating_panel_stroke_rect(pos, knob_x - 5.0f, y - 8.0f, knob_x + 6.0f, y + 9.0f);
    }
  }

  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();

  /* Text layer. */
  const int font = BLF_set_default();
  const float s_title = 12.0f * scale;
  const float s_small = 9.5f * scale;
  const float body_dim[4] = {TEXT_BODY_COLOR[0], TEXT_BODY_COLOR[1], TEXT_BODY_COLOR[2], 0.82f};

  floating_panel_text(font, TEXT_BODY_COLOR, s_title, 14.0f * scale, h - 21.0f, "EBC // GLASS");

  char mono[96];
  if (!host_mode) {
    floating_panel_text(font,
                        TEXT_SECONDARY_COLOR,
                        s_small,
                        14.0f * scale,
                        h - 52.0f,
                        "SIGNAL CHANNEL / BACKDROP");

    snprintf(mono,
             sizeof(mono),
             "A %03d  R %04.1f  W %03d  H %03d",
             int(alpha * 100.0f),
             floating_panel_blur_radius_get(),
             int(w),
             int(h));
    floating_panel_text_mono(
        font, body_dim, 9.0f * scale, int(6.2f * scale), 14.0f * scale, h - 76.0f, mono);

    floating_panel_text(font,
                        TEXT_SECONDARY_COLOR,
                        s_small,
                        14.0f * scale,
                        h - 100.0f,
                        data.locked ? "LOCK // ENGAGED" : "LOCK // OPEN");
  }

  floating_panel_text(font, TEXT_SECONDARY_COLOR, s_small, float(SLIDER_MARGIN), 40.0f, "PANEL ALPHA");
  snprintf(mono, sizeof(mono), "%03d", int(alpha * 100.0f));
  floating_panel_text_mono(font,
                           TEXT_BODY_COLOR,
                           10.0f * scale,
                           int(6.2f * scale),
                           w - float(SLIDER_MARGIN) - 22.0f,
                           40.0f,
                           mono);
}

/** \} */

/** Blur pipeline switch (`EBC_FP_BLUR`, enabled unless the env value is 0). */
static bool floating_panel_blur_enabled()
{
  const char *env = std::getenv("EBC_FP_BLUR");
  if (env == nullptr || env[0] == '\0') {
    return true;
  }
  return std::atof(env) > 0.5;
}

/** Gaussian radius in full-resolution window pixels (`EBC_FP_BLUR_RADIUS`, default 10). */
static float floating_panel_blur_radius_get()
{
  const char *env = std::getenv("EBC_FP_BLUR_RADIUS");
  float radius = 10.0f;
  if (env != nullptr && env[0] != '\0') {
    radius = float(std::atof(env));
  }
  if (!std::isfinite(radius)) {
    radius = 10.0f;
  }
  CLAMP(radius, 0.5f, 40.0f);
  return radius;
}

/** Debug state logging, only on transitions, to prove the drag degradation. */
static void floating_panel_log_blur_state(const char *state)
{
  static const char *last = nullptr;
  if (last != nullptr && STREQ(last, state)) {
    return;
  }
  last = state;
  fprintf(stderr, "[FP-BLUR] %s\n", state);
}

/** Accumulate the measured cost of the overlay blur and report it every 30 blurred frames. */
static void floating_panel_blur_timing_add(const double ms)
{
  static double accum = 0.0;
  static int count = 0;
  accum += ms;
  count++;
  if (count >= 30) {
    fprintf(stderr, "[FP-BLUR] overlay avg %.3f ms over %d frames\n", accum / double(count), count);
    accum = 0.0;
    count = 0;
  }
}

/** Free cached GPU resources. Called from the region free callback. */
static void floating_panel_blur_cache_free(FloatingPanelData &data)
{
  if (data.blur_capture_fb != nullptr || data.blur_capture_tex != nullptr) {
    if (GPU_context_active_get() != nullptr) {
      if (data.blur_capture_fb != nullptr) {
        GPU_framebuffer_free(data.blur_capture_fb);
      }
      if (data.blur_capture_tex != nullptr) {
        GPU_texture_free(data.blur_capture_tex);
      }
    }
  }
  data.blur_capture_fb = nullptr;
  data.blur_capture_tex = nullptr;
  data.blur_cache_size[0] = data.blur_cache_size[1] = 0;
}

/** Allocate the capture texture when the window size changed. */
static bool floating_panel_blur_cache_ensure(FloatingPanelData &data, const int2 win_size)
{
  if (data.blur_capture_tex != nullptr && data.blur_cache_size[0] == win_size[0] &&
      data.blur_cache_size[1] == win_size[1])
  {
    return true;
  }
  floating_panel_blur_cache_free(data);

  const eGPUTextureUsage usage = eGPUTextureUsage(GPU_TEXTURE_USAGE_ATTACHMENT |
                                                    GPU_TEXTURE_USAGE_SHADER_READ);

  data.blur_capture_tex = GPU_texture_create_2d("fp_capture",
                                                win_size[0],
                                                win_size[1],
                                                1,
                                                gpu::TextureFormat::UNORM_8_8_8_8,
                                                usage,
                                                nullptr);
  data.blur_capture_fb = GPU_framebuffer_create("fp_capture_fb");
  if (data.blur_capture_tex == nullptr || data.blur_capture_fb == nullptr) {
    return false;
  }
  GPU_framebuffer_texture_attach(data.blur_capture_fb, data.blur_capture_tex, 0, 0);

  data.blur_cache_size[0] = win_size[0];
  data.blur_cache_size[1] = win_size[1];
  return true;
}

/**
 * Draw a texture quad in the currently bound frame-buffer.
 *
 * `p` is the destination rect and `uv` the source rect, both in the coordinate space set by the
 * caller. The image shader multiplies the sampled color by `tint`, which is how the gaussian
 * weights are applied when additive blending accumulates the taps.
 */
static void floating_panel_draw_image_quad(gpu::Texture *tex,
                                           const rctf &p,
                                           const rctf &uv,
                                           const float tint[4],
                                           const GPUSamplerExtendMode extend_mode)
{
  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);
  const uint tex_coord = GPU_vertformat_attr_add(
      format, "texCoord", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_IMAGE_COLOR);
  immUniformColor4fv(tint);
  immBindTextureSampler("image",
                          tex,
                          {GPU_SAMPLER_FILTERING_LINEAR, extend_mode, extend_mode});
  immRectf_with_texco(pos, tex_coord, p, uv);
  immUnbindProgram();
}

/**
 * Capture the composited window into `data.blur_capture_tex`, blur it inside the panel rect and
 * draw the result over the window.
 *
 * Called from `wm_draw_window_onscreen()` between `wmWindowViewport()` and the floating-region
 * blend loop, i.e. after every area has been composited into the window frame-buffer and before
 * the panel's own (transparent) region buffer is blended. That is the only point where the
 * backdrop exists as pixels and where drawing into the window cannot be overwritten by the
 * viewport redraw (the viewport already finished its draw).
 *
 * Returns false when the fast path is not possible, in which case the caller must keep the
 * fallback visible (the region draw is skipped while blur is on, so the overlay has to paint it).
 */
static bool floating_panel_overlay_blur(FloatingPanelData &data,
                                        const rcti &rect,
                                        const int2 win_size,
                                        float alpha)
{
  const int win_w = win_size[0];
  const int win_h = win_size[1];
  if (rect.xmin < 0 || rect.ymin < 0 || rect.xmax >= win_w || rect.ymax >= win_h) {
    return false;
  }
  gpu::FrameBuffer *win_fb = GPU_framebuffer_active_get();
  if (win_fb == nullptr) {
    return false;
  }
  if (!floating_panel_blur_cache_ensure(data, win_size)) {
    return false;
  }

  /* rctf field order is (xmin, xmax, ymin, ymax). */
  rctf p_panel = {float(rect.xmin),
                  float(rect.xmax + 1),
                  float(rect.ymin),
                  float(rect.ymax + 1)};
  rctf uv_panel = {float(rect.xmin) / float(win_w),
                   float(rect.xmax + 1) / float(win_w),
                   float(rect.ymin) / float(win_h),
                   float(rect.ymax + 1) / float(win_h)};

  /* 1. Copy the composited window: the backdrop cannot be sampled while it is the render target,
   * and the panel's own region buffer has not been blended in yet (`rect` is the only panel
   * region, so its pixels are transparent at this point). */
  GPU_framebuffer_blit(win_fb, 0, data.blur_capture_fb, 0, GPU_COLOR_BIT);

  GPU_viewport(0, 0, win_w, win_h);
  GPU_matrix_ortho_2d_set(0.0f, float(win_w), 0.0f, float(win_h));
  GPU_scissor_test(false);
  GPU_depth_test(GPU_DEPTH_NONE);
  GPU_stencil_test(GPU_STENCIL_NONE);

  /* 2. Single-pass gaussian: 13 taps on a disc, evaluated while compositing into the panel rect.
   * Weights are normalized and accumulated with premultiplied additive blending, so the result is
   * exactly `sum(weight * color)` inside the panel and nothing outside it.
   *
   * Note: drawing the taps into an intermediate offscreen target from this draw hook silently
   * produces nothing on this build (verified with readbacks), so the blur is evaluated in the
   * composite pass itself. This also removes the extra full-window downsample. */
  const float radius = floating_panel_blur_radius_get();
  const float sigma = radius * 0.5f;
  constexpr int TAP_COUNT = 13;
  float tap_dx[TAP_COUNT];
  float tap_dy[TAP_COUNT];
  float tap_w[TAP_COUNT];
  tap_dx[0] = 0.0f;
  tap_dy[0] = 0.0f;
  tap_w[0] = 1.0f;
  for (int ring = 0; ring < 2; ring++) {
    const float d = radius * (ring == 0 ? 0.55f : 1.0f);
    for (int k = 0; k < 6; k++) {
      const float angle = float(k) * float(M_PI) / 3.0f;
      const int i = 1 + ring * 6 + k;
      tap_dx[i] = std::cos(angle) * d;
      tap_dy[i] = std::sin(angle) * d;
      tap_w[i] = std::exp(-0.5f * (d * d) / (sigma * sigma));
    }
  }
  float wsum = 0.0f;
  for (int i = 0; i < TAP_COUNT; i++) {
    wsum += tap_w[i];
  }
  for (int i = 0; i < TAP_COUNT; i++) {
    tap_w[i] /= wsum;
  }

  for (int i = 0; i < TAP_COUNT; i++) {
    rctf uv = uv_panel;
    const float du = tap_dx[i] / float(win_w);
    const float dv = tap_dy[i] / float(win_h);
    uv.xmin += du;
    uv.xmax += du;
    uv.ymin += dv;
    uv.ymax += dv;
    const float tint[4] = {tap_w[i], tap_w[i], tap_w[i], 1.0f};
    GPU_blend(i == 0 ? GPU_BLEND_NONE : GPU_BLEND_ADDITIVE_PREMULT);
    floating_panel_draw_image_quad(
        data.blur_capture_tex, p_panel, uv, tint, GPU_SAMPLER_EXTEND_MODE_EXTEND);
  }
  GPU_blend(GPU_BLEND_NONE);

  /* 3. Glass layer on top of the blurred backdrop. The content layer is drawn by the region's
   * own offscreen buffer, which is blended after this overlay. */
  floating_panel_draw_glass(p_panel.xmin,
                            p_panel.ymin,
                            float(rect.xmax - rect.xmin + 1),
                            float(rect.ymax - rect.ymin + 1),
                            alpha);
  return true;
}

/**
 * Glass for one chrome band around the node canvas (region-local pixels).
 *
 * The gradient is sampled against the full panel height (`full_h`) so the four bands line up
 * seamlessly. The 1px outline is only drawn on edges that coincide with the panel's outer edge,
 * and the resize handle only in the bottom-right corner. Drawing explicit band rectangles (instead
 * of one full-panel quad clipped by `GPU_scissor`) keeps this independent of scissor state, which
 * silently discarded the scissored pass in this build.
 */
static void floating_panel_draw_glass_band(float x0,
                                           float y0,
                                           float x1,
                                           float y1,
                                           float full_w,
                                           float full_h,
                                           float alpha)
{
  if (alpha <= 0.0f || x1 <= x0 || y1 <= y0) {
    return;
  }
  constexpr int GRADIENT_BANDS = 32;

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  GPU_blend(GPU_BLEND_ALPHA);

  /* Body gradient. Band 0 is the darkest one (bottom of the whole panel). */
  for (int i = 0; i < GRADIENT_BANDS; i++) {
    const float t0 = float(i) / float(GRADIENT_BANDS);
    const float t1 = float(i + 1) / float(GRADIENT_BANDS);
    const float ymin = std::max(y0, full_h * t0);
    const float ymax = std::min(y1, full_h * t1);
    if (ymax <= ymin) {
      continue;
    }
    const float t = (t0 + t1) * 0.5f;
    const float r = PANEL_BOTTOM_COLOR[0] + (PANEL_TOP_COLOR[0] - PANEL_BOTTOM_COLOR[0]) * t;
    const float g = PANEL_BOTTOM_COLOR[1] + (PANEL_TOP_COLOR[1] - PANEL_BOTTOM_COLOR[1]) * t;
    const float b = PANEL_BOTTOM_COLOR[2] + (PANEL_TOP_COLOR[2] - PANEL_BOTTOM_COLOR[2]) * t;
    immUniformColor4f(r, g, b, alpha);
    immRectf(pos, x0, ymin, x1, ymax);
  }

  /* 1px outline, only on the panel's outer edges. */
  immUniformColor4f(
      PANEL_BORDER_COLOR[0], PANEL_BORDER_COLOR[1], PANEL_BORDER_COLOR[2], PANEL_BORDER_ALPHA);
  if (y1 >= full_h) {
    immRectf(pos, x0, full_h - 1.0f, x1, full_h); /* top */
  }
  if (y0 <= 0.0f) {
    immRectf(pos, x0, 0.0f, x1, 1.0f); /* bottom */
  }
  if (x0 <= 0.0f) {
    immRectf(pos, 0.0f, y0, 1.0f, y1); /* left */
  }
  if (x1 >= full_w) {
    immRectf(pos, full_w - 1.0f, y0, full_w, y1); /* right */
  }

  /* Bottom-right resize handle. */
  if (x1 >= full_w && y0 <= 0.0f) {
    immUniformColor4fv(HANDLE_COLOR);
    immRectf(pos, full_w - float(RESIZE_HANDLE_SIZE), 0.0f, full_w, float(RESIZE_HANDLE_SIZE));
  }

  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();
}

/** Modal state indicator: a small signal dot in the right chrome band. The panel stays fully
 * drawn while an operator is modal, the dot only marks it as temporarily non-interactive. */
static void floating_panel_draw_modal_dot(const FloatingPanelData &data, const ARegion *region)
{
  const rcti panel = region->winrct;
  const rcti node = data.node_rect;
  if (node.xmax < panel.xmin || node.xmax > panel.xmax) {
    return;
  }
  const float cx = float(node.xmax + (NODE_HOST_INSET / 2) - panel.xmin);
  const float cy = float((node.ymin + node.ymax) / 2 - panel.ymin);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  GPU_blend(GPU_BLEND_ALPHA);

  /* Ring, then the solid core: the centre pixel stays at the exact #178D8D fill. */
  immUniformColor4f(0.522f, 0.698f, 0.698f, 1.0f); /* #85B2B2 */
  imm_draw_circle_fill_2d(pos, cx, cy, float(MODAL_DOT_RADIUS), 24);
  immUniformColor4f(0.090f, 0.553f, 0.553f, 1.0f); /* #178D8D */
  imm_draw_circle_fill_2d(pos, cx, cy, float(MODAL_DOT_RADIUS - 1), 24);

  GPU_blend(GPU_BLEND_NONE);
  immUnbindProgram();
}

/** Draws the chrome and the content around the canvas hole in four bands, keeping the
 * hole itself untouched so the host node region (composited below this region) shows through. */
static void floating_panel_draw_host_cutout(FloatingPanelData &data, ARegion *region)
{
  const rcti panel = region->winrct;
  rcti hole = data.node_rect;
  hole.xmin = max_ii(hole.xmin, panel.xmin);
  hole.xmax = min_ii(hole.xmax, panel.xmax);
  hole.ymin = max_ii(hole.ymin, panel.ymin);
  hole.ymax = min_ii(hole.ymax, panel.ymax);

  /* Fallback: if the hole is degenerate, draw the plain panel (no invisible strip). */
  if (hole.xmin > hole.xmax || hole.ymin > hole.ymax) {
    floating_panel_draw_glass(0.0f,
                              0.0f,
                              float(region->winx),
                              float(region->winy),
                              floating_panel_effective_alpha(data));
    floating_panel_draw_content(data, panel);
    return;
  }

  /* NB: `rcti` is `{xmin, xmax, ymin, ymax}` — the bands must be initialised in that order. */
  const rcti top = {panel.xmin, panel.xmax, hole.ymax + 1, panel.ymax};
  const rcti bottom = {panel.xmin, panel.xmax, panel.ymin, hole.ymin - 1};
  const rcti left = {panel.xmin, hole.xmin - 1, hole.ymin, hole.ymax};
  const rcti right = {hole.xmax + 1, panel.xmax, hole.ymin, hole.ymax};

  const float full_w = float(region->winx);
  const float full_h = float(region->winy);
  const float alpha = floating_panel_effective_alpha(data);

  /* Each band is drawn in region-local pixels, sampling the gradient against the full panel
   * height so the bands join seamlessly. Only the panel's outer edges get the 1px outline. */
  for (const rcti *band : {&top, &bottom, &left, &right}) {
    if (band->xmin > band->xmax || band->ymin > band->ymax) {
      continue;
    }
    floating_panel_draw_glass_band(float(band->xmin - panel.xmin),
                                   float(band->ymin - panel.ymin),
                                   float(band->xmax - panel.xmin + 1),
                                   float(band->ymax - panel.ymin + 1),
                                   full_w,
                                   full_h,
                                   alpha);
  }

  /* Chrome content (title bar, buttons, slider). Items that would fall into the canvas hole are
   * skipped by the `host_mode` flag, so no scissor is needed here either. */
  floating_panel_draw_content(data, panel, true);

  if (data.modal_active) {
    floating_panel_draw_modal_dot(data, region);
  }
}

static void floating_panel_region_draw(const bContext * /*C*/, ARegion *region)
{
  /* Region-local pixel space. This region is drawn into its own offscreen buffer which is then
   * composited over the window (premultiplied), so alpha here is what makes the glass
   * semi-transparent over whatever is below the panel. */
  wmOrtho2_region_pixelspace(region);

  FloatingPanelData *data = static_cast<FloatingPanelData *>(region->regiondata);
  if (data == nullptr) {
    floating_panel_draw_glass(
        0.0f, 0.0f, float(region->winx), float(region->winy), floating_panel_alpha_get());
    return;
  }

  if (data->animating) {
    /* Expand / collapse: glass only, the content does not fit the intermediate rect yet. */
    floating_panel_draw_glass(
        0.0f, 0.0f, float(region->winx), float(region->winy), floating_panel_effective_alpha(*data));
    return;
  }

  if (data->view == FloatingPanelData::View::Anchor) {
    floating_panel_draw_anchor(float(region->winx), float(region->winy), *data);
    return;
  }

  if (data->node_host_active) {
    /* Expanded with a live node host: chrome around the canvas, transparent inside the hole. */
    floating_panel_draw_host_cutout(*data, region);
    return;
  }

  /* The onscreen overlay paints the blurred glass when blur is enabled and no interaction is
   * running; the region buffer then only carries the opaque content layer. */
  const bool overlay_active = floating_panel_blur_enabled() &&
                              (data->action == FloatingPanelData::Action::None);
  if (!overlay_active) {
    floating_panel_draw_glass(
        0.0f, 0.0f, float(region->winx), float(region->winy), floating_panel_effective_alpha(*data));
  }
  floating_panel_draw_content(*data, region->winrct);
}

static void floating_panel_region_free(ARegion *region)
{
  if (region->regiondata != nullptr) {
    FloatingPanelData *data = static_cast<FloatingPanelData *>(region->regiondata);
    floating_panel_blur_cache_free(*data);
    MEM_delete(data);
    region->regiondata = nullptr;
  }
}

static ARegionType *floating_panel_regiontype_get()
{
  /* A static type is enough: this region is runtime only (never saved) and has no space-type
   * dependency. Reuse #RGN_TYPE_HUD ("Floating Region") so no DNA/RNA enum needs to change. */
  static ARegionType type;
  static bool is_init = false;
  if (!is_init) {
    memset(&type, 0, sizeof(type));
    type.regionid = RGN_TYPE_HUD;
    type.draw = floating_panel_region_draw;
    type.free = floating_panel_region_free;
    is_init = true;
  }
  return &type;
}

/** \} */

/** \name Placement and interaction
 * \{ */

static ARegion *floating_panel_region_find(const bScreen *screen)
{
  ARegionType *type = floating_panel_regiontype_get();
  for (ARegion &region : screen->regionbase) {
    if (region.runtime->type == type) {
      return &region;
    }
  }
  return nullptr;
}

/** \name Background input blocker (stage E)
 *
 * Areas are dispatched independently: every area whose `totrct` contains the mouse receives the
 * event. The host area overlaps the 3D view, so without this a click inside the node canvas would
 * also reach the 3D view and start its own select/box-select modal. A UI handler prepended to
 * every region of every *other* area consumes mouse events inside the expanded panel rectangle
 * only, leaving everything outside the panel untouched.
 * \{ */

static bool floating_panel_handler_is_blocker(const ARegion *region, const wmWindow *win)
{
  for (const wmEventHandler &handler : region->runtime->handlers) {
    if (handler.type != WM_HANDLER_TYPE_UI) {
      continue;
    }
    const wmEventHandler_UI *ui_handler = reinterpret_cast<const wmEventHandler_UI *>(&handler);
    if (ui_handler->handle_fn == floating_panel_background_blocker &&
        ui_handler->user_data == win)
    {
      return true;
    }
  }
  return false;
}

static void floating_panel_background_blockers_update(bContext *C,
                                                      wmWindow *win,
                                                      bScreen *screen,
                                                      const ScrArea *host,
                                                      bool enable)
{
  if (screen == nullptr) {
    return;
  }
  for (ScrArea &area : screen->areabase) {
    if (&area == host) {
      continue;
    }
    for (ARegion &region : area.regionbase) {
      if (!enable) {
        WM_event_remove_ui_handler(
            &region.runtime->handlers, floating_panel_background_blocker, nullptr, win, false);
      }
      else if (!floating_panel_handler_is_blocker(&region, win)) {
        WM_event_add_ui_handler(C,
                                &region.runtime->handlers,
                                floating_panel_background_blocker,
                                nullptr,
                                win,
                                eWM_EventHandlerFlag(0));
      }
    }
  }
}

static int floating_panel_background_blocker(bContext * /*C*/,
                                             const wmEvent *event,
                                             void *user_data)
{
  /* Buttons / wheel / gestures inside the panel rectangle are swallowed so the editor below
   * never acts on them. Stage F: the left press is swallowed as well. It used to pass through
   * only to keep the window-level `event_queue_check_drag` flag alive, but that let presses on
   * the node canvas hole reach the viewport below (`node.select` returns `PASS_THROUGH` on an
   * empty canvas, so the lower editor could select/deselect objects). The flag is now re-armed
   * on the following motion event by `floating_panel_event_handler`, and this handler also
   * blocks the synthesized `KM_PRESS_DRAG` copy from reaching the lower editor key-map, so
   * native node drag / link / box-select keep working. */
  if (!(ISMOUSE_BUTTON(event->type) || ISMOUSE_WHEEL(event->type) ||
        ISMOUSE_GESTURE(event->type) || event->type == MOUSEPAN))
  {
    return WM_UI_HANDLER_CONTINUE;
  }
  wmWindow *win = static_cast<wmWindow *>(user_data);
  bScreen *screen = WM_window_get_active_screen(win);
  ARegion *panel = screen ? floating_panel_region_find(screen) : nullptr;
  if (panel == nullptr || panel->regiondata == nullptr) {
    return WM_UI_HANDLER_CONTINUE;
  }
  const FloatingPanelData &data = *static_cast<FloatingPanelData *>(panel->regiondata);
  if (!data.node_host_active) {
    return WM_UI_HANDLER_CONTINUE;
  }
  if (!BLI_rcti_isect_pt_v(&panel->winrct, event->xy)) {
    return WM_UI_HANDLER_CONTINUE;
  }
  if (event->type == LEFTMOUSE && event->val == KM_PRESS) {
    g_panel_press_block_window = win;
  }
  return WM_UI_HANDLER_BREAK;
}

/** \} */

/** Stage A initial placement: hard-coded rectangle in window (native pixel) space. */
static rcti floating_panel_default_rect(const wmWindow *win)
{
  const int2 win_size = WM_window_native_pixel_size(win);

  int xmin = int(float(win_size[0]) * PANEL_X_FAC);
  int ymin = int(float(win_size[1]) * PANEL_Y_FAC);
  int xmax = xmin + PANEL_WIDTH - 1;
  int ymax = ymin + PANEL_HEIGHT - 1;

  /* Keep the whole panel inside the window. */
  CLAMP(xmin, 0, max_ii(0, win_size[0] - 1));
  CLAMP(ymin, 0, max_ii(0, win_size[1] - 1));
  CLAMP(xmax, xmin, max_ii(xmin, win_size[0] - 1));
  CLAMP(ymax, ymin, max_ii(ymin, win_size[1] - 1));

  rcti r;
  BLI_rcti_init(&r, xmin, xmax, ymin, ymax);
  return r;
}

/**
 * Anchor dock position: right edge of the 3D viewport, vertically centred. Recomputed every frame
 * so the anchor stays glued to the viewport edge when the area layout changes.
 */
static void floating_panel_anchor_center_update(const wmWindow *win,
                                                const bScreen *screen,
                                                FloatingPanelData &data)
{
  const int2 win_size = WM_window_native_pixel_size(win);
  /* `rcti` is inclusive: place the right edge exactly `ANCHOR_INSET` px inside the viewport. */
  const int edge_x = win_size[0] - 1;
  const int half = (ANCHOR_SIZE - 1) / 2;
  int2 center = {edge_x - ANCHOR_INSET - half, win_size[1] / 2};

  for (const ScrArea &area : screen->areabase) {
    if (area.spacetype != SPACE_VIEW3D) {
      continue;
    }
    const rcti r = area.totrct;
    if (r.xmax <= r.xmin || r.ymax <= r.ymin) {
      continue;
    }
    center[0] = r.xmax - ANCHOR_INSET - half;
    center[1] = (r.ymin + r.ymax) / 2;
    break;
  }
  data.anchor_center = center;
}

static void floating_panel_json_escape(const char *src, char *dst, const size_t dst_size)
{
  size_t di = 0;
  for (const char *c = src; *c != '\0' && di + 2 < dst_size; c++) {
    if (*c == '"' || *c == '\\') {
      dst[di++] = '\\';
      dst[di++] = *c;
    }
    else if (*c == '\n' || *c == '\r') {
      dst[di++] = ' ';
    }
    else {
      dst[di++] = *c;
    }
  }
  dst[di] = '\0';
}

/**
 * Optional machine-readable state snapshot (`EBC_FP_STATE_FILE`), rewritten every frame so the
 * verification scripts can poll live geometry and animation timings instead of guessing.
 */
static void floating_panel_state_dump(const wmWindow *win,
                                      const ARegion *region,
                                      const FloatingPanelData &data)
{
  const char *path = std::getenv("EBC_FP_STATE_FILE");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  const rcti r = region->winrct;
  const int2 win_size = WM_window_native_pixel_size(win);
  const int anchor_size = floating_panel_anchor_size_px(data);
  const rcti anchor = floating_panel_anchor_rect_px(data, anchor_size);
  const rcti lock_r = floating_panel_button_rect_px(r, true);
  const rcti min_r = floating_panel_button_rect_px(r, false);
  const rcti slider_r = floating_panel_slider_rect_px(r);

  FILE *fp = fopen(path, "w");
  if (fp == nullptr) {
    return;
  }
  const int dot_cx = data.node_host_active ? (data.node_rect.xmax + (NODE_HOST_INSET / 2)) : 0;
  const int dot_cy = data.node_host_active ? ((data.node_rect.ymin + data.node_rect.ymax) / 2) : 0;
  const rcti nregion_r = data.node_region ? data.node_region->winrct : rcti{};
  fprintf(fp,
          "{\"t\":%.6f,\"win\":[%d,%d],\"view\":\"%s\",\"anim\":%d,\"anim_ms\":%.2f,"
          "\"hover_p\":%.4f,\"hover_ms\":%.2f,\"hover\":%d,\"action\":%d,\"locked\":%d,"
          "\"alpha\":%.4f,\"override\":%.4f,\"rect\":[%d,%d,%d,%d],"
          "\"anchor\":[%d,%d,%d,%d],\"anchor_size\":%d,"
          "\"lock\":[%d,%d,%d,%d],\"min\":[%d,%d,%d,%d],\"slider\":[%d,%d,%d,%d],"
          "\"host\":%d,\"modal\":%d,\"node\":[%d,%d,%d,%d],"
          "\"nregion\":[%d,%d,%d,%d,%d,%d],\"dot\":[%d,%d,%d],\"dot_rgb\":[23,141,141]",
          BLI_time_now_seconds(),
          win_size[0],
          win_size[1],
          (data.view == FloatingPanelData::View::Expanded) ? "expanded" : "anchor",
          int(data.animating),
          data.anim_last_ms,
          data.hover_progress,
          data.hover_last_ms,
          int(data.hover),
          int(data.action),
          int(data.locked),
          floating_panel_effective_alpha(data),
          data.alpha_override,
          r.xmin,
          r.xmax,
          r.ymin,
          r.ymax,
          anchor.xmin,
          anchor.xmax,
          anchor.ymin,
          anchor.ymax,
          anchor_size,
          lock_r.xmin,
          lock_r.xmax,
          lock_r.ymin,
          lock_r.ymax,
          min_r.xmin,
          min_r.xmax,
          min_r.ymin,
          min_r.ymax,
          slider_r.xmin,
          slider_r.xmax,
          slider_r.ymin,
          slider_r.ymax,
          int(data.node_host_active),
          int(data.modal_active),
          data.node_rect.xmin,
          data.node_rect.xmax,
          data.node_rect.ymin,
          data.node_rect.ymax,
          nregion_r.xmin,
          nregion_r.xmax,
          nregion_r.ymin,
          nregion_r.ymax,
          data.node_region ? int(data.node_region->winx) : -1,
          data.node_region ? int(data.node_region->winy) : -1,
          dot_cx,
          dot_cy,
          MODAL_DOT_RADIUS);

  /* Node / socket screen geometry, for pointer-accurate automation of the native operators. */
  SpaceNode *snode = data.node_area ? data.node_area->spacedata.first_as<SpaceNode>() : nullptr;
  bNodeTree *ntree = (snode != nullptr) ? snode->edittree : nullptr;
  const ARegion *nregion = data.node_region;
  char escaped[128];
  fprintf(fp, ",\"tree\":\"");
  if (ntree != nullptr) {
    floating_panel_json_escape(ntree->id.name + 2, escaped, sizeof(escaped));
    fprintf(fp, "%s", escaped);
  }
  fprintf(fp, "\",\"links\":%d,\"nodes\":[", ntree ? int(ntree->links.count()) : 0);

  bool first_node = true;
  if (ntree != nullptr && nregion != nullptr) {
    for (bNode &node : ntree->nodes) {
      if (node.runtime == nullptr) {
        continue;
      }
      if (!first_node) {
        fprintf(fp, ",");
      }
      first_node = false;

      rcti node_r = {};
      rctf bounds = node.runtime->draw_bounds;
      ui::view2d_view_to_region_rcti(&nregion->v2d, &bounds, &node_r);
      node_r.xmin += nregion->winrct.xmin;
      node_r.xmax += nregion->winrct.xmin;
      node_r.ymin += nregion->winrct.ymin;
      node_r.ymax += nregion->winrct.ymin;

      floating_panel_json_escape(node.name, escaped, sizeof(escaped));
      fprintf(fp,
              "{\"id\":%d,\"name\":\"%s\",\"sel\":%d,\"rect\":[%d,%d,%d,%d],\"sockets\":[",
              node.identifier,
              escaped,
              (node.flag & NODE_SELECT) ? 1 : 0,
              node_r.xmin,
              node_r.xmax,
              node_r.ymin,
              node_r.ymax);

      bool first_socket = true;
      int socket_count = 0;
      auto emit_socket = [&](bNodeSocket &sock, const char *dir) {
        if (sock.runtime == nullptr || socket_count >= 48) {
          return;
        }
        int sx = 0, sy = 0;
        ui::view2d_view_to_region(
            &nregion->v2d, sock.runtime->location[0], sock.runtime->location[1], &sx, &sy);
        sx += nregion->winrct.xmin;
        sy += nregion->winrct.ymin;
        floating_panel_json_escape(sock.name, escaped, sizeof(escaped));
        if (!first_socket) {
          fprintf(fp, ",");
        }
        first_socket = false;
        fprintf(fp,
                "{\"id\":\"%s\",\"name\":\"%s\",\"dir\":\"%s\",\"pos\":[%d,%d]}",
                sock.identifier,
                escaped,
                dir,
                sx,
                sy);
        socket_count++;
      };
      for (bNodeSocket &sock : node.inputs) {
        emit_socket(sock, "in");
      }
      for (bNodeSocket &sock : node.outputs) {
        emit_socket(sock, "out");
      }
      fprintf(fp, "]}");
    }
  }

  fprintf(fp, "]}\n");
  fclose(fp);
}

/** Keep the current rect, only clamping it into the window (used after the initial placement
 * so a user drag is never reset). */
static void floating_panel_region_clamp(ARegion *region, const wmWindow *win)
{
  const int2 win_size = WM_window_native_pixel_size(win);
  const int max_x = max_ii(0, win_size[0] - 1);
  const int max_y = max_ii(0, win_size[1] - 1);

  rcti rect = region->winrct;
  /* Inclusive pixel counts: `rcti` max coords are inside the rect. */
  const int w = min_ii(BLI_rcti_size_x(&rect) + 1, max_x + 1);
  const int h = min_ii(BLI_rcti_size_y(&rect) + 1, max_y + 1);
  CLAMP(rect.xmin, 0, max_ii(0, max_x - w + 1));
  CLAMP(rect.ymin, 0, max_ii(0, max_y - h + 1));
  rect.xmax = rect.xmin + w - 1;
  rect.ymax = rect.ymin + h - 1;
  BLI_rcti_init(&region->winrct, rect.xmin, rect.xmax, rect.ymin, rect.ymax);
}

/**
 * Update the panel rect from the drag/resize delta.
 *
 * Policy (explicit, see stage B doc): the panel is always clamped fully inside the window.
 * It is NOT clamped to #ScrArea bounds, so dragging across area borders is allowed and works
 * (the region is screen level and is composited above every area).
 */
static void floating_panel_apply_interaction(FloatingPanelData &data,
                                             ARegion *region,
                                             const int2 mouse,
                                             const int2 win_size)
{
  const int dx = mouse[0] - data.grab_mouse[0];
  const int dy = mouse[1] - data.grab_mouse[1];
  const int max_x = max_ii(0, win_size[0] - 1);
  const int max_y = max_ii(0, win_size[1] - 1);

  rcti rect = data.grab_rect;

  if (data.action == FloatingPanelData::Action::Move) {
    rect.xmin += dx;
    rect.xmax += dx;
    rect.ymin += dy;
    rect.ymax += dy;

    const int w = BLI_rcti_size_x(&rect) + 1;
    const int h = BLI_rcti_size_y(&rect) + 1;
    CLAMP(rect.xmin, 0, max_ii(0, max_x - w + 1));
    CLAMP(rect.ymin, 0, max_ii(0, max_y - h + 1));
    rect.xmax = rect.xmin + w - 1;
    rect.ymax = rect.ymin + h - 1;
  }
  else {
    /* Bottom-right corner: `xmax` follows the mouse in x, `ymin` follows it in y. */
    rect.xmax += dx;
    rect.ymin += dy;
    CLAMP(rect.xmax, min_ii(rect.xmin + PANEL_MIN_WIDTH - 1, max_x), max_x);
    CLAMP(rect.ymin, 0, max_ii(0, rect.ymax - PANEL_MIN_HEIGHT + 1));
  }

  BLI_rcti_init(&region->winrct, rect.xmin, rect.xmax, rect.ymin, rect.ymax);
  ED_region_update_rect(region);
  ED_region_tag_redraw(region);
}

/** Map the slider x position to the per-panel alpha override (0.05 - 1.0). */
static void floating_panel_slider_apply(ARegion *region, FloatingPanelData &data, const int2 mouse)
{
  float x0, x1, y;
  floating_panel_slider_track_px(region->winrct, x0, x1, y);
  float frac = (float(mouse[0]) - x0) / std::max(1.0f, x1 - x0);
  CLAMP(frac, 0.0f, 1.0f);
  data.alpha_override = 0.05f + 0.95f * frac;
  ED_region_tag_redraw(region);
}

/**
 * Hover is only evaluated while no interaction is running (stage B rule: no hover animation
 * during drag / resize / slider). Anchor hover drives the 175 ms ramp; expanded-panel icons
 * switch color instantly.
 */
static void floating_panel_hover_update(bContext *C,
                                        wmWindow *win,
                                        ARegion *region,
                                        FloatingPanelData &data,
                                        const int2 mouse)
{
  if (data.animating) {
    return;
  }

  FloatingPanelData::Hover desired = FloatingPanelData::Hover::None;
  if (data.view == FloatingPanelData::View::Anchor) {
    const rcti base = floating_panel_anchor_rect_px(data, ANCHOR_SIZE);
    if (BLI_rcti_isect_pt(&base, mouse[0], mouse[1])) {
      desired = FloatingPanelData::Hover::Anchor;
    }
  }
  else if (BLI_rcti_isect_pt(&region->winrct, mouse[0], mouse[1])) {
    const rcti lock_r = floating_panel_button_rect_px(region->winrct, true);
    const rcti min_r = floating_panel_button_rect_px(region->winrct, false);
    const rcti slider_r = floating_panel_slider_rect_px(region->winrct);
    if (BLI_rcti_isect_pt(&lock_r, mouse[0], mouse[1])) {
      desired = FloatingPanelData::Hover::Lock;
    }
    else if (BLI_rcti_isect_pt(&min_r, mouse[0], mouse[1])) {
      desired = FloatingPanelData::Hover::Minimize;
    }
    else if (!data.locked && BLI_rcti_isect_pt(&slider_r, mouse[0], mouse[1])) {
      desired = FloatingPanelData::Hover::Slider;
    }
  }

  const bool changed = (desired != data.hover);
  data.hover = desired;

  if (data.view == FloatingPanelData::View::Anchor) {
    const float target = (desired == FloatingPanelData::Hover::Anchor) ? 1.0f : 0.0f;
    if (target != data.hover_to) {
      data.hover_from = data.hover_progress;
      data.hover_to = target;
      data.hover_start = BLI_time_now_seconds();
      data.hover_ramp_active = true;
      floating_panel_timer_ensure(C, win, data);
      fprintf(stderr, "[FP-ANIM] hover %s start\n", (target > 0.0f) ? "in" : "out");
    }
  }

  if (changed) {
    ED_region_tag_redraw(region);
  }
}

/** \} */

/** \name Event interception
 * \{ */

/** Modal operators own input while running: the panel degrades to "draw only". */
static bool wm_window_has_modal_operator(const wmWindow *win)
{
  for (const wmEventHandler &handler : win->runtime->modalhandlers) {
    if (handler.type != WM_HANDLER_TYPE_OP) {
      continue;
    }
    const wmEventHandler_Op *op_handler = reinterpret_cast<const wmEventHandler_Op *>(&handler);
    if (op_handler->op != nullptr) {
      return true;
    }
  }
  return false;
}

static void floating_panel_cancel_interaction(ARegion *region, FloatingPanelData &data)
{
  if (data.action != FloatingPanelData::Action::None) {
    data.action = FloatingPanelData::Action::None;
    data.grab_screen = nullptr;
    ED_region_tag_redraw(region);
  }
}

/**
 * Registered in `win->runtime->modalhandlers` (see #ED_screen_floating_panel_ensure).
 *
 * - Anchor: hover ramp + click to expand. No drag in the collapsed state (by design).
 * - Expanded: title-bar move, corner resize, lock toggle, minimize (collapse) and the
 *   per-panel alpha slider. Everything outside the panel rect is passed through untouched.
 * - Timer: drives the 300 ms expand/collapse and the 175 ms anchor hover ramp.
 */
static int floating_panel_event_handler(bContext *C, const wmEvent *event, void *user_data)
{
  wmWindow *win = static_cast<wmWindow *>(user_data);
  if (win == nullptr) {
    return WM_UI_HANDLER_CONTINUE;
  }

  /* Stage F: the left press inside the canvas hole was consumed by
   * `floating_panel_background_blocker`, which clears the window-level drag flag. Re-arm it once
   * on the following motion so the native node editor still gets `KM_PRESS_DRAG` (`node.link`,
   * `node.select_box`). The blocker stops that synthesized drag from reaching the lower editor.
   * Clearing the marker on that first re-arm also prevents a stale re-arm once a modal operator
   * has consumed the release (which would otherwise start a spurious lower-editor drag on the next
   * unrelated motion). */
  if (g_panel_press_block_window == win) {
    if (ISMOUSE_MOTION(event->type)) {
      win->event_queue_check_drag = true;
      g_panel_press_block_window = nullptr;
    }
    else if ((event->type == LEFTMOUSE && event->val == KM_RELEASE) ||
             event->type == WINDEACTIVATE)
    {
      g_panel_press_block_window = nullptr;
    }
  }

  /* Timer events must always pass through (they are "always-pass" events). The timer may also
   * outlive the region (screen switch): then it removes itself here. */
  if (event->type == TIMER) {
    if (g_floating_panel_timer == nullptr || event->customdata != g_floating_panel_timer) {
      return WM_UI_HANDLER_CONTINUE;
    }
    bScreen *timer_screen = WM_window_get_active_screen(win);
    ARegion *timer_region = timer_screen ? floating_panel_region_find(timer_screen) : nullptr;
    if (timer_region == nullptr || timer_region->regiondata == nullptr) {
      wmWindowManager *wm = (C != nullptr) ? CTX_wm_manager(C) : nullptr;
      if (wm != nullptr) {
        WM_event_timer_remove(wm, g_floating_panel_timer_window, g_floating_panel_timer);
      }
      g_floating_panel_timer = nullptr;
      g_floating_panel_timer_window = nullptr;
      return WM_UI_HANDLER_CONTINUE;
    }
    floating_panel_anim_tick(
        C, win, timer_region, *static_cast<FloatingPanelData *>(timer_region->regiondata));
    return WM_UI_HANDLER_CONTINUE;
  }

  bScreen *screen = WM_window_get_active_screen(win);
  ARegion *region = screen ? floating_panel_region_find(screen) : nullptr;
  if (region == nullptr || region->regiondata == nullptr) {
    return WM_UI_HANDLER_CONTINUE;
  }
  FloatingPanelData &data = *static_cast<FloatingPanelData *>(region->regiondata);

  /* Modal operators run first in the same list; they own input. Degrade to drawing only. */
  if (wm_window_has_modal_operator(win)) {
    floating_panel_cancel_interaction(region, data);
    return WM_UI_HANDLER_CONTINUE;
  }

  /* A screen switch during a drag silently cancels it (the release will never arrive here). */
  if (data.action != FloatingPanelData::Action::None && data.grab_screen != screen) {
    floating_panel_cancel_interaction(region, data);
  }

  if (event->type == WINDEACTIVATE) {
    floating_panel_cancel_interaction(region, data);
    return WM_UI_HANDLER_CONTINUE;
  }

  const int2 mouse = {event->xy[0], event->xy[1]};

  /* Inside the canvas hole the native node editor owns the event: never start a panel
   * interaction there, never consume it, and drop any hover state. */
  if (data.action == FloatingPanelData::Action::None && data.node_host_active &&
      BLI_rcti_isect_pt_v(&data.node_rect, event->xy))
  {
    if (data.hover != FloatingPanelData::Hover::None || data.hover_ramp_active) {
      data.hover = FloatingPanelData::Hover::None;
      data.hover_progress = 0.0f;
      data.hover_ramp_active = false;
      ED_region_tag_redraw(region);
    }
    return WM_UI_HANDLER_CONTINUE;
  }

  if (ISMOUSE_MOTION(event->type)) {
    if (data.action == FloatingPanelData::Action::Slider) {
      floating_panel_slider_apply(region, data, mouse);
      return WM_UI_HANDLER_BREAK;
    }
    if (data.action != FloatingPanelData::Action::None) {
      floating_panel_apply_interaction(data, region, mouse, WM_window_native_pixel_size(win));
      return WM_UI_HANDLER_BREAK;
    }
    floating_panel_hover_update(C, win, region, data, mouse);
    return WM_UI_HANDLER_CONTINUE;
  }

  if (event->type != LEFTMOUSE) {
    return WM_UI_HANDLER_CONTINUE;
  }

  if (event->val == KM_PRESS) {
    if (data.animating || data.action != FloatingPanelData::Action::None) {
      return WM_UI_HANDLER_CONTINUE;
    }

    /* --- Collapsed anchor: click expands, drag is not offered. --- */
    if (data.view == FloatingPanelData::View::Anchor) {
      const rcti base = floating_panel_anchor_rect_px(data, ANCHOR_SIZE);
      if (!BLI_rcti_isect_pt(&base, mouse[0], mouse[1])) {
        return WM_UI_HANDLER_CONTINUE;
      }
      if (!data.has_expanded_rect) {
        data.expanded_rect = floating_panel_default_rect(win);
        data.has_expanded_rect = true;
      }
      floating_panel_anim_start(
          data, FloatingPanelData::View::Expanded, region->winrct, data.expanded_rect);
      data.hover_ramp_active = false;
      data.hover = FloatingPanelData::Hover::None;
      floating_panel_timer_ensure(C, win, data);
      ED_region_tag_redraw(region);
      return WM_UI_HANDLER_BREAK;
    }

    /* --- Expanded panel. --- */
    if (!BLI_rcti_isect_pt(&region->winrct, mouse[0], mouse[1])) {
      return WM_UI_HANDLER_CONTINUE;
    }

    const rcti lock_r = floating_panel_button_rect_px(region->winrct, true);
    const rcti min_r = floating_panel_button_rect_px(region->winrct, false);
    const rcti slider_r = floating_panel_slider_rect_px(region->winrct);

    if (BLI_rcti_isect_pt(&lock_r, mouse[0], mouse[1])) {
      /* Freeze the effective alpha on lock so the global env cannot change it either. */
      if (!data.locked) {
        data.alpha_override = floating_panel_effective_alpha(data);
      }
      data.locked = !data.locked;
      ED_region_tag_redraw(region);
      return WM_UI_HANDLER_BREAK;
    }
    if (BLI_rcti_isect_pt(&min_r, mouse[0], mouse[1])) {
      data.expanded_rect = region->winrct;
      data.has_expanded_rect = true;
      const rcti to = floating_panel_anchor_rect_px(data, ANCHOR_SIZE);
      floating_panel_anim_start(data, FloatingPanelData::View::Anchor, region->winrct, to);
      data.hover_progress = 0.0f;
      data.hover_from = 0.0f;
      data.hover_to = 0.0f;
      data.hover_ramp_active = false;
      data.hover = FloatingPanelData::Hover::None;
      floating_panel_timer_ensure(C, win, data);
      ED_region_tag_redraw(region);
      return WM_UI_HANDLER_BREAK;
    }
    if (data.locked) {
      /* Locked: consume the press inside the panel, but move/resize/slider do nothing. */
      return WM_UI_HANDLER_BREAK;
    }
    if (BLI_rcti_isect_pt(&slider_r, mouse[0], mouse[1])) {
      data.action = FloatingPanelData::Action::Slider;
      data.grab_screen = screen;
      data.hover = FloatingPanelData::Hover::Slider;
      floating_panel_slider_apply(region, data, mouse);
      return WM_UI_HANDLER_BREAK;
    }

    rcti handle = floating_panel_handle_rect(region);
    data.action = BLI_rcti_isect_pt(&handle, mouse[0], mouse[1]) ?
                      FloatingPanelData::Action::Resize :
                      FloatingPanelData::Action::Move;
    data.grab_mouse = mouse;
    data.grab_rect = region->winrct;
    data.grab_screen = screen;
    data.hover = FloatingPanelData::Hover::None;
    ED_region_tag_redraw(region);
    return WM_UI_HANDLER_BREAK;
  }

  if (event->val == KM_RELEASE && data.action != FloatingPanelData::Action::None) {
    /* Consume the release too, otherwise the editor below sees an unmatched release. */
    data.action = FloatingPanelData::Action::None;
    data.grab_screen = nullptr;
    floating_panel_hover_update(C, win, region, data, mouse);
    return WM_UI_HANDLER_BREAK;
  }

  return WM_UI_HANDLER_CONTINUE;
}

static void floating_panel_handler_remove(bContext * /*C*/, void * /*user_data*/) {}

static bool floating_panel_handler_exists(const wmWindow *win)
{
  for (const wmEventHandler &handler : win->runtime->modalhandlers) {
    if (handler.type != WM_HANDLER_TYPE_UI) {
      continue;
    }
    const wmEventHandler_UI *ui_handler = reinterpret_cast<const wmEventHandler_UI *>(&handler);
    if (ui_handler->handle_fn == floating_panel_event_handler &&
        ui_handler->user_data == win)
    {
      return true;
    }
  }
  return false;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public API
 * \{ */

void ED_screen_floating_panel_ensure(bContext *C, wmWindow *win, bScreen *screen)
{
  if (screen == nullptr) {
    return;
  }

  ARegion *region = floating_panel_region_find(screen);

  if (region == nullptr) {
    region = BKE_area_region_new();
    region->regiontype = RGN_TYPE_HUD;
    region->alignment = RGN_ALIGN_FLOAT;
    region->runtime->type = floating_panel_regiontype_get();
    FloatingPanelData *data = MEM_new<FloatingPanelData>(__func__);
    data->view = FloatingPanelData::View::Anchor;
    region->regiondata = data;

    /* Start collapsed: dock the anchor to the right edge of the 3D viewport. Placed before
     * initializing so `winx/winy` are valid on the very first draw. */
    floating_panel_anchor_center_update(win, screen, *data);
    const rcti anchor = floating_panel_anchor_rect_px(*data, ANCHOR_SIZE);
    BLI_rcti_init(&region->winrct, anchor.xmin, anchor.xmax, anchor.ymin, anchor.ymax);
    ED_region_floating_init(region);

    BLI_addtail(&screen->regionbase, region);
    ED_region_tag_redraw(region);
  }
  else {
    FloatingPanelData *data = static_cast<FloatingPanelData *>(region->regiondata);
    if (data != nullptr) {
      /* Keep the collapsed anchor glued to the viewport edge; while expanded (or animating)
       * keep whatever the user placed so a drag is never reset. */
      floating_panel_anchor_center_update(win, screen, *data);
      if (!data->animating) {
        if (data->view == FloatingPanelData::View::Anchor) {
          const int size = floating_panel_anchor_size_px(*data);
          const rcti anchor = floating_panel_anchor_rect_px(*data, size);
          if (anchor.xmin != region->winrct.xmin || anchor.ymin != region->winrct.ymin ||
              anchor.xmax != region->winrct.xmax || anchor.ymax != region->winrct.ymax)
          {
            BLI_rcti_init(&region->winrct, anchor.xmin, anchor.xmax, anchor.ymin, anchor.ymax);
            ED_region_tag_redraw(region);
          }
        }
        else {
          floating_panel_region_clamp(region, win);
        }
      }
    }
    /* `ED_screen_exit()` (workspace/screen switch) clears `runtime->visible` and the draw buffer
     * for every screen-level region while the screen is inactive, so re-initialize every frame. */
    ED_region_floating_init(region);
  }

  /* Register the input handler once per window. Registered with a null context on purpose: it
   * must not set/unset #bContext::wm::area or region, and it must not add key-map entries. */
  if (!floating_panel_handler_exists(win)) {
    WM_event_add_ui_handler(nullptr,
                            &win->runtime->modalhandlers,
                            floating_panel_event_handler,
                            floating_panel_handler_remove,
                            win,
                            eWM_EventHandlerFlag(0));
  }

  if (region->regiondata != nullptr) {
    FloatingPanelData &data = *static_cast<FloatingPanelData *>(region->regiondata);

    /* Modal ownership (sculpt stroke, node drag, box select, ...): the panel keeps drawing but
     * stops all interaction and freezes the hover state. */
    const bool modal = wm_window_has_modal_operator(win);
    if (modal != data.modal_active) {
      data.modal_active = modal;
      ED_region_tag_redraw(region);
    }
    if (modal && (data.hover != FloatingPanelData::Hover::None || data.hover_ramp_active)) {
      data.hover = FloatingPanelData::Hover::None;
      data.hover_progress = 0.0f;
      data.hover_ramp_active = false;
      ED_region_tag_redraw(region);
    }

    /* The host area only exists in the fully expanded, resting state. It is created after the
     * expand animation and removed as soon as the collapse starts. */
    const bool host_wanted = (C != nullptr) && !data.animating &&
                             (data.view == FloatingPanelData::View::Expanded);

    if (host_wanted) {
      const rcti node_rect = floating_panel_node_rect(region->winrct);
      if (data.node_area == nullptr) {
        floating_panel_node_host_create(C, win, data, node_rect);
      }
      else {
        floating_panel_node_host_sync(data, node_rect);
        data.node_rect = node_rect;
      }
      if (data.node_host_active) {
        /* A refresh pass can clear an explicitly selected tree; re-bind it (pinned) if that
         * happened, so the canvas never goes blank. */
        SpaceNode *snode = data.node_area->spacedata.first_as<SpaceNode>();
        if (snode != nullptr && snode->edittree == nullptr) {
          floating_panel_node_host_bind(C, data);
        }
        floating_panel_background_blockers_update(C, win, screen, data.node_area, true);
      }
    }
    else if (data.node_area != nullptr) {
      floating_panel_node_host_destroy(C, win, data);
    }
  }

  if (region->regiondata != nullptr) {
    floating_panel_state_dump(win, region, *static_cast<FloatingPanelData *>(region->regiondata));
  }
}

/**
 * Onscreen overlay entry point, called from `wm_draw_window_onscreen()` after all areas were
 * composited into the window frame-buffer and before the panel's own region buffer is blended.
 *
 * This is the only place where the composited backdrop is available as pixels: the region draw
 * callback renders into its own offscreen frame-buffer and cannot see the window content. Drawing
 * here cannot race the viewport redraw because `wm_draw_window_offscreen()` (which draws every
 * area, including the 3D viewport) has already finished for this frame; the window frame-buffer
 * is a plain composite at this point.
 */
void ED_screen_floating_panel_overlay_draw(wmWindow *win)
{
  if (win == nullptr) {
    return;
  }
  bScreen *screen = WM_window_get_active_screen(win);
  ARegion *region = screen ? floating_panel_region_find(screen) : nullptr;
  if (region == nullptr || region->regiondata == nullptr) {
    return;
  }
  FloatingPanelData &data = *static_cast<FloatingPanelData *>(region->regiondata);

  if (data.node_host_active) {
    /* The host area sits first in `screen->areabase` so it wins event dispatch, which also means
     * its non-overlap blit happens *before* the other areas and gets painted over. Re-composite
     * its `GPUViewport` here: after all areas and edges, before the panel region (which has a
     * transparent canvas hole) is blended on top. */
    if (data.node_region != nullptr && data.node_region->runtime->draw_buffer != nullptr) {
      GPUViewport *viewport = data.node_region->runtime->draw_buffer->viewport;
      if (viewport != nullptr) {
        GPU_color_mask(true, true, true, false);
        GPU_viewport_draw_to_screen(viewport, 0, &data.node_region->winrct);
        GPU_color_mask(true, true, true, true);
      }
    }
    /* The region draw owns the chrome around the canvas hole; a window-space blur pass would
     * cover the live node canvas. */
    floating_panel_log_blur_state("skipped (node host)");
    return;
  }

  if (!floating_panel_blur_enabled()) {
    floating_panel_log_blur_state("fallback (blur disabled)");
    return;
  }
  if (data.view != FloatingPanelData::View::Expanded || data.animating) {
    /* Collapsed anchor or a running expand/collapse: the offscreen region draw owns the frame. */
    floating_panel_log_blur_state("skipped (anchor/animation)");
    return;
  }
  if (data.action != FloatingPanelData::Action::None) {
    /* Drag/resize degradation: skip the capture and the blur passes entirely; the region draw
     * paints the cheap fallback glass for those frames. */
    floating_panel_log_blur_state("skipped (drag/resize)");
    return;
  }

  const int2 win_size = WM_window_native_pixel_size(win);
  const rcti rect = region->winrct;
  const double time_start = BLI_time_now_seconds();
  if (floating_panel_overlay_blur(data, rect, win_size, floating_panel_effective_alpha(data))) {
    floating_panel_blur_timing_add((BLI_time_now_seconds() - time_start) * 1000.0);
    floating_panel_log_blur_state("blur capture");
    return;
  }

  /* Blur unavailable (no active frame-buffer, cache allocation failed, ...): paint the fallback
   * in window space. The offscreen region draw was skipped since blur is nominally enabled, so
   * this is what keeps the panel visible on every path. */
  floating_panel_log_blur_state("fallback (blur unavailable)");
  GPU_viewport(0, 0, win_size[0], win_size[1]);
  GPU_matrix_ortho_2d_set(0.0f, float(win_size[0]), 0.0f, float(win_size[1]));
  GPU_scissor_test(false);
  GPU_blend(GPU_BLEND_NONE);
  floating_panel_draw_glass(float(rect.xmin),
                            float(rect.ymin),
                            float(rect.xmax - rect.xmin + 1),
                            float(rect.ymax - rect.ymin + 1),
                            floating_panel_effective_alpha(data));
}

/** \} */

}  // namespace blender::ui
