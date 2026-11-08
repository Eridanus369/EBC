from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicLight


@node_type
class GameNodeLightMakeUnique(GameNodeActionType):
    bl_idname = "NLMakeUniqueLight"
    bl_label = "Make Light Unique"
    bl_description = 'Duplicate light data to allow for independent editing'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULMakeUniqueLight"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicLight, 'Light Object', 'light')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        self.add_output(NodeSocketLogicLight, 'Light', 'LIGHT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT", 'LIGHT']

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "light"]
