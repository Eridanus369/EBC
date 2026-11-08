from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicFloat


@node_type
class GameNodeRandomFloat(GameNodeParameterType):
    bl_idname = "NLActionRandomFloat"
    bl_label = "Random Float"
    nl_module = 'uplogic.nodes.parameters'
    deprecated = True
    deprecation_message = 'Replaced by "Random Value" Node.'

    def init(self, context):
        self.add_input(NodeSocketLogicFloat, "Min")
        self.add_input(NodeSocketLogicFloat, "Max")
        self.add_output(NodeSocketLogicFloat, "Value")
        GameNodeParameterType.init(self, context)

    def get_input_names(self):
        return ["min_value", "max_value"]

    nl_class = "ULRandomFloat"

    def get_output_names(self):
        return ["OUT_A"]
