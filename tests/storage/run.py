#!/usr/bin/env python3
"""Static-local storage regressions; no libc or source/object rewriting.

Run python3 tests/storage/run.py after building host tools.
ONYX_CC overrides the compiler; ONYX_LD overrides the separate-object linker.
ONYX_ROOT optionally selects the checkout (useful for a draft outside it).
Every test gets a unique temporary directory and executes its generated ONX.
"""
import sys
sys.dont_write_bytecode = True

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(os.environ.get('ONYX_ROOT', Path(__file__).resolve().parents[2]))
COMPILER = Path(os.environ.get('ONYX_CC', ROOT / 'onyxcc')).resolve()
LINKER = Path(os.environ.get('ONYX_LD', ROOT / 'onyx-ld')).resolve()
RUNNER = ROOT / 'tools/onx-run'


def start(body):
    return 'void _start(void) { ' + body + '''
        __ecall1(3, !ok); for (;;) {} }
    '''


def fixtures():
    yield 'single-static-control', ['''
        int count(void) { static int n = 10; n = n + 1; return n; }
    ''' + start('int a = count(); int b = count(); int ok = a == 11 && b == 12;')]
    yield 'self-address-initializer', ['''
        int step(void) {
            static void *p = &p;
            int ok = p == &p;
            p = 0;
            return ok;
        }
    ''' + start('int a = step(); int b = step(); int ok = a == 1 && b == 0;')]
    yield 'static-arrays-zero-and-braced-initialization', ['''
        int step(void) {
            static int zero[3];
            static int values[3] = {10, 20};
            int result = zero[0] + zero[1] + zero[2];
            result = result + values[0] + values[1] + values[2];
            zero[0] = zero[0] + 1; zero[2] = zero[2] + 2;
            values[1] = values[1] + 4;
            return result;
        }
    ''' + start('int a = step(); int b = step(); int ok = a == 30 && b == 37;')]
    yield 'static-pointer-to-other-static', ['''
        int step(void) {
            static int value = 10;
            static int *p = &value;
            if (p != &value) return -1;
            *p = *p + 1;
            return value;
        }
    ''' + start('int a = step(); int b = step(); int ok = a == 11 && b == 12;')]
    yield 'static-array-decay-pointer-initializer', ['''
        int step(void) {
            static int values[2] = {10, 20};
            static int *p = values;
            if (p != &values[0]) return -1;
            p[1] = p[1] + 1;
            return values[1];
        }
    ''' + start('int a = step(); int b = step(); int ok = a == 21 && b == 22;')]
    yield 'global-initializer-after-parameter-scope', ['''
        int f(int x) { return x; }
        int x = 42;
        int *p = &x;
    ''' + start('int ok = *p == 42;')]
    yield 'nested-shadow-distinct-persistent', ['''
        int step(void) {
            static int n = 1;
            int *outer = &n;
            int inside;
            { static int n = 10; n = n + 2;
              if (&n == outer) return -1;
              inside = n; }
            n = n + 1;
            return n * 100 + inside;
        }
    ''' + start('int a = step(); int b = step(); int ok = a == 212 && b == 314;')]
    yield 'sibling-blocks-distinct-persistent', ['''
        int step(void) {
            int a; int b; int *first;
            { static int n = 1; n = n + 1; a = n; first = &n; }
            { static int n = 20; n = n + 2; b = n;
              if (&n == first) return -1; }
            return a * 100 + b;
        }
    ''' + start('int a = step(); int b = step(); int ok = a == 222 && b == 324;')]
    # Both identifiers remain legal and distinct within the 63-character
    # frontend limit; only compiler-generated backing names exceed it.
    first = 'f' + 'a' * 61 + 'x'
    second = 'f' + 'a' * 61 + 'y'
    assert len(first) == len(second) == 63
    yield 'long-function-names-distinct-suffix', [f'''
        int {first}(void) {{ static int n = 1; n = n + 1; return n; }}
        int {second}(void) {{ static int n = 20; n = n + 2; return n; }}
    ''' + start(f'int a = {first}(); int b = {second}(); '
                f'int c = {first}(); int d = {second}(); '
                'int ok = a == 2 && b == 22 && c == 3 && d == 24;')]
    left = 'v' + 'b' * 61 + 'x'
    right = 'v' + 'b' * 61 + 'y'
    assert len(left) == len(right) == 63
    yield 'long-local-names-distinct-suffix', [f'''
        int step(void) {{
            static int {left} = 1;
            static int {right} = 20;
            {left} = {left} + 1; {right} = {right} + 2;
            if (&{left} == &{right}) return -1;
            return {left} * 100 + {right};
        }}
    ''' + start('int a = step(); int b = step(); int ok = a == 222 && b == 324;')]
    # Reused file-local function names must have independent backing storage.
    shared = ['''
        static int step(void) { static int n = 1; n = n + 1; return n; }
        int left(void) { return step(); }
    ''', '''
        static int step(void) { static int n = 20; n = n + 2; return n; }
        int right(void) { return step(); }
    ''']
    entry = '''int left(void); int right(void);
    ''' + start('int a = left(); int b = right(); int c = left(); int d = right(); '
                'int ok = a == 2 && b == 22 && c == 3 && d == 24;')
    # Exactly two input files in direct mode; the entry lives in the second.
    yield 'two-files-static-function-private', [shared[0], shared[1] + entry]


