/*
 * cc.h — OnyxCC shared declarations.
 *
 * OnyxCC is a single-pass C/C++ → RISC-V64 → .onx compiler, inspired by
 * tcc in spirit (small, fast, low-memory) but written from scratch for
 * OnyxOS self-hosting.
 *
 * Memory budget target: compile itself on a 512 MB board.
 * Compile speed target: linear in source size, no IR, no global opts.
 */
#ifndef CC_H
#define CC_H

#include "onx.h"
#ifndef CC_FREESTANDING
#include <stdbool.h>   /* bool in cc_options_t */
#include <stddef.h>    /* size_t in prototypes */
#endif

/* ---- Limits ------------------------------------------------------------ */
#define CC_MAX_TOKEN_LEN   256
#define CC_MAX_IDENT        64
#define CC_MAX_PATH        256
#define CC_MAX_INCLUDE_DEPTH 16
#define CC_MAX_MACRO_ARGS   16
#define CC_MAX_FUNC_PARAMS  32
#define CC_MAX_STRUCT_FIELDS 64
#define CC_MAX_ENUM_VALS    64
#define CC_MAX_LOCALS      256
#define CC_MAX_GLOBALS    4096
#define CC_MAX_TAGS        512
#define CC_MAX_STRING_POOL (1u << 20)   /* 1 MiB */
#define CC_MAX_CODE_BUF    (1u << 22)   /* 4 MiB instruction buffer */
#define CC_MAX_RODATA_BUF  (1u << 20)   /* 1 MiB */
#define CC_MAX_DATA_BUF    (1u << 20)
#define CC_MAX_BSS_SIZE    (1u << 22)   /* 4 MiB */

#define CC_TEXT_VADDR  0x00010000uLL
#define CC_RODATA_ALIGN 8u
#define CC_DATA_ALIGN   8u
#define CC_BSS_ALIGN    8u

/* ---- Diagnostics ------------------------------------------------------- */
typedef enum {
    CC_LVL_NOTE,
    CC_LVL_WARN,
    CC_LVL_ERROR,
    CC_LVL_FATAL,
} cc_diag_level_t;

void cc_diag(cc_diag_level_t lvl, const char *file, int line, const char *fmt, ...);

/* Variant used by lexer when full cc_pos_t is not yet attached. */
void cc_error_at(const char *file, int line, const char *fmt, ...);

int cc_get_errors(void);
int cc_get_warnings(void);

#define cc_note(...)    cc_diag(CC_LVL_NOTE,  __FILE__, __LINE__, __VA_ARGS__)
#define cc_warn(...)    cc_diag(CC_LVL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define cc_error(...)   cc_diag(CC_LVL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define cc_fatal(...)   do { cc_diag(CC_LVL_FATAL, __FILE__, __LINE__, __VA_ARGS__); while (1) {} } while (0)

/* ---- Lex/Parse positions ---------------------------------------------- */
typedef struct {
    const char *file;     /* logical file (post-#line) */
    int line;
    int col;
} cc_pos_t;

/* ---- Util: bump arena -------------------------------------------------- */
/* One-shot bump allocator used for AST nodes. We never free individual
 * nodes; on a top-level declaration boundary we reset the arena. */
typedef struct cc_arena {
    char *base;
    size_t size;
    size_t used;
} cc_arena_t;

void  cc_arena_init(cc_arena_t *a, size_t size);
void *cc_arena_alloc(cc_arena_t *a, size_t n, size_t align);
void  cc_arena_reset(cc_arena_t *a);
void  cc_arena_free(cc_arena_t *a);

/* ---- Util: growable byte buffer --------------------------------------- */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} cc_buf_t;

void cc_buf_init(cc_buf_t *b);
void cc_buf_push(cc_buf_t *b, const void *p, size_t n);
void cc_buf_push8(cc_buf_t *b, uint8_t v);
void cc_buf_push16(cc_buf_t *b, uint16_t v);
void cc_buf_push32(cc_buf_t *b, uint32_t v);
void cc_buf_push64(cc_buf_t *b, uint64_t v);
void cc_buf_align(cc_buf_t *b, size_t align);
void cc_buf_free(cc_buf_t *b);

/* ---- Util: string pool (deduplicated rodata) -------------------------- */
uint32_t cc_strpool_add(const char *s, size_t n);  /* returns offset in rodata */
uint32_t cc_strpool_add_cstr(const char *s);

/* ---- Output segments -------------------------------------------------- */
extern cc_buf_t g_text;
extern cc_buf_t g_rodata;
extern cc_buf_t g_data;
extern uint64_t g_bss_size;

extern uint64_t g_entry;     /* set by gen when _start is emitted */

/* ---- CLI options ------------------------------------------------------ */
typedef struct {
    const char *input;          /* primary input file (first one) */
    const char *input_files[30]; /* all input files (user files + auto-linked libc) */
    int n_input_files;
    int current_file_idx;       /* index of file currently being compiled */
    const char *output;         /* default: a.onx */
    const char *entry_sym;      /* default: _start */
    bool ring1;                 /* emit ONX_FLAGS_RING1 */
    bool verbose;
    bool dump_tokens;
    bool dump_ast;
    bool preprocess_only;       /* -E: dump preprocessed source, no codegen */
    bool nostdlib;              /* -nostdlib: skip libonyxc auto-link */
    const char *include_paths[16];
    int n_include_paths;
    const char *define_macros[64];
    int n_define_macros;
} cc_options_t;

extern cc_options_t g_opts;

#endif /* CC_H */
