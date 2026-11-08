import bpy
from .interface import menu_item
from ..utilities import preferences
from ..props.customnode import get_custom_game_nodes


def draw_add_menu(self, context):
    if context.space_data.tree_type != "BGELogicTree":
        return
    layout = self.layout
    layout.operator_context = "INVOKE_DEFAULT"

    # layout.operator('game_nodes.node_search', text="Search", icon="VIEWZOOM")
    # layout.separator()
    if len(get_custom_game_nodes()) > 0:
        layout.menu("LN_MT_custom_menu", text="Custom Nodes", icon="RIGHTARROW_THIN")
        layout.separator()
    layout.menu("LN_MT_events_menu", text="Events", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_game_menu", text="Game", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_input_menu", text="Input", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_values_menu", text="Values", icon="RIGHTARROW_THIN")
    layout.separator()
    layout.menu("LN_MT_animation_menu", text="Animation", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_objects_menu", text="Objects", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_scene_menu", text="Scene", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_sound_menu", text="Sound", icon="RIGHTARROW_THIN")
    layout.separator()
    layout.menu("LN_MT_logic_menu", text="Logic", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_math_menu", text="Math", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_physics_menu", text="Physics", icon="RIGHTARROW_THIN")
    # layout.menu("LN_MT_raycast_menu", text="Raycasts", icon="RIGHTARROW_THIN")  # Deprecated
    layout.menu("LN_MT_time_menu", text="Time", icon="RIGHTARROW_THIN")
    layout.separator()
    layout.menu("LN_MT_data_menu", text="Data", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_file_menu", text="File", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_network_menu", text="Network", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_portal_menu", text="Portals", icon="RIGHTARROW_THIN")
    layout.separator()
    layout.menu("LN_MT_render_menu", text="Render", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_ui_menu", text="UI", icon="RIGHTARROW_THIN")
    layout.separator()
    layout.menu("LN_MT_layout_menu", text="Layout", icon="RIGHTARROW_THIN")
    layout.menu("LN_MT_utility_menu", text="Utility", icon="RIGHTARROW_THIN")


# Legacy editor nodes that have a native runtime replacement in this build.
# The classes stay registered (bl_idname is the .blend type key), only their
# add-menu rows are hidden so a feature never shows up twice in one category.
NATIVE_SUPERSEDED_LEGACY = frozenset({
    'LogicNodeCreateUIButton',
    'LogicNodeCreateUICanvas',
    'LogicNodeCreateUIImage',
    'LogicNodeCreateUILabel',
    'LogicNodeCreateUILayout',
    'LogicNodeCreateUISliderWidget',
    'LogicNodeGetUIWidgetAttr',
    'LogicNodeLogicGate',
    'LogicNodeObjectByName',
    'LogicNodeRandomValue',
    'LogicNodeSendNetworkMessage',
    'LogicNodeSetUIWidgetAttr',
    'LogicNodeStartSound',
    'NLActionAlignAxisToVector',
    'NLActionApplyForce',
    'NLActionApplyImpulse',
    'NLActionApplyTorque',
    'NLActionEndGame',
    'NLActionEndObjectNode',
    'NLActionLoadGame',
    'NLActionMouseLookNode',
    'NLActionMoveTo',
    'NLActionPrint',
    'NLActionRandomFloat',
    'NLActionRestartGame',
    'NLActionRotateTo',
    'NLActionSaveGame',
    'NLActionSetDynamicsNode',
    'NLActionSetGravity',
    'NLActionSetMousePosition',
    'NLActionSetParentNode',
    'NLActionSetResolution',
    'NLActionStopSound',
    'NLActionTimeDelay',
    'NLActionTranslate',
    'NLAddObjectActionNode',
    'NLClampValueNode',
    'NLConditionCollisionNode',
    'NLConditionLogicOperation',
    'NLConditionMouseWheelMoved',
    'NLConditionNone',
    'NLConditionNotNode',
    'NLConditionOnceNode',
    'NLConditionTimeElapsed',
    'NLGamepadButtonsCondition',
    'NLGamepadTriggerCondition',
    'NLGetGravityNode',
    'NLInterpolateValueNode',
    'NLLoadScene',
    'NLMapRangeNode',
    'NLMousePressedCondition',
    'NLOnInitConditionNode',
    'NLOnUpdateConditionNode',
    'NLOwnerGameObjectParameterNode',
    'NLParameterDistance',
    'NLParameterGameObjectParent',
    'NLParameterMathFun',
    'NLParameterWorldPosition',
    'NLSetCollisionMask',
    'NLSetMaterial',
    'NLSetRigidBody',
    'NLThresholdNode',
    'NLVectorAngle',
    'NLWithinRangeNode',
})

# Legacy rows whose label is owned by a native node in the same menu, but the
# legacy class itself is not a same-name conflict pair (the old class is the
# dead half of a label collision, e.g. LogicNodeSimpleValue -> GameNodeStringConst).
NATIVE_SUPERSEDED_LEGACY_ENTRIES = frozenset({
    ('LogicNodeMoveUIWidget', 'Move Widget'),
    ('LogicNodeSimpleValue', 'String'),
    ('NLActionSetGameObjectVisibility', 'Set Visibility'),
    ('NLKeyPressedCondition', 'Keyboard Key'),
    ('NLMouseDataParameter', 'Mouse Wheel'),
    ('NLSetCollisionGroup', 'Set Collision Mask'),
    ('NLSetCollisionGroup', 'Set Collision Group'),
})

# Identity key of the entries already drawn in the menu currently being built.
# Native rows are appended by _merge_native_menu_entries below, this keeps the
# legacy rows and the native rows from showing the same entry twice.
_MENU_SEEN = None

# Legacy classes the native runtime cannot execute.  They stay registered (the
# class and bl_idname are the .blend type key), only their add-menu rows are
# hidden: offering a node that the exporter has to skip is misleading.
# The set is derived from the classes themselves, never hand written.
_LEGACY_WITHOUT_RUNTIME = None


def _legacy_without_runtime():
    global _LEGACY_WITHOUT_RUNTIME
    if _LEGACY_WITHOUT_RUNTIME is None:
        from ..editor.nodes.node import _nodes

        hidden = set()
        for cls in _nodes:
            if getattr(cls, 'native_only', False):
                continue
            if getattr(cls, 'runtime_type_id', ''):
                continue
            bl_idname = getattr(cls, 'bl_idname', '')
            if bl_idname:
                hidden.add(bl_idname)
        _LEGACY_WITHOUT_RUNTIME = frozenset(hidden)
    return _LEGACY_WITHOUT_RUNTIME


def insertNode(layout, type, text, icon="NONE", settings={}):
    if type in NATIVE_SUPERSEDED_LEGACY or (type, text) in NATIVE_SUPERSEDED_LEGACY_ENTRIES:
        return None
    if type in _legacy_without_runtime():
        return None
    key = (type, text, tuple(sorted(settings.items()))) if settings else (type, text)
    if _MENU_SEEN is not None:
        if key in _MENU_SEEN:
            return None
        _MENU_SEEN.add(key)
    operator = layout.operator("node.add_node", text=text, icon=icon)
    operator.type = type
    operator.use_transform = True
    for name, value in settings.items():
        item = operator.settings.add()
        item.name = name
        item.value = repr(value)
    return operator


def insertPortal(layout, text, mode='in', icon="NONE", settings={}):
    operator = layout.operator(f'game_nodes.add_portal_{mode}', text=text, icon=icon)
    return operator


@menu_item
class CustomEventsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_custom_events_menu"
    bl_label = "Custom Events Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionCreateMessage", "Send Event")
        insertNode(layout, "NLParameterReceiveMessage", "Receive Event")


@menu_item
class EventsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_events_menu"
    bl_label = "Events Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_events_lifecycle_menu", text="Lifecycle", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_events_flow_menu", text="Flow", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_events_signals_menu", text="Signals", icon="RIGHTARROW_THIN")


@menu_item
class EventsLifecycleMenu(bpy.types.Menu):
    bl_idname = "LN_MT_events_lifecycle_menu"
    bl_label = "Lifecycle"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLConditionNextFrameNode", "On Next Frame")
        insertNode(layout, "NLConditionValueChanged", "On Value Changed")


@menu_item
class EventsFlowMenu(bpy.types.Menu):
    bl_idname = "LN_MT_events_flow_menu"
    bl_label = "Flow"

    def draw(self, context):
        pass


@menu_item
class EventsSignalsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_events_signals_menu"
    bl_label = "Signals"

    def draw(self, context):
        insertNode(self.layout, "NLParameterReceiveMessage", "Receive Event")


@menu_item
class GameMenu(bpy.types.Menu):
    bl_idname = "LN_MT_game_menu"
    bl_label = "Game Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionStartGame", "Load Blender File")
        insertNode(layout, "NLActionLoadGame", "Load Game")
        insertNode(layout, "NLActionEndGame", "Quit Game")
        insertNode(layout, "NLActionRestartGame", "Restart Game")
        insertNode(layout, "NLActionSaveGame", "Save Game")


@menu_item
class InputMenu(bpy.types.Menu):
    bl_idname = "LN_MT_input_menu"
    bl_label = "Input Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_mouse_menu", text="Mouse Buttons", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_mouse_cursor_menu", text="Mouse Cursor", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_keyboard_menu", text="Keyboard", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_gamepad_menu", text="Gamepad", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_touch_menu", text="Touch", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_input_map_menu", text="Input Map", icon="RIGHTARROW_THIN")


@menu_item
class TouchMenu(bpy.types.Menu):
    bl_idname = "LN_MT_touch_menu"
    bl_label = "Touch"

    def draw(self, context):
        pass


@menu_item
class InputMapMenu(bpy.types.Menu):
    bl_idname = "LN_MT_input_map_menu"
    bl_label = "Input Map"

    def draw(self, context):
        pass


@menu_item
class MouseMenu(bpy.types.Menu):
    bl_idname = "LN_MT_mouse_menu"
    bl_label = "Mouse Buttons"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLMouseMovedCondition", "Mouse Moved")


@menu_item
class MouseCursorMenu(bpy.types.Menu):
    bl_idname = "LN_MT_mouse_cursor_menu"
    bl_label = "Mouse Cursor"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionSetMouseCursorVisibility", "Cursor Visibility")
        insertNode(layout, "NLMouseDataParameter", "Cursor Position", settings={'mode': '0'})
        insertNode(layout, "NLMouseDataParameter", "Cursor Movement", settings={'mode': '1'})


@menu_item
class KeyboardMenu(bpy.types.Menu):
    bl_idname = "LN_MT_keyboard_menu"
    bl_label = "Keyboard Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLKeyPressedCondition", "Keyboard Key")
        # insertNode(layout, "NLKeyReleasedCondition", "Key Up")
        insertNode(layout, "NLKeyboardActive", "Keyboard Active")
        layout.separator()
        insertNode(layout, "NLParameterKeyboardKeyCode", "Key Code")
        insertNode(layout, "NLKeyLoggerAction", "Key Logger")


