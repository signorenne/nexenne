#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::logging module.
 *
 * Including this header pulls in the whole logging stack: levels, records, the
 * compile-time config, the sinks, the formatter, the manager backend, the
 * loggers, and the \c LOG_* macros. Include a specific subheader to take only
 * what you need. The ESP-IDF sink (\c esp_log_sink.hpp) is intentionally
 * excluded; include it explicitly on an ESP-IDF build.
 */

#include <nexenne/logging/async_sink.hpp>
#include <nexenne/logging/config.hpp>
#include <nexenne/logging/format_string.hpp>
#include <nexenne/logging/json_sink.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/logger.hpp>
#include <nexenne/logging/macros.hpp>
#include <nexenne/logging/manager.hpp>
#include <nexenne/logging/multi_sink.hpp>
#include <nexenne/logging/pattern_formatter.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/rotating_file_sink.hpp>
#include <nexenne/logging/sink.hpp>
#include <nexenne/logging/stream_logger.hpp>
