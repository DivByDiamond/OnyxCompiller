#!/usr/bin/env python3
"""Linker-only weak regressions: python3 tests/weak/run.py.

No C attributes are used. Existing host tools are required; ONYX_LD may
select an isolated baseline or sanitizer linker. All artifacts use a unique
temporary directory. A nonzero exit means at least one regression failed.
"""
import sys
sys.dont_write_bytecode = True
import os
from pathlib import Path
import subprocess
import tempfile

from helper import Harness, ROOT, LINKER, patch, weak, library, start
from validation import cases as validation_cases


def cases(h):
    control = h.compile('control', start('42'))
    yield 'strong-v1-control', lambda: h.execute([control])
    v2 = patch(h, control, 'control-v2')
    yield 'strong-v2-control', lambda: h.execute([v2])
    root = h.compile('root', start('pick()+*dp+fp()', '''
        int pick(void); extern int value;
        int (*fp)(void) = pick; int *dp = &value;
    '''))
    strong = h.compile('strong', 'int pick(void) { return 14; } int value = 14;')
    original = h.compile('weak-original', 'int pick(void) { return 3; } int value = 3;')
    fallback = weak(h, original, 'fallback', 'pick', 'value')
    for label, objects in [('weak-first', [fallback, strong]),
                           ('strong-first', [strong, fallback])]:
        yield label, lambda o=objects: h.execute([root, *o])
    other_src = h.compile('other-original', 'int pick(void) { return 14; } int value = 14;')
    other = weak(h, other_src, 'other', 'pick', 'value')
    small_root = h.compile('small-root', start('pick()+*dp+fp()+33', '''
        int pick(void); extern int value;
        int (*fp)(void) = pick; int *dp = &value;
    '''))
    yield 'multiple-weak-first-wins', lambda: h.execute([small_root, fallback, other])
    yield 'multiple-weak-reversed-first-wins', lambda: h.execute([root, other, fallback])

    owner_src = h.compile('owner-original', '''
        int pick(void) { return 3; } int value = 3;
        int (*owner_fp)(void) = pick; int *owner_dp = &value;
        int inspect(void) { return pick()+value+owner_fp()+*owner_dp; }
    ''')
    owner = weak(h, owner_src, 'owner', 'pick', 'value')
    owner_root = h.compile('owner-root', start('inspect()-14', 'int inspect(void);'))
    for label, objects in [('late', [owner, strong]), ('early', [strong, owner])]:
        yield 'weak-owner-self-references-' + label, lambda o=objects: h.execute([owner_root, *o])
    first_owner = h.compile('first-owner', start('inspect()+30', 'int inspect(void);'))
    yield 'losing-weak-owner-references-first-weak', lambda: h.execute([first_owner, fallback, owner])
    external_src = h.compile('external-original', 'int helper(void) { return 42; }')
    external = weak(h, external_src, 'external', 'helper')
    strong_user = h.compile('strong-user', '''
        int helper(void); int (*helper_fp)(void) = helper;
        int call_helper(void) { return helper_fp()+helper(); }
    ''')
    ext_root = h.compile('external-root', start('call_helper()-42', 'int call_helper(void);'))
    yield 'strong-owner-references-external-weak', lambda: h.execute([ext_root, strong_user, external])

    missing_src = h.compile('missing-original', start('42*(fp == 0 && dp == 0)', '''
        int absent(void); extern int absent_data;
        int (*fp)(void) = absent; int *dp = &absent_data;
    '''))
    missing = weak(h, missing_src, 'missing', 'absent', 'absent_data')
    yield 'unresolved-weak-pointers-zero', lambda: h.execute([missing])
    poison = h.compile('poison', '''
        int unavailable(void);
        int absent(void) { return unavailable(); } int absent_data = 7;
    ''')
    poison_lib = library(h, 'poison', poison)
    yield 'weak-undefined-does-not-pull-archive', lambda: h.execute([missing, poison_lib])
    libweak = library(h, 'weak', other)
    yield 'strong-undefined-pulls-weak-archive', lambda: h.execute([root, libweak])
    libstrong = library(h, 'strong', strong)
    yield 'existing-weak-stops-strong-archive-pull', lambda: h.execute([small_root, fallback, libstrong])
    selected = h.compile('selected', '''
        int pick(void) { return 14; } int value = 14;
        int needed(void) { return 0; }
    ''')
    need_root = h.compile('need-root', start('pick()+value+pick()+needed()', '''
        int pick(void); extern int value; int needed(void);
    '''))
    selected_lib = library(h, 'selected', selected)
    yield 'selected-member-strong-overrides-weak', lambda: h.execute([need_root, fallback, selected_lib])
    weakentry = weak(h, control, 'weak-entry', '_start')
    yield 'weak-defined-entry', lambda: h.execute([weakentry])
    yield 'weak-defined-archive-entry', lambda: h.execute([library(h, 'entry', weakentry)])
    entry_src = h.compile('entry-original', '''
        void launch(void); void (*entry_ptr)(void) = launch;
        int dummy(void) { return entry_ptr == 0; }
    ''')
    entry = weak(h, entry_src, 'undefined-entry', 'launch')
    yield 'weak-undefined-entry-required', lambda: h.link(
        ['-e', 'launch', entry], reject=True, diagnostic='launch')
    yield from validation_cases(h, control, original)


def main():
    for tool in (ROOT / 'onyxcc', LINKER, ROOT / 'tools/onx-run'):
        if not os.access(tool, os.X_OK):
            print(f'ERROR: {tool} missing; build host tools first', file=sys.stderr)
            return 2
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix='onyxcc-weak-') as directory:
        h = Harness(Path(directory))
        try:
            for name, test in cases(h):
                try:
                    test()
                    passed += 1
                    print(f'PASS {name}')
                except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
                    failed += 1
                    print(f'FAIL {name}: {error}')
        except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
            failed += 1
            print(f'FAIL fixture setup: {error}')
    print(f'WEAK RESULTS: {passed} PASS, {failed} FAIL')
    return int(failed != 0)


if __name__ == '__main__':
    sys.exit(main())
