cmake_minimum_required(VERSION 3.16)
project(memalloc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/mmap_utils.cpp
    src/slab_registry.cpp
    src/slab_pool.cpp
    src/thread_cache.cpp
    src/free_list.cpp
    src/allocator.cpp
)
target_include_directories(memalloc_core PUBLIC include)
set_target_properties(memalloc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(memalloc_core PUBLIC Threads::Threads)

# Drop-in malloc/free/calloc/realloc replacement for LD_PRELOAD / DYLD_INSERT_LIBRARIES.
add_library(memalloc SHARED src/malloc_shim.cpp)
target_link_libraries(memalloc PRIVATE memalloc_core)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
