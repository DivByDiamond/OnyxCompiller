/*
 * onyx-ld.h — OnyxOS linker interface.
 *
 * Pipeline: onyx_ld_parse_input() once per input, then
 * onyx_ld_merge_sections(), onyx_ld_compute_addresses(),
 * onyx_ld_build_symbol_table(), onyx_ld_apply_relocations() and
 * finally onyx_ld_emit_onx().
 */

#ifndef ONYX_LD_H
#define ONYX_LD_H

#include <stdbool.h>

typedef struct {
    const char *output;    /* output .onx file */
    const char *entry_sym; /* name of the entry symbol */
    bool ring1;            /* set ONYX_LD_FLAG_RING1 in the header */
    bool verbose;          /* extra diagnostics on stderr */
} onyx_ld_options_t;

/* Set by main() from the command line; defaults are a.onx / _start. */
extern onyx_ld_options_t g_ld_opts;

/* Linker flags stored in the .onx header. */
#define ONYX_LD_FLAG_RING1 0x1

int  onyx_ld_parse_input(const char *path);
int  onyx_ld_merge_sections(void);
void onyx_ld_compute_addresses(void);
int  onyx_ld_build_symbol_table(void);
int  onyx_ld_apply_relocations(void);
int  onyx_ld_emit_onx(const char *path);

#endif /* ONYX_LD_H */
