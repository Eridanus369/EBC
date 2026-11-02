import bpy
from bpy.types import Panel, UILayout, Context, Menu, Operator, Image, SpaceNodeEditor
from .preferences import pref
from .utils import text_split_lines, nd_abs_loc
from .nn_typing import NotedNode
from . import operators as ops
from bpy.app.translations import pgettext_iface as iface

class NODE_PT_node_note_gpu_panel(Panel):
    bl_label = "Node Note"
    bl_idname = "NODE_PT_node_note_gpu_panel"
    bl_space_type = 'NODE_EDITOR'
    bl_region_type = 'UI'
    bl_category = "View"

    @classmethod
    def poll(cls, context):
        space = context.space_data
        return space.type == 'NODE_EDITOR' and space.edit_tree

    def draw(self, context):
        layout = self.layout
        draw_panel(layout, context)

def _general_box(body: UILayout, active = True, factor=0.01, box=True):
    body.active = active
    body_split = body.split(factor=factor)
    body_split.label(text="")
    return body_split.box() if box else body_split

def _draw_panel_header(header: UILayout, hide_op: Operator, icon: str, label: str, show: bool, factor: float = 0.01) -> UILayout:
    header.label(text="", icon=icon)
    header.operator(hide_op.bl_idname, text="", icon="HIDE_OFF" if show else "HIDE_ON")
    header_split = header.split(factor=factor, align=False)
    header_split.label(text="")
    # header_split.operator(hide_op.bl_idname, text="", icon="HIDE_OFF" if show else "HIDE_ON")
    return header_split

