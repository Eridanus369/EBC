from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicCollection


@node_type
class GameNodeRemoveOverlayCollection(GameNodeActionType):
    bl_idname = "NLRemoveOverlayCollection"
    bl_label = "Remove Overlay Collection"
    bl_description = 'Remove an overlay collection'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULRemoveOverlayCollection"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicCollection, 'Collection', 'collection')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['condition', 'collection']
