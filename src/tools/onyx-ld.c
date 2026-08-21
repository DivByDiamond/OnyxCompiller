/*
 * onyx-ld.c — OnyxOS linker.
 *
 * Reads one or more .o object files (and optionally .a static archives)
 * and produces a single .onx executable.
 *
 * Pipeline:
 *   1. Parse each .o: extract sections, symbols, relocations.
 *   2. For .a archives: extract each member .o (lazy on demand if
 *      undefined symbols exist; for simplicity we extract all members
 *      that contain a needed symbol, plus all members if --whole-archive).
 *   3. Layout: concatenate .text from all .o's into a single .text,
 *      same for .rodata, .data, .bss. Each .o's section gets a base
 *      offset within the merged section.
 *   4. Build a global symbol table from all .o files. Undefined symbols
 *      are resolved against defined symbols in any .o. Duplicate
 *      definitions are an error.
 *   5. Apply relocations: for each reloc in each .o, look up the symbol
 *      in the global table, compute the target's absolute address
 *      (or PC-relative offset), patch the bytes in the merged section.
 *   6. Resolve entry symbol (default: _start) — its address goes in
 *      the .onx header's entry field.
 *   7. Emit .onx via the same emit.c code path onyxcc uses.
 *
 * Usage:
 *   onyx-ld [-o output.onx] [-e entry_sym] [--ring1]
 *           file1.o file2.o ... libfoo.a ...
 *
 * Limitations:
 *   - Only handles RISC-V HI20/LO12_I/JAL/BRANCH relocations (the subset
 *     that onyxcc emits). Other reloc types are rejected.
 *   - No weak symbols, no symbol versioning, no DWARF.
 *   - No archive index (__.SYMDEF); for now we scan archives linearly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "sys/onyxo.h"

/* ── Constants ───────────────────────────────────────────────────────── */
#define MAX_OBJECTS    64
#define MAX_SECS_PER_OBJ 4
#define MAX_SYMS_TOTAL (64 * 1024)
#define MAX_RELOCS_TOTAL (128 * 1024)
#define MAX_STRINGS    (1 * 1024 * 1024)
#define MAX_OUTPUT_SZ  (16 * 1024 * 1024)

/* Default .text vaddr for the output .onx. Must match OnyxCC's CC_TEXT_VADDR
 * so that onyx-ld output is interchangeable with direct onyxcc output. */
#define ONYX_LD_TEXT_VADDR 0x10000

#define R_SEC_R 0x1
#define R_SEC_W 0x2
#define R_SEC_X 0x4

/* ── Data structures ─────────────────────────────────────────────────── */

typedef struct {
    uint32_t name_off;
    uint32_t type;       /* ONYO_SEC_* */
    uint32_t flags;      /* ONYO_SEC_* flags */
    uint32_t align;
    uint64_t size;
    uint64_t data_off;   /* offset within the merged section (set during layout) */
    uint8_t *data;       /* raw section bytes (NULL for bss) */
} sec_t;

typedef struct {
    char     name[256];
    uint32_t flags;
    int32_t  section_idx; /* -1 if undef */
    uint64_t value;       /* offset within section */
    uint64_t size;
    int      obj_idx;     /* which object this came from */
} sym_t;

typedef struct {
    uint32_t section_idx;  /* within its .o's sections */
    uint32_t type;
    uint64_t offset;       /* within that section */
    uint32_t sym_idx;      /* symbol index within its .o's symbol table */
    int32_t  addend;
    int      obj_idx;
} reloc_t;

typedef struct {
    char     name[256];
    sec_t    secs[MAX_SECS_PER_OBJ];
    int      n_secs;
    sym_t   *syms;
    int      n_syms;
    reloc_t *relocs;
    int      n_relocs;
    uint8_t *file_data;    /* whole .o file content (for section data access) */
    size_t   file_size;
    bool     is_archive_member;
} obj_t;

/* ── Globals ────────────────────────────────────────────────────────── */

static obj_t g_objs[MAX_OBJECTS];
static int   g_n_objs = 0;

