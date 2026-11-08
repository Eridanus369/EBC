"""Editor to runtime bridge: node trees to GraphIR and scene manifest.

The C++ node registry owns the port contract. This module only walks the
Blender node tree and serializes what the compiler needs:

* one GraphIR per mounted node tree (`nodes` + `connections`),
* one SceneManifest per .blend (assets, graphs and object mounts).

The manifest is written to a Scene custom property so the engine can read it
without touching bpy data at runtime.
"""

import json

import bpy
from bpy.types import NodeReroute

from ..editor.nodetree import GameNodeTree
from ..editor.sockets.socket import (
    SOCKET_TYPE_BOOL,
    SOCKET_TYPE_COLOR,
    SOCKET_TYPE_COLLECTION,
    SOCKET_TYPE_CONDITION,
    SOCKET_TYPE_DATABLOCK,
    SOCKET_TYPE_FLOAT,
    SOCKET_TYPE_GENERIC,
    SOCKET_TYPE_INT,
    SOCKET_TYPE_LIST,
    SOCKET_TYPE_OBJECT,
    SOCKET_TYPE_STRING,
    SOCKET_TYPE_TEXT,
    SOCKET_TYPE_VALUE,
    SOCKET_TYPE_VECTOR,
)
from ..utilities import warn

MANIFEST_PROPERTY = 'game_graph_manifest'
ASSET_TEXT_PREFIX = 'GameAsset_'

_VALUE_TAGS = {
    SOCKET_TYPE_BOOL: 'Bool',
    SOCKET_TYPE_INT: 'Int',
    SOCKET_TYPE_FLOAT: 'Float',
    SOCKET_TYPE_VECTOR: 'Vector',
    SOCKET_TYPE_OBJECT: 'Object',
    SOCKET_TYPE_STRING: 'String',
    SOCKET_TYPE_TEXT: 'String',
    SOCKET_TYPE_COLOR: 'Color',
    SOCKET_TYPE_LIST: 'List',
    SOCKET_TYPE_COLLECTION: 'List',
    SOCKET_TYPE_DATABLOCK: 'DataBlock',
}

_ASSET_FIELD_TYPES = ('Bool', 'Int', 'Float', 'Vector')

_ASSET_ZERO = {
    'Bool': False,
    'Int': 0,
    'Float': 0.0,
    'Vector': [0.0, 0.0, 0.0],
}


def _socket_is_trigger(socket):
    return getattr(socket, 'nl_type', SOCKET_TYPE_VALUE) == SOCKET_TYPE_CONDITION


def _socket_value_tag(node, socket, is_input, port_name, ctx):
    nl_type = getattr(socket, 'nl_type', SOCKET_TYPE_GENERIC)
    tag = _VALUE_TAGS.get(nl_type)
    if tag is not None:
        return tag
    resolver = getattr(node, 'resolve_native_port', None)
    if resolver is not None:
        return resolver(port_name, is_input, ctx)
    return None


def _find_socket(collection, identifier, port_name, used):
    for index, socket in enumerate(collection):
        if index in used:
            continue
        if identifier and getattr(socket, 'identifier', '') == identifier:
            used.add(index)
            return index
    for index, socket in enumerate(collection):
        if index in used:
            continue
        if socket.name == port_name:
            used.add(index)
            return index
    return None


def _resolve_reroute_source(socket):
    while isinstance(socket.node, NodeReroute):
        reroute = socket.node
        if not reroute.inputs[0].links:
            return None
        socket = reroute.inputs[0].links[0].from_socket
    return socket


def _resolve_reroute_target(socket):
    while isinstance(socket.node, NodeReroute):
        reroute = socket.node
        if not reroute.outputs[0].links:
            return None
        socket = reroute.outputs[0].links[0].to_socket
    return socket


