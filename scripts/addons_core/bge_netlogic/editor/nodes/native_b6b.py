"""B6.2 native node table: input extensions, string/list and audio.

The rows mirror the `KX_FunctionSpec` tables in
`KX_FunctionNodesB6Input.cpp`, `KX_FunctionNodesB6String.cpp` and
`KX_FunctionNodesB6Audio.cpp`. Enum configs use `values` so the exported
configuration is the integer the C++ side reads through `ConfigInt`.
"""
# pylint: disable=invalid-name

from .native_batch import _DONE
from .native_batch import _make_node

_GAMEPAD_BUTTON = {
    'items': [
        ('a', 'A', 'South face button'),
        ('b', 'B', 'East face button'),
        ('x', 'X', 'West face button'),
        ('y', 'Y', 'North face button'),
        ('back', 'Back', 'Back / select'),
        ('guide', 'Guide', 'Guide / home'),
        ('start', 'Start', 'Start / menu'),
        ('left_stick', 'Left Stick', 'Left stick press'),
        ('right_stick', 'Right Stick', 'Right stick press'),
        ('left_bumper', 'Left Bumper', 'Left shoulder'),
        ('right_bumper', 'Right Bumper', 'Right shoulder'),
        ('dpad_up', 'D-Pad Up', ''),
        ('dpad_down', 'D-Pad Down', ''),
        ('dpad_left', 'D-Pad Left', ''),
        ('dpad_right', 'D-Pad Right', ''),
    ],
    'default': 'a',
    'values': {
        'a': 0, 'b': 1, 'x': 2, 'y': 3, 'back': 4, 'guide': 5, 'start': 6,
        'left_stick': 7, 'right_stick': 8, 'left_bumper': 9, 'right_bumper': 10,
        'dpad_up': 11, 'dpad_down': 12, 'dpad_left': 13, 'dpad_right': 14,
    },
}

_GAMEPAD_AXIS = {
    'items': [
        ('left', 'Left Stick', 'X and Y of the left stick'),
        ('right', 'Right Stick', 'X and Y of the right stick'),
    ],
    'default': 'left',
    'values': {'left': 0, 'right': 1},
}

_GAMEPAD_TRIGGER = {
    'items': [
        ('left', 'Left Trigger', ''),
        ('right', 'Right Trigger', ''),
    ],
    'default': 'left',
    'values': {'left': 0, 'right': 1},
}

_EDGE = {
    'items': [
        ('press', 'Press', 'Fires once on the activation frame'),
        ('release', 'Release', 'Fires once on the release frame'),
        ('hold', 'Hold', 'Fires every frame while held'),
    ],
    'default': 'press',
    'values': {'press': 0, 'release': 1, 'hold': 2},
}