/* Merged output sections (one big buffer for each). */
static uint8_t *g_out_text = NULL;
static size_t   g_out_text_size = 0;
static uint8_t *g_out_rodata = NULL;
static size_t   g_out_rodata_size = 0;
static uint8_t *g_out_data = NULL;
static size_t   g_out_data_size = 0;
static uint64_t g_out_bss_size = 0;

/* Global symbol table (defined symbols across all .o files). */
static sym_t  *g_global_syms = NULL;
static int     g_n_global_syms = 0;

/* Options. */
static const char *g_output = "a.onx";
static const char *g_entry_sym = "_start";
static bool       g_ring1 = false;
static bool       g_verbose = false;

/* ── Little-endian readers ───────────────────────────────────────────── */

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static int32_t rd_s32(const uint8_t *p) { return (int32_t)rd_u32(p); }

/* ── Object file parsing ────────────────────────────────────────────── */

/* Read the string at `name_off` from the .o file's string table. */
static void read_string(const obj_t *obj, uint32_t name_off, char *out, size_t out_sz) {
    size_t strtab_off = ONYO_HDR_SIZE
                      + obj->n_secs * ONYO_SECTION_HDR_SIZE
                      + obj->n_syms * ONYO_SYMBOL_SIZE
                      + obj->n_relocs * ONYO_RELOC_SIZE;
    if ((size_t)name_off >= obj->file_size - strtab_off) {
        if (out_sz > 0) out[0] = 0;
        return;
    }
    size_t off = strtab_off + name_off;
    size_t i = 0;
    while (i < out_sz - 1 && off + i < obj->file_size && obj->file_data[off + i] != 0) {
        out[i] = (char)obj->file_data[off + i];
        i++;
    }
    out[i] = 0;
}

static int parse_object(uint8_t *data, size_t size, const char *name, bool is_archive_member) {
    if (g_n_objs >= MAX_OBJECTS) {
        fprintf(stderr, "onyx-ld: too many input objects (max %d)\n", MAX_OBJECTS);
        return -1;
    }
    if (size < ONYO_HDR_SIZE) {
        fprintf(stderr, "onyx-ld: %s: file too small (%zu bytes)\n", name, size);
        return -1;
    }
    uint32_t magic = rd_u32(data + 0);
    if (magic != ONYO_MAGIC) {
        fprintf(stderr, "onyx-ld: %s: bad magic 0x%08x (expected 0x%08x)\n",
                name, magic, ONYO_MAGIC);
        return -1;
    }
    uint32_t version = rd_u32(data + 4);
    if (version != ONYO_VERSION_1) {
        fprintf(stderr, "onyx-ld: %s: unsupported version %u\n", name, version);
        return -1;
    }

    obj_t *obj = &g_objs[g_n_objs];
    memset(obj, 0, sizeof(*obj));
    obj->file_data = data;
    obj->file_size = size;
    obj->is_archive_member = is_archive_member;
    strncpy(obj->name, name, sizeof(obj->name) - 1);

    uint32_t n_secs  = rd_u32(data + 0x0C);
    uint32_t n_syms  = rd_u32(data + 0x10);
    uint32_t n_relocs = rd_u32(data + 0x14);

    obj->n_secs = (int)n_secs;
    obj->n_syms = (int)n_syms;
    obj->n_relocs = (int)n_relocs;

    if (n_secs > MAX_SECS_PER_OBJ) {
        fprintf(stderr, "onyx-ld: %s: too many sections (%u)\n", name, n_secs);
        return -1;
    }

    obj->syms = (sym_t *)calloc(n_syms ? n_syms : 1, sizeof(sym_t));
    obj->relocs = (reloc_t *)calloc(n_relocs ? n_relocs : 1, sizeof(reloc_t));
    if (!obj->syms || !obj->relocs) {
        fprintf(stderr, "onyx-ld: %s: out of memory\n", name);
        return -1;
    }

    size_t off = ONYO_HDR_SIZE;

    for (uint32_t i = 0; i < n_secs; i++) {
        sec_t *s = &obj->secs[i];
        s->name_off = rd_u32(data + off + 0);
        s->type     = rd_u32(data + off + 4);
        s->flags    = rd_u32(data + off + 8);
        s->align    = rd_u32(data + off + 12);
        s->size     = rd_u64(data + off + 16);
        s->data_off = rd_u64(data + off + 24);
        s->data     = NULL;
        off += ONYO_SECTION_HDR_SIZE;
    }

    for (uint32_t i = 0; i < n_syms; i++) {
        sym_t *s = &obj->syms[i];
        uint32_t name_off = rd_u32(data + off + 0);
        s->flags = rd_u32(data + off + 4);
        s->section_idx = rd_s32(data + off + 8);
        s->value = rd_u64(data + off + 16);
        s->size  = rd_u64(data + off + 24);
        read_string(obj, name_off, s->name, sizeof(s->name));
        s->obj_idx = g_n_objs;
        off += ONYO_SYMBOL_SIZE;
    }

    for (uint32_t i = 0; i < n_relocs; i++) {
        reloc_t *r = &obj->relocs[i];
        r->section_idx = rd_u32(data + off + 0);
        r->type        = rd_u32(data + off + 4);
        r->offset      = rd_u64(data + off + 8);
        r->sym_idx     = rd_u32(data + off + 16);
        r->addend      = rd_s32(data + off + 20);
        r->obj_idx     = g_n_objs;
        off += ONYO_RELOC_SIZE;
    }

    for (uint32_t i = 0; i < n_secs; i++) {
        sec_t *s = &obj->secs[i];
        if (s->type == ONYO_SEC_BSS) {
            s->data = NULL;
            continue;
        }
        if (s->data_off + s->size > size) {
            fprintf(stderr, "onyx-ld: %s: section %u data out of bounds\n", name, i);
            return -1;
        }
        s->data = data + s->data_off;
    }

    g_n_objs++;
    return 0;
}

