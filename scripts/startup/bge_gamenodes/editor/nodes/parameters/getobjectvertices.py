from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicObject
from ...sockets import NodeSocketLogicList


@node_type
class GameNodeGetObjectVertices(GameNodeParameterType):
    bl_idname = "NLGetObjectVertices"
    bl_label = "Get Vertices"
    bl_description = 'Vertex positions of an object in global space'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULObjectDataVertices"

    def init(self, context):
        self.add_input(NodeSocketLogicObject, "Object", 'game_object')
        self.add_output(NodeSocketLogicList, "Vertices", 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["game_object"]

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]
