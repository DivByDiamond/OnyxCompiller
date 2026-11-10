/*
 * ld_internal.h — shared state for the onyx-ld linker.
 *
 * src/tools/ld/ splits the linker by stage:
 *   parse.c — file/archive reading and ONYO table decoding
 *   link.c  — section layout, symbol table, relocation patching
 *   emit.c  — .onx writer
 * src/tools/onyx-ld.c keeps only usage(), CLI parsing and main().
 */

#ifndef ONYX_LD_INTERNAL_H
#define ONYX_LD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sys/onyxo.h"
#include "../onyx-ld.h"

/* Invocation limits: objects are small, anything past these is a bug. */
#define MAX_OBJECTS       64
#define MAX_SECS_PER_OBJ  4
#define MAX_SYMS_TOTAL    (64 * 1024)

/* Default .text vaddr for the output .onx. Must match onyxcc's
 * CC_TEXT_VADDR so onyx-ld output is interchangeable with its output. */
#define ONYX_LD_TEXT_VADDR 0x10000

typedef struct {
    uint32_t name_off;   /* offset into the object string table */
    uint32_t type;       /* ONYO_SEC_* */
    uint32_t flags;      /* ONYO_SEC_* flags */
    uint32_t align;      /* power of two, 0 is treated as 1 */
    uint64_t size;
    uint64_t data_off;   /* offset into the merged section (set by layout) */
    uint8_t *data;       /* raw bytes, NULL for .bss */
} sec_t;

typedef struct {
    char     name[256];
    uint32_t flags;       /* ONYO_SYM_* */
    int32_t  section_idx; /* -1 when undefined */
    uint64_t value;       /* offset inside section_idx */
    uint64_t size;
    int      obj_idx;     /* object that owns this symbol */
} sym_t;

typedef struct {
    uint32_t section_idx; /* patched section, index within its object */
    uint32_t type;        /* R_ONYO_* */
    uint64_t offset;      /* offset inside that section */
    uint32_t sym_idx;     /* symbol index within its object */
    int32_t  addend;
} reloc_t;

typedef struct {
    char     name[256];   /* file name, used in diagnostics */
    sec_t    secs[MAX_SECS_PER_OBJ];
    int      n_secs;
    sym_t   *syms;
    int      n_syms;
    reloc_t *relocs;
    int      n_relocs;
    uint8_t *file_data;   /* whole object image; section data points into it */
    size_t   file_size;
    size_t   strtab_off;  /* string table offset inside file_data */
    size_t   strtab_size;
    bool     is_archive_member;
} obj_t;

/* Every parsed object, in command-line order. */
extern obj_t g_objs[MAX_OBJECTS];
extern int   g_n_objs;

/* Merged output sections; BSS has a size but no backing storage. */
extern uint8_t *g_out_text;
extern size_t   g_out_text_size;
extern uint8_t *g_out_rodata;
extern size_t   g_out_rodata_size;
extern uint8_t *g_out_data;
extern size_t   g_out_data_size;
extern uint64_t g_out_bss_size;

/* DEFINED GLOBAL symbols across all objects (never LOCAL ones). */
extern sym_t *g_global_syms;
extern int    g_n_global_syms;

/* Virtual addresses of the merged sections. */
extern uint64_t g_text_vaddr;
extern uint64_t g_rodata_vaddr;
extern uint64_t g_data_vaddr;
extern uint64_t g_bss_vaddr;

/* link.c helpers shared with emit.c. */
int  ld_find_global_sym(const char *name);
bool ld_sym_vaddr(const sym_t *sym, uint64_t *out_vaddr);

/* Little-endian accessors shared by every stage. */
static inline uint32_t ld_rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t ld_rd_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static inline void ld_wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static inline void ld_wr_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (i * 8)) & 0xff);
}

/* RISC-V immediate patching used when applying relocations. These live
 * here so parse.c, link.c and emit.c each stay inside the 250-line rule. */
static inline void ld_patch_i(uint8_t *p, int32_t imm) {
    ld_wr_u32(p, (ld_rd_u32(p) & 0x000FFFFFu) | (((uint32_t)imm << 20) & 0xFFF00000u));
}

static inline void ld_patch_s(uint8_t *p, int32_t imm) {
    ld_wr_u32(p, (ld_rd_u32(p) & 0x01FFF07Fu) | (((uint32_t)imm & 0x1Fu) << 7) |
                 ((((uint32_t)imm >> 5) & 0x7Fu) << 25));
}

static inline void ld_patch_u(uint8_t *p, uint32_t imm20) {
    ld_wr_u32(p, (ld_rd_u32(p) & 0x00000FFFu) | (imm20 << 12));
}

static inline void ld_patch_j(uint8_t *p, int32_t imm) {
    uint32_t b = (uint32_t)imm;
    ld_wr_u32(p, (ld_rd_u32(p) & 0x7Fu) | ((b & 0x100000u) << 10) |
                 ((b & 0x7FEu) << 20) | ((b & 0x800u) << 9) | (b & 0xFF000u));
}

static inline void ld_patch_b(uint8_t *p, int32_t imm) {
    uint32_t b = (uint32_t)imm;
    ld_wr_u32(p, (ld_rd_u32(p) & 0x01FFF07Fu) | ((b & 0x1000u) << 19) |
                 ((b & 0x1Eu) << 7) | ((b & 0x7E0u) << 20) | ((b & 0x800u) >> 4));
}

/* R_ONYO_64 writes eight bytes; every other reloc patches one word. */
static inline size_t ld_reloc_width(uint32_t type) { return type == R_ONYO_64 ? 8 : 4; }

#endif /* ONYX_LD_INTERNAL_H */
