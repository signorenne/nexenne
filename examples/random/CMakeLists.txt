# One executable per example, named nexenne_example_random_<name>.
set(_random_examples
    engines
    dice_and_picks
    distributions
    showcase
)

foreach(_example IN LISTS _random_examples)
    add_executable(nexenne_example_random_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_random_${_example}
        PRIVATE nexenne::random nexenne::warnings)
endforeach()
