from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicFloat


@node_type
class GameNodeValueAbsolute(GameNodeParameterType):
    bl_idname = "NLAbsoluteValue"
    bl_label = "Absolute"
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULAbsoluteValue"
    deprecated = True
    deprecation_message = 'Replaced by "Vector Math" node'

    def init(self, context):
        self.add_input(NodeSocketLogicFloat, "Value")
        self.add_output(NodeSocketLogicFloat, "Value")
        GameNodeParameterType.init(self, context)


    def get_input_names(self):
        return ["value"]

    def get_output_names(self):
        return ['OUT']
