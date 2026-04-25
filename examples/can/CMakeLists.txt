# One executable per example, named nexenne_example_can_<name>. Each is a
# self-contained program demonstrating part of the nexenne::can module.
set(_can_examples
    database
    fields
    filters
    frame
    loopback
    signal
)

foreach(_example IN LISTS _can_examples)
    add_executable(nexenne_example_can_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_can_${_example}
        PRIVATE nexenne::can nexenne::warnings)
endforeach()
