"""Native game node family.

These nodes mirror the C++ `KX_NodeSignature` port contract exactly and are
exported to the native runtime. The legacy Python nodes stay untouched: their
poll based semantic cannot be mapped onto the edge triggered runtime without
changing existing projects.
"""

from bpy.props import EnumProperty
from bpy.props import StringProperty

from .node import GameNodeActionType
from .node import GameNodeConditionType
from .node import GameNodeParameterType
from .node import node_type
from ..sockets import NodeSocketLogicBoolean
from ..sockets import NodeSocketLogicCondition
from ..sockets import NodeSocketLogicFloat
from ..sockets import NodeSocketLogicValue


@node_type
class GameNodeOnInit(GameNodeConditionType):
    bl_idname = 'GameNodeOnInit'
    bl_label = 'On Init (Native)'
    bl_description = 'Trigger once on the first frame'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'OnInit'
    native_only = True
    native_outputs = (('OnOut', 'ON_OUT'),)

    def init(self, context):
        self.add_output(NodeSocketLogicCondition, 'OnOut', 'ON_OUT')
        GameNodeConditionType.init(self, context)


@node_type
class GameNodeOnUpdate(GameNodeConditionType):
    bl_idname = 'GameNodeOnUpdate'
    bl_label = 'On Update (Native)'
    bl_description = 'Trigger every frame'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'OnUpdate'
    native_only = True
    native_outputs = (('OnOut', 'ON_OUT'),)

    def init(self, context):
        self.add_output(NodeSocketLogicCondition, 'OnOut', 'ON_OUT')
        GameNodeConditionType.init(self, context)


@node_type
class GameNodeBranch(GameNodeConditionType):
    bl_idname = 'GameNodeBranch'
    bl_label = 'Branch (Native)'
    bl_description = 'Route a trigger through True or False'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'Branch'
    native_only = True
    native_inputs = (
        ('OnEnter', 'ON_ENTER'),
        ('Condition', 'CONDITION'),
    )
    native_outputs = (
        ('OnTrue', 'ON_TRUE'),
        ('OnFalse', 'ON_FALSE'),
    )

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
        self.add_input(NodeSocketLogicBoolean, 'Condition', 'CONDITION')
        self.add_output(NodeSocketLogicCondition, 'OnTrue', 'ON_TRUE')
        self.add_output(NodeSocketLogicCondition, 'OnFalse', 'ON_FALSE')
        GameNodeConditionType.init(self, context)


@node_type
class GameNodeDelay(GameNodeConditionType):
    bl_idname = 'GameNodeDelay'
    bl_label = 'Delay (Native)'
    bl_description = 'Relay a trigger after a delay'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'Delay'
    native_only = True
    native_inputs = (
        ('OnEnter', 'ON_ENTER'),
        ('Duration', 'DURATION'),
    )
    native_outputs = (('OnDone', 'ON_DONE'),)

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
        self.add_input(NodeSocketLogicFloat, 'Duration', 'DURATION')
        self.add_output(NodeSocketLogicCondition, 'OnDone', 'ON_DONE')
        GameNodeConditionType.init(self, context)


@node_type
class GameNodeSink(GameNodeActionType):
    bl_idname = 'GameNodeSink'
    bl_label = 'Sink (Native)'
    bl_description = 'Consume a trigger and record the value for debugging'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'Sink'
    native_only = True
    native_inputs = (
        ('OnEnter', 'ON_ENTER'),
        ('Value', 'VALUE'),
    )

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
        self.add_input(NodeSocketLogicFloat, 'Value', 'VALUE')
        GameNodeActionType.init(self, context)


@node_type
class GameNodeAssetGet(GameNodeParameterType):
    bl_idname = 'GameNodeAssetGet'
    bl_label = 'Asset Get (Native)'
    bl_description = 'Read a field of a game data asset'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'AssetGet'
    native_only = True
    native_inputs = (('OnEnter', 'ON_ENTER'),)
    native_outputs = (
        ('Value', 'VALUE'),
        ('OnRead', 'ON_READ'),
    )

    asset_ref: StringProperty(name='Asset', description='Asset id (GameAsset_ text)')
    field: StringProperty(name='Field', description='Field name inside the asset')
    mode: EnumProperty(
        name='Mode',
        items=[
            ('poll', 'Poll', 'Read the field every frame'),
            ('trigger', 'Trigger', 'Read the field when On Enter fires'),
        ],
        default='poll',
    )

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
        self.add_output(NodeSocketLogicValue, 'Value', 'VALUE')
        self.add_output(NodeSocketLogicCondition, 'OnRead', 'ON_READ')
        GameNodeParameterType.init(self, context)

    def draw_buttons(self, context, layout):
        layout.prop(self, 'asset_ref', text='')
        layout.prop(self, 'field', text='')
        layout.prop(self, 'mode', text='')

    def export_config(self):
        return {
            'asset_ref': self.asset_ref,
            'field': self.field,
            'mode': self.mode,
        }

    def resolve_native_port(self, port_name, is_input, ctx):
        if not is_input and port_name == 'Value':
            return ctx.asset_field_type(self.asset_ref, self.field)
        return None


@node_type
class GameNodeAssetSet(GameNodeActionType):
    bl_idname = 'GameNodeAssetSet'
    bl_label = 'Asset Set (Native)'
    bl_description = 'Write a field of a game data asset'
    nl_module = ''
    nl_class = ''
    runtime_type_id = 'AssetSet'
    native_only = True
    native_inputs = (
        ('OnEnter', 'ON_ENTER'),
        ('Value', 'VALUE'),
    )
    native_outputs = (('OnWritten', 'ON_WRITTEN'),)

    asset_ref: StringProperty(name='Asset', description='Asset id (GameAsset_ text)')
    field: StringProperty(name='Field', description='Field name inside the asset')

    def init(self, context):
        self.add_input(NodeSocketLogicCondition, 'OnEnter', 'ON_ENTER')
        self.add_input(NodeSocketLogicValue, 'Value', 'VALUE')
        self.add_output(NodeSocketLogicCondition, 'OnWritten', 'ON_WRITTEN')
        GameNodeActionType.init(self, context)

    def draw_buttons(self, context, layout):
        layout.prop(self, 'asset_ref', text='')
        layout.prop(self, 'field', text='')

    def export_config(self):
        return {
            'asset_ref': self.asset_ref,
            'field': self.field,
        }

    def resolve_native_port(self, port_name, is_input, ctx):
        if is_input and port_name == 'Value':
            return ctx.asset_field_type(self.asset_ref, self.field)
        return None
