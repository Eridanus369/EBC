from ..node import node_type
from ..node import GameNodeActionType
from ...sockets import NodeSocketLogicCondition
from ...sockets import NodeSocketLogicObject
from ...sockets import NodeSocketLogicVectorXYZAngle
from bpy.props import BoolProperty


@node_type
class GameNodeApplyRotation(GameNodeActionType):
    bl_idname = "NLActionApplyRotation"
    bl_label = "Apply Rotation"
    nl_module = 'uplogic.nodes.actions'
    nl_class = "ULApplyRotation"

    deprecated = True
    deprecation_message = 'Replaced by "Apply Transform" Node.'

    local: BoolProperty(default=True, name='Local')

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'Condition')
        self.add_input(NodeSocketLogicObject, 'Object')
        self.add_input(NodeSocketLogicVectorXYZAngle, 'Vector')
        self.add_output(NodeSocketLogicCondition, 'Done')
        GameNodeActionType.init(self, context)

    def get_output_names(self):
        return ["OUT"]

    def draw_buttons(self, context, layout):
        layout.prop(self, "local")

    def get_input_names(self):
        return ["condition", "game_object", "rotation"]

    def get_attributes(self):
        return [("local", self.local)]
