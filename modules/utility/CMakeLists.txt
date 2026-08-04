cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers. Inside the monorepo the root has already done this.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(utility _nexenne_module_version)
    project(nexenne_utility
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::utility - small, dependency-free C++23 utilities"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(utility)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
