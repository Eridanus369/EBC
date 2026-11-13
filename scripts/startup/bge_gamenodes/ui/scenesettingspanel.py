import bpy
from ..utilities import preferences
from .interface import ui_panel


@ui_panel
class LOGIC_NODES_PT_scene_settings(bpy.types.Panel):
    bl_label = "Uplogic Settings"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "scene"
    name: bpy.props.StringProperty()

    @classmethod
    def poll(cls, context):
        return True

    def draw(self, context):
        layout = self.layout
        prefs = preferences()
        layout.prop(context.scene, 'jump_in_game_cam')
        layout.prop(context.scene, 'use_vr_audio_space')
        row = layout.row()
        row.prop(context.scene, 'use_screen_console')
        part = row.row()
        part.prop(context.scene, 'screen_console_open', text='Debug')
        # op = layout.operator("game_nodes.key_selector", text=prefs.screen_console_key)
        # op.is_socket = False
        part.enabled = getattr(context.scene, 'use_screen_console', False)

        use_mainloop = context.scene.get('__main__', '') != ''
        layout.operator(
            'game_nodes.custom_mainloop',
            text='移除自定义主循环' if use_mainloop else '使用自定义主循环',
            icon='CANCEL' if use_mainloop else 'PLAY'
        )
        layout.separator()
        layout.operator(
            'game_nodes.audio_system',
            icon='PLAY'
        )


@ui_panel
class SCENE_PT_game_hysteresis(bpy.types.Panel):
    bl_label = "Level of Detail"
    bl_parent_id = "LOGIC_NODES_PT_scene_settings"
    bl_options = {'DEFAULT_CLOSED'}
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "scene"

    def draw(self, context):
        layout = self.layout
        gs = context.scene.game_settings

        row = layout.row()
        row.prop(gs, "use_scene_hysteresis", text="Hysteresis")
        row = layout.row()
        row.active = gs.use_scene_hysteresis
        row.prop(gs, "scene_hysteresis_percentage", text="")

@ui_panel
class SCENE_PT_game_console(bpy.types.Panel):
    bl_label = "Game Python Console"
    bl_parent_id = "LOGIC_NODES_PT_scene_settings"
    bl_options = {'DEFAULT_CLOSED'}
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "scene"

    def draw_header(self, context):
        gs = context.scene.game_settings
        self.layout.prop(gs, "use_python_console", text="")

    def draw(self, context):
        layout = self.layout
        gs = context.scene.game_settings

        row = layout.row(align=True)
        row.active = gs.use_python_console
        row.label(text="Keys:")
        row.prop(gs, "python_console_key1", text="", event=True)
        row.prop(gs, "python_console_key2", text="", event=True)
        row.prop(gs, "python_console_key3", text="", event=True)
        row.prop(gs, "python_console_key4", text="", event=True)
