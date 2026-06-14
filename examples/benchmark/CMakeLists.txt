# One executable per example, named nexenne_example_benchmark_<name>.
set(_benchmark_examples
    compare_sorts
)

foreach(_example IN LISTS _benchmark_examples)
    add_executable(nexenne_example_benchmark_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_benchmark_${_example}
        PRIVATE nexenne::benchmark nexenne::warnings)
endforeach()