NATIVE_B6B = (
    # ------------------------------------------------------------------ input
    ('GamepadButton', 'Gamepad Button', 'CON',
     [],
     [('OnTrigger', 'Condition'), ('IsDown', 'Bool')],
     {'pad': ('int', 'Pad Index', 0), 'button': ('enum', 'Button', _GAMEPAD_BUTTON),
      'edge': ('enum', 'Edge', _EDGE)}),
    ('GamepadAxis', 'Gamepad Axis', 'PAR',
     [],
     [('X', 'Float'), ('Y', 'Float')],
     {'pad': ('int', 'Pad Index', 0), 'axis': ('enum', 'Axis', _GAMEPAD_AXIS)}),
    ('GamepadTrigger', 'Gamepad Trigger', 'PAR',
     [],
     [('Value', 'Float')],
     {'pad': ('int', 'Pad Index', 0), 'trigger': ('enum', 'Trigger', _GAMEPAD_TRIGGER)}),
    ('TouchInput', 'Touch Input', 'PAR',
     [],
     [('Count', 'Int'), ('Position0', 'Vector'), ('Position1', 'Vector'),
      ('Down0', 'Bool')],
     {}),
    ('KeyCombo', 'Key Combo', 'CON',
     [],
     [('OnTrigger', 'Condition'), ('IsDown', 'Bool')],
     {'keys': ('string', 'Keys', 'ctrl+shift+a'), 'edge': ('enum', 'Edge', _EDGE)}),
    ('InputMap', 'Input Map', 'ACT',
     [('OnEnter', 'Condition'), ('ActionName', 'String'), ('Binding', 'String')],
     _DONE, {}),
    ('GetInputValue', 'Get Input Value', 'PAR',
     [('ActionName', 'String')],
     [('Value', 'Float'), ('IsDown', 'Bool')],
     {}),
    ('VibrateGamepad', 'Vibrate Gamepad', 'ACT',
     [('OnEnter', 'Condition'), ('Pad', 'Int'), ('Intensity', 'Float'),
      ('Duration', 'Float')],
     _DONE, {}),

    # --------------------------------------------------------- string / list
    ('StringSplit', 'String Split', 'PAR',
     [('Source', 'String'), ('Separator', 'String')],
     [('Parts', 'List')], {}),
    ('StringJoin', 'String Join', 'PAR',
     [('Parts', 'List'), ('Separator', 'String')],
     [('Result', 'String')], {}),
    ('StringRegexMatch', 'String Regex Match', 'PAR',
     [('Source', 'String'), ('Pattern', 'String')],
     [('Matched', 'Bool'), ('Groups', 'List')], {},
     'Avoid complex regular expressions on user supplied text of arbitrary length.'),
    ('StringRegexReplace', 'String Regex Replace', 'PAR',
     [('Source', 'String'), ('Pattern', 'String'), ('Replacement', 'String')],
     [('Result', 'String')], {},
     'Avoid complex regular expressions on user supplied text of arbitrary length.'),
    ('StringTrim', 'String Trim', 'PAR',
     [('Source', 'String'), ('Mode', 'Int')],
     [('Result', 'String')], {}),
    ('StringPad', 'String Pad', 'PAR',
     [('Source', 'String'), ('Length', 'Int'), ('PadChar', 'String'),
      ('Side', 'Int')],
     [('Result', 'String')], {}),
    ('StringLengthChars', 'String Length (Chars)', 'PAR',
     [('Source', 'String')],
     [('Count', 'Int')], {}),
    ('StringPadChars', 'String Pad (Chars)', 'PAR',
     [('Source', 'String'), ('Length', 'Int'), ('PadChar', 'String'),
      ('Side', 'Int')],
     [('Result', 'String')], {}),
    ('ListSort', 'List Sort', 'PAR',
     [('List', 'List')],
     [('SortedList', 'List')],
     {'key': ('string', 'Sort Key', ''), 'descending': ('bool', 'Descending', False)}),
    ('ListFilter', 'List Filter', 'PAR',
     [('List', 'List'), ('Predicate', 'String')],
     [('FilteredList', 'List')], {}),
    ('ListMap', 'List Map', 'PAR',
     [('List', 'List'), ('Expression', 'String')],
     [('MappedList', 'List')], {}),
    ('ListFind', 'List Find', 'PAR',
     [('List', 'List'), ('Value', 'Float')],
     [('Index', 'Int'), ('Found', 'Bool')], {}),
    ('ListSlice', 'List Slice', 'PAR',
     [('List', 'List'), ('Start', 'Int'), ('End', 'Int')],
     [('Slice', 'List')], {}),
    ('ListConcat', 'List Concat', 'PAR',
     [('ListA', 'List'), ('ListB', 'List')],
     [('Result', 'List')], {}),

    # ------------------------------------------------------------------ audio
    ('SetAudioAttenuation', 'Set Audio Attenuation', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Model', 'Int'),
      ('RefDist', 'Float'), ('MaxDist', 'Float')],
     _DONE, {}),
    ('SetAudioDoppler', 'Set Audio Doppler', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Factor', 'Float')],
     _DONE, {}),
    ('AudioZone', 'Audio Zone', 'ACT',
     [('OnEnter', 'Condition'), ('ZoneName', 'String'), ('Sound', 'String'),
      ('Volume', 'Float')],
     _DONE, {}),
    ('SetAudioBusVolume', 'Set Audio Bus Volume', 'ACT',
     [('OnEnter', 'Condition'), ('Bus', 'String'), ('Volume', 'Float')],
     _DONE, {}),
    ('PlaySound3D', 'Play Sound 3D', 'ACT',
     [('OnEnter', 'Condition'), ('Sound', 'String'), ('Position', 'Vector'),
      ('Volume', 'Float'), ('Pitch', 'Float')],
     _DONE, {}),
)


for _spec in NATIVE_B6B:
    _make_node(*_spec)