@menu_item
class GamepadMenu(bpy.types.Menu):
    bl_idname = "LN_MT_gamepad_menu"
    bl_label = "Gamepad Menu"

    def draw(self, context):
        layout = self.layout
        # insertNode(layout, "NLGamepadButtonUpCondition", "Button Up")
        insertNode(layout, "NLGamepadButtonsCondition", "Gamepad Button")
        insertNode(layout, "NLGamepadSticksCondition", "Gamepad Sticks")
        # insertNode(layout, "NLGamepadTriggerCondition", "Trigger")
        insertNode(layout, "NLGamepadActive", "Gamepad Active")
        layout.separator()
        insertNode(layout, "NLGamepadVibration", "Gamepad Vibrate")
        insertNode(layout, "NLGamepadLook", "Gamepad Look")


@menu_item
class VRMenu(bpy.types.Menu):
    bl_idname = "LN_MT_vr_menu"
    bl_label = "VR Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLGetVRHeadsetValues", "VR Headset")
        insertNode(layout, "NLGetVRControllerValues", "VR Controller")


@menu_item
class ValuesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_menu"
    bl_label = "Values Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_values_constants_menu", text="Constants", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_values_color_menu", text="Color", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_values_string_menu", text="String Basics", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_values_string_ops_menu", text="String Advanced", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_vector_values_menu", text="Vector", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_values_random_menu", text="Random", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_values_asset_menu", text="Assets", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_values_switch_menu", text="Switches", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_property_menu", text="Properties", icon="RIGHTARROW_THIN")


@menu_item
class ValuesConstantsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_constants_menu"
    bl_label = "Constants"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeSimpleValue", "Boolean", settings={'value_type': '2'})
        insertNode(layout, "LogicNodeSimpleValue", "Float", settings={'value_type': '0'})
        insertNode(layout, "LogicNodeSimpleValue", "Integer", settings={'value_type': '1'})


@menu_item
class ValuesColorMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_color_menu"
    bl_label = "Color"

    def draw(self, context):
        pass


@menu_item
class ValuesStringMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_string_menu"
    bl_label = "String Basics"

    def draw(self, context):
        pass


@menu_item
class ValuesStringOpsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_string_ops_menu"
    bl_label = "String Advanced"

    def draw(self, context):
        pass


@menu_item
class ValuesRandomMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_random_menu"
    bl_label = "Random"

    def draw(self, context):
        pass


@menu_item
class ValuesAssetMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_asset_menu"
    bl_label = "Assets"

    def draw(self, context):
        pass


@menu_item
class ValuesSwitchMenu(bpy.types.Menu):
    bl_idname = "LN_MT_values_switch_menu"
    bl_label = "Switches"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLInvertValueNode", "Invert")
        insertNode(layout, "NLValueSwitch", "Value Switch")


@menu_item
class SimpleValuesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_simple_values_menu"
    bl_label = "Simple Values Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLParameterBooleanValue", "Boolean")
        insertNode(layout, "NLParameterFloatValue", "Float")
        insertNode(layout, "NLParameterIntValue", "Integer")
        insertNode(layout, "NLParameterStringValue", "String")


@menu_item
class GlobalValuesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_global_values_menu"
    bl_label = "Global Values Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLParameterGetGlobalValue", "Get Global Value")
        insertNode(layout, "NLActionSetGlobalValue", "Set Global Value")
        insertNode(layout, "NLActionListGlobalValues", "List Global Category")


@menu_item
class VectorValuesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_vector_values_menu"
    bl_label = "Vector Values Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLParameterRGBNode", "Color RGB")
        insertNode(layout, "NLParameterRGBANode", "Color RGBA")
        insertNode(layout, "NLParameterVector2SplitNode", "Separate XY")
        insertNode(layout, "NLParameterVector3SplitNode", "Separate XYZ")
        insertNode(layout, "NLParameterVector2SimpleNode", "Combine XY")
        insertNode(layout, "NLParameterVector3SimpleNode", "Combine XYZ")
        insertNode(layout, "NLParameterVector4SimpleNode", "Combine XYZW")
        insertNode(layout, "NLParameterEulerSimpleNode", "Euler")
        insertNode(layout, "LogicNodeResizeVector", "Resize Vector")


@menu_item
class RandomValuesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_random_values_menu"
    bl_label = "Random Values Menu"

    def draw(self, context):
        layout = self.layout
        # insertNode(layout, "NLActionRandomFloat", "Random Float")
        # insertNode(layout, "NLActionRandomInteger", "Random Integer")
        # insertNode(layout, "NLRandomVect", "Random Vector")


@menu_item
class AnimationMenu(bpy.types.Menu):
    bl_idname = "LN_MT_animation_menu"
    bl_label = "Animation Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_animation_playback_menu", text="Playback", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_animation_bones_menu", text="Bones", icon="RIGHTARROW_THIN")


@menu_item
class AnimationPlaybackMenu(bpy.types.Menu):
    bl_idname = "LN_MT_animation_playback_menu"
    bl_label = "Playback"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionStopAnimation", "Stop Animation")
        insertNode(layout, "NLActionSetAnimationFrame", "Set Animation Frame")


@menu_item
class AnimationBonesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_animation_bones_menu"
    bl_label = "Bones"

    def draw(self, context):
        pass


