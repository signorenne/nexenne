# One executable per example, named nexenne_example_container_<name>. Each is a
# self-contained program demonstrating a single nexenne::container class.
set(_container_examples
    bag
    bimap
    binary_tree
    bitset_dynamic
    dense_map
    deque
    error
    flat_hash_map
    flat_hash_set
    flat_map
    flat_set
    gap_buffer
    heap
    intrusive_list
    linear_arena
    lru_cache
    object_pool
    ring_buffer
    scratch_pad
    slot_map
    small_vector
    sparse_set
    stable_vector
    static_flat_map
    static_vector
    union_find
)

foreach(_example IN LISTS _container_examples)
    add_executable(nexenne_example_container_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_container_${_example}
        PRIVATE nexenne::container nexenne::warnings)
endforeach()
