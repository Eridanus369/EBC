"""Static consistency scan for native node configuration keys.

The IR carries node config as a JSON-like blob. Python (editor nodes) exports
values with `export_config`, C++ reads them through `ConfigString` / `ConfigInt`
/ `ConfigBool` / `ConfigFloat` / `ConfigVector`. A mismatch is silent at
runtime: the reader just falls back to its default. This script compares the
two sides and fails when they disagree.

Checks:

* Python enum config with a `values` map exports an integer, C++ must not read
  it with `ConfigString`.
* Python enum config without a `values` map exports a string, C++ must not read
  it with `ConfigInt`.
* Config keys must exist on both sides (Python key with no C++ reader, or a C++
  reader for a key Python never exports).

Usage:

    python3 scripts/startup/bge_gamenodes/generator/enum_consistency.py
    python3 .../enum_consistency.py --json /tmp/enum_consistency.json

Exit code is 0 when every config key agrees, 1 otherwise.
"""

import argparse
import ast
import json
import os
import re
import sys

GENERATOR_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(GENERATOR_DIR))))
NODES_DIR = os.path.join(REPO_ROOT, 'scripts', 'startup', 'bge_gamenodes',
                         'editor', 'nodes')
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
    ('KX_FunctionNodesB4.cpp', 'kB4Specs'),
    ('KX_FunctionNodesB4File.cpp', 'kB4FileSpecs'),
    ('KX_FunctionNodesB5.cpp', 'kB5Specs'),
    ('KX_FunctionNodesB6Input.cpp', 'kB6InputSpecs'),
    ('KX_FunctionNodesB6String.cpp', 'kB6StringSpecs'),
    ('KX_FunctionNodesB6Audio.cpp', 'kB6AudioSpecs'),
    ('KX_FunctionNodesB6Net.cpp', 'kB6NetSpecs'),
    ('KX_FunctionNodesB6Sensor.cpp', 'kB6SensorSpecs'),
    ('KX_FunctionNodesB6Scene.cpp', 'kB6SceneSpecs'),
    ('KX_FunctionNodesB6LegacyQuery.cpp', 'kB6LegacyQuerySpecs'),
    ('KX_FunctionNodesB6UI.cpp', 'kB6UISpecs'),
    ('KX_FunctionNodesB6File.cpp', 'kB6FileSpecs'),
    ('KX_FunctionNodesB6.cpp', 'kB6Specs'),
    # B6.9-Fi / B6.9-Ms separate spec tables (see docs/NUMBER_CONSISTENCY_AUDIT.md B-20..B-25).
    ('KX_FunctionNodesB6File.cpp', 'kB6FileStreamSpecs'),
    ('KX_FunctionNodesB6Net.cpp', 'kB6MsgSpecs'),
)

ACCESSORS = {
    'ConfigString': 'string',
    'ConfigInt': 'int',
    'ConfigBool': 'bool',
    'ConfigFloat': 'float',
    'ConfigVector': 'vector',
}

CLASS_INT_AS_STRING = 'Python 导出整数、C++ 按字符串比'
CLASS_STRING_AS_INT = 'Python 导出字符串、C++ 按整数读'
CLASS_KEY_MISMATCH = '键名对不上'
CLASS_TYPE_MISMATCH = '值类型不匹配'


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
        base = _literal(node.value, env, cache)
        key = _literal(node.slice, env, cache)
        return base[key]
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


def _module_env(tree, base_dir=None):
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
        sub_env = _module_env(sub_tree, base_dir)
        sub_cache = {}
        for alias in statement.names:
            if alias.name not in sub_env:
                continue
            cache[alias.asname or alias.name] = _resolve(alias.name, sub_env, sub_cache)


def _table_entries(path, table_name):
    source = open(path, encoding='utf-8').read()
    tree = ast.parse(source)
    env = _module_env(tree, os.path.dirname(path))
    cache = {}
    _preload_imports(tree, os.path.dirname(path), cache)
    target = env.get(table_name)
    if target is None:
        raise SystemExit('table {} not found in {}'.format(table_name, path))
    entries = _literal(target, env, cache)
    return source, entries


