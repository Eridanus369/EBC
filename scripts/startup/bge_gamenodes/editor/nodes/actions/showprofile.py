from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicBoolean


@node_type
class GameNodeShowProfile(GameNodeActionType):
    bl_idname = "NLSetProfile"
    bl_label = "Show Profile"
    bl_description = 'Show telemetry at the top left corner'
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULSetProfile"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition', 'condition')
        self.add_input(NodeSocketLogicBoolean, 'Show', 'use_profile')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "use_profile"]