/* ── Archive (.a) parsing ──────────────────────────────────────────── */

static int parse_archive(const char *path, uint8_t *data, size_t size) {
    if (size < 8 || memcmp(data, "!<arch>\n", 8) != 0) {
        fprintf(stderr, "onyx-ld: %s: not a valid archive\n", path);
        return -1;
    }
    size_t off = 8;
    int n_extracted = 0;
    while (off + 60 <= size) {
        char name_buf[17];
        memcpy(name_buf, (char *)data + off, 16);
        name_buf[16] = 0;
        for (int i = 15; i >= 0 && name_buf[i] == ' '; i--) name_buf[i] = 0;

        char size_buf[11];
        memcpy(size_buf, (char *)data + off + 48, 10);
        size_buf[10] = 0;
        long msize = strtol(size_buf, NULL, 10);
        if (msize < 0 || off + 60 + (size_t)msize > size) {
            break;
        }
        if (strcmp(name_buf, "/") == 0 || strcmp(name_buf, "//") == 0 ||
            strcmp(name_buf, "__.SYMDEF") == 0 || name_buf[0] == 0) {
            off += 60 + (size_t)msize;
            if (msize & 1) off++;
            continue;
        }
        long name_skip = 0;
        if (strncmp(name_buf, "#1/", 3) == 0) {
            name_skip = strtol(name_buf + 3, NULL, 10);
            if (name_skip < 0 || (size_t)name_skip > (size_t)msize) {
                off += 60 + (size_t)msize;
                if (msize & 1) off++;
                continue;
            }
        }
        uint8_t *member = data + off + 60 + name_skip;
        size_t member_size = (size_t)msize - (size_t)name_skip;

        if (member_size >= 4 && rd_u32(member) == ONYO_MAGIC) {
            uint8_t *copy = (uint8_t *)malloc(member_size);
            if (!copy) {
                fprintf(stderr, "onyx-ld: %s: out of memory extracting member\n", path);
                return -1;
            }
            memcpy(copy, member, member_size);
            char member_name[300];
            if (name_skip > 0) {
                size_t cn = (size_t)name_skip < sizeof(member_name) - 1 ? (size_t)name_skip : sizeof(member_name) - 1;
                memcpy(member_name, data + off + 60, cn);
                member_name[cn] = 0;
            } else {
                snprintf(member_name, sizeof(member_name), "%s:%s", path, name_buf);
            }
            if (parse_object(copy, member_size, member_name, true) != 0) {
                free(copy);
            } else {
                n_extracted++;
            }
        }

        off += 60 + (size_t)msize;
        if (msize & 1) off++;
    }
    if (g_verbose) {
        fprintf(stderr, "onyx-ld: archive %s: extracted %d member(s)\n", path, n_extracted);
    }
    return n_extracted > 0 ? 0 : -1;
}

