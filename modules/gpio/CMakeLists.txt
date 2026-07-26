cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(gpio _nexenne_module_version)
    project(nexenne_gpio
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::gpio - GPIO line abstraction with pluggable backends"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Header-only by default. For a compiled module, add KIND STATIC or SHARED
# and a SOURCES list. Inter-module dependencies go in a module.deps file next
# to this one, not here. External libraries go in LINK_PUBLIC / LINK_PRIVATE.
nexenne_add_module(gpio
    HEADERS
        include/nexenne/gpio/gpio.hpp
        include/nexenne/gpio/error.hpp
        include/nexenne/gpio/line_types.hpp
        include/nexenne/gpio/line_spec.hpp
        include/nexenne/gpio/line_config.hpp
        include/nexenne/gpio/line_event.hpp
        include/nexenne/gpio/line_value.hpp
        include/nexenne/gpio/backend.hpp
        include/nexenne/gpio/line.hpp
        include/nexenne/gpio/chip.hpp
        include/nexenne/gpio/decode.hpp
        include/nexenne/gpio/debounce.hpp
        include/nexenne/gpio/sequence_tracker.hpp
        include/nexenne/gpio/sink.hpp
        include/nexenne/gpio/io/callback_sink.hpp
        include/nexenne/gpio/io/mock_chip.hpp
        include/nexenne/gpio/io/queue_sink.hpp
        include/nexenne/gpio/example.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