@menu_item
class GetBoneDataMenu(bpy.types.Menu):
    bl_idname = "LN_MT_get_bone_data_menu"
    bl_label = "Get Bone Data"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Name", settings={'attribute': 'name'})
        layout.separator()
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Location", settings={'attribute': 'location'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Euler Rotation", settings={'attribute': 'pose_rotation_euler'})
        layout.separator()
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Head", settings={'attribute': 'head'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Local Head", settings={'attribute': 'head_local'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Pose Head", settings={'attribute': 'head_local'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Center", settings={'attribute': 'center'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Local Center", settings={'attribute': 'center_local'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Pose Center", settings={'attribute': 'center_local'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Tail", settings={'attribute': 'tail'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Local Tail", settings={'attribute': 'tail_local'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Pose Tail", settings={'attribute': 'tail_pose'})
        layout.separator()
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Inherit Scale", settings={'attribute': 'inherit_scale'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Inherit Rotation", settings={'attribute': 'inherit_rotation'})
        layout.separator()
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Connected", settings={'attribute': 'connected'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Deform", settings={'attribute': 'deform'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Local", settings={'attribute': 'use_local_location'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Relative Parent", settings={'attribute': 'use_relative_parent'})
        insertNode(layout, "LogicNodeGetRigBoneAttribute", "Get Bone Scale Easing", settings={'attribute': 'use_scale_easing'})


@menu_item
class SetBoneDataMenu(bpy.types.Menu):
    bl_idname = "LN_MT_set_bone_data_menu"
    bl_label = "Set Bone Data"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Location", settings={'attribute': 'location'})
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Euler Rotation", settings={'attribute': 'pose_rotation_euler'})
        layout.separator()
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Inherit Scale", settings={'attribute': 'inherit_scale'})
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Inherit Rotation", settings={'attribute': 'inherit_rotation'})
        layout.separator()
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Connected", settings={'attribute': 'connected'})
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Deform", settings={'attribute': 'deform'})
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Local", settings={'attribute': 'use_local_location'})
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Relative Parent", settings={'attribute': 'use_relative_parent'})
        insertNode(layout, "LogicNodeSetRigBoneAttribute", "Set Bone Scale Easing", settings={'attribute': 'use_scale_easing'})
        # insertNode(layout, "NLActionSetBonePos", "Set Bone Position")


@menu_item
class BoneConstraintsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_boneconstraints_menu"
    bl_label = "Bone Constraints Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLSetBoneConstraintAttribute", "Set Attribute")
        insertNode(layout, "NLSetBoneConstraintInfluence", "Set Influence")
        insertNode(layout, "NLSetBoneConstraintTarget", "Set Target")


@menu_item
class LightsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_lights_menu"
    bl_label = "Lights Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLGetLightColorAction", "Get Light Color")
        insertNode(layout, "NLGetLightEnergy", "Get Light Power")
        layout.separator()
        insertNode(layout, "NLSetLightColorAction", "Set Light Color")
        insertNode(layout, "NLSetLightEnergyAction", "Set Light Power")
        insertNode(layout, "NLSetLightShadowAction", "Set Light Shadow")
        layout.separator()
        insertNode(layout, "NLMakeUniqueLight", "Make Light Unique")


@menu_item
class NodesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_nodes_menu"
    bl_label = "Nodes Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_matnodes_menu", text="Materials", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_geonodes_menu", text="Geometry", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_groupnodes_menu", text="Groups", icon="RIGHTARROW_THIN")


@menu_item
class MatNodesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_matnodes_menu"
    bl_label = "Material Nodes Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLGetMaterialNode", "Get Node")
        insertNode(layout, "NLPlayMaterialSequence", "Play Sequence")
        layout.separator()
        insertNode(layout, "NLGetMaterialNodeValue", "Get Socket Value")
        insertNode(layout, "NLSetMaterialNodeValue", "Set Socket")
        layout.separator()
        insertNode(layout, "NLGetMaterialNodeAttribute", "Get Node Value")
        insertNode(layout, "NLSetMaterialNodeAttribute", "Set Node Value")


@menu_item
class GeoNodesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_geonodes_menu"
    bl_label = "Geometry Nodes Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLGetGeometryNodeValue", "Get Socket Value")
        insertNode(layout, "NLSetGeometryNodeValue", "Set Socket")
        layout.separator()
        insertNode(layout, "NLGetGeometryNodeAttribute", "Get Node Value")
        insertNode(layout, "NLSetGeometryNodeAttribute", "Set Node Value")


@menu_item
class GroupNodesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_groupnodes_menu"
    bl_label = "Node Groups Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLGetNodeGroupNodeValue", "Get Socket Value")
        insertNode(layout, "NLSetNodeTreeNodeValue", "Set Socket")
        layout.separator()
        insertNode(layout, "NLGetNodeTreeNodeAttribute", "Get Node Value")
        insertNode(layout, "NLSetNodeTreeNodeAttribute", "Set Node Value")


@menu_item
class ObjectsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_objects_menu"
    bl_label = "Objects Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_transform_menu", text="Transformation", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_objects_movement_menu", text="Movement", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_objects_hierarchy_menu", text="Hierarchy", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_objects_actions_menu", text="Actions", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_objects_lifecycle_menu", text="Lifecycle", icon="RIGHTARROW_THIN")


@menu_item
class ObjectsMovementMenu(bpy.types.Menu):
    bl_idname = "LN_MT_objects_movement_menu"
    bl_label = "Movement"

    def draw(self, context):
        pass


@menu_item
class ObjectsHierarchyMenu(bpy.types.Menu):
    bl_idname = "LN_MT_objects_hierarchy_menu"
    bl_label = "Hierarchy"

    def draw(self, context):
        insertNode(self.layout, "NLActionRemoveParentNode", "Remove Parent")


@menu_item
class ObjectsActionsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_objects_actions_menu"
    bl_label = "Actions"

    def draw(self, context):
        pass


@menu_item
class ObjectsLifecycleMenu(bpy.types.Menu):
    bl_idname = "LN_MT_objects_lifecycle_menu"
    bl_label = "Lifecycle"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionFindObjectNode", "Get Object")
        insertNode(layout, "NLActionSendMessage", "Send Message")


@menu_item
class TransformMenu(bpy.types.Menu):
    bl_idname = "LN_MT_transform_menu"
    bl_label = "Transformation Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeApplyTransform", "Apply Movement")
        insertNode(layout, "LogicNodeApplyTransform", "Apply Rotation", settings={'mode': '1'})
        insertNode(layout, "LogicNodeApplyTransform", "Apply Force", settings={'mode': '2'})
        insertNode(layout, "LogicNodeApplyTransform", "Apply Torque", settings={'mode': '3'})
        insertNode(layout, "LogicNodeApplyTransform", "Apply Impulse", settings={'mode': '4'})
        layout.separator()
        insertNode(layout, "NLActionAlignAxisToVector", "Align Axis to Vector")
        insertNode(layout, "NLActionFollowPath", "Follow Path")
        insertNode(layout, "NLActionMoveTo", "Move To")
        insertNode(layout, "NLActionNavigate", "Move To with Navmesh")
        insertNode(layout, "NLActionRotateTo", "Rotate To")
        insertNode(layout, "NLSlowFollow", "Slow Follow")
        # insertNode(layout, "NLActionTranslate", "Translate")


@menu_item
class PropertyMenu(bpy.types.Menu):
    bl_idname = "LN_MT_property_menu"
    bl_label = "Properties Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeGetLogicTreeProperty", "Get Tree Property")
        insertNode(layout, "LogicNodeSetLogicTreeProperty", "Set Tree Property")
        insertNode(layout, "LogicNodeToggleLogicTreeProperty", "Toggle Tree Property")
        layout.separator()
        insertNode(layout, "NLGameObjectPropertyParameterNode", "Get Object Property")
        insertNode(layout, "NLSetGameObjectGamePropertyActionNode", "Set Object Property")
        insertNode(layout, "NLGameObjectHasPropertyParameterNode", "Object Has Property")
        insertNode(layout, "NLToggleGameObjectGamePropertyActionNode", "Toggle Object Property")
        insertNode(layout, "NLAddToGameObjectGamePropertyActionNode", "Modify Object Property")
        insertNode(layout, "NLObjectPropertyOperator", "Evaluate Object Property")
        insertNode(layout, "NLCopyPropertyFromObject", "Copy Property From Object")
        layout.separator()
        insertNode(layout, "NLParameterGetGlobalValue", "Get Global Property")
        insertNode(layout, "NLActionSetGlobalValue", "Set Global Property")


@menu_item
class ObjectDataMenu(bpy.types.Menu):
    bl_idname = "LN_MT_object_data_menu"
    bl_label = "Object Data Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLParameterAxisVector", "Get Axis Vector")
        insertNode(layout, "NLGetObjectDataName", "Get Object ID")
        insertNode(layout, "NLGetObjectVertices", "Get Vertices")
        # insertNode(layout, "NLObjectAttributeParameterNode", "Get Position / Rotation / Scale etc.")
        insertNode(layout, "NLActionReplaceMesh", "Replace Mesh")
        insertNode(layout, "LogicNodeSetConstraintAttribute", "Set Constraint Attribute")
        # insertNode(layout, "NLSetObjectAttributeActionNode", "Set Position / Rotation / Scale etc.")


