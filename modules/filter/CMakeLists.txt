cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_filter
        VERSION     0.1.0
        DESCRIPTION "nexenne::filter - discrete-time digital filters for embedded signal conditioning"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Header-only by default. For a compiled module, add KIND STATIC or SHARED
# and a SOURCES list. Inter-module dependencies go in a module.deps file next
# to this one, not here. External libraries go in LINK_PUBLIC / LINK_PRIVATE.
nexenne_add_module(filter
    HEADERS
        include/nexenne/filter/filter.hpp
        include/nexenne/filter/concepts.hpp
        include/nexenne/filter/adaptive.hpp
        include/nexenne/filter/biquad.hpp
        include/nexenne/filter/butterworth.hpp
        include/nexenne/filter/complementary.hpp
        include/nexenne/filter/debounce.hpp
        include/nexenne/filter/ema.hpp
        include/nexenne/filter/fir.hpp
        include/nexenne/filter/glitch.hpp
        include/nexenne/filter/highpass.hpp
        include/nexenne/filter/hysteresis.hpp
        include/nexenne/filter/kalman.hpp
        include/nexenne/filter/lowpass.hpp
        include/nexenne/filter/majority.hpp
        include/nexenne/filter/median.hpp
        include/nexenne/filter/range_guard.hpp
        include/nexenne/filter/rate_guard.hpp
        include/nexenne/filter/slew.hpp
        include/nexenne/filter/sma.hpp
        include/nexenne/filter/stale_detector.hpp
        include/nexenne/filter/timed_debounce.hpp
        include/nexenne/filter/validator.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
