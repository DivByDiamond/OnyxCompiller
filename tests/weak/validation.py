"""Malformed symbol contracts must be rejected before linking."""
import struct
from helper import patch, library


def cases(h, root, original):
    # Keep the complete valid object layout and vary one symbol's flags.
    malformed = {
        'weak-local': 0x99,
        'weak-no-global': 0x91,
        'weak-global-and-local': 0x9d,
        'weak-defined-and-undefined': 0x97,
        'weak-neither-defined-nor-undefined': 0x94,
        'weak-v1': 0x95,
        'unknown-v1-flag': 0x115,
        'unknown-v2-flag': 0x195,
    }
    for name, flags in malformed.items():
        version = 1 if name in ('weak-v1', 'unknown-v1-flag') else 2
        obj = patch(h, original, name, {'pick': flags}, version=version)
        yield 'reject-' + name, lambda o=obj: h.link([root, o], reject=True)
        archive = library(h, name, obj)
        yield 'reject-unused-archive-' + name, lambda a=archive: h.link([root, a], reject=True)
    empty = patch(h, original, 'empty-name-base', {'pick': None})
    data = bytearray(empty.read_bytes())
    sections, symbols = struct.unpack_from('<II', data, 12)
    table = 64 + sections * 40
    for i in range(symbols):
        offset = table + i * 32
        if struct.unpack_from('<I', data, offset + 4)[0] & 0x80:
            struct.pack_into('<I', data, offset, 0)
            break
    else:
        raise AssertionError('weak symbol not found')
    obj = h.write('empty-weak-name.o', data)
    yield 'reject-empty-weak-name', lambda: h.link([root, obj], reject=True)
    archive = library(h, 'empty-name', obj)
    yield 'reject-unused-archive-empty-weak-name', lambda: h.link([root, archive], reject=True)