def python_configs():
    """runtime id -> {key: descriptor} plus source line numbers."""
    result = {}
    for filename, table_name in PYTHON_TABLES:
        path = os.path.join(NODES_DIR, filename)
        source, entries = _table_entries(path, table_name)
        for entry in entries:
            runtime_id = entry[0]
            config = entry[5] if len(entry) > 5 else {}
            if not isinstance(config, dict):
                continue
            keys = {}
            for key, spec in config.items():
                kind = spec[0]
                if kind == 'enum':
                    info = spec[2]
                    values = info.get('values')
                    if values:
                        keys[key] = {
                            'export': 'int',
                            'detail': 'int: ' + ', '.join(
                                '{}={}'.format(name, number)
                                for name, number in values.items()),
                            'file': filename,
                            'line': _entry_line(source, runtime_id, key),
                        }
                    else:
                        items = [item[0] for item in info.get('items', [])]
                        keys[key] = {
                            'export': 'string',
                            'detail': 'string: ' + '/'.join(str(item) for item in items),
                            'file': filename,
                            'line': _entry_line(source, runtime_id, key),
                        }
                elif kind in ('compare', 'gate'):
                    items = [item[0] for item in spec[2].get('items', [])]
                    keys[key] = {
                        'export': 'string',
                        'detail': 'string: ' + '/'.join(str(item) for item in items),
                        'file': filename,
                        'line': _entry_line(source, runtime_id, key),
                    }
                else:
                    keys[key] = {
                        'export': kind,
                        'detail': kind,
                        'file': filename,
                        'line': _entry_line(source, runtime_id, key),
                    }
            result[runtime_id] = keys
    return result


def _find_line(source, needle):
    index = source.find(needle)
    if index < 0:
        return 0
    return source.count('\n', 0, index) + 1


def _entry_line(source, runtime_id, key):
    """Line of `key` inside the table entry that starts with `runtime_id`."""
    entry = source.find("('{}'".format(runtime_id))
    if entry < 0:
        entry = source.find('("{}"'.format(runtime_id))
    if entry < 0:
        return 0
    key_index = source.find("'{}'".format(key), entry)
    if key_index < 0:
        return 0
    next_entry = source.find("\n    ('", entry + 1)
    if next_entry >= 0 and key_index > next_entry:
        return 0
    return source.count('\n', 0, key_index) + 1


def _split_spec_entries(block):
    """Split a C++ spec table body on top-level commas."""
    entries = []
    depth = 0
    start = 0
    for index, char in enumerate(block):
        if char in '({[':
            depth += 1
        elif char in ')}]':
            depth -= 1
        elif char == ',' and depth == 0:
            entries.append(block[start:index])
            start = index + 1
    tail = block[start:]
    if tail.strip():
        entries.append(tail)
    return entries


def _spec_table_blocks(source, table_name):
    marker = re.search(r'const\s+KX_FunctionSpec\s+' + table_name + r'\s*\[\]\s*=\s*\{', source)
    if marker is None:
        return None
    start = source.find('{', marker.start())
    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    return None


def _function_body(source, name, start_index=0):
    match = re.search(r'\bvoid\s+' + re.escape(name) + r'\s*\(', source)
    if match is None:
        return None
    brace = source.find('{', match.end())
    if brace < 0:
        return None
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return source[brace + 1:index], source.count('\n', 0, brace) + 1
    return None


def cpp_configs():
    """runtime id -> {key: {'accessor': ..., 'file': ..., 'line': ...}}."""
    result = {}
    for filename, table_name in CPP_TABLES:
        path = os.path.join(GAME_NODES_DIR, filename)
        source = open(path, encoding='utf-8').read()
        block = _spec_table_blocks(source, table_name)
        if block is None:
            continue
        for entry in _split_spec_entries(block):
            id_match = re.search(r'"([^"]+)"', entry)
            if id_match is None:
                continue
            runtime_id = id_match.group(1)
            functions = re.findall(r'\bFunc[A-Za-z0-9_]+\b', entry)
            reads = {}
            for function in functions:
                body = _function_body(source, function)
                if body is None:
                    continue
                body_text, body_line = body
                for call in re.finditer(
                        r'Config(String|Int|Bool|Float|Vector)\s*\(\s*"([^"]+)"', body_text):
                    accessor = 'Config' + call.group(1)
                    key = call.group(2)
                    line = body_line + body_text.count('\n', 0, call.start())
                    reads.setdefault(key, {
                        'accessor': accessor,
                        'type': ACCESSORS[accessor],
                        'file': filename,
                        'line': line,
                    })
            result.setdefault(runtime_id, {}).update(reads)
    return result


