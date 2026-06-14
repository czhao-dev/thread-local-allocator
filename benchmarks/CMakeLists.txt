foreach(bench_name IN ITEMS
    fixed_size_bench
    mixed_size_bench
    latency_bench
)
    add_executable(${bench_name} ${bench_name}.cpp)
    target_link_libraries(${bench_name} PRIVATE Threads::Threads)
endforeach()
