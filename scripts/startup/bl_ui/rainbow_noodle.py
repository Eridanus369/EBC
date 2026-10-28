import bpy
import gpu
import time
import colorsys
import ctypes
from ctypes import c_void_p, c_float, c_char
from math import pi, exp, hypot, sin
from gpu_extras.batch import batch_for_shader
from mathutils import Vector


# Blender 5.2 的 UString 字段使 runtime.location 从偏移 24 移到 32。
_SOCKET_RUNTIME_LOCATION_OFFSET = 32 if bpy.app.version >= (5, 2, 0) else 24


class _BNodeSocketRuntime(ctypes.Structure):
    _fields_ = [
        ("_pad", c_char * _SOCKET_RUNTIME_LOCATION_OFFSET),
        ("location", c_float * 2),
    ]


class _BNodeSocket(ctypes.Structure):
    _fields_ = [
        ("_pad", c_char * 456),
        ("runtime", ctypes.POINTER(_BNodeSocketRuntime)),
    ]


def _get_socket_view_loc(socket):
    """从 socket 指针直接读取视图空间坐标，结果在 _frame_cache['svl'] 内缓存。"""
    ptr = socket.as_pointer()
    cache = _frame_cache.get('svl')
    if cache is None:
        cache = {}
        _frame_cache['svl'] = cache
    elif ptr in cache:
        return cache[ptr]
    try:
        node = socket.node
        nlocx, nlocy = abs_node_location(node)
        # 官方安全 API 读取相对坐标
        result = (nlocx + socket.location.x, nlocy + socket.location.y)
        cache[ptr] = result
        return result
    except Exception:
        cache[ptr] = None
        return None

_DRAW_HANDLE = None
_OUTLINE_UBO = None
_OUTLINE_UBO_BUF = None
_LINK_UBO = None
_LINK_GEOM_UBO = None
_RAINBOW_UBO = None
_INSTANCED_BATCH = None
_INSTANCED_SAMPLES = 25
_SHADER_CACHE = {}
_BEZIER_COEFFS = [(1.0, 0.0, 0.0, 0.0), (0.8801359953703705, 0.11480034722222224, 0.004991319444444444, 7.233796296296295e-05), (0.7702546296296295, 0.21006944444444442, 0.01909722222222222, 0.0005787037037037036), (0.669921875, 0.287109375, 0.041015625, 0.001953125), (0.5787037037037038, 0.3472222222222222, 0.06944444444444445, 0.0046296296296296285), (0.4961660879629629, 0.3917100694444444, 0.10308159722222224, 0.009042245370370372), (0.421875, 0.421875, 0.140625, 0.015625), (0.3553964120370369, 0.43901909722222215, 0.18077256944444448, 0.0248119212962963), (0.2962962962962964, 0.4444444444444445, 0.2222222222222222, 0.03703703703703703), (0.244140625, 0.439453125, 0.263671875, 0.052734375), (0.1984953703703703, 0.4253472222222221, 0.3038194444444444, 0.07233796296296298), (0.1589265046296297, 0.4034288194444445, 0.3413628472222222, 0.09628182870370369), (0.125, 0.375, 0.375, 0.125), (0.09628182870370372, 0.34136284722222227, 0.40342881944444436, 0.1589265046296296), (0.07233796296296294, 0.3038194444444444, 0.4253472222222223, 0.1984953703703704), (0.052734375, 0.263671875, 0.439453125, 0.244140625), (0.03703703703703705, 0.22222222222222224, 0.4444444444444444, 0.2962962962962962), (0.024811921296296287, 0.1807725694444444, 0.4390190972222222, 0.3553964120370371), (0.015625, 0.140625, 0.421875, 0.421875), (0.009042245370370376, 0.10308159722222225, 0.3917100694444445, 0.4961660879629629), (0.004629629629629627, 0.06944444444444442, 0.3472222222222222, 0.5787037037037038), (0.001953125, 0.041015625, 0.287109375, 0.669921875), (0.0005787037037037044, 0.01909722222222224, 0.2100694444444445, 0.7702546296296295), (7.233796296296277e-05, 0.004991319444444435, 0.11480034722222214, 0.8801359953703705), (0.0, 0.0, 0.0, 1.0)]
_AREAS_TO_REDRAW = set()
# 动态降载：记录上一帧的视口/节点指纹，判断是否正在交互（移动节点/平移视口）
_INTERACT_STATE = {}  # region_ptr -> {'prev_fp', 'frozen_time', 'last_move_t', 'outline_alpha'}
_INPUT_UPSTREAM_CACHE = {}
_LINE_UV_CACHE = {}
_RAINBOW_GAP_LIMIT = 64
_LINK_OCCLUDER_LIMIT = 32
_BATCH_GROUP_SIZE = 30
_BATCH_OCC_LIMIT = 16
_OUTLINE_OCCLUDER_LIMIT = 16
_UI_ORDER_OFFSET = None
LINK_RESOL = 24

# 性能检测（每 _PERF_LOG_INTERVAL 帧输出一次）
_PERF_FRAME_CNT = 0
_PERF_ACCUM_TIME = 0.0
_PERF_ACCUM_LINKS = 0
_PERF_ACCUM_OUTLINES = 0
_PERF_ACCUM_OCCLUDERS = 0
_PERF_ACCUM_SELECTED = 0
_PERF_ACCUM_UPSTREAM = 0
_PERF_ACCUM_DOWNSTREAM = 0
_PERF_ACCUM_T_OCCLUDER = 0.0
_PERF_ACCUM_T_BATCH = 0.0
_PERF_ACCUM_T_OUTLINE = 0.0
_PERF_ACCUM_T_FRONT_OCC = 0.0
_PERF_ACCUM_T_OUTLINE_SETUP = 0.0
_PERF_ACCUM_T_OUTLINE_GAP = 0.0
_PERF_ACCUM_T_OUTLINE_UBO = 0.0
_PERF_ACCUM_T_OUTLINE_DRAW = 0.0
_PERF_ACCUM_T_LINK_LOOP = 0.0
_PERF_ACCUM_T_BEZIER_V2R = 0.0
_PERF_ACCUM_T_NODE_DISCOVER = 0.0
_PERF_ACCUM_T_LINK_COLLECT = 0.0
_PERF_ACCUM_T_LINK_SCAN = 0.0
_PERF_ACCUM_T_CACHE_BUILD = 0.0
_PERF_ACCUM_T_BATCH_UBO_MAIN = 0.0
_PERF_ACCUM_T_BATCH_LDATA = 0.0
_PERF_ACCUM_T_BATCH_LDRAW = 0.0
_PERF_LOG_INTERVAL = 30


def _reset_perf_stats():
    global _PERF_FRAME_CNT, _PERF_ACCUM_TIME, _PERF_ACCUM_LINKS, _PERF_ACCUM_OCCLUDERS, _PERF_ACCUM_OUTLINES
    global _PERF_ACCUM_SELECTED, _PERF_ACCUM_UPSTREAM, _PERF_ACCUM_DOWNSTREAM
    global _PERF_ACCUM_T_OCCLUDER, _PERF_ACCUM_T_BATCH, _PERF_ACCUM_T_BATCH_UBO_MAIN
    global _PERF_ACCUM_T_BATCH_LDATA, _PERF_ACCUM_T_BATCH_LDRAW
    global _PERF_ACCUM_T_OUTLINE, _PERF_ACCUM_T_FRONT_OCC, _PERF_ACCUM_T_OUTLINE_SETUP, _PERF_ACCUM_T_OUTLINE_GAP
    global _PERF_ACCUM_T_OUTLINE_UBO, _PERF_ACCUM_T_OUTLINE_DRAW, _PERF_ACCUM_T_LINK_LOOP, _PERF_ACCUM_T_BEZIER_V2R
    global _PERF_ACCUM_T_NODE_DISCOVER, _PERF_ACCUM_T_LINK_COLLECT, _PERF_ACCUM_T_LINK_SCAN, _PERF_ACCUM_T_CACHE_BUILD
    _PERF_FRAME_CNT = 0
    _PERF_ACCUM_TIME = 0.0
    _PERF_ACCUM_LINKS = 0
    _PERF_ACCUM_OCCLUDERS = 0
    _PERF_ACCUM_OUTLINES = 0
    _PERF_ACCUM_SELECTED = 0
    _PERF_ACCUM_UPSTREAM = 0
    _PERF_ACCUM_DOWNSTREAM = 0
    _PERF_ACCUM_T_OCCLUDER = 0.0
    _PERF_ACCUM_T_BATCH = 0.0
    _PERF_ACCUM_T_BATCH_UBO_MAIN = 0.0
    _PERF_ACCUM_T_BATCH_LDATA = 0.0
    _PERF_ACCUM_T_BATCH_LDRAW = 0.0
    _PERF_ACCUM_T_OUTLINE = 0.0
    _PERF_ACCUM_T_FRONT_OCC = 0.0
    _PERF_ACCUM_T_OUTLINE_SETUP = 0.0
    _PERF_ACCUM_T_OUTLINE_GAP = 0.0
    _PERF_ACCUM_T_OUTLINE_UBO = 0.0
    _PERF_ACCUM_T_OUTLINE_DRAW = 0.0
    _PERF_ACCUM_T_LINK_LOOP = 0.0
    _PERF_ACCUM_T_BEZIER_V2R = 0.0
    _PERF_ACCUM_T_NODE_DISCOVER = 0.0
    _PERF_ACCUM_T_LINK_COLLECT = 0.0
    _PERF_ACCUM_T_LINK_SCAN = 0.0
    _PERF_ACCUM_T_CACHE_BUILD = 0.0

# 帧缓存（每帧在 _draw_links 开头清空，避免重复读取 bpy.context）
_frame_cache = {}

_SOCKET_SHAPE_CODE = {
    'CIRCLE': 0,
    'SQUARE': 1,
    'DIAMOND': 2,
    'CIRCLE_DOT': 3,
    'SQUARE_DOT': 4,
    'DIAMOND_DOT': 5,
    'LINE': 6,
    'VOLUME_GRID': 7,
    'LIST': 8,
}


def _get_socket_shape_code(socket):
    cache = _frame_cache.get('sshape')
    if cache is None:
        cache = {}
        _frame_cache['sshape'] = cache
    ptr = socket.as_pointer()
    cached = cache.get(ptr)
    if cached is not None:
        return cached
    shape = getattr(socket, 'display_shape', 'CIRCLE')
    code = _SOCKET_SHAPE_CODE.get(shape, 0)
    cache[ptr] = code
    return code


def _socket_outline_gap(socket, region_pt, center_x, center_y, is_output, edge_x=None):
    shape = _get_socket_shape_code(socket)
    # 多输入长条端口需要沿用 LINE 端口的真实细条宽度，只按连线堆叠去拉高；
    # 之前把 widget 大小和描边额外叠到宽度里，缺口会吃进节点内部，缩放后也会飘。
    if (not is_output) and getattr(socket, "is_multi_input", False):
        counts = _get_mi_counts(socket)
        total = counts.get(socket.as_pointer(), 0)
        view_scale = _frame_cache.get('outline_vs', 1.0)
        stroke_px = _frame_cache.get('outline_stroke_px', 0.0)
        gap_radius_px = _frame_cache.get('outline_gap_radius_px')
        _wu = _frame_cache.get('outline_wu')
        if _wu:
            ps, wu = _wu
        else:
            ps = bpy.context.preferences.system.pixel_size
            wu = round(18 * dpi_fac()) + 2 * ps
        if gap_radius_px is None:
            gap_radius_px = 0.25 * wu * view_scale
        gap_step_px = 0.25 * wu * view_scale
        half_w_px = min(max(stroke_px * 1.05 + 1.5, gap_radius_px * 0.72), gap_radius_px * 1.15)
        half_h_px = gap_step_px + max(0.5 * gap_step_px * max(total, 1), gap_step_px) + stroke_px
        gap_x = edge_x if edge_x is not None else region_pt[0] - center_x + 0.25
        return (gap_x,
                region_pt[1] - center_y + 0.5,
                -half_w_px,   # 负值 = 显式矩形 half_width
                half_h_px)    # half_height
    if shape == 6:
        # LINE sockets need a wider outline slot than their visible bar, otherwise
        # thick outlines cover the narrow port.
        x_offset = 0.75 if is_output else 0.25
        return (region_pt[0] - center_x + x_offset,
                region_pt[1] - center_y + 0.5,
                shape,
                1.0)
    return (region_pt[0] - center_x + 1.0,
            region_pt[1] - center_y + 1.0,
            shape,
            1.0)


def get_pixel_size():
    val = _frame_cache.get('px')
    if val is not None:
        return val
    val = bpy.context.preferences.system.pixel_size
    _frame_cache['px'] = val
    return val


def _on_gradient_change(self, context):
    addon = context.preferences.addons.get(__package__)
    if not addon:
        return
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'NODE_EDITOR':
                area.tag_redraw()


def get_curving_factor():
    val = _frame_cache.get('curv')
    if val is not None:
        return val
    try:
        val = bpy.context.preferences.themes[0].node_editor.noodle_curving / 10.0
    except Exception:
        val = 0.0
    _frame_cache['curv'] = val
    return val


def get_gradient_params():
    val = _frame_cache.get('grad')
    if val is not None:
        return val
    val = (0.0, 1.0, 0.65, 0.4)
    _frame_cache['grad'] = val
    return val


def get_pulse_style():
    """返回脉冲样式编码：0=平滑，1=点状，2=呼吸灯"""
    val = _frame_cache.get('pst')
    if val is not None:
        return val
    addon = bpy.context.preferences.addons.get(__package__)
    p = getattr(addon, "preferences", None) if addon else None
    if p is None:
        val = 0
    else:
        raw = getattr(p, "pulse_style", 'SMOOTH')
        val = 0 if raw == 'SMOOTH' else (2 if raw == 'BREATH' else 1)
    _frame_cache['pst'] = val
    return val


def get_breath_alpha(time_sec):
    if get_pulse_style() != 2:
        return 1.0
    return 0.12 + 0.88 * (0.5 + 0.5 * sin(time_sec * 2.4))


def get_adaptive_handle_offset(x1, y1, x2, y2, curv_factor):
    dx = abs(x2 - x1)
    dy = abs(y2 - y1)
    slope = 0.0 if dx == 0 else dy / dx
    raw_curving = curv_factor * 10.0
    clamp_factor = min(1.0, slope * (4.5 - 0.25 * raw_curving))
    return (curv_factor * 10.0) * 0.1 * dx * clamp_factor


def dpi_fac():
    val = _frame_cache.get('dpi')
    if val is not None:
        return val
    val = bpy.context.preferences.system.dpi / 72
    _frame_cache['dpi'] = val
    return val


def _view_to_region_float(v2d, x, y):
    transform = _frame_cache.get('v2rf')
    if transform is None:
        vx0, vy0 = v2d.region_to_view(0.0, 0.0)
        vx1, vy1 = v2d.region_to_view(1.0, 1.0)
        transform = (vx0, vy0, 1.0 / (vx1 - vx0), 1.0 / (vy1 - vy0))
        _frame_cache['v2rf'] = transform
    vx0, vy0, sx, sy = transform
    return ((x - vx0) * sx, (y - vy0) * sy)


def _clip_segment_to_rect(p0, p1, rect):
    xmin, ymin, xmax, ymax = rect
    x0, y0 = p0
    x1, y1 = p1
    dx = x1 - x0
    dy = y1 - y0
    t0 = 0.0
    t1 = 1.0

    for p, q in ((-dx, x0 - xmin), (dx, xmax - x0), (-dy, y0 - ymin), (dy, ymax - y0)):
        if abs(p) < 1e-8:
            if q < 0.0:
                return None
            continue
        r = q / p
        if p < 0.0:
            if r > t1:
                return None
            if r > t0:
                t0 = r
        else:
            if r < t0:
                return None
            if r < t1:
                t1 = r

    return ((x0 + dx * t0, y0 + dy * t0), (x0 + dx * t1, y0 + dy * t1))


def _find_visible_invalid_center(start, end, ctrl, region_width, region_height, radius, region_padding):
    center = ((start[0] + end[0]) * 0.5, (start[1] + end[1]) * 0.5)

    inner_pad = region_padding + radius
    inner_rect = (inner_pad, inner_pad, region_width - inner_pad, region_height - inner_pad)
    if inner_rect[0] <= center[0] <= inner_rect[2] and inner_rect[1] <= center[1] <= inner_rect[3]:
        return center

    outer_rect = (-radius, -radius, region_width + radius, region_height + radius)
    required_socket_distance = radius * 2.0
    link_points = []
    p0, p1, p2, p3 = ctrl
    for a, b, c, d in _BEZIER_COEFFS:
        link_points.append((
            a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
            a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1],
        ))

    best_pos = None
    best_cost = 0.0
    for i in range(len(link_points) - 1):
        clipped = _clip_segment_to_rect(link_points[i], link_points[i + 1], outer_rect)
        if clipped is None:
            continue
        seg0, seg1 = clipped
        length = hypot(seg1[0] - seg0[0], seg1[1] - seg0[1])
        points_to_check = max(2, 1 + int(length))
        for j in range(points_to_check):
            t = j / (points_to_check - 1)
            px = seg0[0] + (seg1[0] - seg0[0]) * t
            py = seg0[1] + (seg1[1] - seg0[1]) * t
            dist_x = 0.0 if inner_rect[0] <= px <= inner_rect[2] else min(abs(px - inner_rect[0]), abs(px - inner_rect[2]))
            dist_y = 0.0 if inner_rect[1] <= py <= inner_rect[3] else min(abs(py - inner_rect[1]), abs(py - inner_rect[3]))
            distance_to_inner_rect = max(dist_x, dist_y)
            distance_to_center = hypot(px - center[0], py - center[1])
            distance_to_socket = min(hypot(px - start[0], py - start[1]), hypot(px - end[0], py - end[1]))
            if distance_to_socket < required_socket_distance:
                cost = 1e5 + distance_to_center
            else:
                cost = distance_to_center ** 0.5 + 10.0 * distance_to_inner_rect
            if best_pos is None or cost < best_cost:
                best_cost = cost
                best_pos = (px, py)

    return best_pos if best_pos is not None else center


def abs_node_location(node):
    """Return (x, y) in view space of the top-left of the node, taking parent frames into account."""
    cache = _frame_cache.get('nlc')
    if cache is not None:
        cached = cache.get(node.as_pointer())
        if cached is not None:
            return cached
    if node.parent is None:
        result = node.location.copy()
    else:
        result = node.location + abs_node_location(node.parent)
    if cache is None:
        cache = {}
        _frame_cache['nlc'] = cache
    cache[node.as_pointer()] = result.copy()
    return result



