/*
 * mangle.c — Itanium-lite name mangling (spec item 6, Phase B).
 *
 * See cpp/mangle.h for the format. Built through a tiny bounded
 * buffer: every append checks capacity, the first overflow reports
 * cc_error once and latches the failure off (no silent truncation —
 * truncated symbol names collide at link time).
 */
#include "core/compat.h"

#include "cpp/mangle.h"

typedef struct {
    char *out;
    size_t sz;                    /* capacity including the NUL */
    size_t len;
    bool ok;
} mbuf_t;

static void mb_fail(mbuf_t *b) {
    if (b->ok) {
        cc_error("mangled name longer than %d characters", CC_MAX_IDENT);
        b->ok = false;
    }
}

static void mb_ch(mbuf_t *b, char c) {
    if (!b->ok) return;
    if (b->len + 1 >= b->sz) { mb_fail(b); return; }
    b->out[b->len++] = c;
    b->out[b->len] = 0;
}

static void mb_str(mbuf_t *b, const char *s) {
    while (*s) mb_ch(b, *s++);
}

static void mb_num(mbuf_t *b, size_t n) {
    char tmp[24];
    int i = 0;
    if (n == 0) { mb_ch(b, '0'); return; }
    while (n) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) mb_ch(b, tmp[--i]);
}

/* Length-prefixed scope component: "3Foo". */
static void mb_ident(mbuf_t *b, const char *name) {
    mb_num(b, strlen(name));
    mb_str(b, name);
}

/*
 * One type code. `K` (const) is emitted before the base code so the
 * qualifier travels with the type it qualifies: `const char *` is
 * P-K-c -> "PKc". Arrays decay to pointer-to-element ("P" + elem) —
 * an approximation documented in docs/STATUS.md at Phase G; function
 * types use F...E so a function pointer (PF...E) never collapses to
 * the void-pointer code "Pv".
 */
static void mb_type(mbuf_t *b, type_t *t) {
    if (!t) { mb_ch(b, 'v'); return; }
    if (t->is_const) mb_ch(b, 'K');
    switch (t->kind) {
        case TY_VOID:     mb_ch(b, 'v'); break;
        case TY_BOOL:     mb_ch(b, 'b'); break;
        case TY_CHAR:     mb_ch(b, 'c'); break;
        case TY_SCHAR:    mb_ch(b, 'a'); break;
        case TY_UCHAR:    mb_ch(b, 'h'); break;
        case TY_SHORT:    mb_ch(b, 's'); break;
        case TY_USHORT:   mb_ch(b, 't'); break;
        case TY_INT:      mb_ch(b, 'i'); break;
        case TY_UINT:     mb_ch(b, 'j'); break;
        case TY_LONG:     mb_ch(b, 'l'); break;
        case TY_ULONG:    mb_ch(b, 'm'); break;
        case TY_LLONG:    mb_ch(b, 'x'); break;
        case TY_ULLONG:   mb_ch(b, 'y'); break;
        case TY_FLOAT:    mb_ch(b, 'f'); break;
        case TY_DOUBLE:   mb_ch(b, 'd'); break;
        case TY_LDOUBLE:  mb_ch(b, 'g'); break;
        case TY_PTR:      mb_ch(b, 'P'); mb_type(b, t->base); break;
        case TY_ARRAY:    mb_ch(b, 'P'); mb_type(b, t->base); break;
        case TY_FUNC:
            mb_ch(b, 'F');
            mb_type(b, t->ret);
            for (int i = 0; i < t->nparams; i++) mb_type(b, t->params[i].type);
            if (t->is_varargs) mb_str(b, "z");
            mb_ch(b, 'E');
            break;
        case TY_STRUCT:
        case TY_UNION:
            if (t->tag[0]) mb_ident(b, t->tag);
            else mb_ch(b, 'i');   /* anonymous aggregate: stable fallback */
            break;
        case TY_ENUM:     mb_ch(b, 'i'); break;
        default:          mb_ch(b, 'i'); break;
    }
}

/* Declared args, params[skip .. skip+nargs-1]; no args -> 'v'. */
static void mb_args(mbuf_t *b, type_t *ftype, int skip, int nargs) {
    if (!ftype || nargs <= 0) { mb_ch(b, 'v'); return; }
    for (int i = 0; i < nargs; i++) {
        if (skip + i >= ftype->nparams) break;
        mb_type(b, ftype->params[skip + i].type);
    }
}

static void mb_init(mbuf_t *b, char *out, size_t out_sz) {
    b->out = out;
    b->sz = out_sz;
    b->len = 0;
    b->ok = out_sz > 0;
    if (out_sz > 0) out[0] = 0;
    if (b->ok) mb_str(b, "_Z");
}

bool cpp_mangle_member(char *out, size_t out_sz, const char *cls,
                       const char *name, type_t *ftype, int nargs) {
    mbuf_t b;
    mb_init(&b, out, out_sz);
    /* Nested name: _Z N <class> <member> E <args> — the E closes the
     * nested-name scope after the member (or its C1/D1 marker). */
    mb_ch(&b, 'N');
    mb_ident(&b, cls);
    if (name[0] == '~') {
        /* Destructor: D1 + void — no parameter list. */
        mb_str(&b, "D1");
        mb_ch(&b, 'E');
        mb_ch(&b, 'v');
        return b.ok;
    }
    if (strcmp(name, cls) == 0) {
        /* Constructor: C1 (complete-object ctor only) + declared args. */
        mb_str(&b, "C1");
    } else {
        mb_ident(&b, name);
    }
    mb_ch(&b, 'E');
    mb_args(&b, ftype, 0, nargs);
    return b.ok;
}

bool cpp_mangle_free(char *out, size_t out_sz, const char *name,
                     type_t *ftype, int nargs) {
    mbuf_t b;
    mb_init(&b, out, out_sz);
    mb_ident(&b, name);
    mb_args(&b, ftype, 0, nargs);
    return b.ok;
}
