# One executable per example, named nexenne_example_container_<name>. Each is a
# self-contained program demonstrating a single nexenne::container class.
set(_container_examples
    error
)

foreach(_example IN LISTS _container_examples)
    add_executable(nexenne_example_container_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_container_${_example}
        PRIVATE nexenne::container nexenne::warnings)
endforeach()