@menu_item
class GetAttributesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_getattributes_menu"
    bl_label = "Get Attributes Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLObjectAttributeParameterNode", "Get World Position", settings={'attr_name': 'worldPosition', 'nl_label': 'Get World Position'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get World Orientation", settings={'attr_name': 'worldOrientation', 'nl_label': 'Get World Orientation'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get World Linear Velocity", settings={'attr_name': 'worldLinearVelocity', 'nl_label': 'Get World Linear Velocity'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get World Angular Velocity", settings={'attr_name': 'worldAngularVelocity', 'nl_label': 'Get World Angular Velocity'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get World Transform", settings={'attr_name': 'worldTransform', 'nl_label': 'Get World Transform'})
        layout.separator()
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Local Position", settings={'attr_name': 'localPosition', 'nl_label': 'Get Local Position'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Local Orientation", settings={'attr_name': 'localOrientation', 'nl_label': 'Get Local Orientation'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Local Linear Velocity", settings={'attr_name': 'localLinearVelocity', 'nl_label': 'Get Local Linear Velocity'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Local Angular Velocity", settings={'attr_name': 'localAngularVelocity', 'nl_label': 'Get Local Angular Velocity'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Local Transform", settings={'attr_name': 'localTransform', 'nl_label': 'Get Local Transform'})
        layout.separator()
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Name", settings={'attr_name': 'name', 'nl_label': 'Get Name'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Scale", settings={'attr_name': 'worldScale', 'nl_label': 'Get World Scale'})
        insertNode(layout, "NLObjectAttributeParameterNode", "Get Color", settings={'attr_name': 'color', 'nl_label': 'Get Color'})


@menu_item
class SetAttributesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_setattributes_menu"
    bl_label = "Set Attributes Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set World Position", settings={'value_type': 'worldPosition', 'nl_label': 'Set World Position'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set World Orientation", settings={'value_type': 'worldOrientation', 'nl_label': 'Set World Orientation'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set World Linear Velocity", settings={'value_type': 'worldLinearVelocity', 'nl_label': 'Set World Linear Velocity'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set World Angular Velocity", settings={'value_type': 'worldAngularVelocity', 'nl_label': 'Set World Angular Velocity'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set World Transform", settings={'value_type': 'worldTransform', 'nl_label': 'Set World Transform'})
        layout.separator()
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Local Position", settings={'value_type': 'localPosition', 'nl_label': 'Set Local Position'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Local Orientation", settings={'value_type': 'localOrientation', 'nl_label': 'Set Local Orientation'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Local Linear Velocity", settings={'value_type': 'localLinearVelocity', 'nl_label': 'Set Local Linear Velocity'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Local Angular Velocity", settings={'value_type': 'localAngularVelocity', 'nl_label': 'Set Local Angular Velocity'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Local Transform", settings={'value_type': 'localTransform', 'nl_label': 'Set Local Transform'})
        layout.separator()
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Scale", settings={'value_type': 'worldScale', 'nl_label': 'Set World Scale'})
        insertNode(layout, "NLSetObjectAttributeActionNode", "Set Color", settings={'value_type': 'color', 'nl_label': 'Set Color'})


@menu_item
class CurveMenu(bpy.types.Menu):
    bl_idname = "LN_MT_curve_menu"
    bl_label = "Curves Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeDistributeCurvePoints", "Distribute Points on Curve")
        insertNode(layout, "LogicNodeEvaluateCurve", "Evaluate Curve")
        layout.separator()
        insertNode(layout, "NLGetCurvePoints", "Get Curve Points")
        insertNode(layout, "NLSetCurvePoints", "Set Curve Points")


@menu_item
class SceneMenu(bpy.types.Menu):
    bl_idname = "LN_MT_scene_menu"
    bl_label = "Scene Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_camera_menu", text="Camera", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_scene_display_menu", text="Display", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_scene_scenes_menu", text="Scenes", icon="RIGHTARROW_THIN")


@menu_item
class SceneDisplayMenu(bpy.types.Menu):
    bl_idname = "LN_MT_scene_display_menu"
    bl_label = "Display"

    def draw(self, context):
        pass


@menu_item
class SceneScenesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_scene_scenes_menu"
    bl_label = "Scenes"

    def draw(self, context):
        pass


@menu_item
class CameraMenu(bpy.types.Menu):
    bl_idname = "LN_MT_camera_menu"
    bl_label = "Camera Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActiveCameraParameterNode", "Active Camera")
        insertNode(layout, "NLActionSetActiveCamera", "Set Camera")
        insertNode(layout, "NLActionSetCameraFov", "Set FOV")
        insertNode(layout, "NLActionSetCameraOrthoScale", "Set Orthographic Scale")
        insertNode(layout, "NLParameterScreenPosition", "World To Screen")
        insertNode(layout, "NLParameterWorldPosition", "Screen To World")


@menu_item
class PostFXMenu(bpy.types.Menu):
    bl_idname = "LN_MT_post_fx_menu"
    bl_label = "Post FX Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLAddFilter", "Add Filter")
        insertNode(layout, "NLRemoveFilter", "Remove Filter")
        insertNode(layout, "NLSetFilterState", "Set Filter State")
        insertNode(layout, "NLToggleFilter", "Toggle Filter")


@menu_item
class CollectionsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_collections_menu"
    bl_label = "Collection Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLGetCollectionNode", "Get Collection")
        insertNode(layout, "NLGetCollectionObjectNamesNode", "Get Object Names")
        insertNode(layout, "NLGetCollectionObjectsNode", "Get Objects")
        layout.separator()
        insertNode(layout, "NLActionSetCollectionVisibility", "Set Collection Visibility")
        insertNode(layout, "NLSetOverlayCollection", "Set Overlay Collection")
        insertNode(layout, "NLRemoveOverlayCollection", "Remove Overlay Collection")


@menu_item
class SoundMenu(bpy.types.Menu):
    bl_idname = "LN_MT_sound_menu"
    bl_label = "Sound Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_sound_playback_menu", text="Playback", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_sound_mixer_menu", text="Mixer", icon="RIGHTARROW_THIN")
        if preferences().use_fmod_nodes:
            layout.menu("LN_MT_fmod_sound_menu", text="FMOD", icon="RIGHTARROW_THIN")


@menu_item
class SoundPlaybackMenu(bpy.types.Menu):
    bl_idname = "LN_MT_sound_playback_menu"
    bl_label = "Playback"

    def draw(self, context):
        pass


@menu_item
class SoundMixerMenu(bpy.types.Menu):
    bl_idname = "LN_MT_sound_mixer_menu"
    bl_label = "Mixer"

    def draw(self, context):
        pass


@menu_item
class FMODSoundMenu(bpy.types.Menu):
    bl_idname = "LN_MT_fmod_sound_menu"
    bl_label = "FMOD"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeFModLoadBank", "Load Bank")
        insertNode(layout, "LogicNodeFModStartEvent", "Start Event Instance")
        layout.separator()
        insertNode(layout, "LogicNodeFModGetEventAttribute", "Get Event Instance Attribute")
        insertNode(layout, "LogicNodeFModSetEventAttribute", "Set Event Instance Attribute")
        layout.separator()
        insertNode(layout, "LogicNodeFModGetEventParameter", "Get Event Instance Parameter")
        insertNode(layout, "LogicNodeFModSetEventParameter", "Set Event Instance Parameter")
        insertNode(layout, "LogicNodeFModModifyEventParameter", "Modify Event Instance Parameter")


@menu_item
class LogicMenu(bpy.types.Menu):
    bl_idname = "LN_MT_logic_menu"
    bl_label = "Logic Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeLogicGate", "Gate")
        insertNode(layout, "NLParameterSwitchValue", "Branch")
        layout.separator()
        insertNode(layout, "LogicNodeLogicGateList", "Gate List")
        insertNode(layout, "NLConditionNone", "None")
        insertNode(layout, "NLConditionNotNoneNode", "Not None")
        layout.separator()
        insertNode(layout, "LogicNodeRaycast", "Raycast")
        layout.separator()
        insertNode(layout, "LogicNodeLoop", "Loop")
        insertNode(layout, "LogicNodeLoopFromList", "Loop From List")


@menu_item
class LogicTreeMenu(bpy.types.Menu):
    bl_idname = "LN_MT_logic_tree_menu"
    bl_label = "Logic Tree Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLStartLogicNetworkActionNode", "Start Logic Tree")
        insertNode(layout, "NLActionExecuteNetwork", "Run Logic Tree")
        insertNode(layout, "NLStopLogicNetworkActionNode", "Stop Logic Tree")
        layout.separator()
        insertNode(layout, "NLActionInstallSubNetwork", "Add Logic Tree to Object")
        insertNode(layout, "NLConditionLogitNetworkStatusNode", "Logic Tree Status")


