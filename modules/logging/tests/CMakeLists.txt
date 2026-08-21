# The freestanding half of the module is level, record, config, and the
# header-only stream_logger; everything else reaches sink.hpp, directly or
# through a sink it derives from, and only exists when the host sinks do.
set(_logging_test_sources
    test_config.cpp
    test_level.cpp
    test_record.cpp
    test_stream_logger.cpp
)
if(NEXENNE_LOGGING_HOST_SINKS)
    list(APPEND _logging_test_sources
        test_async_sink.cpp
        test_json_sink.cpp
        test_logger.cpp
        test_manager.cpp
        test_multi_sink.cpp
        test_pattern_formatter.cpp
        test_rotating_file_sink.cpp
        test_sink.cpp
    )
endif()

nexenne_add_module_tests(logging SOURCES ${_logging_test_sources})
