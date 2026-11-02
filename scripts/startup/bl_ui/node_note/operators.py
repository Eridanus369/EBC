import bpy
from bpy.types import Operator, Node, Context, Image, UILayout, SpaceImageEditor
from typing import Callable
from bpy.props import EnumProperty, BoolProperty, IntProperty, FloatVectorProperty
from .preferences import pref, txt_width_items
from .node_properties import style_props
from .utils import import_clipboard_image, text_split_lines
from bpy.app.translations import pgettext_iface as iface

class NoteBaseOperator(Operator):
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        space = getattr(context, 'space_data', None)
        if space is None:
            print(f"[调试 poll] space_data 为空, context.type={getattr(context, 'type', 'N/A')}")
            return False
        if space.type != 'NODE_EDITOR':
            print(f"[调试 poll] space_data.type={space.type}, 应为 NODE_EDITOR")
            return False
        active = getattr(context, 'active_node', None)
        if active is None:
            print(f"[调试 poll] active_node 为空")
            return False
        return True

    def get_selected_nodes(self, context: Context, skip_active: bool = False) -> list[Node]:
        nodes = context.selected_nodes
        active = context.active_node
        if active and not active.select and not skip_active:
            nodes.append(active)
        return nodes

class NODE_OT_note_note_swap_order(NoteBaseOperator):
    bl_idname = "node.note_swap_order"
    bl_label = "Swap Text and Image Position"
    bl_description = "Swap text and image note order for selected nodes"

    def execute(self, context):
        nodes = self.get_selected_nodes(context)
        for node in nodes:
            align_txt = node.note_txt_pos
            align_img = node.note_img_pos
            if align_txt != align_img:
                node.note_txt_pos = align_img
                node.note_img_pos = align_txt
            else:
                node.note_swap_order = not node.note_swap_order
        context.area.tag_redraw()
        return {'FINISHED'}

class NODE_OT_note_delete_selected_txt(NoteBaseOperator):
    bl_idname = "node.note_delete_selected_txt"
    bl_label = "Delete Text (Multi-Select)"
    bl_description = "Delete text notes from selected nodes"

    def execute(self, context):
        for node in self.get_selected_nodes(context):
            if node.note_text:
                node.note_text = ""
        return {'FINISHED'}

class NODE_OT_note_delete_selected_badge(NoteBaseOperator):
    bl_idname = "node.note_delete_selected_badge"
    bl_label = "Delete Index (Multi-Select)"
    bl_description = "Delete index notes from selected nodes"

    def execute(self, context):
        for node in self.get_selected_nodes(context):
            if node.note_badge_index:
                node.note_badge_index = 0
        return {'FINISHED'}

class NoteScopeOperator(NoteBaseOperator):
    scope: EnumProperty(
        name="Scope",
        items=[
            ('SELECTED', "Selected Nodes", "Only nodes currently selected"),
            ('CURRENT', "Current Node Tree", "All nodes in the active node tree"),
            ('ALL', "All Node Trees", "All nodes in all node groups/trees"),
        ],
        default='SELECTED'
    )

    def get_scoped_nodes(self, context, skip_active: bool = False)-> list[Node]:
        if self.scope == 'SELECTED':
            return self.get_selected_nodes(context, skip_active)
        elif self.scope == 'CURRENT':
            space = context.space_data
            print(f"[调试 get_scoped_nodes] CURRENT: space={space}, edit_tree={getattr(space, 'edit_tree', 'N/A')}")
            if not space.edit_tree:
                print("[调试 get_scoped_nodes] 错误: edit_tree 为空!")
                return []
            return space.edit_tree.nodes
        else:            # 'ALL':
            nodes = []
            for tree in bpy.data.node_groups:
                nodes.extend(tree.nodes)
            return nodes
        
    def draw_scope(self, layout: UILayout):
        scope_row = layout.split(factor=0.3)
        scope_row.label(text="Operation Scope")
        scope_row.column().prop(self, "scope", expand=True)

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=300)