def get_shader(name):
    shader = _SHADER_CACHE.get(name)
    if shader is not None:
        return shader

    info = gpu.types.GPUShaderCreateInfo()
    info.push_constant('MAT4', 'ModelViewProjectionMatrix')

    if name == 'RAINBOW':
        iface = gpu.types.GPUStageInterfaceInfo("rainbow_noodle_iface")
        iface.smooth('VEC2', 'v_uv')
        iface.smooth('VEC2', 'v_pos')
        iface.smooth('VEC2', 'v_invalid_center')
        info.vertex_in(0, 'VEC2', 'pos')
        info.vertex_in(1, 'VEC2', 'uv')
        info.vertex_in(2, 'VEC2', 'invalid_center')
        info.vertex_out(iface)
        occ_limit = _LINK_OCCLUDER_LIMIT
        info.typedef_source(f'''
            struct NodeWranglerLinkUBO {{
                vec4 occluders[{occ_limit}];
                vec4 meta;
            }};
            struct RainbowParamsUBO {{
                vec4 anim;
                vec4 style;
                vec4 geom;
            }};
        ''')
        info.uniform_buf(0, 'NodeWranglerLinkUBO', 'u_params')
        info.uniform_buf(1, 'RainbowParamsUBO', 'u_rainbow')
        info.fragment_out(0, 'VEC4', 'fragColor')
        info.vertex_source("""
            void main() {
                gl_Position = ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
                v_uv = uv;
                v_pos = pos;
                v_invalid_center = invalid_center;
            }
        """)
        info.fragment_source("""
            vec3 hsv2rgb(vec3 c) {
                vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
                vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
            }

            vec3 gradient4(float cycle, float saturation, float value) {
                float pos = fract(cycle) * 4.0;
                float idx = floor(pos);
                float blend_t = smoothstep(0.0, 1.0, fract(pos));
                vec3 c0 = hsv2rgb(vec3(fract(u_rainbow.anim.z), saturation, value));
                vec3 c1 = hsv2rgb(vec3(fract(u_rainbow.anim.z + u_rainbow.anim.w * 0.25), saturation, value));
                vec3 c2 = hsv2rgb(vec3(fract(u_rainbow.anim.z + u_rainbow.anim.w * 0.50), saturation, value));
                vec3 c3 = hsv2rgb(vec3(fract(u_rainbow.anim.z + u_rainbow.anim.w * 0.75), saturation, value));
                if (idx < 1.0) {
                    return mix(c0, c1, blend_t);
                }
                if (idx < 2.0) {
                    return mix(c1, c2, blend_t);
                }
                if (idx < 3.0) {
                    return mix(c2, c3, blend_t);
                }
                return mix(c3, c0, blend_t);
            }

            float sdRoundBox(vec2 p, vec2 b, float r) {
                vec2 q = abs(p) - b + r;
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }

            void main() {
                float u_time = u_rainbow.anim.x;
                float u_alpha = u_rainbow.anim.y;
                float u_saturation = u_rainbow.style.x;
                float u_value = u_rainbow.style.y;
                float u_pulse_style = u_rainbow.style.z;
                float u_invalid = u_rainbow.style.w;
                float u_zoom = u_rainbow.geom.x;
                float u_dot_spacing = u_rainbow.geom.y;
                vec2 u_invalid_box_half = vec2(u_rainbow.geom.z);
                float u_invalid_box_radius = u_rainbow.geom.w;
                float v_progress = v_uv.x;
                float v_side = v_uv.y;
                vec2 screen_pos = v_pos;
                float t = u_time * 0.5;
                float flow_phase = t * 3.0 - v_progress * 4.0;
                float cycle = fract(flow_phase / 6.28318530718);
                float saturation = u_saturation;
                float value = u_value;
                float pulse = 0.0;
                if (u_pulse_style < 0.5) {
                    /* 平滑脉冲 — 原有效果 */
                    float pulse_t = u_time * 0.8;
                    float pulse_center = fract(pulse_t);
                    float raw_dist = fract(pulse_center - v_progress);
                    if (raw_dist > 0.5) {
                        float front_dist = 1.0 - raw_dist;
                        pulse = exp(-(front_dist * front_dist) / (2.0 * 0.01 * 0.01));
                    } else {
                        pulse = exp(-(raw_dist * raw_dist) / (2.0 * 0.2 * 0.2));
                    }
                } else if (u_pulse_style < 1.5) {
                    /* 点状脉冲 — 归一化网格 + 像素尺寸 */
                    float dot_spacing = u_dot_spacing;
                    float dot_phase = fract(u_time * 0.2925);
                    float dot_center = floor(v_progress / dot_spacing + 0.5) * dot_spacing;

                    float along_px = (v_progress - dot_center) / max(fwidth(v_progress), 1e-6);
                    float side_px = v_side / max(fwidth(v_side), 1e-6);
                    float dot_radius = 4.0 * u_zoom;
                    float sdf = length(vec2(along_px, side_px)) - dot_radius;
                    float dot_shape = 1.0 - smoothstep(0.0, max(0.5, u_zoom), sdf);

                    /* 拖尾亮度 — 归一化空间连续 */
                    float trail_count = 5.0;
                    float trail_width = trail_count * dot_spacing;
                    float dot_dist = fract(dot_phase - dot_center);
                    float t = dot_dist / trail_width;
                    float brightness = t < 1.0 ? 1.0 - t * t * t : 0.0;

                    float dot_alpha = dot_shape * brightness;
                    pulse = dot_alpha;
                }
                float flow_value = max(value, 0.35);
                float boost = 1.0 + 3.0 * pulse;
                float sat_damp = 1.0 - 0.95 * pulse;
                vec3 base_rgb = gradient4(cycle, saturation, value);
                vec3 flow_rgb = gradient4(cycle, saturation * sat_damp, min(1.0, flow_value * boost));
                vec3 rgb = mix(base_rgb, flow_rgb, pulse);
                if (u_invalid > 0.0) {
                    vec3 red_base = vec3(0.8, 0.0, 0.0) * value;
                    vec3 red_flow = mix(vec3(0.8, 0.0, 0.0) * flow_value, vec3(1.0, 0.6, 0.6), pulse);
                    red_base = mix(red_base, red_flow, pulse);
                    rgb = mix(rgb, red_base, u_invalid);
                }
                float edge = abs(v_side);
                float alpha_edge = 1.0 - smoothstep(0.85, 1.0, edge);
                if (u_invalid > 0.0) {
                    vec2 invalid_hole_uv = screen_pos - v_invalid_center;
                    float invalid_hole_sdf = sdRoundBox(
                        invalid_hole_uv, u_invalid_box_half, u_invalid_box_radius);
                    float invalid_hole_soft = max(fwidth(invalid_hole_sdf), 0.75);
                    float invalid_hole_mask = smoothstep(0.0, invalid_hole_soft, invalid_hole_sdf);
                    alpha_edge *= invalid_hole_mask;
                }
                float delta = 1.5 * fwidth(edge);
                float hl_w = delta * 0.25 + 0.16;
                float hl = 0.0;
                if (hl_w > 0.10) {
                    hl = smoothstep(1.0 - hl_w - delta, 1.0 - hl_w, edge)
                         * (1.0 - smoothstep(1.0, 1.0 + delta, edge));
                }
                rgb = mix(rgb, vec3(1.0), hl * 0.35);

                int occ_count = int(u_params.meta.x + 0.5);
                for (int i = 0; i < """ + str(_LINK_OCCLUDER_LIMIT) + """; i++) {
                    if (i >= occ_count) {
                        break;
                    }
                    vec2 oc = u_params.occluders[i].xy;
                    vec2 oh = u_params.occluders[i].zw;
                    if (oh.y < 0.0) {
                        float sr = -oh.y;
                        if (sdRoundBox(screen_pos - oc, vec2(oh.x, sr), sr) < 0.0) {
                            discard;
                        }
                    } else {
                        float r = min(u_params.meta.y, min(oh.x, oh.y));
                        if (sdRoundBox(screen_pos - oc, oh, r) < 0.0) {
                            discard;
                        }
                    }
                }

                fragColor = vec4(rgb, u_alpha * alpha_edge);
            }
        """)
    elif name == 'RAINBOW_INSTANCED':
        iface = gpu.types.GPUStageInterfaceInfo("rainbow_noodle_instanced_iface")
        iface.smooth('VEC2', 'v_uv')
        iface.smooth('VEC2', 'v_pos')
        iface.smooth('VEC2', 'v_invalid_center')
        iface.flat('FLOAT', 'v_link_id')
        iface.flat('VEC2', 'v_endpoint_shape')
        info.vertex_in(0, 'FLOAT', 'a_t')
        info.vertex_in(1, 'FLOAT', 'a_side')
        info.vertex_out(iface)
        info.push_constant('FLOAT', 'u_time')
        info.push_constant('FLOAT', 'u_alpha')
        info.push_constant('FLOAT', 'u_hue_start')
        info.push_constant('FLOAT', 'u_hue_range')
        info.push_constant('FLOAT', 'u_saturation')
        info.push_constant('FLOAT', 'u_value')
        info.push_constant('FLOAT', 'u_pulse_style')
        info.push_constant('FLOAT', 'u_zoom')
        info.push_constant('FLOAT', 'u_half_width')
        info.push_constant('VEC2', 'u_invalid_box_half')
        info.push_constant('FLOAT', 'u_invalid_box_radius')
        _bg = _BATCH_GROUP_SIZE
        _ol = _BATCH_OCC_LIMIT
        info.typedef_source(f"""
            struct MultiLinkUBO {{
                vec4 link_meta[{_bg}];
                vec4 all_occluders[{_bg * _ol}];
            }};
            struct LinkGeomUBO {{
                vec4 ctrl[{_bg * 2}];
                vec4 invalid_center[{_bg}];
            }};
        """)
        info.uniform_buf(0, 'MultiLinkUBO', 'u_params')
        info.uniform_buf(1, 'LinkGeomUBO', 'u_geom')
        info.fragment_out(0, 'VEC4', 'fragColor')
        info.vertex_source("""
            vec2 bezier(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) {
                float mt = 1.0 - t;
                float a = mt * mt * mt;
                float b = 3.0 * mt * mt * t;
                float c = 3.0 * mt * t * t;
                float d = t * t * t;
                return a * p0 + b * p1 + c * p2 + d * p3;
            }

            void main() {
                int lid = gl_InstanceID;
                vec4 c01 = u_geom.ctrl[lid * 2];
                vec4 c23 = u_geom.ctrl[lid * 2 + 1];
                vec2 p0 = c01.xy;
                vec2 p1 = c01.zw;
                vec2 p2 = c23.xy;
                vec2 p3 = c23.zw;
                vec4 invalid_data = u_geom.invalid_center[lid];
                vec2 invalid_center = invalid_data.xy;

                float t = a_t;
                vec2 center = bezier(p0, p1, p2, p3, t);
                float dt = 0.001;
                float t0 = max(0.0, t - dt);
                float t1 = min(1.0, t + dt);
                vec2 pa = bezier(p0, p1, p2, p3, t0);
                vec2 pb = bezier(p0, p1, p2, p3, t1);
                vec2 tangent = pb - pa;
                float tl = length(tangent);
                vec2 nrm = tl > 1e-6 ? vec2(-tangent.y, tangent.x) / tl : vec2(0.0, 1.0);

                vec2 pos = center + nrm * (a_side * u_half_width);
                gl_Position = ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
                v_uv = vec2(t, a_side);
                v_pos = pos;
                v_invalid_center = invalid_center;
                v_link_id = float(lid);
                v_endpoint_shape = invalid_data.zw;
            }
        """)
        occ_limit_inst = _ol
        info.fragment_source(f"""
            vec3 hsv2rgb(vec3 c) {{
                vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
                vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
            }}

            vec3 gradient4(float cycle, float saturation, float value) {{
                float pos = fract(cycle) * 4.0;
                float idx = floor(pos);
                float blend_t = smoothstep(0.0, 1.0, fract(pos));
                vec3 c0 = hsv2rgb(vec3(fract(u_hue_start), saturation, value));
                vec3 c1 = hsv2rgb(vec3(fract(u_hue_start + u_hue_range * 0.25), saturation, value));
                vec3 c2 = hsv2rgb(vec3(fract(u_hue_start + u_hue_range * 0.50), saturation, value));
                vec3 c3 = hsv2rgb(vec3(fract(u_hue_start + u_hue_range * 0.75), saturation, value));
                if (idx < 1.0) {{
                    return mix(c0, c1, blend_t);
                }}
                if (idx < 2.0) {{
                    return mix(c1, c2, blend_t);
                }}
                if (idx < 3.0) {{
                    return mix(c2, c3, blend_t);
                }}
                return mix(c3, c0, blend_t);
            }}

            float sdRoundBox(vec2 p, vec2 b, float r) {{
                vec2 q = abs(p) - b + r;
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }}

            float sdEndpointShape(vec2 p, float radius, float sid) {{
                vec2 q = p / max(radius, 0.001);
                int shape = int(sid + 0.5);
                if (shape == 1 || shape == 4 || shape == 7) {{
                    return sdRoundBox(q, vec2(0.82, 0.82), 0.12) * radius;
                }}
                if (shape == 2 || shape == 5) {{
                    return (abs(q.x) + abs(q.y) - 1.0) * 0.70710678 * radius;
                }}
                if (shape == 6) {{
                    vec2 d = abs(q) - vec2(0.22, 0.88);
                    return (min(max(d.x, d.y), 0.0) + length(max(d, 0.0))) * radius;
                }}
                if (shape == 8) {{
                    return sdRoundBox(q, vec2(0.62, 0.96), 0.14) * radius;
                }}
                return length(p) - radius;
            }}

            void main() {{
                int link_id = int(v_link_id + 0.5);
                float v_progress = v_uv.x;
                float v_side = v_uv.y;
                vec2 screen_pos = v_pos;
                float t = u_time * 0.5;
                float flow_phase = t * 3.0 - v_progress * 4.0;
                float cycle = fract(flow_phase / 6.28318530718);
                float saturation = u_saturation;
                float value = u_value;
                float pulse = 0.0;
                if (u_pulse_style < 0.5) {{
                    float pulse_t = u_time * 0.8;
                    float pulse_center = fract(pulse_t);
                    float raw_dist = fract(pulse_center - v_progress);
                    if (raw_dist > 0.5) {{
                        float front_dist = 1.0 - raw_dist;
                        pulse = exp(-(front_dist * front_dist) / (2.0 * 0.01 * 0.01));
                    }} else {{
                        pulse = exp(-(raw_dist * raw_dist) / (2.0 * 0.2 * 0.2));
                    }}
                }} else if (u_pulse_style < 1.5) {{
                    float dot_count = max(1.0, u_params.link_meta[link_id].w);
                    float dot_spacing = 1.0 / dot_count;
                    float dot_phase = fract(u_time * 0.2925);
                    float dot_center = floor(v_progress / dot_spacing + 0.5) * dot_spacing;

                    float along_px = (v_progress - dot_center) / max(fwidth(v_progress), 1e-6);
                    float side_px = v_side / max(fwidth(v_side), 1e-6);
                    float dot_radius = 4.0 * u_zoom;
                    float sdf = length(vec2(along_px, side_px)) - dot_radius;
                    float dot_shape = 1.0 - smoothstep(0.0, max(0.5, u_zoom), sdf);

                    float trail_count = 5.0;
                    float trail_width = trail_count * dot_spacing;
                    float dot_dist = fract(dot_phase - dot_center);
                    float t = dot_dist / trail_width;
                    float brightness = t < 1.0 ? 1.0 - t * t * t : 0.0;

                    float dot_alpha = dot_shape * brightness;
                    pulse = dot_alpha;
                }}
                float flow_value = max(value, 0.35);
                float boost = 1.0 + 3.0 * pulse;
                float sat_damp = 1.0 - 0.95 * pulse;
                vec3 base_rgb = gradient4(cycle, saturation, value);
                vec3 flow_rgb = gradient4(cycle, saturation * sat_damp, min(1.0, flow_value * boost));
                vec3 rgb = mix(base_rgb, flow_rgb, pulse);
                float u_invalid = u_params.link_meta[link_id].z;
                if (u_invalid > 0.0) {{
                    vec3 red_base = vec3(0.8, 0.0, 0.0) * value;
                    vec3 red_flow = mix(vec3(0.8, 0.0, 0.0) * flow_value, vec3(1.0, 0.6, 0.6), pulse);
                    red_base = mix(red_base, red_flow, pulse);
                    rgb = mix(rgb, red_base, u_invalid);
                }}
                float edge = abs(v_side);
                float alpha_edge = 1.0 - smoothstep(0.85, 1.0, edge);
                vec4 c01 = u_geom.ctrl[link_id * 2];
                vec4 c23 = u_geom.ctrl[link_id * 2 + 1];
                float endpoint_radius = u_invalid_box_half.y;
                if (v_endpoint_shape.x > 0.5) {{
                    float start_dist = sdEndpointShape(
                        screen_pos - c01.xy, endpoint_radius, v_endpoint_shape.x - 1.0);
                    float start_aa = max(fwidth(start_dist), 0.75);
                    alpha_edge *= smoothstep(-start_aa, start_aa, start_dist);
                }}
                if (v_endpoint_shape.y > 0.5) {{
                    float end_dist = sdEndpointShape(
                        screen_pos - c23.zw, endpoint_radius, v_endpoint_shape.y - 1.0);
                    float end_aa = max(fwidth(end_dist), 0.75);
                    alpha_edge *= smoothstep(-end_aa, end_aa, end_dist);
                }}
                if (u_invalid > 0.0) {{
                    vec2 invalid_hole_uv = screen_pos - v_invalid_center;
                    float invalid_hole_sdf = sdRoundBox(
                        invalid_hole_uv, vec2(u_invalid_box_half.x), u_invalid_box_radius);
                    float invalid_hole_soft = max(fwidth(invalid_hole_sdf), 0.75);
                    float invalid_hole_mask = smoothstep(0.0, invalid_hole_soft, invalid_hole_sdf);
                    alpha_edge *= invalid_hole_mask;
                }}
                float delta = 1.5 * fwidth(edge);
                float hl_w = delta * 0.25 + 0.16;
                float hl = 0.0;
                if (hl_w > 0.10) {{
                    hl = smoothstep(1.0 - hl_w - delta, 1.0 - hl_w, edge)
                         * (1.0 - smoothstep(1.0, 1.0 + delta, edge));
                }}
                rgb = mix(rgb, vec3(1.0), hl * 0.35);

                int occ_count = int(u_params.link_meta[link_id].x + 0.5);
                int occ_base = link_id * {occ_limit_inst};
                for (int j = 0; j < {occ_limit_inst}; j++) {{
                    if (j >= occ_count) {{
                        break;
                    }}
                    vec2 oc = u_params.all_occluders[occ_base + j].xy;
                    vec2 oh = u_params.all_occluders[occ_base + j].zw;
                    if (oh.y < 0.0) {{
                        float sr = -oh.y;
                        if (sdRoundBox(screen_pos - oc, vec2(oh.x, sr), sr) < 0.0) {{
                            discard;
                        }}
                    }} else {{
                        float r = min(u_params.link_meta[link_id].y, min(oh.x, oh.y));
                        if (sdRoundBox(screen_pos - oc, oh, r) < 0.0) {{
                            discard;
                        }}
                    }}
                }}

                fragColor = vec4(rgb, u_alpha * alpha_edge);
            }}
        """)
    elif name == 'SMOOTH_COLOR':
        iface = gpu.types.GPUStageInterfaceInfo("rainbow_noodle_smooth_iface")
        iface.smooth('VEC2', 'v_uv')
        iface.smooth('VEC2', 'v_pos')
        info.vertex_in(0, 'VEC2', 'pos')
        info.vertex_in(1, 'VEC2', 'uv')
        info.vertex_out(iface)
        info.push_constant('VEC4', 'color')
        occ_limit = _LINK_OCCLUDER_LIMIT
        info.typedef_source(f'''
            struct NodeWranglerLinkUBO {{
                vec4 occluders[{occ_limit}];
                vec4 meta;
            }};
        ''')
        info.uniform_buf(0, 'NodeWranglerLinkUBO', 'u_params')
        info.fragment_out(0, 'VEC4', 'fragColor')
        info.vertex_source("""
            void main() {
                gl_Position = ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
                v_uv = uv;
                v_pos = pos;
            }
        """)
        info.fragment_source("""
            float sdRoundBox(vec2 p, vec2 b, float r) {
                vec2 q = abs(p) - b + r;
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }

            void main() {
                int occ_count = int(u_params.meta.x + 0.5);
                for (int i = 0; i < """ + str(_LINK_OCCLUDER_LIMIT) + """; i++) {
                    if (i >= occ_count) {
                        break;
                    }
                    vec2 oc = u_params.occluders[i].xy;
                    vec2 oh = u_params.occluders[i].zw;
                    if (oh.y < 0.0) {
                        float sr = -oh.y;
                        if (sdRoundBox(v_pos - oc, vec2(oh.x, sr), sr) < 0.0) {
                            discard;
                        }
                    } else {
                        float r = min(u_params.meta.y, min(oh.x, oh.y));
                        if (sdRoundBox(v_pos - oc, oh, r) < 0.0) {
                            discard;
                        }
                    }
                }
                float alpha_edge = 1.0 - smoothstep(0.85, 1.0, abs(v_uv.y));
                fragColor = vec4(color.rgb, color.a * alpha_edge);
            }
        """)
    elif name == 'SMOOTH_COLOR_BATCH':
        iface = gpu.types.GPUStageInterfaceInfo("rainbow_noodle_smooth_batch_iface")
        iface.smooth('VEC2', 'v_uv')
        iface.smooth('VEC2', 'v_pos')
        iface.smooth('FLOAT', 'v_link_id')
        info.vertex_in(0, 'VEC2', 'pos')
        info.vertex_in(1, 'VEC2', 'uv')
        info.vertex_in(2, 'FLOAT', 'link_id')
        info.vertex_out(iface)
        info.push_constant('VEC4', 'color')
        _bg = _BATCH_GROUP_SIZE
        _ol = _BATCH_OCC_LIMIT
        info.typedef_source(f'''
            struct MultiLinkUBO {{
                vec4 link_meta[{_bg}];
                vec4 all_occluders[{_bg * _ol}];
            }};
        ''')
        info.uniform_buf(0, 'MultiLinkUBO', 'u_params')
        info.fragment_out(0, 'VEC4', 'fragColor')
        info.vertex_source("""
            void main() {
                gl_Position = ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
                v_uv = uv;
                v_pos = pos;
                v_link_id = link_id;
            }
        """)
        occ_limit_batch = _ol
        info.fragment_source(f"""
            float sdRoundBox(vec2 p, vec2 b, float r) {{
                vec2 q = abs(p) - b + r;
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }}

            void main() {{
                int link_id = int(v_link_id + 0.5);
                int occ_count = int(u_params.link_meta[link_id].x + 0.5);
                int occ_base = link_id * {occ_limit_batch};
                for (int j = 0; j < {occ_limit_batch}; j++) {{
                    if (j >= occ_count) {{
                        break;
                    }}
                    vec2 oc = u_params.all_occluders[occ_base + j].xy;
                    vec2 oh = u_params.all_occluders[occ_base + j].zw;
                    if (oh.y < 0.0) {{
                        float sr = -oh.y;
                        if (sdRoundBox(v_pos - oc, vec2(oh.x, sr), sr) < 0.0) {{
                            discard;
                        }}
                    }} else {{
                        float r = min(u_params.link_meta[link_id].y, min(oh.x, oh.y));
                        if (sdRoundBox(v_pos - oc, oh, r) < 0.0) {{
                            discard;
                        }}
                    }}
                }}
                float alpha_edge = 1.0 - smoothstep(0.85, 1.0, abs(v_uv.y));
                fragColor = vec4(color.rgb, color.a * alpha_edge);
            }}
        """)
    elif name == 'SDF_CIRCLE':
        iface = gpu.types.GPUStageInterfaceInfo("rainbow_noodle_sdf_circle_iface")
        iface.smooth('VEC2', 'v_uv')
        info.vertex_in(0, 'VEC2', 'pos')
        info.vertex_in(1, 'VEC2', 'uv')
        info.vertex_out(iface)
        info.push_constant('VEC4', 'color')
        info.fragment_out(0, 'VEC4', 'fragColor')
        info.vertex_source("""
            void main() {
                gl_Position = ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
                v_uv = uv;
            }
        """)
        info.fragment_source("""
            void main() {
                float dist = length(v_uv) - 1.0;
                float delta = max(0.001, 1.5 * fwidth(dist));
                float alpha = 1.0 - smoothstep(0.0, delta, dist);
                fragColor = vec4(color.rgb, color.a * alpha);
            }
        """)
    elif name == 'RAINBOW_BOX_OUTLINE':
        iface = gpu.types.GPUStageInterfaceInfo("rainbow_noodle_rainbow_box_outline_iface")
        iface.smooth('VEC2', 'v_uv')
        info.vertex_in(0, 'VEC2', 'pos')
        info.vertex_in(1, 'VEC2', 'uv')
        info.vertex_out(iface)
        info.push_constant('FLOAT', 'u_time')
        info.push_constant('FLOAT', 'u_alpha')
        info.push_constant('VEC2', 'rect_half_size')
        info.push_constant('FLOAT', 'radius')
        info.push_constant('FLOAT', 'u_occ_radius')
        info.push_constant('FLOAT', 'stroke_width')
        info.push_constant('FLOAT', 'u_hue_start')
        info.push_constant('FLOAT', 'u_hue_range')
        info.push_constant('FLOAT', 'u_saturation')
        info.push_constant('FLOAT', 'u_value')
        info.push_constant('FLOAT', 'u_pulse_style')
        gap_limit = _RAINBOW_GAP_LIMIT
        occ_limit = _OUTLINE_OCCLUDER_LIMIT
        info.typedef_source(f'''
            struct NodeWranglerRainbowOutlineUBO {{
                vec4 gaps[{gap_limit}];
                vec4 occluders[{occ_limit}];
                vec4 meta;
            }};
        ''')
        info.uniform_buf(0, 'NodeWranglerRainbowOutlineUBO', 'u_params')
        info.fragment_out(0, 'VEC4', 'fragColor')
        info.vertex_source("""
            void main() {
                gl_Position = ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
                v_uv = uv;
            }
        """)
        info.fragment_source(f"""
            float sdRoundBox(vec2 p, vec2 b, float r) {{
                vec2 q = abs(p) - b + r;
                return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
            }}

            float sdSocketShapeNorm(vec2 p, float sid) {{
                int shape = int(sid + 0.5);
                if (shape == 1 || shape == 4 || shape == 7) {{
                    return sdRoundBox(p, vec2(0.82, 0.82), 0.12);
                }}
                if (shape == 2 || shape == 5) {{
                    return (abs(p.x) + abs(p.y) - 1.0) * 0.70710678;
                }}
                if (shape == 6) {{
                    vec2 d = abs(p) - vec2(0.18, 0.84);
                    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
                }}
                if (shape == 8) {{
                    return sdRoundBox(p, vec2(0.62, 0.96), 0.14);
                }}
                return length(p) - 1.0;
            }}

            float sdStraightBox(vec2 p, vec2 b) {{
                vec2 d = abs(p) - b;
                return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
            }}

            float sdSocketGap(vec2 p, float sid, float radius_px, float scale, float outline_width) {{
                if (sid < 0.0) {{
                    return sdStraightBox(p, vec2(-sid, scale));
                }}
                int shape = int(sid + 0.5);
                if (shape == 6) {{
                    float s = scale > 0.0 ? scale : 1.0;
                    float half_x = clamp(outline_width * 1.05 + 1.5,
                                         radius_px * 0.72 * s,
                                         radius_px * 1.15 * s);
                    float half_y = clamp(outline_width * 1.20 + 4.0,
                                         radius_px * 1.10 * s,
                                         radius_px * 1.55 * s);
                    vec2 half_size = vec2(half_x, half_y);
                    return sdStraightBox(p, half_size);
                }}
                float r = radius_px * (scale > 0.0 ? scale : 1.0);
                return sdSocketShapeNorm(p / max(r, 0.001), sid) * r;
            }}

            vec3 hsv2rgb(vec3 c) {{
                vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
                vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
            }}

            vec3 gradient4(float cycle, float saturation, float value) {{
                float pos = fract(cycle) * 4.0;
                float idx = floor(pos);
                float blend_t = smoothstep(0.0, 1.0, fract(pos));
                vec3 c0 = hsv2rgb(vec3(fract(u_hue_start), saturation, value));
                vec3 c1 = hsv2rgb(vec3(fract(u_hue_start + u_hue_range * 0.25), saturation, value));
                vec3 c2 = hsv2rgb(vec3(fract(u_hue_start + u_hue_range * 0.50), saturation, value));
                vec3 c3 = hsv2rgb(vec3(fract(u_hue_start + u_hue_range * 0.75), saturation, value));
                if (idx < 1.0) {{
                    return mix(c0, c1, blend_t);
                }}
                if (idx < 2.0) {{
                    return mix(c1, c2, blend_t);
                }}
                if (idx < 3.0) {{
                    return mix(c2, c3, blend_t);
                }}
                return mix(c3, c0, blend_t);
            }}

            void main() {{
                float uv_len = length(v_uv);
                vec2 dir = uv_len > 1e-6 ? (v_uv / uv_len) : vec2(1.0, 0.0);

                float ang = atan(dir.y, dir.x);
                float cycle = fract(ang / 6.28318530718 + u_time * 0.167);

                float pulse = 0.0;
                if (u_pulse_style < 1.5) {{
                    float pulse_ang = fract(u_time * 0.49) * 6.28318530718;
                    vec2 pulse_dir = vec2(cos(pulse_ang), sin(pulse_ang));
                    float dp = clamp(dot(dir, pulse_dir), -1.0, 1.0);
                    float pulse_dist = acos(dp);
                    pulse = exp(-(pulse_dist * pulse_dist) / (2.0 * 0.4 * 0.4));
                }}

                float sat_damp = 1.0 - 0.85 * pulse;
                float val_boost = min(1.0, u_value * (1.0 + 1.8 * pulse));
                vec3 rgb = gradient4(cycle, u_saturation * sat_damp, val_boost);

                bool is_circle = rect_half_size.x < 0.0;
                vec2 rect_half_abs = vec2(abs(rect_half_size.x), abs(rect_half_size.y));
                float sdf_dist = is_circle ? (length(v_uv) - rect_half_abs.x) :
                                             sdRoundBox(v_uv, rect_half_abs, radius);
                float delta = 1.5 * fwidth(sdf_dist);
                float d = abs(sdf_dist);
                float alpha_shape = 1.0 - smoothstep(stroke_width, stroke_width + delta, d);

                float outer_hl_w = delta * 0.25 + stroke_width * 0.20;
                float inner_hl_w = max(delta * 1.10, min(outer_hl_w * 0.42, 1.25));
                float outer_hl = 0.0;
                float inner_hl = 0.0;
                if (outer_hl_w > 0.10) {{
                    outer_hl = smoothstep(stroke_width - outer_hl_w - delta, stroke_width - outer_hl_w, sdf_dist)
                             * (1.0 - smoothstep(stroke_width, stroke_width + delta, sdf_dist));
                    float inner_dist = -sdf_dist;
                    inner_hl = smoothstep(stroke_width - inner_hl_w - delta, stroke_width - inner_hl_w, inner_dist)
                             * (1.0 - smoothstep(stroke_width, stroke_width + delta, inner_dist));

                    if (!is_circle) {{
                        vec2 corner_uv = abs(v_uv) - (rect_half_abs - vec2(radius));
                        float corner_area = smoothstep(0.0, max(radius * 0.18, 1.0), min(corner_uv.x, corner_uv.y));
                        inner_hl *= mix(1.0, 0.45, corner_area);
                    }}
                }}
                rgb = mix(rgb, vec3(1.0), outer_hl * 0.35 + inner_hl * 0.14);

                int gap_count = int(u_params.meta.y + 0.5);
                if (gap_count > 0) {{
                    float min_gap_dist = 1e5;
                    float min_hl_gap_dist = 1e5;
                    for (int i = 0; i < {gap_limit}; i++) {{
                        if (i >= gap_count) {{
                            break;
                        }}
                        vec4 gap = u_params.gaps[i];
                        float gap_dist = sdSocketGap(v_uv - gap.xy, gap.z, u_params.meta.x, gap.w, stroke_width);
                        min_gap_dist = min(min_gap_dist, gap_dist);
                        if (gap.z >= 0.0 || gap.z < -stroke_width) {{
                            min_hl_gap_dist = min(min_hl_gap_dist, gap_dist);
                        }}
                    }}
                    float gap_mask = smoothstep(0.0, delta, min_gap_dist);
                    float gap_hl_w = max(delta * 3.4, min(stroke_width * 0.95 + 0.8, 5.0));
                    float gap_hl = gap_mask * (1.0 - smoothstep(0.0, gap_hl_w, min_hl_gap_dist));
                    rgb = mix(rgb, vec3(1.0), gap_hl * 0.58);
                    alpha_shape *= gap_mask;
                }}

                int occ_count = int(u_params.meta.z + 0.5);
                for (int i = 0; i < {occ_limit}; i++) {{
                    if (i >= occ_count) {{
                        break;
                    }}
            vec2 oc = u_params.occluders[i].xy;
            vec2 oh = u_params.occluders[i].zw;
            if (oh.x < 0.0) {{
                if (length(v_uv - oc) < -oh.x) {{
                    alpha_shape = 0.0;
                }}
            }}
            else if (oh.y < 0.0) {{
                if (sdRoundBox(v_uv - oc, vec2(oh.x, -oh.y), radius + stroke_width) < 0.0) {{
                    alpha_shape = 0.0;
                }}
            }}
            else {{
                if (sdRoundBox(v_uv - oc, oh, u_occ_radius) < 0.0) {{
                    alpha_shape = 0.0;
                }}
            }}
                }}

                fragColor = vec4(rgb, u_alpha * alpha_shape);
            }}
        """)
    else:
        return None

    try:
        shader = gpu.shader.create_from_info(info)
    except Exception as e:
        print(f"Node Wrangler Shader Error ({name}): {e}")
        return None
    _SHADER_CACHE[name] = shader
    return shader


