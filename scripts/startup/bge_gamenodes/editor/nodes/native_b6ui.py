"""B6.4 advanced UI nodes (table driven).

Mirrors `kB6UISpecs` in `KX_FunctionNodesB6UI.cpp`: same port order, kinds and
configuration keys. See docs/B6_UI_ADVANCED.md.
"""

from .native_batch import _make_node

_DIRECTION = {
    'items': [
        ('vertical', 'Vertical', 'Scroll along Y'),
        ('horizontal', 'Horizontal', 'Scroll along X'),
        ('both', 'Both', 'Scroll along both axes'),
    ],
    'default': 'vertical',
}

_EASING = {
    'items': [
        ('linear', 'Linear', 'Constant speed'),
        ('ease_in', 'Ease In', 'Slow start (t^2)'),
        ('ease_out', 'Ease Out', 'Slow end (1-(1-t)^2)'),
        ('ease_in_out', 'Ease In Out', 'Slow start and end'),
    ],
    'default': 'linear',
}

_ANCHOR = {
    'items': [
        ('top_left', 'Top Left', ''),
        ('top_center', 'Top Center', ''),
        ('top_right', 'Top Right', ''),
        ('middle_left', 'Middle Left', ''),
        ('center', 'Center', ''),
        ('middle_right', 'Middle Right', ''),
        ('bottom_left', 'Bottom Left', ''),
        ('bottom_center', 'Bottom Center', ''),
        ('bottom_right', 'Bottom Right', ''),
    ],
    'default': 'center',
}

_MODE = {
    'items': [
        ('push', 'Push', 'Hide the current screen and show a new one'),
        ('pop', 'Pop', 'Hide the top screen and reveal the previous one'),
        ('replace', 'Replace', 'Replace the top screen, stack depth unchanged'),
    ],
    'default': 'push',
}

NATIVE_B6UI = (
    ('CreateScrollView', 'Create Scroll View (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Parent', 'Int'), ('Pos', 'Vector'), ('Size', 'Vector')],
     [('Done', 'Condition'), ('View', 'Int'), ('Content', 'Int')],
     {'direction': ('enum', 'Direction', _DIRECTION)},
     'Scrolling viewport; the content layout scrolls only when it overflows.'),

    ('CreateListView', 'Create List View (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Parent', 'Int'), ('Pos', 'Vector'), ('Size', 'Vector'),
      ('Items', 'List'), ('Clear', 'Condition')],
     [('Done', 'Condition'), ('ListView', 'Int'), ('OnItemClick', 'Int')],
     {'item_height': ('float', 'Item Height', 24.0),
      'virtualize': ('bool', 'Virtualize', True)},
     'List of labels; virtualization only changes how much is laid out and drawn.'),

    ('CreateTabView', 'Create Tab View (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Parent', 'Int'), ('Pos', 'Vector'), ('Size', 'Vector'),
      ('TabNames', 'List')],
     [('Done', 'Condition'), ('TabView', 'Int'), ('OnTabChanged', 'Int')],
     {},
     'Row of buttons; OnTabChanged reports the clicked tab index.'),

    ('UIAnimation', 'UI Animation (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Widget', 'Int'), ('Duration', 'Float'), ('Property', 'String'),
      ('FromValue', 'Value'), ('ToValue', 'Value')],
     [('OnDone', 'Condition')],
     {'easing': ('enum', 'Easing', _EASING)},
     'Ticks a widget attribute; re-entry cancels the previous animation.'),

    ('UIScreenAnchor', 'UI Screen Anchor (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Widget', 'Int')],
     [('OnResize', 'Condition'), ('Width', 'Float'), ('Height', 'Float')],
     {'anchor': ('enum', 'Anchor', _ANCHOR)},
     'Anchors a widget to one of the nine screen positions.'),

    ('UIScreenStack', 'UI Screen Stack (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('ScreenName', 'String')],
     [('OnDone', 'Condition')],
     {'mode': ('enum', 'Mode', _MODE)},
     'Push/pop/replace canvases; screens are hidden, never destroyed.'),
)

for _entry in NATIVE_B6UI:
    _make_node(*_entry)
