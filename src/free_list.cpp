#include "memalloc/free_list.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "memalloc/common.h"
#include "mmap_utils.h"

namespace memalloc {

namespace {

constexpr std::size_t kAllocBit = 0x1;

std::size_t page_size() {
    static const std::size_t sz = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    return sz;
}

}  // namespace

// --- block layout helpers ---------------------------------------------

FreeListAllocator::Header* FreeListAllocator::header_of(void* payload) {
    return reinterpret_cast<Header*>(reinterpret_cast<char*>(payload) - kHeaderSize);
}

void* FreeListAllocator::payload_of(Header* h) {
    return reinterpret_cast<char*>(h) + kHeaderSize;
}

std::size_t FreeListAllocator::size_of(Header* h) {
    return h->size_and_flags & ~kAllocBit;
}

bool FreeListAllocator::is_free(Header* h) {
    return (h->size_and_flags & kAllocBit) == 0;
}

FreeListAllocator::Header* FreeListAllocator::footer_of(Header* h) {
    return reinterpret_cast<Header*>(reinterpret_cast<char*>(h) + size_of(h) - kHeaderSize);
}

void FreeListAllocator::set_tags(Header* h, std::size_t size, bool free) {
    std::size_t flags = size | (free ? 0 : kAllocBit);
    h->size_and_flags = flags;
    reinterpret_cast<Header*>(reinterpret_cast<char*>(h) + size - kHeaderSize)->size_and_flags = flags;
}

FreeListAllocator::Header* FreeListAllocator::next_block(Header* h) {
    return reinterpret_cast<Header*>(reinterpret_cast<char*>(h) + size_of(h));
}

FreeListAllocator::Header* FreeListAllocator::prev_block(Header* h) {
    Header* prev_footer = reinterpret_cast<Header*>(reinterpret_cast<char*>(h) - kHeaderSize);
    std::size_t prev_size = size_of(prev_footer);
    return reinterpret_cast<Header*>(reinterpret_cast<char*>(h) - prev_size);
}

// --- free list management -----------------------------------------------

void FreeListAllocator::insert_free(Header* h) {
    auto* node = reinterpret_cast<FreeNode*>(payload_of(h));
    node->prev = nullptr;
    node->next = free_list_;
    if (free_list_) reinterpret_cast<FreeNode*>(payload_of(free_list_))->prev = h;
    free_list_ = h;
}

void FreeListAllocator::remove_free(Header* h) {
    auto* node = reinterpret_cast<FreeNode*>(payload_of(h));
    if (node->prev) {
        reinterpret_cast<FreeNode*>(payload_of(node->prev))->next = node->next;
    } else {
        free_list_ = node->next;
    }
    if (node->next) {
        reinterpret_cast<FreeNode*>(payload_of(node->next))->prev = node->prev;
    }
}

FreeListAllocator::Header* FreeListAllocator::find_fit(std::size_t total_size) {
    for (Header* h = free_list_; h != nullptr;
         h = reinterpret_cast<FreeNode*>(payload_of(h))->next) {
        if (size_of(h) >= total_size) return h;
    }
    return nullptr;
}

FreeListAllocator::Header* FreeListAllocator::split(Header* h, std::size_t alloc_total_size) {
    std::size_t old_size = size_of(h);
    if (old_size - alloc_total_size >= kMinBlockSize) {
        set_tags(h, alloc_total_size, /*free=*/false);
        Header* rem = next_block(h);
        std::size_t rem_size = old_size - alloc_total_size;
        set_tags(rem, rem_size, /*free=*/true);

        // `rem` may already be adjacent to a free block (split() is also
        // called from reallocate(), where h's neighbors were never
        // re-examined) -- coalesce forward to preserve the "no two
        // adjacent free blocks" invariant.
        Header* next = next_block(rem);
        if (is_free(next)) {
            remove_free(next);
            rem_size += size_of(next);
            set_tags(rem, rem_size, /*free=*/true);
        }
        insert_free(rem);
    } else {
        set_tags(h, old_size, /*free=*/false);
    }
    return h;
}

FreeListAllocator::Header* FreeListAllocator::coalesce(Header* h) {
    Header* prev = prev_block(h);
    Header* next = next_block(h);
    std::size_t total = size_of(h);
    Header* result = h;

    if (is_free(prev)) {
        remove_free(prev);
        total += size_of(prev);
        result = prev;
    }
    if (is_free(next)) {
        remove_free(next);
        total += size_of(next);
    }
    set_tags(result, total, /*free=*/true);
    return result;
}

// --- arena management -----------------------------------------------------

FreeListAllocator::Header* FreeListAllocator::extend(std::size_t min_total_size) {
    std::size_t needed = min_total_size + kArenaPrefix + kArenaSuffix;
    std::size_t arena_size = kArenaSize;
    if (arena_size < needed) arena_size = align_up(needed, page_size());

    void* mem = mmap_region(arena_size);
    if (!mem) return nullptr;

    auto* arena = static_cast<ArenaHeader*>(mem);
    arena->total_size = arena_size;
    arena->next = arenas_;
    arenas_ = arena;

    // Prologue sentinel: a single-word "block" marked allocated so backward
    // coalescing never walks before the arena.
    Header* prologue = reinterpret_cast<Header*>(reinterpret_cast<char*>(mem) + sizeof(ArenaHeader));
    set_tags(prologue, kHeaderSize, /*free=*/false);

    // The entire rest of the arena (minus the epilogue sentinel) starts as
    // one big free block.
    Header* block = next_block(prologue);
    std::size_t block_size = arena_size - kArenaPrefix - kArenaSuffix;
    set_tags(block, block_size, /*free=*/true);

    // Epilogue sentinel: marked allocated so forward coalescing never walks
    // past the arena.
    Header* epilogue = next_block(block);
    set_tags(epilogue, kHeaderSize, /*free=*/false);

    insert_free(block);
    return block;
}

// --- public API ------------------------------------------------------------

FreeListAllocator::~FreeListAllocator() {
    for (ArenaHeader* arena = arenas_; arena != nullptr;) {
        ArenaHeader* next = arena->next;
        munmap_region(arena, arena->total_size);
        arena = next;
    }
}

void* FreeListAllocator::allocate_locked(std::size_t size) {
    std::size_t payload = size < 16 ? 16 : align_up(size, 16);
    std::size_t total = payload + kOverhead;

    Header* h = find_fit(total);
    if (!h) {
        h = extend(total);
        if (!h) return nullptr;
    }
    remove_free(h);
    h = split(h, total);
    return payload_of(h);
}

void* FreeListAllocator::allocate(std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocate_locked(size);
}

void FreeListAllocator::deallocate_locked(void* p) {
    Header* h = header_of(p);
    if (is_free(h)) {
        std::fprintf(stderr, "memalloc: double free or corruption detected at %p\n", p);
        std::abort();
    }
    set_tags(h, size_of(h), /*free=*/true);
    h = coalesce(h);
    insert_free(h);
}

void FreeListAllocator::deallocate(void* p) {
    std::lock_guard<std::mutex> lock(mutex_);
    deallocate_locked(p);
}

void* FreeListAllocator::reallocate(void* p, std::size_t new_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    Header* h = header_of(p);
    std::size_t old_total = size_of(h);
    std::size_t payload = new_size < 16 ? 16 : align_up(new_size, 16);
    std::size_t new_total = payload + kOverhead;

    if (new_total <= old_total) {
        h = split(h, new_total);
        return payload_of(h);
    }

    Header* nxt = next_block(h);
    if (is_free(nxt) && old_total + size_of(nxt) >= new_total) {
        remove_free(nxt);
        set_tags(h, old_total + size_of(nxt), /*free=*/false);
        h = split(h, new_total);
        return payload_of(h);
    }

    void* new_p = allocate_locked(new_size);
    if (!new_p) return nullptr;
    std::size_t old_payload = old_total - kOverhead;
    std::memcpy(new_p, p, old_payload < new_size ? old_payload : new_size);
    deallocate_locked(p);
    return new_p;
}

std::size_t FreeListAllocator::usable_size(void* p) {
    return size_of(header_of(p)) - kOverhead;
}

std::size_t FreeListAllocator::block_total_size(void* p) {
    return size_of(header_of(p));
}

std::size_t FreeListAllocator::free_block_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (Header* h = free_list_; h != nullptr;
         h = reinterpret_cast<FreeNode*>(payload_of(h))->next) {
        ++count;
    }
    return count;
}

}  // namespace memalloc
