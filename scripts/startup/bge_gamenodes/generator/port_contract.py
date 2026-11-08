"""Static port-contract check for native nodes.

Every native node has two port descriptions that must agree:

* the editor side (either a `native_catalog` mapping of a legacy node, or a
  `NATIVE_*` table entry),
* the C++ side (`KX_FunctionSpec` entry → `KX_FuncPort` array in GameNodes).

A drift here is invisible until a graph is exported: the IR writer looks ports
up by identifier/name and the runtime reads them by index, so a renamed,
reordered or missing port either warns during export or silently feeds the
wrong value. This script compares both sides statically and exits non-zero on
any provable mismatch.

Usage:

    python3 scripts/startup/bge_gamenodes/generator/port_contract.py
    python3 .../port_contract.py --json /tmp/port_contract.json

Exit code is 0 when every checked node agrees, 1 otherwise.
"""

import argparse
import ast
import glob
import json
import os
import re
import sys

GENERATOR_DIR = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(GENERATOR_DIR)
NODES_DIR = os.path.join(ADDON_DIR, 'editor', 'nodes')
SOCKETS_DIR = os.path.join(ADDON_DIR, 'editor', 'sockets')
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(GENERATOR_DIR))))
GAME_NODES_DIR = os.path.join(REPO_ROOT, 'source', 'gameengine', 'GameLogic',
                              'GameNodes')

PYTHON_TABLES = (
    ('native_b2.py', 'NATIVE_B2'),
    ('native_b3.py', 'NATIVE_B3'),
    ('native_b4.py', 'NATIVE_B4'),
    ('native_b4b.py', 'NATIVE_B4B'),
    ('native_batch.py', 'NATIVE_BATCH'),
    ('native_b5.py', 'NATIVE_B5'),
    ('native_b6b.py', 'NATIVE_B6B'),
    ('native_b6c.py', 'NATIVE_B6C'),
    ('native_b6ui.py', 'NATIVE_B6UI'),
    ('native_b6file.py', 'NATIVE_B6FILE'),
    ('native_b6.py', 'NATIVE_B6'),
)

CPP_TABLES = (
    ('KX_FunctionNode.cpp', 'kSpecs'),
    ('KX_FunctionNodesB2.cpp', 'kB2Specs'),
    ('KX_FunctionNodesB3.cpp', 'kB3Specs'),
    ('KX_FunctionNodesB4File.cpp', 'kB4FileSpecs'),
    ('KX_FunctionNodesB4.cpp', 'kB4Specs'),
    ('KX_FunctionNodesB5.cpp', 'kB5Specs'),
    ('KX_FunctionNodesB6Input.cpp', 'kB6InputSpecs'),
    ('KX_FunctionNodesB6String.cpp', 'kB6StringSpecs'),
    ('KX_FunctionNodesB6Audio.cpp', 'kB6AudioSpecs'),
    ('KX_FunctionNodesB6Net.cpp', 'kB6NetSpecs'),
    ('KX_FunctionNodesB6Sensor.cpp', 'kB6SensorSpecs'),
    ('KX_FunctionNodesB6Scene.cpp', 'kB6SceneSpecs'),
    ('KX_FunctionNodesB6Legacy.cpp', 'kB6LegacySpecs'),
    ('KX_FunctionNodesB6LegacyQuery.cpp', 'kB6LegacyQuerySpecs'),
    ('KX_FunctionNodesExtra.cpp', 'kExtraSpecs'),
    ('KX_FunctionNodesB6UI.cpp', 'kB6UISpecs'),
    ('KX_FunctionNodesB6File.cpp', 'kB6FileSpecs'),
    ('KX_FunctionNodesB6.cpp', 'kB6Specs'),
    # B6.9-Fi / B6.9-Ms keep their specs in separate tables so the older
    # tables stay untouched; the contract checker must read them too.
    ('KX_FunctionNodesB6File.cpp', 'kB6FileStreamSpecs'),
    ('KX_FunctionNodesB6Net.cpp', 'kB6MsgSpecs'),
)

