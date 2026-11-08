from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicFilePath
from ...sockets import NodeSocketLogicString
from ...sockets import NodeSocketLogicDictionary


@node_type
class GameNodeVariablesLoad(GameNodeParameterType):
    bl_idname = "NLActionLoadVariables"
    bl_label = "Load Variable Dict"
    bl_description = 'Load an externally saved .json file as dictionary'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULLoadVariableDict"

    def init(self, context):
        self.add_input(NodeSocketLogicFilePath, 'Path', 'path', {'default_value': '//Data'})
        self.add_input(NodeSocketLogicString, 'File', 'file_name', {'default_value': 'variables'})
        self.add_output(NodeSocketLogicDictionary, 'Variables', 'VAR')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['path', "file_name"]

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["VAR"]
