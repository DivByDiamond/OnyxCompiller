/*
 * parse.c — ONYO object and `ar` archive reader for onyx-ld.
 *
 * Objects are bounds-checked here so link.c can trust section offsets,
 * symbol section indices (still validated) and string table extents.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ld_internal.h"

onyx_ld_options_t g_ld_opts = { "a.onx", "_start", false, false };

/* Copy a NUL-terminated name out of the object's string table. */
static void read_string(const obj_t *obj, uint32_t name_off, char *out, size_t out_sz) {
    out[0] = 0;
    if (out_sz < 2 || (size_t)name_off >= obj->strtab_size) return;
    const char *src = (const char *)obj->file_data + obj->strtab_off + name_off;
    size_t avail = obj->strtab_size - (size_t)name_off;
    size_t i = 0;
    while (i < out_sz - 1 && i < avail && src[i]) {
        out[i] = src[i];
        i++;
    }
    out[i] = 0;
}

/* ── Object parsing ─────────────────────────────────────────────────── */

static int parse_object(uint8_t *data, size_t size, const char *name, bool is_archive_member) {
    if (g_n_objs >= MAX_OBJECTS) {
        fprintf(stderr, "onyx-ld: too many input objects (max %d)\n", MAX_OBJECTS);
        return -1;
    }
    if (size < ONYO_HDR_SIZE) {
        fprintf(stderr, "onyx-ld: %s: file too small (%zu bytes)\n", name, size);
        return -1;
    }
    uint32_t magic = ld_rd_u32(data + 0);
    if (magic != ONYO_MAGIC) {
        fprintf(stderr, "onyx-ld: %s: bad magic 0x%08x (expected 0x%08x)\n", name, magic, ONYO_MAGIC);
        return -1;
    }
    uint32_t version = ld_rd_u32(data + 4);
    if (version != ONYO_VERSION_1) {
        fprintf(stderr, "onyx-ld: %s: unsupported version %u\n", name, version);
        return -1;
    }

    uint32_t n_secs   = ld_rd_u32(data + 0x0C);
    uint32_t n_syms   = ld_rd_u32(data + 0x10);
    uint32_t n_relocs = ld_rd_u32(data + 0x14);
    uint32_t strtab_size = ld_rd_u32(data + 0x18);

    if (n_secs > MAX_SECS_PER_OBJ) {
        fprintf(stderr, "onyx-ld: %s: too many sections (%u)\n", name, n_secs);
        return -1;
    }
    /* Validate all tables before decoding any entry. */
    size_t tables = ONYO_HDR_SIZE
                  + (size_t)n_secs * ONYO_SECTION_HDR_SIZE
                  + (size_t)n_syms * ONYO_SYMBOL_SIZE
                  + (size_t)n_relocs * ONYO_RELOC_SIZE;
    if (tables > size || (size_t)strtab_size > size - tables) {
        fprintf(stderr, "onyx-ld: %s: truncated object\n", name);
        return -1;
    }

    obj_t *obj = &g_objs[g_n_objs];
    memset(obj, 0, sizeof(*obj));
    obj->file_data = data;
    obj->file_size = size;
    obj->strtab_off = tables;
    obj->strtab_size = strtab_size;
    obj->is_archive_member = is_archive_member;
    snprintf(obj->name, sizeof(obj->name), "%s", name);

    obj->n_secs = (int)n_secs;
    obj->n_syms = (int)n_syms;
    obj->n_relocs = (int)n_relocs;

    size_t off = ONYO_HDR_SIZE;
    for (uint32_t i = 0; i < n_secs; i++) {
        sec_t *s = &obj->secs[i];
        s->name_off = ld_rd_u32(data + off + 0);
        s->type     = ld_rd_u32(data + off + 4);
        s->flags    = ld_rd_u32(data + off + 8);
        s->align    = ld_rd_u32(data + off + 12);
        s->size     = ld_rd_u64(data + off + 16);
        s->data_off = ld_rd_u64(data + off + 24);
        s->data     = NULL;
        /* Validate data extents before allocating symbol/reloc tables. */
        if (s->type != ONYO_SEC_BSS &&
            (s->data_off > (uint64_t)size || s->size > (uint64_t)size - s->data_off)) {
            fprintf(stderr, "onyx-ld: %s: section %u data out of bounds\n", name, i);
            return -1;
        }
        off += ONYO_SECTION_HDR_SIZE;
    }

    obj->syms = (sym_t *)calloc(n_syms ? n_syms : 1, sizeof(sym_t));
    obj->relocs = (reloc_t *)calloc(n_relocs ? n_relocs : 1, sizeof(reloc_t));
    if (!obj->syms || !obj->relocs) {
        fprintf(stderr, "onyx-ld: %s: out of memory\n", name);
        return -1;
    }

    for (uint32_t i = 0; i < n_syms; i++) {
        sym_t *s = &obj->syms[i];
        uint32_t name_off = ld_rd_u32(data + off + 0);
        s->flags       = ld_rd_u32(data + off + 4);
        s->section_idx = (int32_t)ld_rd_u32(data + off + 8);
        s->value       = ld_rd_u64(data + off + 16);
        s->size        = ld_rd_u64(data + off + 24);
        read_string(obj, name_off, s->name, sizeof(s->name));
        s->obj_idx = g_n_objs;
        off += ONYO_SYMBOL_SIZE;
    }

    for (uint32_t i = 0; i < n_relocs; i++) {
        reloc_t *r = &obj->relocs[i];
        r->section_idx = ld_rd_u32(data + off + 0);
        r->type        = ld_rd_u32(data + off + 4);
        r->offset      = ld_rd_u64(data + off + 8);
        r->sym_idx     = ld_rd_u32(data + off + 16);
        r->addend      = (int32_t)ld_rd_u32(data + off + 20);
        off += ONYO_RELOC_SIZE;
    }

    for (uint32_t i = 0; i < n_secs; i++) {
        sec_t *s = &obj->secs[i];
        if (s->type != ONYO_SEC_BSS) s->data = data + s->data_off;
    }

    g_n_objs++;
    return 0;
}

