cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_logging
        VERSION     0.1.0
        DESCRIPTION "nexenne::logging - structured, sink-based logging with levels and formatters"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Header-only. Inter-module dependencies live in module.deps next to this file.
nexenne_add_module(logging
        HEADERS
        include/nexenne/logging/logging.hpp
        include/nexenne/logging/async_sink.hpp
        include/nexenne/logging/config.hpp
        include/nexenne/logging/format_string.hpp
        include/nexenne/logging/esp_log_sink.hpp
        include/nexenne/logging/json_sink.hpp
        include/nexenne/logging/level.hpp
        include/nexenne/logging/logger.hpp
        include/nexenne/logging/macros.hpp
        include/nexenne/logging/manager.hpp
        include/nexenne/logging/multi_sink.hpp
        include/nexenne/logging/pattern_formatter.hpp
        include/nexenne/logging/record.hpp
        include/nexenne/logging/rotating_file_sink.hpp
        include/nexenne/logging/sink.hpp
        include/nexenne/logging/stream_logger.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
