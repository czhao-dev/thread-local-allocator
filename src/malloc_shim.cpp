// Drop-in malloc/free/calloc/realloc replacement, intended to be loaded via
// LD_PRELOAD (Linux) or DYLD_INSERT_LIBRARIES (macOS). See README "Use as a
// drop-in replacement".

#include <cstddef>
#include <cstring>

#include "memalloc/allocator.h"

extern "C" {

__attribute__((visibility("default"))) void* malloc(std::size_t size) {
    return memalloc::allocate(size == 0 ? 1 : size);
}

__attribute__((visibility("default"))) void free(void* ptr) { memalloc::deallocate(ptr); }

__attribute__((visibility("default"))) void* calloc(std::size_t count, std::size_t size) {
    std::size_t total;
    if (__builtin_mul_overflow(count, size, &total)) return nullptr;
    void* p = memalloc::allocate(total == 0 ? 1 : total);
    if (p) std::memset(p, 0, total);
    return p;
}

__attribute__((visibility("default"))) void* realloc(void* ptr, std::size_t size) {
    if (!ptr) return memalloc::allocate(size == 0 ? 1 : size);
    if (size == 0) {
        memalloc::deallocate(ptr);
        return nullptr;
    }
    return memalloc::reallocate(ptr, size);
}

__attribute__((visibility("default"))) std::size_t malloc_usable_size(void* ptr) noexcept {
    return memalloc::usable_size(ptr);
}

}  // extern "C"
