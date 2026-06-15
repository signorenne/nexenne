# One executable per example, named nexenne_example_filter_<name>. Each is a
# self-contained program demonstrating one family of nexenne::filter filters.
set(_filter_examples
    smoothers
    robust
    shaping
    guards
)

foreach(_example IN LISTS _filter_examples)
    add_executable(nexenne_example_filter_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_filter_${_example}
        PRIVATE nexenne::filter nexenne::warnings)
endforeach()
