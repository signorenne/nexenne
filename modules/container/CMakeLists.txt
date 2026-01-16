cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers. Inside the monorepo the root has already done this.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_container
        VERSION     0.1.0
        DESCRIPTION "nexenne::container - header-only C++23 containers"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(container
    HEADERS
        include/nexenne/container/container.hpp
        include/nexenne/container/bitset_dynamic.hpp
        include/nexenne/container/error.hpp
        include/nexenne/container/linear_arena.hpp
        include/nexenne/container/ring_buffer.hpp
        include/nexenne/container/small_vector.hpp
        include/nexenne/container/stable_vector.hpp
        include/nexenne/container/static_vector.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
