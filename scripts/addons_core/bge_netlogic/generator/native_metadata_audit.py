"""Metadata coverage audit for the native node inspector.

`ui/native_inspector.py` renders the exported IR using a metadata table
(inputs / outputs / config keys).  This module checks that table covers every
registered native node and still agrees with the editor-side port tables.

It is deliberately bpy-free so the generator test suite can run it without
starting Blender; the inspector passes the same data it has at runtime.

Report semantics:

* ``missing`` -- a metadata entry whose runtime id is not registered (an
  unknown ``runtime_type_id`` must be highlighted in the UI).
* ``unknown`` -- a registered native node without metadata (the inspector
  would render blank ports for it).
* ``port_mismatch`` / ``config_mismatch`` -- metadata disagrees with the
  editor table that declares the node.
"""

import ast
import glob
import os

GENERATOR_DIR = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(GENERATOR_DIR)
NODES_DIR = os.path.join(ADDON_DIR, 'editor', 'nodes')

TABLE_MODULES = (
    ('native_batch.py', 'NATIVE_BATCH'),
    ('native_b2.py', 'NATIVE_B2'),
    ('native_b3.py', 'NATIVE_B3'),
    ('native_b4.py', 'NATIVE_B4'),
    ('native_b4b.py', 'NATIVE_B4B'),
    ('native_b5.py', 'NATIVE_B5'),
    ('native_b6b.py', 'NATIVE_B6B'),
    ('native_b6c.py', 'NATIVE_B6C'),
    ('native_b6ui.py', 'NATIVE_B6UI'),
    ('native_b6file.py', 'NATIVE_B6FILE'),
)


def _contract_ports(rows):
    return tuple((name, 'trigger' if tag == 'Condition' else 'value',
                  '' if tag == 'Condition' else tag) for name, tag in rows)


def load_editor_tables():
    """runtime id -> (inputs, outputs, config_keys) from the native tables."""
    import port_contract as pc

    tables = {}
    for filename, table_name in TABLE_MODULES:
        path = os.path.join(NODES_DIR, filename)
        if not os.path.isfile(path):
            continue
        for entry in pc._table_entries(filename, table_name):
            runtime_id, _label, _kind, inputs, outputs, config = entry[:6]
            tables[runtime_id] = (tuple(inputs), tuple(outputs), tuple(config))
    return tables


def scan_registered_ids():
    """AST scan of the editor nodes: every non-deprecated runtime_type_id."""
    ids = set()
    for path in glob.glob(os.path.join(NODES_DIR, '**', '*.py'), recursive=True):
        try:
            tree = ast.parse(open(path, encoding='utf-8').read())
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.ClassDef):
                continue
            runtime_id = None
            deprecated = False
            for statement in node.body:
                if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
                    continue
                target = statement.targets[0]
                if not isinstance(target, ast.Name):
                    continue
                if target.id == 'runtime_type_id' and isinstance(statement.value, ast.Constant):
                    runtime_id = statement.value.value
                if target.id == 'deprecated' and isinstance(statement.value, ast.Constant):
                    deprecated = bool(statement.value.value)
            if runtime_id and not deprecated:
                ids.add(runtime_id)
    return ids


def registered_ids_from_sources():
    """Registered ids without importing bpy.

    Table generated classes attach ``runtime_type_id`` inside their type()
    namespace, so the AST scan alone misses them; add the table rows and the
    legacy catalog mapping on top.
    """
    import port_contract as pc

    ids = set(load_editor_tables())
    ids.update(info['runtime_id'] for info in pc.load_catalog().values())
    ids.update(scan_registered_ids())
    return ids


def audit(metadata, registered_ids, tables, dynamic_ids=frozenset(),
          known_runtime_ids=frozenset()):
    report = {
        'checked': [],
        'missing': [],
        'unknown': [],
        'port_mismatch': [],
        'config_mismatch': [],
    }
    for runtime_id in sorted(metadata):
        if runtime_id not in registered_ids and runtime_id not in known_runtime_ids:
            report['missing'].append(runtime_id)
            continue
        report['checked'].append(runtime_id)
        table = tables.get(runtime_id)
        if table is None or runtime_id in dynamic_ids:
            continue
        inputs, outputs, config_keys = table
        meta = metadata[runtime_id]
        if tuple(meta['inputs']) != _contract_ports(inputs):
            report['port_mismatch'].append((runtime_id, 'inputs'))
        if tuple(meta['outputs']) != _contract_ports(outputs):
            report['port_mismatch'].append((runtime_id, 'outputs'))
        if tuple(sorted(meta['config_keys'])) != tuple(sorted(config_keys)):
            report['config_mismatch'].append((runtime_id, 'config'))

    for runtime_id in sorted(registered_ids):
        if runtime_id not in metadata:
            report['unknown'].append(runtime_id)
    return report
