from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicIntegerPositiveCent


@node_type
class GameNodeToggleFilter(GameNodeActionType):
    bl_idname = "NLToggleFilter"
    bl_label = "Toggle Filter"
    bl_description = 'Switch the activity status of a screen filter'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULToggleFilter"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicIntegerPositiveCent, 'Pass Index', 'pass_idx')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['condition', 'pass_idx']

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]
