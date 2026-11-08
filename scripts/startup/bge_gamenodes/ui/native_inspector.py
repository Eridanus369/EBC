"""Native graph data inspector.

Shows the *exported* GraphIR of the selected node instead of re-reading the
Python node: the port types, configuration values and constant injections
displayed here are exactly what the IR exporter (generator/ir_export.py)
produces for the runtime. Diagnostics returned by the exporter are attributed
back to this node and, where possible, to a specific configuration key.
"""

import re

import bpy
from bpy.props import StringProperty
from bpy.types import Operator

from .interface import ui_panel
from ..editor.nodetree import GameNodeTree
from ..editor.nodes.native_metadata_gen import NATIVE_METADATA_GEN
from ..generator.ir_export import _load_assets, export_graph_ir
from ..ops.operator import operator

_NODE_NAME_RE = re.compile(r"Node '([^']+)'")
_CONST_PREFIX = '__const_'
_MAX_TREE_WARNINGS = 30


_cache = {}


# Port contract of the B4.1 / B4.2 native families, copied from the C++
# `KX_FunctionSpec` tables (KX_FunctionNodesB4.cpp / KX_FunctionNodesB4File.cpp).
# Entry format: `kind` is 'trigger' or 'value', `value_tag` is empty for
# triggers. `ports_from_ir` marks JSON nodes whose schema fields add ports at
# runtime; `dynamic_inputs` marks inputs whose tag follows the IR.
NATIVE_METADATA = {
    'CreateUICanvas': {
        'inputs': [('Condition', 'trigger', '')],
        'outputs': [('Done', 'trigger', ''), ('Canvas', 'value', 'Int')],
        'config_keys': ['scale_mode', 'design_w', 'design_h'],
    },
    'CreateUILayout': {
        'inputs': [
            ('Condition', 'trigger', ''),
            ('Parent', 'value', 'Int'),
            ('Relative Position', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Relative Size', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Angle', 'value', 'Float'),
            ('Color', 'value', 'Color'),
            ('Border Width', 'value', 'Int'),
            ('Border Color', 'value', 'Color'),
            ('Inverted', 'value', 'Bool'),
            ('Spacing', 'value', 'Int'),
            ('Rows', 'value', 'Int'),
            ('Columns', 'value', 'Int'),
            ('Starting Angle', 'value', 'Float'),
            ('Radius', 'value', 'Float'),
        ],
        'outputs': [('Done', 'trigger', ''), ('Layout', 'value', 'Int')],
        'config_keys': ['layout_type', 'halign', 'valign'],
    },
    'CreateUILabel': {
        'inputs': [
            ('Condition', 'trigger', ''),
            ('Parent', 'value', 'Int'),
            ('Relative Position', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Angle', 'value', 'Float'),
            ('Text', 'value', 'String'),
            ('', 'value', 'String'),
            ('Font Size', 'value', 'Int'),
            ('Line Height', 'value', 'Float'),
            ('Font Color', 'value', 'Color'),
            ('Use Shadow', 'value', 'Bool'),
            ('Shadow Offset', 'value', 'Vector'),
            ('Shadow Color', 'value', 'Color'),
        ],
        'outputs': [('Done', 'trigger', ''), ('Label', 'value', 'Int')],
        'config_keys': ['halign', 'valign', 'text_halign', 'text_valign', 'wrap', 'font'],
    },
    'CreateUIImage': {
        'inputs': [
            ('Condition', 'trigger', ''),
            ('Parent', 'value', 'Int'),
            ('Relative Position', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Relative Size', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Angle', 'value', 'Float'),
            ('', 'value', 'String'),
        ],
        'outputs': [('Done', 'trigger', ''), ('Image', 'value', 'Int')],
        'config_keys': ['tint_r', 'tint_g', 'tint_b', 'tint_a', 'preserve_aspect', 'texture'],
    },
    'CreateUIButton': {
        'inputs': [
            ('Condition', 'trigger', ''),
            ('Parent', 'value', 'Int'),
            ('Relative Position', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Relative Size', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Angle', 'value', 'Float'),
            ('Color', 'value', 'Color'),
            ('Hover Color', 'value', 'Color'),
            ('Border Width', 'value', 'Int'),
            ('Border Color', 'value', 'Color'),
            ('Text', 'value', 'String'),
            ('Text Position', 'value', 'Vector'),
            ('', 'value', 'String'),
            ('Font Size', 'value', 'Int'),
            ('Line Height', 'value', 'Float'),
            ('Font Color', 'value', 'Color'),
        ],
        'outputs': [
            ('Done', 'trigger', ''),
            ('Button', 'value', 'Int'),
            ('On Click', 'trigger', ''),
            ('On Hover', 'trigger', ''),
            ('On Release', 'trigger', ''),
        ],
        'config_keys': ['halign', 'valign', 'text_halign', 'text_valign', 'font'],
    },
    'CreateUISliderWidget': {
        'inputs': [
            ('Condition', 'trigger', ''),
            ('Parent', 'value', 'Int'),
            ('Relative Position', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Relative Size', 'value', 'Bool'),
            ('', 'value', 'Vector'),
            ('Angle', 'value', 'Float'),
            ('Bar Width', 'value', 'Float'),
            ('Bar Color', 'value', 'Color'),
            ('Bar Hover Color', 'value', 'Color'),
            ('Bar Texture', 'value', 'String'),
            ('Border Width', 'value', 'Int'),
            ('Border Color', 'value', 'Color'),
            ('Knob Size', 'value', 'Float'),
            ('Knob Color', 'value', 'Color'),
            ('Knob Hover Color', 'value', 'Color'),
            ('Knob Texture', 'value', 'String'),
            ('Initial Value', 'value', 'Float'),
            ('Steps', 'value', 'Int'),
            ('Bar Control', 'value', 'Bool'),
        ],
        'outputs': [
            ('Done', 'trigger', ''),
            ('Slider', 'value', 'Int'),
            ('Slider Value', 'value', 'Float'),
            ('Knob Position', 'value', 'Vector'),
        ],
        'config_keys': ['orientation'],
    },
    'MoveWidget': {
        'inputs': [('Condition', 'trigger', ''), ('Widget', 'value', 'Int')],
        'outputs': [('Done', 'trigger', '')],
        'config_keys': ['mode'],
    },
    'SetUIWidgetAttr': {
        'inputs': [
            ('Condition', 'trigger', ''),
            ('Widget', 'value', 'Int'),
            ('Value', 'value', 'Float'),
        ],
        'outputs': [('Done', 'trigger', '')],
        'config_keys': ['widget_attr'],
        'dynamic_inputs': [2],
    },
    'GetUIWidgetAttr': {
        'inputs': [('Widget', 'value', 'Int')],
        'outputs': [
            ('Visibility', 'value', 'Bool'),
            ('Color', 'value', 'Color'),
            ('Alpha', 'value', 'Float'),
            ('Vec2', 'value', 'Vector'),
            ('String', 'value', 'String'),
            ('Int', 'value', 'Int'),
            ('Float', 'value', 'Float'),
            ('Font', 'value', 'String'),
            ('Image', 'value', 'String'),
        ],
        'config_keys': ['widget_attr'],
    },
    'SaveGame': {
        'inputs': [('OnEnter', 'trigger', ''), ('Slot', 'value', 'String')],
        'outputs': [('OnSuccess', 'trigger', ''), ('OnFail', 'trigger', '')],
        'config_keys': ['format'],
    },
    'LoadGame': {
        'inputs': [('OnEnter', 'trigger', ''), ('Slot', 'value', 'String')],
        'outputs': [('OnSuccess', 'trigger', ''), ('OnFail', 'trigger', '')],
        'config_keys': ['format'],
    },
    'ReadFile': {
        'inputs': [('OnEnter', 'trigger', ''), ('Path', 'value', 'String')],
        'outputs': [
            ('Content', 'value', 'String'),
            ('OnSuccess', 'trigger', ''),
            ('OnFail', 'trigger', ''),
        ],
        'config_keys': ['binary'],
    },
    'WriteFile': {
        'inputs': [
            ('OnEnter', 'trigger', ''),
            ('Path', 'value', 'String'),
            ('Content', 'value', 'String'),
        ],
        'outputs': [('OnSuccess', 'trigger', ''), ('OnFail', 'trigger', '')],
        'config_keys': ['binary'],
    },
    'FileExists': {
        'inputs': [('OnEnter', 'trigger', ''), ('Path', 'value', 'String')],
        'outputs': [('Exists', 'value', 'Bool')],
        'config_keys': [],
    },
    'DeleteFile': {
        'inputs': [('OnEnter', 'trigger', ''), ('Path', 'value', 'String')],
        'outputs': [('OnSuccess', 'trigger', ''), ('OnFail', 'trigger', '')],
        'config_keys': [],
    },
    'JSONStringify': {
        'inputs': [('OnEnter', 'trigger', '')],
        'outputs': [
            ('Result', 'value', 'String'),
            ('OnSuccess', 'trigger', ''),
            ('OnFail', 'trigger', ''),
        ],
        'config_keys': ['schema'],
        'ports_from_ir': True,
    },
    'JSONParse': {
        'inputs': [('OnEnter', 'trigger', ''), ('Source', 'value', 'String')],
        'outputs': [('OnSuccess', 'trigger', ''), ('OnFail', 'trigger', '')],
        'config_keys': ['schema'],
        'ports_from_ir': True,
    },
}

# The generated table covers every native runtime node; the hand written B4
# entries above stay authoritative for their little extras (``ports_from_ir``
# notes). Merge without overwriting them.
for _runtime_id, _meta in NATIVE_METADATA_GEN.items():
    NATIVE_METADATA.setdefault(_runtime_id, _meta)
del _runtime_id, _meta


def registered_native_ids():
    """runtime_type_id of every registered, non deprecated node class."""
    from ..editor.nodes.node import _nodes

    ids = set()
    for cls in _nodes:
        runtime_id = getattr(cls, 'runtime_type_id', '')
        if runtime_id and not getattr(cls, 'deprecated', False):
            ids.add(runtime_id)
    return ids


def audit_metadata():
    """Self-check: every metadata entry has a node and matches its port table.

    Returns a report whose `missing`, `unknown`, `port_mismatch` and
    `config_mismatch` lists must all be empty. The generated table covers the
    runtime side; the port/config comparison runs against every editor table
    that declares the node (native table modules, not the legacy catalog, which
    ``port_contract.py`` checks separately).
    """
    from ..generator import native_metadata_audit
    from ..generator import port_contract

    runtime_ids = set(port_contract.load_cpp_specs())
    runtime_ids.update(port_contract.load_signature_specs())
    return native_metadata_audit.audit(
        NATIVE_METADATA,
        registered_native_ids(),
        native_metadata_audit.load_editor_tables(),
        port_contract.load_dynamic_runtime_ids(),
        runtime_ids,
    )


def _tree_signature(tree):
    parts = []
    for node in tree.nodes:
        cfg = ''
        exporter = getattr(node, 'export_config', None)
        if callable(exporter):
            try:
                cfg = repr(exporter())
            except Exception:
                cfg = '?'
        parts.append((node.name, node.bl_idname, bool(node.mute), cfg))
    for link in tree.links:
        parts.append((
            link.from_socket.node.name,
            getattr(link.from_socket, 'identifier', ''),
            link.to_socket.node.name,
            getattr(link.to_socket, 'identifier', ''),
        ))
    return hash(tuple(parts))


def _tree_ir(tree):
    key = tree.as_pointer()
    signature = _tree_signature(tree)
    entry = _cache.get(key)
    if entry is not None and entry[0] == signature:
        return entry[1], entry[2]
    ir, warnings = export_graph_ir(tree, _load_assets())
    _cache[key] = (signature, ir, warnings)
    return ir, warnings


def _index_by_name(ir):
    result = {}
    for index, node in enumerate(ir['nodes']):
        result[node['debug_name']] = index
    return result


def _build_link_maps(ir):
    input_source = {}
    output_targets = {}
    for connection in ir['connections']:
        input_source[(connection['to_node'], connection['to_port'])] = (
            connection['from_node'], connection['from_port'])
        output_targets.setdefault(
            (connection['from_node'], connection['from_port']), []).append(
            (connection['to_node'], connection['to_port']))
    return input_source, output_targets


def _port_name(port_list, index):
    if 0 <= index < len(port_list):
        return port_list[index]['name']
    return f'#{index}'


def _source_text(ir, source_node, source_port, source_lists):
    node = ir['nodes'][source_node]
    if node['debug_name'].startswith(_CONST_PREFIX):
        value = node.get('config', {}).get('value', '')
        tag = ''
        ports = node.get('outputs', [])
        if ports:
            tag = ports[0].get('value_tag', '')
        return f'constant {tag} = {value}'.strip()
    port_name = _port_name(source_lists, source_port)
    return f"{node['debug_name']} ({node['type_id']}).{port_name}"


def _node_warnings(warnings, node_name, config):
    own = []
    keyed = {}
    for message in warnings:
        if f"Node '{node_name}'" in message:
            own.append(message)
        for key, value in config.items():
            if value is None or value == '':
                continue
            text = str(value)
            if len(text) >= 2 and text in message:
                keyed.setdefault(key, []).append(message)
    return own, keyed


def _warning_owner(message, tree):
    match = _NODE_NAME_RE.search(message)
    if match and match.group(1) in tree.nodes:
        return match.group(1)
    for node in tree.nodes:
        exporter = getattr(node, 'export_config', None)
        if not callable(exporter):
            continue
        try:
            config = exporter() or {}
        except Exception:
            continue
        for value in config.values():
            if value is None or value == '':
                continue
            if str(value) in message and len(str(value)) >= 2:
                return node.name
    return ''


def _short(message, limit=90):
    text = message.strip()
    if len(text) > limit:
        text = text[:limit - 1] + '~'
    return text


@operator
class LOGIC_NODES_OT_inspector_select_node(Operator):
    bl_idname = 'game_nodes.inspector_select_node'
    bl_label = 'Select Node'
    bl_description = 'Select the node this diagnostic belongs to'
    bl_options = {'REGISTER', 'UNDO'}

    node_name: StringProperty()

    @classmethod
    def poll(cls, context):
        space = getattr(context, 'space_data', None)
        return bool(getattr(space, 'edit_tree', None))

    def execute(self, context):
        tree = context.space_data.edit_tree
        target = tree.nodes.get(self.node_name)
        if target is None:
            return {'CANCELLED'}
        for node in tree.nodes:
            node.select = False
        target.select = True
        tree.nodes.active = target
        return {'FINISHED'}


@ui_panel
class LOGIC_NODES_PT_native_inspector(bpy.types.Panel):
    bl_label = 'Native Graph Inspector'
    bl_space_type = 'NODE_EDITOR'
    bl_region_type = 'UI'
    bl_category = 'Game Nodes'

    @classmethod
    def poll(cls, context):
        space = getattr(context, 'space_data', None)
        if getattr(space, 'edit_tree', None) is None:
            return False
        return space.tree_type == GameNodeTree.bl_idname

    def draw_node_header(self, layout, node, ir_node):
        box = layout.box()
        column = box.column(align=True)
        row = column.row(align=True)
        row.label(text=node.name, icon='NODE')
        if node.mute:
            row.label(text='MUTED', icon='CANCEL')
        if ir_node is None:
            column.label(text='Not exported to IR', icon='ERROR')
            if node.mute:
                column.label(text='Node is muted')
            else:
                runtime_id = getattr(node, 'runtime_type_id', '')
                if not runtime_id:
                    row = column.row()
                    row.alert = True
                    row.label(text='Runtime: (unknown)', icon='ERROR')
                    column.label(text='No native runtime type', icon='INFO')
                elif runtime_id not in registered_native_ids():
                    row = column.row()
                    row.alert = True
                    row.label(text='Runtime: (unknown)', icon='ERROR')
                    column.label(text=f'Unregistered runtime type {runtime_id}', icon='INFO')
                else:
                    column.label(text='Export failed, see diagnostics', icon='ERROR')
            return
        if ir_node['type_id'] not in registered_native_ids():
            row = column.row()
            row.alert = True
            row.label(text='Runtime: (unknown)', icon='ERROR')
            column.label(text=f'Type id: {ir_node["type_id"]}')
        else:
            column.label(text=f"Runtime: {ir_node['type_id']}")
        column.label(text=f"Node type: {node.bl_idname}")

    def draw_ports(self, layout, ir, ir_node, node_index, input_source, output_targets):
        inputs_box = layout.box()
        inputs_box.label(text='Inputs', icon='IMPORT')
        for port_index, port in enumerate(ir_node['inputs']):
            column = inputs_box.column(align=True)
            row = column.row(align=True)
            row.label(text=port['name'])
            row.label(text=port['value_tag'])
            source = input_source.get((node_index, port_index))
            if source is None:
                column.label(text='unconnected (no constant)')
            else:
                text = _source_text(ir, source[0], source[1],
                                    ir['nodes'][source[0]]['outputs'])
                column.label(text=f'from {text}', icon='LINKED')

        outputs_box = layout.box()
        outputs_box.label(text='Outputs', icon='EXPORT')
        for port_index, port in enumerate(ir_node['outputs']):
            column = outputs_box.column(align=True)
            row = column.row(align=True)
            row.label(text=port['name'])
            row.label(text=port['value_tag'])
            targets = output_targets.get((node_index, port_index), [])
            if not targets:
                column.label(text='not consumed')
            for target_node, target_port in targets:
                text = _source_text(ir, target_node, target_port,
                                    ir['nodes'][target_node]['inputs'])
                column.label(text=f'to {text}', icon='LINKED')

    def draw_config(self, layout, ir_node, keyed_warnings):
        box = layout.box()
        box.label(text='IR Config', icon='PREFERENCES')
        config = ir_node.get('config') or {}
        if not config:
            box.label(text='(empty)')
            return
        for key, value in config.items():
            column = box.column(align=True)
            row = column.row(align=True)
            icon = 'ERROR' if key in keyed_warnings else 'DOT'
            row.label(text=key, icon=icon)
            row.label(text=str(value))
            for message in keyed_warnings.get(key, [])[:2]:
                column.label(text=_short(message), icon='INFO')

    def draw_node_warnings(self, layout, own, keyed_warnings):
        if not own and not keyed_warnings:
            return
        box = layout.box()
        count = len(own) + sum(len(items) for items in keyed_warnings.values())
        box.label(text=f'Diagnostics ({count})', icon='ERROR')
        for message in own:
            box.label(text=_short(message), icon='ERROR')

    def draw_tree_warnings(self, layout, context, warnings):
        box = layout.box()
        box.label(text=f'Tree Diagnostics ({len(warnings)})', icon='INFO')
        if not warnings:
            box.label(text='No export warnings')
            return
        for message in warnings[:_MAX_TREE_WARNINGS]:
            owner = _warning_owner(message, context.space_data.edit_tree)
            if owner:
                op = box.operator('game_nodes.inspector_select_node',
                                  text=_short(message), icon='ERROR')
                op.node_name = owner
            else:
                box.label(text=_short(message), icon='INFO')
        if len(warnings) > _MAX_TREE_WARNINGS:
            box.label(text=f'... {len(warnings) - _MAX_TREE_WARNINGS} more')

    def draw(self, context):
        layout = self.layout
        tree = context.space_data.edit_tree
        node = getattr(context, 'active_node', None) or tree.nodes.active
        if node is None:
            layout.label(text='Select a node to inspect', icon='INFO')
            self.draw_tree_warnings(layout, context, _tree_ir(tree)[1])
            return

        ir, warnings = _tree_ir(tree)
        index_by_name = _index_by_name(ir)
        node_index = index_by_name.get(node.name)
        ir_node = ir['nodes'][node_index] if node_index is not None else None

        self.draw_node_header(layout, node, ir_node)
        if ir_node is not None:
            input_source, output_targets = _build_link_maps(ir)
            config = ir_node.get('config') or {}
            own, keyed = _node_warnings(warnings, node.name, config)
            self.draw_config(layout, ir_node, keyed)
            self.draw_node_warnings(layout, own, keyed)
            self.draw_ports(layout, ir, ir_node, node_index, input_source, output_targets)
        else:
            exporter = getattr(node, 'export_config', None)
            config = {}
            if callable(exporter):
                try:
                    config = exporter() or {}
                except Exception:
                    config = {}
            own, keyed = _node_warnings(warnings, node.name, config)
            self.draw_config(layout, {'config': config}, keyed)
            self.draw_node_warnings(layout, own, keyed)

        self.draw_tree_warnings(layout, context, warnings)
