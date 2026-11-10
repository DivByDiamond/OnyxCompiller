#ifndef CC_OBJ_H
#define CC_OBJ_H

#include "core/compat.h"
#include "core/cc.h"

/*
 * obj.h — ONYO relocatable object emitter (`onyxcc -c`).
 *
 * Writes the four accumulated segments (.text/.rodata/.data/.bss) plus
 * their unresolved address patches as one ONYO object, as defined in
 * include/sys/onyxo.h and docs/plan-separate-compilation.md. Never calls
 * gen_finalize() and never mutates codegen state.
 */
/* Emit the ONYO relocation table from the compiler fixup tables.
 * `sym_map[gidx]` is the object symbol index (-1 when the global has no
 * object symbol). Returns 0 on success, -1 after a cc_error(). */
int obj_write_relocs(cc_buf_t *relocs, const int32_t *sym_map, int n_globals);

int obj_emit(const char *path);
#endif /* CC_OBJ_H */