/* ── File reading ─────────────────────────────────────────────────── */

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static int load_input(const char *path) {
    size_t size;
    uint8_t *data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "onyx-ld: cannot read %s\n", path);
        return -1;
    }
    if (size >= 8 && memcmp(data, "!<arch>\n", 8) == 0) {
        int rc = parse_archive(path, data, size);
        free(data);
        return rc;
    }
    if (size < 4 || rd_u32(data) != ONYO_MAGIC) {
        fprintf(stderr, "onyx-ld: %s: not a .o object or .a archive\n", path);
        free(data);
        return -1;
    }
    return parse_object(data, size, path, false);
}

/* ── Layout: concatenate sections ──────────────────────────────────── */

static int merge_sections(void) {
    size_t text_sz = 0, rodata_sz = 0, data_sz = 0;
    uint64_t bss_sz = 0;

    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_secs; j++) {
            sec_t *s = &obj->secs[j];
            size_t align = s->align ? s->align : 1;
            switch (s->type) {
                case ONYO_SEC_TEXT:
                    text_sz = (text_sz + align - 1) & ~(align - 1);
                    s->data_off = text_sz;
                    text_sz += s->size;
                    break;
                case ONYO_SEC_RODATA:
                    rodata_sz = (rodata_sz + align - 1) & ~(align - 1);
                    s->data_off = rodata_sz;
                    rodata_sz += s->size;
                    break;
                case ONYO_SEC_DATA:
                    data_sz = (data_sz + align - 1) & ~(align - 1);
                    s->data_off = data_sz;
                    data_sz += s->size;
                    break;
                case ONYO_SEC_BSS:
                    bss_sz = (bss_sz + align - 1) & ~(align - 1);
                    s->data_off = bss_sz;
                    bss_sz += s->size;
                    break;
            }
        }
    }

    if (text_sz > 0) {
        g_out_text = (uint8_t *)calloc(1, text_sz);
        if (!g_out_text) { fprintf(stderr, "onyx-ld: OOM text\n"); return -1; }
    }
    if (rodata_sz > 0) {
        g_out_rodata = (uint8_t *)calloc(1, rodata_sz);
        if (!g_out_rodata) { fprintf(stderr, "onyx-ld: OOM rodata\n"); return -1; }
    }
    if (data_sz > 0) {
        g_out_data = (uint8_t *)calloc(1, data_sz);
        if (!g_out_data) { fprintf(stderr, "onyx-ld: OOM data\n"); return -1; }
    }

    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_secs; j++) {
            sec_t *s = &obj->secs[j];
            if (s->size == 0) continue;
            if (s->type == ONYO_SEC_BSS) continue;
            uint8_t *dst = NULL;
            switch (s->type) {
                case ONYO_SEC_TEXT:   dst = g_out_text + s->data_off; break;
                case ONYO_SEC_RODATA: dst = g_out_rodata + s->data_off; break;
                case ONYO_SEC_DATA:   dst = g_out_data + s->data_off; break;
            }
            if (dst && s->data) {
                memcpy(dst, s->data, s->size);
            }
        }
    }

    g_out_text_size = text_sz;
    g_out_rodata_size = rodata_sz;
    g_out_data_size = data_sz;
    g_out_bss_size = bss_sz;
    return 0;
}

/* ── Build global symbol table ─────────────────────────────────────── */

static int find_global_sym(const char *name) {
    for (int i = 0; i < g_n_global_syms; i++) {
        if (strcmp(g_global_syms[i].name, name) == 0) return i;
    }
    return -1;
}

