cmake_minimum_required(VERSION 3.16)
project(memalloc LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_options(-Wall -Wextra)

option(MEMALLOC_ENABLE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(MEMALLOC_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Core allocator: slab pools + boundary-tag free list + facade. Built as a
# static library so it can be linked directly into tests/benchmarks and into
# the malloc-shim shared library below.
find_package(Threads REQUIRED)

add_library(memalloc_core STATIC
    src/mmap_utils.c
    src/slab_registry.c
    src/slab_pool.c
    src/thread_cache.c
    src/free_list.c
    src/allocator.c
)
target_include_directories(memalloc_core PUBLIC include)
set_target_properties(memalloc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(memalloc_core PUBLIC Threads::Threads)

# Drop-in malloc/free/calloc/realloc replacement for LD_PRELOAD / DYLD_INSERT_LIBRARIES.
add_library(memalloc SHARED src/malloc_shim.c)
target_link_libraries(memalloc PRIVATE memalloc_core)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
