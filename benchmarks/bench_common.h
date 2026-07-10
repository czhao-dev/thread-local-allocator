#ifndef MEMALLOC_BENCH_COMMON_H
#define MEMALLOC_BENCH_COMMON_H

#include <sys/resource.h>
#include <time.h>

#include <stdint.h>

typedef struct timespec bench_time_point;

static inline bench_time_point bench_now(void) {
    bench_time_point t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

static inline double bench_seconds_between(bench_time_point start, bench_time_point end) {
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static inline int64_t bench_nanos_between(bench_time_point start, bench_time_point end) {
    return (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL + (int64_t)(end.tv_nsec - start.tv_nsec);
}

/* Peak resident set size, in bytes, of this process so far. */
static inline double bench_peak_rss_bytes(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
    return (double)usage.ru_maxrss;  /* bytes on macOS */
#else
    return (double)usage.ru_maxrss * 1024.0;  /* kilobytes on Linux */
#endif
}

#endif  /* MEMALLOC_BENCH_COMMON_H */
