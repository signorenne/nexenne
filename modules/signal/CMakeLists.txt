cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(signal _nexenne_module_version)
    project(nexenne_signal
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::signal - header-only signal/slot multicast callbacks, no virtual dispatch"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Header-only by default. For a compiled module, add KIND STATIC or SHARED
# and a SOURCES list. Inter-module dependencies go in a module.deps file next
# to this one, not here. External libraries go in LINK_PUBLIC / LINK_PRIVATE.
nexenne_add_module(signal)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