TAG_TO_CPP = {
    'Condition': 'trigger',
    'Bool': 'KX_VALUE_BOOL',
    'Int': 'KX_VALUE_INT',
    'Float': 'KX_VALUE_FLOAT',
    'Vector': 'KX_VALUE_VECTOR',
    'Object': 'KX_VALUE_OBJECT',
    'String': 'KX_VALUE_STRING',
    'List': 'KX_VALUE_LIST',
    'Color': 'KX_VALUE_COLOR',
    'DataBlock': 'KX_VALUE_DATABLOCK',
}

SOCKET_TYPE_TO_TAG = {
    0: None,
    1: 'Condition',
    3: 'Int',
    5: 'Float',
    7: 'String',
    8: 'Bool',
    9: 'Vector',
    11: 'Color',
    12: 'Object',
    14: 'DataBlock',
    16: 'List',
    24: 'List',
}

# Nodes whose port list is computed at runtime; the C++ side reads the ports
# back from the IR, so a static comparison does not apply.
DYNAMIC_RUNTIME_IDS = {'Formula', 'StringFormat', 'SetUIWidgetAttr',
                       'SyncProperty', 'GetRemoteProperty', 'UIAnimation'}


class Unresolved(Exception):
    pass


def _literal(node, env, cache):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return _resolve(node.id, env, cache)
    if isinstance(node, (ast.List, ast.Tuple, ast.Set)):
        return [_literal(item, env, cache) for item in node.elts]
    if isinstance(node, ast.Dict):
        return {_literal(key, env, cache): _literal(value, env, cache)
                for key, value in zip(node.keys, node.values)}
    if isinstance(node, ast.Subscript):
        return _literal(node.value, env, cache)[_literal(node.slice, env, cache)]
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -_literal(node.operand, env, cache)
    raise Unresolved(ast.dump(node))


def _resolve(name, env, cache):
    if name in cache:
        return cache[name]
    node = env.get(name)
    if node is None:
        raise Unresolved(name)
    cache[name] = None
    value = _literal(node, env, cache)
    cache[name] = value
    return value


def _module_env(tree):
    env = {}
    for statement in tree.body:
        if isinstance(statement, ast.Assign):
            for target in statement.targets:
                if isinstance(target, ast.Name):
                    env[target.id] = statement.value
        elif isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name):
            env[statement.target.id] = statement.value
    return env


def _preload_imports(tree, base_dir, cache):
    for statement in tree.body:
        if not isinstance(statement, ast.ImportFrom):
            continue
        module = (statement.module or '').split('.')[-1]
        path = os.path.join(base_dir, module + '.py')
        if not os.path.isfile(path):
            continue
        sub_tree = ast.parse(open(path, encoding='utf-8').read())
        sub_env = _module_env(sub_tree)
        sub_cache = {}
        for alias in statement.names:
            if alias.name in sub_env:
                cache[alias.asname or alias.name] = _resolve(alias.name, sub_env, sub_cache)


def _table_entries(filename, table_name):
    path = os.path.join(NODES_DIR, filename)
    tree = ast.parse(open(path, encoding='utf-8').read())
    env = _module_env(tree)
    cache = {}
    _preload_imports(tree, NODES_DIR, cache)
    return _resolve(table_name, env, cache)


def load_python_tables():
    nodes = {}
    for filename, table_name in PYTHON_TABLES:
        for entry in _table_entries(filename, table_name):
            nodes[entry[0]] = {
                'source': filename,
                'inputs': [(name, tag) for name, tag in entry[3]],
                'outputs': [(name, tag) for name, tag in entry[4]],
            }
    return nodes


def load_catalog():
    path = os.path.join(NODES_DIR, 'native_catalog.py')
    tree = ast.parse(open(path, encoding='utf-8').read())
    env = _module_env(tree)
    cache = {}
    catalog = _literal(env['CATALOG'], env, cache)
    result = {}
    for bl_idname, entry in catalog.items():
        runtime_id, inputs, outputs, generic, config = entry
        result[bl_idname] = {
            'runtime_id': runtime_id,
            'inputs': list(inputs),
            'outputs': list(outputs),
            'generic': dict(generic),
            'source': 'native_catalog.py',
        }
    return result


