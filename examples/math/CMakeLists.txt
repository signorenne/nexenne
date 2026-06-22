# One executable per example, named nexenne_example_math_<name>.
set(_math_examples
    constants
    scalar
    power
    angle
    trigonometry
    fixed
    vectors
    matrices
    rotations
    transforms
    curves
    sampling
    debug
    showcase
)

foreach(_example IN LISTS _math_examples)
    add_executable(nexenne_example_math_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_math_${_example}
        PRIVATE nexenne::math nexenne::warnings)
endforeach()
