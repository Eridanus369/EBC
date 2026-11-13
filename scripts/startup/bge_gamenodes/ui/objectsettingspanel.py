import bpy
from .interface import ui_panel


# [DISABLED] @ui_panel
# [DISABLED] class LOGIC_NODES_PT_object_settings(bpy.types.Panel):
#     bl_label = "Uplogic Settings"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "physics"
    name: bpy.props.StringProperty()

    @classmethod
    def poll(cls, context):
        ob = context.active_object
        return ob and ob.name

    def draw(self, context):
        layout = self.layout
        main_col = layout.column()
        parts = main_col.split()
        col1 = parts.column()
        col2 = parts.column()
        
        if not context.active_object.data:
            row = col1.row()
            row.prop(context.active_object, 'reverb_volume', text='Reverb Volume')
            reverb_settings = col2.column(align=True)
            block = reverb_settings.row(align=True)
            block.prop(context.active_object, 'empty_display_size', text='Radius')
            block.operator(
                'game_nodes.reset_empty_scale',
                text="",
                icon='FULLSCREEN_EXIT'
            )
            block.enabled = context.active_object.reverb_volume
            reverb_settings.prop(context.active_object, 'reverb_samples', text='Samples')
