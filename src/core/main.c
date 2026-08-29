/*
 * main.c — OnyxCC driver.
 *
 * Usage:
 *   onyxcc [options] input1.c [input2.c ...] [-o output.onx]
 *
 * Supports multi-file compilation: all input files are compiled into a
 * single .onx binary. Global symbol table and segment buffers accumulate
 * across files. Static symbols get per-file mangling to avoid collisions.
 *
 * Pipeline:
 *   1. Preprocess (pp.c) → expanded source buffer (per file)
 *   2. Lex (lexer.c) → token stream (per file)
 *   3. Parse + codegen (parse.c + gen.c) → g_text, g_rodata, g_data, g_bss
 *   4. Emit .onx (emit.c) — once after all files
 *
 * Options:
 *   -I <path>     add include path
 *   -D <macro>    define macro (NAME or NAME=value)
 *   -o <file>     output file (default: a.onx)
 *   -e <symbol>   entry symbol (default: _start)
 *   --ring1       emit ONX_FLAGS_RING1 (binary runs in root space)
 *   -v            verbose
 *   --dump-tokens print tokens after lex (debug)
 *   -h, --help    show usage
 */
#include "core/compat.h"
#ifndef CC_FREESTANDING
#include <unistd.h>   /* readlink */
#endif

#include "core/cc.h"
#include "back/pp.h"
#include "front/lexer.h"
#include "core/types.h"
#include "front/ast.h"
#include "front/parse.h"
#include "back/gen.h"
#include "back/emit.h"

static void usage(void) {
    fprintf(stderr,
        "OnyxCC — OnyxOS C/C++ → RISC-V64 → .onx compiler\n"
        "\n"
        "Usage: onyxcc [options] <input.c>... [-o output.onx]\n"
        "\n"
        "Options:\n"
        "  -I, --include <path>  Add include search path\n"
        "  -D, --define  <name>  Predefine macro (NAME or NAME=value)\n"
        "  -o, --output  <file>  Output file (default: a.onx)\n"
        "  -e, --entry   <sym>   Entry symbol (default: _start)\n"
        "      --ring1            Emit RING1 flag (binary runs in root space)\n"
        "      --nostdlib         Do not auto-link libonyxc (freestanding)\n"
        "  -v, --verbose         Verbose diagnostics\n"
        "      --dump-tokens      Print token stream after lex\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Examples:\n"
        "  onyxcc -o prog.onx main.c helper.c         # multi-file\n"
        "  onyxcc -o prog.onx main.c                  # single file\n"
        "\n"
        "Environment:\n"
        "  ONYXCC_INCLUDE  Default include path (e.g. /usr/onyxc/include)\n"
        "\n");
}

static struct option long_opts[] = {
    {"include",    required_argument, 0, 'I'},
    {"define",     required_argument, 0, 'D'},
    {"output",     required_argument, 0, 'o'},
    {"entry",      required_argument, 0, 'e'},
    {"ring1",      no_argument,       0, 'R'},
    {"nostdlib",   no_argument,       0, 'N'},
    {"verbose",    no_argument,       0, 'v'},
    {"dump-tokens",no_argument,       0, 'T'},
    {"preprocess", no_argument,       0, 'E'},
    {"help",       no_argument,       0, 'h'},
    {0, 0, 0, 0},
};

