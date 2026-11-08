from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition


@node_type
class GameNodeQuitGame(GameNodeActionType):
    bl_idname = "NLActionEndGame"
    bl_label = "Quit Game"
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULEndGame"
    bl_description = 'End game runtime'

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'condition')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition"]
