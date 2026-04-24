# One executable per example, named nexenne_example_geometry_<name>.
set(_geometry_examples
    showcase
)

foreach(_example IN LISTS _geometry_examples)
    add_executable(nexenne_example_geometry_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_geometry_${_example}
        PRIVATE nexenne::geometry nexenne::warnings)
endforeach()