def get_outline_ubo(data):
    global _OUTLINE_UBO
    if _OUTLINE_UBO is not None:
        try:
            _OUTLINE_UBO.update(data)
            return _OUTLINE_UBO
        except Exception:
            del _OUTLINE_UBO
            _OUTLINE_UBO = None
    _OUTLINE_UBO = gpu.types.GPUUniformBuf(data)
    return _OUTLINE_UBO


def get_link_ubo(data):
    global _LINK_UBO
    if _LINK_UBO is not None:
        try:
            _LINK_UBO.update(data)
            return _LINK_UBO
        except Exception:
            del _LINK_UBO
            _LINK_UBO = None
    _LINK_UBO = gpu.types.GPUUniformBuf(data)
    return _LINK_UBO


def get_link_geom_ubo(data):
    global _LINK_GEOM_UBO
    if _LINK_GEOM_UBO is not None:
        try:
            _LINK_GEOM_UBO.update(data)
            return _LINK_GEOM_UBO
        except Exception:
            del _LINK_GEOM_UBO
            _LINK_GEOM_UBO = None
    _LINK_GEOM_UBO = gpu.types.GPUUniformBuf(data)
    return _LINK_GEOM_UBO


def get_rainbow_ubo(data):
    global _RAINBOW_UBO
    if _RAINBOW_UBO is not None:
        try:
            _RAINBOW_UBO.update(data)
            return _RAINBOW_UBO
        except Exception:
            del _RAINBOW_UBO
            _RAINBOW_UBO = None
    _RAINBOW_UBO = gpu.types.GPUUniformBuf(data)
    return _RAINBOW_UBO


def get_instanced_batch():
    """静态单位线条 batch：a_t 进度 0..1，a_side ±1，只建一次。"""
    global _INSTANCED_BATCH
    if _INSTANCED_BATCH is not None:
        return _INSTANCED_BATCH
    shader = get_shader('RAINBOW_INSTANCED')
    if shader is None:
        return None
    n = _INSTANCED_SAMPLES
    a_t = []
    a_side = []
    inv = 1.0 / (n - 1)
    for i in range(n):
        u = i * inv
        a_t.append(u)
        a_side.append(1.0)
        a_t.append(u)
        a_side.append(-1.0)
    _INSTANCED_BATCH = batch_for_shader(shader, 'TRI_STRIP', {
        "a_t": a_t, "a_side": a_side,
    })
    return _INSTANCED_BATCH


def _curve_pixel_length(pts):
    total = 0.0
    for i in range(len(pts) - 1):
        total += hypot(pts[i+1][0] - pts[i][0], pts[i+1][1] - pts[i][1])
    return total


def _calc_dot_count(pts, zoom):
    curve_len = _curve_pixel_length(pts)
    spacing_px = 20.0 * zoom
    return max(2.0, round(curve_len / spacing_px))