@menu_item
class LogicBrickMenu(bpy.types.Menu):
    bl_idname = "LN_MT_logic_brick_menu"
    bl_label = "Logic Brick Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLSensorValueNode", "Get Sensor Value")
        insertNode(layout, "NLSetSensorValueNode", "Set Sensor Value")
        insertNode(layout, "NLGetActuatorValue", "Get Actuator Value")
        insertNode(layout, "NLSetActuatorValueNode", "Set Actuator Value")
        layout.separator()
        insertNode(layout, "NLControllerStatus", "Controller Status")
        # insertNode(layout, "NLRunActuatorNode", "Run Actuator")
        insertNode(layout, "NLGetSensorNode", "Sensor Positive")


@menu_item
class MathMenu(bpy.types.Menu):
    bl_idname = "LN_MT_math_menu"
    bl_label = "Math Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_math_scalar_menu", text="Scalar", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_math_ranges_menu", text="Ranges", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_math_vector_menu", text="Vector", icon="RIGHTARROW_THIN")


@menu_item
class MathScalarMenu(bpy.types.Menu):
    bl_idname = "LN_MT_math_scalar_menu"
    bl_label = "Scalar"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeMath", "Math")
        insertNode(layout, "NLLimitRange", "Limit Range")


@menu_item
class MathRangesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_math_ranges_menu"
    bl_label = "Ranges"

    def draw(self, context):
        pass


@menu_item
class MathVectorMenu(bpy.types.Menu):
    bl_idname = "LN_MT_math_vector_menu"
    bl_label = "Vector"

    def draw(self, context):
        pass


@menu_item
class PhysicsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_menu"
    bl_label = "Physics Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_physics_forces_menu", text="Forces", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_physics_bodies_menu", text="Bodies", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_physics_damping_menu", text="Damping & Filtering", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_physics_constraints_menu", text="Constraints", icon="RIGHTARROW_THIN")
        layout.separator()
        layout.menu("LN_MT_vehicle_menu", text="Vehicle", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_physics_navigation_menu", text="Navigation", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_physics_sensors_menu", text="Sensors", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_character_menu", text="Character", icon="RIGHTARROW_THIN")


@menu_item
class PhysicsForcesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_forces_menu"
    bl_label = "Forces"

    def draw(self, context):
        pass


@menu_item
class PhysicsBodiesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_bodies_menu"
    bl_label = "Bodies"

    def draw(self, context):
        pass


@menu_item
class PhysicsDampingMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_damping_menu"
    bl_label = "Damping & Filtering"

    def draw(self, context):
        pass


@menu_item
class PhysicsConstraintsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_constraints_menu"
    bl_label = "Constraints"

    def draw(self, context):
        pass


@menu_item
class PhysicsNavigationMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_navigation_menu"
    bl_label = "Navigation"

    def draw(self, context):
        pass


@menu_item
class PhysicsSensorsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_physics_sensors_menu"
    bl_label = "Sensors"

    def draw(self, context):
        pass


@menu_item
class VehicleMenu(bpy.types.Menu):
    bl_idname = "LN_MT_vehicle_menu"
    bl_label = "Vehicle Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLCreateVehicleFromParent", "Create New Vehicle")
        layout.separator()
        insertNode(layout, "NLVehicleApplyEngineForce", "Accelerate")
        insertNode(layout, "NLVehicleApplyBraking", "Brake")
        insertNode(layout, "NLVehicleSetAttributes", "Set Vehicle Attributes")
        insertNode(layout, "NLVehicleApplySteering", "Steer")


@menu_item
class CharacterMenu(bpy.types.Menu):
    bl_idname = "LN_MT_character_menu"
    bl_label = "Character Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionSetCharacterWalkDir", "Walk")
        insertNode(layout, "NLActionCharacterJump", "Jump")
        layout.separator()
        insertNode(layout, "NLActionGetCharacterInfo", "Get Physics Info")
        insertNode(layout, "NLSetCharacterJumpSpeed", "Set Jump Force")
        insertNode(layout, "NLSetActionCharacterJump", "Set Max Jumps")
        insertNode(layout, "NLActionSetCharacterGravity", "Set Gravity")
        insertNode(layout, "NLActionSetCharacterVelocity", "Set Velocity")


@menu_item
class PythonMenu(bpy.types.Menu):
    bl_idname = "LN_MT_python_menu"
    bl_label = "Python Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLParameterPythonModuleFunction", "Run Python Code")
        layout.separator()
        insertNode(layout, "NLParameterGetAttribute", "Get Instance Attribute")
        insertNode(layout, "NLParameterSetAttribute", "Set Object Attribute")
        insertNode(layout, "NLParameterTypeCast", "Typecast Value")


@menu_item
class RaycastMenu(bpy.types.Menu):
    bl_idname = "LN_MT_raycast_menu"
    bl_label = "Raycast Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionRayCastNode", "Raycast")
        insertNode(layout, "NLActionMousePickNode", "Mouse Ray")
        insertNode(layout, "NLActionCameraPickNode", "Camera Ray")
        insertNode(layout, "NLProjectileRayCast", "Projectile Ray")


@menu_item
class TimeMenu(bpy.types.Menu):
    bl_idname = "LN_MT_time_menu"
    bl_label = "Time Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_time_clock_menu", text="Clock", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_time_timers_menu", text="Timers", icon="RIGHTARROW_THIN")


@menu_item
class TimeClockMenu(bpy.types.Menu):
    bl_idname = "LN_MT_time_clock_menu"
    bl_label = "Clock"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLParameterTimeNode", "Time")
        insertNode(layout, "NLParameterTimeNode", "Delta (Frametime)")
        insertNode(layout, "NLParameterTimeNode", "FPS")
        insertNode(layout, "LogicNodeTimeFactor", "Delta Factor")
        insertNode(layout, "NLParameterGetTimeScale", "Get Timescale")
        insertNode(layout, "NLActionSetTimeScale", "Set Timescale")


@menu_item
class TimeTimersMenu(bpy.types.Menu):
    bl_idname = "LN_MT_time_timers_menu"
    bl_label = "Timers"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionTimeFilter", "Pulsify")
        insertNode(layout, "NLActionTimeBarrier", "Barrier")


@menu_item
class FileMenu(bpy.types.Menu):
    bl_idname = "LN_MT_file_menu"
    bl_label = "File Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeGetFont", "Get Font")
        insertNode(layout, "NLGetImage", "Get Image")
        insertNode(layout, "NLGetSound", "Get Sound")
        layout.separator()
        layout.menu("LN_MT_file_saveload_menu", text="Save & Load", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_file_files_menu", text="Files", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_file_json_menu", text="JSON", icon="RIGHTARROW_THIN")


@menu_item
class FileSaveLoadMenu(bpy.types.Menu):
    bl_idname = "LN_MT_file_saveload_menu"
    bl_label = "Save & Load"

    def draw(self, context):
        pass


@menu_item
class FileFilesMenu(bpy.types.Menu):
    bl_idname = "LN_MT_file_files_menu"
    bl_label = "Files"

    def draw(self, context):
        pass


@menu_item
class FileJSONMenu(bpy.types.Menu):
    bl_idname = "LN_MT_file_json_menu"
    bl_label = "JSON"

    def draw(self, context):
        pass


@menu_item
class NetworkMenu(bpy.types.Menu):
    bl_idname = "LN_MT_network_menu"
    bl_label = "Network Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_network_rooms_menu", text="Rooms", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_network_messaging_menu", text="Messaging", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_network_sync_menu", text="Sync", icon="RIGHTARROW_THIN")


@menu_item
class NetworkRoomsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_network_rooms_menu"
    bl_label = "Rooms"

    def draw(self, context):
        pass


@menu_item
class NetworkMessagingMenu(bpy.types.Menu):
    bl_idname = "LN_MT_network_messaging_menu"
    bl_label = "Messaging"

    def draw(self, context):
        pass


@menu_item
class NetworkSyncMenu(bpy.types.Menu):
    bl_idname = "LN_MT_network_sync_menu"
    bl_label = "Sync"

    def draw(self, context):
        pass

@menu_item
class PathMenu(bpy.types.Menu):
    bl_idname = "LN_MT_path_menu"
    bl_label = "Path"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeGetMasterFolder", "Get Master Folder")
        insertNode(layout, "LogicNodeJoinPath", "Join Path")