def draw_panel(layout: UILayout, context: Context, show_global=True, show_text=True, show_image=True, show_badge=True, show_list=True):
    prefs = pref()
    space: SpaceNodeEditor = bpy.context.space_data  # type: ignore
    if prefs.follow_overlay and not space.overlay.show_overlays:
        layout.label(text="Need turn on Overlay or off Follow", icon="INFO")

    col_vals = []
    def_labels = ["Red", "Green", "Blue", "Orange", "Purple", "None"]
    for i in range(len(def_labels)):
        col_vals.append(getattr(prefs, f"col_preset_{i+1}"))
        custom_lbl = getattr(prefs, f"label_preset_{i+1}", "")
        if custom_lbl:
            def_labels[i] = custom_lbl

    if show_global:
        header, body = layout.panel("setting", default_closed=True)
        header.label(text="Global", icon='TOOL_SETTINGS')
        header.prop(prefs, "show_all_notes", text="All", icon="HIDE_OFF" if prefs.show_all_notes else "HIDE_ON", toggle=True)
        layout.active = prefs.show_all_notes
        select_row = header.row()
        select_row.prop(prefs, "show_selected_only", text="Selected", toggle=True)
        select_row.active = prefs.show_all_notes
        header.operator(ops.NODE_OT_note_copy_active_style.bl_idname, text="", icon='BRUSH_DATA')
        header.operator(ops.NODE_OT_note_delete_notes.bl_idname, text="", icon='TRASH')
        if body:
            box = _general_box(body)
            row_pos = box.row()
            row_pos.operator("preferences.addon_show", text="Preferences", icon='PREFERENCES').module = __package__
            row_pos.prop(prefs, "follow_overlay", toggle=True, icon='OVERLAY')
            split_txt = box.split(factor=0.5)
            split_txt.prop(prefs, "font_path", text="Font")
            if bpy.app.version < (5,2,0):
                split_txt.prop(prefs, "line_separator", text="Line Separator")
            else:
                sep = split_txt.split(factor=0.4)
                sep.prop(prefs, "line_separator", text="")
                sep.operator(ops.NODE_OT_note_migrate_text_separator.bl_idname, icon='FILE_REFRESH')

            # # todo 还很不完善
            # row_pos.prop(prefs, "use_occlusion", text="Hide when occluded")

            if prefs.show_all_notes:
                row = box.row()
                row.prop(prefs, "hide_img_by_bg", text="", icon='FILTER')
                color = row.row(align=True)
                color.prop(prefs, "show_red", text=def_labels[0], toggle=True)
                color.prop(prefs, "show_green", text=def_labels[1], toggle=True)
                color.prop(prefs, "show_blue", text=def_labels[2], toggle=True)
                color.prop(prefs, "show_orange", text=def_labels[3], toggle=True)
                color.prop(prefs, "show_purple", text=def_labels[4], toggle=True)
                color.prop(prefs, "show_other", text="Others", toggle=True)

    node = getattr(context, "active_node", None)
    if node is None:
        space = getattr(context, "space_data", None)
        if space and getattr(space, "type", "") == 'NODE_EDITOR' and getattr(space, "edit_tree", None):
            node = space.edit_tree.nodes.active
    node: NotedNode = node  # type: ignore
    if node:
        if show_text:
            header, body = layout.panel("setting1", default_closed=prefs.hide_text_panel)
            h_split = _draw_panel_header(header, ops.NODE_OT_note_show_selected_txt, 'FILE_TEXT', "Text", node.note_show_txt)
            h_split.prop(node, "note_text", text="")
            if bpy.app.version < (5, 2, 0):
                header.operator(ops.NODE_OT_note_paste_text_from_clipboard.bl_idname, text="", icon='PASTEDOWN')
                header.operator(ops.NODE_OT_note_copy_text_to_clipboard.bl_idname, text="", icon='COPYDOWN')
            header.operator(ops.NODE_OT_note_text_from_node_label.bl_idname, text="", icon='AUTOMERGE_ON')
            header.operator(ops.NODE_OT_note_delete_selected_txt.bl_idname, text="", icon='TRASH')
            if body:
                txt_box = _general_box(body, node.note_show_txt, box=False)
                txt_row = txt_box.row()
                if bpy.app.version >= (5, 2, 0):
                    txt_row.textbox(node, "note_text")
                else:
                    txt_row.prop(node, "note_text", text="")

                sub_header, sub_body = body.panel("sub_setting1", default_closed=prefs.hide_text_panel)
                split_txt = sub_header.split(factor=0.4)
                split_txt.prop(node, "note_font_size", text="Font Size")
                split_txt.row().prop(node, "note_text_color", text="Text")
                split_txt.row().prop(node, "note_txt_bg_color", text="Background")
                if sub_body:
                    sub_body = _general_box(sub_body, node.note_show_txt, factor=0.05)
                    row_pos = sub_body.row()
                    row_pos.label(text="Background:")
                    for i in range(len(def_labels)):
                        split_color = row_pos.split(factor=0.1, align=True)
                        split_color.prop(prefs, f"col_preset_{i+1}", text="")
                        op = split_color.operator(ops.NODE_OT_note_apply_preset.bl_idname, text=def_labels[i])
                        op.bg_color = col_vals[i]

                    row_tag = sub_body.row(align=True)
                    row_tag.prop(prefs,
                                "tag_mode_prepend",
                                text="",
                                icon='ALIGN_LEFT' if prefs.tag_mode_prepend else 'ALIGN_RIGHT',
                                toggle=True)
                    for tag in ["★", "⚠", "?", "!"]:
                        op = row_tag.operator(ops.NODE_OT_note_add_quick_tag.bl_idname, text=tag)
                        op.tag_text = tag

                    width_row = sub_body.row(align=True)
                    width_row.prop(node, "note_txt_width_mode", text="Width")
                    if node.note_txt_width_mode == 'MANUAL':
                        width_row.prop(node, "note_txt_bg_width", text="Width")

                    row_pos = sub_body.row(align=True)
                    row_pos.prop(node, "note_txt_pos", text="")
                    if node.note_txt_pos in ('TOP', 'BOTTOM'):
                        row_pos.prop(node, "note_txt_center", text="Center", toggle=True)
                    row_pos.prop(node, "note_txt_offset", text="")
                    op = row_pos.operator(ops.NODE_OT_note_reset_offset.bl_idname, text="", icon='LOOP_BACK')
                    op.is_txt = True

        if show_image:
            header, body = layout.panel("setting2", default_closed=prefs.hide_image_panel)
            h_split = _draw_panel_header(header, ops.NODE_OT_note_show_selected_img, 'IMAGE_DATA', "Image", node.note_show_img)
            h_split.operator(ops.NODE_OT_note_paste_image.bl_idname, text="", icon='PASTEDOWN')
            if node.note_show_txt and node.note_text and node.note_show_img and node.note_image:
                h_split.operator(ops.NODE_OT_note_note_swap_order.bl_idname, text="⇅")
            if node.note_image:
                header.operator(ops.NODE_OT_note_open_image_editor.bl_idname, text="", icon='IMAGE')
            header.operator(ops.NODE_OT_note_delete_selected_img.bl_idname, text="", icon='TRASH')
            if body:
                img_box = _general_box(body, node.note_show_img)
                img_split = img_box.split(factor=0.01)
                img_split.label(text="")
                img_preview = img_split.box()
                sub_header, sub_body = img_preview.panel("setting_sub", default_closed=True)
                sub_header.template_ID(node, "note_image", open="image.open")
                if sub_body:
                    sub_body.template_ID_preview(node, "note_image", rows=4, cols=4, filter="AVAILABLE", hide_buttons=True)
                    if node.note_image and node.note_image.packed_file is None:  # type: ignore
                        img_filepath = node.note_image.filepath
                        if img_filepath:
                            abs_path = bpy.path.abspath(img_filepath)
                            sub_body.label(text=abs_path, icon='FILE_IMAGE')
                    row_img = sub_body.row()
                    op_unpack = row_img.operator(ops.NODE_OT_note_pack_unpack_images.bl_idname, text="Unpack", icon='PACKAGE')
                    op_unpack.is_pack = False
                    op_pack = row_img.operator(ops.NODE_OT_note_pack_unpack_images.bl_idname, text="Pack", icon='UGLYPACKAGE')
                    op_pack.is_pack = True
                    # row_img.operator(ops.NODE_OT_note_delete_selected_img.bl_idname, text="Remove", icon='TRASH')

                width_row = img_box.row(align=True)
                width_row.prop(node, "note_img_width_mode", text="Width")
                if node.note_img_width_mode in {'MANUAL', 'KEEP'}:
                    width_row.prop(node, "note_img_width", text="Width")

                row_pos = img_box.row(align=True)
                row_pos.prop(node, "note_img_pos", text="")
                if node.note_img_pos in ('TOP', 'BOTTOM'):
                    row_pos.prop(node, "note_img_center", text="Center", toggle=True)
                row_pos.prop(node, "note_img_offset", text="")
                op = row_pos.operator(ops.NODE_OT_note_reset_offset.bl_idname, text="", icon='LOOP_BACK')
                op.is_txt = False

        if show_badge:
            header, body = layout.panel("setting3", default_closed=prefs.hide_badge_panel)
            h_split = _draw_panel_header(header, ops.NODE_OT_note_show_selected_badge, 'EVENT_NDOF_BUTTON_1', "Index", node.note_show_badge)
            h_split.prop(node, "note_badge_index", text="")
            h_split.operator(ops.NODE_OT_note_interactive_badge.bl_idname,
                             text="",
                             icon='STYLUS_PRESSURE',
                             depress=prefs.is_interactive_mode)
            header.operator(ops.NODE_OT_note_delete_selected_badge.bl_idname, text="", icon='TRASH')
            if body:
                badge_box = _general_box(body, node.note_show_badge)
                split = badge_box.split(factor=0.4)
                split.prop(node, "note_badge_index", text="Index")
                split.row().prop(prefs, "badge_font_color", text="Text")
                split.row().prop(node, "note_badge_color", text="Background")

                row_set = badge_box.row(align=True)
                row_set.prop(prefs, "badge_scale_mode", text="Scale")
                if prefs.badge_scale_mode == 'ABSOLUTE':
                    row_set.prop(prefs, "badge_abs_scale", text="Scale")
                else:
                    row_set.prop(prefs, "badge_rel_scale", text="Scale")

                row_set = badge_box.split(factor=0.3)
                row_set.prop(prefs, "show_badge_lines", text="Lines", icon="GRAPH")
                row_set.row().prop(prefs, "badge_line_color", text="Color")
                row_set.prop(prefs, "badge_line_thickness", text="Line Width")
    else:
        layout.label(text="Active node required", icon='INFO')

    if show_list:
        search_key = prefs.navigator_search.strip().lower()
        filter_noted_nodes: list[NotedNode] = []
        all_notes_count = 0

        for node in context.space_data.edit_tree.nodes:
            if node.note_text.strip() or node.note_badge_index > 0 or node.note_image:
                all_notes_count += 1
                if search_key and (search_key not in node.note_text.lower()):
                    continue
                filter_noted_nodes.append(node)

        header, body = layout.panel("setting4", default_closed=True)
        header.label(text=iface("List ({count})").format(count=all_notes_count), icon="ALIGN_JUSTIFY")
        header.prop(prefs, "list_sort_mode", text="")
        if body:
            draw_search_list(body, context, filter_noted_nodes, all_notes_count)

