from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicPython


@node_type
class GameNodeResumeSound(GameNodeActionType):
    bl_idname = "NLActionResumeSound"
    bl_label = "Resume Sound"
    bl_description = 'Resume a sound'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULResumeSound"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'condition')
        self.add_input(NodeSocketLogicPython, "Sound", 'sound')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "sound"]