@menu_item
class DataMenu(bpy.types.Menu):
    bl_idname = "LN_MT_data_menu"
    bl_label = "Data Menu"

    def draw(self, context):
        layout = self.layout
        layout.menu("LN_MT_list_menu", text="List", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_list_ops_menu", text="List Ops", icon="RIGHTARROW_THIN")


@menu_item
class PortalMenu(bpy.types.Menu):
    bl_idname = "LN_MT_portal_menu"
    bl_label = "Portal Menu"

    def draw(self, context):
        layout = self.layout
        insertPortal(layout, "Portal In", 'in')
        # insertPortal(layout, "Portal Out", 'out')
        if len(context.scene.nl_portals):
            layout.separator()
        for portal in context.scene.nl_portals:
            insertNode(layout, "LogicNodeGetPortal", f"{portal.name}", settings={'portal': portal.name, 'hide': True, 'nl_label': portal.name})


@menu_item
class ListMenu(bpy.types.Menu):
    bl_idname = "LN_MT_list_menu"
    bl_label = "List Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLInitEmptyList", "New List")
        insertNode(layout, "LogicNodeListFromItems", "List From Items")
        layout.separator()
        insertNode(layout, "NLAppendListItem", "Append")
        insertNode(layout, "NLExtendList", "Extend")
        insertNode(layout, "NLRemoveListIndex", "Remove Index")
        insertNode(layout, "NLRemoveListValue", "Remove Value")
        layout.separator()
        insertNode(layout, "NLGetListIndexNode", "Get List Index")
        insertNode(layout, "NLSetListIndex", "Set List Index")
        insertNode(layout, "NLGetRandomListIndex", "Get Random List Item")
        insertNode(layout, "NLDuplicateList", "Duplicate")


@menu_item
class ListOpsMenu(bpy.types.Menu):
    bl_idname = "LN_MT_list_ops_menu"
    bl_label = "List Ops"

    def draw(self, context):
        pass


@menu_item
class DictMenu(bpy.types.Menu):
    bl_idname = "LN_MT_dict_menu"
    bl_label = "Dictionary Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLInitEmptyDict", "New Dictionary")
        insertNode(layout, "NLInitNewDict", "Dictionary From Items")
        layout.separator()
        insertNode(layout, "NLGetDictKeyNode", "Get Dictionary Key")
        insertNode(layout, "NLSetDictKeyValue", "Set Dictionary Key")
        insertNode(layout, "NLSetDictDelKey", "Remove Dictionary Key")
        layout.separator()
        insertNode(layout, "LogicNodeDictGetKeys", "Get Dictionary Keys")
        # insertNode(layout, "NLGetDictKeyNode", "Get Dictionary Key")


@menu_item
class VariableMenu(bpy.types.Menu):
    bl_idname = "LN_MT_variable_menu"
    bl_label = "Variable Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionLoadVariable", "Load Variable")
        insertNode(layout, "NLActionSaveVariable", "Save Variable")
        insertNode(layout, "NLActionRemoveVariable", "Remove Variable")
        layout.separator()
        insertNode(layout, "NLActionLoadVariables", "Load Variable Dict")
        insertNode(layout, "NLActionSaveVariables", "Save Variable Dict")
        insertNode(layout, "NLActionClearVariables", "Clear Variables")
        layout.separator()
        insertNode(layout, "NLActionListVariables", "List Saved Variables")


@menu_item
class LayoutMenu(bpy.types.Menu):
    bl_idname = "LN_MT_layout_menu"
    bl_label = "Layout Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NodeReroute", "Reroute")
        insertNode(layout, "NodeFrame", "Frame")


@menu_item
class RenderMenu(bpy.types.Menu):
    bl_idname = "LN_MT_render_menu"
    bl_label = "Render Menu"

    def draw(self, context):
        layout = self.layout
        # layout.menu("LN_MT_eevee_menu", text="EEVEE", icon="RIGHTARROW_THIN")
        insertNode(layout, "NLGetFullscreen", "Get Fullscreen")
        insertNode(layout, "NLActionSetFullscreen", "Set Fullscreen")
        insertNode(layout, "NLGetResolution", "Get Resolution")
        insertNode(layout, "NLActionSetResolution", "Set Resolution")
        insertNode(layout, "NLGetVsyncNode", "Get VSync")
        insertNode(layout, "NLActionSetVSync", "Set VSync")
        layout.separator()
        insertNode(layout, "NLShowFramerate", "Show Framerate")
        insertNode(layout, "NLSetProfile", "Show Profile")


@menu_item
class DrawMenu(bpy.types.Menu):
    bl_idname = "LN_MT_draw_menu"
    bl_label = "Draw Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLDrawLine", "Line")
        insertNode(layout, "NLDrawCube", "Cube")
        insertNode(layout, "NLDrawBox", "Box")


@menu_item
class EeveeMenu(bpy.types.Menu):
    bl_idname = "LN_MT_eevee_menu"
    bl_label = "EEVEE Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLSetEeveeAO", "Set Ambient Occlusion")
        insertNode(layout, "NLSetEeveeBloom", "Set Bloom")
        insertNode(layout, "NLSetExposureAction", "Set Exposure")
        insertNode(layout, "NLSetGammaAction", "Set Gamma")
        insertNode(layout, "NLSetEeveeSSR", "Set SSR")
        insertNode(layout, "NLSetEeveeVolumetrics", "Set Volumetric Light")


@menu_item
class UIMenu(bpy.types.Menu):
    bl_idname = "LN_MT_ui_menu"
    bl_label = "UI Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeCreateUICanvas", "Create Canvas")
        layout.separator()
        layout.menu("LN_MT_ui_create_menu", text="Create", icon="RIGHTARROW_THIN")
        layout.menu("LN_MT_widget_menu", text="Widgets", icon="RIGHTARROW_THIN")
        layout.separator()
        insertNode(layout, "LogicNodeSetCustomCursor", "Set Custom Cursor")


@menu_item
class UICreateMenu(bpy.types.Menu):
    bl_idname = "LN_MT_ui_create_menu"
    bl_label = "Create"

    def draw(self, context):
        pass


@menu_item
class WidgetMenu(bpy.types.Menu):
    bl_idname = "LN_MT_widget_menu"
    bl_label = "Widget Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "LogicNodeCreateUILayout", "Create Layout")
        layout.separator()
        insertNode(layout, "LogicNodeCreateUIButton", "Create Button")
        insertNode(layout, "LogicNodeCreateUILabel", "Create Label")
        insertNode(layout, "LogicNodeCreateUIImage", "Create Image")
        insertNode(layout, "LogicNodeCreateUISliderWidget", "Create Slider")
        insertNode(layout, "LogicNodeCreateUIPath", "Create Path")


@menu_item
class UtilityMenu(bpy.types.Menu):
    bl_idname = "LN_MT_utility_menu"
    bl_label = "Utility Menu"

    def draw(self, context):
        layout = self.layout
        insertNode(layout, "NLActionPrint", "Print")
        insertNode(layout, "LogicNodeDraw", "Draw")
        layout.separator()
        layout.operator(
            "gamenodes.toggle_debug_trace",
            text="Toggle Debug Trace",
            icon="FILE_REFRESH",
        )


@menu_item
class CustomMenu(bpy.types.Menu):
    bl_idname = "LN_MT_custom_menu"
    bl_label = "Custom Menu"

    def draw(self, context):
        layout = self.layout
        for node in get_custom_game_nodes():
            insertNode(layout, node.idname, node.label)


