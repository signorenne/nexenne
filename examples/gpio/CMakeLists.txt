# One executable per example, named nexenne_example_gpio_<name>. Each is a
# self-contained program demonstrating part of the nexenne::gpio module.
set(_gpio_examples
    quickstart
    discovery
    blink
    edges
    reactor
    watch
)

foreach(_example IN LISTS _gpio_examples)
    add_executable(nexenne_example_gpio_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_gpio_${_example}
        PRIVATE nexenne::gpio nexenne::warnings)
endforeach()
