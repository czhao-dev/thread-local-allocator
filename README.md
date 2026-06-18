# memalloc — Custom Memory Allocator in C++

[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

A from-scratch memory allocator implementing two complementary strategies: a
**slab allocator** for fixed-size objects and a **boundary-tag free list** with
immediate coalescing for variable-size allocations. Benchmarked against the
system allocator across workloads with varying allocation size distributions and
concurrency levels.

---

## Overview

Memory allocation is one of the most performance-critical components of a
systems runtime. Poor allocator design causes fragmentation that wastes memory,
cache-inefficient layout that stalls the CPU, and lock contention that throttles
throughput in multithreaded programs. Production allocators like jemalloc,
tcmalloc, and mimalloc each make distinct tradeoffs to address these pressures.

`memalloc` builds the foundational techniques of allocator design from first
principles:

- **Slab allocator** — eliminates internal fragmentation for fixed-size objects
  by carving a single large allocation into uniform slots, amortizing metadata
  overhead and preserving object alignment across free/alloc cycles
- **Free list with boundary tags** — enables O(1) coalescing of adjacent free
  blocks for variable-size allocations, preventing long-term heap fragmentation
- **Per-size-class locking** — allows concurrent allocations of different sizes
  to proceed without contention, scaling better than a single global lock

The result is a drop-in replacement for `malloc`/`free` (via `LD_PRELOAD` on
Linux or `DYLD_INSERT_LIBRARIES` on macOS), benchmarked against the system
allocator on workloads with stable, repeated allocation of same-size objects
— a pattern common in compilers, game engines, and other latency-sensitive
native applications.

---

## Architecture

```
                                allocate(size)
                                       │
                          ┌────────────────────────┐
                          │    Allocator Facade    │
                          └────────────────────────┘
                                       │
             size ≤ 512B                            size > 512B
                   ┌───────────────────┴─────────────────┐
                   ▼                                     ▼
       ┌──────────────────────┐            ┌──────────────────────────┐
       │      Slab Pools      │            │        Free List         │
       │       8..512B        │            │     boundary tags +      │
       │     fixed slots      │            │        coalescing        │
       └──────────────────────┘            └──────────────────────────┘
          mmap-backed slabs                     mmap-backed arenas
```

The facade dispatches on request size. Small, fixed-size requests go to the
slab pool for that size class. Larger requests go to the boundary-tag free list.
Both strategies call `mmap(2)` directly for backing memory, bypassing the system
`brk`/`sbrk` interface for cleaner address space management.

---

## Slab Allocator Design

### Motivation

Consider a system that repeatedly allocates and frees 64-byte objects. With a
naive free list, each allocation carries a header (typically 8–16 bytes),
inflating the effective object size and introducing internal fragmentation.
Metadata accumulates throughout the heap, and every allocation requires a search
of the free list.

A slab allocator solves this by pre-partitioning a large contiguous region (the
*slab*) into same-size slots, with metadata stored once per slab rather than
per object:

```
Slab (mmap'd region, e.g., 4096 bytes for 64-byte slots):

┌──────────────┬───────────────────────────────────────────┐
│  Slab Header │  slot │  slot │  slot │  slot │  ...      │
│  (freelist,  │  [0]  │  [1]  │  [2]  │  [3]  │           │
│   count,     │       │       │       │       │           │
│   next slab) │       │       │       │       │           │
└──────────────┴───────────────────────────────────────────┘
 ▲ one kmalloc/mmap        ▲ 62 usable slots, zero per-slot overhead
```

### Allocation and Deallocation

The slab maintains an embedded free list through the slots themselves: each
free slot stores a pointer to the next free slot. Allocation is a pointer
pop — O(1), branchless, cache-local. Deallocation is a pointer push — equally
O(1).

```cpp
void* SlabPool::allocate() {
    if (!free_slot_) grow();          // current slab exhausted — map new one
    void* p = free_slot_;
    free_slot_ = *static_cast<void**>(free_slot_);  // pop from free list
    return p;
}

void SlabPool::deallocate(void* p) {
    *static_cast<void**>(p) = free_slot_;  // push onto free list
    free_slot_ = p;
}
```

### Cache Alignment

Each slab is `mmap`'d at a page boundary. Slot addresses within the slab are
naturally aligned to the slot size (which is always a power of two), satisfying
the alignment requirements of any type that fits in that size class. This also
enables a fast "which slab owns this pointer" lookup: mask off the low
`log2(slab_size)` bits.

---

## Variable-Size Allocator: Boundary Tags and Coalescing

### The Fragmentation Problem

A simple free list without coalescing suffers from *external fragmentation*:
even when total free memory is sufficient for a request, no single contiguous
block may be large enough. A 100-byte request fails if free memory is split into
twenty 10-byte fragments.

Coalescing — merging adjacent free blocks into one larger block — is the
solution. Efficient coalescing requires O(1) access to a block's neighbors.

### Boundary Tags (Knuth, 1973)

Each block carries a **header** (size + status) at the start and a **footer**
(same size + status) at the end:

```
┌────────────────────────────────────────────┐
│ Header: [size | FREE/ALLOC]  (8 bytes)     │
├────────────────────────────────────────────┤
│                                            │
│  Payload (user-visible memory)             │
│                                            │
├────────────────────────────────────────────┤
│ Footer: [size | FREE/ALLOC]  (8 bytes)     │
└────────────────────────────────────────────┘
```

On `free(p)`:
1. Mark the block free
2. Check the **preceding** block's footer — if free, coalesce backward
3. Check the **following** block's header — if free, coalesce forward
4. Insert the merged block into the free list

All three coalesce cases execute in O(1) time — no traversal required.

### Free List Search Strategy

Free blocks are maintained in an explicit doubly-linked free list. `allocate(n)`
uses **first-fit** search: the first block with `size >= n` is selected and
split if significantly larger than requested. First-fit is O(n) in the number of
free blocks but achieves good utilization in practice and is simple to reason
about correctly.

**Best-fit** (search entire list for the closest match) reduces fragmentation
further but increases allocation time. **Segregated free lists** (one list per
size range) recover O(1) amortized allocation at the cost of implementation
complexity — this is the approach taken by jemalloc.

---

## Thread Safety Model

Each slab pool (one per size class) carries its own `std::mutex`. Concurrent
allocations of different size classes proceed without contention:

```cpp
void* SlabPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    // ... O(1) slot pop
}
```

The free list allocator uses a single mutex over the entire heap. This is
intentionally simple and correctness-first — see *Future Extensions* for the
path toward per-thread caches.

The design mirrors the tradeoff in production allocators: tcmalloc uses
per-thread caches to eliminate lock acquisition on the common path, at the cost
of higher complexity and increased per-thread memory footprint.

---

## Testing & Verification

The test suite ([`tests/`](tests/)) exercises the allocator from the outside —
through the same `allocate`/`deallocate`/`reallocate` facade a real program
would use — rather than poking at internal state, so passing tests mean the
public contract actually holds.

| Test | What it verifies |
|------|-------------------|
| `test_alignment` | Every allocation, across all 7 slab size classes and the free-list path, is aligned to its size class (16 bytes for free-list allocations), and `usable_size()` never reports less than what was requested. |
| `test_values` | Allocated memory holds exactly the bytes written to it, for both slab and free-list size classes, and `reallocate()` preserves existing contents across a grow. |
| `test_coalesce` | Freeing adjacent blocks produces correctly merged block sizes via immediate boundary-tag coalescing — including the three-way backward-and-forward merge case — and a subsequent allocation can reuse the fully coalesced region without growing the heap. |
| `test_double_free` | Freeing the same free-list pointer twice is detected and the process aborts (`SIGABRT`) rather than silently corrupting the heap. Run in a forked child so the crash doesn't take down the test runner. |
| `test_concurrent` | 8 threads × 20,000 allocate/free operations on random sizes (1–4096 bytes) spanning both the slab pools and the free list, writing and verifying a per-allocation byte pattern to catch any corruption from races. |

```
$ ctest --test-dir build --output-on-failure
    Start 1: test_alignment
1/5 Test #1: test_alignment ...................   Passed    0.01 sec
    Start 2: test_values
2/5 Test #2: test_values ......................   Passed    0.00 sec
    Start 3: test_coalesce
3/5 Test #3: test_coalesce ....................   Passed    0.00 sec
    Start 4: test_concurrent
4/5 Test #4: test_concurrent ..................   Passed    0.12 sec
    Start 5: test_double_free
5/5 Test #5: test_double_free .................   Passed    0.02 sec

100% tests passed, 0 tests failed out of 5
```

All five also pass cleanly rebuilt with `-DMEMALLOC_ENABLE_ASAN=ON`
(AddressSanitizer + UndefinedBehaviorSanitizer), including the 160,000-operation
concurrent stress test — no use-after-free, heap-buffer-overflow, data race, or
undefined-behavior reports.

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMEMALLOC_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

This sanitizer build earned its keep during development: it surfaced a real
bug where `reallocate()`'s shrink/grow paths reused the same block-splitting
routine as `allocate()`, but without re-checking whether the leftover
remainder was now adjacent to an already-free block. The result was a quiet
violation of the free list's "no two adjacent free blocks" invariant — not
memory corruption, but unnecessary fragmentation and avoidable arena growth
under repeated realloc cycles. A targeted stress test reproduced it directly
(`free_block_count()` went from 2 to 3 across a single shrinking `reallocate`
where it should have stayed at 2), and the fix — coalescing the remainder
forward in `split()` before inserting it into the free list — brought it back
to 2.

---

## Benchmarks

Measured on Apple Silicon (ARM64) macOS, Apple Clang 21, `-O2`
(`CMAKE_BUILD_TYPE=Release`). Results compare `memalloc` (loaded via
`DYLD_INSERT_LIBRARIES` on macOS, or `LD_PRELOAD` on Linux) against the
platform's system allocator. Reproduce with `benchmarks/run_all.sh`; numbers
will vary by platform and allocator implementation.

### Workload 1 — Fixed-size churn (slab advantage)

Repeatedly allocate and free 64-byte objects from 8 concurrent threads.

| Allocator | Throughput (Mops/sec) | Peak RSS |
|-----------|----------------------|----------|
| memalloc  | 245.9                | 1.7 MB   |
| system    | 346.0                | 1.6 MB   |

The slab pool gives `memalloc` O(1), per-size-class allocation with no
boundary-tag overhead, but on this platform the system allocator's own
small-object zones use a comparable slab-like scheme that is hand-tuned in
assembly, so it edges out `memalloc` on this micro-benchmark. The
per-`free()` slab-registry lookup (see "Architecture") is the main remaining
overhead relative to that baseline.

### Workload 2 — Mixed-size allocation (general case)

Allocate objects with sizes drawn from a log-uniform distribution [8, 4096]
bytes, hold a random subset live, free the rest. Repeat for 10M operations.

| Allocator | Throughput (Mops/sec) | Fragmentation |
|-----------|----------------------|----------------|
| memalloc  | 30.7                 | 87.7%          |
| system    | 27.9                 | 87.6%          |

`memalloc` is slightly faster here. Both allocators report similar
fragmentation; this metric is `1 - live_bytes/peak_RSS`, which is dominated
by the few-MB process baseline RSS relative to the ~0.5 MB of objects live
at any one time, so it mainly reflects fixed overhead rather than heap
layout quality at this scale.

### Workload 3 — Single-threaded latency

Measure p50 / p99 / p999 allocation latency for 64-byte objects.

| Allocator | p50 (ns) | p99 (ns) | p999 (ns) |
|-----------|----------|----------|-----------|
| memalloc  | 0        | 42       | 834       |
| system    | 0        | 42       | 875       |

Both allocators resolve the common case in well under the timer's
resolution; tail latencies are comparable.

---

## Design Decisions and Tradeoffs

**Why mmap instead of sbrk for backing memory**

`sbrk` moves the program break to extend the heap as a single contiguous
region. `mmap` allocates independent regions at arbitrary addresses. For an
allocator that manages its own layout, `mmap` is preferable: returned memory
can be given back to the OS with `munmap` (shrinking the process footprint),
whereas `sbrk`-extended memory can only be returned if it is at the end of the
heap. `mmap` also avoids the single-threaded contention on the program break.

**Slab threshold of 512 bytes**

The crossover point between slab and free-list allocation is empirically chosen.
Below 512 bytes, the per-object overhead of boundary tags (16 bytes) is a
significant fraction of the allocation, and repeated same-size allocation
patterns dominate. Above 512 bytes, the diversity of sizes makes size-class
pre-partitioning wasteful, and the free list's flexibility is more valuable.
This threshold matches the design of jemalloc (small/large boundary at 512B or
4KB depending on configuration).

**Immediate vs deferred coalescing**

`memalloc` coalesces immediately on every `free()`. Deferred coalescing (batching
frees and coalescing periodically) can improve throughput by amortizing the cost
across many frees, but complicates the invariants: a free block's neighbors may
not be coalesced yet, requiring additional bookkeeping. Immediate coalescing is
simpler to verify correct and produces lower fragmentation at the cost of
slightly higher per-free overhead.

---

## Building and Running

### Requirements

- C++17 compiler (GCC 9+, Clang 10+, Apple Clang)
- Linux or macOS (uses `mmap`)
- `cmake` 3.16+

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run benchmarks

```bash
./build/bin/fixed_size_bench
./build/bin/mixed_size_bench
./build/bin/latency_bench

# or compare against the system allocator directly:
./benchmarks/run_all.sh
```

### Use as a drop-in replacement

```bash
# Linux
LD_PRELOAD=./build/lib/libmemalloc.so ./your_program

# macOS
DYLD_INSERT_LIBRARIES=./build/lib/libmemalloc.dylib DYLD_FORCE_FLAT_NAMESPACE=1 ./your_program
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

See *Testing & Verification* above for what each test covers and how to run
the AddressSanitizer/UndefinedBehaviorSanitizer build.

---

## Future Extensions

- **Per-thread caches** — eliminate lock acquisition on the common path by
  maintaining a small per-thread free list, flushing to the global pool when
  full (the tcmalloc design)
- **Segregated free lists for variable-size** — maintain one free list per
  power-of-two size range to recover O(1) amortized search (the jemalloc design)
- **`madvise(MADV_FREE)`** — hint to the kernel that empty slab pages can be
  reclaimed, reducing RSS under memory pressure without `munmap` overhead
- **Valgrind / ASan client requests** — annotate allocations so memory
  debugging tools report errors correctly when `LD_PRELOAD`'d

---

## References

- Knuth, D.E. *The Art of Computer Programming, Vol. 1* — boundary tag
  coalescing algorithm (§2.5)
- Bonwick, J. *The Slab Allocator: An Object-Caching Kernel Memory Allocator*
  (USENIX 1994) — original slab allocator paper
- Berger, E. et al. *Hoard: A Scalable Memory Allocator for Multithreaded Applications*
  (ASPLOS 2000)
- jemalloc design documentation — https://jemalloc.net/jemalloc.3.html
- *CS:APP, 3rd Edition*, Bryant & O'Hallaron — Chapter 9 (dynamic memory
  allocation), the clearest textbook treatment of boundary tags

---

## License

[MIT](LICENSE)
