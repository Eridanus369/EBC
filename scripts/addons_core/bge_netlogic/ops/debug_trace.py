"""Green box overlay for the nodes the player activated (P4.3 editor side).

The player writes `<blend_dir>/.debug_trace.json` at most every 100 ms through
#KX_DebugTrace (the same snapshot is throttled there). This module only reads
it and paints a green outline around the matching nodes.

Enabling is signalled with `<blend_dir>/.debug_trace.on`, the same disk-file
channel the hot reload uses -- no new IPC. Deleting the file turns the trace off
again on the player side.
"""

import json
import os

import bpy
from bpy.types import Operator

from ..utilities import notify, warn
from .operator import operator

TRACE_FILE = '.debug_trace.json'
ENABLE_FILE = '.debug_trace.on'

_draw_handle = None
_cache = {'mtime': None, 'names': frozenset()}


def _blend_dir():
    path = bpy.data.filepath
    if not path:
        return ''
    return os.path.dirname(path)


def _active_names(directory):
    """Node names from the snapshot, re-read only when the file changes."""
    path = os.path.join(directory, TRACE_FILE)
    try:
        mtime = os.path.getmtime(path)
    except OSError:
        return frozenset()
    if _cache['mtime'] == mtime:
        return _cache['names']
    names = set()
    try:
        with open(path, 'r', encoding='utf-8') as handle:
            data = json.load(handle)
        for entry in data.get('nodes', ()):
            name = entry.get('name')
            if name:
                names.add(name)
    except (OSError, ValueError):
        names = set()
    _cache['mtime'] = mtime
    _cache['names'] = frozenset(names)
    return _cache['names']


def _region_point(region, x, y):
    point = region.view2d.view_to_region(x, y, clip=False)
    return point


def _draw_boxes():
    context = bpy.context
    space = context.space_data
    if space is None or space.type != 'NODE_EDITOR' or space.node_tree is None:
        return
    region = context.region
    if region is None or region.type != 'WINDOW':
        return
    directory = _blend_dir()
    if not directory or not os.path.exists(os.path.join(directory, ENABLE_FILE)):
        return
    names = _active_names(directory)
    if not names:
        return

    import gpu
    from gpu_extras.batch import batch_for_shader

    try:
        shader = gpu.shader.from_builtin('UNIFORM_COLOR')
    except ValueError:
        shader = gpu.shader.from_builtin('2D_UNIFORM_COLOR')

    vertices = []
    for node in space.node_tree.nodes:
        if node.name not in names:
            continue
        x0, y0 = node.location
        x1 = x0 + node.dimensions.x
        y1 = y0 + node.dimensions.y
        corners = [
            _region_point(region, x0, y0),
            _region_point(region, x1, y0),
            _region_point(region, x1, y1),
            _region_point(region, x0, y1),
        ]
        for index in range(4):
            vertices.append(corners[index])
            vertices.append(corners[(index + 1) % 4])

    if not vertices:
        return
    shader = gpu.shader.from_builtin('UNIFORM_COLOR') if hasattr(gpu.shader, 'from_builtin') \
        else gpu.shader.from_builtin('2D_UNIFORM_COLOR')
    batch = batch_for_shader(shader, 'LINES', {'pos': vertices})
    shader.bind()
    shader.uniform_float('color', (0.15, 0.9, 0.25, 1.0))
    batch.draw(shader)


def _add_handler():
    global _draw_handle
    if _draw_handle is not None:
        return
    _draw_handle = bpy.types.SpaceNodeEditor.draw_handler_add(
        _draw_boxes, (), 'WINDOW', 'POST_PIXEL'
    )


def _remove_handler():
    global _draw_handle
    if _draw_handle is None:
        return
    bpy.types.SpaceNodeEditor.draw_handler_remove(_draw_handle, 'WINDOW')
    _draw_handle = None


@operator
class LOGIC_NODES_OT_toggle_debug_trace(Operator):
    bl_idname = "netlogic.toggle_debug_trace"
    bl_label = "Toggle Node Debug Trace"
    bl_options = {'REGISTER'}
    bl_description = (
        "Write or remove .debug_trace.on next to the .blend and draw a green "
        "box around every node the running player activated"
    )

    @classmethod
    def poll(cls, context):
        return context.space_data is not None and context.space_data.type == 'NODE_EDITOR'

    def execute(self, context):
        directory = _blend_dir()
        if not directory:
            warn('Debug trace skipped: save the .blend first')
            return {'CANCELLED'}

        enable_path = os.path.join(directory, ENABLE_FILE)
        if os.path.exists(enable_path):
            os.remove(enable_path)
            _remove_handler()
            notify('Debug trace off')
            return {'FINISHED'}

        with open(enable_path, 'w', encoding='utf-8') as handle:
            handle.write('1')
        _add_handler()
        notify('Debug trace on: green boxes follow the player activations')
        return {'FINISHED'}