def load_dynamic_runtime_ids():
    ids = set(DYNAMIC_RUNTIME_IDS)
    for path in glob.glob(os.path.join(NODES_DIR, '**', '*.py'), recursive=True):
        try:
            tree = ast.parse(open(path, encoding='utf-8').read())
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.ClassDef):
                continue
            runtime_id = None
            dynamic = False
            for statement in node.body:
                if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
                    target = statement.targets[0]
                    if isinstance(target, ast.Name) and target.id == 'runtime_type_id':
                        if isinstance(statement.value, ast.Constant):
                            runtime_id = statement.value.value
                    if isinstance(target, ast.Name) and target.id == 'native_dynamic_ports':
                        dynamic = isinstance(statement.value, ast.Constant) and statement.value.value
            if runtime_id and dynamic:
                ids.add(runtime_id)
    return ids


def _socket_type_map():
    """Socket class name -> nl_type integer, resolving simple inheritance."""
    constants = {}
    socket_py = os.path.join(SOCKETS_DIR, 'socket.py')
    tree = ast.parse(open(socket_py, encoding='utf-8').read())
    for statement in tree.body:
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
            target = statement.targets[0]
            if isinstance(target, ast.Name) and target.id.startswith('SOCKET_TYPE_'):
                try:
                    constants[target.id] = ast.literal_eval(statement.value)
                except ValueError:
                    pass
    classes = {}
    for path in glob.glob(os.path.join(SOCKETS_DIR, '*.py')):
        try:
            body = ast.parse(open(path, encoding='utf-8').read())
        except SyntaxError:
            continue
        for node in body.body:
            if not isinstance(node, ast.ClassDef):
                continue
            bases = [base.id for base in node.bases if isinstance(base, ast.Name)]
            nl_type = None
            for statement in node.body:
                if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
                    target = statement.targets[0]
                    if isinstance(target, ast.Name) and target.id == 'nl_type':
                        if isinstance(statement.value, ast.Name):
                            nl_type = constants.get(statement.value.id)
                        elif isinstance(statement.value, ast.Constant):
                            nl_type = statement.value.value
            classes[node.name] = (bases, nl_type)

    def resolve(name, seen=None):
        seen = seen or set()
        if name in seen or name not in classes:
            return None
        seen.add(name)
        bases, nl_type = classes[name]
        if nl_type is not None:
            return nl_type
        for base in bases:
            value = resolve(base, seen)
            if value is not None:
                return value
        return None

    return {name: resolve(name) for name in classes}


SOCKET_TYPES = _socket_type_map()


def _find_class(bl_idname):
    """(file, ClassDef) for the legacy node class declared with bl_idname."""
    for path in glob.glob(os.path.join(NODES_DIR, '**', '*.py'), recursive=True):
        try:
            tree = ast.parse(open(path, encoding='utf-8').read())
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.ClassDef):
                continue
            for statement in node.body:
                if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
                    target = statement.targets[0]
                    if (isinstance(target, ast.Name) and target.id == 'bl_idname'
                            and isinstance(statement.value, ast.Constant)
                            and statement.value.value == bl_idname):
                        return path, node
    return None, None


def _legacy_sockets(bl_idname):
    """Ordered (identifier, socket_class) declarations from the legacy init()."""
    path, cls = _find_class(bl_idname)
    if cls is None:
        return None
    init = None
    for statement in cls.body:
        if isinstance(statement, ast.FunctionDef) and statement.name == 'init':
            init = statement
            break
    if init is None:
        return None
    inputs = []
    outputs = []
    for call in ast.walk(init):
        if not isinstance(call, ast.Call):
            continue
        if not isinstance(call.func, ast.Attribute) or not isinstance(call.func.value, ast.Name):
            continue
        if call.func.value.id != 'self':
            continue
        if call.func.attr not in ('add_input', 'add_output'):
            continue
        args = call.args
        if len(args) < 3:
            continue
        socket_class = args[0].id if isinstance(args[0], ast.Name) else None
        identifier = args[2].value if isinstance(args[2], ast.Constant) else None
        if socket_class is None or identifier is None:
            continue
        (inputs if call.func.attr == 'add_input' else outputs).append((identifier, socket_class))
    return inputs, outputs


def _effective_tag(socket_class, generic, port_name):
    if port_name in generic:
        return generic[port_name]
    nl_type = SOCKET_TYPES.get(socket_class)
    return SOCKET_TYPE_TO_TAG.get(nl_type) if nl_type in SOCKET_TYPE_TO_TAG else None


