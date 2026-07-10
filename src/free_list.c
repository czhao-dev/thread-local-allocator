#include "memalloc/free_list.h"

#include <unistd.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memalloc/common.h"
#include "mmap_utils.h"

#define MEMALLOC_FL_ALLOC_BIT ((size_t)0x1)

struct MemallocFLHeader {
    size_t size_and_flags;
};

typedef struct {
    MemallocFLHeader* prev;
    MemallocFLHeader* next;
} MemallocFLFreeNode;

struct MemallocFLArenaHeader {
    MemallocFLArenaHeader* next;
    size_t total_size;
};

static const size_t kHeaderSize = sizeof(size_t);
static const size_t kOverhead = 2 * sizeof(size_t);  /* header + footer */
static const size_t kMinBlockSize = 32;
static const size_t kArenaSize = 1u << 20;  /* 1 MiB */
/* ArenaHeader + 1-word prologue "block" that precedes the first real block. */
#define MEMALLOC_FL_ARENA_PREFIX (sizeof(MemallocFLArenaHeader) + sizeof(size_t))
/* 1-word epilogue "block" that follows the last real block. */
#define MEMALLOC_FL_ARENA_SUFFIX (sizeof(size_t))

static size_t memalloc_fl_page_size(void) {
    /* Only ever called with fl->mutex held (from extend()), so this lazy
     * init is race-free without needing its own synchronization. */
    static size_t sz = 0;
    if (sz == 0) sz = (size_t)sysconf(_SC_PAGESIZE);
    return sz;
}

/* --- block layout helpers --------------------------------------------- */

static MemallocFLHeader* memalloc_fl_header_of(void* payload) {
    return (MemallocFLHeader*)((char*)payload - kHeaderSize);
}

static void* memalloc_fl_payload_of(MemallocFLHeader* h) {
    return (char*)h + kHeaderSize;
}

static size_t memalloc_fl_size_of(MemallocFLHeader* h) {
    return h->size_and_flags & ~MEMALLOC_FL_ALLOC_BIT;
}

static bool memalloc_fl_is_free(MemallocFLHeader* h) {
    return (h->size_and_flags & MEMALLOC_FL_ALLOC_BIT) == 0;
}

static void memalloc_fl_set_tags(MemallocFLHeader* h, size_t size, bool free) {
    size_t flags = size | (free ? 0 : MEMALLOC_FL_ALLOC_BIT);
    h->size_and_flags = flags;
    ((MemallocFLHeader*)((char*)h + size - kHeaderSize))->size_and_flags = flags;
}

static MemallocFLHeader* memalloc_fl_next_block(MemallocFLHeader* h) {
    return (MemallocFLHeader*)((char*)h + memalloc_fl_size_of(h));
}

static MemallocFLHeader* memalloc_fl_prev_block(MemallocFLHeader* h) {
    MemallocFLHeader* prev_footer = (MemallocFLHeader*)((char*)h - kHeaderSize);
    size_t prev_size = memalloc_fl_size_of(prev_footer);
    return (MemallocFLHeader*)((char*)h - prev_size);
}

/* --- free list management ---------------------------------------------- */

static void memalloc_fl_insert_free(MemallocFreeList* fl, MemallocFLHeader* h) {
    MemallocFLFreeNode* node = (MemallocFLFreeNode*)memalloc_fl_payload_of(h);
    node->prev = NULL;
    node->next = fl->free_list;
    if (fl->free_list) ((MemallocFLFreeNode*)memalloc_fl_payload_of(fl->free_list))->prev = h;
    fl->free_list = h;
}

static void memalloc_fl_remove_free(MemallocFreeList* fl, MemallocFLHeader* h) {
    MemallocFLFreeNode* node = (MemallocFLFreeNode*)memalloc_fl_payload_of(h);
    if (node->prev) {
        ((MemallocFLFreeNode*)memalloc_fl_payload_of(node->prev))->next = node->next;
    } else {
        fl->free_list = node->next;
    }
    if (node->next) {
        ((MemallocFLFreeNode*)memalloc_fl_payload_of(node->next))->prev = node->prev;
    }
}