int main(int argc, char **argv) {
    memset(&g_opts, 0, sizeof(g_opts));
    g_opts.entry_sym = "_start";
    g_opts.output = "a.onx";

    /* Default include path from env. */
    const char *env_inc = getenv("ONYXCC_INCLUDE");
    if (env_inc && g_opts.n_include_paths < 16) {
        g_opts.include_paths[g_opts.n_include_paths++] = env_inc;
    }

    int c;
    int opt_idx = 0;
    while ((c = getopt_long(argc, argv, "I:D:o:e:vhEN", long_opts, &opt_idx)) != -1) {
        switch (c) {
            case 'I':
                if (g_opts.n_include_paths < 16)
                    g_opts.include_paths[g_opts.n_include_paths++] = optarg;
                break;
            case 'D':
                if (g_opts.n_define_macros < 64)
                    g_opts.define_macros[g_opts.n_define_macros++] = optarg;
                break;
            case 'o': g_opts.output = optarg; break;
            case 'e': g_opts.entry_sym = optarg; break;
            case 'R': g_opts.ring1 = true; break;
            case 'N': g_opts.nostdlib = true; break;
            case 'v': g_opts.verbose = true; break;
            case 'T': g_opts.dump_tokens = true; break;
            case 'E':
                g_opts.preprocess_only = true;
                break;
            case 'h': usage(); return 0;
            default: usage(); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "onyxcc: no input file\n");
        usage();
        return 1;
    }

    /* -E: preprocess only (handled after full option parse so that -I/-D
     * given after -E are honoured). Dumps expanded source of the first
     * input file to stdout. */
    if (g_opts.preprocess_only) {
        size_t pp_len = 0;
        char *pp_src = pp_preprocess_file(argv[optind],
                                          g_opts.include_paths, g_opts.n_include_paths,
                                          g_opts.define_macros, g_opts.n_define_macros,
                                          &pp_len);
        if (!pp_src) return 1;
        fwrite(pp_src, 1, pp_len, stdout);
        free(pp_src);
        return (cc_get_errors() > 0) ? 1 : 0;
    }

    /* Collect all input files. */
    g_opts.n_input_files = 0;
    for (int i = optind; i < argc && g_opts.n_input_files < 30; i++) {
        g_opts.input_files[g_opts.n_input_files++] = argv[i];
    }

    /* Auto-link libonyxc: unless -nostdlib, the compiler appends the
     * standard library sources (start_cc + core libc) to the translation
     * unit list and registers the default include path. This makes
     *   onyxcc -o prog.onx prog.c
     * work out of the box — no manual -I / extra .c files needed.
     * ONYXCC_LIB overrides the library directory (default: <bindir>/../libonyxc
     * when running from the repo, or /usr/onyxc/lib). */
    if (!g_opts.nostdlib) {
        static char libdir[CC_MAX_PATH * 2];
        const char *env_lib = getenv("ONYXCC_LIB");
        if (env_lib) {
            snprintf(libdir, sizeof(libdir), "%s", env_lib);
        } else {
            /* Locate libonyxc relative to the executable:
             *   <exe-dir>/libonyxc          (repo layout: OnyxCompiller/)
             *   <exe-dir>/../libonyxc       (installed alongside)
             *   /usr/onyxc/lib/libonyxc     (system install) */
            char exepath[CC_MAX_PATH * 2] = {0};
            ssize_t n = -1;
#ifndef CC_FREESTANDING
            n = readlink("/proc/self/exe", exepath, sizeof(exepath) - 1);
#endif
            libdir[0] = 0;
            if (n > 0) {
                exepath[n] = 0;
                char *slash = strrchr(exepath, '/');
                if (slash) *slash = 0;
                static const char *cands[] = {
                    "/libonyxc",
                    "/../libonyxc",
                    "/../lib/libonyxc",
                };
                for (int i = 0; i < 3 && libdir[0] == 0; i++) {
                    char probe_dir[CC_MAX_PATH * 3];
                    snprintf(probe_dir, sizeof(probe_dir), "%s%s/src/core/start_cc.c", exepath, cands[i]);
                    FILE *f = fopen(probe_dir, "rb");
                    if (f) {
                        fclose(f);
                        snprintf(libdir, sizeof(libdir), "%s%s", exepath, cands[i]);
                    }
                }
            }
            if (libdir[0] == 0) {
                snprintf(libdir, sizeof(libdir), "/usr/onyxc/lib/libonyxc");
            }
        }
        /* Sanity: start_cc.c must exist there. */
        char probe[CC_MAX_PATH * 3];
        snprintf(probe, sizeof(probe), "%s/src/core/start_cc.c", libdir);
        FILE *f = fopen(probe, "rb");
        if (!f) {
            if (g_opts.verbose) {
                fprintf(stderr, "onyxcc: libonyxc not found at %s "
                        "(set ONYXCC_LIB); skipping auto-link\n", libdir);
            }
        } else {
            fclose(f);
            static const char *libsrcs[] = {
                "/src/core/start_cc.c",
                "/src/core/syscalls.c",
                "/src/io/stdio.c",
                "/src/io/stdlib.c",
                "/src/io/string.c",
                "/src/io/strerror.c",
                "/src/ctype/ctype.c",
                "/src/io/time.c",
                "/src/io/termios.c",
                "/src/io/math.c",
            };
            static char libpaths[10][CC_MAX_PATH * 2];
            for (int i = 0; i < 10; i++) {
                snprintf(libpaths[i], sizeof(libpaths[i]), "%s%s", libdir, libsrcs[i]);
                g_opts.input_files[g_opts.n_input_files++] = libpaths[i];
            }
            /* Default include paths for the library headers. */
            static char inc1[CC_MAX_PATH * 2], inc2[CC_MAX_PATH * 2], inc3[CC_MAX_PATH * 2];
            snprintf(inc1, sizeof(inc1), "%s/include/core", libdir);
            snprintf(inc2, sizeof(inc2), "%s/include/io", libdir);
            snprintf(inc3, sizeof(inc3), "%s/include/ctype", libdir);
            const char *defaults[3] = { inc1, inc2, inc3 };
            for (int i = 0; i < 3; i++) {
                bool dup = false;
                for (int j = 0; j < g_opts.n_include_paths; j++) {
                    if (strcmp(g_opts.include_paths[j], defaults[i]) == 0) { dup = true; break; }
                }
                if (!dup && g_opts.n_include_paths < 16) {
                    g_opts.include_paths[g_opts.n_include_paths++] = defaults[i];
                }
            }
            if (g_opts.verbose) {
                fprintf(stderr, "onyxcc: auto-linking libonyxc from %s\n", libdir);
            }
        }
    }
    g_opts.input = g_opts.input_files[0];

    if (g_opts.verbose) {
        fprintf(stderr, "onyxcc: inputs=");
        for (int i = 0; i < g_opts.n_input_files; i++) {
            fprintf(stderr, "%s%s", i > 0 ? "," : "", g_opts.input_files[i]);
        }
        fprintf(stderr, " output=%s entry=%s ring=%d\n",
                g_opts.output, g_opts.entry_sym, g_opts.ring1 ? 1 : 2);
    }

    /* Init arenas, types, symbol table (shared across all files). */
    cc_arena_init(&g_ast_arena, 256 * 1024 * 1024);  /* 256 MiB for AST (auto-linked libc, multi-file apps) */
    cc_arena_init(&g_type_arena, 256 * 1024 * 1024); /* 256 MiB for types */
    types_init(&g_type_arena);
    tags_init();
    symtab_init();
    gen_init();

    /* Process each input file. */
    int rc = 0;
    for (g_opts.current_file_idx = 0;
         g_opts.current_file_idx < g_opts.n_input_files;
         g_opts.current_file_idx++) {

        const char *input = g_opts.input_files[g_opts.current_file_idx];

        if (g_opts.verbose) {
            fprintf(stderr, "onyxcc: processing [%d/%d] %s\n",
                    g_opts.current_file_idx + 1,
                    g_opts.n_input_files, input);
        }

        /* 1. Preprocess. */
        size_t pp_len = 0;
        char *pp_src = pp_preprocess_file(input,
                                          g_opts.include_paths, g_opts.n_include_paths,
                                          g_opts.define_macros, g_opts.n_define_macros,
                                          &pp_len);
        if (!pp_src) {
            return 1;
        }
        if (g_opts.verbose) {
            fprintf(stderr, "onyxcc: preprocessed %s -> %zu bytes\n",
                    input, pp_len);
        }

        /* 2. Lex. */
        lexer_t lx;
        lex_init(&lx, pp_src, pp_len, input);

        if (g_opts.dump_tokens) {
            while (lx.cur.kind != T_EOF) {
                printf("%s:%d: %s '%s'\n",
                       lx.cur.pos.file ? lx.cur.pos.file : "?",
                       lx.cur.pos.line,
                       tok_kind_str(lx.cur.kind),
                       lx.cur.text);
                lex_next(&lx);
            }
            free(pp_src);
            /* If dump-tokens, only process the first file. */
            goto cleanup;
        }

        /* 3. Parse + codegen. */
        parse_translation_unit(&lx);

        /* 4. Reset per-file state (keep globals, segments, fixups). */
        gen_reset_for_file();

        free(pp_src);
    }

    /* 5. Finalize (resolve addresses) — once after all files. */
    gen_finalize(g_opts.entry_sym);

    /* 6. Emit .onx. */
    rc = onx_emit(g_opts.output, &g_text, &g_rodata, &g_data,
                  g_bss_size, g_entry, g_opts.ring1);

cleanup:
    cc_arena_free(&g_ast_arena);
    cc_arena_free(&g_type_arena);
    cc_buf_free(&g_text);
    cc_buf_free(&g_rodata);
    cc_buf_free(&g_data);

    if (cc_get_errors() > 0) {
        fprintf(stderr, "onyxcc: %d error(s), %d warning(s)\n",
                cc_get_errors(), cc_get_warnings());
        return 1;
    }
    return rc;
}
