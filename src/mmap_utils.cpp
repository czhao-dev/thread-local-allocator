#include "mmap_utils.h"

#include <sys/mman.h>

#include <cstdint>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace memalloc {

void* mmap_region(std::size_t size) {
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

void* mmap_aligned(std::size_t size, std::size_t alignment) {
    // Over-allocate so that an aligned subrange of `size` bytes is
    // guaranteed to exist, then trim the unused head/tail back to the OS.
    std::size_t request = size + alignment;
    void* p = mmap(nullptr, request, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;

    auto addr = reinterpret_cast<std::uintptr_t>(p);
    std::uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);

    std::size_t front_slack = aligned - addr;
    std::size_t back_slack = request - front_slack - size;

    if (front_slack) munmap(p, front_slack);
    if (back_slack) munmap(reinterpret_cast<void*>(aligned + size), back_slack);

    return reinterpret_cast<void*>(aligned);
}

void munmap_region(void* ptr, std::size_t size) { munmap(ptr, size); }

}  // namespace memalloc
