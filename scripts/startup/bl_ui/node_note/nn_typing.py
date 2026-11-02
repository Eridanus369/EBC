from typing import Literal
from bpy.types import Node, Image
float2 = tuple[float, float]
int2 = tuple[int, int]
int3 = tuple[int, int, int]
RGBA = tuple[float, float, float, float]
Rect = tuple[float, float, float, float]

AlignMode = Literal["TOP", "BOTTOM", "LEFT", "RIGHT"]
TextWidthMode = Literal["AUTO", "FIT", "MANUAL", "KEEP"]
ImageWidthMode = Literal["AUTO", "ORIGINAL", "MANUAL", "KEEP"]
BadgeScaleMode = Literal["RELATIVE", "ABSOLUTE"]

class NotedNode(Node):
    note_show_txt: bool
    note_show_img: bool
    note_show_badge: bool
    note_swap_order: bool
    note_text: str
    note_image: Image | None
    note_badge_index: int
    note_text_color: RGBA
    note_txt_bg_color: RGBA
    note_badge_color: RGBA
    note_font_size: int
    note_txt_bg_width: int
    note_img_width: int
    
    note_txt_width_mode: TextWidthMode
    note_img_width_mode: ImageWidthMode
    note_txt_pos: AlignMode
    note_img_pos: AlignMode
    
    note_txt_center: bool
    note_img_center: bool
    note_txt_offset: int2
    note_img_offset: int2
