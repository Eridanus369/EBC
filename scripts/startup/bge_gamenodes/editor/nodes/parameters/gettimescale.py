from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicParameter


@node_type
class GameNodeGetTimescale(GameNodeParameterType):
    bl_idname = "NLParameterGetTimeScale"
    bl_label = "Get Timescale"
    bl_description = "The current scene's time scale factor"
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetTimeScale"

    def init(self, context):
        self.add_output(NodeSocketLogicParameter, 'Timescale', 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT']