def _verdict(python_type, cpp_type):
    if python_type == 'int' and cpp_type == 'string':
        return CLASS_INT_AS_STRING
    if python_type == 'string' and cpp_type == 'int':
        return CLASS_STRING_AS_INT
    if python_type == cpp_type:
        return None
    return CLASS_TYPE_MISMATCH


def scan():
    py = python_configs()
    cpp = cpp_configs()
    rows = []
    checked = 0
    for runtime_id in sorted(py):
        python_keys = py[runtime_id]
        cpp_keys = cpp.get(runtime_id)
        if cpp_keys is None:
            rows.append({
                'runtime_type_id': runtime_id,
                'config_key': '*',
                'python_value': '-',
                'cpp_read': '未找到 C++ 节点定义',
                'verdict': CLASS_KEY_MISMATCH,
                'python_location': '',
                'cpp_location': '',
            })
            continue
        for key, info in sorted(python_keys.items()):
            reads = cpp_keys.get(key)
            if reads is None:
                rows.append({
                    'runtime_type_id': runtime_id,
                    'config_key': key,
                    'python_value': info['detail'],
                    'cpp_read': '未读取该键',
                    'verdict': CLASS_KEY_MISMATCH,
                    'python_location': '{}:{}'.format(info['file'], info['line']),
                    'cpp_location': '',
                })
                continue
            checked += 1
            verdict = _verdict(info['export'], reads['type'])
            if verdict is not None:
                rows.append({
                    'runtime_type_id': runtime_id,
                    'config_key': key,
                    'python_value': info['detail'],
                    'cpp_read': '{}("{}") -> {}'.format(reads['accessor'], key, reads['type']),
                    'verdict': verdict,
                    'python_location': '{}:{}'.format(info['file'], info['line']),
                    'cpp_location': '{}:{}'.format(reads['file'], reads['line']),
                })
        for key, reads in sorted(cpp_keys.items()):
            if key in python_keys:
                continue
            rows.append({
                'runtime_type_id': runtime_id,
                'config_key': key,
                'python_value': '未导出该键',
                'cpp_read': '{}("{}") -> {}'.format(reads['accessor'], key, reads['type']),
                'verdict': CLASS_KEY_MISMATCH,
                'python_location': '',
                'cpp_location': '{}:{}'.format(reads['file'], reads['line']),
            })
    return {'checked_keys': checked, 'inconsistencies': rows}


def _print_report(report):
    rows = report['inconsistencies']
    print('ENUM CONFIG CONSISTENCY checked_pairs={} inconsistencies={}'.format(
        report['checked_keys'], len(rows)))
    if not rows:
        print('OK: Python export type and C++ config reader agree on every key.')
        return
    header = ('runtime_type_id', 'config_key', 'python_value', 'cpp_read',
              'verdict', 'python_location', 'cpp_location')
    widths = [len(value) for value in header]
    for row in rows:
        values = [str(row[field]) for field in header]
        for index, value in enumerate(values):
            widths[index] = max(widths[index], len(value))
    template = '  '.join('{:<%d}' % width for width in widths)
    print(template.format(*header))
    for row in rows:
        print(template.format(*[str(row[field]) for field in header]))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', help='write the full report to this path')
    args = parser.parse_args(argv)
    report = scan()
    _print_report(report)
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as handle:
            json.dump(report, handle, indent=2, ensure_ascii=False)
    return 1 if report['inconsistencies'] else 0


if __name__ == '__main__':
    sys.exit(main())
