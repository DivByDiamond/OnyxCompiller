/*
 * onyx-ld.c — OnyxOS linker command-line front end.
 *
 * Reads .o object files (from `onyxcc -c`) and .a archives, merges their
 * sections, builds a global symbol table and applies RISC-V relocations.
 * The work is staged in src/tools/ld/: parse.c, link.c and emit.c.
 *
 * Usage:
 *   onyx-ld [-o output.onx] [-e entry_sym] [--ring1] [-v]
 *           file1.o file2.o ... libfoo.a ...
 *
 * Limitations:
 *   - No archive symbol index; members are extracted linearly.
 *   - No weak symbols, no symbol versioning, no DWARF.
 */

#include <stdio.h>
#include <string.h>

#include "ld/ld_internal.h"

static void usage(void) {
    fprintf(stderr,
        "OnyxOS linker — onyx-ld\n"
        "\n"
        "Usage: onyx-ld [options] <input.o>... [lib.a]...\n"
        "\n"
        "Options:\n"
        "  -o, --output <file>   Output .onx file (default: a.onx)\n"
        "  -e, --entry <sym>     Entry symbol (default: _start)\n"
        "      --ring1           Emit RING1 flag (binary runs in root space)\n"
        "  -v, --verbose         Verbose diagnostics\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Inputs:\n"
        "  <input.o>   Compiled object file (from `onyxcc -c`)\n"
        "  libfoo.a    Static archive (collection of .o members)\n"
        "\n"
        "Example:\n"
        "  onyxcc -c -o main.o main.c\n"
        "  onyxcc -c -o util.o util.c\n"
        "  onyx-ld -o prog.onx main.o util.o libfoo.a\n");
}

int main(int argc, char **argv) {
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            g_ld_opts.output = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--entry") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            g_ld_opts.entry_sym = argv[++i];
        } else if (strcmp(argv[i], "--ring1") == 0) {
            g_ld_opts.ring1 = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_ld_opts.verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "onyx-ld: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        } else {
            if (onyx_ld_parse_input(argv[i]) != 0) return 1;
        }
        i++;
    }

    if (g_n_objs == 0) {
        fprintf(stderr, "onyx-ld: no input files\n");
        usage();
        return 1;
    }

    if (onyx_ld_merge_sections() != 0) return 1;
    onyx_ld_compute_addresses();
    if (onyx_ld_build_symbol_table() != 0) return 1;
    if (onyx_ld_apply_relocations() != 0) return 1;
    if (onyx_ld_emit_onx(g_ld_opts.output) != 0) return 1;

    if (g_ld_opts.verbose) {
        fprintf(stderr, "onyx-ld: linked %d object(s) into %s\n", g_n_objs, g_ld_opts.output);
    }
    return 0;
}
