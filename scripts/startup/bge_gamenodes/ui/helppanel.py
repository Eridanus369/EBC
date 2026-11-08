import bpy
from .interface import ui_panel
from ..editor.nodetree import GameNodeTree
from ..utilities import preferences


@ui_panel
class LOGIC_NODES_PT_help(bpy.types.Panel):
    bl_label = "Help & Documentation"
    bl_space_type = "NODE_EDITOR"
    bl_region_type = "UI"
    bl_category = "Help & Documentation"
    _current_tree = None

    @classmethod
    def poll(cls, context):
        enabled = (context.space_data.tree_type == GameNodeTree.bl_idname)
        return enabled

    def draw(self, context):
        layout = self.layout
        layout.scale_y = 1.3
        
        box = layout.box()
        box.operator(
            "game_nodes.open_upbge_docs",
            text="Engine API",
            icon='FILE_BLEND'
        )
        box.operator(
            "game_nodes.open_upbge_manual",
            text="Manual",
            icon='BLENDER'
        )
        box = layout.box()
        box.operator(
            "game_nodes.open_github",
            text="Github Sources",
            icon='MODIFIER_ON'
        )
        box.operator(
            "game_nodes.open_donate",
            text="Support Game Nodes",
            icon='FUND'
        )
        box = layout.box()
        col = box.column(align=True)
        col.operator('game_nodes.install_uplogic', icon='IMPORT', text='Get Uplogic')
        col.prop(preferences(), 'uplogic_version', text='')
