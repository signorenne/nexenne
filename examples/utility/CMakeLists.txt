# One executable per example, named nexenne_example_utility_<name>. Each is a
# self-contained program demonstrating a single nexenne::utility class.
find_package(Threads REQUIRED)  # lazy uses std::call_once

set(_utility_examples
    align
    bit
    defer
    enum_to_string
    expected_utils
    flags
    function_ref
    hash
    in_place_function
    lazy
    meta
    narrow_cast
    non_null
    overloaded
    scope_guard
    source_location_format
    static_string
    strong_typedef
    type_list
    unique_resource
)

foreach(_example IN LISTS _utility_examples)
    add_executable(nexenne_example_utility_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_utility_${_example}
        PRIVATE nexenne::utility nexenne::warnings Threads::Threads)
endforeach()