# ---------------------------------------------------------------------------
# Native runtime nodes merged into the existing category menus.
#
# node_palette.DOMAINS is the single source of truth for the node labels; this
# table only decides which *submenu* each native node belongs to.  Every native
# node is listed exactly once, each submenu stays below 11 entries, and the top
# level keeps the original category names (no separate "Native Nodes" tree).
# ---------------------------------------------------------------------------
_NATIVE_GROUP_MENU = {
    'LN_MT_events_lifecycle_menu': (
        'GameNodeOnInit', 'GameNodeOnUpdate', 'GameNodeOnce',
    ),
    'LN_MT_events_flow_menu': (
        'GameNodeSequence', 'GameNodeBranch', 'GameNodeForEach',
    ),
    'LN_MT_events_signals_menu': (
        'GameNodeActionDone', 'GameNodeMessageReceived',
        'GameNodeSoundFinished', 'GameNodeSink',
    ),
    'LN_MT_mouse_menu': (
        'GameNodeMouseButton', 'GameNodeMousePosition',
        'GameNodeMouseDelta', 'GameNodeMouseWheel',
    ),
    'LN_MT_mouse_cursor_menu': (
        'GameNodeSetMousePosition', 'GameNodeSetCursorVisible', 'GameNodeMouseLook',
    ),
    'LN_MT_keyboard_menu': (
        'GameNodeKeyboard', 'GameNodeKeyCombo',
    ),
    'LN_MT_gamepad_menu': (
        'GameNodeJoystickAxis', 'GameNodeJoystickButton',
        'GameNodeGamepadButton', 'GameNodeGamepadAxis', 'GameNodeGamepadTrigger',
        'GameNodeGamepadCount', 'GameNodeVibrateGamepad',
    ),
    'LN_MT_touch_menu': (
        'GameNodeTouch', 'GameNodeTouchInput', 'GameNodeTouchGet',
        'GameNodeTouchGesture', 'GameNodeTouchLegacy',
    ),
    'LN_MT_input_map_menu': (
        'GameNodeInputMap', 'GameNodeGetInputValue',
    ),
    'LN_MT_values_constants_menu': (
        'GameNodeStringConst',
    ),
    'LN_MT_values_color_menu': (
        'GameNodeColorConst', 'GameNodeColorFromVector', 'GameNodeColorLerp',
        'GameNodeColorMix', 'GameNodeColorToRGBA', 'GameNodeRGBAToColor',
        'GameNodeHSVToRGB', 'GameNodeRGBToHSV', 'GameNodeVectorFromColor',
    ),
    'LN_MT_values_string_menu': (
        'GameNodeStringConcat', 'GameNodeStringFormat', 'GameNodeStringFind',
        'GameNodeStringLength', 'GameNodeStringReplace', 'GameNodeStringSubstr',
        'GameNodeStringToLower', 'GameNodeStringToUpper',
        'GameNodeStringCompare', 'GameNodeStringToNumber',
    ),
    'LN_MT_values_string_ops_menu': (
        'GameNodeNumberToString', 'GameNodeStringSplit', 'GameNodeStringJoin',
        'GameNodeStringRegexMatch', 'GameNodeStringRegexReplace',
        'GameNodeStringTrim', 'GameNodeStringPad',
        'GameNodeStringLengthChars', 'GameNodeStringPadChars',
    ),
    'LN_MT_values_random_menu': (
        'GameNodeRandomFloat', 'GameNodeRandomValue',
    ),
    'LN_MT_values_asset_menu': (
        'GameNodeAssetGet', 'GameNodeAssetSet',
    ),
    'LN_MT_values_switch_menu': (
        'GameNodeObjectPropertyOperator', 'GameNodeWorldPositionLegacy',
    ),
    'LN_MT_animation_playback_menu': (
        'GameNodePlayActionByLayer', 'GameNodeBlendActions',
        'GameNodeSetAnimationSpeed', 'GameNodeAddAnimationEvent',
        'GameNodeGetAnimationFrame',
    ),
    'LN_MT_animation_bones_menu': (
        'GameNodeSetBoneTransform', 'GameNodeClearBoneOverride',
        'GameNodeSetBoneWeight', 'GameNodePlayBoneAction',
    ),
    'LN_MT_transform_menu': (
        'GameNodeSetPosition', 'GameNodeSetRotation', 'GameNodeSetScale',
        'GameNodeTranslate', 'GameNodeWorldPosition', 'GameNodeWorldRotation',
        'GameNodeWorldScale',
    ),
    'LN_MT_objects_movement_menu': (
        'GameNodeMoveTo', 'GameNodeRotateTo', 'GameNodeAlignAxisToVector',
    ),
    'LN_MT_objects_hierarchy_menu': (
        'GameNodeGetOwner', 'GameNodeGetParent', 'GameNodeSetParent',
        'GameNodeClearParent',
    ),
    'LN_MT_objects_actions_menu': (
        'GameNodePlayAction', 'GameNodePlayActionByName', 'GameNodeStopAction',
        'GameNodeSetActionFrame', 'GameNodeGetActionFrame', 'GameNodeSetActionSpeed',
    ),
    'LN_MT_objects_lifecycle_menu': (
        'GameNodeAddObject', 'GameNodeRemoveObject', 'GameNodeObjectByName',
        'GameNodeSetVisibility', 'GameNodeSetMaterial',
    ),
    'LN_MT_camera_menu': (
        'GameNodeSetActiveCamera',
    ),
    'LN_MT_scene_display_menu': (
        'GameNodeSetFontText', 'GameNodeSetLayerWeight',
    ),
    'LN_MT_scene_scenes_menu': (
        'GameNodeLoadScene', 'GameNodeSuspendScene', 'GameNodeResumeScene',
        'GameNodeLoadSceneAsync', 'GameNodeUnloadScene', 'GameNodeMountChildScene',
        'GameNodeUnmountChildScene', 'GameNodeTransitionScene', 'GameNodeGetActiveScenes',
    ),
    'LN_MT_sound_playback_menu': (
        'GameNodeStartSound', 'GameNodePlaySoundByName', 'GameNodePlaySound3D',
        'GameNodePlaySoundPlaylist', 'GameNodeStopSound',
    ),
    'LN_MT_sound_mixer_menu': (
        'GameNodeSetSoundVolume', 'GameNodeSetSoundPitch', 'GameNodeSet3DListener',
        'GameNodeSetAudioAttenuation', 'GameNodeSetAudioDoppler', 'GameNodeAudioZone',
        'GameNodeSetAudioBusVolume',
    ),
    'LN_MT_logic_menu': (
        'GameNodeCompare', 'GameNodeLogicGate', 'GameNodeLogicNot', 'GameNodeIsNone',
    ),
    'LN_MT_math_scalar_menu': (
        'GameNodeClamp', 'GameNodeClampValue', 'GameNodeWrap',
        'GameNodeRound', 'GameNodeSign', 'GameNodeFormula',
    ),
    'LN_MT_math_ranges_menu': (
        'GameNodeThreshold', 'GameNodeWithinRange', 'GameNodeMapRange',
        'GameNodeInterpolate',
    ),
    'LN_MT_math_vector_menu': (
        'GameNodeDistance', 'GameNodeVectorAngle', 'GameNodeVectorDistance',
        'GameNodeVectorProject', 'GameNodeVectorReflect', 'GameNodeVectorRotateToward',
    ),
    'LN_MT_physics_forces_menu': (
        'GameNodeApplyForce', 'GameNodeApplyImpulse', 'GameNodeApplyTorque',
    ),
    'LN_MT_physics_bodies_menu': (
        'GameNodeCollision', 'GameNodeGetVelocity', 'GameNodeSetLinearVelocity',
        'GameNodeSetRigidBody', 'GameNodeSetDynamics', 'GameNodeSetPhysicsMass',
        'GameNodeSetPhysicsFriction', 'GameNodeSetPhysicsRestitution',
    ),
    'LN_MT_physics_damping_menu': (
        'GameNodeGetGravity', 'GameNodeSetGravity', 'GameNodeSetLinearDamping',
        'GameNodeSetAngularDamping', 'GameNodeSetCollisionMask',
        'GameNodeSetCollisionGroup',
    ),
    'LN_MT_physics_constraints_menu': (
        'GameNodeAddConstraint', 'GameNodeRemoveConstraint', 'GameNodeAddJoint',
    ),
    'LN_MT_vehicle_menu': (
        'GameNodeCreateVehicle', 'GameNodeVehicleControl',
    ),
    'LN_MT_physics_navigation_menu': (
        'GameNodeBuildNavMesh', 'GameNodeQueryPath', 'GameNodeRaycastBatch',
    ),
    'LN_MT_physics_sensors_menu': (
        'GameNodeRadar', 'GameNodeRadarExtended', 'GameNodeLineOfSight',
        'GameNodeAreaSensor', 'GameNodeRaycastSensor', 'GameNodeRaycastAll',
        'GameNodeProximitySensor',
    ),
    'LN_MT_time_clock_menu': (
        'GameNodeSetSceneTimeScale',
    ),
    'LN_MT_time_timers_menu': (
        'GameNodeDelay', 'GameNodeTimer',
    ),
    'LN_MT_list_menu': (
        'GameNodeListAdd', 'GameNodeListClear', 'GameNodeListContains',
        'GameNodeListGet', 'GameNodeListSet', 'GameNodeListRemove',
        'GameNodeListLength',
    ),
    'LN_MT_list_ops_menu': (
        'GameNodeListSort', 'GameNodeListFilter', 'GameNodeListMap',
        'GameNodeListFind', 'GameNodeListSlice', 'GameNodeListConcat',
        'GameNodeListReverse', 'GameNodeListUnique', 'GameNodeListIndexOf',
    ),
    'LN_MT_file_saveload_menu': (
        'GameNodeSaveGame', 'GameNodeLoadGame', 'GameNodeSaveGameBinary',
        'GameNodeSaveGameSlots', 'GameNodeEncryptData', 'GameNodeDecryptData',
    ),
    'LN_MT_file_files_menu': (
        'GameNodeReadFile', 'GameNodeWriteFile', 'GameNodeFileExists',
        'GameNodeDeleteFile', 'GameNodeFileReadLines', 'GameNodeFileReadChunk',
        'GameNodeFileWriteChunk', 'GameNodeListDirectory',
        'GameNodeCreateDirectory', 'GameNodeRemoveDirectory',
    ),
    'LN_MT_file_json_menu': (
        'GameNodeJSONStringify', 'GameNodeJSONParse',
    ),
    'LN_MT_network_rooms_menu': (
        'GameNodeHostRoom', 'GameNodeJoinRoom', 'GameNodeLeaveRoom',
        'GameNodeListRooms', 'GameNodeRefreshRooms', 'GameNodeDiscoverRooms',
        'GameNodeReconnect',
    ),
    'LN_MT_network_messaging_menu': (
        'GameNodeOnNetworkEvent', 'GameNodeSendNetworkMessage', 'GameNodeBroadcastMessage',
        'GameNodeSendDelayedMessage', 'GameNodeSendRPC', 'GameNodeOnRPC',
    ),
    'LN_MT_network_sync_menu': (
        'GameNodeSyncProperty', 'GameNodeGetRemoteProperty',
        'GameNodeSyncPlayerState', 'GameNodeGetPlayerState',
    ),
    'LN_MT_render_menu': (
        'GameNodeSetFullScreen', 'GameNodeSetResolution',
    ),
    'LN_MT_ui_create_menu': (
        'GameNodeCreateUICanvas', 'GameNodeCreateUILayout', 'GameNodeCreateUILabel',
        'GameNodeCreateUIImage', 'GameNodeCreateUIButton', 'GameNodeCreateUISliderWidget',
        'GameNodeCreateScrollView', 'GameNodeCreateListView', 'GameNodeCreateTabView',
    ),
    'LN_MT_widget_menu': (
        'GameNodeMoveWidget', 'GameNodeSetUIWidgetAttr', 'GameNodeGetUIWidgetAttr',
        'GameNodeUIAnimation', 'GameNodeUIScreenAnchor', 'GameNodeUIScreenStack',
    ),
    'LN_MT_game_menu': (
        'GameNodeQuitGame', 'GameNodeRestartGame',
    ),
    'LN_MT_utility_menu': (
        'GameNodePrint', 'GameNodeFrameTime', 'GameNodeSetTimeScale', 'GameNodeSendMessage',
    ),
}

