from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicBoolean


@node_type
class GameNodeSetEeveeSMAA(GameNodeActionType):
    bl_idname = "NLSetEeveeSMAA"
    bl_label = "Set SMAA"
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULSetEeveeSMAA"
    deprecated = True
    deprecation_message = 'This node will be removed in a future update.'

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition')
        self.add_input(NodeSocketLogicBoolean, 'Use SMAA')
        self.add_output(NodeSocketLogicCondition, 'Done')
        GameNodeActionType.init(self, context)

    def get_output_names(self):
        return ["OUT"]

    def get_input_names(self):
        return [
            "condition",
            "value"
        ]
