from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicCamera
from ...sockets import NodeSocketLogicCollection


@node_type
class GameNodeSetOverlayCollection(GameNodeActionType):
    bl_idname = "NLSetOverlayCollection"
    bl_label = "Set Overlay Collection"
    bl_description = 'Add a new overlay collection'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULSetOverlayCollection"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicCamera, 'Camera', 'camera')
        self.add_input(NodeSocketLogicCollection, 'Collection', 'collection')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['condition', 'camera', 'collection']

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT']
