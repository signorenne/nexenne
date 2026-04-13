cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(random _nexenne_module_version)
    project(nexenne_random
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::random - seedable RNG engines, distributions and sampling"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(random
    HEADERS
        include/nexenne/random/discrete.hpp
        include/nexenne/random/exponential.hpp
        include/nexenne/random/gamma.hpp
        include/nexenne/random/normal.hpp
        include/nexenne/random/pcg.hpp
        include/nexenne/random/poisson.hpp
        include/nexenne/random/random.hpp
        include/nexenne/random/sample.hpp
        include/nexenne/random/seed_seq.hpp
        include/nexenne/random/uniform.hpp
        include/nexenne/random/xoshiro.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
