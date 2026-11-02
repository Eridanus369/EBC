import bpy
from bpy.types import PropertyGroup, Context
from bpy.props import BoolProperty, StringProperty, FloatProperty, FloatVectorProperty, IntProperty, EnumProperty
from .nn_typing import AlignMode, TextWidthMode, ImageWidthMode, BadgeScaleMode

align_items: list[tuple[AlignMode, str, str]] = [
    ('TOP', "Top", ""),
    ('BOTTOM', "Bottom", ""),
    ('LEFT', "Left", ""),
    ('RIGHT', "Right", ""),
]
txt_width_items: list[tuple[TextWidthMode, str, str]] = [
    ('AUTO', "Follow Node", "Width automatically follows node width"),
    ('FIT', "Fit Content", "Width automatically fits text content"),
    ('MANUAL', "Manual", "Manually set width"),
    ('KEEP', "Screen Space", "Keep constant size in screen space"),
]
img_width_items: list[tuple[ImageWidthMode, str, str]] = [
    ('AUTO', "Follow Node", "Width automatically follows node width"),
    ('ORIGINAL', "Original Width", "Display image at original width"),
    ('MANUAL', "Manual", "Manually set width"),
    ('KEEP', "Screen Space", "Keep constant size in screen space"),
]
badge_width_items: list[tuple[BadgeScaleMode, str, str]] = [
    ('RELATIVE', "Relative Scale", "Follow node editor zoom"),
    ('ABSOLUTE', "Screen Space", "Fixed screen pixel size"),
]

def tag_redraw(self, context: Context):
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'NODE_EDITOR':
                area.tag_redraw()

def _sync_note_default_props(self, context: Context):
    from . import node_properties  # 延迟导入避免循环依赖
    node_properties.refresh_node_defaults()
    tag_redraw(self, context)

sort_mode_items: list[tuple[str, str, str]] = [
    ('COLOR_BADGE', "Color + Index", "Sort by color(RGB) ascending, then by index ascending"),
    ('BADGE_COLOR', "Index + Color", "Sort by index ascending, then by color(RGB) ascending"),
    ('POSITION', "Position", "Text notes first, sorted by node position (top-left to bottom-right)"),
]