static MemallocFLHeader* memalloc_fl_find_fit(MemallocFreeList* fl, size_t total_size) {
    for (MemallocFLHeader* h = fl->free_list; h != NULL;
         h = ((MemallocFLFreeNode*)memalloc_fl_payload_of(h))->next) {
        if (memalloc_fl_size_of(h) >= total_size) return h;
    }
    return NULL;
}

static MemallocFLHeader* memalloc_fl_split(MemallocFreeList* fl, MemallocFLHeader* h,
                                            size_t alloc_total_size) {
    size_t old_size = memalloc_fl_size_of(h);
    if (old_size - alloc_total_size >= kMinBlockSize) {
        memalloc_fl_set_tags(h, alloc_total_size, false);
        MemallocFLHeader* rem = memalloc_fl_next_block(h);
        size_t rem_size = old_size - alloc_total_size;
        memalloc_fl_set_tags(rem, rem_size, true);

        /* `rem` may already be adjacent to a free block (split() is also
         * called from reallocate(), where h's neighbors were never
         * re-examined) -- coalesce forward to preserve the "no two
         * adjacent free blocks" invariant. */
        MemallocFLHeader* next = memalloc_fl_next_block(rem);
        if (memalloc_fl_is_free(next)) {
            memalloc_fl_remove_free(fl, next);
            rem_size += memalloc_fl_size_of(next);
            memalloc_fl_set_tags(rem, rem_size, true);
        }
        memalloc_fl_insert_free(fl, rem);
    } else {
        memalloc_fl_set_tags(h, old_size, false);
    }
    return h;
}

static MemallocFLHeader* memalloc_fl_coalesce(MemallocFreeList* fl, MemallocFLHeader* h) {
    MemallocFLHeader* prev = memalloc_fl_prev_block(h);
    MemallocFLHeader* next = memalloc_fl_next_block(h);
    size_t total = memalloc_fl_size_of(h);
    MemallocFLHeader* result = h;

    if (memalloc_fl_is_free(prev)) {
        memalloc_fl_remove_free(fl, prev);
        total += memalloc_fl_size_of(prev);
        result = prev;
    }
    if (memalloc_fl_is_free(next)) {
        memalloc_fl_remove_free(fl, next);
        total += memalloc_fl_size_of(next);
    }
    memalloc_fl_set_tags(result, total, true);
    return result;
}

/* --- arena management ---------------------------------------------------- */

static MemallocFLHeader* memalloc_fl_extend(MemallocFreeList* fl, size_t min_total_size) {
    size_t needed = min_total_size + MEMALLOC_FL_ARENA_PREFIX + MEMALLOC_FL_ARENA_SUFFIX;
    size_t arena_size = kArenaSize;
    if (arena_size < needed) arena_size = memalloc_align_up(needed, memalloc_fl_page_size());

    void* mem = memalloc_mmap_region(arena_size);
    if (!mem) return NULL;

    MemallocFLArenaHeader* arena = (MemallocFLArenaHeader*)mem;
    arena->total_size = arena_size;
    arena->next = fl->arenas;
    fl->arenas = arena;

    /* Prologue sentinel: a single-word "block" marked allocated so backward
     * coalescing never walks before the arena. */
    MemallocFLHeader* prologue = (MemallocFLHeader*)((char*)mem + sizeof(MemallocFLArenaHeader));
    memalloc_fl_set_tags(prologue, kHeaderSize, false);

    /* The entire rest of the arena (minus the epilogue sentinel) starts as
     * one big free block. */
    MemallocFLHeader* block = memalloc_fl_next_block(prologue);
    size_t block_size = arena_size - MEMALLOC_FL_ARENA_PREFIX - MEMALLOC_FL_ARENA_SUFFIX;
    memalloc_fl_set_tags(block, block_size, true);

    /* Epilogue sentinel: marked allocated so forward coalescing never walks
     * past the arena. */
    MemallocFLHeader* epilogue = memalloc_fl_next_block(block);
    memalloc_fl_set_tags(epilogue, kHeaderSize, false);

    memalloc_fl_insert_free(fl, block);
    return block;
}

/* --- public API ------------------------------------------------------------ */

void memalloc_freelist_init(MemallocFreeList* fl) {
    pthread_mutex_init(&fl->mutex, NULL);
    fl->free_list = NULL;
    fl->arenas = NULL;
}

