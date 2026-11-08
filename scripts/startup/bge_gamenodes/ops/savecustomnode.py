from .operator import operator
from bpy.types import Operator
from bpy.props import IntProperty
from ..props.customnode import get_custom_game_nodes, save_custom_game_nodes
import bpy


@operator
class LOGIC_NODES_OT_save_custom_node(Operator):
    bl_idname = "game_nodes.save_custom_node"
    bl_label = "Save Custom Logic Node"
    bl_options = {'REGISTER', 'UNDO'}
    bl_description = "Save Custom Logic Node"
    index: IntProperty()

    @classmethod
    def poll(cls, context):
        return True

    def execute(self, context):
        node = get_custom_game_nodes()[self.index]
        text = bpy.data.texts.get(node.modname, None)
        if text is None:
            return {'CANCELLED'}
        node.logic_code = text.as_string()
        save_custom_game_nodes()
        return {'FINISHED'}
