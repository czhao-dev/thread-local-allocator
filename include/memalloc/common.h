#pragma once

#include <cstddef>
#include <cstdint>

namespace memalloc {

// Requests at or below this size are served by the slab pools; larger
// requests fall through to the boundary-tag free list.
inline constexpr std::size_t kSlabThreshold = 512;

// Size classes served by the slab allocator.
inline constexpr std::size_t kSlabSizeClasses[] = {8, 16, 32, 64, 128, 256, 512};
inline constexpr std::size_t kNumSlabSizeClasses =
    sizeof(kSlabSizeClasses) / sizeof(kSlabSizeClasses[0]);

// Alignment guarantee provided to callers (matches max_align_t on x86-64/ARM64).
inline constexpr std::size_t kDefaultAlign = 16;

inline constexpr std::size_t align_up(std::size_t n, std::size_t align) {
    return (n + align - 1) & ~(align - 1);
}

// Returns the smallest slab size class that can hold `size`, or 0 if `size`
// exceeds the largest size class (and should go to the free list instead).
inline constexpr std::size_t slab_class_for(std::size_t size) {
    for (std::size_t i = 0; i < kNumSlabSizeClasses; ++i) {
        if (size <= kSlabSizeClasses[i]) return kSlabSizeClasses[i];
    }
    return 0;
}

}  // namespace memalloc
