cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_chrono
        VERSION     0.1.0
        DESCRIPTION "nexenne::chrono - portable clock-templated timing primitives"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(chrono
    HEADERS
        include/nexenne/chrono/alarm.hpp
        include/nexenne/chrono/chrono.hpp
        include/nexenne/chrono/concepts.hpp
        include/nexenne/chrono/conversion.hpp
        include/nexenne/chrono/countdown.hpp
        include/nexenne/chrono/deadline.hpp
        include/nexenne/chrono/duration_parts.hpp
        include/nexenne/chrono/fixed_stopwatch.hpp
        include/nexenne/chrono/frame_timer.hpp
        include/nexenne/chrono/frequency.hpp
        include/nexenne/chrono/interval.hpp
        include/nexenne/chrono/manual_clock.hpp
        include/nexenne/chrono/profiler.hpp
        include/nexenne/chrono/rate_limiter.hpp
        include/nexenne/chrono/scope_timer.hpp
        include/nexenne/chrono/stopwatch.hpp
        include/nexenne/chrono/tick_clock.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
