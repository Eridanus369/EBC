from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicCollection
from ...sockets import NodeSocketLogicList


@node_type
class GameNodeGetCollectionObjectNames(GameNodeParameterType):
    bl_idname = "NLGetCollectionObjectNamesNode"
    bl_label = "Get Object Names"
    bl_description = 'List of names of objects in a collection'
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetCollectionObjectNames"

    def init(self, context):
        self.add_input(NodeSocketLogicCollection, 'Collection', 'collection')
        self.add_output(NodeSocketLogicList, "Object Names", 'OUT')
        GameNodeParameterType.init(self, context)

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ['collection']

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]