static uint64_t g_text_vaddr   = ONYX_LD_TEXT_VADDR;
static uint64_t g_rodata_vaddr = 0;
static uint64_t g_data_vaddr   = 0;
static uint64_t g_bss_vaddr    = 0;

static uint64_t compute_sym_vaddr(const sym_t *s) {
    if (s->section_idx < 0) return 0;
    obj_t *obj = &g_objs[s->obj_idx];
    if (s->section_idx >= obj->n_secs) return 0;
    sec_t *sec = &obj->secs[s->section_idx];
    uint64_t base = 0;
    switch (sec->type) {
        case ONYO_SEC_TEXT:   base = g_text_vaddr;   break;
        case ONYO_SEC_RODATA: base = g_rodata_vaddr; break;
        case ONYO_SEC_DATA:   base = g_data_vaddr;   break;
        case ONYO_SEC_BSS:    base = g_bss_vaddr;    break;
    }
    return base + sec->data_off + s->value;
}

static int build_global_symbol_table(void) {
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_syms; j++) {
            sym_t *s = &obj->syms[j];
            if (!(s->flags & ONYO_SYM_DEFINED)) continue;
            if ((s->flags & ONYO_SYM_LOCAL) && !(s->flags & ONYO_SYM_GLOBAL)) continue;
            if (!s->name[0]) continue;
            int existing = find_global_sym(s->name);
            if (existing >= 0) {
                fprintf(stderr, "onyx-ld: duplicate symbol '%s' (in %s and %s)\n",
                        s->name, g_objs[g_global_syms[existing].obj_idx].name, obj->name);
                return -1;
            }
            if (g_n_global_syms >= MAX_SYMS_TOTAL) {
                fprintf(stderr, "onyx-ld: too many symbols\n");
                return -1;
            }
            g_global_syms[g_n_global_syms] = *s;
            g_n_global_syms++;
        }
    }
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_syms; j++) {
            sym_t *s = &obj->syms[j];
            if (s->flags & ONYO_SYM_DEFINED) continue;
            if (!s->name[0]) continue;
            int existing = find_global_sym(s->name);
            if (existing < 0) {
                fprintf(stderr, "onyx-ld: undefined symbol '%s' (referenced from %s)\n",
                        s->name, obj->name);
                return -1;
            }
        }
    }
    return 0;
}

/* ── Relocation application ────────────────────────────────────────── */

static void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static void wr_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (v >> (i * 8)) & 0xff;
}

static void patch_imm_i(uint8_t *p, int32_t imm) {
    uint32_t inst = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    inst = (inst & 0x000FFFFF) | (((uint32_t)imm << 20) & 0xFFF00000);
    wr_u32(p, inst);
}

static void patch_imm_s(uint8_t *p, int32_t imm) {
    uint32_t inst = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    uint32_t imm_lo = ((uint32_t)imm & 0x1F) << 7;
    uint32_t imm_hi = ((uint32_t)imm >> 5 & 0x7F) << 25;
    inst = (inst & 0x01FFF07F) | imm_lo | imm_hi;
    wr_u32(p, inst);
}

static void patch_imm_u(uint8_t *p, uint32_t imm20) {
    uint32_t inst = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    inst = (inst & 0x00000FFF) | (imm20 << 12);
    wr_u32(p, inst);
}

static void patch_imm_j(uint8_t *p, int32_t imm) {
    uint32_t inst = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    uint32_t b = (uint32_t)imm;
    uint32_t sign = (b & 0x100000) << 10;
    uint32_t lo10 = (b & 0x7FE) << 20;
    uint32_t mid1 = (b & 0x800) << 9;
    uint32_t hi8 = (b & 0xFF000) << 0;
    uint32_t imm_field = sign | lo10 | mid1 | hi8;
    inst = (inst & 0x0000007F) | imm_field;
    wr_u32(p, inst);
}

static void patch_imm_b(uint8_t *p, int32_t imm) {
    uint32_t inst = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    uint32_t b = (uint32_t)imm;
    uint32_t sign = (b & 0x1000) << 19;
    uint32_t lo4 = (b & 0x1E) << 7;
    uint32_t mid6 = (b & 0x7E0) << 20;
    uint32_t b11 = (b & 0x800) >> 4;
    uint32_t imm_field = sign | lo4 | mid6 | b11;
    inst = (inst & 0x1FFF07F) | imm_field;
    wr_u32(p, inst);
}