def _calc_dot_count_ctrl(rp0, rp3, zoom):
    # 借鉴 Blender 源码：dot_count 只决定点脉冲疏密，用端点直线距离近似曲线长度即可，
    # 无需逐点采样贝塞尔。
    curve_len = hypot(rp3[0] - rp0[0], rp3[1] - rp0[1])
    spacing_px = 20.0 * zoom
    return max(2.0, round(curve_len / spacing_px))


def _build_group_ubo(occluders_list, invalid_list=None, dot_count_list=None):
    """构建 MultiLinkUBO 的 FLOAT buffer。

    occluders_list: list of list of (cx,cy,hx,hy,...) 每个 link 的遮挡物列表。
    invalid_list: 可选 list of float (0.0/1.0)，对应每个 link 的 invalid 状态。
    dot_count_list: 可选 list of float，对应每个 link 的点状脉冲点数。
    返回 gpu.types.Buffer 用于 GPUUniformBuf。
    """
    bg = _BATCH_GROUP_SIZE
    ol = _BATCH_OCC_LIMIT
    total = (bg + bg * ol) * 4
    floats = [0.0] * total

    for i in range(min(len(occluders_list), bg)):
        occ = occluders_list[i]
        if not occ:
            occ = []
        if len(occ) > ol:
            occ = occ[:ol]
        invalid = invalid_list[i] if invalid_list and i < len(invalid_list) else 0.0

        # link_meta[i]: x=occ_count, y=radius_px, z=invalid, w=dot_count
        mi = i * 4
        floats[mi] = float(len(occ))
        radius_px = 0.0
        for j, occ_data in enumerate(occ):
            oi = (bg + i * ol + j) * 4
            floats[oi] = occ_data[0]
            floats[oi + 1] = occ_data[1]
            floats[oi + 2] = occ_data[2]
            floats[oi + 3] = occ_data[3]
            if len(occ_data) > 4:
                radius_px = max(radius_px, occ_data[4])
        floats[mi + 1] = radius_px
        floats[mi + 2] = invalid
        floats[mi + 3] = dot_count_list[i] if dot_count_list and i < len(dot_count_list) else 16.0

    return gpu.types.Buffer('FLOAT', total, floats)


def _get_line_strip_geometry(vertices, width):
    n = len(vertices)
    if n < 2:
        return [], []

    half_w = width * 0.5
    pos_data = [None] * (n * 2)
    uv_data = _LINE_UV_CACHE.get(n)
    if uv_data is None:
        uv_data = [None] * (n * 2)
        inv = 1.0 / (n - 1)
        for i in range(n):
            u = i * inv
            base = i * 2
            uv_data[base] = (u, 1.0)
            uv_data[base + 1] = (u, -1.0)
        _LINE_UV_CACHE[n] = uv_data

    for i in range(n):
        cx, cy = vertices[i]

        if i == 0:
            dx = vertices[1][0] - cx
            dy = vertices[1][1] - cy
        elif i == n - 1:
            dx = cx - vertices[i - 1][0]
            dy = cy - vertices[i - 1][1]
        else:
            dx1 = cx - vertices[i - 1][0]
            dy1 = cy - vertices[i - 1][1]
            len1 = hypot(dx1, dy1)
            dx2 = vertices[i + 1][0] - cx
            dy2 = vertices[i + 1][1] - cy
            len2 = hypot(dx2, dy2)
            if len1 > 0 and len2 > 0:
                dx = dx1 / len1 + dx2 / len2
                dy = dy1 / len1 + dy2 / len2
            elif len1 > 0:
                dx = dx1 / len1
                dy = dy1 / len1
            elif len2 > 0:
                dx = dx2 / len2
                dy = dy2 / len2
            else:
                dx, dy = 1.0, 0.0

        # Normalize tangent → normal
        t_len = hypot(dx, dy)
        if t_len > 0:
            nx, ny = -dy / t_len, dx / t_len
        else:
            nx, ny = 0.0, 1.0

        p0x = cx + nx * half_w
        p0y = cy + ny * half_w
        p1x = cx - nx * half_w
        p1y = cy - ny * half_w
        base = i * 2
        pos_data[base] = (p0x, p0y)
        pos_data[base + 1] = (p1x, p1y)

    return pos_data, uv_data


def _bind_link_occluders(shader, occluders):
    occ_limit = _LINK_OCCLUDER_LIMIT
    occ = occluders or []
    if len(occ) > occ_limit:
        occ = occ[:occ_limit]
    ubo_floats = [0.0] * ((occ_limit + 1) * 4)
    radius_px = 0.0
    for i, occ_data in enumerate(occ):
        ocx, ocy, ohx, ohy = occ_data[:4]
        if len(occ_data) > 4:
            radius_px = max(radius_px, occ_data[4])
        base = i * 4
        ubo_floats[base] = ocx
        ubo_floats[base + 1] = ocy
        ubo_floats[base + 2] = ohx
        ubo_floats[base + 3] = ohy
    meta_base = occ_limit * 4
    ubo_floats[meta_base] = float(len(occ))
    ubo_floats[meta_base + 1] = radius_px
    ubo_buf = gpu.types.Buffer('FLOAT', len(ubo_floats), ubo_floats)
    shader.uniform_block("u_params", get_link_ubo(ubo_buf))


def draw_batch_lines(all_lines_data, shader_name, width, color=None, time_sec=0.0, alpha_mod=1.0, occluders=None, invalid=0.0, zoom=1.0, invalid_zoom=1.0, dot_spacing=0.0625):
    if not all_lines_data:
        return
    shader = get_shader(shader_name)
    if not shader:
        return

    all_pos = []
    all_uv = []
    all_invalid_centers = []
    first = True
    for vertices in all_lines_data:
        if len(vertices) < 2:
            continue
        pos, uv = _get_line_strip_geometry(vertices, width)
        if not pos:
            continue
        mid_i = len(vertices) // 2
        if len(vertices) % 2 == 0:
            center = ((vertices[mid_i - 1][0] + vertices[mid_i][0]) * 0.5,
                      (vertices[mid_i - 1][1] + vertices[mid_i][1]) * 0.5)
        else:
            center = vertices[mid_i]
        invalid_centers = [center] * len(pos)
        if not first:
            all_pos.append(all_pos[-1])
            all_pos.append(pos[0])
            all_uv.append(all_uv[-1])
            all_uv.append(uv[0])
            all_invalid_centers.append(all_invalid_centers[-1])
            all_invalid_centers.append(invalid_centers[0])
        all_pos.extend(pos)
        all_uv.extend(uv)
        all_invalid_centers.extend(invalid_centers)
        first = False

    if not all_pos:
        return

    batch_data = {"pos": all_pos, "uv": all_uv}
    if shader_name == 'RAINBOW':
        batch_data["invalid_center"] = all_invalid_centers
    batch = batch_for_shader(shader, 'TRI_STRIP', batch_data)
    shader.bind()
    if shader_name == 'RAINBOW':
        h_start, h_range, sat, val = get_gradient_params()
        ps = get_pixel_size()
        invalid_widget_unit = (round(18 * dpi_fac()) + 2 * ps) * invalid_zoom
        invalid_box_half = invalid_widget_unit * 0.5
        invalid_box_radius = invalid_widget_unit * 0.2
        rainbow_params = gpu.types.Buffer('FLOAT', 12, [
            time_sec % 1000.0, alpha_mod * get_breath_alpha(time_sec), h_start, h_range,
            sat, val, float(get_pulse_style()), invalid,
            zoom, dot_spacing, invalid_box_half, invalid_box_radius,
        ])
        shader.uniform_block("u_rainbow", get_rainbow_ubo(rainbow_params))
        _bind_link_occluders(shader, occluders)
    elif shader_name == 'SMOOTH_COLOR':
        shader.uniform_float("color", color)
        _bind_link_occluders(shader, occluders)
    else:
        shader.uniform_float("color", color)
    gpu.state.blend_set('ALPHA')
    batch.draw(shader)
    gpu.state.blend_set('NONE')


def _draw_batch_group(group_links, width_rainbow, time_sec, zoom=1.0, invalid_zoom=1.0):
    global _PERF_ACCUM_T_BATCH_UBO_MAIN, _PERF_ACCUM_T_BATCH_LDATA, _PERF_ACCUM_T_BATCH_LDRAW
    """批量绘制一组 link（最多 _BATCH_GROUP_SIZE 个）的彩虹线条。

    group_links: list of dicts with keys:
        - 'vertices': bezier 采样点 (region coords)
        - 'occ': 完整遮挡物列表
        - 'invalid': float 0/1
        - 'invalid_center': 错误标签/镂空的最终 region 坐标
        - 'start_shape': 起点 socket display_shape 编码
        - 'end_shape': 终点 socket display_shape 编码
    """
    n = len(group_links)
    if n == 0:
        return

    bg = _BATCH_GROUP_SIZE
    ol = _BATCH_OCC_LIMIT

    # ---- 构建 UBO ----
    occ_list = [gd.get('occ', []) for gd in group_links]
    invalid_list = [gd.get('invalid', 0.0) for gd in group_links]
    dot_count_list = [gd.get('dot_count', 16.0) for gd in group_links]
    # 补齐到 bg 长度（空数据）
    while len(occ_list) < bg:
        occ_list.append([])
    while len(invalid_list) < bg:
        invalid_list.append(0.0)
    while len(dot_count_list) < bg:
        dot_count_list.append(16.0)
    _perf_t_mark = time.perf_counter()
    ubo_buf = _build_group_ubo(occ_list, invalid_list, dot_count_list)
    _PERF_ACCUM_T_BATCH_UBO_MAIN += time.perf_counter() - _perf_t_mark
    gpu.state.blend_set('ALPHA')

    # ---- 绘制 rainbow lines (RAINBOW_INSTANCED) ----
    shader_rainbow = get_shader('RAINBOW_INSTANCED')
    inst_batch = get_instanced_batch()
    if shader_rainbow and inst_batch:
        _perf_t_mark = time.perf_counter()
        geom_floats = [0.0] * (bg * 3 * 4)
        for li in range(n):
            rp0, rp1, rp2, rp3 = group_links[li]['ctrl']
            base = li * 8
            geom_floats[base] = rp0[0]
            geom_floats[base + 1] = rp0[1]
            geom_floats[base + 2] = rp1[0]
            geom_floats[base + 3] = rp1[1]
            geom_floats[base + 4] = rp2[0]
            geom_floats[base + 5] = rp2[1]
            geom_floats[base + 6] = rp3[0]
            geom_floats[base + 7] = rp3[1]
            icx, icy = group_links[li].get('invalid_center', ((rp0[0] + rp3[0]) * 0.5, (rp0[1] + rp3[1]) * 0.5))
            ic_base = bg * 8 + li * 4
            geom_floats[ic_base] = icx
            geom_floats[ic_base + 1] = icy
            geom_floats[ic_base + 2] = float(group_links[li].get('start_shape', 0) + 1)
            geom_floats[ic_base + 3] = (0.0 if group_links[li].get('skip_end')
                                         else float(group_links[li].get('end_shape', 0) + 1))
        geom_buf = gpu.types.Buffer('FLOAT', bg * 3 * 4, geom_floats)
        _PERF_ACCUM_T_BATCH_LDATA += time.perf_counter() - _perf_t_mark
        _perf_t_mark = time.perf_counter()
        shader_rainbow.bind()
        h_start, h_range, sat, val = get_gradient_params()
        ps = get_pixel_size()
        invalid_widget_unit = (round(18 * dpi_fac()) + 2 * ps) * invalid_zoom
        invalid_box_half = invalid_widget_unit * 0.5
        invalid_box_radius = invalid_widget_unit * 0.2
        shader_rainbow.uniform_float("u_time", time_sec % 1000.0)
        group_alpha = (group_links[0].get('alpha', 1.0) if group_links else 1.0) * get_breath_alpha(time_sec)
        shader_rainbow.uniform_float("u_alpha", group_alpha)
        shader_rainbow.uniform_float("u_hue_start", h_start)
        shader_rainbow.uniform_float("u_hue_range", h_range)
        shader_rainbow.uniform_float("u_saturation", sat)
        shader_rainbow.uniform_float("u_value", val)
        shader_rainbow.uniform_float("u_pulse_style", float(get_pulse_style()))
        shader_rainbow.uniform_float("u_zoom", zoom)
        shader_rainbow.uniform_float("u_half_width", width_rainbow * 0.5)
        endpoint_radius_px = 0.25 * (round(18 * dpi_fac()) + 2 * ps) * invalid_zoom
        shader_rainbow.uniform_float("u_invalid_box_half", (invalid_box_half, endpoint_radius_px))
        shader_rainbow.uniform_float("u_invalid_box_radius", invalid_box_radius)
        shader_rainbow.uniform_block("u_params", get_link_ubo(ubo_buf))
        shader_rainbow.uniform_block("u_geom", get_link_geom_ubo(geom_buf))
        inst_batch.draw_instanced(shader_rainbow, instance_count=n)
        _PERF_ACCUM_T_BATCH_LDRAW += time.perf_counter() - _perf_t_mark

    gpu.state.blend_set('NONE')

def _link_occluder_rects(link, ordered_nodes, order_index, v2d, exclude_nodes=None, outline_stroke_width=0.0):
    # 盖住连线的节点 = 绘制顺序在两端节点之上、且与连线包围盒相交的节点。
    # 返回 region 像素空间的圆角矩形 (cx, cy, half_x, half_y, radius)。
    from_node = link.from_node
    to_node = link.to_node
    fp = from_node.as_pointer()
    tp = to_node.as_pointer()
    excluded = {n.as_pointer() for n in (exclude_nodes or ()) if n is not None}
    # ordered_nodes 为 front-to-back,索引越小越靠前(上层)。
    # 连线的"所属层级"取两端节点里更靠后(更下层)的那个,只有比它更靠前的才算遮挡。
    fi = order_index.get(fp, 0)
    ti = order_index.get(tp, 0)
    link_back_index = max(fi, ti)

    lb = _link_view_bounds(link)
    if lb is None:
        return []
    overlap_pad = max(1.0, 4.0 * dpi_fac())
    lb = (lb[0] - overlap_pad, lb[1] - overlap_pad, lb[2] + overlap_pad, lb[3] + overlap_pad)
    p_ref_1 = v2d.view_to_region(0.0, 0.0, clip=False)
    p_ref_2 = v2d.view_to_region(100.0, 0.0, clip=False)
    view_scale = 1.0
    if p_ref_1 and p_ref_2:
        view_scale = abs(p_ref_2[0] - p_ref_1[0]) / 100.0
    outline_pad_px = outline_stroke_width * get_pixel_size() * 0.7 * view_scale + 2.0
    ps_link = bpy.context.preferences.system.pixel_size
    widget_unit_link = round(18 * dpi_fac()) + 2 * ps_link
    sock_r = 0.25 * widget_unit_link * view_scale
    rects = []
    for idx, node in enumerate(ordered_nodes):
        if idx >= link_back_index:
            break
        if node.as_pointer() in excluded:
            continue
        if node.type == 'FRAME':
            continue
        nb = _node_draw_bounds(node)
        if not _rects_overlap(lb, nb):
            continue
        if node.select or node.type == 'REROUTE':
            c0 = v2d.view_to_region(nb[0], nb[3], clip=False)
            c1 = v2d.view_to_region(nb[2], nb[1], clip=False)
        else:
            c0 = v2d.view_to_region(nb[0] - 0.5, nb[3] + 0.5, clip=False)
            c1 = v2d.view_to_region(nb[2] + 0.5, nb[1] - 0.5, clip=False)
        if not c0 or not c1:
            continue
        cx = 0.5 * (c0[0] + c1[0])
        cy = 0.5 * (c0[1] + c1[1])
        pad = outline_pad_px if node.select else 0.0
        if node.select or node.type == 'REROUTE':
            hx = abs(c1[0] - c0[0]) * 0.5 + pad
            hy = abs(c1[1] - c0[1]) * 0.5 + pad
            r = 6.0 * view_scale + pad
            if node.type == 'REROUTE':
                cx += 2.0
                hx *= 0.88
                hy *= 0.88
                r *= 0.88
        else:
            hx = abs(c1[0] - c0[0]) * 0.5 + 1.0
            hy = abs(c1[1] - c0[1]) * 0.5 + 1.0
            cx += 1.5
            r = 6.0 * view_scale
        rects.append((cx, cy, hx, hy, r))
        for sox, is_out in ((node.inputs, False), (node.outputs, True)):
            for si, socket in enumerate(sox):
                if not socket.enabled or socket.hide:
                    continue
                if hasattr(socket, "is_icon_visible") and not socket.is_icon_visible:
                    continue
                slx, sly = get_socket_loc(node, is_out, si)
                spt = v2d.view_to_region(slx, sly, clip=False)
                if not spt:
                    continue
                rects.append((spt[0] + 1.0, spt[1] + 1.5, sock_r, -sock_r, sock_r))
                if len(rects) >= _LINK_OCCLUDER_LIMIT:
                    break
            if len(rects) >= _LINK_OCCLUDER_LIMIT:
                break
        if len(rects) >= _LINK_OCCLUDER_LIMIT:
            break
    return rects


def _get_mi_counts(any_socket_or_node):
    """帧级缓存：multi-input to_socket 指针 -> 连入线数。
    传入任意 socket 或 node 以取到 tree（id_data）。"""
    counts = _frame_cache.get('mi_counts')
    if counts is not None:
        return counts
    counts = {}
    tree = getattr(any_socket_or_node, "id_data", None)
    if tree is not None:
        try:
            for lk in tree.links:
                sp = lk.to_socket.as_pointer()
                counts[sp] = counts.get(sp, 0) + 1
        except Exception:
            pass
    _frame_cache['mi_counts'] = counts
    return counts


def _multi_input_y_offset(link):
    """多输入端口(multi-input socket)的连线终点 y 偏移。

    复刻 Blender 源码 node_link_calculate_multi_input_position:
        GAP    = 0.25 * widget_unit
        offset = (total_inputs * GAP - GAP) * 0.5
        y      = socket.y - offset + sort_id * GAP
    仅当 to_socket 是多输入端口、且节点未折叠时生效；否则返回 0。
    total_inputs 用帧级缓存统计该 to_socket 连入的线数。
    """
    to_sock = link.to_socket
    if not getattr(to_sock, "is_multi_input", False):
        return 0.0
    to_node = link.to_node
    if getattr(to_node, "hide", False):
        return 0.0
    # 帧级统计：to_socket 指针 -> 连入线数
    counts = _get_mi_counts(to_sock)
    total_inputs = counts.get(to_sock.as_pointer(), 0)
    if total_inputs < 2:
        return 0.0
    sort_id = getattr(link, "multi_input_sort_id", None)
    if sort_id is None:
        return 0.0
    ps = bpy.context.preferences.system.pixel_size
    widget_unit = round(18 * dpi_fac()) + 2 * ps
    gap = 0.25 * widget_unit
    offset = (total_inputs * gap - gap) * 0.5
    return -offset + sort_id * gap


