#pragma once

#include <cstddef>

namespace memalloc {

// Allocates at least `size` bytes, 16-byte aligned. Requests of
// kSlabThreshold bytes or fewer are served by a slab pool; larger requests
// go to the boundary-tag free list. Returns nullptr on failure.
void* allocate(std::size_t size);

// Frees a pointer previously returned by allocate/reallocate. `p == nullptr`
// is a no-op. Aborts on double-free of a free-list allocation.
void deallocate(void* p) noexcept;

// Resizes an allocation, copying its contents. `p == nullptr` behaves like
// allocate(new_size); `new_size == 0` behaves like deallocate(p) and returns
// nullptr.
void* reallocate(void* p, std::size_t new_size);

// Returns the usable size of an allocation, which may be larger than the
// size originally requested.
std::size_t usable_size(void* p) noexcept;

}  // namespace memalloc
