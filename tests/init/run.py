#!/usr/bin/env python3
"""Designated-initializer diagnostics: invalid designators must be rejected.

Run python3 tests/init/run.py after building host tools. ONYX_CC overrides
the compiler. Positive behavior lives in tests/run/44_designated_initializers.c.
"""
import sys
sys.dont_write_bytecode = True

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(os.environ.get('ONYX_CC', ROOT / 'onyxcc')).resolve()

REJECTED = [
    ('unknown-member-local', 'struct S { int a; }; void f(void) { struct S s = { .b = 1 }; }', "no member named 'b'"),
    ('unknown-member-global', 'struct S { int a; }; struct S s = { .b = 1 };', "no member named 'b'"),
    ('index-out-of-range-local', 'void f(void) { int a[2] = { [2] = 1 }; }', 'out of range'),
    ('index-out-of-range-global', 'int a[2] = { [5] = 1 };', 'out of range'),
    ('negative-index', 'int a[2] = { [-1] = 1 };', 'out of range'),
    ('member-designator-on-array', 'int a[2] = { .x = 1 };', 'non-struct'),
    ('index-designator-on-struct', 'struct S { int a; }; struct S s = { [0] = 1 };', 'non-array'),
    ('missing-equals', 'struct S { int a; }; struct S s = { .a 1 };', "expected '='"),
]


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as directory:
        for name, source, message in REJECTED:
            path = Path(directory) / (name + '.c')
            path.write_text(source + '\nvoid _start(void) { for (;;) {} }\n')
            result = subprocess.run(
                [str(COMPILER), '-N', '-o', str(Path(directory) / (name + '.onx')), str(path)],
                capture_output=True, text=True, timeout=60)
            output = result.stdout + result.stderr
            if result.returncode == 0 or message not in output:
                print(f'FAIL {name}: rc={result.returncode} output={output.strip()!r}')
                failures += 1
            else:
                print(f'PASS rejects {name}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
