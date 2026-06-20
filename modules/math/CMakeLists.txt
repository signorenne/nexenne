cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_math
        VERSION     0.1.0
        DESCRIPTION "nexenne::math - fixed-size vectors, matrices, quaternions and scalar math"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(math
    HEADERS
        include/nexenne/math/concepts.hpp
        include/nexenne/math/constants.hpp
        include/nexenne/math/error.hpp
        include/nexenne/math/power.hpp
        include/nexenne/math/scalar.hpp
        include/nexenne/math/math.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
