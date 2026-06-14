// Multi-threaded stress test: many threads concurrently allocate/free
// random-sized objects spanning both the slab pools and the free list,
// writing and verifying a per-allocation byte pattern to catch any
// corruption from races (intended to be run under ASan -- see
// MEMALLOC_ENABLE_ASAN).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "memalloc/allocator.h"

namespace {

constexpr int kThreads = 8;
constexpr int kOpsPerThread = 20000;

void worker(int seed) {
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_int_distribution<std::size_t> size_dist(1, 4096);

    std::vector<std::pair<void*, std::size_t>> live;
    for (int i = 0; i < kOpsPerThread; ++i) {
        if (!live.empty() && rng() % 3 == 0) {
            std::size_t idx = rng() % live.size();
            auto [p, s] = live[idx];

            unsigned char pattern = static_cast<unsigned char>(reinterpret_cast<std::uintptr_t>(p));
            for (std::size_t j = 0; j < s; ++j) {
                if (static_cast<unsigned char*>(p)[j] != pattern) {
                    std::fprintf(stderr, "heap corruption detected at %p+%zu\n", p, j);
                    std::abort();
                }
            }
            memalloc::deallocate(p);
            live[idx] = live.back();
            live.pop_back();
        } else {
            std::size_t s = size_dist(rng);
            void* p = memalloc::allocate(s);
            if (!p) {
                std::fprintf(stderr, "allocate(%zu) returned nullptr\n", s);
                std::abort();
            }
            unsigned char pattern = static_cast<unsigned char>(reinterpret_cast<std::uintptr_t>(p));
            std::memset(p, pattern, s);
            live.emplace_back(p, s);
        }
    }

    for (auto& [p, s] : live) {
        (void)s;
        memalloc::deallocate(p);
    }
}

}  // namespace

int main() {
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();

    std::printf("concurrent: OK (%d threads x %d ops)\n", kThreads, kOpsPerThread);
    return 0;
}