class NodeNoteAddonPreferences(PropertyGroup):

    # 1. Global Settings
    cursor_warp_x          : IntProperty(default=0, min=1, max=500, description="Shortcut key cursor offset")
    panel_width            : IntProperty(name="Panel Width", default=220, min=100, max=2000, description="Width of the shortcut panel")
    show_all_notes         : BoolProperty(name="Show All", default=True)
    show_selected_only     : BoolProperty(name="Show Selected Only", default=False, description="Only show notes of selected nodes")
    follow_overlay         : BoolProperty(name="Follow Overlay", default=False, description="Whether to hide notes when node editor overlay is closed")
    show_badge_lines       : BoolProperty(name="Show Connection Lines", default=False, description="Show index lines between nodes")
    is_interactive_mode    : BoolProperty(name="Interactive Mode", default=False, description="Click nodes to number, right-click or ESC to exit")
    list_sort_mode         : EnumProperty(name="Sort Mode", items=sort_mode_items, default='BADGE_COLOR', description="Choose list sort method")
    use_occlusion          : BoolProperty(name="Auto Occlusion", default=False)
    tag_mode_prepend       : BoolProperty(name="Prepend Mode", default=True, description="Add special characters before existing text")
    navigator_search       : StringProperty(name="Search", default="", options={'TEXTEDIT_UPDATE'})
    # 兼容5.2之前
    if bpy.app.version >= (5,2,0):
        line_separator     : StringProperty(name="Line Separator", default=";|\\", description="Before blender5.2, line break separator in text, supports multiple (separated by |), e.g.: ;|\\")
    else:
        line_separator     : StringProperty(name="Line Separator", default=";|\\", options={'TEXTEDIT_UPDATE'}, description="Before blender5.2, line break separator in text, supports multiple (separated by |), e.g.: ;|\\")

    hide_text_panel    : BoolProperty(name="Hide Text Panel", default=True, description="Default hide text note panel")
    hide_image_panel   : BoolProperty(name="Hide Image Panel", default=True, description="Default hide image note panel")
    hide_badge_panel   : BoolProperty(name="Hide Index Panel", default=True, description="Default hide index note panel")

    # 2. 文本设置区
    default_font_size      : IntProperty(name="Default Font Size", default=8, min=4, max=100, update=_sync_note_default_props)
    font_path              : StringProperty(default='', subtype='FILE_PATH', options={'TEXTEDIT_UPDATE'}, description="Font file path, leave empty to use default font")
    bg_rect_roundness      : FloatProperty(name="Roundness", default=0.35, min=0, max=1, description="Roundness of note background rectangle")
    default_text_color     : FloatVectorProperty(name="Default Text Color", subtype='COLOR', size=4, default=(1.0, 1.0, 1.0, 1.0), min=0, max=1, update=_sync_note_default_props)
    default_txt_bg_color   : FloatVectorProperty(name="Default Background Color", subtype='COLOR', size=4, default=(0.2, 0.3, 0.5, 0.9), min=0, max=1, update=_sync_note_default_props)

    default_txt_width_mode : EnumProperty(name="Width Mode", items=txt_width_items, default=0, update=_sync_note_default_props)
    default_txt_bg_width   : IntProperty(name="Default Text Background Width", default=200, min=50, max=2000, update=_sync_note_default_props)
    default_txt_pos        : EnumProperty(name="Default Alignment", items=align_items, default='TOP', update=_sync_note_default_props)

    default_img_width_mode : EnumProperty(name="Width Mode", items=img_width_items, default=0, update=_sync_note_default_props)
    default_img_width      : IntProperty(name="Default Image Width", default=300, min=10, max=4000, update=_sync_note_default_props)
    default_img_pos        : EnumProperty(name="Default Alignment", items=align_items, default='TOP', update=_sync_note_default_props)

    default_badge_color    : FloatVectorProperty(name="Circle Background Color", subtype='COLOR', size=4, default=(0.8, 0.1, 0.1, 1.0), min=0, max=1, update=_sync_note_default_props)
    badge_scale_mode       : EnumProperty(name="Scale Mode", items=badge_width_items, default='ABSOLUTE')
    badge_rel_scale        : FloatProperty(name="Index Scale", default=1.0, min=0.1, max=20.0)
    badge_abs_scale        : FloatProperty(name="Screen Space Scale", default=1.0, min=0.1, max=10.0)
    badge_font_color       : FloatVectorProperty(name="Number Color", subtype='COLOR', size=4, default=(1.0, 1.0, 1.0, 1.0), min=0, max=1, description="Number color for all indexes")
    badge_line_color       : FloatVectorProperty(name="Line Color", subtype='COLOR', size=4, default=(1.0, 0.8, 0.2, 0.8), min=0, max=1)
    badge_line_thickness   : IntProperty(name="Line Width", default=4, min=1, max=40)

    # 预设颜色
    col_preset_1           : FloatVectorProperty(name="Preset Red", subtype='COLOR', size=4, default=(0.6, 0.1, 0.1, 0.9), min=0, max=1)
    col_preset_2           : FloatVectorProperty(name="Preset Green", subtype='COLOR', size=4, default=(0.2, 0.5, 0.2, 0.9), min=0, max=1)
    col_preset_3           : FloatVectorProperty(name="Preset Blue", subtype='COLOR', size=4, default=(0.2, 0.3, 0.5, 0.9), min=0, max=1)
    col_preset_4           : FloatVectorProperty(name="Preset Orange", subtype='COLOR', size=4, default=(0.8, 0.35, 0.05, 0.9), min=0, max=1)
    col_preset_5           : FloatVectorProperty(name="Preset Purple", subtype='COLOR', size=4, default=(0.4, 0.1, 0.5, 0.9), min=0, max=1)
    col_preset_6           : FloatVectorProperty(name="Preset None", subtype='COLOR', size=4, default=(0.0, 0.0, 0.0, 0.0), min=0, max=1)

    label_preset_1         : StringProperty(name="Label 1", default="Red")
    label_preset_2         : StringProperty(name="Label 2", default="Green")
    label_preset_3         : StringProperty(name="Label 3", default="Blue")
    label_preset_4         : StringProperty(name="Label 4", default="Orange")
    label_preset_5         : StringProperty(name="Label 5", default="Purple")
    label_preset_6         : StringProperty(name="Label 6", default="None")

    show_global      : BoolProperty(name="Show Global Settings", default=True)
    show_text        : BoolProperty(name="Show Text Notes", default=True)
    show_image       : BoolProperty(name="Show Image Notes", default=True)
    show_badge       : BoolProperty(name="Show Index Notes", default=True)
    show_list        : BoolProperty(name="Show Notes List", default=True)

    show_red             : BoolProperty(name="Show Red", default=True, description="Show by preset color")
    show_green           : BoolProperty(name="Show Green", default=True, description="Show by preset color")
    show_blue            : BoolProperty(name="Show Blue", default=True, description="Show by preset color")
    show_orange          : BoolProperty(name="Show Orange", default=True, description="Show by preset color")
    show_purple          : BoolProperty(name="Show Purple", default=True, description="Show by preset color")
    show_other           : BoolProperty(name="Show Others", default=True, description="Show Others")
    hide_img_by_bg       : BoolProperty(name="Also Filter Images", default=True, description="Filter images when filtering text")

    def draw(self, context):
        layout = self.layout
        layout.label(text="Plugin Location: Right-click Menu + NPanel->Node->Node Note", icon='INFO')
        layout.label(text="Saving v5.0+ files in v4.x results in missing notes when reopening in v5.x.", icon='WARNING_LARGE')
        layout.label(text="Versions within 4.x are fully compatible. v5.0 can load notes saved in v4.x.", icon='CHECKMARK')
        box = layout.box()
        row = box.row()
        row.label(text="Global", icon='WORLD_DATA')
        row.prop(self, "follow_overlay", toggle=True, icon='OVERLAY')
        col = box.column()
        split = col.split(factor=0.33)
        split0 = split.row()
        split0.label(text="Panel Shortcut Key:", icon='KEYINGSET')
        row_hide = col.row()
        row_hide.prop(self, "hide_text_panel")
        row_hide.prop(self, "hide_image_panel")
        row_hide.prop(self, "hide_badge_panel")

        wm = context.window_manager
        kc = wm.keyconfigs.user
        km = kc.keymaps.get('Node Editor')
        if km:
            kmi = None
            for k in km.keymap_items:
                if k.idname == 'node.note_quick_edit':
                    kmi = k
                    break
            if kmi:
                split0.prop(kmi, "type", text="", full_event=True)
            else:
                split0.label(text="Shortcut key not registered", icon='ERROR')
        split1 = split.row()
        split1.prop(self, "panel_width", text="Shortcut Panel Width")
        split1.prop(self, "cursor_warp_x", text="Default Mouse Offset")

        layout.label(text="Some property changes require Blender restart to take effect", icon='INFO')

        # region Text Notes
        txt_box = layout.box()
        txt_box.label(text="Text Notes Default Settings", icon='FILE_TEXT')

        split_txt = txt_box.split(factor=0.33)
        split_txt.prop(self, "default_font_size", text="Font Size")
        split_txt.prop(self, "font_path", text="Font")
        # 兼容5.2之前
        split_txt.prop(self, "line_separator", text="Line Separator")

        split_color = txt_box.split(factor=0.33)
        split_color.row().prop(self, "default_text_color", text="Text")
        split_color.row().prop(self, "default_txt_bg_color", text="Background")
        split_color.prop(self, "bg_rect_roundness")

        row = txt_box.row()
        row.prop(self, "default_txt_width_mode", text="Width")
        if self.default_txt_width_mode == 'MANUAL':
            row.prop(self, "default_txt_bg_width", text="")
        row.prop(self, "default_txt_pos", text="Alignment")

        txt_box.label(text="Background Presets:")
        box_preset = txt_box.box()
        split_preset = box_preset.split(factor=0.05)
        split_preset.label(text="")
        split_preset = split_preset.column()
        grid_color = split_preset.grid_flow(row_major=True, columns=6, align=True)
        for i in range(1, 7):
            grid_color.prop(self, f"col_preset_{i}", text="")

        grid_label = split_preset.grid_flow(row_major=True, columns=6, align=True)
        for i in range(1, 7):
            grid_label.prop(self, f"label_preset_{i}", text="")
        # endregion

        # region Image Notes
        box = layout.box()
        box.label(text="Image Notes Default Settings", icon='IMAGE_DATA')

        col = box.column()
        row = col.row()

        row.prop(self, "default_img_width_mode", text="Width")
        if self.default_img_width_mode in {'MANUAL', 'KEEP'}:
            row.prop(self, "default_img_width", text="")
        row.prop(self, "default_img_pos", text="Alignment")
        # endregion

        # region Index Notes
        badge_box = layout.box()
        badge_box.label(text="Index Notes Default Settings", icon='EVENT_NDOF_BUTTON_1')

        split = badge_box.split(factor=0.5)
        split.row().prop(self, "default_badge_color", text="Background")
        split.row().prop(self, "badge_font_color", text="Text")

        row_set = badge_box.row(align=True)
        row_set.prop(self, "badge_scale_mode", text="Scale")
        if self.badge_scale_mode == 'ABSOLUTE':
            row_set.prop(self, "badge_abs_scale", text="Screen Scale")
        else:
            row_set.prop(self, "badge_rel_scale", text="Relative Scale")

        row_set = badge_box.split(factor=0.3)
        row_set.prop(self, "show_badge_lines", text="Lines", icon="GRAPH")
        row_set.row().prop(self, "badge_line_color", text="Color")
        row_set.prop(self, "badge_line_thickness", text="Line Width")
        # endregion

def pref() -> NodeNoteAddonPreferences:
    if hasattr(bpy.context, "scene") and bpy.context.scene is not None:
        if hasattr(bpy.context.scene, "node_note_prefs"):
            return bpy.context.scene.node_note_prefs
            
    class Fallback:
        default_text_color = (1.0, 1.0, 1.0, 1.0)
        default_txt_bg_color = (0.1, 0.1, 0.1, 1.0)
        default_badge_color = (1.0, 0.5, 0.0, 1.0)
        default_font_size = 14
        default_txt_bg_width = 100
        default_img_width = 100
        default_txt_pos = 'ABOVE'
        default_img_pos = 'ABOVE'
        def __getattr__(self, name):
            if "color" in name: return (1.0, 1.0, 1.0, 1.0)
            if "width" in name or "size" in name: return 100
            if "pos" in name: return 'ABOVE'
            return True
            
    return Fallback()