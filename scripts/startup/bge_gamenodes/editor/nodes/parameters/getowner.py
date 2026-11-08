from ..node import node_type
from ..node import GameNodeParameterType
from ...sockets import NodeSocketLogicObject


@node_type
class GameNodeGetOwner(GameNodeParameterType):
    """The owner of this logic tree.
    Each Object that has this tree installed is
    the "owner" of a logic tree
    """
    bl_idname = "NLOwnerGameObjectParameterNode"
    bl_label = "Get Owner"
    nl_module = 'uplogic.nodes.parameters'
    nl_class = "ULGetOwner"
    deprecated = True

    def init(self, context):
        self.add_output(NodeSocketLogicObject, "Owner Object")
        GameNodeParameterType.init(self, context)

    def get_attributes(self):
        return [('owner', 'game_object')]
