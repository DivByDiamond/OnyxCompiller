/*
 * main.c — OnyxCC driver: pipeline order and per-file loop.
 *
 * Usage:
 *   onyxcc [options] input1.c [input2.c ...] [-o output.onx]
 *
 * Supports multi-file compilation: all input files are compiled into a
 * single .onx binary. Global symbol table and segment buffers accumulate
 * across files. Static symbols get per-file mangling to avoid collisions.
 * With -c exactly one file becomes a relocatable ONYO object instead.
 *
 * Pipeline:
 *   1. Preprocess (pp.c) → expanded source buffer (per file)
 *   2. Lex (lexer.c) → token stream (per file)
 *   3. Parse + codegen (parse.c + gen.c) → g_text, g_rodata, g_data, g_bss
 *   4. Emit .onx (emit.c) — once after all files, or ONYO with -c
 *
 * CLI parsing, libonyxc discovery and -c live in src/c/driver/.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "back/pp.h"
#include "front/lexer.h"
#include "core/types.h"
#include "front/ast.h"
#include "front/parse.h"
#include "back/gen.h"
#include "back/emit.h"
#include "front/parser_priv.h"
#include "front/driver.h"

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
    bool output_given = false;   /* -o seen: -c defaults to a.o otherwise */
    while ((c = getopt_long(argc, argv, driver_optstring(),
                            driver_long_options(), &opt_idx)) != -1) {
        switch (c) {
            case 'I':
                if (g_opts.n_include_paths < 16)
                    g_opts.include_paths[g_opts.n_include_paths++] = optarg;
                break;
            case 'D':
                if (g_opts.n_define_macros < 64)
                    g_opts.define_macros[g_opts.n_define_macros++] = optarg;
                break;
            case 'S': g_opts.syntax_only = true; break;
            case 'c': g_opts.compile_only = true; break;
            case 'f':
                if (strcmp(optarg, "syntax-only") != 0) {
                    fprintf(stderr, "onyxcc: unsupported -f option: %s\n", optarg);
                    return 1;
                }
                g_opts.syntax_only = true;
                break;
            case 'o': g_opts.output = optarg; output_given = true; break;
            case 'e': g_opts.entry_sym = optarg; break;
            case 'R': g_opts.ring1 = true; break;
            case 'N': g_opts.nostdlib = true; break;
            case 'v': g_opts.verbose = true; break;
            case 'T': g_opts.dump_tokens = true; break;
            case 'E': g_opts.preprocess_only = true; break;
            case 'h': driver_usage(); return 0;
            default: driver_usage(); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "onyxcc: no input file\n");
        driver_usage();
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

    /* -c: exactly one translation unit per object, no libonyxc auto-link.
     * The default output differs from a linked build, so only an explicit
     * -o selects the object path. */
    if (g_opts.compile_only) {
        if (argc - optind != 1) {
            fprintf(stderr, "onyxcc: -c requires exactly one input file\n");
            return 1;
        }
        if (!output_given) g_opts.output = "a.o";
    }

    /* Collect all input files. */
    int lib_slots = (g_opts.nostdlib || g_opts.compile_only) ? 0 : CC_LIB_SOURCE_COUNT;
    g_opts.n_input_files = 0;
    if (argc - optind > CC_MAX_INPUT_FILES - lib_slots) {
        fprintf(stderr, "onyxcc: too many input files (maximum %d)\n",
                CC_MAX_INPUT_FILES - lib_slots);
        return 1;
    }
    for (int i = optind; i < argc; i++) {
        g_opts.input_files[g_opts.n_input_files++] = argv[i];
    }

    /* Auto-link libonyxc (no-op for -nostdlib and -c). */
    driver_autolink_libonyxc();
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
    cc_arena_init(&g_ast_arena, 256 * 1024 * 1024);
    cc_arena_init(&g_type_arena, 1024UL * 1024 * 1024);
    types_init(&g_type_arena);
    tags_init();
    symtab_init();
    gen_init();

    int rc = 0;
    /* -c: single translation unit, object output, no address resolution. */
    if (g_opts.compile_only) {
        rc = driver_compile_object(g_opts.input_files[0], g_opts.output);
        goto cleanup;
    }

    /* Process each input file. */
    for (g_opts.current_file_idx = 0;
         g_opts.current_file_idx < g_opts.n_input_files;
         g_opts.current_file_idx++) {

        const char *input = g_opts.input_files[g_opts.current_file_idx];

        /* Mode must be correct per file BEFORE preprocess/lex/parse. */
        g_cpp_mode = driver_is_cpp_source(input);

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

    if (cc_get_errors() > 0) { rc = 1; goto cleanup; }
    if (g_opts.syntax_only) goto cleanup;
    /* 5. Finalize (resolve addresses) — once after all files. */
    gen_finalize(g_opts.entry_sym);
    if (cc_get_errors() > 0) { rc = 1; goto cleanup; }

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
