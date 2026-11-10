/*
 * driver/opts.c — CLI surface of the OnyxCC driver.
 *
 * Kept separate from main.c so the pipeline file stays within the
 * project's 250-line budget; the option table is the single source of
 * truth for both getopt_long() and usage().
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/driver.h"

const char *driver_optstring(void) {
    /* Short options; long-only switches use long_opts entries below. */
    return "I:D:o:e:cf:vhENS";
}

struct option *driver_long_options(void) {
    static struct option long_opts[] = {
        {"include",    required_argument, 0, 'I'},
        {"define",     required_argument, 0, 'D'},
        {"output",     required_argument, 0, 'o'},
        {"entry",      required_argument, 0, 'e'},
        {"compile",    no_argument,       0, 'c'},
        {"ring1",      no_argument,       0, 'R'},
        {"nostdlib",   no_argument,       0, 'N'},
        {"verbose",    no_argument,       0, 'v'},
        {"dump-tokens",no_argument,       0, 'T'},
        {"preprocess", no_argument,       0, 'E'},
        {"syntax-only", no_argument,      0, 'S'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0},
    };
    return long_opts;
}

void driver_usage(void) {
    fprintf(stderr,
        "OnyxCC — OnyxOS C/C++ → RISC-V64 → .onx compiler\n"
        "\n"
        "Usage: onyxcc [options] <input.c>... [-o output.onx]\n"
        "\n"
        "Options:\n"
        "  -I, --include <path>  Add include search path\n"
        "  -D, --define  <name>  Predefine macro (NAME or NAME=value)\n"
        "  -o, --output  <file>  Output file (default: a.onx, or a.o with -c)\n"
        "  -e, --entry   <sym>   Entry symbol (default: _start)\n"
        "  -c, --compile         Emit a relocatable .o for one input file\n"
        "      --ring1            Emit RING1 flag (binary runs in root space)\n"
        "      --nostdlib         Do not auto-link libonyxc (freestanding)\n"
        "      -fsyntax-only      Check translation units without linking/output\n"
        "  -v, --verbose         Verbose diagnostics\n"
        "      --dump-tokens      Print token stream after lex\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Examples:\n"
        "  onyxcc -o prog.onx main.c helper.c         # multi-file\n"
        "  onyxcc -o prog.onx main.c                  # single file\n"
        "  onyxcc -c -o main.o main.c                 # separate compilation\n"
        "\n"
        "Environment:\n"
        "  ONYXCC_INCLUDE  Default include path (e.g. /usr/onyxc/lib/include/core)\n"
        "\n");
}