static int apply_relocations(void) {
    for (int i = 0; i < g_n_objs; i++) {
        obj_t *obj = &g_objs[i];
        for (int j = 0; j < obj->n_relocs; j++) {
            reloc_t *r = &obj->relocs[j];
            if (r->section_idx < 0 || r->section_idx >= obj->n_secs) {
                fprintf(stderr, "onyx-ld: bad section_idx in reloc\n");
                return -1;
            }
            sec_t *sec = &obj->secs[r->section_idx];
            uint8_t *merged = NULL;
            uint64_t base_vaddr = 0;
            switch (sec->type) {
                case ONYO_SEC_TEXT:   merged = g_out_text;   base_vaddr = g_text_vaddr;   break;
                case ONYO_SEC_RODATA: merged = g_out_rodata; base_vaddr = g_rodata_vaddr; break;
                case ONYO_SEC_DATA:   merged = g_out_data;   base_vaddr = g_data_vaddr;   break;
                case ONYO_SEC_BSS:
                    fprintf(stderr, "onyx-ld: cannot have reloc in BSS\n");
                    return -1;
            }
            if (!merged) continue;
            uint8_t *patch_at = merged + sec->data_off + r->offset;

            if (r->sym_idx >= (uint32_t)obj->n_syms) {
                fprintf(stderr, "onyx-ld: bad sym_idx in reloc\n");
                return -1;
            }
            sym_t *target = &obj->syms[r->sym_idx];
            int gi = find_global_sym(target->name);
            if (gi < 0) {
                fprintf(stderr, "onyx-ld: unresolved symbol '%s' in %s\n",
                        target->name, obj->name);
                return -1;
            }
            sym_t *gtarget = &g_global_syms[gi];
            uint64_t S = compute_sym_vaddr(gtarget);
            uint64_t A = (uint64_t)(int64_t)r->addend;
            uint64_t P = base_vaddr + sec->data_off + r->offset;

            switch (r->type) {
                case R_ONYO_64:
                    wr_u64(patch_at, S + A);
                    break;
                case R_ONYO_32:
                    wr_u32(patch_at, (uint32_t)(S + A));
                    break;
                case R_ONYO_HI20: {
                    uint32_t val = (uint32_t)((S + A + 0x800) & 0xFFFFF000);
                    patch_imm_u(patch_at, val >> 12);
                    break;
                }
                case R_ONYO_LO12_I: {
                    int32_t lo = (int32_t)((S + A) & 0xFFF);
                    patch_imm_i(patch_at, lo);
                    break;
                }
                case R_ONYO_LO12_S: {
                    int32_t lo = (int32_t)((S + A) & 0xFFF);
                    patch_imm_s(patch_at, lo);
                    break;
                }
                case R_ONYO_PCREL_HI20: {
                    int64_t delta = (int64_t)(S + A) - (int64_t)P;
                    uint32_t val = (uint32_t)((delta + 0x800) & 0xFFFFF000);
                    patch_imm_u(patch_at, val >> 12);
                    break;
                }
                case R_ONYO_PCREL_LO12_I: {
                    int64_t delta = (int64_t)(S + A) - (int64_t)P;
                    int32_t lo = (int32_t)(delta & 0xFFF);
                    patch_imm_i(patch_at, lo);
                    break;
                }
                case R_ONYO_PCREL_LO12_S: {
                    int64_t delta = (int64_t)(S + A) - (int64_t)P;
                    int32_t lo = (int32_t)(delta & 0xFFF);
                    patch_imm_s(patch_at, lo);
                    break;
                }
                case R_ONYO_JAL: {
                    int64_t delta = (int64_t)(S + A) - (int64_t)P;
                    if (delta > (1 << 20) - 1 || delta < -(1 << 20)) {
                        fprintf(stderr, "onyx-ld: jal delta %lld out of range for '%s'\n",
                                (long long)delta, target->name);
                        return -1;
                    }
                    patch_imm_j(patch_at, (int32_t)delta);
                    break;
                }
                case R_ONYO_BRANCH: {
                    int64_t delta = (int64_t)(S + A) - (int64_t)P;
                    if (delta > 4095 || delta < -4096) {
                        fprintf(stderr, "onyx-ld: branch delta %lld out of range for '%s'\n",
                                (long long)delta, target->name);
                        return -1;
                    }
                    patch_imm_b(patch_at, (int32_t)delta);
                    break;
                }
                default:
                    fprintf(stderr, "onyx-ld: unknown reloc type %u\n", r->type);
                    return -1;
            }
        }
    }
    return 0;
}

