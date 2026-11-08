"""B6.3 native node table: LAN network, extended sensors and advanced scenes.

The rows mirror the `KX_FunctionSpec` tables in `KX_FunctionNodesB6Net.cpp`,
`KX_FunctionNodesB6Sensor.cpp` and `KX_FunctionNodesB6Scene.cpp`. Enum configs
use `values` so the exported configuration is the integer the C++ side reads
through `ConfigInt`.

`SyncProperty` and `GetRemoteProperty` keep the `Value` port polymorphic: the
C++ specs are flagged `ports_from_ir` and the classes below are flagged
`native_dynamic_ports`, so the IR exporter builds the port list from the
sockets and resolves the value type from the incoming link (defaulting to
Float). The static port contract therefore skips those two runtime ids.

The `Collision` row only documents the extended port contract for an already
registered runtime node; it is skipped during registration because
`native_batch.py` owns that class.
"""
# pylint: disable=invalid-name

from .native_batch import _DONE
from .native_batch import _make_node
from .node import _nodes

_NETWORK_EVENT = {
    'items': [
        ('any', 'Any', 'Every event type'),
        ('join', 'Player Join', 'A peer joined the room'),
        ('leave', 'Player Leave', 'A peer left the room'),
        ('message', 'Message', 'A message packet arrived'),
        ('disconnect', 'Disconnect', 'The room connection dropped'),
    ],
    'default': 'any',
    'values': {'any': 0, 'join': 1, 'leave': 2, 'message': 3, 'disconnect': 4},
}

_B6C_LOCAL = ('bool', 'Reliable', True)
_B6C_VERBOSE = ('bool', 'Verbose', False)
_B6C_RADAR = {
    'axis_x': ('float', 'Axis X', 1.0),
    'axis_y': ('float', 'Axis Y', 0.0),
    'axis_z': ('float', 'Axis Z', 0.0),
    'radius': ('float', 'Radius', 10.0),
    'angle': ('float', 'Angle', 0.0),
    'property': ('string', 'Property', ''),
}

_TRIGGER_LIST = [('OnEnter', 'Condition')]

# Same enum semantics as MessageReceived: the C++ side reads ConfigInt('match').
_B6C_RPC_MATCH = {
    'items': [
        ('exact', 'Exact', 'Subject must match exactly'),
        ('prefix', 'Prefix', 'Subject must start with the filter'),
    ],
    'default': 'exact',
    'values': {'exact': 0, 'prefix': 1},
}


