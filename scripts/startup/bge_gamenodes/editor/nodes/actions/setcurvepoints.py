from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicVector
from ...sockets import NodeSocketLogicCurve


@node_type
class GameNodeSetCurvePoints(GameNodeActionType):
    bl_idname = "NLSetCurvePoints"
    bl_label = "Set Curve Points"
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULSetCurvePoints"
    bl_description = 'Set the points of a curve in world space'

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, "Condition", 'condition')
        self.add_input(NodeSocketLogicCurve, "Curve", 'curve_object')
        self.add_input(NodeSocketLogicVector, "Points", 'points', shape='SQUARE')
        self.add_output(NodeSocketLogicCondition, 'Done', 'OUT')
        GameNodeActionType.init(self, context)

    # XXX: Remove for 5.0
    def get_output_names(self):
        return ["OUT"]

    # XXX: Remove for 5.0
    def get_input_names(self):
        return ["condition", "curve_object", "points"]