def run(args, directory):
    result = subprocess.run([str(a) for a in args], cwd=directory,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (f'exit {result.returncode}: {args}\n'
                                    f'{result.stdout}{result.stderr}')
    assert 'AddressSanitizer' not in result.stderr, result.stderr
    assert 'runtime error:' not in result.stderr, result.stderr
    return result


def check(source_files, mode, directory):
    sources = []
    for index, source in enumerate(source_files):
        path = directory / f'unit{index}.c'
        path.write_text(source)
        sources.append(path)
    output = directory / 'program.onx'
    if mode == 'direct':
        run([COMPILER, '-N', '-o', output, *sources], directory)
    else:
        objects = []
        for source in sources:
            obj = source.with_suffix('.o')
            run([COMPILER, '-N', '-c', source, '-o', obj], directory)
            objects.append(obj)
        run([LINKER, '-o', output, *objects], directory)
    result = run([RUNNER, output], directory)
    assert not result.stdout, f'unexpected stdout: {result.stdout!r}'


def negative_fixtures():
    yield 'reject-static-pointer-to-automatic', '''
        int f(void) { int x = 1; static int *p = &x; return *p; }
    '''
    yield 'reject-same-scope-duplicate-static', '''
        int f(void) { static int x = 1; static int x = 2; return x; }
    '''


def check_rejection(source, mode, directory):
    path = directory / 'invalid.c'
    path.write_text(source + start('int ok = 1;'))
    output = directory / ('invalid.o' if mode == 'objects' else 'invalid.onx')
    args = [COMPILER, '-N', path, '-o', output]
    if mode == 'objects':
        args.append('-c')
    result = subprocess.run([str(a) for a in args], cwd=directory,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode > 0, f'expected rejection: {result.returncode}'
    assert result.stderr.strip(), 'missing diagnostic'
    assert 'AddressSanitizer' not in result.stderr, result.stderr
    assert 'runtime error:' not in result.stderr, result.stderr
    assert not output.exists(), 'rejected source emitted output'


def main():
    for tool in (COMPILER, LINKER, RUNNER):
        if not os.access(tool, os.X_OK):
            print(f'ERROR: missing executable {tool}', file=sys.stderr)
            return 2
    passed = failed = 0
    for name, sources in fixtures():
        for mode in ('direct', 'objects'):
            with tempfile.TemporaryDirectory(prefix='onyxcc-storage-') as temporary:
                try:
                    check(sources, mode, Path(temporary))
                    passed += 1
                    print(f'PASS {name}-{mode}')
                except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
                    failed += 1
                    print(f'FAIL {name}-{mode}: {error}')
    for name, source in negative_fixtures():
        for mode in ('direct', 'objects'):
            with tempfile.TemporaryDirectory(prefix='onyxcc-storage-negative-') as temporary:
                try:
                    check_rejection(source, mode, Path(temporary))
                    passed += 1
                    print(f'PASS {name}-{mode}')
                except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
                    failed += 1
                    print(f'FAIL {name}-{mode}: {error}')
    print(f'STORAGE RESULTS: {passed} PASS, {failed} FAIL')
    return int(failed != 0)


if __name__ == '__main__':
    sys.exit(main())