void memalloc_freelist_destroy(MemallocFreeList* fl) {
    for (MemallocFLArenaHeader* arena = fl->arenas; arena != NULL;) {
        MemallocFLArenaHeader* next = arena->next;
        memalloc_munmap_region(arena, arena->total_size);
        arena = next;
    }
    pthread_mutex_destroy(&fl->mutex);
}

static void* memalloc_freelist_allocate_locked(MemallocFreeList* fl, size_t size) {
    size_t payload = size < 16 ? 16 : memalloc_align_up(size, 16);
    size_t total = payload + kOverhead;

    MemallocFLHeader* h = memalloc_fl_find_fit(fl, total);
    if (!h) {
        h = memalloc_fl_extend(fl, total);
        if (!h) return NULL;
    }
    memalloc_fl_remove_free(fl, h);
    h = memalloc_fl_split(fl, h, total);
    return memalloc_fl_payload_of(h);
}

void* memalloc_freelist_allocate(MemallocFreeList* fl, size_t size) {
    pthread_mutex_lock(&fl->mutex);
    void* p = memalloc_freelist_allocate_locked(fl, size);
    pthread_mutex_unlock(&fl->mutex);
    return p;
}

static void memalloc_freelist_deallocate_locked(MemallocFreeList* fl, void* p) {
    MemallocFLHeader* h = memalloc_fl_header_of(p);
    if (memalloc_fl_is_free(h)) {
        fprintf(stderr, "memalloc: double free or corruption detected at %p\n", p);
        abort();
    }
    memalloc_fl_set_tags(h, memalloc_fl_size_of(h), true);
    h = memalloc_fl_coalesce(fl, h);
    memalloc_fl_insert_free(fl, h);
}

void memalloc_freelist_deallocate(MemallocFreeList* fl, void* p) {
    pthread_mutex_lock(&fl->mutex);
    memalloc_freelist_deallocate_locked(fl, p);
    pthread_mutex_unlock(&fl->mutex);
}

void* memalloc_freelist_reallocate(MemallocFreeList* fl, void* p, size_t new_size) {
    pthread_mutex_lock(&fl->mutex);

    MemallocFLHeader* h = memalloc_fl_header_of(p);
    size_t old_total = memalloc_fl_size_of(h);
    size_t payload = new_size < 16 ? 16 : memalloc_align_up(new_size, 16);
    size_t new_total = payload + kOverhead;

    if (new_total <= old_total) {
        h = memalloc_fl_split(fl, h, new_total);
        pthread_mutex_unlock(&fl->mutex);
        return memalloc_fl_payload_of(h);
    }

    MemallocFLHeader* nxt = memalloc_fl_next_block(h);
    if (memalloc_fl_is_free(nxt) && old_total + memalloc_fl_size_of(nxt) >= new_total) {
        memalloc_fl_remove_free(fl, nxt);
        memalloc_fl_set_tags(h, old_total + memalloc_fl_size_of(nxt), false);
        h = memalloc_fl_split(fl, h, new_total);
        pthread_mutex_unlock(&fl->mutex);
        return memalloc_fl_payload_of(h);
    }

    void* new_p = memalloc_freelist_allocate_locked(fl, new_size);
    if (!new_p) {
        pthread_mutex_unlock(&fl->mutex);
        return NULL;
    }
    size_t old_payload = old_total - kOverhead;
    memcpy(new_p, p, old_payload < new_size ? old_payload : new_size);
    memalloc_freelist_deallocate_locked(fl, p);
    pthread_mutex_unlock(&fl->mutex);
    return new_p;
}

size_t memalloc_freelist_usable_size(void* p) {
    return memalloc_fl_size_of(memalloc_fl_header_of(p)) - kOverhead;
}

size_t memalloc_freelist_block_total_size(void* p) {
    return memalloc_fl_size_of(memalloc_fl_header_of(p));
}

size_t memalloc_freelist_free_block_count(MemallocFreeList* fl) {
    pthread_mutex_lock(&fl->mutex);
    size_t count = 0;
    for (MemallocFLHeader* h = fl->free_list; h != NULL;
         h = ((MemallocFLFreeNode*)memalloc_fl_payload_of(h))->next) {
        ++count;
    }
    pthread_mutex_unlock(&fl->mutex);
    return count;
}
