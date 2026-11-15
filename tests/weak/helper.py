"""Compile real C and patch only ONYO symbol flags and header version."""
import sys
from pathlib import Path
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'archives'))
from support import Harness, ROOT, LINKER, archive, start

ONYO_VERSION_2 = 2
WEAK = 0x80


def patch(h, obj, name, symbols=None, version=ONYO_VERSION_2):
    data = bytearray(obj.read_bytes())
    magic, old_version, _, sections, count, relocs, strings = struct.unpack_from('<7I', data)
    assert magic == 0x4F594E4F and old_version == 1
    table = 64 + sections * 40
    strtab = table + count * 32 + relocs * 24
    assert strtab + strings <= len(data)
    pending = dict(symbols or {})
    for index in range(count):
        offset = table + index * 32
        name_offset, flags = struct.unpack_from('<II', data, offset)
        end = data.index(0, strtab + name_offset, strtab + strings)
        symbol = data[strtab + name_offset:end].decode()
        if symbol in pending:
            change = pending.pop(symbol)
            struct.pack_into('<I', data, offset + 4,
                             flags | WEAK if change is None else change)
    assert not pending, f'symbols absent from {obj}: {pending}'
    struct.pack_into('<I', data, 4, version)
    return h.write(name + '.o', data)


def weak(h, obj, name, *symbols):
    return patch(h, obj, name, dict.fromkeys(symbols))


def library(h, name, *objects):
    return h.write(name + '.a', archive(*(
        (f'm{i}.o/', obj.read_bytes()) for i, obj in enumerate(objects))))
