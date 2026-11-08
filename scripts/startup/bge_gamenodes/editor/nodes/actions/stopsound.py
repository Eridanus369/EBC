from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicPython


@node_type
class GameNodeStopSound(GameNodeActionType):
    bl_idname = "NLActionStopSound"
    bl_label = "Stop Sound"
    bl_description = 'Stop a sound'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULStopSound"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'condition')
        self.add_input(NodeSocketLogicPython, "Sound", 'sound')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "sound"]
