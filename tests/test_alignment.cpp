// Verifies that every allocation -- across all slab size classes and the
// free-list path -- is aligned to its size class (per README "Cache
// Alignment": slab slots are aligned to the slot size, which satisfies the
// alignment requirements of any object that fits in that size class), and
// that free-list allocations (> kSlabThreshold) get the default 16-byte
// alignment. Also checks that usable_size() never reports less than what
// was requested.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "memalloc/allocator.h"
#include "memalloc/common.h"

int main() {
    const std::size_t sizes[] = {1,   2,   7,   8,    9,    15,   16,  17,
                                  31,  32,  33,  63,   64,   65,   100, 128,
                                  200, 256, 400, 512,  513,  1000, 4096, 1 << 20};

    std::vector<void*> ptrs;
    for (std::size_t s : sizes) {
        void* p = memalloc::allocate(s);
        if (!p) {
            std::fprintf(stderr, "allocate(%zu) returned nullptr\n", s);
            return 1;
        }
        std::size_t cls = memalloc::slab_class_for(s);
        std::size_t expected_align = cls != 0 ? cls : memalloc::kDefaultAlign;

        auto addr = reinterpret_cast<std::uintptr_t>(p);
        if (addr % expected_align != 0) {
            std::fprintf(stderr, "allocate(%zu) = %p is not %zu-byte aligned\n", s, p,
                          expected_align);
            return 1;
        }
        if (memalloc::usable_size(p) < s) {
            std::fprintf(stderr, "usable_size(%p) < requested size %zu\n", p, s);
            return 1;
        }
        ptrs.push_back(p);
    }

    for (void* p : ptrs) memalloc::deallocate(p);

    std::printf("alignment: OK (%zu sizes checked)\n", sizeof(sizes) / sizeof(sizes[0]));
    return 0;
}
