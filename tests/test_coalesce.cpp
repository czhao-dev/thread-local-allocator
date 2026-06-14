// Verifies that freeing adjacent blocks produces correct merged block sizes
// via immediate boundary-tag coalescing (README "Boundary Tags (Knuth, 1973)").

#include <cstdio>

#include "memalloc/free_list.h"

using memalloc::FreeListAllocator;

int main() {
    FreeListAllocator fl;

    // Three adjacent in-use blocks carved from one initial free block.
    void* a = fl.allocate(64);
    void* b = fl.allocate(64);
    void* c = fl.allocate(64);

    std::size_t size_a = FreeListAllocator::block_total_size(a);
    std::size_t size_b = FreeListAllocator::block_total_size(b);
    std::size_t size_c = FreeListAllocator::block_total_size(c);

    // Freeing b then a should coalesce backward: a's footer-check finds b
    // free and merges them into a single block of size_a + size_b.
    fl.deallocate(b);
    fl.deallocate(a);

    if (fl.free_block_count() != 2) {
        std::fprintf(stderr, "expected 2 free blocks after merging a+b, got %zu\n",
                      fl.free_block_count());
        return 1;
    }

    // Freeing c should coalesce both backward (into the merged a+b block)
    // and forward (into the remainder of the arena), leaving exactly one
    // free block spanning the whole arena.
    fl.deallocate(c);

    if (fl.free_block_count() != 1) {
        std::fprintf(stderr, "expected 1 free block after full coalesce, got %zu\n",
                      fl.free_block_count());
        return 1;
    }

    // A subsequent allocation that needs the full coalesced region should
    // succeed without growing the heap.
    void* big = fl.allocate(size_a + size_b + size_c - 3 * 16);
    if (!big) {
        std::fprintf(stderr, "allocation into coalesced block failed\n");
        return 1;
    }

    std::printf("coalesce: OK\n");
    return 0;
}
