# One executable per example, named nexenne_example_logging_<name>.
# levels and embedded use only the header-only stream_logger path, so they are
# exactly the examples a freestanding configuration can still build.
set(_logging_examples
    levels
    embedded
)
if(NEXENNE_LOGGING_HOST_SINKS)
    list(APPEND _logging_examples
        basic
        showcase
        async_sync
        sinks
    )
endif()

foreach(_example IN LISTS _logging_examples)
    add_executable(nexenne_example_logging_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_logging_${_example}
        PRIVATE nexenne::logging nexenne::warnings)
endforeach()
