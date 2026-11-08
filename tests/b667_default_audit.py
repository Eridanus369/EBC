"""Audit: native inputs with no constant representation for their default.

Instantiates every catalog node in a scratch tree and resolves the value tag of
each *unconnected, non-trigger* input. Any tag outside
`_CONSTANT_NODE_TYPES` is a port whose editor default still cannot reach the
runtime (they are listed, not silently fixed).
"""

import bpy

import bge_gamenodes.generator.ir_export as ir_export
from bge_gamenodes.editor.nodes.native_catalog import CATALOG


def main():
    tree = bpy.data.node_groups.new('b667_audit', 'BGELogicTree')
    ctx = ir_export.ExportContext({})
    uncovered = []
    checked = 0

    for bl_idname in CATALOG:
        try:
            node = tree.nodes.new(bl_idname)
        except Exception:
            continue
        if getattr(node, 'native_dynamic_ports', False):
            continue
        native_inputs = getattr(node, 'native_inputs', None)
        if not native_inputs:
            continue
        for port_name, identifier in native_inputs:
            socket = next(
                (s for s in node.inputs if getattr(s, 'identifier', '') == identifier),
                None,
            )
            if socket is None or socket.links:
                continue
            if ir_export._socket_is_trigger(socket):
                continue
            tag = ir_export._socket_value_tag(node, socket, True, port_name, ctx)
            checked += 1
            if tag not in ir_export._CONSTANT_NODE_TYPES:
                uncovered.append((bl_idname, port_name, tag))

    for row in uncovered:
        print('UNCOVERED', *row)
    print(f'AUDIT checked={checked} uncovered={len(uncovered)}')


main()
