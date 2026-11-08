"""Table driven editor nodes for the bulk native runtime families.

Every entry mirrors a `KX_FunctionSpec` in `KX_FunctionNode.cpp`: the port
order, trigger/value kinds and configuration keys must match the C++ table.
The runtime resolves connections by IR port index, so the socket declaration
order here is the port order contract.

Node classes are generated from the table below instead of being written out
one by one. Adding a runtime family means adding one row.
"""

from bpy.props import BoolProperty
from bpy.props import EnumProperty
from bpy.props import FloatProperty
from bpy.props import IntProperty
from bpy.props import StringProperty

from .node import GameNodeActionType
from .node import GameNodeConditionType
from .node import GameNodeParameterType
from .node import node_type
from ..sockets import NodeSocketLogicBoolean
from ..sockets import NodeSocketLogicColorRGBA
from ..sockets import NodeSocketLogicCondition
from ..sockets import NodeSocketLogicFloat
from ..sockets import NodeSocketLogicInteger
from ..sockets import NodeSocketLogicList
from ..sockets import NodeSocketLogicObject
from ..sockets import NodeSocketLogicString
from ..sockets import NodeSocketLogicValue
from ..sockets import NodeSocketLogicVectorXYZ

SOCKETS = {
    'Condition': NodeSocketLogicCondition,
    'Bool': NodeSocketLogicBoolean,
    'Int': NodeSocketLogicInteger,
    'Float': NodeSocketLogicFloat,
    'Vector': NodeSocketLogicVectorXYZ,
    'Object': NodeSocketLogicObject,
    'String': NodeSocketLogicString,
    'Color': NodeSocketLogicColorRGBA,
    'List': NodeSocketLogicList,
    # Polymorphic port used by the B6.3 network nodes (type resolved from the link).
    'Value': NodeSocketLogicValue,
}

BASES = {
    'CON': GameNodeConditionType,
    'ACT': GameNodeActionType,
    'PAR': GameNodeParameterType,
}

_INPUT_MODE = {
    'items': [
        ('press', 'Press', 'Active on the activation frame'),
        ('release', 'Release', 'Active on the release frame'),
        ('hold', 'Hold', 'Active while held'),
    ],
    'default': 'hold',
    'values': {'press': 0, 'release': 1, 'hold': 2},
}

_COMPARE = {
    'items': [
        ('eq', 'Equal', ''),
        ('ne', 'Not Equal', ''),
        ('lt', 'Less', ''),
        ('le', 'Less or Equal', ''),
        ('gt', 'Greater', ''),
        ('ge', 'Greater or Equal', ''),
    ],
    'default': 'eq',
}

_LOGIC_GATE = {
    'items': [
        ('and', 'And', ''),
        ('or', 'Or', ''),
        ('xor', 'Xor', ''),
        ('not', 'Not', ''),
        ('nand', 'Nand', ''),
        ('nor', 'Nor', ''),
    ],
    'default': 'and',
}

_OPS = {
    'mode': ('enum', 'Mode', _INPUT_MODE),
    'op_compare': ('compare', 'Operation', _COMPARE),
    'op_gate': ('gate', 'Operation', _LOGIC_GATE),
    'local': ('bool', 'Local Space', False),
    'clamp': ('bool', 'Clamp', True),
}

_SEQUENCE_MODE = {
    'items': [
        ('serial', 'Serial', 'Run each branch subtree before the next one'),
        ('parallel', 'Parallel', 'Fire every branch on the same frame'),
    ],
    'default': 'serial',
}

_DONE = [('OnDone', 'Condition')]
_TRIGGER = [('OnEnter', 'Condition')]


def _ident(name):
    return name.upper().replace(' ', '_').replace('-', '_')


def _prop_for(key, spec, annotations):
    kind = spec[0]
    label = spec[1]
    if kind == 'bool':
        annotations[key] = BoolProperty(name=label, default=bool(spec[2]))
    elif kind == 'int':
        annotations[key] = IntProperty(name=label, default=int(spec[2]))
    elif kind == 'float':
        annotations[key] = FloatProperty(name=label, default=float(spec[2]))
    elif kind == 'string':
        annotations[key] = StringProperty(name=label, default=str(spec[2]))
    elif kind == 'enum':
        info = spec[2]
        annotations[key] = EnumProperty(
            name=label,
            items=[tuple(item) for item in info['items']],
            default=info['default'],
        )
    elif kind == 'compare':
        annotations[key] = EnumProperty(
            name=label,
            items=[tuple(item) for item in spec[2]['items']],
            default=spec[2]['default'],
        )
    elif kind == 'gate':
        annotations[key] = EnumProperty(
            name=label,
            items=[tuple(item) for item in spec[2]['items']],
            default=spec[2]['default'],
        )
    else:
        raise ValueError(f'unknown config kind {kind}')


