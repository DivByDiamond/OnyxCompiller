/* Host-only harness: rename allocator symbols so sanitizers retain host malloc. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void *onyx_malloc(size_t);
void *onyx_calloc(size_t, size_t);
void *onyx_realloc(void *, size_t);
void onyx_free(void *);
static unsigned char arena[4 * 1024 * 1024];
static size_t used = 1; /* Exercise an initially unaligned program break. */
static int deny_growth;
static int allocator_errno;
int *___errno_location(void) { return &allocator_errno; }
void *_onyx_sbrk(long increment) {
    void *old;
    assert(increment > 0);
    if (deny_growth || (size_t)increment > sizeof(arena) - used)
        return (void *)-1;
    old = arena + used;
    used += (size_t)increment;
    return old;
}
static void check(const unsigned char *p, size_t n, unsigned char value) {
    size_t i;
    for (i = 0; i < n; ++i) assert(p[i] == value);
}
int main(void) {
    unsigned char *a, *b, *c, *grown;
    void *slots[64] = {0};
    size_t lengths[64] = {0};
    unsigned state = 31;
    size_t i, j;
    for (i = 0; i < 64; ++i) {
        slots[i] = onyx_malloc(i);
        assert(slots[i] && (uintptr_t)slots[i] % 16 == 0);
    }
    for (i = 0; i < 64; ++i) onyx_free(slots[i]);
    a = onyx_malloc(128); b = onyx_malloc(128); c = onyx_malloc(128);
    assert(a && b && c);
    onyx_free(b);
    assert(onyx_malloc(32) == b); /* Split the freed block. */
    grown = onyx_realloc(b, 128); /* Merge its free remainder in place. */
    assert(grown == b);
    onyx_free(a); onyx_free(b);
    grown = onyx_malloc(256); /* Requires coalescing the adjacent blocks. */
    assert(grown == a);
    onyx_free(grown); onyx_free(c);
    a = onyx_malloc(128); b = onyx_malloc(128);
    memset(a, 0xa5, 128);
    grown = onyx_realloc(a, 4096); /* Occupied neighbor forces a move. */
    assert(grown && grown != a); check(grown, 128, 0xa5);
    a = onyx_realloc(grown, 32); assert(a == grown); check(a, 32, 0xa5);
    deny_growth = 1;
    assert(onyx_realloc(a, sizeof(arena)) == NULL);
    check(a, 32, 0xa5); /* Failed growth preserves original ownership/data. */
    deny_growth = 0;
    assert(onyx_realloc(a, (size_t)-1) == NULL); check(a, 32, 0xa5);
    assert(onyx_calloc((size_t)-1, 2) == NULL);
    onyx_free(a); onyx_free(b); onyx_free(NULL);
    a = onyx_calloc(37, 19); assert(a); check(a, 37 * 19, 0); onyx_free(a);
    a = onyx_calloc(0, 19); onyx_free(a);
    a = onyx_realloc(NULL, 32); assert(a);
    assert(onyx_realloc(a, 0) == NULL);
    memset(slots, 0, sizeof(slots));
    /* Deterministic churn checks overlap, stale links, and preservation. */
    for (i = 0; i < 20000; ++i) {
        state = state * 1664525u + 1013904223u;
        j = (state >> 16) % 64;
        if (slots[j]) check(slots[j], lengths[j], (unsigned char)j);
        if (state & 1) {
            size_t n = (state >> 8) % 4096 + 1;
            a = onyx_realloc(slots[j], n); assert(a);
            if (slots[j]) check(a, n < lengths[j] ? n : lengths[j], (unsigned char)j);
            slots[j] = a; lengths[j] = n; memset(a, (unsigned char)j, n);
        } else {
            onyx_free(slots[j]); slots[j] = NULL;
        }
    }
    for (j = 0; j < 64; ++j) {
        if (slots[j]) check(slots[j], lengths[j], (unsigned char)j);
        onyx_free(slots[j]);
    }
    puts("native allocator: PASS");
    return 0;
}
