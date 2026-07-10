/* Workload 2 (README): allocate objects with sizes drawn from a log-uniform
 * distribution over [8, 4096] bytes, hold a random subset live and free the
 * rest, for 10M operations -- the general-case workload. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bench_common.h"

#define MEMALLOC_BENCH_TOTAL_OPS 10000000
#define MEMALLOC_BENCH_MIN_SIZE 8
#define MEMALLOC_BENCH_MAX_SIZE 4096
#define MEMALLOC_BENCH_FREE_PROBABILITY 0.5
#define MEMALLOC_BENCH_SAMPLE_INTERVAL 100000

typedef struct {
    void* p;
    size_t s;
} LiveAlloc;

/* splitmix64 -- small, fast, deterministic PRNG so this benchmark is
 * reproducible across runs (mirrors the fixed std::mt19937_64 seed used by
 * the original C++ version). */
static uint64_t rng_state;

static uint64_t next_u64(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static double next_unit(void) { return (double)(next_u64() >> 11) * (1.0 / 9007199254740992.0); }

int main(void) {
    rng_state = 42;

    const double log_min = log((double)MEMALLOC_BENCH_MIN_SIZE);
    const double log_max = log((double)MEMALLOC_BENCH_MAX_SIZE);

    size_t live_capacity = 1024;
    size_t live_count = 0;
    LiveAlloc* live = malloc(live_capacity * sizeof(LiveAlloc));

    size_t live_bytes = 0;
    double peak_rss = 0;

    bench_time_point start = bench_now();
    for (int i = 0; i < MEMALLOC_BENCH_TOTAL_OPS; ++i) {
        if (live_count > 0 && next_unit() < MEMALLOC_BENCH_FREE_PROBABILITY) {
            size_t idx = next_u64() % live_count;
            live_bytes -= live[idx].s;
            free(live[idx].p);
            live[idx] = live[live_count - 1];
            --live_count;
        } else {
            double t = log_min + next_unit() * (log_max - log_min);
            size_t size = (size_t)exp(t);
            void* p = malloc(size);
            live_bytes += size;
            if (live_count == live_capacity) {
                live_capacity *= 2;
                live = realloc(live, live_capacity * sizeof(LiveAlloc));
            }
            live[live_count].p = p;
            live[live_count].s = size;
            ++live_count;
        }
        if (i % MEMALLOC_BENCH_SAMPLE_INTERVAL == 0) {
            double rss = bench_peak_rss_bytes();
            if (rss > peak_rss) peak_rss = rss;
        }
    }
    bench_time_point end = bench_now();

    double secs = bench_seconds_between(start, end);
    double final_rss = bench_peak_rss_bytes();
    if (final_rss > peak_rss) peak_rss = final_rss;
    double fragmentation = peak_rss > 0 ? 1.0 - (double)live_bytes / peak_rss : 0.0;

    printf("mixed_size_bench: %d ops, sizes in [%d, %d]\n", MEMALLOC_BENCH_TOTAL_OPS,
           MEMALLOC_BENCH_MIN_SIZE, MEMALLOC_BENCH_MAX_SIZE);
    printf("  wall time:    %.3f s\n", secs);
    printf("  throughput:   %.2f Mops/sec\n", MEMALLOC_BENCH_TOTAL_OPS / secs / 1e6);
    printf("  live bytes:   %.1f MB (%zu objects)\n", live_bytes / 1e6, live_count);
    printf("  peak RSS:     %.1f MB\n", peak_rss / 1e6);
    printf("  fragmentation: %.1f%%\n", fragmentation * 100.0);

    for (size_t i = 0; i < live_count; ++i) free(live[i].p);
    free(live);
    return 0;
}
