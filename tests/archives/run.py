#!/usr/bin/env python3
"""Lazy positional archive regressions. Run python3 tests/archives/run.py.

Uses existing onyxcc, onyx-ld and tools/onx-run without rebuilding them.
ONYX_LD overrides the linker executable (for example a sanitizer build).
All generated C, ONYO, ar and ONX files live in a unique temporary directory.
"""
import sys
sys.dont_write_bytecode = True

import os
import subprocess
import tempfile
from pathlib import Path

from support import Harness, LINKER, ROOT, archive, object_bytes, start
from malformed import malformed_cases


def cases(h):
    root = h.compile('root', start('first()', 'int first(void);'))
    first = h.compile('first', 'int middle(void); int first(void) { return middle()+1; }')
    middle = h.compile('middle', 'int last(void); int middle(void) { return last()+1; }')
    last = h.compile('last', 'int last(void) { return 40; }')
    lib = h.write('backwards.a', archive(('last.o/', last.read_bytes()),
                                        ('middle.o/', middle.read_bytes()),
                                        ('first.o/', first.read_bytes())))
    yield 'backwards-fixedpoint-exec', lambda: h.execute([root, lib])

    duplicate = h.compile('unused_duplicate', 'int first(void) { return 99; }')
    poison = h.compile('unused_undefined',
                       'int missing(void); int poison(void) { return missing(); }')
    unused = h.write('unused.a', archive(('first.o/', first.read_bytes()),
                                        ('middle.o/', middle.read_bytes()),
                                        ('last.o/', last.read_bytes()),
                                        ('duplicate.o/', duplicate.read_bytes()),
                                        ('poison.o/', poison.read_bytes())))
    yield 'unused-duplicate-and-undefined-ignored-exec', lambda: h.execute([root, unused])
    standalone = h.compile('standalone', start('42'))
    undefined_only = h.write('undefined_only.a', archive(('poison.o/', poison.read_bytes())))
    yield 'unused-undefined-ignored-exec', lambda: h.execute([standalone, undefined_only])
    yield 'unneeded-archive-exec', lambda: h.execute([standalone, lib])
    entry = h.write('entry.a', archive(('start.o/', standalone.read_bytes())))
    yield 'archive-only-entry-exec', lambda: h.execute([entry])
    prefixed = h.write('metadata_prefix.a', archive(('__.SYMDEFfoo.o/', standalone.read_bytes())))
    yield 'symdef-prefix-is-ordinary-member-exec', lambda: h.execute([prefixed])
    custom = h.compile('custom', start('first()', 'int first(void);', 'launch'))
    customlib = h.write('custom.a', archive(('first.o/', first.read_bytes()),
                                          ('middle.o/', middle.read_bytes()),
                                          ('last.o/', last.read_bytes()),
                                          ('launch.o/', custom.read_bytes())))
    yield 'custom-entry-after-archive-exec', lambda: h.execute([customlib, '-e', 'launch'])
    yield 'last-entry-option-wins-exec', lambda: h.execute(
        ['-e', 'nonexistent', customlib, '--entry', 'launch'])
    yield 'late-entry-overrides-default-exec', lambda: h.execute([entry, '-e', '_start'])

    early = h.write('early.a', archive(('last.o/', last.read_bytes())))
    late = h.write('late.a', archive(('first.o/', first.read_bytes()),
                                    ('middle.o/', middle.read_bytes())))
    yield 'positional-archive-not-globally-rescanned', lambda: h.link(
        [root, early, late], reject=True, diagnostic='last')
    yield 'explicit-archive-repeat-exec', lambda: h.execute([root, early, late, early])
    yield 'archive-before-demand-fails', lambda: h.link(
        [lib, root], reject=True, diagnostic='first')
    yield 'archive-repeated-after-demand-exec', lambda: h.execute([lib, root, lib])

    needs_both = h.compile('needs_both', start('left()+right()',
                                            'int left(void); int right(void);'))
    left = h.compile('left', 'int clash(void) { return 1; } int left(void) { return 20; }')
    right = h.compile('right', 'int clash(void) { return 2; } int right(void) { return 22; }')
    collisions = h.write('collisions.a', archive(('left.o/', left.read_bytes()),
                                               ('right.o/', right.read_bytes())))
    yield 'selected-duplicate-rejected', lambda: h.link(
        [needs_both, collisions], reject=True, diagnostic='clash')

    # 64 total selected objects includes the ordinary root object. Repeated
    # valid unused members must consume no selected-object slots.
    fillers = [(f'u{i}.o/', object_bytes(f'unused_{i}')) for i in range(100)]
    large = h.write('large.a', archive(*fillers, ('first.o/', first.read_bytes()),
                                     ('middle.o/', middle.read_bytes()),
                                     ('last.o/', last.read_bytes())))
    yield 'over-64-members-few-selected-exec', lambda: h.execute([root, large])
    for selected in (63, 64):
        names = [f'bound_{i}' for i in range(selected)]
        declarations = ''.join(f'int {n}(void);' for n in names)
        bounded = h.compile(f'bound_root_{selected}', start(
            '+'.join(f'{n}()' for n in names), declarations))
        boundlib = h.write(f'bound_{selected}.a', archive(
            *((f'b{i}.o/', object_bytes(n)) for i, n in enumerate(names))))
        # Handcrafted members contain ret only: link validation, no execution.
        yield f'selected-total-{selected+1}', lambda r=bounded, a=boundlib, n=selected: h.link(
            [r, a], reject=(n == 64), diagnostic='64' if n == 64 else None)

    longname = b'very_long_archive_member_name_for_first.o'
    for style in ('gnu', 'bsd'):
        if style == 'gnu':
            entries = [('/', b'\0\0\0\0'), ('//', longname + b'/\n'),
                       ('/0', first.read_bytes())]
        else:
            entries = [('__.SYMDEF', b'\0\0\0\0'),
                       (f'#1/{len(longname)}', longname + first.read_bytes())]
        entries += [('middle.o/', middle.read_bytes()), ('last.o/', last.read_bytes()),
                    ('notes/', b'odd')]
        named = h.write(style + '.a', archive(*entries))
        yield style + '-long-name-exec', lambda a=named: h.execute([root, a])
    yield from malformed_cases(h, standalone)


def main():
    for tool in (ROOT / 'onyxcc', LINKER, ROOT / 'tools/onx-run'):
        if not os.access(tool, os.X_OK):
            print(f'ERROR: {tool} missing; build host tools first', file=sys.stderr)
            return 2
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix='onyxcc-archives-') as temporary:
        h = Harness(Path(temporary))
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
    print(f'ARCHIVE RESULTS: {passed} PASS, {failed} FAIL')
    return int(failed != 0)


if __name__ == '__main__':
    sys.exit(main())