def _split_top_level(text):
    items = []
    depth = 0
    start = 0
    for index, char in enumerate(text):
        if char in '([{':
            depth += 1
        elif char in ')]}':
            depth -= 1
        elif char == ',' and depth == 0:
            items.append(text[start:index])
            start = index + 1
    if text[start:].strip():
        items.append(text[start:])
    return items


def _brace_block(source, start):
    depth = 0
    for index in range(start, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    return ''


def load_cpp_specs():
    sources = {}
    for filename, _ in CPP_TABLES:
        path = os.path.join(GAME_NODES_DIR, filename)
        sources[filename] = open(path, encoding='utf-8').read()
    port_arrays = {}

    def parse_array(name):
        if name in port_arrays:
            return port_arrays[name]
        for source in sources.values():
            match = re.search(r'const\s+KX_FuncPort\s+' + re.escape(name)
                              + r'\s*\[\]\s*=\s*\{', source)
            if match is None:
                continue
            block = _brace_block(source, source.find('{', match.start()))
            ports = []
            for call in re.finditer(
                    r'\b(FP|TP)\s*\(\s*"([^"]*)"\s*(?:,\s*(KX_VALUE_[A-Z_]+))?', block):
                if call.group(1) == 'TP':
                    ports.append((call.group(2), 'trigger'))
                else:
                    ports.append((call.group(2), call.group(3) or 'KX_VALUE_NONE'))
            port_arrays[name] = ports
            return ports
        port_arrays[name] = None
        return None

    specs = {}
    for filename, table_name in CPP_TABLES:
        source = sources[filename]
        marker = re.search(r'const\s+KX_FunctionSpec\s+' + table_name
                           + r'\s*\[\]\s*=\s*\{', source)
        if marker is None:
            continue
        block = _brace_block(source, source.find('{', marker.start()))
        # Section markers such as `/* B2.1 math / vector */` sit between entries
        # and would otherwise glue themselves to the following spec entry.
        block = re.sub(r'/\*.*?\*/', '', block, flags=re.S)
        for entry in _split_top_level(block):
            entry = entry.strip()
            if not entry or entry.startswith('/*'):
                continue
            call = re.match(r'([A-Z0-9_]+)\s*\(', entry)
            if call is None:
                continue
            macro = call.group(1)
            inner = entry[call.end():]
            if not inner.endswith(')'):
                continue
            args = [arg.strip() for arg in _split_top_level(inner[:-1])]
            if len(args) < 2:
                continue
            runtime_id = args[0].strip('"')
            if macro in ('NO_INPUT_VALUE_NODE', 'NO_INPUT_VALUE', 'NO_INPUT_STATE_NODE'):
                input_array, output_array = None, args[1]
            elif macro in ('B6L_NO_OUTPUT',):
                input_array, output_array = args[1], None
            elif macro in ('ACTION', 'POLY_ACTION'):
                input_array, output_array = args[1], 'kDone'
            elif macro == 'CONDITION':
                input_array, output_array = (args[1] if len(args) > 2 else None), 'kConditionOutput'
            elif macro == 'NO_INPUT_CONDITION':
                input_array, output_array = None, 'kConditionOutput'
            else:
                input_array, output_array = args[1], args[2]
            if input_array == 'nullptr':
                input_array = None
            specs[runtime_id] = {
                'file': filename,
                'macro': macro,
                'inputs': parse_array(input_array) if input_array else [],
                'outputs': parse_array(output_array) if output_array else [],
                'input_array': input_array,
                'output_array': output_array,
            }
    return specs


def _pair_legacy(entries, mapping):
    """Pair catalog (name, identifier) rows with legacy sockets by occurrence.

    Legacy nodes may declare the same identifier twice (MapRange has a float and
    a vector group both called `value`), so each declaration can only be
    consumed once, in order.
    """
    unused = list(entries)
    paired = []
    for name, identifier in mapping:
        socket_class = None
        for position, (legacy_id, candidate) in enumerate(unused):
            if legacy_id == identifier:
                socket_class = candidate
                del unused[position]
                break
        paired.append((name, socket_class))
    return paired


def load_signature_specs():
    """Ports of nodes registered through `Signature()` in KX_RuntimeNodes.cpp.

    These are hand-written runtime nodes (OnInit, Delay, Branch, ...). Their
    ports live in C++ constructor methods rather than in a KX_FunctionSpec
    table, so they need a small dedicated parser.
    """
    path = os.path.join(GAME_NODES_DIR, 'KX_RuntimeNodes.cpp')
    source = open(path, encoding='utf-8').read()
    registered = {}
    for match in re.finditer(
            r'RegisterGameNodeType\s*<\s*([A-Za-z0-9_:]+)\s*>\s*\('
            r'\s*"([^"]+)"\s*,', source):
        registered[match.group(1)] = match.group(2)

    signatures = {}
    for match in re.finditer(
            r'KX_NodeSignature\s+([A-Za-z0-9_:]+)::Signature\s*\(\s*\)\s*\{', source):
        block = _brace_block(source, source.find('{', match.start()))
        inputs = []
        outputs = []
        for call in re.finditer(
                r'\.\s*(inputs|outputs)\s*\.push_back\s*\(\s*'
                r'(TriggerPort|ValuePort)\s*\(\s*"([^"]+)"'
                r'(?:\s*,\s*(KX_VALUE_[A-Z_]+))?', block):
            side, kind, name, tag = call.groups()
            port = (name, 'trigger' if kind == 'TriggerPort' else tag or 'KX_VALUE_NONE')
            (inputs if side == 'inputs' else outputs).append(port)
        signatures[match.group(1)] = {'inputs': inputs, 'outputs': outputs}

    specs = {}
    for class_name, runtime_id in registered.items():
        signature = signatures.get(class_name)
        if signature is None:
            continue
        specs[runtime_id] = {
            'file': 'KX_RuntimeNodes.cpp',
            'macro': 'SIGNATURE',
            'inputs': signature['inputs'],
            'outputs': signature['outputs'],
            'input_array': None,
            'output_array': None,
        }
    return specs


def _compare_side(label, declared, expected, problems, notes):
    """Compare one port list; only type/count drift is a failure.

    Port names are informational for static nodes: the graph compiler aligns
    ports by index and the runtime reads them by index, so a rename is reported
    as a note instead of a hard failure.
    """
    if len(declared) != len(expected):
        problems.append('{} count {} != C++ {}'.format(
            label, len(declared), len(expected)))
        return
    for index, ((name, tag), (cpp_name, cpp_type)) in enumerate(zip(declared, expected)):
        if name != cpp_name:
            notes.append('{}[{}] name {!r} vs C++ {!r} (index aligned)'.format(
                label, index, name, cpp_name))
        if tag is None:
            problems.append('{}[{}] {!r} has no resolvable type (C++ {})'.format(
                label, index, name, cpp_type))
            continue
        expected_tag = 'trigger' if tag in ('trigger', 'Condition') else TAG_TO_CPP.get(tag)
        if expected_tag is None:
            problems.append('{}[{}] {!r} has unknown editor type {!r} (C++ {})'.format(
                label, index, name, tag, cpp_type))
        elif expected_tag != cpp_type:
            problems.append('{}[{}] {!r} is {} but C++ expects {}'.format(
                label, index, name, expected_tag, cpp_type))


def check():
    specs = load_cpp_specs()
    for runtime_id, spec in load_signature_specs().items():
        specs.setdefault(runtime_id, spec)
    dynamic = load_dynamic_runtime_ids()
    tables = load_python_tables()
    catalog = load_catalog()
    rows = []
    errors = 0
    warnings = 0

    def record(node, runtime_id, issues, notes=None, source=None):
        nonlocal errors, warnings
        notes = notes or []
        if issues:
            errors += 1
            rows.append({'node': node, 'runtime_id': runtime_id, 'source': source,
                         'problems': issues, 'notes': notes})
        elif notes:
            warnings += 1
            rows.append({'node': node, 'runtime_id': runtime_id, 'source': source,
                         'problems': [], 'notes': notes})
        else:
            rows.append({'node': node, 'runtime_id': runtime_id, 'source': source,
                         'problems': [], 'notes': []})

    # Native table nodes: ports are declared directly as (name, tag).
    for runtime_id, info in sorted(tables.items()):
        if runtime_id in dynamic:
            continue
        spec = specs.get(runtime_id)
        if spec is None:
            record(runtime_id, runtime_id, ['no C++ spec entry'])
            continue
        declared_in = [(name, tag) for name, tag in info['inputs']]
        declared_out = [(name, tag) for name, tag in info['outputs']]
        issues = []
        notes = []
        _compare_side('in', declared_in, spec['inputs'], issues, notes)
        _compare_side('out', declared_out, spec['outputs'], issues, notes)
        record(runtime_id, runtime_id, issues, notes, info['source'])

    # Catalog nodes: editor ports are legacy sockets plus generic overrides.
    for bl_idname, info in sorted(catalog.items()):
        runtime_id = info['runtime_id']
        if runtime_id in dynamic:
            continue
        spec = specs.get(runtime_id)
        if spec is None:
            record(bl_idname, runtime_id, ['no C++ spec entry'])
            continue
        legacy = _legacy_sockets(bl_idname)
        notes = []
        issues = []
        if legacy is None:
            notes.append('legacy sockets unavailable (dynamic init)')
            declared_in = [(name, info['generic'].get(name)) for name, _ in info['inputs']]
            declared_out = [(name, info['generic'].get(name)) for name, _ in info['outputs']]
        else:
            legacy_in, legacy_out = legacy
            in_order = [identifier for identifier, _ in legacy_in]
            out_order = [identifier for identifier, _ in legacy_out]
            mapped_in = [identifier for _, identifier in info['inputs']]
            mapped_out = [identifier for _, identifier in info['outputs']]
            if mapped_in != [item for item in in_order if item in mapped_in]:
                issues.append('input mapping order differs from legacy declaration: {} vs {}'.format(
                    mapped_in, in_order))
            if out_order != [item for item in out_order if item in mapped_out]:
                issues.append('output mapping order differs from legacy declaration: {} vs {}'.format(
                    mapped_out, out_order))
            unmatched = sorted(set(mapped_in) - set(in_order))
            if unmatched:
                notes.append('mapped identifiers not in legacy init: ' + ', '.join(unmatched))
            paired_in = _pair_legacy(legacy_in, info['inputs'])
            paired_out = _pair_legacy(legacy_out, info['outputs'])
            declared_in = []
            unresolved_in = []
            for name, socket_class in paired_in:
                tag = _effective_tag(socket_class, info['generic'], name)
                if tag is None and name not in info['generic']:
                    unresolved_in.append(name)
                declared_in.append((name, tag))
            declared_out = []
            unresolved_out = []
            for name, socket_class in paired_out:
                tag = _effective_tag(socket_class, info['generic'], name)
                if tag is None and name not in info['generic']:
                    unresolved_out.append(name)
                declared_out.append((name, tag))
            if unresolved_in or unresolved_out:
                notes.append('type unresolved for: ' + ', '.join(unresolved_in + unresolved_out))
        _compare_side('in', declared_in, spec['inputs'], issues, notes)
        _compare_side('out', declared_out, spec['outputs'], issues, notes)
        record(bl_idname, runtime_id, issues, notes, info['source'])

    return {
        'checked_native_nodes': len(tables),
        'checked_catalog_nodes': len(catalog),
        'errors': errors,
        'warnings': warnings,
        'details': rows,
    }


def _print_report(report):
    print('PORT CONTRACT native_nodes={} catalog_nodes={} errors={} warnings={}'.format(
        report['checked_native_nodes'], report['checked_catalog_nodes'],
        report['errors'], report['warnings']))
    for row in report['details']:
        if row['problems']:
            print('FAIL {} -> {} (editor: {})'.format(
                row['node'], row['runtime_id'], row['source']))
            for problem in row['problems']:
                print('  - ' + problem)
        elif row['notes']:
            print('NOTE {} -> {} (editor: {})'.format(
                row['node'], row['runtime_id'], row['source']))
        for note in row['notes']:
            print('  note: ' + note)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', help='write the full report to this path')
    args = parser.parse_args(argv)
    report = check()
    _print_report(report)
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as handle:
            json.dump(report, handle, indent=2, ensure_ascii=False)
    return 1 if report['errors'] else 0


if __name__ == '__main__':
    sys.exit(main())
