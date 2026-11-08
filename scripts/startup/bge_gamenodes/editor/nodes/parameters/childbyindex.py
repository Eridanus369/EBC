from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicObject
from ...sockets import NodeSocketLogicInteger


@node_type
class GameNodeChildByIndex(GameNodeParameterType):
    bl_idname = "NLParameterFindChildByIndexNode"
    bl_label = "Get Child By Index"
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULChildByIndex"
    bl_description = 'Child at an idex in an objects children'

    def init(self, context):
        self.add_input(NodeSocketLogicObject, 'Parent', 'from_parent')
        self.add_input(NodeSocketLogicInteger, 'Index', 'index')
        self.add_output(NodeSocketLogicObject, 'Child', 'CHILD')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["from_parent", "index"]

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ['CHILD']
