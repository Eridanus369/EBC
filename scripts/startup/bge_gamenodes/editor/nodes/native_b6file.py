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

    # ------------------------------------------------------------ B6.9-Fi
    # Streaming and directory rows mirror `kB6FileStreamSpecs`. Offset is an
    # explicit input (the node keeps no cursor); EOF is Content="" + Eof=True
    # with OnSuccess, an offset past the end is OnFail.
    ('FileReadChunk', 'File Read Chunk (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String'), ('Offset', 'Int')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Content', 'String'),
      ('Size', 'Int'), ('Eof', 'Bool')],
     {'chunk_size': ('int', 'Chunk Size (bytes)', 65536)},
     'Reads one chunk at an explicit byte offset. Chunk size must be 1..1048576; '
     'offset == file size returns empty content with Eof=True, offset > size takes OnFail.'),

    ('FileWriteChunk', 'File Write Chunk (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String'), ('Content', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'chunk_size': ('int', 'Chunk Size (bytes)', 65536),
      'append': ('bool', 'Append', False)},
     'Writes Content in chunk_size slices. Append=False truncates on the first slice of '
     'each activation; Append=True always appends. No file lock across nodes.'),

    ('ListDirectory', 'List Directory (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition'), ('Entries', 'List'),
      ('Directories', 'List'), ('Count', 'Int')],
     {},
     'Direct children only, sorted by name. Empty Path lists the blend root. '
     'Paths are blend-relative; absolute paths, .. and . are rejected.'),

    ('CreateDirectory', 'Create Directory (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'parents': ('bool', 'Create Parents', True)},
     'Creates a blend-relative directory. Parents=True creates missing parents.'),

    ('RemoveDirectory', 'Remove Directory (Native)', 'ACT',
     [('OnEnter', 'Condition'), ('Path', 'String')],
     [('OnSuccess', 'Condition'), ('OnFail', 'Condition')],
     {'recursive': ('bool', 'Recursive', False)},
     'Removes a directory under the blend root. Recursive=False refuses a non-empty '
     'directory; Recursive=True deletes the whole subtree. The root cannot be removed.'),
)

for _entry in NATIVE_B6FILE:
    _make_node(*_entry)
