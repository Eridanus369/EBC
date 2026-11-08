"""B6.4 metadata audit check (no bpy required)."""

import os
import sys

REPO = '/home/eridanusepsilon/Code/EBC/EBC5.3'
sys.path.insert(0, os.path.join(REPO, 'scripts/startup/bge_gamenodes/generator'))
sys.path.insert(0, os.path.join(REPO, 'scripts/startup/bge_gamenodes/editor/nodes'))

import port_contract as pc  # noqa: E402
import native_metadata_audit as audit_mod  # noqa: E402
from native_metadata_gen import NATIVE_METADATA_GEN  # noqa: E402

metadata = dict(NATIVE_METADATA_GEN)
tables = audit_mod.load_editor_tables()
registered = audit_mod.registered_ids_from_sources()
runtime_ids = set(pc.load_cpp_specs()) | set(pc.load_signature_specs())
report = audit_mod.audit(metadata, registered, tables,
                         pc.load_dynamic_runtime_ids(), runtime_ids)

print('checked={} missing={} unknown={} port_mismatch={} config_mismatch={}'.format(
    len(report['checked']), len(report['missing']), len(report['unknown']),
    len(report['port_mismatch']), len(report['config_mismatch'])))
for key in ('missing', 'unknown', 'port_mismatch', 'config_mismatch'):
    if report[key]:
        print(key, report[key][:20])
sys.exit(0 if not any(report[key] for key in ('missing', 'unknown',
                                              'port_mismatch', 'config_mismatch')) else 1)
