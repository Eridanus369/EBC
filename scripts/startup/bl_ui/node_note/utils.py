import math
from .preferences import pref
from .nn_typing import int2, RGBA, Rect
import bpy
from bpy.types import Context, Node, Image, SpaceImageEditor

# ==================== 工具函数 ====================

def ui_scale():
    return bpy.context.preferences.system.ui_scale

def dpi_scale():
    return bpy.context.preferences.system.dpi / 72

def nd_abs_loc(node: Node):
    if hasattr(node, "location_absolute"):
        return node.location_absolute
    return node.location + nd_abs_loc(node.parent) if node.parent else node.location

def node_depth(node: Node) -> int:
    depth = 0
    while node.parent:
        depth += 1
        node = node.parent
    return depth

def text_split_lines(text: str) -> list[str]:
    if bpy.app.version < (5,2,0):
        """使用偏好设置中的分隔符将文本转换为换行"""
        separator: str = pref().line_separator
        if not separator:
            return text.splitlines()
        for sep in separator.split('|'):
            text = text.replace(sep, "\n")
    return text.splitlines()

def get_region_zoom(context: Context) -> float:
    """获取节点编辑器的缩放比例"""
    view2d = context.region.view2d
    x0, y0 = view2d.view_to_region(0, 0, clip=False)
    x1, y1 = view2d.view_to_region(1000, 1000, clip=False)
    return 1.0 if x1 == x0 else math.sqrt((x1 - x0)**2 + (y1 - y0)**2) / 1000

def view_to_region_scaled(x: float, y: float) -> int2:
    """将节点编辑器坐标转换为屏幕坐标（考虑UI缩放）"""
    return bpy.context.region.view2d.view_to_region(x * ui_scale(), y * ui_scale(), clip=False)

def _color_equal(c1: RGBA, c2: RGBA, tol: float = 0.001) -> bool:
    return all(abs(a - b) < tol for a, b in zip(c1, c2))

def check_color_visibility(color: RGBA) -> bool:
    """检查背景颜色是否可见（用于过滤显示）"""
    prefs = pref()
    if _color_equal(color, prefs.col_preset_1):
        return prefs.show_red
    if _color_equal(color, prefs.col_preset_2):
        return prefs.show_green
    if _color_equal(color, prefs.col_preset_3):
        return prefs.show_blue
    if _color_equal(color, prefs.col_preset_4):
        return prefs.show_orange
    if _color_equal(color, prefs.col_preset_5):
        return prefs.show_purple

    return prefs.show_other

def get_node_screen_rect(node: Node) -> Rect:
    """获取节点在屏幕空间中的矩形区域"""
    loc_x, loc_y = nd_abs_loc(node)
    width, height = node.width, node.dimensions.y
    min_x, min_y = view_to_region_scaled(loc_x, loc_y)
    max_x, max_y = view_to_region_scaled(loc_x + width, loc_y - height)
    return min_x, min_y, max_x, max_y

def is_rect_overlap(r1: Rect, r2: Rect) -> bool:
    """检查两个矩形是否重叠"""
    return not (r1[2] < r2[0] or r1[0] > r2[2] or r1[3] < r2[1] or r1[1] > r2[3])

# 来自 来一点咖啡吗 RARA_Blender_Helper: https://space.bilibili.com/27284213
def import_clipboard_image() -> Image | None:
    area = bpy.context.area
    old_ui_type = area.ui_type
    area.ui_type = 'IMAGE_EDITOR'
    space: SpaceImageEditor = area.spaces.active  # type: ignore
    old_image = space.image
    try:
        bpy.ops.image.clipboard_paste()
    except:
        space.image = old_image
        area.ui_type = old_ui_type
        return None
    clipboard_image = space.image
    clipboard_image.pack()
    space.image = old_image
    area.ui_type = old_ui_type
    return clipboard_image