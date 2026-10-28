/* Single-threaded userspace heap. Blocks are 16-byte aligned and reusable.
 * sbrk extents remain owned by the allocator until process exit. */
#include "onyxc.h"

typedef struct block block_t;
struct block {
    size_t size;
    block_t *next;
    block_t *previous;
    long available;
};

static block_t *first_block;
static block_t *last_block;

static void merge_next(block_t *block) {
    block_t *next = block->next;
    if (!next || !next->available) return;
    if ((char *)(block + 1) + block->size != (char *)next) return;
    block->size += sizeof(block_t) + next->size;
    block->next = next->next;
    if (block->next) block->next->previous = block;
    else last_block = block;
}

static void split_block(block_t *block, size_t size) {
    if (block->size - size < sizeof(block_t) + 16) return;
    block_t *rest = (block_t *)((char *)(block + 1) + size);
    rest->size = block->size - size - sizeof(block_t);
    rest->available = 1;
    rest->previous = block;
    rest->next = block->next;
    if (rest->next) rest->next->previous = rest;
    else last_block = rest;
    block->next = rest;
    block->size = size;
    merge_next(rest);
}

void *malloc(size_t size) {
    if (size > (size_t)LONG_MAX - sizeof(block_t) - 65551) {
        errno = ENOMEM;
        return NULL;
    }
    if (size == 0) size = 1;
    size = (size + 15) & ~15UL;
    block_t *block = first_block;
    while (block) {
        if (block->available && block->size >= size) break;
        block = block->next;
    }
    if (!block) {
        size_t extent = (size + sizeof(block_t) + 65535) & ~65535UL;
        /* sbrk may initially be only byte aligned. Reserve room to align the
         * first header without assuming any loader-specific heap address. */
        char *memory = _onyx_sbrk((long)(extent + 15));
        if (memory == (void *)-1 || memory == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        block = (block_t *)(((uintptr_t)memory + 15) & ~15UL);
        block->size = extent - sizeof(block_t);
        block->next = NULL;
        block->previous = last_block;
        block->available = 1;
        if (last_block) last_block->next = block;
        else first_block = block;
        last_block = block;
    }
    split_block(block, size);
    block->available = 0;
    return block + 1;
}

void free(void *pointer) {
    if (!pointer) return;
    block_t *block = (block_t *)pointer - 1;
    block->available = 1;
    merge_next(block);
    if (block->previous && block->previous->available) merge_next(block->previous);
}

void *calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = count * size;
    void *pointer = malloc(total);
    if (pointer) memset(pointer, 0, total);
    return pointer;
}

void *realloc(void *pointer, size_t size) {
    if (!pointer) return malloc(size);
    if (!size) { free(pointer); return NULL; }
    if (size > (size_t)LONG_MAX - sizeof(block_t) - 65551) {
        errno = ENOMEM;
        return NULL;
    }
    block_t *block = (block_t *)pointer - 1;
    size_t old_size = block->size;
    size_t rounded = (size + 15) & ~15UL;
    merge_next(block);
    if (block->size >= rounded) {
        split_block(block, rounded);
        return pointer;
    }
    void *replacement = malloc(size);
    if (!replacement) return NULL;
    /* Copy only the old allocation: reading 'size' bytes here would read
     * past its end when growing, which broke Lua's realloc-based allocator. */
    memcpy(replacement, pointer, old_size);
    free(pointer);
    return replacement;
}
