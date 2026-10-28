#!/usr/bin/env python3
"""Build with OnyxCC, then execute that compiler and its freestanding output."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)


def run(arguments, timeout=60, success=True):
    result = subprocess.run([str(a) for a in arguments], capture_output=True,
                            text=True, timeout=timeout)
    if success and result.returncode:
        raise AssertionError(f"{arguments}: exit {result.returncode}\n"
                             f"{result.stdout}\n{result.stderr}")
    return result


with tempfile.TemporaryDirectory(prefix="onyx-selfhost-") as directory:
    temporary = Path(directory)
    compiler = temporary / "onyxcc.onx"
    command = run(["make", "-n", "selfhost"]).stdout.splitlines()[0]
    arguments = shlex.split(command)
    arguments[arguments.index("onyxcc_self.onx")] = str(compiler)
    run(arguments)
    runner = ROOT / "tools/onx-run"
    help_result = run([runner, compiler, "--help"])
    assert "Usage: onyxcc" in help_result.stdout + help_result.stderr

    macro_source = temporary / "macros.c"
    macro_source.write_text("#define KEEP(a,b) b\nKEEP(x,int)\n"
                            "KEEP(x,double)\nKEEP(x,marker)\n")
    expansion = run([runner, compiler, "-N", "-E", macro_source])
    assert expansion.stdout.split() == ["int", "double", "marker"], expansion.stdout

    (temporary / "marker.h").write_text('#define MARKER "selfhost-output-ok\\n"\n')
    source = temporary / "hello.c"
    source.write_text('''#include "marker.h"
void _start(void) {
    __ecall4(1, 1, (long)MARKER, sizeof(MARKER) - 1);
    __ecall2(3, 0);
}
''')
    output = temporary / "hello.onx"
    run([runner, compiler, "-N", "-I", temporary, "-o", output, source])
    assert output.read_bytes()[:4] == b"ONX1", "invalid ONX output"
    result = run([runner, output], timeout=15)
    assert result.stdout == "selfhost-output-ok\n", result.stdout

    # Attached short options exercise a different getopt path. Rewriting an
    # existing longer file must truncate, rather than leave trailing bytes.
    original = output.read_bytes()
    output.write_bytes(original + b"unexpected trailing bytes")
    run([runner, compiler, "-N", f"-I{temporary}", f"-o{output}", source])
    assert output.read_bytes() == original
    run([runner, compiler, "--nostdlib", f"--include={temporary}",
         f"--output={output}", source])
    assert output.read_bytes() == original

    bad_source = temporary / "bad.c"
    bad_source.write_text("void _start(void) { return unknown_identifier; }\n")
    bad_output = temporary / "bad.onx"
    result = run([runner, compiler, "-N", "-o", bad_output, bad_source], success=False)
    assert result.returncode == 1 and "error:" in result.stderr
    assert not bad_output.exists()

    canary = temporary / "heap.c"
    canary.write_text('''#include "core/compat.h"
int main(int argc, char **argv) {
    unsigned char *p = malloc(16);
    unsigned char *guard = malloc(16);
    if (!p || !guard) return 1;
    unsigned char *original_pointer = p;
    for (int i = 0; i < 16; i++) { p[i] = i; guard[i] = 77; }
    p = realloc(p, 4096);
    if (!p) return 2;
    for (int i = 0; i < 16; i++) if (p[i] != i || guard[i] != 77) return 3;
    free(p); free(guard);
    p = malloc(16);
    if (p != original_pointer) return 15;
    free(p);
    for (int i = 0; i < 256; i++) {
        p = calloc(1024, 16);
        if (!p) return 4;
        for (int j = 0; j < 16384; j++) if (p[j]) return 5;
        free(p);
    }
    if (calloc((unsigned long)-1, 2)) return 6;
    if (argc != 2) return 7;
    FILE *file = fopen(argv[1], "w+b");
    if (!file || fwrite("abc", 1, 3, file) != 3) return 8;
    if (fseek(file, 0, 2) != 0 || ftell(file) != 3 || fseek(file, 0, 0) != 0) return 9;
    char bytes[4];
    if (fread(bytes, 1, 3, file) != 3 || memcmp(bytes, "abc", 3)) return 10;
    if (fread(NULL, 0, 1, file) || fwrite(NULL, 0, 1, file)) return 11;
    fclose(file);
    file = fopen(argv[1], "a");
    if (!file || fwrite("d", 1, 1, file) != 1) return 12;
    fclose(file);
    file = fopen(argv[1], "rb");
    if (!file || fread(bytes, 1, 4, file) != 4 || memcmp(bytes, "abcd", 4)) return 13;
    fclose(file);
    file = fopen(argv[1], "w");
    if (!file || fseek(file, 0, 2) || ftell(file) != 0) return 14;
    fclose(file);
    unsigned char from[128], to[128];
    for (int i = 0; i < 128; i++) from[i] = (unsigned char)(i * 3);
    for (int offset = 0; offset < 8; offset++) for (int n = 0; n < 65; n++) {
        memset(to, 77, 128);
        memcpy(to + offset, from + 7 - offset, n);
        for (int j = 0; j < n; j++) if (to[offset+j] != from[7-offset+j]) return 16;
        for (int j = 0; j < offset; j++) if (to[j] != 77) return 17;
        if (to[offset+n] != 77) return 18;
        memset(to + offset, 0x123, n);
        for (int j = 0; j < n; j++) if (to[offset+j] != 0x23) return 19;
        if (to[offset+n] != 77) return 20;
    }
    if (strcmp("same", "same") || strcmp("a", "b") >= 0 || strcmp("ab", "a") <= 0) return 21;
    long before = __ecall2(13, 0);
    void *chunks[16];
    for (int i = 0; i < 16; i++) {
        chunks[i] = malloc(65560);
        if (!chunks[i]) return 22;
    }
    long after = __ecall2(13, 0);
    if (after - before > 16 * 80000) return 23;
    for (int i = 0; i < 16; i++) free(chunks[i]);
    return 0;
}
''')
    heap_output = temporary / "heap.onx"
    run(["./onyxcc", "-DCC_FREESTANDING", "-N", "-I", "include",
         "-I", "include/core", "-e", "_start", "-o", heap_output,
         canary, "src/core/shim.c"])
    run([runner, heap_output, temporary / "shim-io.txt"], timeout=30)
    print("selfhost-runtime-ok: help, compile/run, includes/options, output truncation, rejection, heap")
