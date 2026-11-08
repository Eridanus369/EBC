from ...sockets import NodeSocketLogicCondition
from ..node import GameNodeConditionType
from ..node import node_type


@node_type
class GameNodeOnNextFrame(GameNodeConditionType):
    bl_idname = "NLConditionNextFrameNode"
    bl_label = "On Next Frame"
    bl_description = 'Relay with one frame delay'
    nl_module = 'uplogic.nodes.conditions'
    nl_class = "ULOnNextTick"

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'input_condition', {'show_prop': True})
        self.add_output(NodeSocketLogicCondition, "Out", 'OUT')
        GameNodeConditionType.init(self, context)
        self.hide = True

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["input_condition"]
