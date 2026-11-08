# SPDX-FileCopyrightText: 2026 EBC
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""B6.1 advanced physics and animation nodes (table driven)."""

# pylint: disable=invalid-name

from .native_batch import _DONE
from .native_batch import _make_node

_CONSTRAINT_TYPE = {
    'items': [
        ('point', 'Point', 'Point to point constraint'),
        ('hinge', 'Hinge', 'Line hinge constraint'),
        ('slider', 'Slider', 'Generic 6DOF locked to one linear axis'),
        ('cone', 'Cone', 'Cone twist constraint'),
        ('generic', 'Generic', 'Generic 6DOF constraint'),
    ],
    'default': 'point',
    'values': {'point': 0, 'hinge': 1, 'slider': 2, 'cone': 3, 'generic': 4},
}

NATIVE_B6 = (
    # ------------------------------------------------------------------ physics
    ('CreateVehicle', 'Create Vehicle', 'ACT',
     [('OnEnter', 'Condition'), ('Chassis', 'Object'), ('Template', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Vehicle', 'Int')],
     {'wheel_names': ('string', 'Wheel Names', ''),
      'suspension': ('string', 'Suspension', '')}),
    ('VehicleControl', 'Vehicle Control', 'ACT',
     [('OnEnter', 'Condition'), ('Vehicle', 'Int'), ('Steering', 'Float'),
      ('Throttle', 'Float'), ('Brake', 'Float')],
     _DONE, {}),
    ('AddConstraint', 'Add Constraint', 'ACT',
     [('OnEnter', 'Condition'), ('ObjectA', 'Object'), ('ObjectB', 'Object')],
     _DONE, {'type': ('enum', 'Constraint Type', _CONSTRAINT_TYPE),
             'params': ('string', 'Params', '0,0,0,0,0,0'),
             'pivot': ('string', 'Pivot', ''),
             'axis': ('string', 'Axis', '')}),
    ('RemoveConstraint', 'Remove Constraint', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object')],
     _DONE, {}),
    ('AddJoint', 'Add Joint', 'ACT',
     [('OnEnter', 'Condition'), ('ObjectA', 'Object'), ('ObjectB', 'Object'),
      ('Pivot', 'Vector'), ('Axis', 'Vector')],
     _DONE, {}),
    ('SetPhysicsMass', 'Set Physics Mass', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Mass', 'Float')],
     _DONE, {}),
    ('SetPhysicsFriction', 'Set Physics Friction', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Friction', 'Float')],
     _DONE, {}),
    ('SetPhysicsRestitution', 'Set Physics Restitution', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Restitution', 'Float')],
     _DONE, {}),
    ('SetLinearDamping', 'Set Linear Damping', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Damping', 'Float')],
     _DONE, {}),
    ('SetAngularDamping', 'Set Angular Damping', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Damping', 'Float')],
     _DONE, {}),
    ('BuildNavMesh', 'Build Nav Mesh', 'ACT',
     [('OnEnter', 'Condition')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'agent_radius': ('float', 'Agent Radius', 0.3),
      'agent_height': ('float', 'Agent Height', 1.8)}),
    ('QueryPath', 'Query Path', 'ACT',
     [('OnEnter', 'Condition'), ('From', 'Vector'), ('To', 'Vector')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'),
      ('Waypoints', 'List')],
     {}),
    ('RaycastBatch', 'Raycast Batch', 'ACT',
     [('OnEnter', 'Condition'), ('Origins', 'List'), ('Dirs', 'List'),
      ('Distance', 'Float')],
     [('Hits', 'List'), ('Points', 'List')],
     {}),
    ('SetCollisionGroup', 'Set Collision Group', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Group', 'Int'),
      ('Mask', 'Int')],
     _DONE, {}),

    # ---------------------------------------------------------------- animation
    ('PlayActionByLayer', 'Play Action By Layer', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Action', 'String'),
      ('Layer', 'Int'), ('BlendIn', 'Float')],
     _DONE, {}),
    ('BlendActions', 'Blend Actions', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('ActionA', 'String'),
      ('ActionB', 'String'), ('Factor', 'Float')],
     _DONE, {}),
    ('SetBoneTransform', 'Set Bone Transform', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Bone', 'String'),
      ('Position', 'Vector'), ('Rotation', 'Vector')],
     _DONE,
     # Keep Override as the default so existing graphs behave exactly as before. Additive
     # combines the delta quaternion with the action-evaluated pose; Multiplicative is reserved
     # for P2 and rejected by the adapter instead of silently behaving like Override.
     {'mode': ('enum', 'Blend Mode',
               {'items': [('override', 'Override', 'Replace the action pose'),
                          ('additive', 'Additive', 'Add the quaternion delta'),
                          ('multiplicative', 'Multiplicative', 'Reserved (not implemented)')],
                'default': 'override',
                'values': {'override': 0, 'additive': 1, 'multiplicative': 2}})}),
    ('SetBoneWeight', 'Set Bone Weight', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Bone', 'String'),
      ('Weight', 'Float')],
     _DONE, {}),
    ('PlayBoneAction', 'Play Bone Action', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Bone', 'String'),
      ('Action', 'String')],
     _DONE, {}),
    ('SetAnimationSpeed', 'Set Animation Speed', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Layer', 'Int'),
      ('Speed', 'Float')],
     _DONE, {}),
    ('AddAnimationEvent', 'Add Animation Event', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Action', 'String'),
      ('Frame', 'Int')],
     [('OnEvent', 'Condition')],
     {'event_name': ('string', 'Event Name', '')}),
    ('GetAnimationFrame', 'Get Animation Frame', 'PAR',
     [('Object', 'Object'), ('Layer', 'Int')],
     [('Frame', 'Float'), ('Normalized', 'Float')],
     {}),
    ('ClearBoneOverride', 'Clear Bone Override', 'ACT',
     [('OnEnter', 'Condition'), ('Object', 'Object'), ('Bone', 'String')],
     _DONE, {}),
)


def _register_table():
    for runtime_id, label, kind, inputs, outputs, config in NATIVE_B6:
        _make_node(runtime_id, label, kind, inputs, outputs, config)


_register_table()
