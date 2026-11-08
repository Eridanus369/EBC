from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicInteger


@node_type
class GameNodeSetSMAAQuality(GameNodeActionType):
    bl_idname = "NLSetEeveeSMAAQuality"
    bl_label = "Set SMAA Quality"
    nl_module = 'uplogic.nodes.actions'
    deprecated = True
    deprecation_message = 'Node will be removed in future update.'

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition')
        self.add_input(NodeSocketLogicInteger, 'Quality')
        self.add_output(NodeSocketLogicCondition, 'Done')
        GameNodeActionType.init(self, context)

    def get_output_names(self):
        return ["OUT"]

    nl_class = "ULSetEeveeSMAAQuality"

    def get_input_names(self):
        return [
            "condition",
            "value"
        ]
