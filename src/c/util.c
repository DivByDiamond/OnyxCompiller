/*
 * util.c — diagnostics, arena, growable buffers, string pool.
 */
#include "core/compat.h"

#include "core/cc.h"

cc_buf_t g_text;
cc_buf_t g_rodata;
cc_buf_t g_data;
uint64_t g_bss_size = 0;
uint64_t g_entry = 0;

cc_options_t g_opts;

/* ---- Diagnostics ------------------------------------------------------ */
static int g_n_errors = 0;
static int g_n_warnings = 0;

int cc_get_errors(void)   { return g_n_errors; }
int cc_get_warnings(void) { return g_n_warnings; }

void cc_diag(cc_diag_level_t lvl, const char *file, int line, const char *fmt, ...) {
    const char *tag = "?";
    switch (lvl) {
        case CC_LVL_NOTE:   tag = "note";   break;
        case CC_LVL_WARN:   tag = "warn";   g_n_warnings++; break;
        case CC_LVL_ERROR:  tag = "error";  g_n_errors++;   break;
        case CC_LVL_FATAL:  tag = "fatal";  g_n_errors++;   break;
    }
    fprintf(stderr, "onyxcc: %s: ", tag);
    if (file) fprintf(stderr, "%s:%d: ", file, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);

    if (lvl == CC_LVL_FATAL) {
        exit(1);
    }
}

/* ---- cc_error_at (file:line variant for lexer) ----------------------- */
void cc_error_at(const char *file, int line, const char *fmt, ...) {
    fprintf(stderr, "onyxcc: error: %s:%d: ", file ? file : "?", line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    g_n_errors++;
}

/* ---- Arena ------------------------------------------------------------ */
typedef struct cc_arena_chunk {
    struct cc_arena_chunk *next;
    size_t capacity;
    size_t used;
} cc_arena_chunk_t;

void cc_arena_init(cc_arena_t *a, size_t size) {
    a->first = a->current = NULL;
    a->size = size;
    a->used = 0;
}

void *cc_arena_alloc(cc_arena_t *a, size_t n, size_t align) {
    if (align == 0) align = 1;
    size_t mask = align - 1;
    if (align & mask) cc_fatal("arena: alignment must be a power of two");
    if (n > (size_t)-1 - mask) cc_fatal("arena: allocation size overflow");
    if (!n) return NULL;
    if (a->used > a->size || n > a->size - a->used) cc_fatal("arena: exhausted");
    cc_arena_chunk_t *chunk = a->current;
    cc_arena_chunk_t *last = NULL;
    size_t padding = 0;
    while (chunk) {
        uintptr_t address = (uintptr_t)(chunk + 1) + chunk->used;
        padding = (0 - address) & mask;
        if (padding <= chunk->capacity - chunk->used &&
            n <= chunk->capacity - chunk->used - padding) break;
        last = chunk;
        chunk = chunk->next;
    }
    if (!chunk) {
        size_t capacity = n + mask;
        size_t remaining = a->size - a->used;
        if (capacity < 65536) capacity = remaining < 65536 ? remaining : 65536;
        if (capacity > (size_t)-1 - sizeof(cc_arena_chunk_t))
            cc_fatal("arena: chunk size overflow");
        chunk = (cc_arena_chunk_t *)malloc(sizeof(cc_arena_chunk_t) + capacity);
        if (!chunk) cc_fatal("arena: out of memory (%zu bytes)", capacity);
        chunk->next = NULL;
        chunk->capacity = capacity;
        chunk->used = 0;
        if (last) last->next = chunk;
        else a->first = chunk;
        padding = (0 - (uintptr_t)(chunk + 1)) & mask;
    }
    if (padding > a->size - a->used - n) cc_fatal("arena: exhausted by alignment");
    void *p = (char *)(chunk + 1) + chunk->used + padding;
    chunk->used += padding + n;
    a->current = chunk;
    a->used += padding + n;
    memset(p, 0, n);
    return p;
}

void cc_arena_reset(cc_arena_t *a) {
    for (cc_arena_chunk_t *c = a->first; c; c = c->next) c->used = 0;
    a->current = a->first;
    a->used = 0;
}

void cc_arena_free(cc_arena_t *a) {
    cc_arena_chunk_t *chunk = a->first;
    while (chunk) {
        cc_arena_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    a->first = a->current = NULL;
    a->size = a->used = 0;
}

/* ---- Growable buffer -------------------------------------------------- */
void cc_buf_init(cc_buf_t *b) {
    b->data = NULL;
    b->size = 0;
    b->cap = 0;
}

static void buf_grow(cc_buf_t *b, size_t need) {
    if (b->cap >= need) return;
    size_t newcap = b->cap ? b->cap : 256;
    while (newcap < need) newcap <<= 1;
    uint8_t *p = (uint8_t *)realloc(b->data, newcap);
    if (!p) cc_fatal("buf: out of memory (%zu bytes)", newcap);
    b->data = p;
    b->cap = newcap;
}

void cc_buf_push(cc_buf_t *b, const void *p, size_t n) {
    if (!n) return;
    buf_grow(b, b->size + n);
    memcpy(b->data + b->size, p, n);
    b->size += n;
}

void cc_buf_push8(cc_buf_t *b, uint8_t v) {
    buf_grow(b, b->size + 1);
    b->data[b->size++] = v;
}

void cc_buf_push16(cc_buf_t *b, uint16_t v) {
    cc_buf_push8(b, v & 0xff);
    cc_buf_push8(b, (v >> 8) & 0xff);
}

void cc_buf_push32(cc_buf_t *b, uint32_t v) {
    cc_buf_push8(b, v & 0xff);
    cc_buf_push8(b, (v >> 8) & 0xff);
    cc_buf_push8(b, (v >> 16) & 0xff);
    cc_buf_push8(b, (v >> 24) & 0xff);
}

void cc_buf_push64(cc_buf_t *b, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        cc_buf_push8(b, (v >> (8 * i)) & 0xff);
    }
}

void cc_buf_align(cc_buf_t *b, size_t align) {
    if (align == 0) return;
    size_t mask = align - 1;
    size_t aligned = (b->size + mask) & ~mask;
    while (b->size < aligned) cc_buf_push8(b, 0);
}

void cc_buf_free(cc_buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->cap = 0;
}

/* ---- String pool (rodata dedup) -------------------------------------- */
/* Linear scan dedup. Adequate for typical programs with up to a few
 * thousand string literals. For larger inputs a hash table would be
 * better, but this keeps memory usage minimal. */
uint32_t cc_strpool_add(const char *s, size_t n) {
    /* Search existing rodata for a match. */
    if (g_rodata.size >= n + 1) {
        for (size_t off = 0; off + n + 1 <= g_rodata.size; off++) {
            if (g_rodata.data[off + n] == 0 &&
                memcmp(g_rodata.data + off, s, n) == 0) {
                return (uint32_t)off;
            }
        }
    }
    /* Append. */
    cc_buf_align(&g_rodata, 1);
    uint32_t off = (uint32_t)g_rodata.size;
    cc_buf_push(&g_rodata, s, n);
    cc_buf_push8(&g_rodata, 0);
    return off;
}

uint32_t cc_strpool_add_cstr(const char *s) {
    return cc_strpool_add(s, strlen(s));
}
