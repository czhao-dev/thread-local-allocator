#pragma once

#include <cstddef>
#include <mutex>

namespace memalloc {

// Variable-size allocator backed by mmap'd arenas, using Knuth's boundary
// tags for O(1) coalescing on free (see README "Variable-Size Allocator").
//
// Block layout (sizes are always multiples of 16):
//
//   [Header: size_t size|flag] [ ... payload ... ] [Footer: size_t size|flag]
//
// Free blocks additionally store {prev, next} pointers (a FreeNode) at the
// start of their payload, threading an explicit doubly-linked free list used
// for first-fit search.
class FreeListAllocator {
public:
    FreeListAllocator() = default;
    ~FreeListAllocator();

    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;

    void* allocate(std::size_t size);
    void deallocate(void* p);
    void* reallocate(void* p, std::size_t new_size);

    // Usable payload size of an allocation (>= the size it was requested with).
    static std::size_t usable_size(void* p);

    // Total block size (header + payload + footer). Test/debug helper.
    static std::size_t block_total_size(void* p);

    // Number of blocks currently on the free list. Test/debug helper.
    std::size_t free_block_count() const;

private:
    struct Header {
        std::size_t size_and_flags;
    };
    struct FreeNode {
        Header* prev;
        Header* next;
    };
    struct ArenaHeader {
        ArenaHeader* next;
        std::size_t total_size;
    };

    static constexpr std::size_t kHeaderSize = sizeof(std::size_t);
    static constexpr std::size_t kOverhead = 2 * kHeaderSize;  // header + footer
    static constexpr std::size_t kMinBlockSize = 32;
    static constexpr std::size_t kArenaSize = 1u << 20;  // 1 MiB
    // ArenaHeader + 1-word prologue "block" that precedes the first real block.
    static constexpr std::size_t kArenaPrefix = sizeof(ArenaHeader) + kHeaderSize;
    // 1-word epilogue "block" that follows the last real block.
    static constexpr std::size_t kArenaSuffix = kHeaderSize;

    static Header* header_of(void* payload);
    static void* payload_of(Header* h);
    static Header* footer_of(Header* h);
    static std::size_t size_of(Header* h);
    static bool is_free(Header* h);
    static void set_tags(Header* h, std::size_t size, bool free);
    static Header* prev_block(Header* h);
    static Header* next_block(Header* h);

    void insert_free(Header* h);
    void remove_free(Header* h);
    Header* find_fit(std::size_t total_size);
    Header* extend(std::size_t min_total_size);
    Header* coalesce(Header* h);
    Header* split(Header* h, std::size_t alloc_total_size);

    void* allocate_locked(std::size_t size);
    void deallocate_locked(void* p);

    mutable std::mutex mutex_;
    Header* free_list_ = nullptr;
    ArenaHeader* arenas_ = nullptr;
};

}  // namespace memalloc
