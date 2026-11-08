from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicBoolean


@node_type
class GameNodeSetFullscreen(GameNodeActionType):
    bl_idname = "NLActionSetFullscreen"
    bl_label = "Set Fullscreen"
    bl_description = 'Set the fullscreen status of the game'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULSetFullscreen"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicBoolean, 'Fullscreen', 'use_fullscreen')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "use_fullscreen"]