def _config_keys(config):
    keys = []
    for key, spec in config.items():
        if spec[0] == 'enum':
            keys.append((key, spec[2].get('values')))
        else:
            keys.append((key, None))
    return keys


def _make_node(runtime_id, label, kind, inputs, outputs, config, description=None):
    base = BASES[kind]
    annotations = {}
    for key, spec in config.items():
        _prop_for(key, spec, annotations)

    port_types = {}
    for name, tag in inputs + outputs:
        if tag != 'Condition':
            port_types[name] = tag

    def init(self, context):
        for name, tag in inputs:
            self.add_input(SOCKETS[tag], name, _ident(name))
        for name, tag in outputs:
            self.add_output(SOCKETS[tag], name, _ident(name))
        base.init(self, context)

    def draw_buttons(self, context, layout):
        for key in config:
            layout.prop(self, key, text='')

    def export_config(self):
        result = {}
        for key, values in _config_keys(config):
            value = getattr(self, key)
            if values is not None:
                value = values[value]
            result[key] = value
        return result

    namespace = {
        '__module__': __name__,
        '__annotations__': annotations,
        'bl_idname': 'GameNode' + runtime_id,
        'bl_label': label + ' (Native)',
        'bl_description': description or label,
        'nl_module': '',
        'nl_class': '',
        'runtime_type_id': runtime_id,
        'native_only': True,
        'native_inputs': tuple((name, _ident(name)) for name, _tag in inputs),
        'native_outputs': tuple((name, _ident(name)) for name, _tag in outputs),
        'native_port_types': port_types,
        'init': init,
        'draw_buttons': draw_buttons,
        'export_config': export_config,
    }
    cls = type('GameNode' + runtime_id, (base,), namespace)
    node_type(cls)
    return cls


