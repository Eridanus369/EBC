"""B6.6 regression (exporter half): unconnected editor defaults become constants.

Runs inside Blender, where `bge_gamenodes` is importable. No node tree is
needed: the fake node/socket drive `ExportContext.materialize_constants()`
directly, which is the exact function that used to drop String/Int/Vector
defaults on the floor.
"""

import json

import bge_gamenodes.generator.ir_export as ir_export
from bge_gamenodes.editor.sockets.socket import (
    SOCKET_TYPE_CONDITION,
    SOCKET_TYPE_INT,
    SOCKET_TYPE_STRING,
    SOCKET_TYPE_VECTOR,
)

LEGACY_TYPES = {'Float': 'ConstFloat', 'Bool': 'ConstBool'}


class FakeSocket:
    def __init__(self, name, nl_type, default_value):
        self.name = name
        self.identifier = name.lower()
        self.nl_type = nl_type
        self.default_value = default_value


class FakeNode:
    def __init__(self, name):
        self.name = name
        self.inputs = []


def export_with(socket):
    ctx = ir_export.ExportContext({})
    node = FakeNode('print')
    node.inputs = [FakeSocket('Condition', SOCKET_TYPE_CONDITION, True), socket]
    ctx.nodes.append({
        'type_id': 'Print',
        'debug_name': 'print',
        'inputs': [
            {'name': 'OnEnter', 'kind': 'trigger', 'value_tag': 'None'},
            {'name': 'Value', 'kind': 'value', 'value_tag': 'None'},
        ],
        'outputs': [{'name': 'OnDone', 'kind': 'trigger', 'value_tag': 'None'}],
        'config': {},
    })
    ctx.port_sockets[(0, True, 0)] = (node, 0)
    ctx.port_sockets[(0, True, 1)] = (node, 1)
    ctx.materialize_constants()
    return ctx


def summarize(label, ctx):
    constants = [n for n in ctx.nodes if n['type_id'].startswith('Const')]
    print(f'{label} nodes={len(ctx.nodes)} connections={len(ctx.connections)} '
          f'const={constants[0]["type_id"] if constants else "none"} '
          f'config={json.dumps(constants[0]["config"]) if constants else "{}"}')


def main():
    checks = []

    original = ir_export._CONSTANT_NODE_TYPES
    ir_export._CONSTANT_NODE_TYPES = LEGACY_TYPES
    before = export_with(FakeSocket('Value', SOCKET_TYPE_STRING, 'E'))
    summarize('BEFORE', before)
    checks.append(('BEFORE has no constant', len(before.nodes) == 1 and not before.connections))
    ir_export._CONSTANT_NODE_TYPES = original

    cases = [
        ('String', FakeSocket('Value', SOCKET_TYPE_STRING, 'E'),
         'ConstString', {'value': 'E'}),
        ('Int', FakeSocket('Value', SOCKET_TYPE_INT, 1920),
         'ConstInt', {'value': 1920}),
        ('Vector', FakeSocket('Value', SOCKET_TYPE_VECTOR, (0.0, 0.0, -9.8)),
         'ConstVector', {'value': [0.0, 0.0, -9.8]}),
    ]
    for name, socket, node_type, config in cases:
        ctx = export_with(socket)
        summarize(f'AFTER-{name}', ctx)
        constant = ctx.nodes[-1]
        checks.append((f'{name} emits {node_type}',
                       len(ctx.nodes) == 2 and len(ctx.connections) == 1
                       and constant['type_id'] == node_type
                       and constant['config'] == config))

    for label, ok in checks:
        print(f'{"PASS" if ok else "FAIL"} {label}')
    print(f'RESULT assertions={len(checks)} failures={sum(1 for _, ok in checks if not ok)}')


main()
