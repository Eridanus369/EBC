"""Table driven editor nodes for the B4 file domain runtime family.

Fixed port nodes mirror the `KX_FunctionSpec` rows in
`KX_FunctionNodesB4File.cpp`: socket order, trigger/value kinds and config keys
are the port contract. `JSONStringify` and `JSONParse` derive their value
sockets from the `schema` config, so they are written by hand and flagged
`native_dynamic_ports`; the C++ nodes read those ports back from the IR.
"""

from bpy.props import StringProperty

from .node import GameNodeActionType
from .node import node_type
from ..sockets import NodeSocketLogicBoolean
from ..sockets import NodeSocketLogicCondition
from ..sockets import NodeSocketLogicFloat
from ..sockets import NodeSocketLogicInteger
from ..sockets import NodeSocketLogicString

from .native_batch import _make_node

_SCHEMA_SOCKETS = {
    'string': NodeSocketLogicString,
    'float': NodeSocketLogicFloat,
    'int': NodeSocketLogicInteger,
    'bool': NodeSocketLogicBoolean,
}

_FORMAT = {
    'items': [
        ('json', 'JSON', 'Serialize the asset tables as JSON'),
        ('binary', 'Binary', 'Reserved: only json is implemented'),
    ],
    'default': 'json',
}


def _schema_fields(schema):
    """Parse `key:type,key:type` into (key, type) pairs, dropping invalid parts."""
    fields = []
    seen = set()
    for part in schema.split(','):
        part = part.strip()
        if ':' not in part:
            continue
        key, value_type = part.split(':', 1)
        key = key.strip()
        value_type = value_type.strip().lower()
        if not key or value_type not in _SCHEMA_SOCKETS or key in seen:
            continue
        seen.add(key)
        fields.append((key, value_type))
    return fields


def _schema_identifier(key):
    identifier = ''.join(char if char.isalnum() or char == '_' else '_' for char in key)
    return identifier.upper() or 'FIELD'


def _socket_key(text):
    """Compare socket identifiers ignoring separators Blender may normalise."""
    return ''.join(char for char in text.upper() if char.isalnum())


def _sync_stringify(node):
    fields = _schema_fields(node.schema)
    expected = ['ONENTER'] + [_schema_identifier(key) for key, _ in fields]
    if [_socket_key(socket.identifier) for socket in node.inputs] == expected:
        return
    while len(node.inputs):
        node.inputs.remove(node.inputs[-1])
    node.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
    for key, value_type in fields:
        node.add_input(_SCHEMA_SOCKETS[value_type], key, _schema_identifier(key))


def _sync_parse(node):
    fields = _schema_fields(node.schema)
    expected = ([_schema_identifier(key) for key, _ in fields]
                + ['ONSUCCESS', 'ONFAIL'])
    if [_socket_key(socket.identifier) for socket in node.outputs] == expected:
        return
    while len(node.outputs):
        node.outputs.remove(node.outputs[-1])
    for key, value_type in fields:
        node.add_output(_SCHEMA_SOCKETS[value_type], key, _schema_identifier(key))
    node.add_output(NodeSocketLogicCondition, 'OnSuccess', 'ON_SUCCESS')
    node.add_output(NodeSocketLogicCondition, 'OnFail', 'ON_FAIL')


def _update_stringify(node, context=None):
    if not node.ready:
        return
    _sync_stringify(node)


def _update_parse(node, context=None):
    if not node.ready:
        return
    _sync_parse(node)


@node_type
class GameNodeJSONStringify(GameNodeActionType):
    bl_idname = 'GameNodeJSONStringify'
    bl_label = 'JSON Stringify (Native)'
    bl_description = 'Build a JSON object from schema-typed inputs'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'JSONStringify'
    native_only = True
    native_dynamic_ports = True
    native_outputs = (('Result', 'RESULT'),)

    schema: StringProperty(
        name='Schema',
        default='name:string,hp:float',
        update=_update_stringify,
    )

    def init(self, context):
        _sync_stringify(self)
        self.add_output(NodeSocketLogicString, 'Result', 'RESULT')
        self.add_output(NodeSocketLogicCondition, 'OnSuccess', 'ON_SUCCESS')
        self.add_output(NodeSocketLogicCondition, 'OnFail', 'ON_FAIL')
        GameNodeActionType.init(self, context)

    def draw_buttons(self, context, layout):
        layout.prop(self, 'schema', text='')

    def export_config(self):
        return {'schema': self.schema}


@node_type
class GameNodeJSONParse(GameNodeActionType):
    bl_idname = 'GameNodeJSONParse'
    bl_label = 'JSON Parse (Native)'
    bl_description = 'Read schema-typed fields from a JSON object'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'JSONParse'
    native_only = True
    native_dynamic_ports = True
    native_outputs = (('OnSuccess', 'ON_SUCCESS'), ('OnFail', 'ON_FAIL'))

    schema: StringProperty(
        name='Schema',
        default='name:string,hp:float',
        update=_update_parse,
    )

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
        self.add_input(NodeSocketLogicString, 'Source', 'SOURCE')
        _sync_parse(self)
        GameNodeActionType.init(self, context)

    def draw_buttons(self, context, layout):
        layout.prop(self, 'schema', text='')

    def export_config(self):
        return {'schema': self.schema}


# (runtime_id, label, kind, inputs, outputs, config)
NATIVE_B4B = (
    ('SaveGame', 'Save Game', 'ACT',
     [('OnEnter', 'Condition'), ('Slot', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'format': ('enum', 'Format', _FORMAT)}),
    ('LoadGame', 'Load Game', 'ACT',
     [('OnEnter', 'Condition'), ('Slot', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'format': ('enum', 'Format', _FORMAT)}),
    ('ReadFile', 'Read File', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('Content', 'String'), ('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'binary': ('bool', 'Binary', False)}),
    ('WriteFile', 'Write File', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String'), ('Content', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'binary': ('bool', 'Binary', False)}),
    ('FileExists', 'File Exists', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('Exists', 'Bool')], {}),
    ('DeleteFile', 'Delete File', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')], {}),

    # Dynamic port nodes are defined by the hand written classes above; these
    # rows document their config contract for the consistency scanners.
    ('JSONStringify', 'JSON Stringify', 'ACT',
     [('OnEnter', 'Condition')],
     [('Result', 'String'), ('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'schema': ('string', 'Schema', 'name:string,hp:float')}),
    ('JSONParse', 'JSON Parse', 'ACT',
     [('OnEnter', 'Condition'), ('Source', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'schema': ('string', 'Schema', 'name:string,hp:float')}),
)

_DYNAMIC_IDS = {'JSONStringify', 'JSONParse'}

for _spec in NATIVE_B4B:
    if _spec[0] in _DYNAMIC_IDS:
        continue
    _make_node(*_spec)
