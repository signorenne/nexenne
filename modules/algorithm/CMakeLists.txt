cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers. Inside the monorepo the root has already done this.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_algorithm
        VERSION     0.1.0
        DESCRIPTION "nexenne::algorithm - header-only C++23 algorithms"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(algorithm
    HEADERS
        include/nexenne/algorithm/algorithm.hpp
        include/nexenne/algorithm/binary_search.hpp
        include/nexenne/algorithm/checksum/crc.hpp
        include/nexenne/algorithm/checksum/modular_sum.hpp
        include/nexenne/algorithm/encoding/alphabet.hpp
        include/nexenne/algorithm/encoding/base_n.hpp
        include/nexenne/algorithm/encoding/cobs.hpp
        include/nexenne/algorithm/encoding/codec_error.hpp
        include/nexenne/algorithm/encoding/url.hpp
        include/nexenne/algorithm/hash/fnv.hpp
        include/nexenne/algorithm/hash/murmur.hpp
        include/nexenne/algorithm/hash/xxhash.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
