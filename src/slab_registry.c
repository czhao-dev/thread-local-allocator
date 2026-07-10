#include "slab_registry.h"

#include <stdio.h>
#include <stdlib.h>

#include "mmap_utils.h"

#define MEMALLOC_SLABREGISTRY_INITIAL_CAPACITY ((size_t)1024)  /* power of two */

static size_t memalloc_slabregistry_slot_for(uintptr_t addr, size_t capacity) {
    /* Slab bases are aligned to a large power of two, so shift away the
     * common zero bits before applying a multiplicative hash. */
    uint64_t h = (addr >> 16) * 0x9E3779B97F4A7C15ull;
    return (size_t)h & (capacity - 1);
}

void memalloc_slabregistry_init(MemallocSlabRegistry* reg) {
    pthread_rwlock_init(&reg->rwlock, NULL);
    reg->table = NULL;
    reg->capacity = 0;
    reg->count = 0;
}

void memalloc_slabregistry_destroy(MemallocSlabRegistry* reg) {
    if (reg->table) memalloc_munmap_region(reg->table, reg->capacity * sizeof(uintptr_t));
    pthread_rwlock_destroy(&reg->rwlock);
}

static void memalloc_slabregistry_grow(MemallocSlabRegistry* reg) {
    size_t new_capacity = reg->capacity == 0 ? MEMALLOC_SLABREGISTRY_INITIAL_CAPACITY : reg->capacity * 2;
    uintptr_t* new_table = (uintptr_t*)memalloc_mmap_region(new_capacity * sizeof(uintptr_t));
    if (!new_table) {
        fprintf(stderr, "memalloc: failed to grow slab registry (out of memory)\n");
        abort();
    }

    if (reg->table) {
        for (size_t i = 0; i < reg->capacity; ++i) {
            uintptr_t addr = reg->table[i];
            if (addr == 0) continue;
            size_t slot = memalloc_slabregistry_slot_for(addr, new_capacity);
            while (new_table[slot] != 0) slot = (slot + 1) & (new_capacity - 1);
            new_table[slot] = addr;
        }
        memalloc_munmap_region(reg->table, reg->capacity * sizeof(uintptr_t));
    }

    reg->table = new_table;
    reg->capacity = new_capacity;
}

void memalloc_slabregistry_insert(MemallocSlabRegistry* reg, uintptr_t slab_base) {
    pthread_rwlock_wrlock(&reg->rwlock);
    if (reg->capacity == 0 || (reg->count + 1) * 2 > reg->capacity) memalloc_slabregistry_grow(reg);

    size_t slot = memalloc_slabregistry_slot_for(slab_base, reg->capacity);
    while (reg->table[slot] != 0) {
        if (reg->table[slot] == slab_base) {
            pthread_rwlock_unlock(&reg->rwlock);
            return;  /* already registered */
        }
        slot = (slot + 1) & (reg->capacity - 1);
    }
    reg->table[slot] = slab_base;
    ++reg->count;
    pthread_rwlock_unlock(&reg->rwlock);
}

bool memalloc_slabregistry_contains(MemallocSlabRegistry* reg, uintptr_t slab_base) {
    pthread_rwlock_rdlock(&reg->rwlock);
    if (reg->capacity == 0) {
        pthread_rwlock_unlock(&reg->rwlock);
        return false;
    }

    size_t slot = memalloc_slabregistry_slot_for(slab_base, reg->capacity);
    while (reg->table[slot] != 0) {
        if (reg->table[slot] == slab_base) {
            pthread_rwlock_unlock(&reg->rwlock);
            return true;
        }
        slot = (slot + 1) & (reg->capacity - 1);
    }
    pthread_rwlock_unlock(&reg->rwlock);
    return false;
}