class ExportContext:
    """Collects IR nodes, connections and diagnostics for one node tree."""

    def __init__(self, assets=None):
        self.nodes = []
        self.connections = []
        self.node_index = {}
        self.port_sockets = {}
        self.port_index = {}
        self.assets = assets or {}
        self.warnings = []
        self.connected_inputs = set()
        self._tag_guard = set()

    def linked_value_tag(self, socket):
        """Value tag feeding #socket, following reroute nodes.

        Polymorphic runtime ports use this to keep the connected type instead
        of narrowing it to the table default.
        """
        if socket is None or not getattr(socket, 'links', None):
            return None
        source = _resolve_reroute_source(socket.links[0].from_socket)
        if source is None:
            return None
        key = (source.node.name, getattr(source, 'identifier', source.name))
        if key in self._tag_guard:
            return None
        self._tag_guard.add(key)
        try:
            return _socket_value_tag(source.node, source, False, source.name, self)
        finally:
            self._tag_guard.discard(key)

    def warn(self, message):
        self.warnings.append(message)
        warn(message)

    def asset_field_type(self, asset_ref, field_name):
        asset = self.assets.get(asset_ref)
        if asset is None:
            self.warn(f"Asset '{asset_ref}' is not defined by any {ASSET_TEXT_PREFIX}* text")
            return None
        for field in asset['fields']:
            if field['name'] == field_name:
                return field['type']
        self.warn(f"Asset '{asset_ref}' has no field '{field_name}'")
        return None

    def add_node(self, node):
        runtime_id = getattr(node, 'runtime_type_id', '')
        if not runtime_id:
            self.warn(f"Node '{node.name}' ({node.bl_idname}) has no native runtime type")
            return -1

        inputs, input_slots = self._build_ports(node, node.native_inputs, True)
        outputs, output_slots = self._build_ports(node, node.native_outputs, False)
        if inputs is None or outputs is None:
            return -1

        index = len(self.nodes)
        self.nodes.append(
            {
                'type_id': runtime_id,
                'debug_name': node.name,
                'inputs': inputs,
                'outputs': outputs,
                'config': node.export_config() or {},
            }
        )
        self.node_index[node.name] = index
        for port_index, socket_index in enumerate(input_slots):
            self.port_sockets[(index, True, port_index)] = (node, socket_index)
            self.port_index[(node.name, True, socket_index)] = port_index
        for port_index, socket_index in enumerate(output_slots):
            self.port_sockets[(index, False, port_index)] = (node, socket_index)
            self.port_index[(node.name, False, socket_index)] = port_index
        return index

    def _build_ports(self, node, spec, is_input):
        if getattr(node, 'native_dynamic_ports', False):
            return self._dynamic_ports(node, is_input)
        collection = node.inputs if is_input else node.outputs
        ports = []
        slots = []
        used = set()
        for port_name, identifier in spec:
            socket_index = _find_socket(collection, identifier, port_name, used)
            if socket_index is None:
                self.warn(
                    f"Node '{node.name}' ({node.bl_idname}) declares native port "
                    f"'{port_name}' but no matching socket was found"
                )
                return None, None
            socket = collection[socket_index]
            if _socket_is_trigger(socket):
                port = {'name': port_name, 'kind': 'trigger', 'value_tag': 'None'}
            else:
                tag = _socket_value_tag(node, socket, is_input, port_name, self)
                if tag is None:
                    self.warn(
                        f"Node '{node.name}' ({node.bl_idname}) cannot resolve the "
                        f"runtime type of port '{port_name}'"
                    )
                    return None, None
                port = {'name': port_name, 'kind': 'value', 'value_tag': tag}
            ports.append(port)
            slots.append(socket_index)
        return ports, slots

    def _dynamic_ports(self, node, is_input):
        """Build ports from the enabled sockets, in declaration order.

        Nodes flagged `native_dynamic_ports` have a variable number of sockets
        (a string formatter, an execute chain, a math node with N inputs, ...).
        The C++ side reads the ports back from the IR, so names only have to
        be unique and deterministic.
        """
        collection = node.inputs if is_input else node.outputs
        ports = []
        slots = []
        seen = {}
        for socket_index, socket in enumerate(collection):
            if not getattr(socket, 'enabled', True):
                continue
            base = getattr(socket, 'identifier', '') or socket.name or 'Port'
            count = seen.get(base, 0)
            seen[base] = count + 1
            port_name = base if count == 0 else f'{base}_{count}'
            if _socket_is_trigger(socket):
                ports.append({'name': port_name, 'kind': 'trigger', 'value_tag': 'None'})
            else:
                tag = _socket_value_tag(node, socket, is_input, port_name, self)
                if tag is None:
                    self.warn(
                        f"Node '{node.name}' ({node.bl_idname}) cannot resolve the "
                        f"runtime type of dynamic port '{port_name}'"
                    )
                    return None, None
                ports.append({'name': port_name, 'kind': 'value', 'value_tag': tag})
            slots.append(socket_index)
        return ports, slots

    def add_connection(self, from_socket, to_socket):
        from_socket = _resolve_reroute_source(from_socket)
        to_socket = _resolve_reroute_target(to_socket)
        if from_socket is None or to_socket is None:
            return
        from_node = from_socket.node
        to_node = to_socket.node
        if from_node.name not in self.node_index or to_node.name not in self.node_index:
            return

        from_port = self._port_index_of(from_node, from_socket, False)
        to_port = self._port_index_of(to_node, to_socket, True)
        if from_port is None or to_port is None:
            return

        if _socket_is_trigger(from_socket) != _socket_is_trigger(to_socket):
            self.warn(
                f"Ignoring connection between '{from_node.name}' and '{to_node.name}': "
                "a trigger socket cannot drive a value socket or vice versa"
            )
            return

        self.connections.append(
            {
                'from_node': self.node_index[from_node.name],
                'from_port': from_port,
                'to_node': self.node_index[to_node.name],
                'to_port': to_port,
            }
        )
        self.connected_inputs.add((self.node_index[to_node.name], to_port))

    def _port_index_of(self, node, socket, is_input):
        collection = node.inputs if is_input else node.outputs
        for socket_index, candidate in enumerate(collection):
            if candidate == socket:
                return self.port_index.get((node.name, is_input, socket_index))
        return None

    def materialize_constants(self):
        """Feed unconnected value inputs from their editor default value."""
        for (index, is_input, port_index), (node, socket_index) in self.port_sockets.items():
            if not is_input or (index, port_index) in self.connected_inputs:
                continue
            socket = node.inputs[socket_index]
            if _socket_is_trigger(socket):
                continue
            tag = _socket_value_tag(node, socket, True, self.nodes[index]['inputs'][port_index]['name'], self)
            if tag not in _CONSTANT_NODE_TYPES:
                continue
            value = getattr(socket, 'default_value', None)
            if value is None:
                continue
            if not _constant_is_usable(tag, value):
                # Generic sockets without a usable default stay unset.
                continue
            constant_index = len(self.nodes)
            self.nodes.append(
                {
                    'type_id': _CONSTANT_NODE_TYPES[tag],
                    'debug_name': f"__const_{node.name}_{port_index}",
                    'inputs': [],
                    'outputs': [{'name': 'Value', 'kind': 'value', 'value_tag': tag}],
                    'config': {'value': _format_constant(value, tag)},
                }
            )
            self.connections.append(
                {
                    'from_node': constant_index,
                    'from_port': 0,
                    'to_node': index,
                    'to_port': port_index,
                }
            )