/* ── .onx output ──────────────────────────────────────────────────── */

#define ONX_V1_MAGIC        0x4F4E5831   /* "ONX1" */
#define ONX_V1_VERSION_1    1
#define ONX_V1_HEADER_SIZE  344
#define ONX_V1_SEG_SIZE     40
#define ONX_V1_FIXED_HDR    24
#define ONX_VMM_R           0x1
#define ONX_VMM_W           0x2
#define ONX_VMM_X           0x4
#define ONX_FLAGS_RING1     0x1

static void onx_put_u32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
static void onx_put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (v >> (i * 8)) & 0xff;
}

static int emit_onx(const char *path) {
    int entry_idx = find_global_sym(g_entry_sym);
    if (entry_idx < 0) {
        fprintf(stderr, "onyx-ld: entry symbol '%s' not found\n", g_entry_sym);
        return -1;
    }
    uint64_t entry = compute_sym_vaddr(&g_global_syms[entry_idx]);

    uint64_t text_vaddr = g_text_vaddr;
    uint64_t rodata_vaddr = (text_vaddr + g_out_text_size + 15) & ~(uint64_t)15;
    uint64_t data_vaddr = (rodata_vaddr + g_out_rodata_size + 15) & ~(uint64_t)15;
    uint64_t bss_vaddr = (data_vaddr + g_out_data_size + 15) & ~(uint64_t)15;

    typedef struct { uint64_t vaddr; uint64_t filesz; uint64_t memsz;
                     uint32_t offset; uint32_t flags; uint32_t align; uint32_t reserved; } seg_t;
    seg_t segs[4];
    int nsegs = 0;

    segs[nsegs].vaddr = text_vaddr;
    segs[nsegs].filesz = g_out_text_size;
    segs[nsegs].memsz = g_out_text_size;
    segs[nsegs].offset = ONX_V1_HEADER_SIZE;
    segs[nsegs].flags = ONX_VMM_R | ONX_VMM_X;
    segs[nsegs].align = 4;
    segs[nsegs].reserved = 0;
    nsegs++;

    segs[nsegs].vaddr = rodata_vaddr;
    segs[nsegs].filesz = g_out_rodata_size;
    segs[nsegs].memsz = g_out_rodata_size;
    segs[nsegs].offset = ONX_V1_HEADER_SIZE + g_out_text_size;
    segs[nsegs].flags = ONX_VMM_R;
    segs[nsegs].align = 16;
    segs[nsegs].reserved = 0;
    nsegs++;

    if (g_out_data_size > 0) {
        segs[nsegs].vaddr = data_vaddr;
        segs[nsegs].filesz = g_out_data_size;
        segs[nsegs].memsz = g_out_data_size;
        segs[nsegs].offset = ONX_V1_HEADER_SIZE + g_out_text_size + g_out_rodata_size;
        segs[nsegs].flags = ONX_VMM_R | ONX_VMM_W;
        segs[nsegs].align = 16;
        segs[nsegs].reserved = 0;
        nsegs++;
    }

    if (g_out_bss_size > 0) {
        segs[nsegs].vaddr = bss_vaddr;
        segs[nsegs].filesz = 0;
        segs[nsegs].memsz = g_out_bss_size;
        segs[nsegs].offset = 0;
        segs[nsegs].flags = ONX_VMM_R | ONX_VMM_W;
        segs[nsegs].align = 16;
        segs[nsegs].reserved = 0;
        nsegs++;
    }

    uint8_t hdr[ONX_V1_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    onx_put_u32(hdr + 0,  ONX_V1_MAGIC);
    onx_put_u32(hdr + 4,  ONX_V1_VERSION_1);
    onx_put_u64(hdr + 8,  entry);
    onx_put_u32(hdr + 16, (uint32_t)nsegs);
    onx_put_u32(hdr + 20, g_ring1 ? ONX_FLAGS_RING1 : 0);
    for (int i = 0; i < nsegs; i++) {
        uint8_t *p = hdr + ONX_V1_FIXED_HDR + i * ONX_V1_SEG_SIZE;
        onx_put_u64(p + 0,  segs[i].vaddr);
        onx_put_u64(p + 8,  segs[i].filesz);
        onx_put_u64(p + 16, segs[i].memsz);
        onx_put_u32(p + 24, segs[i].offset);
        onx_put_u32(p + 28, segs[i].flags);
        onx_put_u32(p + 32, segs[i].align);
        onx_put_u32(p + 36, segs[i].reserved);
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "onyx-ld: cannot open %s for writing\n", path);
        return -1;
    }
    fwrite(hdr, 1, sizeof(hdr), f);
    if (g_out_text_size > 0)   fwrite(g_out_text, 1, g_out_text_size, f);
    if (g_out_rodata_size > 0) fwrite(g_out_rodata, 1, g_out_rodata_size, f);
    if (g_out_data_size > 0)   fwrite(g_out_data, 1, g_out_data_size, f);
    fclose(f);

    if (g_verbose) {
        fprintf(stderr, "onyx-ld: emitted %s\n", path);
        fprintf(stderr, "  entry  = 0x%llx\n", (unsigned long long)entry);
        fprintf(stderr, "  text   = 0x%06llx size=%zu\n",
                (unsigned long long)text_vaddr, g_out_text_size);
        fprintf(stderr, "  rodata = 0x%06llx size=%zu\n",
                (unsigned long long)rodata_vaddr, g_out_rodata_size);
        fprintf(stderr, "  data   = 0x%06llx size=%zu\n",
                (unsigned long long)data_vaddr, g_out_data_size);
        fprintf(stderr, "  bss    = 0x%06llx size=%llu\n",
                (unsigned long long)bss_vaddr, (unsigned long long)g_out_bss_size);
    }
    return 0;
}

