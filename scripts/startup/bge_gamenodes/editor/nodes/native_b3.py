"""Table driven editor nodes for the B3 animation runtime family.

The port order here is the C++ port order contract of `KX_FunctionNodesB3.cpp`.
`PlayAction`, `StopAction` and `SetActionFrame` are declared in
`native_batch.py`; this table adds the remaining animation nodes.
"""

from .native_batch import _DONE
from .native_batch import _make_node

# (runtime_id, label, kind, inputs, outputs, config)
NATIVE_B3 = (
    ('PlayActionByName', 'Play Action By Name', 'ACT',
     [('OnEnter', 'Condition'), ('ObjectName', 'String'), ('Action', 'String'),
      ('Start', 'Float'), ('End', 'Float'), ('Layer', 'Int'), ('Loop', 'Bool')],
     [('Started', 'Condition'), ('Frame', 'Float')],
     {'action': ('string', 'Action', '')}),
    ('SetActionSpeed', 'Set Action Speed', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Layer', 'Int'),
      ('Speed', 'Float')], _DONE, {}),
    ('SetLayerWeight', 'Set Layer Weight', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Layer', 'Int'),
      ('Weight', 'Float')], _DONE, {}),
    ('GetActionFrame', 'Get Action Frame', 'PAR',
     [('Object', 'Object'), ('Layer', 'Int')], [('Frame', 'Float')], {}),
    ('ActionDone', 'Action Done', 'CON',
     [('Object', 'Object'), ('Layer', 'Int')], [('OnDone', 'Condition')], {}),

    # B3.2 message bus (existing Send Message node is reused; only the receiver is new)
    ('MessageReceived', 'Message Received', 'CON',
     [('Object', 'Object'), ('Subject', 'String')],
     [('OnMessage', 'Condition'), ('Body', 'String'), ('Sender', 'String')],
     {'subject': ('string', 'Subject', ''),
      'match': ('enum', 'Subject Match', {
          'items': [('exact', 'Exact', 'Subject must match exactly'),
                    ('prefix', 'Prefix', 'Subject starts with the configured text')],
          'default': 'exact',
          'values': {'exact': 0, 'prefix': 1},
      })}),

    # B3.2 audio
    ('StartSound', 'Start Sound', 'ACT',
     [('OnEnter', 'Condition'), ('File', 'String'), ('Loop', 'Bool'),
      ('Volume', 'Float'), ('Pitch', 'Float')],
     [('OnStarted', 'Condition'), ('Handle', 'Int')],
     {'file': ('string', 'File', '')}),
    ('StopSound', 'Stop Sound', 'ACT',
     [('OnEnter', 'Condition'), ('Handle', 'Int')], _DONE, {}),
    ('SetSoundVolume', 'Set Sound Volume', 'ACT',
     [('OnEnter', 'Condition'), ('Handle', 'Int'), ('Volume', 'Float')], _DONE, {}),
    ('SetSoundPitch', 'Set Sound Pitch', 'ACT',
     [('OnEnter', 'Condition'), ('Handle', 'Int'), ('Pitch', 'Float')], _DONE, {}),
    ('SoundFinished', 'Sound Finished', 'CON',
     [('Handle', 'Int')], [('OnFinish', 'Condition')], {}),
    ('PlaySoundByName', 'Play Sound By Name', 'ACT',
     [('OnEnter', 'Condition'), ('Name', 'String'), ('Loop', 'Bool'),
      ('Volume', 'Float'), ('Pitch', 'Float')],
     [('OnDone', 'Condition'), ('Handle', 'Int'), ('Found', 'Bool')],
     {'name': ('string', 'Name', '')}),
    ('Set3DListener', 'Set 3D Listener', 'ACT',
     [('OnEnter', 'Condition'), ('Position', 'Vector'),
      ('Orientation', 'Vector')], _DONE, {}),
    ('SuspendScene', 'Suspend Scene', 'ACT',
     [('OnEnter', 'Condition')],
     [('OnDone', 'Condition'), ('Suspended', 'Bool')], {}),
    ('ResumeScene', 'Resume Scene', 'ACT',
     [('OnEnter', 'Condition')],
     [('OnDone', 'Condition'), ('Suspended', 'Bool')], {}),
    ('AddObject', 'Add Object', 'ACT',
     [('OnEnter', 'Condition'), ('Name', 'String')],
     [('OnDone', 'Condition'), ('Spawned', 'Bool'), ('Object', 'Object')],
     {'name': ('string', 'Name', '')}),
    ('Touch', 'Touch', 'ACT',
     [('OnEnter', 'Condition'), ('Camera', 'Object'), ('Button', 'Int')],
     [('OnHit', 'Condition'), ('Object', 'Object'), ('Point', 'Vector'),
      ('Normal', 'Vector'), ('Distance', 'Float'), ('Hit', 'Bool'),
      ('IsOwner', 'Bool')], {}),

    # B3.3 proximity sensor; Angle 0 means a plain sphere, otherwise a cone
    ('Radar', 'Radar', 'CON',
     [('Object', 'Object'), ('Axis', 'Vector'), ('Radius', 'Float'),
      ('Angle', 'Float'), ('Property', 'String')],
     [('OnDetect', 'Condition'), ('Target', 'Object'), ('Distance', 'Float'),
      ('Count', 'Int')], {}),
)

for _spec in NATIVE_B3:
    _make_node(*_spec)
