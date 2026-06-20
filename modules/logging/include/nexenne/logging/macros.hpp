#pragma once

/**
 * @file
 * @brief Compile-time-gated \c LOG_* macros.
 *
 * Each macro compiles to nothing when its severity is below
 * \c NEXENNE_LOG_MIN_LEVEL. When the level is enabled at compile time the macro
 * expands into a runtime-checked logger call; a level disabled at runtime costs
 * a single relaxed atomic load. The source location is captured through the
 * \c format_string wrapper the logger overloads accept, so callers never thread
 * \c std::source_location explicitly.
 *
 * Two flavours: \c LOG_INFO(fmt, ...) targets the default logger, while
 * \c LOG_INFO_TO(lgr, fmt, ...) targets a supplied logger.
 */

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/logger.hpp>

/**
 * @def NEXENNE_LOG_IF_ENABLED_
 * @brief Internal: emit to the default logger when the compile-time gate passes.
 */
#define NEXENNE_LOG_IF_ENABLED_(level_, ...)                                                       \
  do {                                                                                             \
    if constexpr ((level_) >= NEXENNE_LOG_MIN_LEVEL) {                                             \
      ::nexenne::logging::default_logger().log((level_), __VA_ARGS__);                             \
    }                                                                                              \
  } while (false)

/**
 * @def NEXENNE_LOG_TO_IF_ENABLED_
 * @brief Internal: emit to a supplied logger when the compile-time gate passes.
 */
#define NEXENNE_LOG_TO_IF_ENABLED_(level_, logger_, ...)                                           \
  do {                                                                                             \
    if constexpr ((level_) >= NEXENNE_LOG_MIN_LEVEL) {                                             \
      (logger_).log((level_), __VA_ARGS__);                                                        \
    }                                                                                              \
  } while (false)

/** @brief Logs to the default logger at \c level::trace. */
#define LOG_TRACE(...) NEXENNE_LOG_IF_ENABLED_(::nexenne::logging::level::trace, __VA_ARGS__)
/** @brief Logs to the default logger at \c level::debug. */
#define LOG_DEBUG(...) NEXENNE_LOG_IF_ENABLED_(::nexenne::logging::level::debug, __VA_ARGS__)
/** @brief Logs to the default logger at \c level::info. */
#define LOG_INFO(...) NEXENNE_LOG_IF_ENABLED_(::nexenne::logging::level::info, __VA_ARGS__)
/** @brief Logs to the default logger at \c level::warn. */
#define LOG_WARN(...) NEXENNE_LOG_IF_ENABLED_(::nexenne::logging::level::warn, __VA_ARGS__)
/** @brief Logs to the default logger at \c level::error. */
#define LOG_ERROR(...) NEXENNE_LOG_IF_ENABLED_(::nexenne::logging::level::error, __VA_ARGS__)
/** @brief Logs to the default logger at \c level::critical. */
#define LOG_CRITICAL(...) NEXENNE_LOG_IF_ENABLED_(::nexenne::logging::level::critical, __VA_ARGS__)

/** @brief Logs to \p logger_ at \c level::trace. */
#define LOG_TRACE_TO(logger_, ...)                                                                 \
  NEXENNE_LOG_TO_IF_ENABLED_(::nexenne::logging::level::trace, (logger_), __VA_ARGS__)
/** @brief Logs to \p logger_ at \c level::debug. */
#define LOG_DEBUG_TO(logger_, ...)                                                                 \
  NEXENNE_LOG_TO_IF_ENABLED_(::nexenne::logging::level::debug, (logger_), __VA_ARGS__)
/** @brief Logs to \p logger_ at \c level::info. */
#define LOG_INFO_TO(logger_, ...)                                                                  \
  NEXENNE_LOG_TO_IF_ENABLED_(::nexenne::logging::level::info, (logger_), __VA_ARGS__)
/** @brief Logs to \p logger_ at \c level::warn. */
#define LOG_WARN_TO(logger_, ...)                                                                  \
  NEXENNE_LOG_TO_IF_ENABLED_(::nexenne::logging::level::warn, (logger_), __VA_ARGS__)
/** @brief Logs to \p logger_ at \c level::error. */
#define LOG_ERROR_TO(logger_, ...)                                                                 \
  NEXENNE_LOG_TO_IF_ENABLED_(::nexenne::logging::level::error, (logger_), __VA_ARGS__)
/** @brief Logs to \p logger_ at \c level::critical. */
#define LOG_CRITICAL_TO(logger_, ...)                                                              \
  NEXENNE_LOG_TO_IF_ENABLED_(::nexenne::logging::level::critical, (logger_), __VA_ARGS__)
