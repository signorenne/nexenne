cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(nexenne_can
        VERSION     0.1.0
        DESCRIPTION "nexenne::can - CAN bus communications"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

# Header-only by default. For a compiled module, add KIND STATIC or SHARED
# and a SOURCES list. Inter-module dependencies go in a module.deps file next
# to this one, not here. External libraries go in LINK_PUBLIC / LINK_PRIVATE.
nexenne_add_module(can
    HEADERS
        include/nexenne/can/bus.hpp
        include/nexenne/can/byte_field.hpp
        include/nexenne/can/byte_order.hpp
        include/nexenne/can/can.hpp
        include/nexenne/can/codec.hpp
        include/nexenne/can/database.hpp
        include/nexenne/can/database_builder.hpp
        include/nexenne/can/dlc.hpp
        include/nexenne/can/error.hpp
        include/nexenne/can/error_frame.hpp
        include/nexenne/can/filter.hpp
        include/nexenne/can/frame.hpp
        include/nexenne/can/id.hpp
        include/nexenne/can/io/loopback_bus.hpp
        include/nexenne/can/io/socketcan_bus.hpp
        include/nexenne/can/j1939_id.hpp
        include/nexenne/can/j1939_transport.hpp
        include/nexenne/can/message.hpp
        include/nexenne/can/message_builder.hpp
        include/nexenne/can/packing_plan.hpp
        include/nexenne/can/registry.hpp
        include/nexenne/can/signal.hpp
        include/nexenne/can/signal_builder.hpp
        include/nexenne/can/socket_options.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
