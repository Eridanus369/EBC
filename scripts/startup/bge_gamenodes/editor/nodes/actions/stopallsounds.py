from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition


@node_type
class GameNodeStopAllSounds(GameNodeActionType):
    bl_idname = "NLActionStopAllSounds"
    bl_label = "Stop All Sounds"
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULStopAllSounds"
    bl_description = 'Stop all active sounds'

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'condition')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition"]
