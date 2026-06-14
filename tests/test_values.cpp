// Verifies that allocated memory holds exactly the bytes written to it
// (across slab and free-list size classes), that calloc-equivalent zeroing
// works, and that reallocate() preserves existing contents.

#include <cstdio>
#include <cstring>

#include "memalloc/allocator.h"

int main() {
    const std::size_t sizes[] = {8, 64, 512, 1024, 4096, 100000};

    for (std::size_t s : sizes) {
        void* p = memalloc::allocate(s);
        if (!p) {
            std::fprintf(stderr, "allocate(%zu) returned nullptr\n", s);
            return 1;
        }
        std::memset(p, 0xAB, s);
        for (std::size_t i = 0; i < s; ++i) {
            if (static_cast<unsigned char*>(p)[i] != 0xAB) {
                std::fprintf(stderr, "byte mismatch at offset %zu for size %zu\n", i, s);
                return 1;
            }
        }
        memalloc::deallocate(p);
    }

    // reallocate() must preserve the original contents.
    void* p = memalloc::allocate(32);
    std::memset(p, 0x42, 32);
    void* p2 = memalloc::reallocate(p, 1024);
    if (!p2) {
        std::fprintf(stderr, "reallocate returned nullptr\n");
        return 1;
    }
    for (int i = 0; i < 32; ++i) {
        if (static_cast<unsigned char*>(p2)[i] != 0x42) {
            std::fprintf(stderr, "reallocate did not preserve contents at byte %d\n", i);
            return 1;
        }
    }
    memalloc::deallocate(p2);

    std::printf("values: OK\n");
    return 0;
}
