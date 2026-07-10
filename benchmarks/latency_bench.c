/* Workload 3 (README): single-threaded p50/p99/p999 allocation latency for
 * 64-byte objects. */

#include <stdio.h>
#include <stdlib.h>

#include "bench_common.h"

#define MEMALLOC_BENCH_ITERATIONS 500000
#define MEMALLOC_BENCH_OBJ_SIZE 64

static int cmp_int64(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a;
    int64_t y = *(const int64_t*)b;
    return (x > y) - (x < y);
}

int main(void) {
    int64_t* latencies = malloc(MEMALLOC_BENCH_ITERATIONS * sizeof(int64_t));
    void** ptrs = malloc(MEMALLOC_BENCH_ITERATIONS * sizeof(void*));

    for (int i = 0; i < MEMALLOC_BENCH_ITERATIONS; ++i) {
        bench_time_point t0 = bench_now();
        ptrs[i] = malloc(MEMALLOC_BENCH_OBJ_SIZE);
        bench_time_point t1 = bench_now();
        latencies[i] = bench_nanos_between(t0, t1);
    }
    for (int i = 0; i < MEMALLOC_BENCH_ITERATIONS; ++i) free(ptrs[i]);
    free(ptrs);

    qsort(latencies, MEMALLOC_BENCH_ITERATIONS, sizeof(int64_t), cmp_int64);

    printf("latency_bench: %d allocations of %dB\n", MEMALLOC_BENCH_ITERATIONS, MEMALLOC_BENCH_OBJ_SIZE);
    printf("  p50:  %lld ns\n",
           (long long)latencies[(size_t)(0.50 * (MEMALLOC_BENCH_ITERATIONS - 1))]);
    printf("  p99:  %lld ns\n",
           (long long)latencies[(size_t)(0.99 * (MEMALLOC_BENCH_ITERATIONS - 1))]);
    printf("  p999: %lld ns\n",
           (long long)latencies[(size_t)(0.999 * (MEMALLOC_BENCH_ITERATIONS - 1))]);

    free(latencies);
    return 0;
}
