"""Editor nodes for the B2 native families.

Every fixed port node mirrors a `KX_FunctionSpec` in `KX_FunctionNodesB2.cpp`:
socket declaration order, trigger/value kinds and configuration keys must match
the C++ table. The runtime resolves connections by IR port index.

`Formula` and `StringFormat` carry a variable number of ports, so they are
written out by hand and flagged `native_dynamic_ports`. The IR exporter reads
their port names from the enabled sockets, and the C++ nodes resolve their own
ports from the IR (`ports_from_ir`).
"""

from bpy.props import StringProperty

from .node import GameNodeParameterType
from .node import node_type
from ..sockets import NodeSocketLogicFloat
from ..sockets import NodeSocketLogicString

from .native_batch import _make_node

# (runtime_id, label, kind, inputs, outputs, config)
NATIVE_B2 = (
    # B2.1 math / vector
    ('RandomValue', 'Random Value', 'PAR',
     [('Min', 'Float'), ('Max', 'Float')], [('Result', 'Float')], {}),
    ('ClampValue', 'Clamp', 'PAR',
     [('Value', 'Float'), ('Range', 'Vector'), ('Min', 'Float'), ('Max', 'Float')],
     [('Result', 'Float')], {}),
    ('Wrap', 'Wrap', 'PAR',
     [('Value', 'Float'), ('Min', 'Float'), ('Max', 'Float')],
     [('Result', 'Float')], {}),
    ('Round', 'Round', 'PAR', [('Value', 'Float')], [('Result', 'Float')],
     {'mode': ('enum', 'Mode', {
         'items': [
             ('round', 'Round', ''),
             ('floor', 'Floor', ''),
             ('ceil', 'Ceil', ''),
             ('trunc', 'Truncate', ''),
         ],
         'default': 'round',
     })}),
    ('Sign', 'Sign', 'PAR', [('Value', 'Float')], [('Result', 'Float')], {}),
    ('VectorReflect', 'Vector Reflect', 'PAR',
     [('A', 'Vector'), ('B', 'Vector')], [('Result', 'Vector')], {}),
    ('VectorProject', 'Vector Project', 'PAR',
     [('A', 'Vector'), ('B', 'Vector')], [('Result', 'Vector')], {}),
    ('VectorRotateToward', 'Vector Rotate Toward', 'PAR',
     [('From', 'Vector'), ('To', 'Vector'), ('MaxRadians', 'Float')],
     [('Result', 'Vector')], {}),
    ('VectorAngle', 'Vector Angle', 'PAR',
     [('A', 'Vector'), ('B', 'Vector')], [('Result', 'Float')], {}),
    ('VectorDistance', 'Vector Distance', 'PAR',
     [('A', 'Vector'), ('B', 'Vector')], [('Result', 'Float')], {}),

    # B2.2 colour
    ('ColorMix', 'Color Mix', 'PAR',
     [('A', 'Color'), ('B', 'Color'), ('Factor', 'Float')],
     [('Result', 'Color')], {}),
    ('ColorLerp', 'Color Lerp', 'PAR',
     [('A', 'Color'), ('B', 'Color'), ('T', 'Float')],
     [('Result', 'Color')], {}),
    ('ColorToRGBA', 'Color To RGBA', 'PAR',
     [('Color', 'Color')],
     [('R', 'Float'), ('G', 'Float'), ('B', 'Float'), ('A', 'Float')], {}),
    ('RGBAToColor', 'RGBA To Color', 'PAR',
     [('R', 'Float'), ('G', 'Float'), ('B', 'Float'), ('A', 'Float')],
     [('Result', 'Color')], {}),
    ('HSVToRGB', 'HSV To RGB', 'PAR',
     [('H', 'Float'), ('S', 'Float'), ('V', 'Float')],
     [('Result', 'Color')], {}),
    ('RGBToHSV', 'RGB To HSV', 'PAR',
     [('Color', 'Color')],
     [('H', 'Float'), ('S', 'Float'), ('V', 'Float')], {}),

    # B2.3 string
    ('StringCompare', 'String Compare', 'PAR',
     [('A', 'String'), ('B', 'String')], [('Result', 'Bool')],
     {'mode': ('enum', 'Mode', {
         'items': [
             ('eq', 'Equal', ''),
             ('ne', 'Not Equal', ''),
             ('lt', 'Less', ''),
             ('gt', 'Greater', ''),
         ],
         'default': 'eq',
     })}),
    ('StringFind', 'String Find', 'PAR',
     [('Haystack', 'String'), ('Needle', 'String')], [('Index', 'Int')], {}),
    ('StringToUpper', 'String To Upper', 'PAR',
     [('Source', 'String')], [('Result', 'String')], {}),
    ('StringToLower', 'String To Lower', 'PAR',
     [('Source', 'String')], [('Result', 'String')], {}),
    ('StringSubstr', 'String Substring', 'PAR',
     [('Source', 'String'), ('Start', 'Int'), ('Length', 'Int')],
     [('Result', 'String')], {}),
    ('StringToNumber', 'String To Number', 'ACT',
     [('OnEnter', 'Condition'), ('Source', 'String')],
     [('Result', 'Float'), ('OnSuccess', 'Condition'), ('OnFail', 'Condition')], {}),
    ('NumberToString', 'Number To String', 'PAR',
     [('Value', 'Float')], [('Result', 'String')],
     {'precision': ('int', 'Precision', 3)}),

    # B2.4 list
    ('ListAdd', 'List Add', 'PAR',
     [('List', 'List'), ('Value', 'Float')], [('List', 'List')], {}),
    ('ListRemove', 'List Remove', 'PAR',
     [('List', 'List'), ('Index', 'Int')], [('List', 'List')], {}),
    ('ListSet', 'List Set', 'PAR',
     [('List', 'List'), ('Index', 'Int'), ('Value', 'Float')],
     [('List', 'List')], {}),
    ('ListGet', 'List Get', 'ACT',
     [('OnEnter', 'Condition'), ('List', 'List'), ('Index', 'Int')],
     [('Value', 'Float'), ('OnSuccess', 'Condition'), ('OnFail', 'Condition')], {}),
    ('ListContains', 'List Contains', 'PAR',
     [('List', 'List'), ('Value', 'Float')], [('Result', 'Bool')], {}),
    ('ListClear', 'List Clear', 'PAR', [('List', 'List')], [('List', 'List')], {}),
    ('ForEach', 'For Each', 'CON',
     [('List', 'List'), ('OnEnter', 'Condition')],
     [('OnEach', 'Condition'), ('Element', 'Float'), ('OnDone', 'Condition')], {}),
)

