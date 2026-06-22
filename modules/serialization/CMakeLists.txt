cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_serialization
        VERSION     0.1.0
        DESCRIPTION "nexenne::serialization - JSON, CBOR, MessagePack, binary, COBS codecs"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(serialization
    HEADERS
        include/nexenne/serialization/serialization.hpp
        include/nexenne/serialization/error.hpp
        include/nexenne/serialization/format.hpp
        include/nexenne/serialization/cobs.hpp
        include/nexenne/serialization/versioned.hpp
        include/nexenne/serialization/cbor.hpp
        include/nexenne/serialization/msgpack.hpp
        include/nexenne/serialization/binary/reader.hpp
        include/nexenne/serialization/binary/writer.hpp
        include/nexenne/serialization/json/value.hpp
        include/nexenne/serialization/json/parse.hpp
        include/nexenne/serialization/json/serialize.hpp
        include/nexenne/serialization/json/sax.hpp
        include/nexenne/serialization/json/writer.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
