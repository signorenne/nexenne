# One executable per example, named nexenne_example_container_<name>. Each is a
# self-contained program demonstrating a single nexenne::container class.
set(_container_examples
    bag
    bitset_dynamic
    deque
    error
    linear_arena
    ring_buffer
    scratch_pad
    small_vector
    stable_vector
    static_vector
)

foreach(_example IN LISTS _container_examples)
    add_executable(nexenne_example_container_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_container_${_example}
        PRIVATE nexenne::container nexenne::warnings)
endforeach()
