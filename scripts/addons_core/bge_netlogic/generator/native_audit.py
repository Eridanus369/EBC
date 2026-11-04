"""Coverage audit for the legacy to native node migration.

The audit splits the legacy node classes into four buckets instead of the old
single "migrated" counter, so progress numbers reflect how much real work a
mapping carries:

* direct     - the native node is a port for port replacement of the legacy node
* translated - same feature, but ports were renamed/typed or config keys mapped
* rewritten  - the native node reimplements the semantics (new type system,
               dynamic ports, or a different execution contract)
* deferred   - no native runtime type yet

Running it inside Blender:

    blender -b --factory-startup --python-expr \
        "import sys; sys.path.append('<addon parent>'); \
         from bge_netlogic.generator.native_audit import main; main()"
"""

import json

from ..editor.nodes.node import _nodes
from ..editor.nodes import native_catalog


# Native implementations which are not mechanical ports of a legacy node.
# Strings and lists introduced the owned value type system, Formula parses to
# RPN, StringFormat resolves variable ports and ForEach expands a synchronous
# element sequence inside the scheduler.
_REWRITTEN_RUNTIME_IDS = frozenset({
    'Formula',
    'StringFormat',
    'ForEach',
    'StringConcat',
    'StringCompare',
    'StringFind',
    'StringReplace',
    'StringLength',
    'StringSubstr',
    'StringToUpper',
    'StringToLower',
    'StringToNumber',
    'NumberToString',
    'ListAdd',
    'ListRemove',
    'ListGet',
    'ListSet',
    'ListLength',
    'ListContains',
    'ListClear',
})

# B2 native node types that mirror an existing node closely enough to stay in
# the translational bucket, `ClampValue` is the only exact legacy port match.
_B2_DIRECT_RUNTIME_IDS = frozenset({'ClampValue'})


def _catalog_entry(bl_idname):
    return native_catalog.CATALOG.get(bl_idname)


def _classify(bl_idname, runtime_id):
    if runtime_id in _REWRITTEN_RUNTIME_IDS:
        return 'migrated_rewritten'
    entry = _catalog_entry(bl_idname)
    if entry is not None:
        generic_types, config = entry[3], entry[4]
        if generic_types or config:
            return 'migrated_translated'
    return 'migrated_direct'


# B3 native node types, one bucket per runtime id. Nodes without a legacy
# counterpart (PlayActionByName, SetActionSpeed, ...) are new surface and count
# as rewritten by definition.
_B3_CATEGORIES = {
    # animation (B3.1)
    'PlayAction': 'migrated_rewritten',
    'PlayActionByName': 'migrated_rewritten',
    'StopAction': 'migrated_direct',
    'SetActionFrame': 'migrated_translated',
    'SetActionSpeed': 'migrated_rewritten',
    'GetActionFrame': 'migrated_rewritten',
    'ActionDone': 'migrated_rewritten',
    'SetLayerWeight': 'migrated_rewritten',
    # message + audio (B3.2)
    'MessageReceived': 'migrated_rewritten',
    'StartSound': 'migrated_rewritten',
    'StopSound': 'migrated_rewritten',
    'SetSoundVolume': 'migrated_rewritten',
    'SetSoundPitch': 'migrated_rewritten',
    'SoundFinished': 'migrated_rewritten',
    # proximity (B3.3)
    'Radar': 'migrated_rewritten',
    # audio listener and by-name playback (B3.4)
    'PlaySoundByName': 'migrated_rewritten',
    'Set3DListener': 'migrated_rewritten',
    # scene control (B3.4)
    'SuspendScene': 'migrated_rewritten',
    'ResumeScene': 'migrated_rewritten',
    'AddObject': 'migrated_rewritten',
    # picking (B3.4)
    'Touch': 'migrated_rewritten',
}


def _b3_inventory():
    """Inventory the editor's B3 node list, defaulting every native id to rewritten."""
    try:
        from ..editor.nodes.native_b3 import NATIVE_B3
        ids = [entry[0] for entry in NATIVE_B3]
    except Exception:
        ids = list(_B3_CATEGORIES)
    return [{'runtime_id': runtime_id,
             'category': _B3_CATEGORIES.get(runtime_id, 'migrated_rewritten')}
            for runtime_id in ids]


def _b2_inventory():
    """Map every B2 native node type onto a bucket for the progress baseline."""
    try:
        from ..editor.nodes.native_b2 import NATIVE_B2
    except Exception:
        return []
    rewritten = _REWRITTEN_RUNTIME_IDS
    inventory = []
    for runtime_id, _label, _kind, _inputs, _outputs, _config in NATIVE_B2:
        if runtime_id in _B2_DIRECT_RUNTIME_IDS:
            category = 'migrated_direct'
        elif runtime_id in rewritten:
            category = 'migrated_rewritten'
        else:
            category = 'migrated_translated'
        inventory.append({'runtime_id': runtime_id, 'category': category})
    # Formula and StringFormat resolve their ports from the IR, they are
    # hand written in native_b2.py instead of the fixed port table.
    inventory.extend([
        {'runtime_id': 'Formula', 'category': 'migrated_rewritten'},
        {'runtime_id': 'StringFormat', 'category': 'migrated_rewritten'},
    ])
    return inventory


_SEMANTIC_ALIASES = {
    # Legacy names that were renamed in the native surface.
    'quitgame': 'endgame',
    'applyimpulse': 'applytransform',
}


