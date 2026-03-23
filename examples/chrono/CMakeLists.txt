# One executable per example, named nexenne_example_chrono_<name>. Each is a
# self-contained program demonstrating a single nexenne::chrono facility.
set(_chrono_examples
    duration_format
    stopwatch
    countdown
    scope_timer
    rate_limiter
    frame_timer
    showcase
    profiler
    static_stopwatch
    interval
    alarm
    deadline
    tick_clock
)

foreach(_example IN LISTS _chrono_examples)
    add_executable(nexenne_example_chrono_${_example} ${_example}.cpp)
    target_link_libraries(nexenne_example_chrono_${_example}
        PRIVATE nexenne::chrono nexenne::warnings)
endforeach()
