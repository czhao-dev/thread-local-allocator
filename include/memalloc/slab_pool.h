#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace memalloc {

class SlabRegistry;

// A pool of fixed-size slots carved out of mmap'd "slabs". Each slab embeds
// its own free list directly in the unused slots (see README "Slab
// Allocator Design"), so allocation/deallocation are O(1) pointer
// pop/push operations once the pool's mutex is held.
class SlabPool {
public:
    // Slabs are mapped at an address aligned to their own size, so that the
    // owning slab of any slot pointer can be found by masking off the low
    // log2(kSlabSize) bits. Must be a power of two.
    static constexpr std::size_t kSlabSize = 1u << 16;  // 64 KiB

    // Header stored at the start of every slab (i.e. at the slab's aligned
    // base address). Exposed so the allocator facade can identify which
    // pool owns a slot purely from its address.
    struct SlabHeader {
        SlabHeader* next_partial;  // next slab in this pool with a free slot
        SlabHeader* next_all;      // next slab in this pool, for teardown
        void* free_list;           // embedded free list of free slots
        std::uint32_t free_count;
        std::uint32_t slot_size;
    };

    // `registry` is notified of every slab base address mapped by this pool
    // so the allocator facade can route deallocate(p) correctly.
    SlabPool(std::size_t slot_size, SlabRegistry& registry);
    ~SlabPool();

    SlabPool(const SlabPool&) = delete;
    SlabPool& operator=(const SlabPool&) = delete;

    void* allocate();
    void deallocate(void* p);

    // Batch variants used by ThreadCache to amortize lock acquisition.
    // allocate_batch pops up to `n` slots under a single lock, threads them
    // via their embedded next-pointer field, and returns the list head (or
    // nullptr if OOM even after grow()). *out_count is set to the actual
    // number obtained.
    void* allocate_batch(std::uint32_t n, std::uint32_t* out_count);

    // deallocate_batch pushes all `count` slots in `list_head` (linked via
    // embedded next-pointer field) back to their owning slabs under one lock.
    void deallocate_batch(void* list_head, std::uint32_t count);

    std::size_t slot_size() const { return slot_size_; }

    // Returns the number of slabs currently mapped by this pool. Test helper.
    std::size_t mapped_slab_count() const;

    static SlabHeader* header_for(void* p) {
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        return reinterpret_cast<SlabHeader*>(addr & ~(kSlabSize - 1));
    }

private:
    // Maps a new slab and adds it to the pool. Returns false if the mmap
    // failed (out of memory).
    bool grow();

    mutable std::mutex mutex_;
    SlabRegistry& registry_;
    SlabHeader* partial_ = nullptr;  // slabs with at least one free slot
    SlabHeader* all_ = nullptr;      // every slab ever mapped, for teardown
    std::size_t slot_size_;
    std::size_t slots_per_slab_;
    std::size_t first_slot_offset_;
};

}  // namespace memalloc
