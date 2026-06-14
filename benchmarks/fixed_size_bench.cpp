// Workload 1 (README): repeatedly allocate and free 64-byte objects from
// several concurrent threads -- the case the slab allocator is built for.
//
// Uses plain malloc/free so this binary can be run unmodified, or with
// libmemalloc preloaded (LD_PRELOAD / DYLD_INSERT_LIBRARIES), to compare
// against the system allocator.

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "bench_common.h"

namespace {

constexpr int kThreads = 8;
constexpr int kOpsPerThread = 2'000'000;
constexpr int kLiveSlots = 64;
constexpr std::size_t kObjSize = 64;

void worker() {
    void* slots[kLiveSlots] = {};
    for (int i = 0; i < kOpsPerThread; ++i) {
        int idx = i % kLiveSlots;
        if (slots[idx]) std::free(slots[idx]);
        slots[idx] = std::malloc(kObjSize);
    }
    for (void* p : slots) std::free(p);
}

}  // namespace

int main() {
    auto start = bench::now();

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    auto end = bench::now();
    double secs = bench::seconds_between(start, end);
    double total_ops = static_cast<double>(kThreads) * kOpsPerThread * 2;  // alloc + free

    std::printf("fixed_size_bench: %d threads x %d iters of %zuB\n", kThreads, kOpsPerThread,
                 kObjSize);
    std::printf("  wall time:  %.3f s\n", secs);
    std::printf("  throughput: %.2f Mops/sec\n", total_ops / secs / 1e6);
    std::printf("  peak RSS:   %.1f MB\n", bench::peak_rss_bytes() / 1e6);
    return 0;
}
