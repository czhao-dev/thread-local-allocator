#include "slab_registry.h"

#include "mmap_utils.h"

namespace memalloc {

namespace {

constexpr std::size_t kInitialCapacity = 1024;  // power of two

std::size_t slot_for(std::uintptr_t addr, std::size_t capacity) {
    // Slab bases are aligned to a large power of two, so shift away the
    // common zero bits before applying a multiplicative hash.
    std::uint64_t h = (addr >> 16) * 0x9E3779B97F4A7C15ull;
    return static_cast<std::size_t>(h) & (capacity - 1);
}

}  // namespace

SlabRegistry::~SlabRegistry() {
    if (table_) munmap_region(table_, capacity_ * sizeof(std::uintptr_t));
}

void SlabRegistry::grow() {
    std::size_t new_capacity = capacity_ == 0 ? kInitialCapacity : capacity_ * 2;
    auto* new_table = static_cast<std::uintptr_t*>(
        mmap_region(new_capacity * sizeof(std::uintptr_t)));

    if (table_) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            std::uintptr_t addr = table_[i];
            if (addr == 0) continue;
            std::size_t slot = slot_for(addr, new_capacity);
            while (new_table[slot] != 0) slot = (slot + 1) & (new_capacity - 1);
            new_table[slot] = addr;
        }
        munmap_region(table_, capacity_ * sizeof(std::uintptr_t));
    }

    table_ = new_table;
    capacity_ = new_capacity;
}

void SlabRegistry::insert(std::uintptr_t slab_base) {
    std::unique_lock lock(mutex_);
    if (capacity_ == 0 || (count_ + 1) * 2 > capacity_) grow();

    std::size_t slot = slot_for(slab_base, capacity_);
    while (table_[slot] != 0) {
        if (table_[slot] == slab_base) return;  // already registered
        slot = (slot + 1) & (capacity_ - 1);
    }
    table_[slot] = slab_base;
    ++count_;
}

bool SlabRegistry::contains(std::uintptr_t slab_base) const {
    std::shared_lock lock(mutex_);
    if (capacity_ == 0) return false;

    std::size_t slot = slot_for(slab_base, capacity_);
    while (table_[slot] != 0) {
        if (table_[slot] == slab_base) return true;
        slot = (slot + 1) & (capacity_ - 1);
    }
    return false;
}

}  // namespace memalloc
