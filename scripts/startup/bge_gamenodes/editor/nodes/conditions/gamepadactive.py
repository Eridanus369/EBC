from ..node import node_type
from ..node import GameNodeConditionType
from ...sockets import NodeSocketLogicIntegerPositiveCent
from ...sockets import NodeSocketLogicCondition


@node_type
class GameNodeGamepadActive(GameNodeConditionType):
    bl_idname = "NLGamepadActive"
    bl_label = "Gamepad Active"
    bl_description = 'Register gamepad activity'
    nl_module = 'uplogic.nodes.conditions'
    nl_class = "ULGamepadActive"

    def init(self, context):
        self.add_input(NodeSocketLogicIntegerPositiveCent, 'Index', 'index')
        self.add_output(NodeSocketLogicCondition, 'Active', 'OUT')
        GameNodeConditionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["index"]

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT']
