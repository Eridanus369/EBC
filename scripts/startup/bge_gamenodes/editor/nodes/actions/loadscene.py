from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicScene
from ...sockets import NodeSocketLogicFloat
from ...sockets import NodeSocketLogicString


@node_type
class GameNodeLoadScene(GameNodeActionType):
    bl_idname = "NLLoadScene"
    bl_label = "Load Scene"
    bl_description = 'Load the contents of another scene. This will not change the current scene'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULLoadScene"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicScene, "Scene", 'scene')
        self.add_output(NodeSocketLogicCondition, 'Loaded', 'OUT')
        self.add_output(NodeSocketLogicCondition, 'Updated', 'UPDATED')
        self.add_output(NodeSocketLogicFloat, 'Status', 'STATUS')
        self.add_output(NodeSocketLogicString, 'Datatype', 'DATATYPE')
        self.add_output(NodeSocketLogicString, 'Item', 'ITEM')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['condition', 'scene']

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT', 'UPDATED', 'STATUS', 'DATATYPE', 'ITEM']