def _link_occluder_rects_cached(link, ordered_nodes, order_index, v2d, outline_stroke_width=0.0):
    """计算曲线遮挡区域并返回两端 socket 坐标。

    内部使用 _frame_cache 做帧级缓存：同一帧内节点 body rect + socket rects 只算一次。
    """
    fi = order_index.get(link.from_node.as_pointer(), 0)
    ti = order_index.get(link.to_node.as_pointer(), 0)
    link_back_index = max(fi, ti)

    # 直接读 socket 位置，跳过 index 搜索
    p1 = _get_socket_view_loc(link.from_socket)
    p2 = _get_socket_view_loc(link.to_socket)
    if p1 is None or p2 is None:
        return [], None, None, None, None
    l1x, l1y = p1
    l2x, l2y = p2
    # 多输入端口：终点按连接顺序在端口上垂直排开（复刻原生 Blender）
    l2y += _multi_input_y_offset(link)
    curv = get_curving_factor()
    if curv > 0.001:
        handle_len = get_adaptive_handle_offset(l1x, l1y, l2x, l2y, curv)
        lb = (min(l1x, l2x, l1x + handle_len, l2x - handle_len),
              min(l1y, l2y),
              max(l1x, l2x, l1x + handle_len, l2x - handle_len),
              max(l1y, l2y))
    else:
        lb = (min(l1x, l2x), min(l1y, l2y), max(l1x, l2x), max(l1y, l2y))
    overlap_pad = max(1.0, 4.0 * dpi_fac())
    lb = (lb[0] - overlap_pad, lb[1] - overlap_pad, lb[2] + overlap_pad, lb[3] + overlap_pad)

    # 帧级上下文（view_scale / sock_r 等常量每帧只算一次）
    cached_ctx = _frame_cache.get('_occ_ctx')
    if cached_ctx is None:
        p_ref_1 = v2d.view_to_region(0.0, 0.0, clip=False)
        p_ref_2 = v2d.view_to_region(100.0, 0.0, clip=False)
        view_scale = 1.0
        if p_ref_1 and p_ref_2:
            view_scale = abs(p_ref_2[0] - p_ref_1[0]) / 100.0
        outline_pad_px = outline_stroke_width * get_pixel_size() * 0.7 * view_scale + 2.0
        ps_link = bpy.context.preferences.system.pixel_size
        widget_unit_link = round(18 * dpi_fac()) + 2 * ps_link
        sock_r = 0.25 * widget_unit_link * view_scale
        cached_ctx = {
            'view_scale': view_scale,
            'outline_pad_px': outline_pad_px,
            'sock_r': sock_r,
            'cache': {},
        }
        _frame_cache['_occ_ctx'] = cached_ctx
    else:
        view_scale = cached_ctx['view_scale']
        outline_pad_px = cached_ctx['outline_pad_px']
        sock_r = cached_ctx['sock_r']

    node_cache = cached_ctx['cache']
    occ_limit = _LINK_OCCLUDER_LIMIT

    rects = []

    occ_ordered_data = _frame_cache.get('_occ_ordered_data')
    if occ_ordered_data is None:
        bounds_cache = _frame_cache.get('node_bounds')
        occ_ordered_data = []
        for idx, node in enumerate(ordered_nodes):
            ptr = node.as_pointer()
            occ_ordered_data.append((idx, ptr, node, bounds_cache.get(ptr) if bounds_cache else None))

    for idx, ptr, node, nb in occ_ordered_data:
        if idx >= link_back_index:
            break

        if nb is None:
            nb = _node_draw_bounds(node)
        if not _rects_overlap(lb, nb):
            continue

        cached = node_cache.get(ptr)
        if cached is None:
            body_rect = None
            socket_rects = []
            if node.select or node.type == 'REROUTE':
                c0 = v2d.view_to_region(nb[0], nb[3], clip=False)
                c1 = v2d.view_to_region(nb[2], nb[1], clip=False)
            else:
                c0 = v2d.view_to_region(nb[0] - 0.5, nb[3] + 0.5, clip=False)
                c1 = v2d.view_to_region(nb[2] + 0.5, nb[1] - 0.5, clip=False)
            if c0 and c1:
                cx = 0.5 * (c0[0] + c1[0])
                cy = 0.5 * (c0[1] + c1[1])
                pad = outline_pad_px if node.select else 0.0
                if node.select or node.type == 'REROUTE':
                    hx = abs(c1[0] - c0[0]) * 0.5 + pad
                    hy = abs(c1[1] - c0[1]) * 0.5 + pad
                    r = 6.0 * view_scale + pad
                    if node.type == 'REROUTE':
                        cx += 2.0
                        hx *= 0.88
                        hy *= 0.88
                        r *= 0.88
                else:
                    hx = abs(c1[0] - c0[0]) * 0.5 + 1.0
                    hy = abs(c1[1] - c0[1]) * 0.5 + 1.0
                    cx += 1.5
                    r = 6.0 * view_scale
                body_rect = (cx, cy, hx, hy, r)

                for sox, is_out in ((node.inputs, False), (node.outputs, True)):
                    for si, socket in enumerate(sox):
                        if not socket.enabled or socket.hide:
                            continue
                        if hasattr(socket, "is_icon_visible") and not socket.is_icon_visible:
                            continue
                        # 多输入端口不是圆形端口，跳过其圆形遮挡（否则会在汇聚的连线中间留下圆形缺口）
                        if (not is_out) and getattr(socket, "is_multi_input", False):
                            continue
                        sloc = _get_socket_view_loc(socket)
                        if sloc is None:
                            continue
                        slx, sly = sloc
                        spt = v2d.view_to_region(slx, sly, clip=False)
                        if spt:
                            socket_rects.append((spt[0] + 1.0, spt[1] + 1.5, sock_r, -sock_r, sock_r))
                        if len(socket_rects) >= occ_limit:
                            break
                    if len(socket_rects) >= occ_limit:
                        break
            cached = {'nb': nb, 'body': body_rect, 'sockets': socket_rects}
            node_cache[ptr] = cached

        body_rect = cached['body']
        if body_rect:
            rects.append(body_rect)

        for sock_item in cached['sockets']:
            rects.append(sock_item)
            if len(rects) >= occ_limit:
                break
        if len(rects) >= occ_limit:
            break

    return rects[:occ_limit], l1x, l1y, l2x, l2y


def _link_view_bounds(link):
    # 连线两端插槽的视图空间包围盒(用于和节点包围盒做相交筛选)
    try:
        fi = next((j for j, s in enumerate(link.from_node.outputs)
                   if s.as_pointer() == link.from_socket.as_pointer()), None)
        ti = next((j for j, s in enumerate(link.to_node.inputs)
                   if s.as_pointer() == link.to_socket.as_pointer()), None)
        if fi is None or ti is None:
            return None
        x1, y1 = get_socket_loc(link.from_node, True, fi)
        x2, y2 = get_socket_loc(link.to_node, False, ti)
    except Exception:
        return None
    curv = get_curving_factor()
    if curv > 0.001:
        handle_len = get_adaptive_handle_offset(x1, y1, x2, y2, curv)
        x_min = min(x1, x2, x1 + handle_len, x2 - handle_len)
        x_max = max(x1, x2, x1 + handle_len, x2 - handle_len)
        return (x_min, min(y1, y2), x_max, max(y1, y2))
    return (min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2))


def draw_drag_circle(center, radius, color):
    shader = get_shader('SDF_CIRCLE')
    if not shader:
        return

    r = radius * dpi_fac()
    size = r + 2.0
    uv_scale = size / r
    cx, cy = center
    pos = [
        (cx - size, cy - size), (cx - size, cy + size), (cx + size, cy - size),
        (cx + size, cy - size), (cx - size, cy + size), (cx + size, cy + size),
    ]
    uv = [
        (-uv_scale, -uv_scale), (-uv_scale, uv_scale), (uv_scale, -uv_scale),
        (uv_scale, -uv_scale), (-uv_scale, uv_scale), (uv_scale, uv_scale),
    ]

    gpu.state.blend_set('ALPHA')
    batch = batch_for_shader(shader, 'TRIS', {"pos": pos, "uv": uv})
    shader.bind()
    shader.uniform_float("color", color)
    batch.draw(shader)
    gpu.state.blend_set('NONE')


def draw_rainbow_node_outline(node, radius=8, stroke_width=2.0, alpha_mod=1.0, time_sec=None, occluders=None):
    global _PERF_ACCUM_T_OUTLINE_SETUP, _PERF_ACCUM_T_OUTLINE_GAP, _PERF_ACCUM_T_OUTLINE_UBO, _PERF_ACCUM_T_OUTLINE_DRAW
    _perf_outline_mark = time.perf_counter()
    shader = get_shader('RAINBOW_BOX_OUTLINE')
    if not shader:
        _PERF_ACCUM_T_OUTLINE_SETUP += time.perf_counter() - _perf_outline_mark
        return

    if time_sec is None:
        time_sec = time.time()

    # 帧级 socket region 缓存，避免同一 socket 在多个 outline 节点间重复 view_to_region
    sock_region_cache = _frame_cache.get('sr')
    if sock_region_cache is None:
        sock_region_cache = {}
        _frame_cache['sr'] = sock_region_cache

    nlocx, nlocy = abs_node_location(node)
    nlocx = nlocx * dpi_fac()
    nlocy = nlocy * dpi_fac()
    ndimx = node.dimensions.x
    ndimy = node.dimensions.y

    v2r = bpy.context.region.view2d.view_to_region

    # view_scale 是全帧统一的水平缩放（仿射变换与参考点无关），帧级缓存一次即可，
    # 避免每个 outline 节点重复 2 次 view_to_region。
    view_scale = _frame_cache.get('outline_vs')
    if view_scale is None:
        p_ref_1 = v2r(0.0, 0.0, clip=False)
        p_ref_2 = v2r(100.0, 0.0, clip=False)
        view_scale = 1.0
        if p_ref_1 and p_ref_2:
            view_scale = abs(p_ref_2[0] - p_ref_1[0]) / 100.0
        _frame_cache['outline_vs'] = view_scale

    stroke_px = max(0.0, stroke_width * get_pixel_size() * 0.7)
    stroke_px *= view_scale
    _frame_cache['outline_stroke_px'] = stroke_px
    radius_px = (0.2 * (round(18 * dpi_fac()) + 2 * get_pixel_size()) + get_pixel_size()) * view_scale

    if node.type == 'REROUTE':
        rlocx, rlocy = abs_node_location(node)
        cx = rlocx * dpi_fac()
        cy = rlocy * dpi_fac()
        reroute_radius = 5.0 * dpi_fac()
        nlocx = cx - reroute_radius
        nlocy = cy + reroute_radius
        ndimx = reroute_radius * 2
        ndimy = reroute_radius * 2

    if node.hide and node.type not in {'REROUTE', 'FRAME'}:
        y_center = nlocy - (9.0 * dpi_fac())
        y_min = y_center - (ndimy * 0.5)
        y_max = y_center + (ndimy * 0.5)
        p1 = v2r(nlocx, y_min, clip=False)
        p2 = v2r(nlocx + ndimx, y_max, clip=False)
    else:
        p1 = v2r(nlocx, nlocy, clip=False)
        p2 = v2r(nlocx + ndimx, nlocy - ndimy, clip=False)

    if not p1 or not p2:
        _PERF_ACCUM_T_OUTLINE_SETUP += time.perf_counter() - _perf_outline_mark
        return

    if node.type == 'REROUTE':
        rlocx, rlocy = abs_node_location(node)
        cx = rlocx * dpi_fac()
        cy = rlocy * dpi_fac()
        rc_pt = v2r(cx, cy, clip=False)
        if not rc_pt:
            _PERF_ACCUM_T_OUTLINE_SETUP += time.perf_counter() - _perf_outline_mark
            return
        center_x, center_y = rc_pt[0] + 0.75, rc_pt[1] + 1.0
        r = (5.0 * dpi_fac()) * view_scale
        hw = r
        hh = r
    else:
        rmin_x = min(p1[0], p2[0])
        rmax_x = max(p1[0], p2[0]) + 1.5
        rmin_y = min(p1[1], p2[1])
        rmax_y = max(p1[1], p2[1]) + 1.5
        center_x = (rmin_x + rmax_x) * 0.5
        center_y = (rmin_y + rmax_y) * 0.5
        hw = (rmax_x - rmin_x) * 0.5
        hh = (rmax_y - rmin_y) * 0.5

    # ponytail: skip off-screen
    region = bpy.context.region
    if region and hw > 0 and (center_x + hw + 300 < 0 or
                  center_x - hw - 300 > region.width or
                  center_y + hh + 300 < 0 or
                  center_y - hh - 300 > region.height):
        _PERF_ACCUM_T_OUTLINE_SETUP += time.perf_counter() - _perf_outline_mark
        return

    pad = radius_px + stroke_px + 2.0

    vertices = [
        (center_x - hw - pad, center_y - hh - pad),
        (center_x + hw + pad, center_y - hh - pad),
        (center_x - hw - pad, center_y + hh + pad),
        (center_x + hw + pad, center_y + hh + pad),
    ]

    uvs = [
        (-hw - pad, -hh - pad),
        ( hw + pad, -hh - pad),
        (-hw - pad,  hh + pad),
        ( hw + pad,  hh + pad),
    ]

    _wu = _frame_cache.get('outline_wu')
    if _wu is None:
        ps = bpy.context.preferences.system.pixel_size
        widget_unit = round(18 * dpi_fac()) + 2 * ps
        _wu = (ps, widget_unit)
        _frame_cache['outline_wu'] = _wu
    else:
        ps, widget_unit = _wu
    gaps = []
    gap_radius_px = 0.25 * widget_unit * view_scale
    _frame_cache['outline_gap_radius_px'] = gap_radius_px
    _PERF_ACCUM_T_OUTLINE_SETUP += time.perf_counter() - _perf_outline_mark
    _perf_outline_mark = time.perf_counter()

    if stroke_px > 0.1:
        def collect_gaps(sockets, is_output):
            for i, socket in enumerate(sockets):
                if not socket.enabled or socket.hide:
                    continue
                if hasattr(socket, "is_icon_visible") and not socket.is_icon_visible:
                    continue
                sptr = socket.as_pointer()
                cached_pt = sock_region_cache.get(sptr)
                if cached_pt is None:
                    sloc = _get_socket_view_loc(socket)
                    if sloc is None:
                        continue
                    lx, ly = sloc
                    cached_pt = v2r(lx, ly, clip=False)
                    if cached_pt:
                        sock_region_cache[sptr] = cached_pt
                if cached_pt:
                    gaps.append(_socket_outline_gap(socket, cached_pt, center_x, center_y, is_output))

        collect_gaps(node.inputs, False)
        collect_gaps(node.outputs, True)

    gap_limit = 64
    gaps = gaps[:gap_limit]

    occ_uv = []
    occ_limit = _OUTLINE_OCCLUDER_LIMIT
    body_pad = 0.5
    if occluders:
        for occ in occluders:
            ox0, oy0, ox1, oy1 = _node_draw_bounds(occ)
            if occ.type == 'REROUTE':
                c0 = v2r(ox0, oy1, clip=False)
                c1 = v2r(ox1, oy0, clip=False)
            else:
                c0 = v2r(ox0 - body_pad, oy1 + body_pad, clip=False)
                c1 = v2r(ox1 + body_pad, oy0 - body_pad, clip=False)
            if not c0 or not c1:
                continue
            omin_x = min(c0[0], c1[0])
            omax_x = max(c0[0], c1[0])
            omin_y = min(c0[1], c1[1])
            omax_y = max(c0[1], c1[1])
            ocx = (omin_x + omax_x) * 0.5 - center_x
            ocy = (omin_y + omax_y) * 0.5 - center_y
            ohx = (omax_x - omin_x) * 0.5
            ohy = (omax_y - omin_y) * 0.5
            if occ.type == 'REROUTE':
                ohx = -(ohx * 0.32 + (pad if occ.select else 0.0))
            elif occ.select:
                occ_pad = stroke_px + 1.0
                ohx += occ_pad
                ohy = -(ohy + occ_pad)
            else:
                ohx += 1.0
                ocx += 1.5
                ocy += 0.0
                ohy += 1.0
                if len(gaps) < gap_limit:
                    for sox, is_out in ((occ.inputs, False), (occ.outputs, True)):
                        for i, socket in enumerate(sox):
                            if len(gaps) >= gap_limit:
                                break
                            if not socket.enabled or socket.hide:
                                continue
                            if hasattr(socket, "is_icon_visible") and not socket.is_icon_visible:
                                continue
                            sptr = socket.as_pointer()
                            cached_pt = sock_region_cache.get(sptr)
                            if cached_pt is None:
                                sloc = _get_socket_view_loc(socket)
                                if sloc is None:
                                    continue
                                slx, sly = sloc
                                cached_pt = v2r(slx, sly, clip=False)
                                if cached_pt:
                                    sock_region_cache[sptr] = cached_pt
                            if cached_pt:
                                gaps.append(_socket_outline_gap(socket, cached_pt, center_x, center_y, is_out))
            occ_uv.append((ocx, ocy, ohx, ohy))
            if len(occ_uv) >= occ_limit:
                break

    gap_count = len(gaps)
    _PERF_ACCUM_T_OUTLINE_GAP += time.perf_counter() - _perf_outline_mark
    _perf_outline_mark = time.perf_counter()

    batch = batch_for_shader(shader, 'TRI_STRIP', {"pos": vertices, "uv": uvs})

    shader.bind()
    h_start, h_range, sat, val = get_gradient_params()
    occ_radius = (0.2 * widget_unit + 1.0) * view_scale
    shader.uniform_float("u_time", time_sec % 1000.0)
    shader.uniform_float("u_alpha", alpha_mod * get_breath_alpha(time_sec))
    shader.uniform_float("rect_half_size", ((-hw, hh) if node.type == 'REROUTE' else (hw, hh)))
    shader.uniform_float("radius", radius_px)
    shader.uniform_float("u_occ_radius", occ_radius)
    shader.uniform_float("stroke_width", stroke_px)
    shader.uniform_float("u_hue_start", h_start)
    shader.uniform_float("u_hue_range", h_range)
    shader.uniform_float("u_saturation", sat)
    shader.uniform_float("u_value", val)
    shader.uniform_float("u_pulse_style", float(get_pulse_style()))

    ubo_floats = [0.0] * ((gap_limit + occ_limit + 1) * 4)
    for i, gap in enumerate(gaps):
        gx, gy = gap[0], gap[1]
        base = i * 4
        ubo_floats[base] = gx
        ubo_floats[base + 1] = gy
        ubo_floats[base + 2] = float(gap[2]) if len(gap) > 2 else 0.0
        ubo_floats[base + 3] = float(gap[3]) if len(gap) > 3 else 1.0

    occ_base = gap_limit * 4
    for i, (ocx, ocy, ohx, ohy) in enumerate(occ_uv):
        base = occ_base + i * 4
        ubo_floats[base] = ocx
        ubo_floats[base + 1] = ocy
        ubo_floats[base + 2] = ohx
        ubo_floats[base + 3] = ohy

    meta_base = (gap_limit + occ_limit) * 4
    ubo_floats[meta_base] = gap_radius_px
    ubo_floats[meta_base + 1] = float(gap_count)
    ubo_floats[meta_base + 2] = float(len(occ_uv))

    global _OUTLINE_UBO_BUF
    if _OUTLINE_UBO_BUF is None or len(_OUTLINE_UBO_BUF) != len(ubo_floats):
        _OUTLINE_UBO_BUF = gpu.types.Buffer('FLOAT', len(ubo_floats))
    _OUTLINE_UBO_BUF[:] = ubo_floats
    shader.uniform_block("u_params", get_outline_ubo(_OUTLINE_UBO_BUF))
    _PERF_ACCUM_T_OUTLINE_UBO += time.perf_counter() - _perf_outline_mark
    _perf_outline_mark = time.perf_counter()

    gpu.state.blend_set('ALPHA')
    batch.draw(shader)
    gpu.state.blend_set('NONE')
    _PERF_ACCUM_T_OUTLINE_DRAW += time.perf_counter() - _perf_outline_mark


