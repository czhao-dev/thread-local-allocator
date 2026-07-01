#include "memalloc/allocator.h"

#include <cstdint>
#include <cstring>

#include "memalloc/common.h"
#include "memalloc/free_list.h"
#include "memalloc/slab_pool.h"
#include "memalloc/thread_cache.h"
#include "slab_registry.h"

namespace memalloc {

namespace {

// size_class is one of kSlabSizeClasses (a power of two from 8 to 512);
// maps it to the corresponding index into Allocator::pools_.
inline std::size_t pool_index(std::size_t size_class) {
    return static_cast<std::size_t>(__builtin_ctz(static_cast<unsigned>(size_class))) - 3;
}

// Top-level facade: dispatches allocate/deallocate/reallocate between the
// per-thread cache -> slab pools (small, fixed-size requests) and the
// boundary-tag free list (everything above kSlabThreshold). See README.
class Allocator {
public:
    static Allocator& instance() {
        // Leaked intentionally: avoids a destruction-order race between this
        // singleton's static destructor and any thread_local ThreadCache
        // destructors still flushing on the main thread at program exit.
        static Allocator& inst = *new Allocator();
        return inst;
    }

    void* allocate(std::size_t size) {
        std::size_t cls = slab_class_for(size);
        if (cls != 0) return thread_cache_for(pools_).allocate(pool_index(cls));
        return free_list_.allocate(size);
    }

    void deallocate(void* p) {
        if (!p) return;
        if (SlabPool::SlabHeader* slab = slab_owner(p)) {
            thread_cache_for(pools_).deallocate(pool_index(slab->slot_size), p);
        } else {
            free_list_.deallocate(p);
        }
    }

    void* reallocate(void* p, std::size_t new_size) {
        if (!p) return allocate(new_size);
        if (new_size == 0) {
            deallocate(p);
            return nullptr;
        }

        if (SlabPool::SlabHeader* slab = slab_owner(p)) {
            std::size_t old_size = slab->slot_size;
            if (new_size <= old_size) return p;
            void* np = allocate(new_size);
            if (!np) return nullptr;
            std::memcpy(np, p, old_size);
            thread_cache_for(pools_).deallocate(pool_index(old_size), p);
            return np;
        }
        return free_list_.reallocate(p, new_size);
    }

    std::size_t usable_size(void* p) {
        if (!p) return 0;
        if (SlabPool::SlabHeader* slab = slab_owner(p)) return slab->slot_size;
        return FreeListAllocator::usable_size(p);
    }

private:
    Allocator()
        : pools_{SlabPool(kSlabSizeClasses[0], registry_),
                 SlabPool(kSlabSizeClasses[1], registry_),
                 SlabPool(kSlabSizeClasses[2], registry_),
                 SlabPool(kSlabSizeClasses[3], registry_),
                 SlabPool(kSlabSizeClasses[4], registry_),
                 SlabPool(kSlabSizeClasses[5], registry_),
                 SlabPool(kSlabSizeClasses[6], registry_)} {}

    // Returns the slab header owning `p`, or nullptr if `p` was not
    // allocated from a slab pool (i.e. it belongs to the free list).
    SlabPool::SlabHeader* slab_owner(void* p) const {
        auto base = reinterpret_cast<std::uintptr_t>(p) & ~(SlabPool::kSlabSize - 1);
        if (!registry_.contains(base)) return nullptr;
        return reinterpret_cast<SlabPool::SlabHeader*>(base);
    }

    SlabRegistry       registry_;
    SlabPool           pools_[kNumSlabSizeClasses];
    FreeListAllocator  free_list_;
};

}  // namespace

void* allocate(std::size_t size) { return Allocator::instance().allocate(size); }

void deallocate(void* p) noexcept { Allocator::instance().deallocate(p); }

void* reallocate(void* p, std::size_t new_size) {
    return Allocator::instance().reallocate(p, new_size);
}

std::size_t usable_size(void* p) noexcept { return Allocator::instance().usable_size(p); }

}  // namespace memalloc
