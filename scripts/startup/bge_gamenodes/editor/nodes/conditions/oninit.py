from ..node import node_type
from ..node import GameNodeConditionType
from ...sockets import NodeSocketLogicCondition


@node_type
class GameNodeOnInit(GameNodeConditionType):
    bl_idname = "NLOnInitConditionNode"
    bl_label = "On Init"
    bl_description = 'On first frame'
    nl_module = 'uplogic.nodes.conditions'
    nl_class = "ULOnInit"

    def init(self, context):
        self.add_output(NodeSocketLogicCondition, "Out", 'OUT')
        GameNodeConditionType.init(self, context)
        self.hide = True
