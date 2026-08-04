cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(serialization _nexenne_module_version)
    project(nexenne_serialization
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::serialization - JSON, CBOR, MessagePack, binary, COBS codecs"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Compiled: the JSON parser and serializer are ordinary runtime code, built once
# here rather than in every consumer translation unit. The schema-driven binary
# reader/writer, CBOR, MessagePack, and COBS stay templates in the headers.
nexenne_add_module(serialization KIND STATIC SOURCES
    src/json/parse.cpp
    src/json/serialize.cpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
