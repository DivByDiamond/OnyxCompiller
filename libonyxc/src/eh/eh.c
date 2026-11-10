/*
 * eh.c — C++ exception runtime for OnyxCC (Phase E).
 *
 * A plain setjmp/longjmp handler chain: the compiler allocates a small
 * frame record on the try-block's stack, pushes it here, and longjmps
 * into the most recent handler when `throw` runs. Single-threaded —
 * one process has one handler chain. Dtors of scopes skipped by the
 * jump are NOT executed (documented limitation).
 *
 * The buffer type is declared locally instead of including <setjmp.h>:
 * the auto-link re-processes lib sources after the user's headers, and
 * redefining the `jmp_buf` typedef there would confuse the C parser.
 */
#include "onyxc.h"

/* Same layout as libonyxc's jmp_buf (27 unsigned longs, RV64 LP64D). */
typedef unsigned long __eh_jmp_buf[27];

typedef struct eh_frame {
    struct eh_frame *prev;
    __eh_jmp_buf *env;
} eh_frame;

static eh_frame *g_top;
static void *g_obj;
static int g_tag;

void __onyx_eh_push(void *frame, void *env) {
    eh_frame *f = (eh_frame *)frame;
    f->prev = g_top;
    f->env = (__eh_jmp_buf *)env;
    g_top = f;
}

void __onyx_eh_pop(void) {
    if (g_top) g_top = g_top->prev;
}

void *__onyx_eh_obj(void) { return g_obj; }
int __onyx_eh_tag(void) { return g_tag; }

void __onyx_eh_throw(void *obj, int tag) {
    g_obj = obj;
    g_tag = tag;
    if (!g_top) {
        static const char msg[] = "terminate: uncaught exception\n";
        write(2, msg, sizeof(msg) - 1);
        exit(134);
    }
    __builtin_onyx_longjmp(*g_top->env, 1);
}

void __onyx_eh_rethrow(void) {
    __onyx_eh_throw(g_obj, g_tag);
}
