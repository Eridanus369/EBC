"""Create a Game Nodes tree and bind it to the active object.

Replaces the old "create in the editor, then press Apply" workflow: binding
happens at creation time, exactly like Blender's `new_node_tree_impl` assigns a
new tree to its owner (`node_add.cc`, `node_templateID_assign`).
"""

import bpy
from bpy.types import Operator

from ..editor.nodetree import GameNodeTree
from ..utilities import add_tree_to_active_objects
from ..utilities import make_valid_name
from ..utilities import warn
from .operator import operator


@operator
class LOGIC_NODES_OT_new_logic_tree(Operator):
    bl_idname = "game_nodes.new_logic_tree"
    bl_label = "New Game Nodes Tree"
    bl_description = (
        "Create a Game Nodes tree and bind it to the active object. "
        "The tree becomes the object's primary Game Nodes tree"
    )
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        space = getattr(context, 'space_data', None)
        return (space is not None and
                getattr(space, 'tree_type', '') == GameNodeTree.bl_idname)

    def execute(self, context):
        obj = context.active_object or context.object
        if obj is None:
            warn('No active object: select an object before creating a tree')
            return {'CANCELLED'}

        tree = bpy.data.node_groups.new('Game Node Tree', GameNodeTree.bl_idname)
        tree.use_fake_user = True

        # Geometry-nodes semantics: the header/empty-state "New" creates a tree
        # for *this* object only, never for the rest of the selection.
        add_tree_to_active_objects(tree, only_active=True)

        # Creation binds the active object and makes it the primary tree, even
        # when the object already had one.  Otherwise the next redraw would
        # resolve back to the old primary and the new tree would disappear from
        # the editor.
        obj.game_node_tree = tree
        refs = obj.logic_trees
        if not any(ref.tree == tree for ref in refs):
            ref = refs.add()
            ref.tree = tree
            ref.tree_name = make_valid_name(tree.name)
            ref.tree_initial_status = True

        space = getattr(context, 'space_data', None)
        if (space is not None and
                getattr(space, 'tree_type', '') == GameNodeTree.bl_idname):
            space.node_tree = tree
        return {'FINISHED'}
