/* Workload 1 (README): repeatedly allocate and free 64-byte objects from
 * several concurrent threads -- the case the slab allocator is built for.
 *
 * Uses plain malloc/free so this binary can be run unmodified, or with
 * libmemalloc preloaded (LD_PRELOAD / DYLD_INSERT_LIBRARIES), to compare
 * against the system allocator. */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "bench_common.h"

#define MEMALLOC_BENCH_THREADS 8
#define MEMALLOC_BENCH_OPS_PER_THREAD 2000000
#define MEMALLOC_BENCH_LIVE_SLOTS 64
#define MEMALLOC_BENCH_OBJ_SIZE 64

static void* worker(void* arg) {
    (void)arg;
    void* slots[MEMALLOC_BENCH_LIVE_SLOTS] = {0};
    for (int i = 0; i < MEMALLOC_BENCH_OPS_PER_THREAD; ++i) {
        int idx = i % MEMALLOC_BENCH_LIVE_SLOTS;
        if (slots[idx]) free(slots[idx]);
        slots[idx] = malloc(MEMALLOC_BENCH_OBJ_SIZE);
    }
    for (int i = 0; i < MEMALLOC_BENCH_LIVE_SLOTS; ++i) free(slots[i]);
    return NULL;
}

int main(void) {
    bench_time_point start = bench_now();

    pthread_t threads[MEMALLOC_BENCH_THREADS];
    for (int i = 0; i < MEMALLOC_BENCH_THREADS; ++i) pthread_create(&threads[i], NULL, worker, NULL);
    for (int i = 0; i < MEMALLOC_BENCH_THREADS; ++i) pthread_join(threads[i], NULL);

    bench_time_point end = bench_now();
    double secs = bench_seconds_between(start, end);
    double total_ops = (double)MEMALLOC_BENCH_THREADS * MEMALLOC_BENCH_OPS_PER_THREAD * 2;  /* alloc + free */

    printf("fixed_size_bench: %d threads x %d iters of %dB\n", MEMALLOC_BENCH_THREADS,
           MEMALLOC_BENCH_OPS_PER_THREAD, MEMALLOC_BENCH_OBJ_SIZE);
    printf("  wall time:  %.3f s\n", secs);
    printf("  throughput: %.2f Mops/sec\n", total_ops / secs / 1e6);
    printf("  peak RSS:   %.1f MB\n", bench_peak_rss_bytes() / 1e6);
    return 0;
}