/* ── Archive (.a) parsing ───────────────────────────────────────────── */

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
        if (msize < 0 || (size_t)msize > size - off - 60) break;
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

        /* Members that are not ONYO objects (e.g. metadata) are skipped. */
        if (member_size >= 4 && ld_rd_u32(member) == ONYO_MAGIC) {
            uint8_t *copy = (uint8_t *)malloc(member_size);
            if (!copy) {
                fprintf(stderr, "onyx-ld: %s: out of memory extracting member\n", path);
                return -1;
            }
            memcpy(copy, member, member_size);
            char member_name[300];
            if (name_skip > 0) {
                size_t cn = (size_t)name_skip < sizeof(member_name) - 1 ? (size_t)name_skip
                                                                       : sizeof(member_name) - 1;
                memcpy(member_name, data + off + 60, cn);
                member_name[cn] = 0;
            } else {
                snprintf(member_name, sizeof(member_name), "%s:%s", path, name_buf);
            }
            if (parse_object(copy, member_size, member_name, true) != 0) free(copy);
            else n_extracted++;
        }

        off += 60 + (size_t)msize;
        if (msize & 1) off++;
    }
    if (g_ld_opts.verbose) {
        fprintf(stderr, "onyx-ld: archive %s: extracted %d member(s)\n", path, n_extracted);
    }
    if (n_extracted == 0) {
        fprintf(stderr, "onyx-ld: %s: no ONYO objects in archive\n", path);
        return -1;
    }
    return 0;
}

/* ── Input dispatch ─────────────────────────────────────────────────── */

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0 || sz < 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc(sz > 0 ? (size_t)sz : 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

int onyx_ld_parse_input(const char *path) {
    size_t size;
    uint8_t *data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "onyx-ld: cannot read %s\n", path);
        return -1;
    }
    if (size >= 8 && memcmp(data, "!<arch>\n", 8) == 0) {
        /* parse_archive copies every member object, so the image is free. */
        int rc = parse_archive(path, data, size);
        free(data);
        return rc;
    }
    if (size < 4 || ld_rd_u32(data) != ONYO_MAGIC) {
        fprintf(stderr, "onyx-ld: %s: not a .o object or .a archive\n", path);
        free(data);
        return -1;
    }
    int rc = parse_object(data, size, path, false);
    if (rc != 0) free(data);
    return rc;
}
