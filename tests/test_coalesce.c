/* Verifies that freeing adjacent blocks produces correct merged block sizes
 * via immediate boundary-tag coalescing (README "Boundary Tags (Knuth, 1973)"). */

#include <stdio.h>

#include "memalloc/free_list.h"

int main(void) {
    MemallocFreeList fl;
    memalloc_freelist_init(&fl);

    /* Three adjacent in-use blocks carved from one initial free block. */
    void* a = memalloc_freelist_allocate(&fl, 64);
    void* b = memalloc_freelist_allocate(&fl, 64);
    void* c = memalloc_freelist_allocate(&fl, 64);

    size_t size_a = memalloc_freelist_block_total_size(a);
    size_t size_b = memalloc_freelist_block_total_size(b);
    size_t size_c = memalloc_freelist_block_total_size(c);

    /* Freeing b then a should coalesce backward: a's footer-check finds b
     * free and merges them into a single block of size_a + size_b. */
    memalloc_freelist_deallocate(&fl, b);
    memalloc_freelist_deallocate(&fl, a);

    if (memalloc_freelist_free_block_count(&fl) != 2) {
        fprintf(stderr, "expected 2 free blocks after merging a+b, got %zu\n",
                memalloc_freelist_free_block_count(&fl));
        return 1;
    }

    /* Freeing c should coalesce both backward (into the merged a+b block)
     * and forward (into the remainder of the arena), leaving exactly one
     * free block spanning the whole arena. */
    memalloc_freelist_deallocate(&fl, c);

    if (memalloc_freelist_free_block_count(&fl) != 1) {
        fprintf(stderr, "expected 1 free block after full coalesce, got %zu\n",
                memalloc_freelist_free_block_count(&fl));
        return 1;
    }

    /* A subsequent allocation that needs the full coalesced region should
     * succeed without growing the heap. */
    void* big = memalloc_freelist_allocate(&fl, size_a + size_b + size_c - 3 * 16);
    if (!big) {
        fprintf(stderr, "allocation into coalesced block failed\n");
        return 1;
    }

    memalloc_freelist_destroy(&fl);

    printf("coalesce: OK\n");
    return 0;
}
