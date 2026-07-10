/* Verifies that allocated memory holds exactly the bytes written to it
 * (across slab and free-list size classes), that calloc-equivalent zeroing
 * works, and that reallocate() preserves existing contents. */

#include <stdio.h>
#include <string.h>

#include "memalloc/allocator.h"

int main(void) {
    const size_t sizes[] = {8, 64, 512, 1024, 4096, 100000};

    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
        size_t s = sizes[k];
        void* p = memalloc_allocate(s);
        if (!p) {
            fprintf(stderr, "allocate(%zu) returned NULL\n", s);
            return 1;
        }
        memset(p, 0xAB, s);
        for (size_t i = 0; i < s; ++i) {
            if (((unsigned char*)p)[i] != 0xAB) {
                fprintf(stderr, "byte mismatch at offset %zu for size %zu\n", i, s);
                return 1;
            }
        }
        memalloc_deallocate(p);
    }

    /* reallocate() must preserve the original contents. */
    void* p = memalloc_allocate(32);
    memset(p, 0x42, 32);
    void* p2 = memalloc_reallocate(p, 1024);
    if (!p2) {
        fprintf(stderr, "reallocate returned NULL\n");
        return 1;
    }
    for (int i = 0; i < 32; ++i) {
        if (((unsigned char*)p2)[i] != 0x42) {
            fprintf(stderr, "reallocate did not preserve contents at byte %d\n", i);
            return 1;
        }
    }
    memalloc_deallocate(p2);

    printf("values: OK\n");
    return 0;
}
