from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicScene


@node_type
class GameNodeGetScene(GameNodeParameterType):
    bl_idname = "NLGetScene"
    bl_label = "Get Scene"
    bl_description = 'The current scene'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetScene"

    def init(self, context):
        self.add_output(NodeSocketLogicScene, 'Scene', 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT']
