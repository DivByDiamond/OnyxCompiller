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
    ('two-ranges', 'int a[4][4] = { [0 ... 1][0 ... 1] = 1 };', 'only one range'),
    ('descending-range', 'int a[4] = { [3 ... 1] = 1 };', 'out of range'),
    ('missing-equals', 'struct S { int a; }; struct S s = { .a 1 };', "expected '='"),
]


# Multi-file programs: data relocations must carry the address addend.
LINKED = [
    ('address-addend-across-files', [
        'int arr[5] = {0, 1, 2, 3, 4};\n'
        'extern int use(void);\n'
        'void _start(void) { __ecall1(3, use() != 7); for (;;) {} }\n',
        'extern int arr[];\n'
        'int *const p[3] = { [2] = &arr[3], [0] = arr + 1 };\n'
        'int use(void) { return (p[2] == &arr[3]) + (p[0] == &arr[1]) * 2 + (p[1] == 0) * 4; }\n']),
    ('file-scope-literals-across-files', [
        'extern int use(void); extern int *const q;\n'
        'int *const q2 = (int[]){ 8, 9 };\n'
        'void _start(void) { __ecall1(3, use() != 7 || q2[1] != 9 || q[0] != 5); for (;;) {} }\n',
        'int *const q = (int[]){ 5, 6, 7 };\n'
        'int use(void) { return q[2]; }\n']),
]


def run_linked(directory):
    failures = 0
    runner = ROOT / 'tools/onx-run'
    for name, sources in LINKED:
        paths = []
        for index, text in enumerate(sources):
            path = Path(directory) / f'{name}{index}.c'
            path.write_text(text)
            paths.append(str(path))
        image = Path(directory) / (name + '.onx')
        built = subprocess.run([str(COMPILER), '-N', '-o', str(image)] + paths,
                               capture_output=True, text=True, timeout=60)
        ran = built.returncode == 0 and subprocess.run(
            [str(runner), str(image)], capture_output=True, timeout=60)
        if not ran or ran.returncode != 0:
            print(f'FAIL {name}: {built.stderr.strip()!r}')
            failures += 1
        else:
            print(f'PASS links {name}')
    return failures


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as directory:
        failures += run_linked(directory)
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
