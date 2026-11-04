"""Catalog mapping legacy editor nodes onto native runtime node types.

Each entry attaches `runtime_type_id` plus the native port contract to an
existing legacy node class. The sockets stay untouched, so existing graphs keep
their layout, while the IR exporter can emit them for the native runtime.

Entry format:

    bl_idname: (runtime_id, inputs, outputs, generic_port_types, config)

The port tuples are `(port_name, socket_identifier)` pairs and must follow the
socket declaration order of the legacy node; that order is also the C++ port
order. `generic_port_types` supplies the value tag for sockets whose `nl_type`
is generic (the legacy "any value" sockets). `config` lists
`(key, property, value_map)` triples exported through `export_config`.
"""

from . import node as _node

CATALOG = {
    # ------------------------------------------------------------------ values
    'NLParameterVector3SimpleNode': (
        'Vector3',
        (('X', 'input_x'), ('Y', 'input_y'), ('Z', 'input_z')),
        (('Vector', 'OUTV'),),
        {},
        (),
    ),
    'NLParameterVector3SplitNode': (
        'VectorSplit3',
        (('Vector', 'input_v'),),
        (('X', 'OUTX'), ('Y', 'OUTY'), ('Z', 'OUTZ')),
        {},
        (),
    ),
    'NLInvertValueNode': (
        'ValueInvert',
        (('Value', 'value'),),
        (('Value', 'OUT'),),
        {'Value': 'Float'},
        (),
    ),
    'NLLimitRange': (
        'LimitRange',
        (('Value', 'value'), ('Threshold', 'threshold'),
         ('Min', 'min_value'), ('Max', 'max_value')),
        (('Value', 'OUT'),),
        {'Value': 'Float'},
        (('op', 'operator', {'INSIDE': 'inside', 'OUTSIDE': 'outside'}),),
    ),
    'NLMapRangeNode': (
        'MapRangeNode',
        (('Value', 'value'), ('FromMin', 'from_min'), ('FromMax', 'from_max'),
         ('ToMin', 'to_min'), ('ToMax', 'to_max'), ('Steps', 'steps'),
         ('ValueVector', 'value'), ('FromMinVector', 'from_min'),
         ('FromMaxVector', 'from_max'), ('ToMinVector', 'to_min'),
         ('ToMaxVector', 'to_max'), ('StepsVector', 'steps')),
        (('Result', 'OUT'),),
        {'Result': 'Float'},
        (('clamp', 'clamp', None), ('mode', 'mode', None)),
    ),
    'NLInterpolateValueNode': (
        'InterpolateValue',
        (('From', 'a'), ('To', 'b'), ('Factor', 'fac')),
        (('Value', 'OUT'),),
        {'Value': 'Float'},
        (),
    ),
    'NLThresholdNode': (
        'ThresholdNode',
        (('ElseZero', 'else_z'), ('Value', 'value'), ('Threshold', 'threshold')),
        (('Value', 'OUT'),),
        {'Value': 'Float'},
        (('op', 'operator', {'GREATER': 'gt', 'LESS': 'lt'}),),
    ),
    'NLValueSwitch': (
        'ValueSwitch',
        (('Condition', 'condition'), ('IfTrue', 'val_a'), ('IfFalse', 'val_b')),
        (('Result', 'VAL'),),
        {'IfTrue': 'Float', 'IfFalse': 'Float', 'Result': 'Float'},
        (),
    ),
    'LogicNodeTimeFactor': (
        'TimeFactor',
        (),
        (('Factor', 'TIMEFACTOR'),),
        {},
        (),
    ),
    'NLParameterGetTimeScale': (
        'GetTimeScale',
        (),
        (('Timescale', 'OUT'),),
        {'Timescale': 'Float'},
        (),
    ),
    'NLGetResolution': (
        'GetResolution',
        (),
        (('Width', 'WIDTH'), ('Height', 'HEIGHT'), ('Resolution', 'RES')),
        {'Width': 'Float', 'Height': 'Float'},
        (),
    ),
    'NLGetFullscreen': (
        'GetFullscreen',
        (),
        (('Fullscreen', 'OUT'),),
        {'Fullscreen': 'Bool'},
        (),
    ),
    'NLGetVsyncNode': (
        'GetVsync',
        (),
        (('Mode', 'OUT'),),
        {'Mode': 'Bool'},
        (),
    ),
    'NLMouseDataParameter': (
        'MouseData',
        (),
        (('Position', 'MXY0'), ('Movement', 'MDXY0'), ('X', 'MX'), ('Y', 'MY'),
         ('DeltaX', 'MDX'), ('DeltaY', 'MDY'), ('Wheel', 'MDWHEEL')),
        {'X': 'Float', 'Y': 'Float', 'DeltaX': 'Float', 'DeltaY': 'Float'},
        (),
    ),
    'NLGameObjectPropertyParameterNode': (
        'GetPropertyNode',
        (('Object', 'game_object'), ('Property', 'property_name')),
        (('Value', 'OUT'),),
        {'Value': 'Float'},
        (),
    ),
    'NLWithinRangeNode': (
        'WithinRangeNode',
        (('Value', 'value'), ('Range', 'range'),
         ('Min', 'min_value'), ('Max', 'max_value')),
        (('Out', 'OUT'),),
        {},
        (('op', 'operator', {'INSIDE': 'inside', 'OUTSIDE': 'outside'}),),
    ),

    # B2: legacy nodes whose port contract already matches the native table.
    'NLParameterMathFun': (
        'Formula',
        (('a', 'a'), ('b', 'b')),
        (('Result', 'OUT'),),
        {},
        (('expression', 'value', None),),
    ),
    'NLClampValueNode': (
        'ClampValue',
        (('Value', 'value'), ('Range', 'range'),
         ('Min', 'min_value'), ('Max', 'max_value')),
        (('Result', 'OUT'),),
        {},
        (),
    ),

    # ----------------------------------------------------------------- actions
    'NLActionSetCameraFov': (
        'SetCameraFov',
        (('OnEnter', 'condition'), ('Camera', 'camera'), ('Value', 'fov')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetCameraOrthoScale': (
        'SetCameraOrthoScale',
        (('OnEnter', 'condition'), ('Camera', 'camera'), ('Value', 'scale')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetVSync': (
        'SetVSync',
        (('OnEnter', 'condition'),),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetMousePosition': (
        'SetMousePositionLegacy',
        (('OnEnter', 'condition'), ('X', 'screen_x'), ('Y', 'screen_y')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetGameObjectVisibility': (
        'SetVisibilityLegacy',
        (('OnEnter', 'condition'), ('Object', 'game_object'),
         ('Visible', 'visible'), ('IncludeChildren', 'recursive')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetParentNode': (
        'SetParentLegacy',
        (('OnEnter', 'condition'), ('Child', 'child_object'),
         ('Parent', 'parent_object'), ('Compound', 'compound'), ('Ghost', 'ghost')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionCharacterJump': (
        'CharacterJump',
        (('OnEnter', 'condition'), ('Object', 'game_object')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetCharacterGravity': (
        'CharacterGravity',
        (('OnEnter', 'condition'), ('Object', 'game_object'), ('Vector', 'gravity')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetCharacterVelocity': (
        'CharacterVelocity',
        (('OnEnter', 'condition'), ('Object', 'game_object'),
         ('Vector', 'vel'), ('Time', 'time')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetCharacterWalkDir': (
        'CharacterWalkDir',
        (('OnEnter', 'condition'), ('Object', 'game_object'), ('Vector', 'walkDir')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLSetCharacterJumpSpeed': (
        'CharacterJumpSpeed',
        (('OnEnter', 'condition'), ('Object', 'game_object'), ('Force', 'force')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLSetGameObjectGamePropertyActionNode': (
        'SetPropertyLegacy',
        (('OnEnter', 'condition'), ('Object', 'game_object'),
         ('Property', 'property_name'), ('Value', 'property_value')),
        (('OnDone', 'OUT'),),
        {'Value': 'Float'},
        (),
    ),

    # -------------------------------------------------------------- conditions
    'NLKeyPressedCondition': (
        'KeyPressed',
        (('KeyCode', 'key_code'),),
        (('Out', 'OUT'),),
        {},
        (('mode', 'input_type', {'0': 0, '1': 2, '2': 1}),),
    ),
    'NLMousePressedCondition': (
        'MousePressed',
        (('Button', 'mouse_button_code'),),
        (('Out', 'OUT'),),
        {},
        (('mode', 'input_type', {'0': 0, '1': 2, '2': 1}),),
    ),
    'NLMouseMovedCondition': (
        'MouseMoved',
        (),
        (('IfMoved', 'OUT'),),
        {},
        (),
    ),
    'NLConditionValueChanged': (
        'ValueChanged',
        (('Value', 'current_value'),),
        (('IfChanged', 'OUT'), ('Old', 'OLD'), ('New', 'NEW')),
        {'Value': 'Float', 'Old': 'Float', 'New': 'Float'},
        (),
    ),
    'NLConditionNextFrameNode': (
        'NextFrame',
        (('OnEnter', 'input_condition'),),
        (('Out', 'OUT'),),
        {},
        (),
    ),

    # ----------------------------------------------- aliases to existing nodes
    'NLOnInitConditionNode': (
        'OnInit',
        (),
        (('OnOut', 'OUT'),),
        {},
        (),
    ),
    'NLOnUpdateConditionNode': (
        'OnUpdate',
        (),
        (('OnOut', 'OUT'),),
        {},
        (),
    ),
    'NLActionTimeDelay': (
        'Delay',
        (('OnEnter', 'condition'), ('Duration', 'delay')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetActiveCamera': (
        'SetActiveCamera',
        (('OnEnter', 'condition'), ('Camera', 'camera')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetResolution': (
        'SetResolution',
        (('OnEnter', 'condition'), ('Width', 'x_res'), ('Height', 'y_res')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetFullscreen': (
        'SetFullScreen',
        (('OnEnter', 'condition'), ('Enabled', 'use_fullscreen')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetGravity': (
        'SetGravity',
        (('OnEnter', 'condition'), ('Gravity', 'gravity')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetMouseCursorVisibility': (
        'SetCursorVisible',
        (('OnEnter', 'condition'), ('Visible', 'visibility_status')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionSetTimeScale': (
        'SetTimeScale',
        (('OnEnter', 'condition'), ('Scale', 'timescale')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionRestartGame': (
        'RestartGame',
        (('OnEnter', 'condition'),),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionEndObjectNode': (
        'RemoveObject',
        (('OnEnter', 'condition'), ('Object', 'game_object')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    'NLActionPrint': (
        'Print',
        (('OnEnter', 'condition'), ('Value', 'value')),
        (('OnDone', 'OUT'),),
        {'Value': 'String'},
        (),
    ),
    'NLGetGravityNode': (
        'GetGravity',
        (),
        (('Gravity', 'OUT'),),
        {},
        (),
    ),
    'NLParameterGameObjectParent': (
        'GetParent',
        (('Child', 'game_object'),),
        (('Parent', 'OUT'),),
        {},
        (),
    ),

    # --------------------------------------------------------- B3 animation
    # Exact port match: condition/game_object/action_layer -> OnEnter/Object/Layer.
    'NLActionStopAnimation': (
        'StopAction',
        (('OnEnter', 'condition'), ('Object', 'game_object'), ('Layer', 'action_layer')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),
    # The runtime seeks by layer only; action_name, freeze and layer_weight are
    # not part of the native contract yet, so this is a translated mapping.
    'NLActionSetAnimationFrame': (
        'SetActionFrame',
        (('OnEnter', 'condition'), ('Object', 'game_object'), ('Layer', 'action_layer'),
         ('Frame', 'action_frame')),
        (('OnDone', 'OUT'),),
        {},
        (),
    ),

    # ------------------------------------------------------------ B3 message
    # The native bus is address based; the legacy "From" socket is unused.
    'NLActionSendMessage': (
        'SendMessage',
        (('OnEnter', 'condition'), ('To', 'to_obj'), ('Subject', 'subject'),
         ('Body', 'body')),
        (('OnDone', 'OUT'),),
        {'Subject': 'String', 'Body': 'String'},
        (),
    ),
}


_PROPERTY_TYPE_TAGS = {
    'FLOAT': 'Float',
    'INT': 'Int',
    'BOOL': 'Bool',
    'STRING': 'String',
    'TIMER': 'Float',
}


def _default_property_tag(node):
    """Value tag of the game property named by an unlinked editor socket."""
    if getattr(node, 'mode', '0') != '0':
        return None
    name_socket = node.native_port_socket('Property')
    object_socket = node.native_port_socket('Object')
    name = getattr(name_socket, 'default_value', '') if name_socket is not None else ''
    obj = getattr(object_socket, 'default_value', None) if object_socket is not None else None
    if not name or obj is None or getattr(object_socket, 'is_linked', False):
        return None
    properties = getattr(getattr(obj, 'game', None), 'properties', None)
    if properties is None:
        return None
    prop = properties.get(name)
    if prop is None:
        return None
    return _PROPERTY_TYPE_TAGS.get(getattr(prop, 'type', ''))


def _resolve_valueswitch_port(self, port_name, is_input, ctx):
    """ValueSwitch keeps whichever type is plugged into the branches."""
    if is_input and port_name in ('IfTrue', 'IfFalse'):
        return self.native_linked_type(port_name, ctx) or 'Float'
    if not is_input and port_name == 'Result':
        return (self.native_linked_type('IfTrue', ctx) or
                self.native_linked_type('IfFalse', ctx) or 'Float')
    return None


def _resolve_getproperty_port(self, port_name, is_input, ctx):
    if is_input or port_name != 'Value':
        return None
    return _default_property_tag(self) or 'Float'


def _resolve_setproperty_port(self, port_name, is_input, ctx):
    if is_input and port_name == 'Value':
        return (self.native_linked_type(port_name, ctx) or
                _default_property_tag(self) or 'Float')
    return None


def _resolve_valuechanged_port(self, port_name, is_input, ctx):
    tag = self.native_linked_type('Value', ctx) or 'Float'
    if is_input and port_name == 'Value':
        return tag
    if not is_input and port_name in ('Old', 'New'):
        return tag
    return None


def _resolve_print_port(self, port_name, is_input, ctx):
    if is_input and port_name == 'Value':
        return self.native_linked_type(port_name, ctx) or 'String'
    return None


def _resolve_maprange_port(self, port_name, is_input, ctx):
    """Map Range emits Float or Vector depending on the selected mode."""
    if is_input:
        return None
    if port_name == 'Result':
        return 'Vector' if getattr(self, 'mode', '0') == '1' else 'Float'
    return None


# Nodes whose port value types follow the connected socket, not the table
# default. The C++ specs are flagged `ports_from_ir` so the compiler accepts
# these types without inserting a narrowing conversion.
POLYMORPHIC_PORTS = {
    'NLValueSwitch': _resolve_valueswitch_port,
    'NLGameObjectPropertyParameterNode': _resolve_getproperty_port,
    'NLSetGameObjectGamePropertyActionNode': _resolve_setproperty_port,
    'NLConditionValueChanged': _resolve_valuechanged_port,
    'NLActionPrint': _resolve_print_port,
    'NLMapRangeNode': _resolve_maprange_port,
}


def _make_export_config(config):
    def export_config(self):
        result = {}
        for key, attribute, value_map in config:
            value = getattr(self, attribute)
            if value_map is not None:
                value = value_map.get(value, value)
            result[key] = value
        return result

    return export_config


def apply_native_catalog():
    """Attach the native contract to every catalogued legacy node class."""
    classes = {cls.bl_idname: cls for cls in _node._nodes}
    applied = 0
    for bl_idname, (runtime_id, inputs, outputs, generic, config) in CATALOG.items():
        cls = classes.get(bl_idname)
        if cls is None:
            continue
        if getattr(cls, 'runtime_type_id', ''):
            continue
        cls.runtime_type_id = runtime_id
        cls.native_inputs = tuple(inputs)
        cls.native_outputs = tuple(outputs)
        cls.native_port_types = dict(generic)
        resolver = POLYMORPHIC_PORTS.get(bl_idname)
        if resolver is not None:
            cls.resolve_native_port = resolver
        if config:
            cls.export_config = _make_export_config(config)
        applied += 1
    return applied


apply_native_catalog()
