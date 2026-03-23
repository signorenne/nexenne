# One executable per example, named nexenne_example_logging_<name>.
set(_logging_examples
    levels
    basic
    showcase
    embedded
    async_sync
    sinks
)

foreach(_example IN LISTS _logging_examples)
    add_executable(nexenne_example_logging_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_logging_${_example}
        PRIVATE nexenne::logging nexenne::warnings)
endforeach()
