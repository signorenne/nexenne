# One executable per example, named nexenne_example_serialization_<name>.
set(_serialization_examples
    round_trip
)

foreach(_example IN LISTS _serialization_examples)
    add_executable(nexenne_example_serialization_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_serialization_${_example}
        PRIVATE nexenne::serialization nexenne::warnings)
endforeach()
