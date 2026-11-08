from bpy.types import Context, NodeTree
from bpy.types import NodeReroute
from bpy.types import NodeInputs
from bpy.types import NodeOutputs
from bpy.types import NodeSocket
from bpy.props import StringProperty
from bpy.props import CollectionProperty
from bpy.props import BoolProperty
from bpy.props import IntProperty
from ..utilities import make_valid_name
from ..utilities import error
from ..utilities import success
from ..utilities import add_tree_to_active_objects
from ..props.logictreeproperty import GameNodesLogicTreeProperty
import bpy
from time import time


class GameNodeTree(NodeTree):
    bl_idname = "BGELogicTree"
    bl_label = "Game Nodes"
    bl_icon = "OUTLINER"
    bl_category = "Scripting"

    def group_update(self, context: Context) -> None:
        for n in self.nodes:
            n.group_update(self)

    changes_staged: BoolProperty(default=False)

    type: StringProperty(default='LOGIC')
    old_name: StringProperty()
    properties: CollectionProperty(type=GameNodesLogicTreeProperty, name='Properties')

    @classmethod
    def poll(cls, context):
        return True

    @classmethod
    def get_from_context(cls, context):
        """Resolve the tree for the active object (geometry-nodes style).

        Strictly read-only: never creates data, never writes object properties,
        never touches `logic_trees`.  When the active object has no tree, return
        nothing so the editor shows its empty state, exactly like geometry and
        shader nodes.  Pinning is handled by the C caller: `snode_set_context`
        skips this hook while the editor is pinned and keeps its current tree.
        """
        obj = getattr(context, 'active_object', None) or getattr(
            context, 'object', None
        )
        if obj is None:
            return None, None, None

        tree = getattr(obj, 'game_node_tree', None)
        if tree is None:
            # Legacy files only have the mount list (no primary pointer).
            for ref in getattr(obj, 'logic_trees', ()) or ():
                if ref.tree is not None:
                    tree = ref.tree
                    break
        if tree is None:
            return None, None, None
        return tree, obj, obj

    def get_name(self):
        pass

    def update_name(self, update=True):
        clsname = make_valid_name(self.name)
        if clsname == '':
            error('Tree name cannot consist of illegal letters only!')
            self.name = self.old_name
            return
        if not self.old_name:
            add_tree_to_active_objects(self, only_active=True)
        for obj in bpy.context.scene.objects:
            for ref in obj.logic_trees:
                if ref.tree is self:
                    ref.tree_name = self.name

        if self.old_name != '':
            success(f'Successfully Renamed {self.old_name} to {self.name}')
        self.old_name = self.name

    def mark_invalid_links(self):
        '''Mark invalid links, must be called from a timer'''
        for link in self.links:
            if hasattr(link.to_socket, 'validate') and not link.to_socket.skip_validation:
                link.to_socket.validate(link, link.from_socket, self)

    def interface_update(self, context: Context) -> None:
        self.group_update(context)
        return super().interface_update(context)

    def update_draw(self, context):
        for n in self.nodes:
            if hasattr(n, 'update_draw'):
                n.update_draw(context)

    def color_sockets(self):
        for n in filter(lambda n: isinstance(n, NodeReroute), self.nodes):
            outsocket = n.outputs[0]
            insocket = n.inputs[0]

            outlinks = outsocket.links
            inlinks = insocket.links
            if not insocket.is_linked or isinstance(inlinks[0].from_node, NodeReroute):
                # print('trying to source type from another reroute')
                continue
            from_socket = inlinks[0].from_socket
            insocket.type = from_socket.type
            outsocket.type = from_socket.type
            outsocket.display_shape = 'SQUARE'
    
            # print(from_socket.type)
            # print(insocket.type)

    def update(self):
        bpy.app.timers.register(self.mark_invalid_links)
        start = time()
        self.changes_staged = True
    
        for n in self.nodes:
            # Nodes of unknown types are loaded as plain bpy.types.Node and
            # have no update() method; skip them instead of failing on open.
            update = getattr(n, 'update', None)
            if update is not None:
                update()
        self.color_sockets()