# (runtime_id, label, kind, inputs, outputs, config)
NATIVE_BATCH = (
    # Input
    ('Keyboard', 'Keyboard Key', 'PAR', [], [('Active', 'Bool')],
     {'key': ('int', 'Key Code', -1), 'mode': _OPS['mode']}),
    ('MouseButton', 'Mouse Button', 'PAR', [], [('Active', 'Bool')],
     {'button': ('int', 'Button', 0), 'mode': _OPS['mode']}),
    ('MousePosition', 'Mouse Position', 'PAR', [], [('Position', 'Vector'), ('Normalized', 'Vector')], {}),
    ('MouseDelta', 'Mouse Delta', 'PAR', [], [('Delta', 'Vector')], {}),
    ('MouseWheel', 'Mouse Wheel', 'PAR', [], [('Wheel', 'Float')], {}),
    ('JoystickAxis', 'Joystick Axis', 'PAR', [], [('Value', 'Float')],
     {'axis': ('int', 'Axis', 0)}),
    ('JoystickButton', 'Joystick Button', 'PAR', [], [('Active', 'Bool')],
     {'button': ('int', 'Button', 0), 'mode': _OPS['mode']}),
    ('SetMousePosition', 'Set Mouse Position', 'ACT',
     [('OnEnter', 'Condition'), ('Position', 'Vector')], _DONE, {}),
    ('SetCursorVisible', 'Show Mouse Cursor', 'ACT',
     [('OnEnter', 'Condition'), ('Visible', 'Bool')], _DONE, {}),

    # Engine actions
    ('Print', 'Print', 'ACT',
     [('OnEnter', 'Condition'), ('Value', 'String')], _DONE, {}),
    ('QuitGame', 'Quit Game', 'ACT', _TRIGGER, _DONE, {}),
    ('RestartGame', 'Restart Game', 'ACT', _TRIGGER, _DONE, {}),
    ('SetTimeScale', 'Set Time Scale', 'ACT',
     [('OnEnter', 'Condition'), ('Scale', 'Float')], _DONE, {}),
    ('SetGravity', 'Set Gravity', 'ACT',
     [('OnEnter', 'Condition'), ('Gravity', 'Vector')], _DONE, {}),
    ('SetResolution', 'Set Resolution', 'ACT',
     [('OnEnter', 'Condition'), ('Width', 'Int'), ('Height', 'Int')], _DONE, {}),
    ('SetFullScreen', 'Set Full Screen', 'ACT',
     [('OnEnter', 'Condition'), ('Enabled', 'Bool')], _DONE, {}),
    ('SetActiveCamera', 'Set Active Camera', 'ACT',
     [('OnEnter', 'Condition'), ('Camera', 'Object')], _DONE, {}),
    ('LoadScene', 'Load Scene', 'ACT',
     [('OnEnter', 'Condition'), ('Scene', 'String')], _DONE,
     {'scene': ('string', 'Scene', '')}),

    # Object actions
    ('RemoveObject', 'Remove Object', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object')], _DONE, {}),
    ('SetVisibility', 'Set Visibility', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Visible', 'Bool')], _DONE, {}),
    ('SetParent', 'Set Parent', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Parent', 'Object')], _DONE, {}),
    ('ClearParent', 'Clear Parent', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object')], _DONE, {}),

    # Physics actions
    ('SetLinearVelocity', 'Set Linear Velocity', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Velocity', 'Vector')], _DONE, {}),
    ('ApplyImpulse', 'Apply Impulse', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Impulse', 'Vector'),
      ('Point', 'Vector')], _DONE, {'local': _OPS['local']}),
    ('ApplyForce', 'Apply Force', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Force', 'Vector'),
      ('Point', 'Vector')], _DONE, {'local': _OPS['local']}),
    ('ApplyTorque', 'Apply Torque', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Torque', 'Vector')],
     _DONE, {'local': _OPS['local']}),
    ('SetDynamics', 'Set Dynamics', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Enabled', 'Bool')], _DONE, {}),
    ('SetRigidBody', 'Set Rigid Body', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Enabled', 'Bool')], _DONE, {}),
    ('SetCollisionMask', 'Set Collision Mask', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Mask', 'Int')], _DONE, {}),

    # Network / animation / text
    ('SendMessage', 'Send Message', 'ACT',
     [('OnEnter', 'Condition'), ('To', 'Object'), ('Subject', 'String'),
      ('Body', 'String')], _DONE, {}),
    ('PlayAction', 'Play Action', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Action', 'String'),
      ('Start', 'Float'), ('End', 'Float'), ('Layer', 'Int'), ('Loop', 'Bool')],
     [('Started', 'Condition'), ('Frame', 'Float')],
     {'action': ('string', 'Action', '')}),
    ('StopAction', 'Stop Action', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Layer', 'Int')], _DONE, {}),
    ('SetActionFrame', 'Set Action Frame', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Layer', 'Int'), ('Frame', 'Float')],
     _DONE, {}),
    ('SetFontText', 'Set Font Text', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Text', 'String')], _DONE, {}),

    # Transform
    ('SetPosition', 'Set Position', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Value', 'Vector')], _DONE, {}),
    ('SetRotation', 'Set Rotation', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Value', 'Vector')], _DONE, {}),
    ('SetScale', 'Set Scale', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Value', 'Vector')], _DONE, {}),
    ('Translate', 'Translate', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Value', 'Vector')], _DONE, {}),

    # Time
    ('Timer', 'Timer', 'CON',
     [('OnEnter', 'Condition'), ('Duration', 'Float')],
     [('Elapsed', 'Float'), ('OnFinished', 'Condition')], {}),
    ('Once', 'Once', 'CON', _TRIGGER, [('OnOut', 'Condition')], {}),
    ('Sequence', 'Sequence', 'CON', _TRIGGER,
     [('Out0', 'Condition'), ('Out1', 'Condition'), ('Out2', 'Condition'),
      ('Out3', 'Condition')],
     {'mode': ('enum', 'Mode', _SEQUENCE_MODE)}),
    ('FrameTime', 'Frame Time', 'PAR', [],
     [('Delta', 'Float'), ('Time', 'Float'), ('Frame', 'Int')], {}),

    # Object queries
    ('WorldPosition', 'World Position', 'PAR', [('Object', 'Object')], [('Value', 'Vector')], {}),
    ('WorldRotation', 'World Rotation', 'PAR', [('Object', 'Object')], [('Value', 'Vector')], {}),
    ('WorldScale', 'World Scale', 'PAR', [('Object', 'Object')], [('Value', 'Vector')], {}),
    ('GetParent', 'Get Parent', 'PAR', [('Object', 'Object')], [('Object', 'Object')], {}),
    ('ObjectByName', 'Object By Name', 'PAR', [], [('Object', 'Object')],
     {'name': ('string', 'Name', '')}),
    ('GetOwner', 'Get Owner', 'PAR', [], [('Object', 'Object')], {}),
    ('IsNone', 'Is None', 'PAR', [('Object', 'Object')], [('Value', 'Bool')], {}),
    ('GetGravity', 'Get Gravity', 'PAR', [], [('Gravity', 'Vector')], {}),
    ('GetVelocity', 'Get Velocity', 'PAR', [('Object', 'Object')], [('Velocity', 'Vector')], {}),
    ('Distance', 'Distance', 'PAR', [('A', 'Object'), ('B', 'Object')], [('Distance', 'Float')], {}),
    ('WithinRange', 'Within Range', 'PAR',
     [('A', 'Object'), ('B', 'Object'), ('Distance', 'Float')], [('Value', 'Bool')], {}),
    ('Collision', 'Collision', 'CON', [('Object', 'Object')],
     [('OnEnter', 'Condition'), ('OnStay', 'Condition'), ('OnLeave', 'Condition'),
      ('ContactPoint', 'Vector'), ('ContactNormal', 'Vector'),
      ('ImpactForce', 'Float')], {}),

    # Math / logic
    ('Compare', 'Compare', 'PAR', [('A', 'Float'), ('B', 'Float')], [('Value', 'Bool')],
     {'op': _OPS['op_compare']}),
    ('LogicGate', 'Logic Gate', 'CON',
     [('A', 'Condition'), ('B', 'Condition')],
     [('Result', 'Bool'), ('OnTrue', 'Condition'), ('OnFalse', 'Condition')],
     {'op': _OPS['op_gate']}),
    ('LogicNot', 'Logic Not', 'PAR', [('Value', 'Bool')], [('Value', 'Bool')], {}),
    ('Clamp', 'Clamp', 'PAR',
     [('Value', 'Float'), ('Min', 'Float'), ('Max', 'Float')], [('Value', 'Float')], {}),
    ('MapRange', 'Map Range', 'PAR',
     [('Value', 'Float'), ('FromMin', 'Float'), ('FromMax', 'Float'),
      ('ToMin', 'Float'), ('ToMax', 'Float')], [('Value', 'Float')],
     {'clamp': _OPS['clamp']}),
    ('Interpolate', 'Interpolate', 'PAR',
     [('A', 'Float'), ('B', 'Float'), ('Factor', 'Float')], [('Value', 'Float')],
     {'clamp': _OPS['clamp']}),
    ('RandomFloat', 'Random Float', 'PAR', [('Min', 'Float'), ('Max', 'Float')],
     [('Value', 'Float')], {}),
    ('Threshold', 'Threshold', 'PAR', [('Value', 'Float'), ('Threshold', 'Float')],
     [('Value', 'Bool')], {'op': _OPS['op_compare']}),

    # String / colour / list
    ('StringConst', 'String', 'PAR', [], [('Value', 'String')],
     {'value': ('string', 'Value', '')}),
    ('StringConcat', 'String Join', 'PAR', [('A', 'String'), ('B', 'String')],
     [('Value', 'String')], {}),
    ('StringLength', 'String Length', 'PAR', [('Text', 'String')], [('Value', 'Int')], {}),
    ('StringReplace', 'String Replace', 'PAR',
     [('Text', 'String'), ('Find', 'String'), ('Replace', 'String')],
     [('Value', 'String')], {}),
    ('ColorConst', 'Color', 'PAR', [], [('Color', 'Color')],
     {'r': ('float', 'Red', 0.0), 'g': ('float', 'Green', 0.0),
      'b': ('float', 'Blue', 0.0), 'a': ('float', 'Alpha', 1.0)}),
    ('ColorFromVector', 'Color From Vector', 'PAR', [('Vector', 'Vector')],
     [('Color', 'Color')], {}),
    ('VectorFromColor', 'Vector From Color', 'PAR', [('Color', 'Color')],
     [('Value', 'Vector')], {}),
    ('ListLength', 'List Length', 'PAR', [('List', 'List')], [('Value', 'Int')], {}),
)

def _resolve_print_port(self, port_name, is_input, ctx):
    if is_input and port_name == 'Value':
        return self.native_linked_type(port_name, ctx) or 'String'
    return None


for _spec in NATIVE_BATCH:
    _cls = _make_node(*_spec)
    if _spec[0] == 'Print':
        _cls.resolve_native_port = _resolve_print_port