def _get_socket_index_cached(cache, node, socket, is_output):
    key = (node.as_pointer(), bool(is_output))
    socket_map = cache.get(key)
    if socket_map is None:
        sockets = node.outputs if is_output else node.inputs
        socket_map = {s.as_pointer(): i for i, s in enumerate(sockets)}
        cache[key] = socket_map
    return socket_map.get(socket.as_pointer())


def _node_draw_bounds(node):
    fac = dpi_fac()
    nlocx, nlocy = abs_node_location(node)
    x0 = nlocx * fac
    if node.type == 'REROUTE':
        r = 5.0 * fac
        cy = nlocy * fac
        return (x0 - r, cy - r, x0 + r, cy + r)
    x1 = x0 + node.dimensions.x
    if node.hide and node.type not in {'REROUTE', 'FRAME'}:
        y_center = nlocy * fac - (9.0 * fac)
        half = node.dimensions.y * 0.5
        return (x0, y_center - half, x1, y_center + half)
    y_top = nlocy * fac
    y_bot = y_top - node.dimensions.y
    return (x0, y_bot, x1, y_top)


def _calibrate_ui_order_offset(node):
    from ctypes import string_at, c_int16
    try:
        ptr = node.as_pointer()
        blob = string_at(c_void_p(ptr), 256)
        name_off = blob.find(node.name.encode('utf-8'))
        if name_off < 0:
            return None
        off = name_off + 64 + 4 + 4 + 64 + 8 + 2
        c_int16.from_address(ptr + off)
        return off
    except Exception:
        return None


def _node_ui_order(node):
    # prefer RNA identifier (works in all versions), fallback ctypes
    try:
        return node.identifier
    except Exception:
        pass
    global _UI_ORDER_OFFSET
    from ctypes import c_int16
    if _UI_ORDER_OFFSET is None:
        _UI_ORDER_OFFSET = _calibrate_ui_order_offset(node)
        if _UI_ORDER_OFFSET is None:
            return None
    try:
        return c_int16.from_address(node.as_pointer() + _UI_ORDER_OFFSET).value
    except Exception:
        return None


def _draw_order_front_to_back(nodes):
    node_list = list(nodes)
    ranked = []
    for i, n in enumerate(node_list):
        uo = _node_ui_order(n)
        key = uo if uo is not None else i
        ranked.append((key, i, n))
    ranked.sort(key=lambda t: (t[0], t[1]), reverse=True)
    return [n for _, _, n in ranked]


