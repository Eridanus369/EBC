"""Empty-state panel for Game Nodes.

The node editor follows the active object (see `GameNodeTree.get_from_context`);
when the editor has no tree at all this panel offers the in-place creation
entry point. Binding happens at creation, so there is no Apply button anymore.
"""

import bpy
from .interface import ui_panel
from ..editor.nodetree import GameNodeTree


@ui_panel
class LOGIC_NODES_PT_logic_tree_state(bpy.types.Panel):
    bl_label = "Game Nodes"
    bl_space_type = "NODE_EDITOR"
    bl_region_type = "UI"
    bl_category = "Dashboard"

    @classmethod
    def poll(cls, context):
        space = getattr(context, 'space_data', None)
        if space is None or space.tree_type != GameNodeTree.bl_idname:
            return False
        return space.node_tree is None

    def draw(self, context):
        layout = self.layout
        layout.label(text="该对象还没有 Game Nodes 树", icon='INFO')
        layout.operator('game_nodes.new_logic_tree', text="New", icon='ADD')
