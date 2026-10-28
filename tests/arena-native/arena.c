#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "core/cc.h"

static size_t allocations, live, largest;

void *arena_test_malloc(size_t n) {
    allocations++;
    live++;
    if (n > largest) largest = n;
    void *p = malloc(n);
    assert(p);
    return p;
}

void arena_test_free(void *p) {
    if (p) { assert(live); live--; }
    free(p);
}

static void rejected(int which) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        cc_arena_t arena;
        cc_arena_init(&arena, 1024);
        (void)freopen("/dev/null", "w", stderr);
        if (which == 0) cc_arena_alloc(&arena, 1, 3);
        if (which == 1) cc_arena_alloc(&arena, (size_t)-1, 2);
        if (which == 2) cc_arena_alloc(&arena, 1025, 1);
        if (which == 3) {
            cc_arena_init(&arena, (size_t)-1);
            cc_arena_alloc(&arena, (size_t)-1, 1);
        }
        _exit(99);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
}

int main(void) {
    for (int i = 0; i < 4; i++) rejected(i);
    cc_arena_t arena, other;
    cc_arena_init(&arena, 256 * 1024 * 1024);
    cc_arena_init(&other, 256 * 1024 * 1024);
    assert(allocations == 0 && live == 0); /* No eager 512 MiB allocation. */
    assert(cc_arena_alloc(&arena, 0, 8) == NULL);
    unsigned char *first = cc_arena_alloc(&arena, 64, 8);
    for (size_t i = 0; i < 64; i++) assert(first[i] == 0);
    first[0] = 123;
    unsigned char *pointers[128];
    for (size_t i = 0; i < 128; i++) {
        size_t alignment = (size_t)1 << (i % 13);
        pointers[i] = cc_arena_alloc(&arena, 2049, alignment);
        assert(((uintptr_t)pointers[i] & (alignment - 1)) == 0);
        for (size_t j = 0; j < 2049; j++) assert(pointers[i][j] == 0);
        pointers[i][0] = (unsigned char)i;
    }
    unsigned char *large = cc_arena_alloc(&arena, 200000, 4096);
    assert(((uintptr_t)large & 4095) == 0);
    large[199999] = 77;
    assert(first[0] == 123 && large[199999] == 77);
    for (size_t i = 0; i < 128; i++) assert(pointers[i][0] == i);
    assert(allocations > 1 && largest < 300000);
    size_t before = allocations;
    cc_arena_reset(&arena);
    assert(cc_arena_alloc(&arena, 64, 8) == first && first[0] == 0);
    for (size_t i = 0; i < 128; i++) {
        void *p = cc_arena_alloc(&arena, 2049, (size_t)1 << (i % 13));
        assert(p == pointers[i] && pointers[i][0] == 0);
    }
    assert(cc_arena_alloc(&arena, 200000, 4096) == large);
    assert(large[199999] == 0 && allocations == before);
    cc_arena_free(&arena);
    cc_arena_free(&other);
    assert(live == 0 && arena.first == NULL && arena.current == NULL);
    assert(arena.size == 0 && arena.used == 0);
    cc_arena_free(&arena);
    cc_arena_reset(&arena);
    cc_buf_t buffer;
    cc_buf_init(&buffer);
    cc_buf_push(&buffer, NULL, 0);
    assert(buffer.data == NULL && buffer.size == 0);
    cc_buf_free(&buffer);
    puts("arena-native-ok");
    return 0;
}