for _spec in NATIVE_B2:
    _make_node(*_spec)


def _formula_variables(expression):
    """Return the variable names of an expression, in first appearance order."""
    names = []
    index = 0
    size = len(expression)
    while index < size:
        char = expression[index]
        if char.isdigit() or char == '.':
            while index < size and (expression[index].isdigit() or expression[index] == '.'):
                index += 1
            if index < size and expression[index] in 'eE':
                index += 1
                if index < size and expression[index] in '+-':
                    index += 1
                while index < size and expression[index].isdigit():
                    index += 1
            continue
        if char.isalpha() or char == '_':
            start = index
            while index < size and (expression[index].isalnum() or expression[index] == '_'):
                index += 1
            name = expression[start:index]
            if name not in names:
                names.append(name)
            continue
        index += 1
    return names


def _sync_formula(node):
    names = _formula_variables(node.expression)
    if [socket.identifier for socket in node.inputs] == names:
        return
    while len(node.inputs):
        node.inputs.remove(node.inputs[-1])
    for name in names:
        node.add_input(NodeSocketLogicFloat, name, name)


def _update_formula(node, context=None):
    if not node.ready:
        return
    _sync_formula(node)


def _format_arg_count(text):
    """Number of `{n}` placeholders, mirroring the C++ StringFormat scan."""
    count = 0
    index = 0
    while index < len(text):
        if text[index] == '{':
            close = text.find('}', index + 1)
            if close != -1:
                inner = text[index + 1:close]
                if inner.isdigit():
                    count = max(count, int(inner) + 1)
                    index = close + 1
                    continue
        index += 1
    return count


def _sync_format(node):
    count = _format_arg_count(node.format)
    if len(node.inputs) == count:
        return
    while len(node.inputs):
        node.inputs.remove(node.inputs[-1])
    for index in range(count):
        node.add_input(NodeSocketLogicString, 'Arg %d' % index, 'ARG_%d' % index)


def _update_format(node, context=None):
    if not node.ready:
        return
    _sync_format(node)


@node_type
class GameNodeFormula(GameNodeParameterType):
    bl_idname = 'GameNodeFormula'
    bl_label = 'Formula (Native)'
    bl_description = 'Evaluate an arithmetic expression over input variables'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'Formula'
    native_only = True
    native_dynamic_ports = True
    native_outputs = (('Result', 'RESULT'),)

    expression: StringProperty(
        name='Formula',
        default='(a + b) * 2',
        update=_update_formula,
    )

    def init(self, context):
        _sync_formula(self)
        self.add_output(NodeSocketLogicFloat, 'Result', 'RESULT')
        GameNodeParameterType.init(self, context)

    def draw_buttons(self, context, layout):
        layout.prop(self, 'expression', text='')

    def export_config(self):
        variables = _formula_variables(self.expression)
        return {
            'expression': self.expression,
            'vars': ','.join(variables),
        }

    def resolve_native_port(self, port_name, is_input, ctx):
        return 'Float'


@node_type
class GameNodeStringFormat(GameNodeParameterType):
    bl_idname = 'GameNodeStringFormat'
    bl_label = 'String Format (Native)'
    bl_description = 'Build a string from a template with {0}, {1}, ... placeholders'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'StringFormat'
    native_only = True
    native_dynamic_ports = True
    native_outputs = (('Result', 'RESULT'),)

    format: StringProperty(
        name='Format',
        default='{0} {1}',
        update=_update_format,
    )

    def init(self, context):
        _sync_format(self)
        self.add_output(NodeSocketLogicString, 'Result', 'RESULT')
        GameNodeParameterType.init(self, context)

    def draw_buttons(self, context, layout):
        layout.prop(self, 'format', text='')

    def export_config(self):
        return {
            'format': self.format,
            'arg_count': _format_arg_count(self.format),
        }

    def resolve_native_port(self, port_name, is_input, ctx):
        return 'String'