class NoteDeleteOperator(NoteScopeOperator):
    delete_image: BoolProperty(description="Also remove images from Blender file", default=False)

    def draw(self, context):
        self.layout.prop(self, "delete_image", text="Also remove images from Blender file")

class NODE_OT_note_delete_selected_img(NoteDeleteOperator):
    bl_idname = "node.note_delete_selected_img"
    bl_label = "Remove Image (Multi-Select)"
    bl_description = "Remove images corresponding to selected nodes' image notes from Blender file"

    def execute(self, context):
        for node in self.get_selected_nodes(context):
            if node.note_image:
                image = node.note_image
                node.note_image = None
                if self.delete_image:
                    bpy.data.images.remove(image)
        return {'FINISHED'}

class NODE_OT_note_delete_notes(NoteDeleteOperator):
    bl_idname = "node.note_delete_notes"
    bl_label = "Delete Notes"
    bl_description = "Delete notes with customizable scope and types"

    del_text: BoolProperty(name="Text", default=True)
    del_image: BoolProperty(name="Image", default=True)
    del_index: BoolProperty(name="Index", default=True)

    def draw(self, context):
        layout = self.layout
        row1 = layout.split(factor=0.3)
        row1.label(text="Operation Scope")
        row1.column().prop(self, "scope", expand=True)

        row2 = layout.split(factor=0.3)
        row2.label(text="Delete Types")
        row3 = row2.row()
        row3.prop(self, "del_text", toggle=True, text="Text")
        row3.prop(self, "del_image", toggle=True, text="Image")
        row3.prop(self, "del_index", toggle=True, text="Index")
        if self.del_image:
            layout.separator()
            layout.prop(self, "delete_image", text="Also Remove Image")

    def execute(self, context):
        nodes_to_process = self.get_scoped_nodes(context)
        images_to_remove = []
        for node in nodes_to_process:
            if self.del_text and node.note_text:
                node.note_text = ""
            if self.del_image and node.note_image:
                img = node.note_image
                node.note_image = None
                if self.delete_image:
                    images_to_remove.append(img)
            if self.del_index and node.note_badge_index:
                node.note_badge_index = 0

        if self.delete_image:
            for img in set(images_to_remove):
                try:
                    bpy.data.images.remove(img)
                except:
                    pass

        context.area.tag_redraw()
        return {'FINISHED'}

class NODE_OT_note_show_selected_txt(NoteBaseOperator):
    bl_idname = "node.note_show_selected_txt"
    bl_label = "Show Text (Multi-Select)"
    bl_description = "Show text notes of selected nodes"

    def execute(self, context):
        active_node = context.active_node
        new_value = not active_node.note_show_txt
        for node in self.get_selected_nodes(context):
            node.note_show_txt = new_value
        return {'FINISHED'}

class NODE_OT_note_show_selected_img(NoteBaseOperator):
    bl_idname = "node.note_show_selected_img"
    bl_label = "Show Image (Multi-Select)"
    bl_description = "Show image notes of selected nodes"

    def execute(self, context):
        active_node = context.active_node
        new_value = not active_node.note_show_img
        for node in self.get_selected_nodes(context):
            node.note_show_img = new_value
        return {'FINISHED'}

class NODE_OT_note_show_selected_badge(NoteBaseOperator):
    bl_idname = "node.note_show_selected_badge"
    bl_label = "Show Index (Multi-Select)"
    bl_description = "Show index notes of selected nodes"

    def execute(self, context):
        active_node = context.active_node
        new_value = not active_node.note_show_badge
        for node in self.get_selected_nodes(context):
            node.note_show_badge = new_value
        return {'FINISHED'}

class NODE_OT_note_apply_preset(NoteBaseOperator):
    bl_idname = "node.note_apply_preset"
    bl_label = "Apply Preset to Selected Nodes (Multi-Select)"
    bl_description = "Apply Preset to Selected Nodes (Multi-Select)"
    bg_color: bpy.props.FloatVectorProperty(size=4)

    def execute(self, context):
        for node in self.get_selected_nodes(context):
            node.note_txt_bg_color = self.bg_color
            node.note_text_color = (1, 1, 1, 1)
        return {'FINISHED'}

