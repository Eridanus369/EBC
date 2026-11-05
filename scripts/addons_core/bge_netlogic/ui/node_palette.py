"""Native node palette.

A second, coexisting view of the native game nodes grouped by capability
domain. The NODE_MT_add menu stays untouched: this panel only adds a
collapsible list in the node editor sidebar, with the collapse state stored on
the addon preferences so it survives restarts.
"""

import bpy

from .interface import ui_panel
from .node_menu import insertNode
from ..editor.nodetree import LogicNodeTree
from ..utilities import preferences

DOMAINS = (
    ('trigger', 'Trigger', (
        ('GameNodeOnInit', 'On Init'),
        ('GameNodeOnUpdate', 'On Update'),
        ('GameNodeOnce', 'Once'),
        ('GameNodeTimer', 'Timer'),
        ('GameNodeDelay', 'Delay'),
        ('GameNodeSequence', 'Sequence'),
        ('GameNodeBranch', 'Branch'),
        ('GameNodeForEach', 'For Each'),
        ('GameNodeActionDone', 'Action Done'),
        ('GameNodeMessageReceived', 'Message Received'),
        ('GameNodeSoundFinished', 'Sound Finished'),
        ('GameNodeSink', 'Sink'),
    )),
    ('input', 'Input', (
        ('GameNodeKeyboard', 'Keyboard Key'),
        ('GameNodeMouseButton', 'Mouse Button'),
        ('GameNodeMousePosition', 'Mouse Position'),
        ('GameNodeMouseDelta', 'Mouse Delta'),
        ('GameNodeMouseWheel', 'Mouse Wheel'),
        ('GameNodeJoystickAxis', 'Joystick Axis'),
        ('GameNodeJoystickButton', 'Joystick Button'),
        ('GameNodeTouch', 'Touch'),
        ('GameNodeRadar', 'Radar'),
        ('GameNodeSetMousePosition', 'Set Mouse Position'),
        ('GameNodeSetCursorVisible', 'Show Mouse Cursor'),
        ('GameNodeMouseLook', 'Mouse Look'),
        ('GameNodeGamepadButton', 'Gamepad Button'),
        ('GameNodeGamepadAxis', 'Gamepad Axis'),
        ('GameNodeGamepadTrigger', 'Gamepad Trigger'),
        ('GameNodeTouchInput', 'Touch Input'),
        ('GameNodeKeyCombo', 'Key Combo'),
        ('GameNodeInputMap', 'Input Map'),
        ('GameNodeGetInputValue', 'Get Input Value'),
        ('GameNodeVibrateGamepad', 'Vibrate Gamepad'),
    )),
    ('object', 'Object', (
        ('GameNodeAddObject', 'Add Object'),
        ('GameNodeRemoveObject', 'Remove Object'),
        ('GameNodeObjectByName', 'Object By Name'),
        ('GameNodeGetOwner', 'Get Owner'),
        ('GameNodeGetParent', 'Get Parent'),
        ('GameNodeSetParent', 'Set Parent'),
        ('GameNodeClearParent', 'Clear Parent'),
        ('GameNodeSetPosition', 'Set Position'),
        ('GameNodeSetRotation', 'Set Rotation'),
        ('GameNodeSetScale', 'Set Scale'),
        ('GameNodeTranslate', 'Translate'),
        ('GameNodeSetVisibility', 'Set Visibility'),
        ('GameNodeWorldPosition', 'World Position'),
        ('GameNodeWorldRotation', 'World Rotation'),
        ('GameNodeWorldScale', 'World Scale'),
        ('GameNodePlayAction', 'Play Action'),
        ('GameNodePlayActionByName', 'Play Action By Name'),
        ('GameNodeStopAction', 'Stop Action'),
        ('GameNodeSetActionFrame', 'Set Action Frame'),
        ('GameNodeGetActionFrame', 'Get Action Frame'),
        ('GameNodeSetActionSpeed', 'Set Action Speed'),
        ('GameNodeRotateTo', 'Rotate To'),
        ('GameNodeMoveTo', 'Move To'),
        ('GameNodeAlignAxisToVector', 'Align Axis To Vector'),
        ('GameNodeSetMaterial', 'Set Material'),
    )),
    ('physics', 'Physics', (
        ('GameNodeApplyForce', 'Apply Force'),
        ('GameNodeApplyImpulse', 'Apply Impulse'),
        ('GameNodeApplyTorque', 'Apply Torque'),
        ('GameNodeCollision', 'Collision'),
        ('GameNodeGetVelocity', 'Get Velocity'),
        ('GameNodeSetLinearVelocity', 'Set Linear Velocity'),
        ('GameNodeGetGravity', 'Get Gravity'),
        ('GameNodeSetGravity', 'Set Gravity'),
        ('GameNodeSetDynamics', 'Set Dynamics'),
        ('GameNodeSetCollisionMask', 'Set Collision Mask'),
        ('GameNodeSetRigidBody', 'Set Rigid Body'),
    )),
    ('logic', 'Logic & Math', (
        ('GameNodeCompare', 'Compare'),
        ('GameNodeLogicGate', 'Logic Gate'),
        ('GameNodeLogicNot', 'Logic Not'),
        ('GameNodeIsNone', 'Is None'),
        ('GameNodeThreshold', 'Threshold'),
        ('GameNodeWithinRange', 'Within Range'),
        ('GameNodeClamp', 'Clamp'),
        ('GameNodeClampValue', 'Clamp Value'),
        ('GameNodeWrap', 'Wrap'),
        ('GameNodeRound', 'Round'),
        ('GameNodeSign', 'Sign'),
        ('GameNodeFormula', 'Formula'),
        ('GameNodeMapRange', 'Map Range'),
        ('GameNodeInterpolate', 'Interpolate'),
        ('GameNodeDistance', 'Distance'),
        ('GameNodeVectorAngle', 'Vector Angle'),
        ('GameNodeVectorDistance', 'Vector Distance'),
        ('GameNodeVectorProject', 'Vector Project'),
        ('GameNodeVectorReflect', 'Vector Reflect'),
        ('GameNodeVectorRotateToward', 'Vector Rotate Toward'),
    )),
    ('value', 'Value & Data', (
        ('GameNodeColorConst', 'Color'),
        ('GameNodeColorFromVector', 'Color From Vector'),
        ('GameNodeColorLerp', 'Color Lerp'),
        ('GameNodeColorMix', 'Color Mix'),
        ('GameNodeColorToRGBA', 'Color To RGBA'),
        ('GameNodeRGBAToColor', 'RGBA To Color'),
        ('GameNodeHSVToRGB', 'HSV To RGB'),
        ('GameNodeRGBToHSV', 'RGB To HSV'),
        ('GameNodeVectorFromColor', 'Vector From Color'),
        ('GameNodeStringConst', 'String'),
        ('GameNodeStringConcat', 'String Join'),
        ('GameNodeStringFormat', 'String Format'),
        ('GameNodeStringFind', 'String Find'),
        ('GameNodeStringLength', 'String Length'),
        ('GameNodeStringReplace', 'String Replace'),
        ('GameNodeStringSubstr', 'String Substring'),
        ('GameNodeStringToLower', 'String To Lower'),
        ('GameNodeStringToUpper', 'String To Upper'),
        ('GameNodeStringCompare', 'String Compare'),
        ('GameNodeStringToNumber', 'String To Number'),
        ('GameNodeNumberToString', 'Number To String'),
        ('GameNodeListAdd', 'List Add'),
        ('GameNodeListClear', 'List Clear'),
        ('GameNodeListContains', 'List Contains'),
        ('GameNodeListGet', 'List Get'),
        ('GameNodeListSet', 'List Set'),
        ('GameNodeListRemove', 'List Remove'),
        ('GameNodeListLength', 'List Length'),
        ('GameNodeStringSplit', 'String Split'),
        ('GameNodeStringJoin', 'String Join'),
        ('GameNodeStringRegexMatch', 'String Regex Match'),
        ('GameNodeStringRegexReplace', 'String Regex Replace'),
        ('GameNodeStringTrim', 'String Trim'),
        ('GameNodeStringPad', 'String Pad'),
        ('GameNodeStringLengthChars', 'String Length (Chars)'),
        ('GameNodeStringPadChars', 'String Pad (Chars)'),
        ('GameNodeListSort', 'List Sort'),
        ('GameNodeListFilter', 'List Filter'),
        ('GameNodeListMap', 'List Map'),
        ('GameNodeListFind', 'List Find'),
        ('GameNodeListSlice', 'List Slice'),
        ('GameNodeListConcat', 'List Concat'),
        ('GameNodeRandomFloat', 'Random Float'),
        ('GameNodeRandomValue', 'Random Value'),
        ('GameNodeAssetGet', 'Asset Get'),
        ('GameNodeAssetSet', 'Asset Set'),
        ('GameNodeObjectPropertyOperator', 'Object Property Operator'),
    )),
    ('uiscene', 'UI & Scene', (
        ('GameNodeSetActiveCamera', 'Set Active Camera'),
        ('GameNodeSetFullScreen', 'Set Full Screen'),
        ('GameNodeSetResolution', 'Set Resolution'),
        ('GameNodeSetFontText', 'Set Font Text'),
        ('GameNodeSetLayerWeight', 'Set Layer Weight'),
        ('GameNodeLoadScene', 'Load Scene'),
        ('GameNodeSuspendScene', 'Suspend Scene'),
        ('GameNodeResumeScene', 'Resume Scene'),
        ('GameNodeStartSound', 'Start Sound'),
        ('GameNodePlaySoundByName', 'Play Sound By Name'),
        ('GameNodeStopSound', 'Stop Sound'),
        ('GameNodeSetSoundVolume', 'Set Sound Volume'),
        ('GameNodeSetSoundPitch', 'Set Sound Pitch'),
        ('GameNodeSet3DListener', 'Set 3D Listener'),
        ('GameNodeSetAudioAttenuation', 'Set Audio Attenuation'),
        ('GameNodeSetAudioDoppler', 'Set Audio Doppler'),
        ('GameNodeAudioZone', 'Audio Zone'),
        ('GameNodeSetAudioBusVolume', 'Set Audio Bus Volume'),
        ('GameNodePlaySound3D', 'Play Sound 3D'),
    )),
    ('ui', 'UI Widgets', (
        ('GameNodeCreateUICanvas', 'Create Canvas'),
        ('GameNodeCreateUILayout', 'Create Layout'),
        ('GameNodeCreateUILabel', 'Create Label'),
        ('GameNodeCreateUIImage', 'Create Image'),
        ('GameNodeCreateUIButton', 'Create Button'),
        ('GameNodeCreateUISliderWidget', 'Create Slider'),
        ('GameNodeMoveWidget', 'Move Widget'),
        ('GameNodeSetUIWidgetAttr', 'Set Widget Attribute'),
        ('GameNodeGetUIWidgetAttr', 'Get Widget Attribute'),
    )),
    ('file', 'Files', (
        ('GameNodeSaveGame', 'Save Game'),
        ('GameNodeLoadGame', 'Load Game'),
        ('GameNodeReadFile', 'Read File'),
        ('GameNodeWriteFile', 'Write File'),
        ('GameNodeFileExists', 'File Exists'),
        ('GameNodeDeleteFile', 'Delete File'),
        ('GameNodeJSONStringify', 'JSON Stringify'),
        ('GameNodeJSONParse', 'JSON Parse'),
    )),
    ('utility', 'Utility', (
        ('GameNodePrint', 'Print'),
        ('GameNodeFrameTime', 'Frame Time'),
        ('GameNodeSetTimeScale', 'Set Time Scale'),
        ('GameNodeQuitGame', 'Quit Game'),
        ('GameNodeRestartGame', 'Restart Game'),
        ('GameNodeSendMessage', 'Send Message'),
    )),
)


