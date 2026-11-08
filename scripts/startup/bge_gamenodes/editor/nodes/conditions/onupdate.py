from ..node import node_type
from ..node import GameNodeConditionType
from ...sockets import NodeSocketLogicCondition


@node_type
class GameNodeOnUpdate(GameNodeConditionType):
    bl_idname = "NLOnUpdateConditionNode"
    bl_label = "On Update"
    bl_description = 'Every frame'
    nl_module = 'uplogic.nodes.conditions'
    nl_class = "ULOnUpdate"

    def init(self, context):
        self.add_output(NodeSocketLogicCondition, "Out", 'OUT')
        GameNodeConditionType.init(self, context)
        self.hide = True
