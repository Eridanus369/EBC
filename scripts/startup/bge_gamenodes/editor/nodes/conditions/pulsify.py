from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicTime
from ..node import GameNodeConditionType
from ..node import node_type


@node_type
class GameNodePulsify(GameNodeConditionType):
    """Convert a constant True condition into an interval signal"""
    bl_idname = "NLActionTimeFilter"
    bl_label = "Pulsify"
    bl_description = 'Insert time gaps into a consecutive "True" condition'
    nl_module = 'uplogic.nodes.conditions'
    nl_class = "ULPulsify"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'condition')
        self.add_input(NodeSocketLogicTime, "Gap", 'delay', {'default_value': 1.0})
        self.add_output(NodeSocketLogicCondition, "Out", 'OUT')
        GameNodeConditionType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "delay"]