_MENU_CLASSES = {
    'LN_MT_events_menu': EventsMenu,
    'LN_MT_events_lifecycle_menu': EventsLifecycleMenu,
    'LN_MT_events_flow_menu': EventsFlowMenu,
    'LN_MT_events_signals_menu': EventsSignalsMenu,
    'LN_MT_game_menu': GameMenu,
    'LN_MT_input_menu': InputMenu,
    'LN_MT_mouse_menu': MouseMenu,
    'LN_MT_mouse_cursor_menu': MouseCursorMenu,
    'LN_MT_keyboard_menu': KeyboardMenu,
    'LN_MT_gamepad_menu': GamepadMenu,
    'LN_MT_touch_menu': TouchMenu,
    'LN_MT_input_map_menu': InputMapMenu,
    'LN_MT_values_menu': ValuesMenu,
    'LN_MT_values_constants_menu': ValuesConstantsMenu,
    'LN_MT_values_color_menu': ValuesColorMenu,
    'LN_MT_values_string_menu': ValuesStringMenu,
    'LN_MT_values_string_ops_menu': ValuesStringOpsMenu,
    'LN_MT_values_random_menu': ValuesRandomMenu,
    'LN_MT_values_asset_menu': ValuesAssetMenu,
    'LN_MT_values_switch_menu': ValuesSwitchMenu,
    'LN_MT_animation_menu': AnimationMenu,
    'LN_MT_animation_playback_menu': AnimationPlaybackMenu,
    'LN_MT_animation_bones_menu': AnimationBonesMenu,
    'LN_MT_objects_menu': ObjectsMenu,
    'LN_MT_transform_menu': TransformMenu,
    'LN_MT_objects_movement_menu': ObjectsMovementMenu,
    'LN_MT_objects_hierarchy_menu': ObjectsHierarchyMenu,
    'LN_MT_objects_actions_menu': ObjectsActionsMenu,
    'LN_MT_objects_lifecycle_menu': ObjectsLifecycleMenu,
    'LN_MT_scene_menu': SceneMenu,
    'LN_MT_camera_menu': CameraMenu,
    'LN_MT_scene_display_menu': SceneDisplayMenu,
    'LN_MT_scene_scenes_menu': SceneScenesMenu,
    'LN_MT_sound_menu': SoundMenu,
    'LN_MT_sound_playback_menu': SoundPlaybackMenu,
    'LN_MT_sound_mixer_menu': SoundMixerMenu,
    'LN_MT_logic_menu': LogicMenu,
    'LN_MT_math_menu': MathMenu,
    'LN_MT_math_scalar_menu': MathScalarMenu,
    'LN_MT_math_ranges_menu': MathRangesMenu,
    'LN_MT_math_vector_menu': MathVectorMenu,
    'LN_MT_physics_menu': PhysicsMenu,
    'LN_MT_physics_forces_menu': PhysicsForcesMenu,
    'LN_MT_physics_bodies_menu': PhysicsBodiesMenu,
    'LN_MT_physics_damping_menu': PhysicsDampingMenu,
    'LN_MT_physics_constraints_menu': PhysicsConstraintsMenu,
    'LN_MT_vehicle_menu': VehicleMenu,
    'LN_MT_physics_navigation_menu': PhysicsNavigationMenu,
    'LN_MT_physics_sensors_menu': PhysicsSensorsMenu,
    'LN_MT_character_menu': CharacterMenu,
    'LN_MT_time_menu': TimeMenu,
    'LN_MT_time_clock_menu': TimeClockMenu,
    'LN_MT_time_timers_menu': TimeTimersMenu,
    'LN_MT_data_menu': DataMenu,
    'LN_MT_list_menu': ListMenu,
    'LN_MT_list_ops_menu': ListOpsMenu,
    'LN_MT_file_menu': FileMenu,
    'LN_MT_file_saveload_menu': FileSaveLoadMenu,
    'LN_MT_file_files_menu': FileFilesMenu,
    'LN_MT_file_json_menu': FileJSONMenu,
    'LN_MT_network_menu': NetworkMenu,
    'LN_MT_network_rooms_menu': NetworkRoomsMenu,
    'LN_MT_network_messaging_menu': NetworkMessagingMenu,
    'LN_MT_network_sync_menu': NetworkSyncMenu,
    'LN_MT_render_menu': RenderMenu,
    'LN_MT_ui_menu': UIMenu,
    'LN_MT_ui_create_menu': UICreateMenu,
    'LN_MT_widget_menu': WidgetMenu,
    'LN_MT_utility_menu': UtilityMenu,
}


def _native_menu_entries():
    from .node_palette import DOMAINS

    labels = {
        bl_idname: label
        for _key, _label, nodes in DOMAINS
        for bl_idname, label in nodes
    }
    grouped = {}
    for menu_id, bl_idnames in _NATIVE_GROUP_MENU.items():
        for bl_idname in bl_idnames:
            grouped.setdefault(menu_id, []).append((bl_idname, labels[bl_idname]))
    return grouped


# Resolved on the first menu draw: node_palette imports insertNode from this
# module, so reading DOMAINS at import time would be a circular import.
_NATIVE_ENTRIES_CACHE = None


def _native_entries_for(menu_id):
    global _NATIVE_ENTRIES_CACHE
    if _NATIVE_ENTRIES_CACHE is None:
        _NATIVE_ENTRIES_CACHE = _native_menu_entries()
    return _NATIVE_ENTRIES_CACHE.get(menu_id, ())


def _merge_native_entries(menu_cls, menu_id):
    original = menu_cls.draw

    def draw(self, context):
        global _MENU_SEEN
        previous = _MENU_SEEN
        _MENU_SEEN = set()
        try:
            original(self, context)
            column = None
            for bl_idname, label in _native_entries_for(menu_id):
                if _MENU_SEEN is not None and (bl_idname, label) in _MENU_SEEN:
                    continue
                if column is None:
                    self.layout.separator()
                    column = self.layout.column(align=True)
                insertNode(column, bl_idname, label)
        finally:
            _MENU_SEEN = previous

    menu_cls.draw = draw


for _menu_id, _menu_cls in _MENU_CLASSES.items():
    _merge_native_entries(_menu_cls, _menu_id)

