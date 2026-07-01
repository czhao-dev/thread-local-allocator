#include "memalloc/slab_pool.h"

#include "mmap_utils.h"
#include "slab_registry.h"

#include "memalloc/common.h"

namespace memalloc {

SlabPool::SlabPool(std::size_t slot_size, SlabRegistry& registry)
    : registry_(registry), slot_size_(slot_size) {
    first_slot_offset_ = align_up(sizeof(SlabHeader), slot_size_);
    slots_per_slab_ = (kSlabSize - first_slot_offset_) / slot_size_;
}

SlabPool::~SlabPool() {
    for (SlabHeader* slab = all_; slab != nullptr;) {
        SlabHeader* next = slab->next_all;
        munmap_region(slab, kSlabSize);
        slab = next;
    }
}

bool SlabPool::grow() {
    void* mem = mmap_aligned(kSlabSize, kSlabSize);
    if (!mem) return false;

    auto* slab = static_cast<SlabHeader*>(mem);

    slab->slot_size = static_cast<std::uint32_t>(slot_size_);
    slab->free_count = static_cast<std::uint32_t>(slots_per_slab_);

    // Build the embedded free list, threading a `void*` "next" pointer
    // through each unused slot.
    auto base = reinterpret_cast<std::uintptr_t>(mem);
    for (std::size_t i = 0; i < slots_per_slab_; ++i) {
        void* slot = reinterpret_cast<void*>(base + first_slot_offset_ + i * slot_size_);
        void* next = (i + 1 < slots_per_slab_)
                          ? reinterpret_cast<void*>(base + first_slot_offset_ + (i + 1) * slot_size_)
                          : nullptr;
        *reinterpret_cast<void**>(slot) = next;
    }
    slab->free_list = reinterpret_cast<void*>(base + first_slot_offset_);

    slab->next_partial = partial_;
    partial_ = slab;
    slab->next_all = all_;
    all_ = slab;

    registry_.insert(base);
    return true;
}

void* SlabPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!partial_ && !grow()) return nullptr;

    SlabHeader* slab = partial_;
    void* p = slab->free_list;
    slab->free_list = *static_cast<void**>(p);
    --slab->free_count;

    if (slab->free_count == 0) {
        partial_ = slab->next_partial;
        slab->next_partial = nullptr;
    }
    return p;
}

void SlabPool::deallocate(void* p) {
    std::lock_guard<std::mutex> lock(mutex_);
    SlabHeader* slab = header_for(p);

    bool was_full = (slab->free_count == 0);
    *static_cast<void**>(p) = slab->free_list;
    slab->free_list = p;
    ++slab->free_count;

    if (was_full) {
        slab->next_partial = partial_;
        partial_ = slab;
    }
}

void* SlabPool::allocate_batch(std::uint32_t n, std::uint32_t* out_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    void*         list_head = nullptr;
    std::uint32_t count     = 0;
    while (count < n) {
        if (!partial_ && !grow()) break;
        SlabHeader* slab = partial_;
        void* p          = slab->free_list;
        slab->free_list  = *static_cast<void**>(p);
        --slab->free_count;
        if (slab->free_count == 0) {
            partial_           = slab->next_partial;
            slab->next_partial = nullptr;
        }
        *static_cast<void**>(p) = list_head;
        list_head = p;
        ++count;
    }
    *out_count = count;
    return list_head;
}

void SlabPool::deallocate_batch(void* list_head, std::uint32_t count) {
    if (!list_head) return;
    (void)count;
    std::lock_guard<std::mutex> lock(mutex_);
    for (void* p = list_head; p;) {
        void*       next = *static_cast<void**>(p);
        SlabHeader* slab = header_for(p);
        bool was_full    = (slab->free_count == 0);
        *static_cast<void**>(p) = slab->free_list;
        slab->free_list = p;
        ++slab->free_count;
        if (was_full) {
            slab->next_partial = partial_;
            partial_           = slab;
        }
        p = next;
    }
}

std::size_t SlabPool::mapped_slab_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (SlabHeader* s = all_; s; s = s->next_all) ++n;
    return n;
}

}  // namespace memalloc
