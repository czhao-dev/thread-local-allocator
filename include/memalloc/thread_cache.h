#pragma once

#include <cstddef>
#include <cstdint>

#include "memalloc/common.h"

namespace memalloc {

class SlabPool;

// Per-thread front-end cache for the slab allocator. Each thread holds one
// ThreadCache with a small free list per size class (8..512B). On the common
// path there is no locking — allocation/deallocation are a pointer pop/push.
//
// When a bucket empties, kRefillBatch slots are pulled from the central
// SlabPool in a single locked call. When a bucket exceeds kHighWater,
// kRefillBatch slots are flushed back in a single locked call (hysteresis
// keeps the bucket near kHighWater/2, avoiding flush/refill thrashing at the
// boundary).
//
// Thread-exit: the destructor flushes all buckets back to the central pools.
// If malloc is called again on this thread after the destructor has run (e.g.
// by another library's TLS destructor), the dead_ guard redirects all
// operations directly to the central SlabPool, bypassing the stale cache.
class ThreadCache {
public:
    static constexpr std::uint32_t kRefillBatch = 32;
    static constexpr std::uint32_t kHighWater   = 64;

    explicit ThreadCache(SlabPool* pools) noexcept;
    ~ThreadCache();

    ThreadCache(const ThreadCache&) = delete;
    ThreadCache& operator=(const ThreadCache&) = delete;

    // Returns a slot for pool index `idx`, or nullptr on OOM. If called after
    // thread-exit destruction, bypasses the cache and goes to the central pool.
    void* allocate(std::size_t idx);

    // Returns `p` to the cache for pool index `idx`. Flushes a batch to the
    // central pool if the bucket exceeds kHighWater. If called after
    // thread-exit destruction, goes directly to the central pool.
    void deallocate(std::size_t idx, void* p);

    // Flushes all cached slots back to their central pools. Safe to call
    // multiple times (second call is a no-op).
    void flush_all();

private:
    struct Bucket {
        void*         head  = nullptr;
        std::uint32_t count = 0;
    };

    SlabPool*  pools_;
    Bucket     buckets_[kNumSlabSizeClasses];
    bool       dead_ = false;  // true after destructor runs; guards post-exit calls
};

// Returns the calling thread's ThreadCache, constructing it on first call.
// `pools` must point to the Allocator's slab pool array and must outlive the
// calling thread (guaranteed when the Allocator uses a leaked singleton).
ThreadCache& thread_cache_for(SlabPool* pools) noexcept;

}  // namespace memalloc
