"""Read compiler-emitted objects; never rewrite ONYO flags or versions."""
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'archives'))
from support import Harness, ROOT, LINKER, archive, start

COMPILER = Path(os.environ.get('ONYX_CC', ROOT / 'onyxcc')).resolve()


class SourceHarness(Harness):
    def compile(self, name, source):
        src = self.write(name + '.c', source.encode())
        obj = self.directory / (name + '.o')
        result = self.run([COMPILER, '-N', '-c', src, '-o', obj])
        assert result.returncode == 0, f'compile {name}: {result.stderr}'
        return obj

    def inspect(self, obj, expected, version):
        data = obj.read_bytes()
        assert len(data) >= 64, 'truncated ONYO header'
        magic, actual, _, sections, count, relocs, size = struct.unpack_from('<7I', data)
        assert magic == 0x4F594E4F, 'not ONYO'
        assert actual == version, f'ONYO version {actual}, expected {version}'
        table = 64 + sections * 40
        strings = table + count * 32 + relocs * 24
        assert strings + size <= len(data), 'truncated tables'
        symbols = {}
        for index in range(count):
            offset, flags, section = struct.unpack_from('<IIi', data, table + index * 32)
            assert offset < size, 'bad symbol name offset'
            end = data.index(0, strings + offset, strings + size)
            name = data[strings + offset:end].decode()
            assert name not in symbols, f'duplicate symbol {name}'
            symbols[name] = (flags, section)
            assert not (flags & 0x80) or actual == 2, 'weak in v1'
        for name, flags in expected.items():
            assert name in symbols, f'missing symbol {name}'
            actual_flags, section = symbols[name]
            assert actual_flags == flags, f'{name}: flags {actual_flags:#x}, expected {flags:#x}'
            assert (section == -1) == bool(flags & 2), f'{name}: bad section'
        if version == 1:
            assert all(not flags & 0x80 for flags, _ in symbols.values())

    def reject(self, name, source, direct=False):
        src = self.write(name + '.c', source.encode())
        output = self.directory / (name + ('.onx' if direct else '.o'))
        args = [COMPILER, '-N', src, '-o', output]
        if not direct:
            args.append('-c')
        result = self.run(args)
        assert result.returncode != 0, 'invalid weak declaration accepted'
        assert result.stderr.strip(), 'missing diagnostic'
        assert not output.exists(), 'rejected input emitted output'
        if direct:
            message = result.stderr.lower()
            assert 'weak' in message and '-c' in message, result.stderr

    def library(self, name, obj):
        return self.write(name + '.a', archive(('member.o/', obj.read_bytes())))
