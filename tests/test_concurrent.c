/* Multi-threaded stress test: many threads concurrently allocate/free
 * random-sized objects spanning both the slab pools and the free list,
 * writing and verifying a per-allocation byte pattern to catch any
 * corruption from races (intended to be run under ASan -- see
 * MEMALLOC_ENABLE_ASAN). */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memalloc/allocator.h"

#define MEMALLOC_TEST_THREADS 8
#define MEMALLOC_TEST_OPS_PER_THREAD 20000

typedef struct {
    void* p;
    size_t s;
} LiveAlloc;

static void* worker(void* arg) {
    unsigned seed = (unsigned)(uintptr_t)arg;

    LiveAlloc* live = malloc(MEMALLOC_TEST_OPS_PER_THREAD * sizeof(LiveAlloc));
    size_t live_count = 0;

    for (int i = 0; i < MEMALLOC_TEST_OPS_PER_THREAD; ++i) {
        if (live_count > 0 && rand_r(&seed) % 3 == 0) {
            size_t idx = rand_r(&seed) % live_count;
            void* p = live[idx].p;
            size_t s = live[idx].s;

            unsigned char pattern = (unsigned char)(uintptr_t)p;
            for (size_t j = 0; j < s; ++j) {
                if (((unsigned char*)p)[j] != pattern) {
                    fprintf(stderr, "heap corruption detected at %p+%zu\n", p, j);
                    abort();
                }
            }
            memalloc_deallocate(p);
            live[idx] = live[live_count - 1];
            --live_count;
        } else {
            size_t s = 1 + rand_r(&seed) % 4096;
            void* p = memalloc_allocate(s);
            if (!p) {
                fprintf(stderr, "allocate(%zu) returned NULL\n", s);
                abort();
            }
            unsigned char pattern = (unsigned char)(uintptr_t)p;
            memset(p, pattern, s);
            live[live_count].p = p;
            live[live_count].s = s;
            ++live_count;
        }
    }

    for (size_t i = 0; i < live_count; ++i) memalloc_deallocate(live[i].p);
    free(live);
    return NULL;
}

int main(void) {
    pthread_t threads[MEMALLOC_TEST_THREADS];
    for (int i = 0; i < MEMALLOC_TEST_THREADS; ++i) {
        pthread_create(&threads[i], NULL, worker, (void*)(uintptr_t)i);
    }
    for (int i = 0; i < MEMALLOC_TEST_THREADS; ++i) pthread_join(threads[i], NULL);

    printf("concurrent: OK (%d threads x %d ops)\n", MEMALLOC_TEST_THREADS,
           MEMALLOC_TEST_OPS_PER_THREAD);
    return 0;
}
