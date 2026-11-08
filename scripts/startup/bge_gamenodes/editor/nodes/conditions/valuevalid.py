from ...sockets import NodeSocketLogicParameter
from ...sockets import NodeSocketLogicCondition
from ..node import GameNodeConditionType
from ..node import node_type


@node_type
class GameNodeValueValid(GameNodeConditionType):
    bl_idname = "NLConditionValueValidNode"
    bl_label = "Value Valid"
    nl_module = 'uplogic.nodes.conditions'
    nl_class = "ULValueValid"
    deprecated = True

    def init(self, context):
        self.add_input(NodeSocketLogicParameter, "Value")
        self.add_output(NodeSocketLogicCondition, "If Valid")
        GameNodeConditionType.init(self, context)

    def get_input_names(self):
        return ["checked_value"]