def _normalise_semantics(name):
    """Reduce a legacy or native node id to a comparable semantic key."""
    value = name.strip().lower()
    for prefix in ('ul', 'nl', 'logicnode'):
        if value.startswith(prefix):
            value = value[len(prefix):]
    return _SEMANTIC_ALIASES.get(value, value)


def _batch_ids():
    try:
        from ..editor.nodes.native_batch import NATIVE_BATCH
    except Exception:
        return []
    return [entry[0] for entry in NATIVE_BATCH]


def _semantic_coverage(report):
    """Deduped semantic coverage: legacy semantics plus genuinely new native ids.

    Native nodes that have a legacy counterpart (same semantic key after the
    UL/NL prefix is stripped) must not be counted twice. The remaining native
    ids are added to the numerator and denominator as new semantics.
    """
    legacy_keys = set()
    migrated_keys = set()
    for bucket in ('migrated_direct', 'migrated_translated', 'migrated_rewritten'):
        for entry in report[bucket]:
            key = _normalise_semantics(entry.get('nl_class', ''))
            if key:
                legacy_keys.add(key)
                migrated_keys.add(key)
    for entry in report['deferred']:
        key = _normalise_semantics(entry.get('nl_class', ''))
        if key:
            legacy_keys.add(key)

    # Deprecated legacy nodes still define semantics for deduplication even
    # though they are excluded from the legacy coverage denominator.
    reference_keys = set(legacy_keys)
    for cls in _nodes:
        if not getattr(cls, 'deprecated', False):
            continue
        key = _normalise_semantics(getattr(cls, 'nl_class', ''))
        if key:
            reference_keys.add(key)

    native_ids = {entry['runtime_id'] for entry in report['native_b2']}
    native_ids |= {entry['runtime_id'] for entry in report['native_b3']}
    native_ids |= set(_batch_ids())
    new_native = sorted(runtime_id for runtime_id in native_ids
                        if _normalise_semantics(runtime_id) not in reference_keys)
    deduped = sorted(native_ids - set(new_native))

    report['legacy_semantics'] = len(legacy_keys)
    report['migrated_semantics'] = len(migrated_keys)
    report['native_only_semantics'] = new_native
    report['deduped_native_ids'] = deduped
    return report


def collect():
    buckets = {
        'migrated_direct': [],
        'migrated_translated': [],
        'migrated_rewritten': [],
        'deferred': [],
    }
    for cls in _nodes:
        if cls.__name__.startswith('GameNode'):
            continue
        if getattr(cls, 'deprecated', False):
            continue
        bl_idname = getattr(cls, 'bl_idname', '')
        if not bl_idname:
            continue
        runtime_id = getattr(cls, 'runtime_type_id', '')
        if runtime_id:
            bucket = _classify(bl_idname, runtime_id)
            buckets[bucket].append({'bl_idname': bl_idname,
                                    'runtime_id': runtime_id,
                                    'nl_class': getattr(cls, 'nl_class', '')})
        else:
            buckets['deferred'].append(
                {'bl_idname': bl_idname, 'nl_class': getattr(cls, 'nl_class', '')})

    migrated = sum(len(buckets[key]) for key in buckets if key != 'deferred')
    report = {
        'total': migrated + len(buckets['deferred']),
        'migrated': migrated,
        'migrated_direct': buckets['migrated_direct'],
        'migrated_translated': buckets['migrated_translated'],
        'migrated_rewritten': buckets['migrated_rewritten'],
        'deferred': buckets['deferred'],
        'native_b2': _b2_inventory(),
        'native_b3': _b3_inventory(),
    }
    return _semantic_coverage(report)


def main():
    report = collect()
    print('NATIVE AUDIT total={} migrated={} '
          'migrated_direct={} migrated_translated={} migrated_rewritten={} deferred={}'.format(
              report['total'],
              report['migrated'],
              len(report['migrated_direct']),
              len(report['migrated_translated']),
              len(report['migrated_rewritten']),
              len(report['deferred'])))
    legacy_pct = (100.0 * report['migrated'] / report['total']) if report['total'] else 0.0
    native_only = len(report['native_only_semantics'])
    semantic_covered = report['migrated_semantics'] + native_only
    semantic_total = report['legacy_semantics'] + native_only
    semantic_pct = (100.0 * semantic_covered / semantic_total) if semantic_total else 0.0
    print('LEGACY COVERAGE migrated={} total={} pct={:.1f}%'.format(
        report['migrated'], report['total'], legacy_pct))
    print('SEMANTIC COVERAGE covered={} total={} pct={:.1f}% '
          'legacy_semantics={} native_only_new={} deduped={}'.format(
              semantic_covered,
              semantic_total,
              semantic_pct,
              report['legacy_semantics'],
              native_only,
              len(report['deduped_native_ids'])))
    b3_counts = {}
    for entry in report['native_b3']:
        b3_counts[entry['category']] = b3_counts.get(entry['category'], 0) + 1
    print('NATIVE B3 inventory={} direct={} translated={} rewritten={}'.format(
        len(report['native_b3']),
        b3_counts.get('migrated_direct', 0),
        b3_counts.get('migrated_translated', 0),
        b3_counts.get('migrated_rewritten', 0)))
    print(json.dumps(report, indent=1, default=str))
    return report
