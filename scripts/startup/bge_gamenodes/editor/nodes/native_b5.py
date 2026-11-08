"""B5.2 native node table: smooth transforms, mouse look and data operators."""
# pylint: disable=invalid-name

from bpy.props import BoolProperty

from .node import GameNodeActionType
from .node import node_type
from .native_batch import _DONE
from .native_batch import _ident
from .native_batch import _make_node
from ..sockets import NodeSocketLogicCondition
from ..sockets import NodeSocketLogicObject
from ..sockets import NodeSocketLogicString
from ..sockets import NodeSocketLogicValue

_B5_LOCAL = ('bool', 'Local Space', False)
_B5_INVERT = {
    'invert_x': ('bool', 'Invert X', False),
    'invert_y': ('bool', 'Invert Y', False),
}
_B5_MATERIAL = {
    'slot': ('int', 'Material Slot', 0),
}

NATIVE_B5 = (
    ('RotateTo', 'Rotate To', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Target', 'Vector'),
      ('Speed', 'Float')],
     _DONE, {'local': _B5_LOCAL}),
    ('MoveTo', 'Move To', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Target', 'Vector'),
      ('Speed', 'Float')],
     _DONE, {'local': _B5_LOCAL}),
    ('MouseLook', 'Mouse Look', 'ACT',
     [('OnEnter', 'Condition'), ('Sensitivity', 'Float'), ('MinPitch', 'Float'),
      ('MaxPitch', 'Float')],
     [('OnUpdate', 'Condition')], _B5_INVERT),
    ('AlignAxisToVector', 'Align Axis To Vector', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Axis', 'Vector'),
      ('Target', 'Vector')],
     _DONE, {}),
    ('SetMaterial', 'Set Material', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Material', 'String')],
     _DONE, _B5_MATERIAL),
)

_PROPERTY_TYPE_TAGS = {
    'FLOAT': 'Float',
    'INT': 'Int',
    'BOOL': 'Bool',
    'STRING': 'String',
    'TIMER': 'Float',
}


def _property_socket(node, name):
    for socket in node.inputs:
        if getattr(socket, 'identifier', '') == _ident(name):
            return socket
    return None


def _default_property_tag(node):
    """Infer the property type from the Object/Property editor sockets."""
    object_socket = _property_socket(node, 'Object')
    property_socket = _property_socket(node, 'Property')
    if object_socket is None or property_socket is None:
        return None
    obj = getattr(object_socket, 'default_value', None)
    prop_name = getattr(property_socket, 'default_value', '')
    if not obj or not prop_name:
        return None
    for prop in getattr(obj.game, 'properties', ()):
        if prop.name == prop_name:
            return _PROPERTY_TYPE_TAGS.get(prop.type, 'Float')
    value = getattr(obj, prop_name, None)
    if isinstance(value, bool):
        return 'Bool'
    if isinstance(value, int):
        return 'Int'
    if isinstance(value, float):
        return 'Float'
    if isinstance(value, str):
        return 'String'
    return None


@node_type
class GameNodeObjectPropertyOperator(GameNodeActionType):
    """Read-modify-write a game property with a numeric operator."""

    bl_idname = 'GameNodeObjectPropertyOperator'
    bl_label = 'Object Property Operator (Native)'
    bl_description = 'Apply add/sub/mul/div/min/max to an object property'
    nl_module = ''
    nl_class = ''
    nl_category = 'Object'
    runtime_type_id = 'ObjectPropertyOperator'
    native_only = True
    native_inputs = (
        ('OnEnter', 'ONENTER'),
        ('Object', 'OBJECT'),
        ('Property', 'PROPERTY'),
        ('Op', 'OP'),
        ('Value', 'VALUE'),
    )
    native_outputs = (
        ('Result', 'RESULT'),
        ('OnDone', 'ONDONE'),
    )
    native_port_types = {
        'Property': 'String',
        'Op': 'String',
        'Value': 'Float',
        'Result': 'Float',
    }

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ONENTER')
        self.add_input(NodeSocketLogicObject, 'Object', 'OBJECT')
        self.add_input(NodeSocketLogicString, 'Property', 'PROPERTY')
        self.add_input(NodeSocketLogicString, 'Op', 'OP')
        self.add_input(NodeSocketLogicValue, 'Value', 'VALUE')
        self.add_output(NodeSocketLogicValue, 'Result', 'RESULT')
        self.add_output(NodeSocketLogicCondition, 'OnDone', 'ONDONE')
        GameNodeActionType.init(self, context)

    def resolve_native_port(self, port_name, is_input, context):
        if port_name in ('Value', 'Result'):
            linked = self.native_linked_type('Value', context)
            if linked:
                return linked
            tag = _default_property_tag(self)
            return tag or 'Float'
        return self.native_port_types.get(port_name)


def _register_table():
    for runtime_id, label, kind, inputs, outputs, config in NATIVE_B5:
        _make_node(runtime_id, label, kind, inputs, outputs, config)


_register_table()
