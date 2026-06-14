#pragma once

#include <cstddef>

namespace memalloc {

// Maps an anonymous, zero-filled region of `size` bytes (rounded up to a
// page boundary by the kernel). Returns nullptr on failure.
void* mmap_region(std::size_t size);

// Maps an anonymous region of `size` bytes whose base address is aligned to
// `alignment` (which must be a power of two and a multiple of the page
// size). Returns nullptr on failure. The returned region can later be
// released with `munmap(ptr, size)` directly -- the kernel allows partial
// unmaps of the over-allocation that this function trims away.
void* mmap_aligned(std::size_t size, std::size_t alignment);

// Unmaps a region previously returned by mmap_region/mmap_aligned.
void munmap_region(void* ptr, std::size_t size);

}  // namespace memalloc
