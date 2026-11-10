/*
 * onyx-ld.h — OnyxOS linker public API.
 *
 * This header declares the public interface for the onyx-ld linker.
 * It should be included by the onyx-ld.c source file and potentially
 * by other tools that need to use the linker functionality.
 */

#ifndef ONYX_LD_H
#define ONYX_LD_H

#include <stdint.h>
#include <stdbool.h>

/* Linker options and configuration */
typedef struct {
    const char *output;       /* Output .onx file (default: a.onx) */
    const char *entry_sym;    /* Entry symbol (default: _start) */
    bool ring1;               /* Emit RING1 flag (binary runs in root space) */
    bool verbose;           /* Verbose diagnostics */
} onyx_ld_options_t;

/* Object file and archive parsing */
int onyx_ld_parse_object(const char *path, bool is_archive_member);
int onyx_ld_parse_archive(const char *path);

/* Main linker function */
int onyx_ld_link(const onyx_ld_options_t *opts, int argc, char **argv);

/* Helper functions (optional, for more complex integration) */
int onyx_ld_merge_sections(void);
int onyx_ld_build_global_symbol_table(void);
int onyx_ld_apply_relocations(void);
int onyx_ld_emit_onx(const char *path);

/* Constants for linker flags and types */
#define ONYX_LD_FLAG_RING1 0x1

#endif /* ONYX_LD_H */