class NODE_OT_note_copy_active_style(NoteScopeOperator):
    bl_idname = "node.note_copy_active_style"
    bl_label = "Sync Active Node Note Style"

    # Text
    sync_font_size     : BoolProperty(name="Font Size", default=False)
    sync_text_color    : BoolProperty(name="Font Color", default=False)
    sync_txt_bg_color  : BoolProperty(name="Background Color", default=False)
    sync_txt_bg_width  : BoolProperty(name="Background Width", default=False)
    sync_txt_width_mode: BoolProperty(name="Width Mode", default=False)
    sync_txt_pos       : BoolProperty(name="Alignment Mode", default=False)
    sync_txt_center    : BoolProperty(name="Center", default=False)
    sync_txt_offset    : BoolProperty(name="Offset", default=False)

    # Image
    sync_img_width     : BoolProperty(name="Image Width", default=False)
    sync_img_width_mode: BoolProperty(name="Width Mode", default=False)
    sync_img_pos       : BoolProperty(name="Alignment Mode", default=False)
    sync_img_center    : BoolProperty(name="Center", default=False)
    sync_img_offset    : BoolProperty(name="Offset", default=False)

    # Index
    sync_badge_color   : BoolProperty(name="Background Color", default=False)

    def draw(self, context):
        layout = self.layout
        self.draw_scope(layout)

        row = layout.row()
        col1 = row.column(align=True)
        col1.label(text="Text")
        col2 = row.column(align=True)
        col2.label(text="Image")

        for prop in style_props:
            sync_name = prop.replace("note_", "sync_")
            if "txt" in prop or "text" in prop or "font" in prop:
                col1.prop(self, sync_name)
            elif "img" in prop:
                col2.prop(self, sync_name)

        col2.separator()
        col2.label(text="Index")
        col2.prop(self, "sync_badge_color")

    def execute(self, context):
        count = 0
        active = context.active_node
        print(f"[调试 execute] active={active}, type={type(active)}")
        if not active:
            self.report({'WARNING'}, "Active node required")
            return {'CANCELLED'}
        print(f"[调试 execute] 活动节点名称={getattr(active, 'name', 'N/A')}, 类型={active.bl_rna.name}")

        nodes = self.get_scoped_nodes(context, skip_active=True)
        for node in nodes:
            for prop in style_props:
                need_sync = getattr(self, prop.replace("note_", "sync_"))
                if not need_sync: continue
                try:
                    # 转接点宽度模式不支持跟随节点宽度(AUTO)
                    setattr(node, prop, getattr(active, prop))
                except TypeError:
                    continue
            count += 1

        self.report({'INFO'}, iface("Style synced to {count} nodes").format(count=count))
        context.area.tag_redraw()
        return {'FINISHED'}

class NODE_OT_note_add_quick_tag(NoteBaseOperator):
    bl_idname = "node.note_add_quick_tag"
    bl_label = "Tag"
    tag_text: bpy.props.StringProperty()

    def execute(self, context):
        for node in self.get_selected_nodes(context):
            node.note_text = (self.tag_text + " " + node.note_text) if pref().tag_mode_prepend else (node.note_text + " " + self.tag_text)
        return {'FINISHED'}

class NODE_OT_note_reset_offset(NoteBaseOperator):
    bl_idname = "node.note_reset_offset"
    bl_label = "Reset Offset (Multi-Select)"
    is_txt: BoolProperty()

    def execute(self, context):
        for node in self.get_selected_nodes(context):
            if self.is_txt:
                node.note_txt_offset = (0, 0)
            else:
                node.note_img_offset = (0, 0)
        return {'FINISHED'}

