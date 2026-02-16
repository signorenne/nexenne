# One executable per example, named nexenne_example_ecs_<name>.
set(_ecs_examples
    simulation
)

foreach(_example IN LISTS _ecs_examples)
    add_executable(nexenne_example_ecs_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_ecs_${_example}
        PRIVATE nexenne::ecs nexenne::warnings)
endforeach()