NATIVE_B6C = (
    # ---------------------------------------------------------------- network
    ('HostRoom', 'Host Room', 'ACT',
     [('OnEnter', 'Condition'), ('Port', 'Int'), ('MaxPlayers', 'Int')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'room_name': ('string', 'Room Name', 'room')}),
    ('JoinRoom', 'Join Room', 'ACT',
     [('OnEnter', 'Condition'), ('Host', 'String'), ('Port', 'Int')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('LocalPlayerId', 'Int')], {}),
    ('LeaveRoom', 'Leave Room', 'ACT', _TRIGGER_LIST, _DONE, {}),
    ('OnNetworkEvent', 'On Network Event', 'CON',
     [],
     [('OnPlayerJoin', 'Condition'), ('OnPlayerLeave', 'Condition'),
      ('OnMessage', 'Condition'), ('OnDisconnect', 'Condition'),
      ('PlayerId', 'Int'), ('Data', 'String')],
     {'event_type': ('enum', 'Event Type', _NETWORK_EVENT)}),
    ('SendNetworkMessage', 'Send Network Message', 'ACT',
     [('OnEnter', 'Condition'), ('TargetId', 'Int'), ('Data', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'reliable': _B6C_LOCAL}),
    ('BroadcastMessage', 'Broadcast Message', 'ACT',
     [('OnEnter', 'Condition'), ('Data', 'String')],
     [('OnSuccess', 'Condition')], {}),
    ('SyncProperty', 'Sync Property', 'ACT',
     [('OnEnter', 'Condition'), ('Key', 'String'), ('Value', 'Value')],
     _DONE, {}),
    ('GetRemoteProperty', 'Get Remote Property', 'ACT',
     [('OnEnter', 'Condition'), ('PlayerId', 'Int'), ('Key', 'String')],
     [('Value', 'Value'), ('OnSuccess', 'Condition'), ('OnFail', 'Condition')], {}),
    ('ListRooms', 'List Rooms', 'ACT',
     [('OnEnter', 'Condition'), ('Host', 'String'), ('Port', 'Int')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Rooms', 'List'),
      ('Count', 'Int'), ('Error', 'String')], {}),
    ('RefreshRooms', 'Refresh Rooms', 'ACT',
     [('OnEnter', 'Condition'), ('Host', 'String'), ('Port', 'Int')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Rooms', 'List'),
      ('Count', 'Int'), ('Error', 'String')], {}),
    ('Reconnect', 'Reconnect', 'ACT',
     [('OnEnter', 'Condition')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Attempts', 'Int'),
      ('Status', 'String')],
     {'max_attempts': ('int', 'Max Attempts', 3), 'backoff_ms': ('int', 'Backoff (ms)', 250)}),

    # ------------------------------------------------------- B6.9-N network
    # Broadcast discovery has no mDNS/handshake; a completed window is success
    # even when Count is 0. SyncPlayerState is fixed at 20 Hz; GetPlayerState
    # reads the last packet (no interpolation).
    ('DiscoverRooms', 'Discover Rooms', 'ACT',
     [('OnEnter', 'Condition'), ('Port', 'Int'), ('TimeoutMs', 'Int')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Rooms', 'List'),
      ('Count', 'Int'), ('Error', 'String')], {}),
    ('SyncPlayerState', 'Sync Player State', 'ACT',
     [('OnEnter', 'Condition'), ('Position', 'Vector'), ('Rotation', 'Vector'),
      ('Animation', 'Int'), ('AnimationPhase', 'Float')],
     _DONE, {'sync_animation': ('bool', 'Sync Animation', True)}),
    ('GetPlayerState', 'Get Player State', 'ACT',
     [('OnEnter', 'Condition'), ('PlayerId', 'Int')],
     [('Position', 'Vector'), ('Rotation', 'Vector'), ('Animation', 'Int'),
      ('AnimationPhase', 'Float'), ('OnSuccess', 'Condition'), ('OnFail', 'Condition')], {}),

    # ------------------------------------------------------- B6.9-Ms messages
    # RPC rows mirror `kB6MsgSpecs`. Local loopback always runs (single player
    # included); networking only adds a broadcast. `match` is the same
    # ConfigInt enum as MessageReceived: 0 exact, 1 prefix.
    ('SendRPC', 'Send RPC', 'ACT',
     [('OnEnter', 'Condition'), ('Subject', 'String'), ('Payload', 'String'),
      ('Target', 'Object')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {},
     'Delivers "rpc:"+Subject locally (broadcast, or to Target) and broadcasts an '
     'EBCRPC1 envelope when connected. Local delivery happens even without a network adapter.'),

    ('OnRPC', 'On RPC', 'CON',
     [('Object', 'Object'), ('Subject', 'String')],
     [('OnRPC', 'Condition'), ('Subject', 'String'), ('Payload', 'String'),
      ('Sender', 'String')],
     {'subject': ('string', 'Subject Filter', ''),
      'match': ('enum', 'Match', _B6C_RPC_MATCH)},
     'One trigger per received RPC, in arrival order. Subject input overrides the '
     'filter config; an empty filter matches every RPC subject.'),

    ('SendDelayedMessage', 'Send Delayed Message', 'ACT',
     [('OnEnter', 'Condition'), ('Subject', 'String'), ('Payload', 'String'),
      ('Delay', 'Float')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {},
     'Schedules a scene message Delay seconds ahead. Countdown advances with logic frame '
     'deltas (not wall clock); pending messages are dropped on scene unload.'),

    # ---------------------------------------------------------------- sensors
    ('RadarExtended', 'Radar Extended', 'ACT',
     [('OnEnter', 'Condition')],
     [('OnFound', 'Condition'), ('FoundObjects', 'List'), ('Closest', 'Object'),
      ('Distance', 'Float')],
     _B6C_RADAR),
    ('LineOfSight', 'Line Of Sight', 'ACT',
     [('OnEnter', 'Condition'), ('Target', 'Object'), ('IgnoreMask', 'Int')],
     [('OnVisible', 'Condition'), ('OnHidden', 'Condition'), ('Distance', 'Float')], {}),
    ('AreaSensor', 'Area Sensor', 'ACT',
     [('OnEnter', 'Condition'), ('AreaName', 'String')],
     [('OnEnterArea', 'Condition'), ('OnLeaveArea', 'Condition'), ('Occupants', 'List')], {}),
    ('RaycastSensor', 'Raycast Sensor', 'ACT',
     [('OnEnter', 'Condition'), ('Direction', 'Vector'), ('Length', 'Float')],
     [('OnHit', 'Condition'), ('OnMiss', 'Condition'), ('HitObject', 'Object'),
      ('HitPoint', 'Vector')], {}),
    # B6.9: every hit, nearest first (adapter contract, not a hint).
    ('RaycastAll', 'Raycast All', 'ACT',
     [('OnEnter', 'Condition'), ('Direction', 'Vector'), ('Length', 'Float')],
     [('OnHit', 'Condition'), ('OnMiss', 'Condition'), ('Objects', 'List'),
      ('Distances', 'List'), ('Count', 'Int')], {}),
    ('Collision', 'Collision', 'CON',
     [('Object', 'Object')],
     [('OnEnter', 'Condition'), ('OnStay', 'Condition'), ('OnLeave', 'Condition'),
      ('ContactPoint', 'Vector'), ('ContactNormal', 'Vector'),
      ('ImpactForce', 'Float')], {}),
    ('ProximitySensor', 'Proximity Sensor', 'CON',
     [('Object', 'Object'), ('Distance', 'Float')],
     [('OnNear', 'Condition'), ('OnFar', 'Condition'), ('DistanceValue', 'Float')], {}),

    # ----------------------------------------------------------- scene control
    ('LoadSceneAsync', 'Load Scene Async', 'ACT',
     [('OnEnter', 'Condition'), ('SceneName', 'String')],
     [('OnProgress', 'Float'), ('OnLoaded', 'Condition'), ('OnFail', 'Condition')],
     {'verbose': _B6C_VERBOSE}),
    ('UnloadScene', 'Unload Scene', 'ACT',
     [('OnEnter', 'Condition'), ('SceneName', 'String')],
     [('OnDone', 'Condition'), ('OnFail', 'Condition')], {}),
    # B6.9: single-active-scene push/pop. The parent goes inactive, the child
    # scene is not freed. Freeing stays UnloadScene's Unsupported path.
    ('MountChildScene', 'Mount Child Scene', 'ACT',
     [('OnEnter', 'Condition'), ('ParentScene', 'String'), ('ChildScene', 'String')],
     [('OnDone', 'Condition'), ('OnFail', 'Condition'), ('Reason', 'String')], {}),
    ('UnmountChildScene', 'Unmount Child Scene', 'ACT',
     [('OnEnter', 'Condition'), ('ParentScene', 'String'), ('ChildScene', 'String')],
     [('OnDone', 'Condition'), ('OnFail', 'Condition'), ('Reason', 'String')], {}),
    ('TransitionScene', 'Transition Scene', 'ACT',
     [('OnEnter', 'Condition'), ('FromScene', 'String'), ('ToScene', 'String'),
      ('Duration', 'Float')], _DONE, {}),
    ('GetActiveScenes', 'Get Active Scenes', 'PAR',
     [], [('Scenes', 'List'), ('Count', 'Int')], {}),
    ('SetSceneTimeScale', 'Set Scene Time Scale', 'ACT',
     [('OnEnter', 'Condition'), ('SceneName', 'String'), ('TimeScale', 'Float')],
     _DONE, {}),

    # ---------------------------------------------------- legacy screen queries
    # WorldPosition/Touch are screen space queries, not physics contacts:
    # WorldPosition inverse-projects a pixel at an arbitrary depth, Touch starts
    # from one ray hit and then queries nearby objects. The Collision row above
    # is untouched by both.
    ('WorldPositionLegacy', 'World Position Legacy', 'ACT',
     [('OnEnter', 'Condition'), ('ScreenX', 'Float'), ('ScreenY', 'Float'),
      ('Depth', 'Float')],
     [('OnDone', 'Condition'), ('WorldPos', 'Vector')], {}),
    ('TouchLegacy', 'Touch Legacy', 'ACT',
     [('OnEnter', 'Condition'), ('ScreenX', 'Float'), ('ScreenY', 'Float')],
     [('OnDone', 'Condition'), ('Target', 'Object'), ('Objects', 'List'),
      ('Point', 'Vector'), ('Normal', 'Vector')],
     {'overlap_threshold': ('float', 'Overlap Threshold', 0.1)}),
)


def _resolve_polymorphic(self, port_name, is_input, ctx):
    """Keep the connected value type for the network Value ports."""
    if port_name in ('Value', 'VALUE'):
        if is_input:
            return self.native_linked_type('Value', ctx) or 'Float'
        return 'Float'
    tag = self.native_port_types.get(port_name)
    if tag is not None:
        return tag
    # Dynamic ports use the socket identifier, while the table keys use the
    # port label, so fall back to a case insensitive lookup.
    for name, value in self.native_port_types.items():
        if name.upper() == str(port_name).upper():
            return value
    return None


_existing_classes = {getattr(cls, 'bl_idname', '') for cls in _nodes}

for _spec in NATIVE_B6C:
    runtime_id = _spec[0]
    if 'GameNode' + runtime_id in _existing_classes:
        # Collision is declared in native_batch.py; the row above mirrors its
        # extended port contract and is not registered twice.
        continue
    _cls = _make_node(*_spec)
    if runtime_id in ('SyncProperty', 'GetRemoteProperty'):
        # The C++ specs use `ports_from_ir`, so the editor builds the IR port
        # list dynamically and may change the Value socket type.
        _cls.native_dynamic_ports = True
        _cls.resolve_native_port = _resolve_polymorphic