def _rects_overlap(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


def _front_occluders(node, ordered_nodes):
    result = []
    bounds_cache = _frame_cache.get('node_bounds')
    nb = bounds_cache.get(node.as_pointer()) if bounds_cache else _node_draw_bounds(node)
    fac = dpi_fac()
    border_view = 9.0 * fac
    nb = (nb[0] - border_view, nb[1] - border_view, nb[2] + border_view, nb[3] + border_view)
    for other in ordered_nodes:
        if other == node:
            break
        if other.type == 'FRAME':
            continue
        ob = bounds_cache.get(other.as_pointer()) if bounds_cache else _node_draw_bounds(other)
        if _rects_overlap(nb, ob):
            result.append(other)
    return result


def _front_occluders_batch(outline_nodes, ordered_nodes):
    if not outline_nodes:
        return {}

    bounds_cache = _frame_cache.get('node_bounds')
    ordered_data = []
    order_index = {}
    for idx, node in enumerate(ordered_nodes):
        ptr = node.as_pointer()
        bounds = bounds_cache.get(ptr) if bounds_cache else _node_draw_bounds(node)
        ordered_data.append((idx, ptr, node, bounds))
        order_index[ptr] = idx

    fac = dpi_fac()
    border_view = 9.0 * fac
    max_idx = len(ordered_data)
    targets = []
    result = {}
    for node in outline_nodes:
        ptr = node.as_pointer()
        nb = bounds_cache.get(ptr) if bounds_cache else _node_draw_bounds(node)
        expanded = (
            nb[0] - border_view,
            nb[1] - border_view,
            nb[2] + border_view,
            nb[3] + border_view,
        )
        target_idx = order_index.get(ptr, max_idx)
        targets.append((target_idx, ptr, expanded))
        result[ptr] = []

    targets.sort(key=lambda item: item[0])
    for target_idx, target_ptr, target_bounds in targets:
        occ = result[target_ptr]
        for idx, other_ptr, other, other_bounds in ordered_data:
            if idx >= target_idx:
                break
            if _rects_overlap(target_bounds, other_bounds):
                occ.append(other)

    return result


def _socket_occluded(cursor_x, cursor_y, sloc, owner_node, ordered_nodes, node_socksize):
    tolerance = 0.1 * (node_socksize / 0.25)
    half = node_socksize - tolerance
    sx0, sy0, sx1, sy1 = sloc[0] - half, sloc[1] - half, sloc[0] + half, sloc[1] + half
    for node in ordered_nodes:
        if node == owner_node:
            return False
        bx0, by0, bx1, by1 = _node_draw_bounds(node)
        if bx0 <= cursor_x <= bx1 and by0 <= cursor_y <= by1:
            return True
        if bx0 <= sx0 and sx1 <= bx1 and by0 <= sy0 and sy1 <= by1:
            return True
    return False


def get_socket_loc(node, is_output, index):
    try:
        sockets = node.outputs if is_output else node.inputs
        if index < len(sockets):
            socket = sockets[index]
            # Blender 5.3 结构体发生变化，安全扫描 runtime 指针偏移
            ptr = socket.as_pointer()
            for offset in range(440, 560, 8):
                runtime_ptr = c_void_p.from_address(ptr + offset).value
                if runtime_ptr and runtime_ptr > 0x1000000:
                    try:
                        loc_x = c_float.from_address(runtime_ptr + _SOCKET_RUNTIME_LOCATION_OFFSET).value
                        loc_y = c_float.from_address(runtime_ptr + _SOCKET_RUNTIME_LOCATION_OFFSET + 4).value
                        # 合理的坐标数值过滤
                        if -100000 < loc_x < 100000 and -100000 < loc_y < 100000:
                            return loc_x, loc_y
                    except Exception:
                        pass
    except Exception:
        pass

    if node.type == 'REROUTE':
        nlocx, nlocy = abs_node_location(node)
        fac = dpi_fac()
        return nlocx * fac, nlocy * fac

    fac = dpi_fac()
    nlocx, nlocy = abs_node_location(node)
    base_x = nlocx * fac
    base_y = nlocy * fac
    x = base_x + node.dimensions.x if is_output else base_x
    header_height = 32.0 * fac
    socket_height = 21.0 * fac
    y = base_y - header_height
    sockets = node.outputs if is_output else node.inputs
    enabled_sockets = [s for s in sockets if s.enabled]
    try:
        real_idx = enabled_sockets.index(sockets[index])
    except (ValueError, IndexError):
        real_idx = 0
    y = y - (real_idx * socket_height) - (socket_height * 0.5)
    if is_output:
        y += 0.5 * fac
    else:
        y -= 0.5 * fac
    return x + 0.5 * fac, y + 0.5 * fac


def _draw_links(context, draw_selected_node_outlines=True, draw_connected_links=True):
    global _SHADER_CACHE, _OUTLINE_UBO, _OUTLINE_UBO_BUF, _LINK_UBO, _LINK_GEOM_UBO, _RAINBOW_UBO, _INSTANCED_BATCH
    _frame_cache.clear()
    if context.space_data is None or context.space_data.type != 'NODE_EDITOR':
        return False

    # GPU context change detection - clear cached shaders/UBOs
    if context.region:
        rid = context.region.as_pointer()
        if rid != getattr(_draw_links, '_rid', 0):
            _SHADER_CACHE.clear()
            _OUTLINE_UBO = None
            _OUTLINE_UBO_BUF = None
            _LINK_UBO = None
            _LINK_GEOM_UBO = None
            _RAINBOW_UBO = None
            _INSTANCED_BATCH = None
            _draw_links._rid = rid
    tree = context.space_data.edit_tree or context.space_data.node_tree
    if not tree:
        return False
    tree_node_ptrs = {node.as_pointer() for node in tree.nodes}
    # 单次预计算全节点包围盒，避免 _front_occluders / occ 重复调用
    _frame_cache['node_bounds'] = {n.as_pointer(): _node_draw_bounds(n) for n in tree.nodes}
    selected_nodes = [node for node in context.selected_nodes if node.as_pointer() in tree_node_ptrs]

    thickness_mul = 1.0
    trace_linked_flow = True

    time_sec = time.time()
    # 动态降载：用视口变换+节点位置指纹与上帧比对，判断是否正在交互（移动节点/平移视口）
    # 冷却帧：停下后持续静止 _INTERACT_COOLDOWN 秒才算静止，避免拖动中微停顿造成闪现
    # 淡入淡出：高光 alpha 每帧朝目标平滑靠拢，恢复时柔和出现
    _INTERACT_COOLDOWN = 0.144
    rid = context.region.as_pointer() if context.region else 0
    interact_state = _INTERACT_STATE.get(rid)
    if interact_state is None:
        interact_state = {
            'prev_fp': None,
            'frozen_time': None,
            'last_move_t': 0.0,
            'outline_alpha': 1.0,
            'show_alpha': 0.0,
            'visible_tree_ptr': 0,
            'visible_node_ptrs': set(),
            'visible_link_node_ptrs': set(),
            'fade_alpha': 0.0,
            'fade_node_ptrs': set(),
            'fade_link_node_ptrs': set(),
        }
        _INTERACT_STATE[rid] = interact_state
    else:
        interact_state.setdefault('show_alpha', 0.0)
        interact_state.setdefault('visible_tree_ptr', 0)
        interact_state.setdefault('visible_node_ptrs', set())
        interact_state.setdefault('visible_link_node_ptrs', set())
        interact_state.setdefault('fade_alpha', 0.0)
        interact_state.setdefault('fade_node_ptrs', set())
        interact_state.setdefault('fade_link_node_ptrs', set())
    _moved = False
    try:
        _v2d_fp = context.region.view2d
        _vfx0, _vfy0 = _v2d_fp.region_to_view(0.0, 0.0)
        _vfx1, _vfy1 = _v2d_fp.region_to_view(float(context.region.width), float(context.region.height))
        _nb_fp = _frame_cache['node_bounds']
        _ns_fp = 0.0
        for _b_fp in _nb_fp.values():
            _ns_fp += _b_fp[0] + _b_fp[1]
        _fp = (round(_vfx0, 2), round(_vfy0, 2), round(_vfx1, 2), round(_vfy1, 2), len(_nb_fp), round(_ns_fp, 1))
        if interact_state['prev_fp'] is not None and _fp != interact_state['prev_fp']:
            _moved = True
        interact_state['prev_fp'] = _fp
    except Exception:
        _moved = False
    if _moved:
        interact_state['last_move_t'] = time_sec
    # 冷却期内仍视为交互中
    _interacting = (time_sec - interact_state['last_move_t']) < _INTERACT_COOLDOWN
    if _interacting:
        if interact_state['frozen_time'] is None:
            interact_state['frozen_time'] = time_sec
        time_sec = interact_state['frozen_time']
    else:
        interact_state['frozen_time'] = None
    # 高光 alpha 朝目标平滑过渡（交互→0 隐藏，静止→1 显示）
    _alpha_target = 0.0 if _interacting else 1.0
    _alpha_step = 0.18
    if interact_state['outline_alpha'] < _alpha_target:
        interact_state['outline_alpha'] = min(_alpha_target, interact_state['outline_alpha'] + _alpha_step)
    elif interact_state['outline_alpha'] > _alpha_target:
        interact_state['outline_alpha'] = max(_alpha_target, interact_state['outline_alpha'] - _alpha_step)
    _perf_t0 = time.perf_counter()
    _perf_link_cnt = 0
    _perf_occ_cnt = 0
    _perf_t_node = time.perf_counter()
    # 用 tree.links 构建邻接表，避免 socket.links 属性遍历
    # up_adj: to_node → [from_nodes]（上游方向）
    # down_adj: from_node → [to_nodes]（下游方向）
    up_adj = {}
    down_adj = {}
    node_links = {}  # node_ptr → [link]，供 REROUTE 展开替代 .links 属性
    for lk in tree.links:
        fn = lk.from_node
        tn = lk.to_node
        if fn.as_pointer() in tree_node_ptrs and tn.as_pointer() in tree_node_ptrs:
            if not lk.from_socket.enabled or not lk.to_socket.enabled:
                continue
            lst = up_adj.get(tn)
            if lst is None:
                up_adj[tn] = [fn]
            else:
                lst.append(fn)
            lst = down_adj.get(fn)
            if lst is None:
                down_adj[fn] = [tn]
            else:
                lst.append(tn)
            # 双向缓存 link 对象，避免 REROUTE 展开访问 .links 属性
            for ptr in (fn.as_pointer(), tn.as_pointer()):
                nl = node_links.get(ptr)
                if nl is None:
                    node_links[ptr] = [lk]
                else:
                    nl.append(lk)


    def _collect_adjacent_real_nodes(nodes, adj_map):
        result = set()
        reroutes = set()
        for start in nodes:
            queue = list(adj_map.get(start, ()))
            seen = set()
            while queue:
                node = queue.pop(0)
                if node in seen:
                    continue
                seen.add(node)
                if node.type == 'REROUTE':
                    reroutes.add(node)
                    queue.extend(adj_map.get(node, ()))
                    continue
                result.add(node)
        return result, reroutes

    # 向上追溯（跟随输入端，仅向上游方向）
    upstream = set()
    if trace_linked_flow:
        queue = list(selected_nodes)
        while queue:
            node = queue.pop(0)
            lst = up_adj.get(node)
            if lst is None:
                continue
            for src in lst:
                if src not in upstream:
                    upstream.add(src)
                    queue.append(src)
        upstream_reroutes = set()
        link_adj_upstream, link_adj_upstream_reroutes = set(), set()
    else:
        upstream, upstream_reroutes = set(), set()
        link_adj_upstream, link_adj_upstream_reroutes = _collect_adjacent_real_nodes(selected_nodes, up_adj)

    # 向下追溯（跟随输出端，仅向下游方向）
    downstream = set()
    if trace_linked_flow:
        queue = list(selected_nodes)
        while queue:
            node = queue.pop(0)
            lst = down_adj.get(node)
            if lst is None:
                continue
            for dst in lst:
                if dst not in downstream:
                    downstream.add(dst)
                    queue.append(dst)
        downstream_reroutes = set()
        link_adj_downstream, link_adj_downstream_reroutes = set(), set()
    else:
        downstream, downstream_reroutes = set(), set()
        link_adj_downstream, link_adj_downstream_reroutes = _collect_adjacent_real_nodes(selected_nodes, down_adj)

    outline_nodes = set(selected_nodes) | upstream | downstream
    link_scope_nodes = outline_nodes | upstream_reroutes | downstream_reroutes | link_adj_upstream | link_adj_upstream_reroutes | link_adj_downstream | link_adj_downstream_reroutes
    node_by_ptr = {node.as_pointer(): node for node in tree.nodes}
    tree_ptr = tree.as_pointer()
    current_selected_ptrs = {node.as_pointer() for node in selected_nodes}
    current_node_ptrs = {node.as_pointer() for node in outline_nodes}
    current_link_node_ptrs = {node.as_pointer() for node in link_scope_nodes}
    _prev_tree_ptr = interact_state.get('visible_tree_ptr')
    _prev_node_ptrs = interact_state.get('visible_node_ptrs', set())
    _prev_link_node_ptrs = interact_state.get('visible_link_node_ptrs', set())
    _selection_already_visible = (
        _prev_tree_ptr == tree_ptr and
        current_selected_ptrs and
        current_selected_ptrs.issubset(_prev_node_ptrs)
    )
    _visible_changed = outline_nodes and (
        _prev_tree_ptr != tree_ptr or
        _prev_node_ptrs != current_node_ptrs or
        _prev_link_node_ptrs != current_link_node_ptrs
    )
    if _visible_changed:
        if _prev_tree_ptr == tree_ptr and _prev_node_ptrs:
            interact_state['fade_node_ptrs'] = set(_prev_node_ptrs)
            interact_state['fade_link_node_ptrs'] = set(_prev_link_node_ptrs)
            interact_state['fade_alpha'] = interact_state.get('show_alpha', 1.0)
        else:
            interact_state['fade_alpha'] = 0.0
            interact_state['fade_node_ptrs'] = set()
            interact_state['fade_link_node_ptrs'] = set()
        if not _selection_already_visible:
            interact_state['show_alpha'] = 0.0
    _show_target = 1.0 if outline_nodes else 0.0
    _show_step = 0.28
    if interact_state['show_alpha'] < _show_target:
        interact_state['show_alpha'] = min(_show_target, interact_state['show_alpha'] + _show_step)
    elif interact_state['show_alpha'] > _show_target:
        interact_state['show_alpha'] = max(_show_target, interact_state['show_alpha'] - _show_step)
    display_alpha = interact_state['show_alpha']
    if interact_state['fade_alpha'] > 0.0:
        interact_state['fade_alpha'] = max(0.0, interact_state['fade_alpha'] - _show_step)
        if interact_state['fade_alpha'] <= 0.001:
            interact_state['fade_alpha'] = 0.0
            interact_state['fade_node_ptrs'] = set()
            interact_state['fade_link_node_ptrs'] = set()
    fade_alpha = interact_state['fade_alpha']
    if outline_nodes:
        interact_state['visible_tree_ptr'] = tree_ptr
        interact_state['visible_node_ptrs'] = current_node_ptrs
        interact_state['visible_link_node_ptrs'] = current_link_node_ptrs
    elif display_alpha > 0.001 and interact_state.get('visible_tree_ptr') == tree_ptr:
        outline_nodes = {node_by_ptr[ptr] for ptr in interact_state.get('visible_node_ptrs', set()) if ptr in node_by_ptr}
        link_scope_nodes = {node_by_ptr[ptr] for ptr in interact_state.get('visible_link_node_ptrs', set()) if ptr in node_by_ptr}
    else:
        interact_state['visible_tree_ptr'] = tree_ptr
        interact_state['visible_node_ptrs'] = set()
        interact_state['visible_link_node_ptrs'] = set()
    fade_outline_nodes = set()
    fade_link_scope_nodes = set()
    if fade_alpha > 0.001 and interact_state.get('visible_tree_ptr') == tree_ptr:
        fade_outline_nodes = {node_by_ptr[ptr] for ptr in interact_state.get('fade_node_ptrs', set()) if ptr in node_by_ptr}
        fade_outline_nodes.difference_update(outline_nodes)
        fade_link_scope_nodes = {node_by_ptr[ptr] for ptr in interact_state.get('fade_link_node_ptrs', set()) if ptr in node_by_ptr}
    ordered_nodes = _draw_order_front_to_back(tree.nodes)
    # 预构建无 FRAME 的 ordered 列表，供 occ / outline 使用
    non_frame_ordered = [n for n in ordered_nodes if n.type != 'FRAME']
    non_frame_order_index = {}
    bounds_cache = _frame_cache.get('node_bounds')
    occ_ordered_data = []
    for i, n in enumerate(non_frame_ordered):
        ptr = n.as_pointer()
        non_frame_order_index[ptr] = i
        occ_ordered_data.append((i, ptr, n, bounds_cache.get(ptr) if bounds_cache else None))
    _frame_cache['_occ_ordered_data'] = occ_ordered_data
    global _PERF_ACCUM_T_OCCLUDER, _PERF_ACCUM_T_BATCH, _PERF_ACCUM_T_OUTLINE, _PERF_ACCUM_T_FRONT_OCC, _PERF_ACCUM_T_OUTLINE_SETUP, _PERF_ACCUM_T_OUTLINE_GAP, _PERF_ACCUM_T_OUTLINE_UBO, _PERF_ACCUM_T_OUTLINE_DRAW, _PERF_ACCUM_T_LINK_LOOP, _PERF_ACCUM_T_BEZIER_V2R, _PERF_ACCUM_T_NODE_DISCOVER, _PERF_ACCUM_T_LINK_COLLECT, _PERF_ACCUM_T_LINK_SCAN, _PERF_ACCUM_T_CACHE_BUILD, _PERF_ACCUM_T_BATCH_UBO_MAIN, _PERF_ACCUM_T_BATCH_LDATA, _PERF_ACCUM_T_BATCH_LDRAW
    _PERF_ACCUM_T_NODE_DISCOVER += time.perf_counter() - _perf_t_node

    # 彩虹边框节点不超过 30 个时不参与降载淡出，始终正常显示并正常流动
    _outline_few = len(outline_nodes) <= 30
    _outline_eff_alpha = (1.0 if _outline_few else interact_state['outline_alpha']) * display_alpha
    _outline_time = time.time() if _outline_few else time_sec
    # when few nodes, don't freeze time for link curves either
    if _outline_few:
        time_sec = time.time()
    if draw_selected_node_outlines and fade_outline_nodes and fade_alpha > 0.001:
        _fade_outline_few = len(fade_outline_nodes) <= 30
        _fade_outline_eff_alpha = (1.0 if _fade_outline_few else interact_state['outline_alpha']) * fade_alpha
        if _fade_outline_eff_alpha > 0.001:
            _perf_t_front_occ = time.perf_counter()
            front_occ_map = _front_occluders_batch(fade_outline_nodes, non_frame_ordered)
            _PERF_ACCUM_T_FRONT_OCC += time.perf_counter() - _perf_t_front_occ
            _perf_t_outline = time.perf_counter()
            _fade_outline_time = time.time() if _fade_outline_few else time_sec
            for node in fade_outline_nodes:
                occ = front_occ_map.get(node.as_pointer())
                if occ is None:
                    occ = _front_occluders(node, non_frame_ordered)
                draw_rainbow_node_outline(node, radius=8, stroke_width=2.1 * thickness_mul, time_sec=_fade_outline_time, occluders=occ, alpha_mod=_fade_outline_eff_alpha)
            _PERF_ACCUM_T_OUTLINE += time.perf_counter() - _perf_t_outline
    if draw_selected_node_outlines and outline_nodes and _outline_eff_alpha > 0.001:
        _perf_t_front_occ = time.perf_counter()
        front_occ_map = _front_occluders_batch(outline_nodes, non_frame_ordered)
        _PERF_ACCUM_T_FRONT_OCC += time.perf_counter() - _perf_t_front_occ
        _perf_t_outline = time.perf_counter()
        for node in outline_nodes:
            occ = front_occ_map.get(node.as_pointer())
            if occ is None:
                occ = _front_occluders(node, non_frame_ordered)
            draw_rainbow_node_outline(node, radius=8, stroke_width=2.1 * thickness_mul, time_sec=_outline_time, occluders=occ, alpha_mod=_outline_eff_alpha)
        _PERF_ACCUM_T_OUTLINE += time.perf_counter() - _perf_t_outline

    if not draw_connected_links:
        return (display_alpha > 0.001 or fade_alpha > 0.001) and draw_selected_node_outlines

    _perf_t_collect = time.perf_counter()
    _perf_t_linkscan = time.perf_counter()
    _nl = node_links

    def _collect_links_for_scope(scope_nodes):
        initial_links = set()
        # 直接遍历 tree.links 替代 4 重嵌套循环遍历 socket
        for link in tree.links:
            if link.from_node in scope_nodes and link.to_node in scope_nodes:
                if link.from_socket.enabled and link.to_socket.enabled:
                    initial_links.add(link)

        scoped_links = set()
        links_to_process = list(initial_links)
        while links_to_process:
            link = links_to_process.pop()
            if link in scoped_links:
                continue
            scoped_links.add(link)
            if link.to_node.type == 'REROUTE':
                for _lk in _nl.get(link.to_node.as_pointer(), ()):
                    if _lk.from_node in scope_nodes and _lk.to_node in scope_nodes:
                        links_to_process.append(_lk)
            if link.from_node.type == 'REROUTE':
                for _lk in _nl.get(link.from_node.as_pointer(), ()):
                    if _lk.from_node in scope_nodes and _lk.to_node in scope_nodes:
                        links_to_process.append(_lk)
        return scoped_links

    current_links_to_draw = _collect_links_for_scope(link_scope_nodes)
    fade_links_to_draw = _collect_links_for_scope(fade_link_scope_nodes) if fade_alpha > 0.001 else set()
    links_to_draw = current_links_to_draw | fade_links_to_draw
    _PERF_ACCUM_T_LINK_SCAN += time.perf_counter() - _perf_t_linkscan

    v2d = context.region.view2d
    zoom = 1.0
    invalid_zoom = 1.0
    try:
        x0, _ = v2d.region_to_view(0.0, 0.0)
        x1, _ = v2d.region_to_view(1.0, 0.0)
        if abs(x1 - x0) > 1e-8:
            invalid_zoom = 1.0 / abs(x1 - x0)
            zoom = max(0.35, min(6.0, invalid_zoom))
    except Exception:
        pass

    backing_col = (0.0, 0.0, 0.0, 0.55)
    width_backing_px = zoom * get_pixel_size() * 9.0 * thickness_mul
    width_rainbow_px = zoom * get_pixel_size() * 5.85 * thickness_mul
    r_rainbow_circle = zoom * 3.9
    invalid_widget_unit = (round(18 * dpi_fac()) + 2 * get_pixel_size()) * invalid_zoom
    invalid_label_radius = invalid_widget_unit * 0.5
    invalid_region_padding = invalid_widget_unit * 0.5

    link_drag_drawn = False
    win = getattr(context, "window", None)
    modal_ops = getattr(win, "modal_operators", None) if win is not None else None
    dragging = bool(modal_ops) and any(
        getattr(o, "bl_idname", "") == "NODE_OT_link" for o in modal_ops
    )

    if not dragging:
        _perf_t_cache = time.perf_counter()
        _INPUT_UPSTREAM_CACHE.clear()
        for lk in tree.links:
            up_node = lk.from_node
            up_sock = lk.from_socket
            up_ptr = up_sock.as_pointer()
            up_idx = next((j for j, s in enumerate(up_node.outputs) if s.as_pointer() == up_ptr), None)
            if up_idx is not None:
                _INPUT_UPSTREAM_CACHE[lk.to_socket.as_pointer()] = (up_node, up_idx)
        _PERF_ACCUM_T_CACHE_BUILD += time.perf_counter() - _perf_t_cache

    if modal_ops:
        for op in modal_ops:
            if getattr(op, "bl_idname", "") != "NODE_OT_link":
                continue
            drag_start = getattr(op, "drag_start", None)
            if drag_start is None or len(drag_start) < 2:
                continue

            best = None
            best_dist = 1e20
            best_is_output = False
            best_socket = None
            for node in tree.nodes:
                if node.type == 'FRAME':
                    continue
                for sockets, is_output in ((node.outputs, True), (node.inputs, False)):
                    for i, socket in enumerate(sockets):
                        if not socket.enabled or socket.hide:
                            continue
                        if hasattr(socket, "is_icon_visible") and not socket.is_icon_visible:
                            continue
                        loc = get_socket_loc(node, is_output, i)
                        pt = v2d.view_to_region(loc[0], loc[1], clip=False)
                        if not pt:
                            continue
                        dist = hypot(loc[0] - drag_start[0], loc[1] - drag_start[1])
                        if dist < best_dist:
                            best_dist = dist
                            best = (loc, pt)
                            best_is_output = is_output
                            best_socket = socket

            if best is None or best_dist > 64.0:
                continue

            if (not best_is_output) and best_socket is not None:
                upstream_hit = _INPUT_UPSTREAM_CACHE.get(best_socket.as_pointer())
                if upstream_hit is not None:
                    up_node, up_idx = upstream_hit
                    up_loc = get_socket_loc(up_node, True, up_idx)
                    up_pt = v2d.view_to_region(up_loc[0], up_loc[1], clip=False)
                    if up_pt:
                        best = (up_loc, up_pt)
                        best_is_output = True
                        try:
                            best_socket = up_node.outputs[up_idx]
                        except Exception:
                            pass

            cursor_loc = context.space_data.cursor_location
            fac = dpi_fac()
            cursor_x = (cursor_loc.x if hasattr(cursor_loc, "x") else cursor_loc[0]) * fac
            cursor_y = (cursor_loc.y if hasattr(cursor_loc, "y") else cursor_loc[1]) * fac
            p0 = best[0]
            p3 = (cursor_x, cursor_y)

            snap_target = None
            snap_dist = 1e20
            pixelsize = bpy.context.preferences.system.pixel_size
            widget_unit = round(18.0 * fac) + 2 * pixelsize
            node_socksize = 0.25 * widget_unit
            va = v2d.region_to_view(0.0, 0.0)
            vb = v2d.region_to_view(1000.0, 0.0)
            span = abs(vb[0] - va[0]) if (va and vb) else 0.0
            view2d_scale = (1000.0 / span) if span > 1e-9 else 1e6
            max_distance = node_socksize + min(30.0, max(5.0, 20.0 / view2d_scale))
            for node in ordered_nodes:
                if node.type == 'FRAME':
                    continue
                if node.type != 'REROUTE' and not node.hide:
                    _, _, _, y_top = _node_draw_bounds(node)
                    if y_top - cursor_y < widget_unit:
                        continue
                sockets = node.inputs if best_is_output else node.outputs
                for i, socket in enumerate(sockets):
                    if not socket.enabled or socket.hide:
                        continue
                    if hasattr(socket, "is_icon_visible") and not socket.is_icon_visible:
                        continue
                    sloc = get_socket_loc(node, best_is_output is False, i)
                    if node.hide and node.type != 'REROUTE':
                        inner_dx = cursor_x - sloc[0] if best_is_output else sloc[0] - cursor_x
                        if inner_dx > node_socksize:
                            continue
                        if 0.0 < inner_dx <= node_socksize + 4.0 and abs(cursor_y - sloc[1]) > node_socksize:
                            continue
                    d = hypot(sloc[0] - cursor_x, sloc[1] - cursor_y)
                    if d < snap_dist:
                        if _socket_occluded(cursor_x, cursor_y, sloc, node, ordered_nodes, node_socksize):
                            continue
                        snap_dist = d
                        snap_target = sloc
            is_snapped = snap_target is not None and snap_dist <= max_distance
            if is_snapped:
                p3 = (snap_target[0], snap_target[1])

            curv = get_curving_factor()
            if curv <= 0.001:
                p1 = (p0[0] + (p3[0] - p0[0]) / 3.0, p0[1] + (p3[1] - p0[1]) / 3.0)
                p2 = (p0[0] + (p3[0] - p0[0]) * 2.0 / 3.0, p0[1] + (p3[1] - p0[1]) * 2.0 / 3.0)
            else:
                handle_len = get_adaptive_handle_offset(p0[0], p0[1], p3[0], p3[1], curv)
                if best_is_output:
                    p1 = (p0[0] + handle_len, p0[1])
                    p2 = (p3[0] - handle_len, p3[1])
                else:
                    p1 = (p0[0] - handle_len, p0[1])
                    p2 = (p3[0] + handle_len, p3[1])
            r0 = v2d.view_to_region(*p0, clip=False)
            r3 = v2d.view_to_region(*p3, clip=False)
            if not r0 or not r3:
                continue
            pts = []
            for a, b, c, d in _BEZIER_COEFFS:
                x = a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0]
                y = a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]
                pts.append(v2d.view_to_region(x, y, clip=False))

            drag_hole_radius = r_rainbow_circle * dpi_fac()
            drag_endpoint_holes = [
                (best[1][0], best[1][1], drag_hole_radius, -drag_hole_radius, drag_hole_radius),
                (r3[0], r3[1], drag_hole_radius, -drag_hole_radius, drag_hole_radius),
            ]
            draw_batch_lines([pts], 'SMOOTH_COLOR', width_backing_px, backing_col,
                             occluders=drag_endpoint_holes)
            draw_batch_lines([pts], 'RAINBOW', width_rainbow_px, time_sec=time_sec, zoom=zoom, invalid_zoom=invalid_zoom,
                             dot_spacing=1.0 / _calc_dot_count(pts, zoom),
                             occluders=drag_endpoint_holes)
            drag_socket_color = _get_socket_color(best_socket)
            draw_drag_circle(best[1], r_rainbow_circle, drag_socket_color)
            if not is_snapped:
                draw_drag_circle(r3, r_rainbow_circle, drag_socket_color)
            link_drag_drawn = True
            break

    pts_to_redraw = False
    ordered_for_links = ordered_nodes
    link_order_index = {n.as_pointer(): i for i, n in enumerate(ordered_for_links)}
    _PERF_ACCUM_T_LINK_COLLECT += time.perf_counter() - _perf_t_collect
    _perf_t_link = time.perf_counter()
    _batch_links = []
    _fade_batch_links = []
    _shared_batch_links = []
    _reg_w = context.region.width
    _reg_h = context.region.height
    _reg_margin = 200.0
    for link in links_to_draw:
        link_is_current = link in current_links_to_draw
        link_is_shared = link_is_current and link in fade_links_to_draw
        link_alpha = min(1.0, display_alpha + fade_alpha) if link_is_shared else (
            display_alpha if link_is_current else fade_alpha)
        if link_alpha <= 0.001:
            continue
        # ponytail: pre-filter off-screen links before occ + bezier
        _p1 = _get_socket_view_loc(link.from_socket)
        _p2 = _get_socket_view_loc(link.to_socket)
        if _p1 is not None and _p2 is not None:
            _r1 = v2d.view_to_region(_p1[0], _p1[1], clip=False)
            _r2 = v2d.view_to_region(_p2[0], _p2[1], clip=False)
            if _r1 and _r2:
                _mnx = min(_r1[0], _r2[0])
                _mxx = max(_r1[0], _r2[0])
                _mny = min(_r1[1], _r2[1])
                _mxy = max(_r1[1], _r2[1])
                if (_mxx + _reg_margin < 0 or _mnx - _reg_margin > _reg_w or
                    _mxy + _reg_margin < 0 or _mny - _reg_margin > _reg_h):
                    continue

        # occ 放在最前，返回 socket 视图坐标供后续复用
        outline_stroke_width = 2.1 * thickness_mul
        _perf_t_occ = time.perf_counter()
        occ, l1x, l1y, l2x, l2y = _link_occluder_rects_cached(
            link, non_frame_ordered, non_frame_order_index, v2d,
            outline_stroke_width=outline_stroke_width)
        _PERF_ACCUM_T_OCCLUDER += time.perf_counter() - _perf_t_occ
        if l1x is None:
            continue
        _perf_occ_cnt += len(occ)
        _perf_link_cnt += 1

        curv_factor = get_curving_factor()

        pass  # ponytail: pre-filter at loop start covers this

        # 视图空间控制点（复用已有 l1x/l1y/l2x/l2y）
        _perf_t_bezier = time.perf_counter()
        vp0x, vp0y = l1x, l1y
        vp3x, vp3y = l2x, l2y
        if curv_factor <= 0.001:
            vp1x = vp0x + (vp3x - vp0x) / 3.0
            vp1y = vp0y + (vp3y - vp0y) / 3.0
            vp2x = vp0x + (vp3x - vp0x) * 2.0 / 3.0
            vp2y = vp0y + (vp3y - vp0y) * 2.0 / 3.0
        else:
            handle_len = get_adaptive_handle_offset(vp0x, vp0y, vp3x, vp3y, curv_factor)
            vp1x, vp1y = vp0x + handle_len, vp0y
            vp2x, vp2y = vp3x - handle_len, vp3y
        # 控制点转 region 浮点像素空间，避免整数截断造成方向相关偏移
        rp0 = _view_to_region_float(v2d, vp0x, vp0y)
        rp1 = _view_to_region_float(v2d, vp1x, vp1y)
        rp2 = _view_to_region_float(v2d, vp2x, vp2y)
        rp3 = _view_to_region_float(v2d, vp3x, vp3y)
        if None in (rp0, rp1, rp2, rp3):
            _PERF_ACCUM_T_BEZIER_V2R += time.perf_counter() - _perf_t_bezier
            continue
        # 实例化后贝塞尔采样移至 GPU，主循环不再逐点求值。
        # dot_count 用端点距离近似（借鉴 Blender 源码 lineLength = distance(P0,P3)）。
        _dot_count = _calc_dot_count_ctrl(rp0, rp3, zoom)
        _PERF_ACCUM_T_BEZIER_V2R += time.perf_counter() - _perf_t_bezier

        invalid = 0.0 if getattr(link, "is_valid", True) else 1.0
        invalid_center = ((rp0[0] + rp3[0]) * 0.5, (rp0[1] + rp3[1]) * 0.5)
        if invalid:
            invalid_center = _find_visible_invalid_center(
                rp0, rp3, (rp0, rp1, rp2, rp3),
                context.region.width, context.region.height,
                invalid_label_radius, invalid_region_padding)
        _target_batch_links = (_shared_batch_links if link_is_shared else
                               (_batch_links if link_is_current else _fade_batch_links))
        _target_batch_links.append({
            'ctrl': (rp0, rp1, rp2, rp3),
            'occ': occ,
            'alpha': link_alpha,
            'invalid': invalid,
            'invalid_center': invalid_center,
            'start_shape': _get_socket_shape_code(link.from_socket),
            'end_shape': _get_socket_shape_code(link.to_socket),
            'dot_count': _dot_count,
            # 多输入端口使用原生长条端口，不裁切每条连线的独立终点
            'skip_end': getattr(link.to_socket, "is_multi_input", False),
        })
    _PERF_ACCUM_T_LINK_LOOP += time.perf_counter() - _perf_t_link
    pts_to_redraw = True

    # 批量绘制
    if _fade_batch_links or _batch_links or _shared_batch_links:
        _perf_t_batch = time.perf_counter()
        bg = _BATCH_GROUP_SIZE
        for _draw_batch_links in (_fade_batch_links, _batch_links, _shared_batch_links):
            for bi in range(0, len(_draw_batch_links), bg):
                _draw_batch_group(_draw_batch_links[bi:bi + bg],
                                  width_rainbow_px, time_sec, zoom, invalid_zoom)
        _PERF_ACCUM_T_BATCH += time.perf_counter() - _perf_t_batch

    _result = link_drag_drawn or pts_to_redraw or display_alpha > 0.001 or fade_alpha > 0.001

    # 性能累计
    global _PERF_FRAME_CNT, _PERF_ACCUM_TIME, _PERF_ACCUM_LINKS, _PERF_ACCUM_OCCLUDERS, _PERF_ACCUM_OUTLINES
    global _PERF_ACCUM_SELECTED, _PERF_ACCUM_UPSTREAM, _PERF_ACCUM_DOWNSTREAM
    debug_perf = False
    if not debug_perf:
        _reset_perf_stats()
        return _result
    if not selected_nodes:
        return _result

    _PERF_FRAME_CNT += 1
    _PERF_ACCUM_TIME += time.perf_counter() - _perf_t0
    _PERF_ACCUM_LINKS += _perf_link_cnt
    _PERF_ACCUM_OCCLUDERS += _perf_occ_cnt
    _PERF_ACCUM_OUTLINES += len(outline_nodes)
    _PERF_ACCUM_SELECTED += len(selected_nodes)
    _PERF_ACCUM_UPSTREAM += len(upstream)
    _PERF_ACCUM_DOWNSTREAM += len(downstream)
    if _PERF_FRAME_CNT >= _PERF_LOG_INTERVAL:
        avg_ms = _PERF_ACCUM_TIME / _PERF_LOG_INTERVAL * 1000
        avg_links = _PERF_ACCUM_LINKS / _PERF_LOG_INTERVAL
        avg_occs = _PERF_ACCUM_OCCLUDERS / _PERF_LOG_INTERVAL
        avg_outs = _PERF_ACCUM_OUTLINES / _PERF_LOG_INTERVAL
        avg_selected = _PERF_ACCUM_SELECTED / _PERF_LOG_INTERVAL
        avg_upstream = _PERF_ACCUM_UPSTREAM / _PERF_LOG_INTERVAL
        avg_downstream = _PERF_ACCUM_DOWNSTREAM / _PERF_LOG_INTERVAL
        avg_t_occ = _PERF_ACCUM_T_OCCLUDER / _PERF_LOG_INTERVAL * 1000
        avg_t_batch = _PERF_ACCUM_T_BATCH / _PERF_LOG_INTERVAL * 1000
        avg_t_outline = _PERF_ACCUM_T_OUTLINE / _PERF_LOG_INTERVAL * 1000
        avg_t_front_occ = _PERF_ACCUM_T_FRONT_OCC / _PERF_LOG_INTERVAL * 1000
        avg_t_outline_setup = _PERF_ACCUM_T_OUTLINE_SETUP / _PERF_LOG_INTERVAL * 1000
        avg_t_outline_gap = _PERF_ACCUM_T_OUTLINE_GAP / _PERF_LOG_INTERVAL * 1000
        avg_t_outline_ubo = _PERF_ACCUM_T_OUTLINE_UBO / _PERF_LOG_INTERVAL * 1000
        avg_t_outline_draw = _PERF_ACCUM_T_OUTLINE_DRAW / _PERF_LOG_INTERVAL * 1000
        avg_t_link_loop = _PERF_ACCUM_T_LINK_LOOP / _PERF_LOG_INTERVAL * 1000
        avg_t_bezier = _PERF_ACCUM_T_BEZIER_V2R / _PERF_LOG_INTERVAL * 1000
        avg_t_node = _PERF_ACCUM_T_NODE_DISCOVER / _PERF_LOG_INTERVAL * 1000
        avg_t_linkscan = _PERF_ACCUM_T_LINK_SCAN / _PERF_LOG_INTERVAL * 1000
        avg_t_cache = _PERF_ACCUM_T_CACHE_BUILD / _PERF_LOG_INTERVAL * 1000
        avg_t_batch_ubo_main = _PERF_ACCUM_T_BATCH_UBO_MAIN / _PERF_LOG_INTERVAL * 1000
        avg_t_batch_ldata = _PERF_ACCUM_T_BATCH_LDATA / _PERF_LOG_INTERVAL * 1000
        avg_t_batch_ldraw = _PERF_ACCUM_T_BATCH_LDRAW / _PERF_LOG_INTERVAL * 1000
        avg_t_batch_other = avg_t_batch - avg_t_batch_ubo_main - avg_t_batch_ldata - avg_t_batch_ldraw
        avg_t_collect = _PERF_ACCUM_T_LINK_COLLECT / _PERF_LOG_INTERVAL * 1000
        avg_t_loop = avg_t_link_loop - avg_t_occ
        avg_t_rest = avg_ms - avg_t_link_loop - avg_t_batch - avg_t_outline - avg_t_front_occ - avg_t_node - avg_t_collect
        print(f"[Perf] {_PERF_LOG_INTERVAL}帧均值 | "
              f"{avg_ms:.2f}ms/帧 | "
              f"link={avg_links:.1f} | "
              f"occ={avg_occs:.1f} | "
              f"outline={avg_outs:.1f} | "
              f"sel={avg_selected:.1f} | "
              f"up={avg_upstream:.1f} | "
              f"down={avg_downstream:.1f} | "
              f"t_occ={avg_t_occ:.1f} | "
              f"t_batch={avg_t_batch:.1f}[ubo={avg_t_batch_ubo_main:.2f} ld={avg_t_batch_ldata:.2f} ldr={avg_t_batch_ldraw:.2f} other={avg_t_batch_other:.2f}] | "
              f"t_outline={avg_t_outline:.1f} | "
              f"t_focc={avg_t_front_occ:.1f} | "
              f"o_setup={avg_t_outline_setup:.1f} | "
              f"o_gap={avg_t_outline_gap:.1f} | "
              f"o_ubo={avg_t_outline_ubo:.1f} | "
              f"o_draw={avg_t_outline_draw:.1f} | "
              f"t_loop={avg_t_loop:.1f} | "
              f"t_bezier={avg_t_bezier:.1f} | "
              f"t_node={avg_t_node:.1f} | "
              f"t_lscan={avg_t_linkscan:.1f} | "
              f"t_cache={avg_t_cache:.1f} | "
              f"t_rest={avg_t_rest:.1f}")
        _reset_perf_stats()

    return _result


