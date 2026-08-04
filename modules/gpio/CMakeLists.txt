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
# Compiled: the Linux character-device backend is ordinary runtime code, so it
# is built once here rather than in every consumer's translation unit. Doing so
# also keeps <linux/gpio.h> and the ioctl surface out of the public headers.
# The sources compile to nothing off Linux (they carry the same #ifdef __linux__
# guard as the headers), so the non-Linux stub path is unaffected.
nexenne_add_module(gpio KIND STATIC SOURCES
    src/io/chardev_chip.cpp
    src/io/chardev_watch.cpp
    src/io/chardev_info.cpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
