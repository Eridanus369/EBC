"""Table driven editor nodes for the B4.1 UI family.

Every row mirrors a `KX_FunctionSpec` entry in `KX_FunctionNodesB4.cpp`:
the socket order matches the legacy `ui/*.py` declarations, the tags match the
C++ value types, and every enum config key has a matching `ConfigInt` reader.

Font and image sockets carry datablock names as strings (decision D3); the
runtime resolves them from `bmain` and reports a missing name on stderr.
"""

from .native_batch import _make_node

_ALIGN_X = {
    'items': [('left', 'Left', ''), ('center', 'Center', ''), ('right', 'Right', '')],
    'default': 'left',
    'values': {'left': 0, 'center': 1, 'right': 2},
}

_ALIGN_Y = {
    'items': [('bottom', 'Bottom', ''), ('center', 'Center', ''), ('top', 'Top', '')],
    'default': 'top',
    'values': {'bottom': 0, 'center': 1, 'top': 2},
}

_SCALE_MODE = {
    'items': [('stretch', 'Stretch', ''), ('fit', 'Fit', ''), ('fill', 'Fill', '')],
    'default': 'stretch',
    'values': {'stretch': 0, 'fit': 1, 'fill': 2},
}

_LAYOUT_TYPE = {
    'items': [('sequential', 'Sequential', ''), ('box', 'Box', ''),
              ('polar', 'Polar', '')],
    'default': 'sequential',
    'values': {'sequential': 0, 'box': 1, 'polar': 2},
}

_ORIENTATION = {
    'items': [('horizontal', 'Horizontal', ''), ('vertical', 'Vertical', '')],
    'default': 'horizontal',
    'values': {'horizontal': 0, 'vertical': 1},
}

_MOVE_MODE = {
    'items': [('front', 'Front', ''), ('up', 'Up', ''), ('down', 'Down', ''), ('back', 'Back', '')],
    'default': 'front',
    'values': {'front': 0, 'up': 1, 'down': 2, 'back': 3},
}

_WIDGET_ATTR = {
    'items': [
        ('show', 'Visibility', ''),
        ('bg_color', 'Color', ''),
        ('opacity', 'Opacity', ''),
        ('pos', 'Position', ''),
        ('size', 'Size', ''),
        ('angle', 'Angle', ''),
        ('width', 'Width', ''),
        ('height', 'Height', ''),
        ('border_width', 'Border Width', ''),
        ('border_color', 'Border Color', ''),
        ('hover_color', 'Hover Color', ''),
        ('text', 'Text', ''),
        ('font', 'Font', ''),
        ('font_color', 'Font Color', ''),
        ('font_size', 'Font Size', ''),
        ('line_height', 'Line Height', ''),
        ('text_halign', 'Text X Align', ''),
        ('text_valign', 'Text Y Align', ''),
        ('wrap', 'Word Wrap', ''),
        ('shadow', 'Use Shadow', ''),
        ('shadow_offset', 'Shadow Offset', ''),
        ('shadow_color', 'Shadow Color', ''),
        ('texture', 'Image', ''),
        ('orientation', 'Orientation', ''),
        ('spacing', 'Spacing', ''),
    ],
    'default': 'show',
    'values': {
        'show': 0, 'bg_color': 1, 'opacity': 2, 'pos': 3, 'size': 4, 'angle': 5,
        'width': 6, 'height': 7, 'border_width': 8, 'border_color': 9,
        'hover_color': 10, 'text': 11, 'font': 12, 'font_color': 13,
        'font_size': 14, 'line_height': 15, 'text_halign': 16,
        'text_valign': 17, 'wrap': 18, 'shadow': 19, 'shadow_offset': 20,
        'shadow_color': 21, 'texture': 22, 'orientation': 23, 'spacing': 24,
    },
}

_DONE = [('Done', 'Condition')]
_TRIGGER = [('Condition', 'Condition')]

