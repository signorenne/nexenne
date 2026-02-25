# One executable per example, named nexenne_example_algorithm_<name>.
set(_algorithm_examples
    binary_search
    checksum
    encoding
    hashing
    numerical
    sort
    string
)

foreach(_example IN LISTS _algorithm_examples)
    add_executable(nexenne_example_algorithm_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_algorithm_${_example}
        PRIVATE nexenne::algorithm nexenne::warnings)
endforeach()
