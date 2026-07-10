#include "mmap_utils.h"

#include <sys/mman.h>

#include <stdint.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

void* memalloc_mmap_region(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

void* memalloc_mmap_aligned(size_t size, size_t alignment) {
    /* Over-allocate so that an aligned subrange of `size` bytes is
     * guaranteed to exist, then trim the unused head/tail back to the OS. */
    size_t request = size + alignment;
    void* p = mmap(NULL, request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;

    uintptr_t addr = (uintptr_t)p;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);

    size_t front_slack = aligned - addr;
    size_t back_slack = request - front_slack - size;

    if (front_slack) munmap(p, front_slack);
    if (back_slack) munmap((void*)(aligned + size), back_slack);

    return (void*)aligned;
}

void memalloc_munmap_region(void* ptr, size_t size) { munmap(ptr, size); }