# (runtime_id, label, kind, inputs, outputs, config)
NATIVE_B4 = (
    ('CreateUICanvas', 'Create Canvas', 'ACT', _TRIGGER,
     [('Done', 'Condition'), ('Canvas', 'Int')],
     {'scale_mode': ('enum', 'Scale Mode', _SCALE_MODE),
      'design_w': ('int', 'Design Width', 1920),
      'design_h': ('int', 'Design Height', 1080)}),

    ('CreateUILayout', 'Create Layout', 'ACT',
     [('Condition', 'Condition'), ('Parent', 'Int'), ('Relative Position', 'Bool'),
      ('', 'Vector'), ('Relative Size', 'Bool'), ('', 'Vector'), ('Angle', 'Float'),
      ('Color', 'Color'), ('Border Width', 'Int'), ('Border Color', 'Color'),
      ('Inverted', 'Bool'), ('Spacing', 'Int'), ('Rows', 'Int'), ('Columns', 'Int'),
      ('Starting Angle', 'Float'), ('Radius', 'Float')],
     [('Done', 'Condition'), ('Layout', 'Int')],
     {'layout_type': ('enum', 'Layout Type', _LAYOUT_TYPE),
      'halign': ('enum', 'X', _ALIGN_X),
      'valign': ('enum', 'Y', _ALIGN_Y)}),

    ('CreateUILabel', 'Create Label', 'ACT',
     [('Condition', 'Condition'), ('Parent', 'Int'), ('Relative Position', 'Bool'),
      ('', 'Vector'), ('Angle', 'Float'), ('Text', 'String'), ('', 'String'),
      ('Font Size', 'Int'), ('Line Height', 'Float'), ('Font Color', 'Color'),
      ('Use Shadow', 'Bool'), ('Shadow Offset', 'Vector'), ('Shadow Color', 'Color')],
     [('Done', 'Condition'), ('Label', 'Int')],
     {'halign': ('enum', 'X', _ALIGN_X),
      'valign': ('enum', 'Y', _ALIGN_Y),
      'text_halign': ('enum', 'Text X', _ALIGN_X),
      'text_valign': ('enum', 'Text Y', _ALIGN_Y),
      'wrap': ('bool', 'Word Wrap', False),
      'font': ('string', 'Font', '')}),

    ('CreateUIImage', 'Create Image', 'ACT',
     [('Condition', 'Condition'), ('Parent', 'Int'), ('Relative Position', 'Bool'),
      ('', 'Vector'), ('Relative Size', 'Bool'), ('', 'Vector'), ('Angle', 'Float'),
      ('', 'String')],
     [('Done', 'Condition'), ('Image', 'Int')],
     {'tint_r': ('float', 'Tint R', 1.0),
      'tint_g': ('float', 'Tint G', 1.0),
      'tint_b': ('float', 'Tint B', 1.0),
      'tint_a': ('float', 'Tint A', 1.0),
      'preserve_aspect': ('bool', 'Preserve Aspect', False),
      'texture': ('string', 'Image', '')}),

    ('CreateUIButton', 'Create Button', 'ACT',
     [('Condition', 'Condition'), ('Parent', 'Int'), ('Relative Position', 'Bool'),
      ('', 'Vector'), ('Relative Size', 'Bool'), ('', 'Vector'), ('Angle', 'Float'),
      ('Color', 'Color'), ('Hover Color', 'Color'), ('Border Width', 'Int'),
      ('Border Color', 'Color'), ('Text', 'String'), ('Text Position', 'Vector'),
      ('', 'String'), ('Font Size', 'Int'), ('Line Height', 'Float'),
      ('Font Color', 'Color')],
     [('Done', 'Condition'), ('Button', 'Int'), ('On Click', 'Condition'),
      ('On Hover', 'Condition'), ('On Release', 'Condition')],
     {'halign': ('enum', 'X', _ALIGN_X),
      'valign': ('enum', 'Y', _ALIGN_Y),
      'text_halign': ('enum', 'Text X', _ALIGN_X),
      'text_valign': ('enum', 'Text Y', _ALIGN_Y),
      'font': ('string', 'Font', '')}),

    ('CreateUISliderWidget', 'Create Slider', 'ACT',
     [('Condition', 'Condition'), ('Parent', 'Int'), ('Relative Position', 'Bool'),
      ('', 'Vector'), ('Relative Size', 'Bool'), ('', 'Vector'), ('Angle', 'Float'),
      ('Bar Width', 'Float'), ('Bar Color', 'Color'), ('Bar Hover Color', 'Color'),
      ('Bar Texture', 'String'), ('Border Width', 'Int'), ('Border Color', 'Color'),
      ('Knob Size', 'Float'), ('Knob Color', 'Color'), ('Knob Hover Color', 'Color'),
      ('Knob Texture', 'String'), ('Initial Value', 'Float'), ('Steps', 'Int'),
      ('Bar Control', 'Bool')],
     [('Done', 'Condition'), ('Slider', 'Int'), ('Slider Value', 'Float'),
      ('Knob Position', 'Vector')],
     {'orientation': ('enum', 'Orientation', _ORIENTATION)}),

    ('MoveWidget', 'Move Widget', 'ACT',
     [('Condition', 'Condition'), ('Widget', 'Int')],
     _DONE,
     {'mode': ('enum', 'Mode', _MOVE_MODE)}),

    ('SetUIWidgetAttr', 'Set Widget Attribute', 'ACT',
     [('Condition', 'Condition'), ('Widget', 'Int'), ('Value', 'Float')],
     _DONE,
     {'widget_attr': ('enum', 'Attribute', _WIDGET_ATTR)}),

    ('GetUIWidgetAttr', 'Get Widget Attribute', 'PAR',
     [('Widget', 'Int')],
     [('Visibility', 'Bool'), ('Color', 'Color'), ('Alpha', 'Float'),
      ('Vec2', 'Vector'), ('String', 'String'), ('Int', 'Int'), ('Float', 'Float'),
      ('Font', 'String'), ('Image', 'String')],
     {'widget_attr': ('enum', 'Attribute', _WIDGET_ATTR)}),
)

for _entry in NATIVE_B4:
    _make_node(*_entry)