class NODE_OT_note_paste_image(NoteBaseOperator):
    bl_idname = "node.note_paste_image"
    bl_label = "Paste Image"
    bl_description = "Paste Image from Clipboard"

    def execute(self, context):
        active_node = context.active_node
        image = import_clipboard_image()
        if image:
            image.use_fake_user = False
            active_node.note_image = image
            active_node.note_show_img = True
            self.report({'INFO'}, iface("Successfully imported image: {image_name}").format(image_name=image.name))
            return {'FINISHED'}
        else:
            self.report({'WARNING'}, "Clipboard is empty")
            return {'CANCELLED'}

class NODE_OT_note_pack_unpack_images(NoteDeleteOperator):
    bl_idname = "node.note_pack_unpack_images"
    bl_label = "Pack/Unpack Images"
    bl_options = {'REGISTER', 'UNDO'}

    is_pack: BoolProperty(name="Pack Mode", default=False)
    unpack_method: EnumProperty(
        name="Unpack Method",
        items=[
            ('USE_LOCAL', "Use Current Directory", "Create files in blend file directory (no overwrite)", 'FILE_FOLDER', 0),
            ('WRITE_LOCAL', "Write to Current Directory", "Write files to blend file directory (overwrite)", 'EXPORT', 1),
            ('USE_ORIGINAL', "Use Original Location", "Create files in original image directory (no overwrite)", 'FILE', 2),
            ('WRITE_ORIGINAL', "Write to Original Location", "Write files to original image directory (overwrite)", 'EXPORT', 3),
        ],
        default='USE_LOCAL',
    )

    @classmethod
    def description(cls, context, properties):
        if properties.is_pack:
            return iface("External images will be packed into Blend file")
        else:
            return iface("Images will be unpacked to original or current directory")

    def draw(self, context):
        layout = self.layout
        self.draw_scope(layout)
        if not self.is_pack:
            row2 = layout.split(factor=0.3)
            row2.label(text="Unpack Method")
            row2.column().prop(self, "unpack_method", expand=True)

    def execute(self, context):
        nodes = self.get_scoped_nodes(context)
        if self.is_pack:
            return self._pack_images(nodes)
        else:
            return self._unpack_images(nodes)

    def _unpack_images(self, nodes: list[Node]):
        if not bpy.data.filepath:
            self.report({'WARNING'}, "Please save Blend file first")
            return {'CANCELLED'}
        def process(img: Image):
            img.save()
            bpy.ops.file.unpack_item(method=self.unpack_method, id_name=img.name)
        return self._process_images(nodes, process)

    def _pack_images(self, nodes: list[Node]):
        return self._process_images(nodes, process=lambda img: img.pack())

    def _process_images(self, nodes: list[Node], process: Callable[[Image], None]):
        pack_status = self.is_pack
        count = found = valid = 0
        action = "Pack" if pack_status else "Unpack"
        status_name = "External images" if pack_status else "Already packed"

        for node in nodes:
            if not node.note_image:
                continue

            found += 1
            image: Image = node.note_image
            is_packed = bool(image.packed_file)
            if is_packed == pack_status:
                continue
            valid += 1
            try:
                process(image)
                count += 1
            except Exception as e:
                print(f"Cannot {action} image {image.name}: {e}")

        if count > 0:
            self.report({'INFO'}, iface("{action}ed {count} images").format(action=iface(action), count=count))
            # 强制重绘所有区域，确保外部/打包图标状态同步更新
            bpy.context.area.tag_redraw()
        else:
            self.report({'WARNING'}, iface("No images found to {action} (found: {found}, {status_name}: {valid})").format(action=iface(action), found=found, status_name=iface(status_name), valid=valid))

        return {'FINISHED'}

class NODE_OT_note_jump_to_note(Operator):
    bl_idname = "node.note_jump_to_note"
    bl_label = "Jump to Node with Note"
    bl_description = "Jump to the node containing the note"
    node_name: bpy.props.StringProperty()

    def execute(self, context):
        nodes = context.space_data.edit_tree.nodes
        target_node = nodes[self.node_name]
        for node in nodes:
            node.select = False
        target_node.select = True
        nodes.active = target_node
        bpy.ops.node.view_selected()
        return {'FINISHED'}

