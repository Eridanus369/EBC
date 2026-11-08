import bpy
from .globalvalue import GameNodesGlobalValue
from .property import propgroup
from .property import check_double_cat


@propgroup
class GameNodesGlobalCategory(bpy.types.PropertyGroup):
    name: bpy.props.StringProperty(name='Name', default='category', update=check_double_cat)
    content: bpy.props.CollectionProperty(type=GameNodesGlobalValue)
    selected: bpy.props.IntProperty(name='Value')