def _get_socket_color(socket):
    try:
        return tuple(socket.draw_color(bpy.context, socket.node))
    except Exception:
        try:
            return tuple(socket.draw_color_simple())
        except Exception:
            return (0.5, 0.5, 0.5, 1.0)


def draw_callback_highlight_connections():
    context = bpy.context
    if not _draw_links(context, draw_selected_node_outlines=True, draw_connected_links=True):
        return
    # 存 area 指针（整数）而非 area 对象，避免全屏切换等场景下 area 被销毁后留下悬空引用
    if context.area:
        _AREAS_TO_REDRAW.add(context.area.as_pointer())
    if not bpy.app.timers.is_registered(force_redraw):
        bpy.app.timers.register(force_redraw, first_interval=0.03)


def force_redraw():
    try:
        if not _AREAS_TO_REDRAW:
            return None
        # 遍历当前真实存在的 area，只对指针匹配上的重绘；
        # 已销毁的旧 area 不在当前列表里，自然跳过，杜绝访问悬空 C 指针导致崩溃
        for window in bpy.context.window_manager.windows:
            screen = window.screen
            if screen is None:
                continue
            for area in screen.areas:
                if area.as_pointer() in _AREAS_TO_REDRAW:
                    area.tag_redraw()
        _AREAS_TO_REDRAW.clear()
    except Exception:
        return None
    return None


class RainbowNoodleRandomGradient(bpy.types.Operator):
    bl_idname = "rainbow_noodle.random_gradient"
    bl_label = "随机渐变"
    bl_description = "随机生成一组好看的渐变颜色"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        import random
        addon = context.preferences.addons.get(__package__)
        if not addon:
            return {'CANCELLED'}
        p = addon.preferences

        # 保存旧历史，准备移位
        old_1 = tuple(p.history_1)
        old_0 = tuple(p.history_0)

        # 生成新的随机参数
        new_start = random.random()
        new_range = random.uniform(0.15, 0.5)
        sat = p.gradient_saturation
        val = p.gradient_value

        # 移位并更新
        p.history_2 = old_1
        p.history_1 = old_0
        p.history_0 = (new_start, new_range, sat, val)

        # 更新分段色块
        def _set_segs(idx, hs, hr, s, v):
            for seg in range(4):
                h = (hs + hr * (seg / 4.0)) % 1.0
                r, g, b = colorsys.hsv_to_rgb(h, s, v)
                setattr(p, f'hist_{idx}_s{seg}', (r, g, b))
        for i, hist in enumerate([p.history_0, p.history_1, p.history_2]):
            if any(hist):
                _set_segs(i, hist[0], hist[1], hist[2], hist[3])

        p.hue_start = new_start
        p.hue_range = new_range
        for window in context.window_manager.windows:
            for area in window.screen.areas:
                if area.type in ('NODE_EDITOR', 'PREFERENCES'):
                    area.tag_redraw()
        return {'FINISHED'}


class RainbowNoodleRestoreHistory(bpy.types.Operator):
    bl_idname = "rainbow_noodle.restore_history"
    bl_label = "恢复历史颜色"
    bl_description = "恢复到该历史颜色的参数"
    bl_options = {'REGISTER', 'UNDO'}

    index: bpy.props.IntProperty()

    def execute(self, context):
        addon = context.preferences.addons.get(__package__)
        if not addon:
            return {'CANCELLED'}
        p = addon.preferences
        hist = [p.history_0, p.history_1, p.history_2][self.index]
        if not any(hist):
            return {'CANCELLED'}

        cur = (p.hue_start, p.hue_range, p.gradient_saturation, p.gradient_value)
        if cur == tuple(hist):
            return {'CANCELLED'}

        p.hue_start = hist[0]
        p.hue_range = max(0.05, hist[1])
        p.gradient_saturation = hist[2]
        p.gradient_value = hist[3]
        for window in context.window_manager.windows:
            for area in window.screen.areas:
                if area.type in ('NODE_EDITOR', 'PREFERENCES'):
                    area.tag_redraw()
        return {'FINISHED'}


class RainbowNoodlePreferences(bpy.types.AddonPreferences):
    bl_idname = __package__


    connected_rainbow_thickness: bpy.props.FloatProperty(
        name="粗细",
        default=1.0,
        min=0.0,
        max=4.0,
        update=_on_gradient_change,
    )
    trace_linked_flow: bpy.props.BoolProperty(
        name="查找相连数据流上下游",
        default=True,
    )
    debug_performance_log: bpy.props.BoolProperty(
        name="Debug 性能日志",
        default=False,
    )
    pulse_style: bpy.props.EnumProperty(
        name="脉冲样式",
        items=[
            ('SMOOTH', "平滑", "平滑移动的脉冲光效"),
            ('DOT', "点状", "点状脉冲，圆角矩形点依次显示并淡出"),
            ('BREATH', "呼吸灯", "整体柔和明暗呼吸，复用淡入淡出透明度"),
        ],
        default='SMOOTH',
        update=_on_gradient_change,
    )
    hue_start: bpy.props.FloatProperty(
        name="色相起点",
        default=0.0,
        min=0.0,
        max=1.0,
        update=_on_gradient_change,
    )
    hue_range: bpy.props.FloatProperty(
        name="色相范围",
        default=1.0,
        min=0.05,
        max=1.0,
        update=_on_gradient_change,
    )
    gradient_saturation: bpy.props.FloatProperty(
        name="饱和度",
        default=0.65,
        min=0.0,
        max=1.0,
        update=_on_gradient_change,
    )
    gradient_value: bpy.props.FloatProperty(
        name="亮度",
        default=0.4,
        min=0.0,
        max=1.0,
        update=_on_gradient_change,
    )
    history_0: bpy.props.FloatVectorProperty(
        name="颜色历史 1", size=4, default=(0.0, 0.0, 0.0, 0.0),
    )
    history_1: bpy.props.FloatVectorProperty(
        name="颜色历史 2", size=4, default=(0.0, 0.0, 0.0, 0.0),
    )
    history_2: bpy.props.FloatVectorProperty(
        name="颜色历史 3", size=4, default=(0.0, 0.0, 0.0, 0.0),
    )
    hist_0_s0: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_0_s1: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_0_s2: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_0_s3: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_1_s0: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_1_s1: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_1_s2: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_1_s3: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_2_s0: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_2_s1: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_2_s2: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))
    hist_2_s3: bpy.props.FloatVectorProperty(name="", subtype='COLOR', size=3, default=(0.0, 0.0, 0.0))

    def draw(self, context):
        layout = self.layout
        col = layout.column()
        col.prop(self, "connected_rainbow_thickness", text="粗细", slider=True)
        col.prop(self, "trace_linked_flow", text="查找相连数据流上下游")
        col.prop(self, "debug_performance_log", text="Debug 性能日志")
        col.prop(self, "pulse_style", text="脉冲样式")

        col.prop(self, "hue_start", text="色相起点", slider=True)
        col.prop(self, "hue_range", text="色相范围", slider=True)
        col.prop(self, "gradient_saturation", text="饱和度", slider=True)
        col.prop(self, "gradient_value", text="亮度", slider=True)
        col.operator("rainbow_noodle.random_gradient", icon='FILE_REFRESH')

        histories = [(self.history_0, 0), (self.history_1, 1), (self.history_2, 2)]
        visible = [(hp, idx) for hp, idx in histories if any(hp)]
        if visible:
            remaining = layout.row(align=False)
            for i, (hp, idx) in enumerate(visible):
                if i < len(visible) - 1:
                    s = remaining.split(factor=1.0 / (len(visible) - i), align=False)
                    col = s.column(align=True)
                    remaining = s
                else:
                    col = remaining.column(align=True)
                bar_row = col.row(align=True)
                bar_row.scale_y = 0.6
                for seg in range(4):
                    b = bar_row.row(align=True)
                    b.prop(self, f'hist_{idx}_s{seg}', text="")
                op = col.row(align=True)
                op.scale_y = 0.7
                op.operator(
                    "rainbow_noodle.restore_history", text="  ", emboss=True,
                ).index = idx


def register():
    global _DRAW_HANDLE
    _SHADER_CACHE.clear()
    # 清理可能残留的注册（上次卸载不完整时）
    for cls in [RainbowNoodlePreferences, RainbowNoodleRestoreHistory, RainbowNoodleRandomGradient]:
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass
    try:
        prefs = bpy.context.preferences.addons.get(__package__)
        if prefs and hasattr(prefs, 'preferences'):
            p = prefs.preferences
            for i, hist in enumerate([p.history_0, p.history_1, p.history_2]):
                if any(hist):
                    for seg in range(4):
                        h = (hist[0] + hist[1] * (seg / 4.0)) % 1.0
                        r, g, b = colorsys.hsv_to_rgb(h, hist[2], hist[3])
                        setattr(p, f'hist_{i}_s{seg}', (r, g, b))
    except Exception:
        pass
    bpy.utils.register_class(RainbowNoodleRandomGradient)
    bpy.utils.register_class(RainbowNoodleRestoreHistory)
    _DRAW_HANDLE = bpy.types.SpaceNodeEditor.draw_handler_add(
        draw_callback_highlight_connections, (), 'WINDOW', 'POST_PIXEL'
    )


def unregister():
    global _DRAW_HANDLE, _OUTLINE_UBO, _OUTLINE_UBO_BUF, _LINK_UBO, _LINK_GEOM_UBO, _RAINBOW_UBO, _INSTANCED_BATCH
    if bpy.app.timers.is_registered(force_redraw):
        bpy.app.timers.unregister(force_redraw)
    if _DRAW_HANDLE is not None:
        bpy.types.SpaceNodeEditor.draw_handler_remove(_DRAW_HANDLE, 'WINDOW')
        _DRAW_HANDLE = None
    for cls in [RainbowNoodlePreferences, RainbowNoodleRestoreHistory, RainbowNoodleRandomGradient]:
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass
    _SHADER_CACHE.clear()
    _OUTLINE_UBO = None
    _OUTLINE_UBO_BUF = None
    _LINK_UBO = None
    _LINK_GEOM_UBO = None
    _RAINBOW_UBO = None
    _INSTANCED_BATCH = None