class NODE_OT_note_open_image_editor(NoteBaseOperator):
    bl_idname = "node.note_open_image_editor"
    bl_label = "Open Image Editor"
    bl_description = "View image in floating image editor"

    def execute(self, context):
        node = context.active_node

        # Use render resolution to control window size
        render = context.scene.render
        view = context.preferences.view

        # Save original settings
        old_res_x = render.resolution_x
        old_res_y = render.resolution_y
        old_res_percent = render.resolution_percentage
        old_display_type = view.render_display_type

        try:
            # Set target resolution to fit image within half window size
            # Calculate scale based on area (target: 1/4 of window area)
            image_width, image_height = node.note_image.size
            window_width = context.window.width
            window_height = context.window.height

            target_area = (window_width*window_height) / 4
            image_area = image_width * image_height
            scale = (target_area / image_area)**0.5
            scale = min(scale, (window_width*0.8) / image_width, (window_height*0.8) / image_height)

            res_x = int(image_width * scale)
            res_y = int(image_height * scale)
            render.resolution_x = res_x
            render.resolution_y = res_y
            render.resolution_percentage = 100
            view.render_display_type = 'WINDOW'

            # Open window
            bpy.ops.render.view_show('INVOKE_DEFAULT')

            # Configure new window
            new_window = context.window_manager.windows[-1]
            area = next((_area for _area in new_window.screen.areas if _area.type == 'IMAGE_EDITOR'), None)
            if area:
                space: SpaceImageEditor = area.spaces.active # type: ignore
                space.ui_mode = "PAINT"
                space.image = node.note_image
                space.show_region_toolbar = True

                for region in area.regions:
                    if region.type == 'WINDOW':
                        with context.temp_override(window=new_window, area=area, region=region):
                            bpy.ops.image.view_all(fit_view=True)
                            bpy.ops.image.view_zoom_out(location=(0.5, 0.5))
                        break
        except Exception as e:
            self.report({'WARNING'}, iface("Failed to open image editor: {error}").format(error=e))
        finally:
            # Restore settings
            render.resolution_x = old_res_x
            render.resolution_y = old_res_y
            render.resolution_percentage = old_res_percent
            view.render_display_type = old_display_type

        return {'FINISHED'}

class NODE_OT_note_note_quick_edit(Operator):
    bl_idname = "node.note_quick_edit"
    bl_label = "Node Note"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):  # 空的也得有
        return {'FINISHED'}

    def invoke(self, context, event):
        context.window.cursor_warp(event.mouse_x + pref().cursor_warp_x, event.mouse_y)
        self.target_node = context.space_data.edit_tree.nodes.active if context.space_data and context.space_data.edit_tree else None
        return context.window_manager.invoke_popup(self, width=pref().panel_width)

    def draw(self, context):
        from .ui import draw_panel_for_shortcut
        draw_panel_for_shortcut(self.layout, context)

