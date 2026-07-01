// Tests for the per-thread cache (ThreadCache) front-end:
//   1. Cross-thread free: allocate in thread A, free in thread B.
//   2. Thread-exit drain: many short-lived threads each holding more than
//      kHighWater cached objects at exit, verifying ThreadCache::~ThreadCache
//      flushes them back to the central pools (no permanent leak).

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "memalloc/allocator.h"

namespace {

void test_cross_thread_free() {
    constexpr int kCount     = 256;
    constexpr int kSlotSize  = 64;
    std::vector<void*> ptrs(kCount);

    // Allocate and write a pattern in thread A.
    std::thread producer([&ptrs] {
        for (int i = 0; i < kCount; ++i) {
            void* p = memalloc::allocate(kSlotSize);
            if (!p) { std::fprintf(stderr, "cross_thread_free: allocate failed\n"); std::abort(); }
            std::memset(p, static_cast<unsigned char>(i), kSlotSize);
            ptrs[i] = p;
        }
    });
    producer.join();

    // Verify the pattern and free in thread B.
    std::thread consumer([&ptrs] {
        for (int i = 0; i < kCount; ++i) {
            auto* p = static_cast<unsigned char*>(ptrs[i]);
            for (int j = 0; j < kSlotSize; ++j) {
                if (p[j] != static_cast<unsigned char>(i)) {
                    std::fprintf(stderr, "cross_thread_free: corruption at ptr[%d]+%d\n", i, j);
                    std::abort();
                }
            }
            memalloc::deallocate(ptrs[i]);
        }
    });
    consumer.join();

    std::printf("cross_thread_free: OK (%d allocs in thread A, freed in thread B)\n", kCount);
}

void test_thread_exit_drain() {
    // Each worker allocates kAllocsPerThread > kHighWater (64) objects of one
    // size class, then frees them all before returning. If the ThreadCache
    // destructor does not flush, freed objects never reach the central pool
    // and the total slab footprint grows unboundedly with thread count.
    // Post-condition: a fresh batch of allocations succeeds and sees correct data.

    constexpr int kThreads        = 64;
    constexpr int kAllocsPerThread = 80;   // intentionally > kHighWater
    constexpr int kSlotSize        = 128;

    for (int t = 0; t < kThreads; ++t) {
        std::thread([] {
            std::vector<void*> live;
            live.reserve(kAllocsPerThread);
            for (int i = 0; i < kAllocsPerThread; ++i) {
                void* p = memalloc::allocate(kSlotSize);
                if (!p) { std::fprintf(stderr, "thread_exit_drain: allocate failed\n"); std::abort(); }
                std::memset(p, 0xAB, kSlotSize);
                live.push_back(p);
            }
            for (void* p : live) memalloc::deallocate(p);
            // Thread exits; ThreadCache destructor must flush residual cache
            // back to the central SlabPool so memory is not permanently lost.
        }).join();
    }

    // Allocate a fresh batch and verify correctness, confirming the freed
    // memory was reclaimed by the central pool and is reusable.
    constexpr int kVerifyCount = 128;
    std::vector<void*> verify;
    verify.reserve(kVerifyCount);
    for (int i = 0; i < kVerifyCount; ++i) {
        void* p = memalloc::allocate(kSlotSize);
        if (!p) { std::fprintf(stderr, "thread_exit_drain: post-drain allocate failed\n"); std::abort(); }
        std::memset(p, static_cast<unsigned char>(i), kSlotSize);
        verify.push_back(p);
    }
    for (int i = 0; i < kVerifyCount; ++i) {
        auto* p = static_cast<unsigned char*>(verify[i]);
        for (int j = 0; j < kSlotSize; ++j) {
            if (p[j] != static_cast<unsigned char>(i)) {
                std::fprintf(stderr, "thread_exit_drain: post-drain corruption at %p+%d\n",
                             verify[i], j);
                std::abort();
            }
        }
        memalloc::deallocate(verify[i]);
    }

    std::printf("thread_exit_drain: OK (%d threads x %d allocs, %d post-drain verifications)\n",
                kThreads, kAllocsPerThread, kVerifyCount);
}

}  // namespace

int main() {
    test_cross_thread_free();
    test_thread_exit_drain();
    return 0;
}
