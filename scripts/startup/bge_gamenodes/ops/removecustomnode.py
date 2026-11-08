from .operator import operator
from bpy.types import Operator
from bpy.props import IntProperty
from ..props.customnode import get_custom_game_nodes, save_custom_game_nodes, _registered_custom_classes
import bpy


@operator
class LOGIC_NODES_OT_remove_custom_node(Operator):
    bl_idname = "game_nodes.remove_custom_node"
    bl_label = "Remove Custom Logic Node"
    bl_options = {'REGISTER', 'UNDO'}
    bl_description = "Remove Custom Logic Node"
    index: IntProperty()

    @classmethod
    def poll(cls, context):
        return True

    def execute(self, context):
        node = get_custom_game_nodes()[self.index]
        for c in _registered_custom_classes:
            if c.bl_idname == node.idname:
                bpy.utils.unregister_class(c)
                _registered_custom_classes.remove(c)
        get_custom_game_nodes().pop(self.index)
        save_custom_game_nodes()
        return {'FINISHED'}