def draw_search_list(layout: UILayout, context: Context, filter_noted_nodes: list[NotedNode], all_notes_count: int):
    prefs = pref()
    row = layout.row(align=True)
    row.prop(prefs, "navigator_search", text="", icon='VIEWZOOM')

    list_sort_mode = prefs.list_sort_mode
    if list_sort_mode == 'COLOR_BADGE':
        filter_noted_nodes.sort(key=lambda node: (
            node.note_txt_bg_color[:3] if node.note_text else (2, 2, 2),
            node.note_badge_index == 0,
            node.note_badge_index,
        ))
    elif list_sort_mode == 'BADGE_COLOR':
        filter_noted_nodes.sort(key=lambda node: (
            node.note_badge_index == 0,
            node.note_badge_index,
            node.note_txt_bg_color[:3] if node.note_text else (2, 2, 2),
        ))
    elif list_sort_mode == 'POSITION':
        filter_noted_nodes.sort(key=lambda node: (
            not node.note_text,
            nd_abs_loc(node)[0] - nd_abs_loc(node)[1],
            node.note_badge_index,
        ))
    if not filter_noted_nodes and all_notes_count > 0:
        box = layout.box()
        col = box.column(align=True)
        col.label(text="No matching items found", icon='FILE_SCRIPT')
        return

    if all_notes_count == 0:
        layout.label(text="No notes yet", icon='INFO')
        return

    list_sort_mode = prefs.list_sort_mode
    if prefs.navigator_search.strip().lower():
        sort_icon = 'FILTER'
    elif list_sort_mode == 'BADGE_COLOR':
        sort_icon = 'SORT_ASC'
    elif list_sort_mode == 'POSITION':
        sort_icon = 'GRID'
    else:
        sort_icon = 'COLOR'

    layout.label(text=iface("List ({count})").format(count=len(filter_noted_nodes)), icon=sort_icon)
    col = layout.column()
    header: UILayout
    body: UILayout
    for index, node in enumerate(filter_noted_nodes):
        header, body = layout.panel(f"note_{index}_{node.name}", default_closed=True)
        row = header.row(align=True)
        split1 = row.split(factor=0.1)
        split_color = split1.row()
        split2 = split1.split(factor=0.5)
        split_text = split2.row()
        if node == context.active_node:
            split_text.alert = True
        split3 = split2.split(factor=0.75)
        split_img_name = split3.row()
        split_badge = split3.row()

        if node.note_text:
            split_color.prop(node, "note_txt_bg_color", text="")
        else:
            split_color.label(text="None")

        badge_idx = node.note_badge_index
        txt_content = node.note_text.strip()

        display_text = "No Text"
        split_lines: list[str] = []
        if txt_content:
            split_lines = text_split_lines(txt_content)
            display_text = split_lines[0]
        op = split_text.operator(ops.NODE_OT_note_jump_to_note.bl_idname, text=display_text, icon='VIEW_ZOOM', emboss=True)
        op.node_name = node.name

        image: Image | None = node.note_image
        split_img_name.label(text=image.name if image else "No Image")
        split_badge.label(text=f"{badge_idx:0>2}" if badge_idx else "None")

        if body:
            body_split = body.split(factor=0.03)
            body_split.label(text="")
            note_box = body_split.box()
            note_col = note_box.column(align=True)
            row_node = note_col.row()
            row_node.label(text=iface("Node:  {name}").format(name=node.name), icon='NODE')
            # note_col.label(text="Node:  {name}".format(name=node.name), icon='NODE')
            # note_col.label(text=f"Node:  {node.name}", icon='NODE')
            row_img = note_col.row()
            row_img.label(text=iface("Image:  {name}").format(name=image.name if image else iface("None")), icon='IMAGE_DATA')

            if image:
                img_split = note_col.split(factor=0.03)
                img_split.label(text="")
                img_box = img_split.box()
                img_box.template_ID_preview(node, "note_image", open="image.open", rows=4, cols=4, filter="AVAILABLE")

            row_text = note_col.column()
            if split_lines:
                row_text0 = row_text.split(factor=0.03)
                row_text0.label(text="", icon='FILE_TEXT')
                if bpy.app.version >= (5,2,0):
                    row_text0.textbox(node, "note_text")
                else:
                    row_text0.prop(node, "note_text", text="")
                for line in split_lines:
                    row_text.label(text=" "*10 + line)
            else:
                row_text.label(text="Text:  None", icon='FILE_TEXT')

def draw_panel_for_shortcut(layout: UILayout, context: Context):
    row = layout.row()
    prefs = pref()
    row.prop(prefs, "show_global", text="", icon="TOOL_SETTINGS")
    row.prop(prefs, "show_text", text="", icon="FILE_TEXT")
    row.prop(prefs, "show_image", text="", icon="IMAGE_DATA")
    row.prop(prefs, "show_badge", text="", icon="EVENT_NDOF_BUTTON_1")
    row.prop(prefs, "show_list", text="", icon="ALIGN_JUSTIFY")
    draw_panel(layout,
               context,
               show_global=prefs.show_global,
               show_text=prefs.show_text,
               show_image=prefs.show_image,
               show_badge=prefs.show_badge,
               show_list=prefs.show_list)

def draw_to_context_menu(self: Menu, context: Context):
    layout = self.layout
    layout.operator_context = 'INVOKE_DEFAULT'
    layout.operator(ops.NODE_OT_note_note_quick_edit.bl_idname, text="Node Note", icon='TEXT')

def draw_to_overlay_panel(self: Panel, context):
    self.layout.prop(pref(), "show_all_notes", text="Node Note", icon='HIDE_OFF', toggle=True)