class NODE_OT_note_interactive_badge(Operator):
    bl_idname = "node.note_interactive_badge"
    bl_label = "Interactive Numbering"
    bl_description = "Click nodes to auto-number (right-click/ESC to exit)"
    bl_options = {'REGISTER', 'UNDO'}

    def modal(self, context, event):
        # 检查标记,如果被外部关闭则退出
        if not pref().is_interactive_mode:
            context.window.cursor_modal_restore()
            context.area.header_text_set(None)
            return {'FINISHED'}

        if event.type in {'RIGHTMOUSE', 'ESC'}:
            pref().is_interactive_mode = False
            context.window.cursor_modal_restore()
            context.area.header_text_set(None)
            return {'FINISHED'}

        if context.region.type == 'WINDOW':
            context.window.cursor_modal_set('PAINT_BRUSH')

        if event.type == 'LEFTMOUSE' and event.value == 'PRESS':
            if context.region.type == 'WINDOW':
                bpy.ops.node.select(location=(event.mouse_region_x, event.mouse_region_y), extend=False)
                node = context.active_node

                if node and node != self.last_node:
                    self.current_idx += 1
                    node.note_badge_index = self.current_idx

                    if node.note_badge_index > 0:
                        node.note_badge_color = pref().default_badge_color

                    self.last_node = node

                    msg = iface("Interactive Mode [Right-click/ESC to exit] | Market: {node_name} -> #{index}").format(node_name=node.name, index=self.current_idx)
                    context.area.header_text_set(msg)
                    context.area.tag_redraw()
                return {'RUNNING_MODAL'}
            else:
                return {'PASS_THROUGH'}

        return {'PASS_THROUGH'}

    def invoke(self, context, event):
        # 如果已经是交互模式,再次点击则关闭(自开关逻辑)
        if pref().is_interactive_mode:
            pref().is_interactive_mode = False
            return {'FINISHED'}

        if context.area.type == 'NODE_EDITOR':
            # 开启模式
            pref().is_interactive_mode = True

            max_idx = 0
            tree = context.space_data.edit_tree
            if tree:
                max_idx = max((node.note_badge_index for node in tree.nodes), default=0)

            self.current_idx = max_idx
            self.last_node = None

            context.window_manager.modal_handler_add(self)
            context.window.cursor_modal_set('PAINT_BRUSH')
            context.area.header_text_set(iface("Interactive Mode [Right-click/ESC to exit] | Current max index: #{max_idx}").format(max_idx=max_idx))
            return {'RUNNING_MODAL'}
        else:
            self.report({'WARNING'}, "Node editor not found")
            pref().is_interactive_mode = False
            return {'CANCELLED'}

# 兼容5.2之前
class NODE_OT_note_paste_text_from_clipboard(NoteBaseOperator):
    bl_idname = "node.note_paste_text_from_clipboard"
    bl_label = "Paste Text from Clipboard"
    bl_description = "Paste text from clipboard, replace line breaks according to settings"

    def execute(self, context):
        node = context.active_node
        text = context.window_manager.clipboard
        if not text:
            self.report({'WARNING'}, "Clipboard is empty")
            return {'CANCELLED'}

        separator = pref().line_separator
        if separator:
            first_sep = separator.split('|')[0]
            text = text.replace('\n', first_sep)

        node.note_text = text
        return {'FINISHED'}

# 兼容5.2之前
class NODE_OT_note_copy_text_to_clipboard(NoteBaseOperator):
    bl_idname = "node.note_copy_text_to_clipboard"
    bl_label = "Copy Text to Clipboard"
    bl_description = "Copy text to clipboard, restore line breaks to \\n"

    def execute(self, context):
        node = context.active_node
        lines = text_split_lines(node.note_text)
        text = "\n".join(lines)
        context.window_manager.clipboard = text
        self.report({'INFO'}, "Copied to clipboard")
        return {'FINISHED'}

class NODE_OT_note_migrate_text_separator(Operator):
    bl_idname = "node.note_migrate_text_separator"
    bl_label = "Migrate"
    bl_description = "Replace legacy line separators with new line characters"

    def execute(self, context):
        separator = pref().line_separator
        if not separator:
            self.report({'INFO'}, "No separator configured, nothing to migrate")
            return {'CANCELLED'}

        sep_list = separator.split('|')
        count = 0

        for tree in bpy.data.node_groups:
            for node in tree.nodes:
                if hasattr(node, "note_text") and node.note_text:
                    original = node.note_text
                    new_text = original
                    for sep in sep_list:
                        new_text = new_text.replace(sep, "\n")
                    if new_text != original:
                        node.note_text = new_text
                        count += 1

        self.report({'INFO'}, iface("Migrated {count} text notes").format(count=count))
        return {'FINISHED'}


