from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicVectorXYZ


@node_type
class GameNodeGetGravity(GameNodeParameterType):
    bl_idname = "NLGetGravityNode"
    bl_label = "Get Gravity"
    bl_description = 'Scene gravity vector'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetGravity"

    def init(self, context):
        self.add_output(NodeSocketLogicVectorXYZ, "Gravity", 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]
