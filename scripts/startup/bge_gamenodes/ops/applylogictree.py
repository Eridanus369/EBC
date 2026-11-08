from ..utilities import check_uplogic_module
from ..utilities import add_tree_to_active_objects
from .operator import operator
from bpy.types import Operator
from bpy.props import StringProperty
from ..editor.nodetree import GameNodeTree


@operator
class LOGIC_NODES_OT_apply_logic_tree(Operator):
    bl_idname = "game_nodes.apply_logic_tree"
    bl_label = "Apply Logic"
    bl_description = "Apply the current tree to the selected objects."
    bl_options = {'REGISTER', 'UNDO'}

    owner: StringProperty()

    @classmethod
    def poll(cls, context):
        if not hasattr(context.space_data, 'edit_tree'):
            return False
        tree = context.space_data.edit_tree
        if not tree:
            return False
        if not (tree.bl_idname == GameNodeTree.bl_idname):
            return False
        scene = context.scene
        for ob in scene.objects:
            if ob.select_get():
                return True
        return False

    def execute(self, context):
        check_uplogic_module()
        tree = context.space_data.edit_tree
        add_tree_to_active_objects(tree)
        return {'FINISHED'}