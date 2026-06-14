foreach(test_name IN ITEMS
    test_alignment
    test_values
    test_coalesce
    test_concurrent
)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE memalloc_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

add_executable(test_double_free test_double_free.cpp)
target_link_libraries(test_double_free PRIVATE memalloc_core)
add_test(NAME test_double_free COMMAND test_double_free)