_CONSTANT_NODE_TYPES = {
    'Bool': 'ConstBool',
    'Float': 'ConstFloat',
    'Int': 'ConstInt',
    'String': 'ConstString',
    'Vector': 'ConstVector',
}


def _constant_is_usable(tag, value):
    if tag in ('Float', 'Int'):
        try:
            float(value) if tag == 'Float' else int(value)
        except (TypeError, ValueError):
            return False
        return True
    if tag == 'Vector':
        try:
            return len([float(component) for component in value]) >= 2
        except (TypeError, ValueError):
            return False
    return True


def _format_constant(value, tag):
    """Serialize a socket default into the config payload of its const node.

    ConstString reads the raw token, so strings are left to the JSON encoder.
    ConstVector expects a `[x, y, z]` array (see KX_IRConfigGetVector).
    """
    if tag == 'Bool':
        return 'true' if bool(value) else 'false'
    if tag == 'Int':
        return int(value)
    if tag == 'String':
        return str(value)
    if tag == 'Vector':
        components = [float(component) for component in value][:3]
        while len(components) < 3:
            components.append(0.0)
        return components
    return str(float(value))


def export_graph_ir(tree, assets=None):
    ctx = ExportContext(assets)
    for node in tree.nodes:
        if node.mute:
            continue
        if not getattr(node, 'runtime_type_id', ''):
            if getattr(node, 'nl_module', None):
                ctx.warn(f"Node '{node.name}' ({node.bl_idname}) has no native runtime type and was skipped")
            continue
        node.export_ir(ctx)
    for link in tree.links:
        ctx.add_connection(link.from_socket, link.to_socket)
    ctx.materialize_constants()
    return {
        'graph_id': tree.name,
        'nodes': ctx.nodes,
        'connections': ctx.connections,
    }, ctx.warnings


