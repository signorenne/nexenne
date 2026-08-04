cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(logging _nexenne_module_version)
    project(nexenne_logging
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::logging - structured, sink-based logging with levels and formatters"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Compiled: the sinks, the formatter, and the record helpers are ordinary
# runtime code, so they are built once here instead of in every consumer's
# translation unit. The generic parts (the logger, the manager policy, the
# format string, ring_sink, stream_logger) stay templates in the headers.
#
# Host sinks are separated because they need a hosted standard library
# (threads, files, stdio). A freestanding target keeps the header-only
# stream_logger path by configuring with -DNEXENNE_LOGGING_HOST_SINKS=OFF.
option(NEXENNE_LOGGING_HOST_SINKS "Build the sinks that need a hosted standard library" ON)

set(_logging_sources
    src/record.cpp
    src/multi_sink.cpp
)
if(NEXENNE_LOGGING_HOST_SINKS)
    list(APPEND _logging_sources
        src/sink.cpp
        src/pattern_formatter.cpp
        src/json_sink.cpp
        src/rotating_file_sink.cpp
        src/async_sink.cpp
    )
endif()

nexenne_add_module(logging KIND STATIC SOURCES ${_logging_sources})

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
