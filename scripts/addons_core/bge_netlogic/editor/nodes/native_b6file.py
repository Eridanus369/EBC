"""B6.4 advanced file nodes (table driven).

Mirrors `kB6FileSpecs` in `KX_FunctionNodesB6File.cpp`. See
docs/B6_FILE_ADVANCED.md.
"""

from .native_batch import _make_node

_ALGORITHM = {
    'items': [
        ('xor', 'XOR', 'Base64 wrapped XOR stream (obfuscation only)'),
        ('aes', 'AES (not available)', 'Requires a third-party crypto library; reports an error instead of falling back to XOR'),
    ],
    'default': 'xor',
}

NATIVE_B6FILE = (
    ('SaveGameBinary', 'Save Game Binary (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Slot', 'String'), ('Compress', 'Bool')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {},
     'Versioned binary save with CRC32; same schema as the JSON save. '
     'Compress=True deflates the payload with zlib (version 2); the 16-byte header layout is unchanged.'),

    ('SaveGameSlots', 'Save Game Slots (Native)', 'PAR',
     [],
     [('Slots', 'List'), ('Count', 'Int')],
     {},
     'Names of the JSON and binary save slots under <blend>/save/.'),

    ('EncryptData', 'Encrypt Data (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Data', 'String'), ('Key', 'String')],
     [('OnDone', 'Condition'), ('Encrypted', 'String')],
     {'algorithm': ('enum', 'Algorithm', _ALGORITHM)},
     'XOR + Base64 obfuscation only: hides bytes from casual reading, not resistant to attacks. AES needs a third-party library and is not enabled.'),

    ('DecryptData', 'Decrypt Data (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Encrypted', 'String'), ('Key', 'String')],
     [('OnDone', 'Condition'), ('Data', 'String')],
     {'algorithm': ('enum', 'Algorithm', _ALGORITHM)},
     'Inverse of Encrypt Data (XOR + Base64 obfuscation only; AES is not enabled).'),

    ('FileReadLines', 'File Read Lines (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Lines', 'List')],
     {},
     'Splits on newlines, strips CRLF, keeps UTF-8 bytes intact.'),
)

for _entry in NATIVE_B6FILE:
    _make_node(*_entry)