# The two B4.5 domains use the explicit preference names requested for them;
# the older domains keep their historical `palette_<key>_open` properties.
_EXPANDED_PROPS = {
    'ui': 'palette_ui_expanded',
    'file': 'palette_file_expanded',
}


@ui_panel
class LOGIC_NODES_PT_native_palette(bpy.types.Panel):
    bl_label = 'Node Palette'
    bl_space_type = 'NODE_EDITOR'
    bl_region_type = 'UI'
    bl_category = 'Game Nodes'

    @classmethod
    def poll(cls, context):
        space = getattr(context, 'space_data', None)
        if getattr(space, 'edit_tree', None) is None:
            return False
        return space.tree_type == LogicNodeTree.bl_idname

    def draw(self, context):
        prefs = preferences()
        layout = self.layout
        for key, label, nodes in DOMAINS:
            prop = _EXPANDED_PROPS.get(key) or f'palette_{key}_open'
            is_open = getattr(prefs, prop, True)
            row = layout.row(align=True)
            row.prop(
                prefs, prop,
                text=f'{label} ({len(nodes)})',
                emboss=False,
                icon='TRIA_DOWN' if is_open else 'TRIA_RIGHT',
            )
            if not is_open:
                continue
            column = layout.column(align=True)
            for bl_idname, node_label in nodes:
                insertNode(column, bl_idname, node_label)
