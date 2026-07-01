#include "memalloc/thread_cache.h"

#include "memalloc/slab_pool.h"

namespace memalloc {

ThreadCache::ThreadCache(SlabPool* pools) noexcept : pools_(pools) {}

ThreadCache::~ThreadCache() {
    flush_all();
    dead_ = true;
}

void ThreadCache::flush_all() {
    for (std::size_t i = 0; i < kNumSlabSizeClasses; ++i) {
        Bucket& b = buckets_[i];
        if (b.count > 0) {
            pools_[i].deallocate_batch(b.head, b.count);
            b.head  = nullptr;
            b.count = 0;
        }
    }
}

void* ThreadCache::allocate(std::size_t idx) {
    if (dead_) return pools_[idx].allocate();

    Bucket& b = buckets_[idx];
    if (b.count == 0) {
        std::uint32_t got = 0;
        b.head  = pools_[idx].allocate_batch(kRefillBatch, &got);
        b.count = got;
        if (b.count == 0) return nullptr;  // OOM
    }
    void* p = b.head;
    b.head = *static_cast<void**>(p);
    --b.count;
    return p;
}

void ThreadCache::deallocate(std::size_t idx, void* p) {
    if (dead_) { pools_[idx].deallocate(p); return; }

    Bucket& b = buckets_[idx];
    *static_cast<void**>(p) = b.head;
    b.head = p;
    ++b.count;

    if (b.count > kHighWater) {
        // Split off kRefillBatch objects from the front of the bucket to flush.
        void* flush_head = b.head;
        void* cursor     = b.head;
        for (std::uint32_t i = 1; i < kRefillBatch; ++i)
            cursor = *static_cast<void**>(cursor);
        b.head   = *static_cast<void**>(cursor);
        b.count -= kRefillBatch;
        *static_cast<void**>(cursor) = nullptr;  // terminate the flush sublist
        pools_[idx].deallocate_batch(flush_head, kRefillBatch);
    }
}

ThreadCache& thread_cache_for(SlabPool* pools) noexcept {
    // pools is used only on first construction per thread; subsequent calls
    // with the same (singleton) pools pointer are harmless.
    static thread_local ThreadCache tls_cache(pools);
    return tls_cache;
}

}  // namespace memalloc
