from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicFilePath


@node_type
class GameNodeFilePath(GameNodeParameterType):
    bl_idname = "NLParameterFileValue"
    bl_label = "File Path"
    bl_description = 'An absolute system path'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULSimpleValue"

    def init(self, context):
        self.add_input(NodeSocketLogicFilePath, "", 'value')
        self.add_output(NodeSocketLogicFilePath, "Path", 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["value"]

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]
