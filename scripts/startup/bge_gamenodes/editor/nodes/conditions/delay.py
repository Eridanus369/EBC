from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicTime
from ..node import GameNodeConditionType
from ..node import node_type


@node_type
class GameNodeDelay(GameNodeConditionType):
    bl_idname = 'NLActionTimeDelay'
    bl_label = 'Delay'
    bl_description = 'Relay an input with a delay'
    nl_module = 'uplogic.nodes.conditions'
    nl_class = 'ULTimeDelay'

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicTime, 'Delay', 'delay')
        self.add_output(NodeSocketLogicCondition, 'Out', 'OUT')
        GameNodeConditionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['condition', 'delay']