def _load_assets():
    assets = {}
    for text in bpy.data.texts:
        if not text.name.startswith(ASSET_TEXT_PREFIX):
            continue
        try:
            data = json.loads(text.as_string())
        except Exception as error:
            warn(f"Asset text '{text.name}' is not valid JSON: {error}")
            continue
        if not isinstance(data, dict):
            warn(f"Asset text '{text.name}' must contain a JSON object")
            continue
        asset_id = data.get('asset_id') or text.name[len(ASSET_TEXT_PREFIX):]
        asset_type = data.get('asset_type') or asset_id
        fields = []
        for field in data.get('fields', []):
            name = field.get('name')
            field_type = field.get('type', 'Float')
            if not name or field_type not in _ASSET_FIELD_TYPES:
                warn(f"Asset '{asset_id}' has an invalid field definition: {field}")
                continue
            default = field.get('default', _ASSET_ZERO[field_type])
            fields.append({'name': name, 'type': field_type, 'default': default})
        assets[asset_id] = {
            'asset_id': asset_id,
            'asset_type': asset_type,
            'fields': fields,
        }
    return assets


def iter_object_mounts(obj):
    """Return the node tree names mounted on `obj`.

    The single `game_node_tree` pointer is the primary mount shown in the
    Outliner and is emitted first. The remaining `logic_trees` entries follow
    without duplicates, so the manifest order is unchanged for files that
    migrate from a list-only layout (primary == list[0]).
    """
    names = []
    primary = getattr(obj, 'game_node_tree', None)
    if primary is not None:
        primary_name = getattr(primary, 'name', '')
        if primary_name:
            names.append(primary_name)
    trees = getattr(obj, 'logic_trees', None)
    if trees is not None:
        try:
            for ref in trees:
                name = getattr(ref, 'tree_name', '')
                if not name and getattr(ref, 'tree', None) is not None:
                    name = ref.tree.name
                if name and name not in names:
                    names.append(name)
        except AttributeError:
            pass
        if names:
            return names

    game = getattr(obj, 'game', None)
    components = getattr(game, 'components', None) if game is not None else None
    if components is None:
        return names
    for component in components:
        module = getattr(component, 'module', '')
        name = getattr(component, 'name', '')
        if not module.startswith('nl_') or not name:
            continue
        tree = bpy.data.node_groups.get(name)
        if tree is not None and tree.bl_idname == GameNodeTree.bl_idname:
            names.append(name)
    return names


def export_scene_manifest(scene=None):
    assets = _load_assets()
    graphs = []
    mounts = []
    exported_graphs = set()

    scenes = [scene] if scene is not None else list(bpy.data.scenes)
    for current_scene in scenes:
        for obj in current_scene.objects:
            for tree_name in iter_object_mounts(obj):
                tree = bpy.data.node_groups.get(tree_name)
                if tree is None or tree.bl_idname != GameNodeTree.bl_idname:
                    warn(f"Object '{obj.name}' references unknown node tree '{tree_name}'")
                    continue
                if tree_name not in exported_graphs:
                    exported_graphs.add(tree_name)
                    graph, _warnings = export_graph_ir(tree, assets)
                    graphs.append(graph)
                mount = {
                    'scene': current_scene.name,
                    'object': obj.name,
                    'graph_id': tree_name,
                    'priority': int(obj.get('game_graph_priority', 0)),
                }
                mounts.append(mount)

    return {
        'version': 1,
        'native_runtime': True,
        'assets': list(assets.values()),
        'graphs': graphs,
        'mounts': mounts,
    }


def _write_manifest_for_scene(scene):
    manifest = export_scene_manifest(scene)
    if not manifest['graphs'] or not manifest['mounts']:
        # An empty manifest must not look like "native runtime active"; drop
        # the property so KX_Scene::GraphTick stays a strict no-op.
        if MANIFEST_PROPERTY in scene:
            del scene[MANIFEST_PROPERTY]
        return None
    scene[MANIFEST_PROPERTY] = json.dumps(manifest)
    return manifest


def write_scene_manifest(scene=None):
    """Serialize the manifest into the scene custom property(s).

    The C++ runtime reads `scene["game_graph_manifest"]`. Every scene gets its own
    manifest (mounts and objects are scene local).
    """
    scenes = [scene] if scene is not None else list(bpy.data.scenes)
    manifest = None
    for current_scene in scenes:
        manifest = _write_manifest_for_scene(current_scene)
    return manifest
