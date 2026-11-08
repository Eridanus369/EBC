from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicFloat


@node_type
class GameNodeTimeDeltaFactor(GameNodeParameterType):
    bl_idname = "LogicNodeTimeFactor"
    bl_label = "Delta Factor"
    bl_description = 'Time per frame compared to 60 fps'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULFPSFactor"

    def init(self, context):
        self.add_output(NodeSocketLogicFloat, "Factor", 'TIMEFACTOR')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["TIMEFACTOR"]