class NODE_OT_note_text_from_node_label(NoteScopeOperator):
    bl_idname = "node.note_text_from_node_label"
    bl_label = "Import Node Label to Text Note"
    bl_description = "Import node label to text note"

    font_size         : IntProperty(name="Font Size", default=15, min=4, max=500)
    only_frame_node   : BoolProperty(name="Only Frame Nodes", default=True, description="Only process Frame nodes")
    use_node_color    : BoolProperty(name="Use Node Color", default=True, description="Inherit node custom color as background color")
    center_text       : BoolProperty(name="Center Text", default=True, description="Set text alignment to center")
    skip_existing_text: BoolProperty(name="Skip not none", default=True, description="Skip nodes that already have text notes")
    width_mode        : EnumProperty(name="Width Mode", items=txt_width_items, default='KEEP')
    txt_bg_color      : FloatVectorProperty(name="Background Color", subtype='COLOR', size=4, default=(0.2, 0.3, 0.5, 0.9), min=0, max=1)
    delete_label      : BoolProperty(name="Delete Label", default=False, description="Delete nodes custom label if processed/imported")

    def draw(self, context):
        layout = self.layout
        self.draw_scope(layout)

        row_width = layout.split(factor=0.3)
        row_width.label(text="Width Mode")
        row_width.column().prop(self, "width_mode", expand=True)

        layout.separator()
        row3 = layout.row()
        row3.prop(self, "only_frame_node", toggle=True)
        row3.prop(self, "center_text", toggle=True)
        row3 = layout.row()
        row3.prop(self, "skip_existing_text", toggle=True)
        row3.prop(self, "use_node_color", toggle=True)
        row3 = layout.split(factor=0.5)
        row3.prop(self, "font_size")
        row3.row().prop(self, "txt_bg_color")
        layout.prop(self, "delete_label", toggle=True)

    def execute(self, context):
        nodes_to_process = self.get_scoped_nodes(context)
        count = 0
        for node in nodes_to_process:
            if self.only_frame_node and node.type != 'FRAME':  # type: ignore
                continue

            if self.skip_existing_text and node.note_text:
                continue

            if text_content := node.label :
                node.note_text = text_content
                node.note_show_txt = True
                count += 1
                if self.delete_label:
                    node.label = ""

            node.note_font_size = self.font_size
            node.note_txt_width_mode = self.width_mode
            if self.use_node_color and node.use_custom_color:
                # todo 不确定是否要从偏好设置导入透明度, 框内的框透明度要更低吗
                node.note_txt_bg_color = (*node.color, 0.8)
            else:
                node.note_txt_bg_color = self.txt_bg_color
            if self.center_text:
                node.note_txt_center = True

        self.report({'INFO'}, f"Processed {count} nodes")
        context.area.tag_redraw()
        return {'FINISHED'}

classes = [
    NODE_OT_note_delete_selected_txt,
    NODE_OT_note_delete_selected_img,
    NODE_OT_note_delete_selected_badge,
    NODE_OT_note_delete_notes,
    NODE_OT_note_show_selected_txt,
    NODE_OT_note_show_selected_img,
    NODE_OT_note_show_selected_badge,
    NODE_OT_note_note_swap_order,
    NODE_OT_note_interactive_badge,
    NODE_OT_note_reset_offset,
    NODE_OT_note_paste_image,
    NODE_OT_note_apply_preset,
    NODE_OT_note_copy_active_style,
    NODE_OT_note_add_quick_tag,
    NODE_OT_note_pack_unpack_images,
    NODE_OT_note_note_quick_edit,
    NODE_OT_note_jump_to_note,
    NODE_OT_note_open_image_editor,
    NODE_OT_note_paste_text_from_clipboard,
    NODE_OT_note_copy_text_to_clipboard,
    NODE_OT_note_migrate_text_separator,
    NODE_OT_note_text_from_node_label,
]

def register():
    for cls in classes:
        bpy.utils.register_class(cls)

def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
