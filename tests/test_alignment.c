/* Verifies that every allocation -- across all slab size classes and the
 * free-list path -- is aligned to its size class (per README "Cache
 * Alignment": slab slots are aligned to the slot size, which satisfies the
 * alignment requirements of any object that fits in that size class), and
 * that free-list allocations (> MEMALLOC_SLAB_THRESHOLD) get the default
 * 16-byte alignment. Also checks that usable_size() never reports less than
 * what was requested. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "memalloc/allocator.h"
#include "memalloc/common.h"

int main(void) {
    const size_t sizes[] = {1,   2,   7,   8,    9,    15,   16,  17,
                             31,  32,  33,  63,   64,   65,   100, 128,
                             200, 256, 400, 512,  513,  1000, 4096, 1 << 20};
    const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    void** ptrs = malloc(num_sizes * sizeof(void*));
    for (size_t k = 0; k < num_sizes; ++k) {
        size_t s = sizes[k];
        void* p = memalloc_allocate(s);
        if (!p) {
            fprintf(stderr, "allocate(%zu) returned NULL\n", s);
            return 1;
        }
        size_t cls = memalloc_slab_class_for(s);
        size_t expected_align = cls != 0 ? cls : MEMALLOC_DEFAULT_ALIGN;

        uintptr_t addr = (uintptr_t)p;
        if (addr % expected_align != 0) {
            fprintf(stderr, "allocate(%zu) = %p is not %zu-byte aligned\n", s, p, expected_align);
            return 1;
        }
        if (memalloc_usable_size(p) < s) {
            fprintf(stderr, "usable_size(%p) < requested size %zu\n", p, s);
            return 1;
        }
        ptrs[k] = p;
    }

    for (size_t k = 0; k < num_sizes; ++k) memalloc_deallocate(ptrs[k]);
    free(ptrs);

    printf("alignment: OK (%zu sizes checked)\n", num_sizes);
    return 0;
}
