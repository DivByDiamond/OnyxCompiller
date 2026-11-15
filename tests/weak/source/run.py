#!/usr/bin/env python3
"""Source weak regressions: python3 tests/weak/source/run.py.

Build host tools first. ONYX_CC and ONYX_LD select alternate host compiler
and linker binaries, including isolated baseline or sanitizer builds.
Reuses tests/archives/support.py Harness; all artifacts use unique temporary
storage. ONYO objects are inspected as emitted, never patched. Direct mode
is tested only for its required rejection, never executed.
"""
import sys
sys.dont_write_bytecode = True

import os
from pathlib import Path
import subprocess
import tempfile

from support_source import SourceHarness, ROOT, LINKER, COMPILER
from declarations import cases as declaration_cases
from runtime import cases as runtime_cases


def main():
    for tool in (COMPILER, LINKER, ROOT / 'tools/onx-run'):
        if not os.access(tool, os.X_OK):
            print(f'ERROR: {tool} missing; build host tools first', file=sys.stderr)
            return 2
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix='onyxcc-weak-source-') as temporary:
        h = SourceHarness(Path(temporary))
        for suite in (declaration_cases, runtime_cases):
            for name, test in suite(h):
                try:
                    test()
                    passed += 1
                    print(f'PASS {name}')
                except (AssertionError, OSError, ValueError,
                        subprocess.TimeoutExpired) as error:
                    failed += 1
                    print(f'FAIL {name}: {error}')
    print(f'SOURCE WEAK RESULTS: {passed} PASS, {failed} FAIL')
    return int(failed != 0)


if __name__ == '__main__':
    sys.exit(main())