/* ── Usage ──────────────────────────────────────────────────────────── */

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

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            g_output = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--entry") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            g_entry_sym = argv[++i];
        } else if (strcmp(argv[i], "--ring1") == 0) {
            g_ring1 = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "onyx-ld: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        } else {
            if (load_input(argv[i]) != 0) {
                return 1;
            }
        }
        i++;
    }

    if (g_n_objs == 0) {
        fprintf(stderr, "onyx-ld: no input files\n");
        usage();
        return 1;
    }

    g_global_syms = (sym_t *)calloc(MAX_SYMS_TOTAL, sizeof(sym_t));
    if (!g_global_syms) {
        fprintf(stderr, "onyx-ld: OOM allocating global symbol table\n");
        return 1;
    }

    if (merge_sections() != 0) return 1;

    uint64_t text_vaddr = g_text_vaddr;
    uint64_t rodata_vaddr = (text_vaddr + g_out_text_size + 15) & ~(uint64_t)15;
    uint64_t data_vaddr = (rodata_vaddr + g_out_rodata_size + 15) & ~(uint64_t)15;
    uint64_t bss_vaddr = (data_vaddr + g_out_data_size + 15) & ~(uint64_t)15;
    g_rodata_vaddr = rodata_vaddr;
    g_data_vaddr = data_vaddr;
    g_bss_vaddr = bss_vaddr;

    if (build_global_symbol_table() != 0) return 1;
    if (apply_relocations() != 0) return 1;
    if (emit_onx(g_output) != 0) return 1;

    if (g_verbose) {
        fprintf(stderr, "onyx-ld: linked %d object(s) into %s\n", g_n_objs, g_output);
    }
    return 0;
}
