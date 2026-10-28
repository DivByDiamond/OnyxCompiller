#!/usr/bin/env python3
"""Driver bounds and multi-translation-unit regressions; run from any directory.

Use --sanitize to build a temporary ASan/UBSan compiler and repeat the checks.
The installed compiler and repository outputs are never replaced.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(argv, env, timeout=90):
    result = subprocess.run([str(arg) for arg in argv], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=timeout)
    if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise AssertionError(result.stderr[-6000:])
    return result


def require_success(result, label):
    if result.returncode:
        raise AssertionError(f'{label}: exit {result.returncode}\n{result.stderr[-6000:]}')


def limits():
    header = (ROOT / 'include/core/cc.h').read_text()
    return tuple(int(re.search(r'#define\s+' + name + r'\s+(\d+)', header)[1])
                 for name in ('CC_MAX_INPUT_FILES', 'CC_LIB_SOURCE_COUNT'))


def build_sanitized(directory, env):
    makefile = (ROOT / 'Makefile').read_text()
    source_block = re.search(r'ONYXCC_SRCS\s*=\s*(.*?)\n\n', makefile, re.S)[1]
    sources = re.findall(r'src/[\w/.-]+\.c', source_block)
    assert sources, 'Makefile compiler source list missing'
    output = directory / 'onyxcc-sanitized'
    command = shlex.split(os.environ.get('CC', 'cc'))
    command += ['-std=c99', '-g', '-O1', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-Iinclude',
                '-o', output, *sources]
    require_success(run(command, env, timeout=180), 'sanitizer build')
    return output


def check_compiler(compiler, directory, env):
    maximum, library_count = limits()
    assert maximum > 40 and 0 < library_count < maximum
    units = []
    for index in range(maximum + 1):
        source = directory / f'unit_{index:03d}.c'
        source.write_text(f'int unit_{index}(void) {{ return {index}; }}\n')
        units.append(source)
    # This declaration crosses translation units. Requiring the very last
    # source prevents silent input truncation from passing execution checks.
    main = directory / 'main.c'
    output = directory / 'driver.onx'
    for count in (40, maximum - library_count):
        last = count - 2
        main.write_text('#include <stdio.h>\n'
                        f'int unit_{last}(void);\n'
                        f'int main(void) {{ printf("%d\\n", unit_{last}()); return 0; }}\n')
        result = run([compiler, '-o', output, main, *units[:count - 1]], env)
        require_success(result, f'{count} inputs with libc')
        result = run([ROOT / 'tools/onx-run', output], env, timeout=15)
        require_success(result, 'multi-file execution')
        assert result.stdout == f'{last}\n', (count, result.stdout)
        print(f'PASS {count} inputs with libc, last translation unit executed')

    # Without libc every slot belongs to the caller. Verbose processing
    # records verify that even empty/unreferenced units are retained.
    result = run([compiler, '-N', '-v', '-o', output, *units[:maximum]], env)
    require_success(result, f'{maximum} inputs without libc')
    assert f'processing [{maximum}/{maximum}]' in result.stderr, result.stderr[-2000:]
    print(f'PASS {maximum} inputs without libc, all units processed')

    for options, count in (([], maximum - library_count + 1), (['-N'], maximum + 1)):
        rejected = directory / 'rejected.onx'
        result = run([compiler, *options, '-o', rejected, *units[:count]], env)
        assert result.returncode > 0, (count, result.returncode)
        assert 'too many input files' in result.stderr, result.stderr[-2000:]
        assert not rejected.exists(), 'rejected invocation produced output'
        print(f'PASS rejects {count} inputs {"without" if options else "with"} libc')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT / 'onyxcc')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    env = dict(os.environ, ONYXCC_LIB=str(ROOT / 'libonyxc'))
    # Existing compiler arenas deliberately survive process exit; these
    # tests focus on bounds errors and undefined behavior, not leak accounting.
    if args.sanitize:
        env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    with tempfile.TemporaryDirectory(prefix='onyxcc-driver-') as temporary:
        directory = Path(temporary)
        compiler = build_sanitized(directory, env) if args.sanitize else args.compiler.resolve()
        check_compiler(compiler, directory, env)


if __name__ == '__main__':
    main()
