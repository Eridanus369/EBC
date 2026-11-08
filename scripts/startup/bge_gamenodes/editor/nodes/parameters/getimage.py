from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicImage


@node_type
class GameNodeGetImage(GameNodeParameterType):
    bl_idname = "NLGetImage"
    bl_label = "Get Image"
    bl_description = 'Image (ID)'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetImage"

    def init(self, context):
        self.add_input(NodeSocketLogicImage, "Image", 'image')
        self.add_output(NodeSocketLogicImage, 'Image', 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["image"]

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['OUT']
