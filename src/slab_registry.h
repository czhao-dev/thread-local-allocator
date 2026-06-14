#pragma once

#include <cstddef>
#include <cstdint>
#include <shared_mutex>

namespace memalloc {

// Records the base addresses of every slab that has been mapped, so that
// `free(p)` can determine in O(1) whether `p` falls inside a slab (and
// therefore should be returned to a SlabPool) or belongs to the free-list
// heap. Backed by mmap'd memory directly -- it must never call malloc, since
// it is on the deallocation fast path of the malloc shim itself.
class SlabRegistry {
public:
    SlabRegistry() = default;
    ~SlabRegistry();

    SlabRegistry(const SlabRegistry&) = delete;
    SlabRegistry& operator=(const SlabRegistry&) = delete;

    void insert(std::uintptr_t slab_base);
    bool contains(std::uintptr_t slab_base) const;

private:
    void grow();

    mutable std::shared_mutex mutex_;
    std::uintptr_t* table_ = nullptr;  // open-addressed hash table, 0 = empty slot
    std::size_t capacity_ = 0;
    std::size_t count_ = 0;
};

}  // namespace memalloc
