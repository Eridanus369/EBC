from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicParameter


@node_type
class GameNodeGetFullscreen(GameNodeParameterType):
    bl_idname = "NLGetFullscreen"
    bl_label = "Get Fullscreen"
    bl_description = 'Fullscreen Flag'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetFullscreen"

    def init(self, context):
        self.add_output(NodeSocketLogicParameter, "Fullscreen", 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT']
