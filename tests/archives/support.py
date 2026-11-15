"""Deterministic ar fixtures and subprocess harness; no host ar required."""
from pathlib import Path
import os
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LINKER = Path(os.environ.get('ONYX_LD', ROOT / 'onyx-ld')).resolve()


def member(name, data):
    name = name.encode('ascii')
    assert len(name) <= 16
    header = (name.ljust(16) + b'0'.ljust(12) + b'0'.ljust(6)
              + b'0'.ljust(6) + b'100644'.ljust(8)
              + str(len(data)).encode().ljust(10) + b'`\n')
    return header + data + (b'\n' if len(data) % 2 else b'')


def archive(*members):
    return b'!<arch>\n' + b''.join(member(n, d) for n, d in members)


def object_bytes(name):
    """Valid tiny ONYO definition, used only for selection/count bounds."""
    strings = b'\0' + name.encode() + b'\0'
    header = struct.pack('<8I', 0x4F594E4F, 1, 0, 1, 1, 0,
                         len(strings), 0xFFFFFFFF) + bytes(32)
    section = struct.pack('<4I2Q', 0, 0, 5, 4, 4, 136 + len(strings)) + bytes(8)
    symbol = struct.pack('<IIiIQQ', 1, 0x15, 0, 0, 0, 4)
    return header + section + symbol + strings + b'\x67\x80\x00\x00'


class Harness:
    def __init__(self, directory):
        self.directory = directory
        self.serial = 0

    def write(self, name, data):
        path = self.directory / name
        path.write_bytes(data)
        return path

    def run(self, args):
        result = subprocess.run([str(a) for a in args], cwd=self.directory,
                                capture_output=True, text=True, timeout=20)
        if result.returncode < 0:
            raise AssertionError(f'process crashed: {args}\n{result.stderr}')
        if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
            raise AssertionError(result.stderr)
        return result

    def compile(self, name, source):
        src = self.write(name + '.c', source.encode())
        obj = self.directory / (name + '.o')
        result = self.run([ROOT / 'onyxcc', '-N', '-c', src, '-o', obj])
        if result.returncode:
            raise AssertionError(f'compile {name}: {result.stderr}')
        return obj

    def link(self, args, reject=False, diagnostic=None):
        self.serial += 1
        output = self.directory / f'link-{self.serial}.onx'
        result = self.run([LINKER, '-o', output, *args])
        if reject:
            if result.returncode == 0:
                raise AssertionError('link unexpectedly succeeded')
            if not result.stderr.strip():
                raise AssertionError('rejection lacks diagnostic')
            if diagnostic and diagnostic not in result.stderr.lower():
                raise AssertionError(f'expected {diagnostic!r}: {result.stderr}')
            if output.exists():
                raise AssertionError('rejected link emitted output')
        elif result.returncode:
            raise AssertionError(f'link exit {result.returncode}: {result.stderr}')
        return output

    def execute(self, args):
        output = self.link(args)
        result = self.run([ROOT / 'tools/onx-run', output])
        if result.returncode or result.stdout:
            raise AssertionError(f'run exit {result.returncode}, '
                                 f'stdout={result.stdout!r}: {result.stderr}')


def start(expression, declarations='', entry='_start'):
    return (declarations + f'\nvoid {entry}(void) {{ '
            f'__ecall2(3, ({expression}) != 42); for (;;) {{}} }}\n')
