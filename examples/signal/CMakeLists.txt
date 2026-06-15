# One executable per example, named nexenne_example_signal_<name>. Each is a
# self-contained program demonstrating a nexenne::signal facility.
set(_signal_examples
    observer
    static_signal
)

foreach(_example IN LISTS _signal_examples)
    add_executable(nexenne_example_signal_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_signal_${_example}
        PRIVATE nexenne::signal nexenne::warnings)
endforeach()
