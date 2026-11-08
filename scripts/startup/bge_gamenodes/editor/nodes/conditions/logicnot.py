from ...sockets import NodeSocketLogicCondition
from ..node import GameNodeConditionType
from ..node import node_type


@node_type
class GameNodeLogicNot(GameNodeConditionType):
    bl_idname = "NLConditionNotNode"
    bl_label = "Not"
    bl_width_min = 60
    bl_width_default = 80
    nl_module = 'uplogic.nodes.conditions'
    deprecated = True

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition")
        self.add_output(NodeSocketLogicCondition, "If Not")
        GameNodeConditionType.init(self, context)
        self.hide = True

    nl_class = "ULNot"

    def get_input_names(self):
        return ["condition"]

    def get_output_names(self):
        return ['OUT']
