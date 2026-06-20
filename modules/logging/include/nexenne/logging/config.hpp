#pragma once

/**
 * @file
 * @brief Compile-time configuration policy for the logger and manager.
 *
 * Every templated type in the module (\c basic_manager, \c basic_logger) takes
 * a \c Config type parameter carrying:
 *
 *   - \c queue_size : capacity of the async backend queue (a power of two in
 *     async mode; ignored in sync mode).
 *   - \c async : \c true runs a backend thread plus a lock-free queue;
 *     \c false dispatches every \c push synchronously on the calling thread,
 *     with no thread and no queue.
 *
 * The library deliberately ships no named presets ("embedded", "server", ...):
 * real targets vary too much to fit a few buckets, so choose \c queue_size and
 * \c async to match the platform. The \c default_config alias reads its values
 * from the build-time macros \c NEXENNE_LOG_QUEUE_SIZE and \c NEXENNE_LOG_ASYNC,
 * so a project can switch defaults without touching source.
 *
 * Footprint note: the async queue reserves \c queue_size slots statically, each
 * \c sizeof(record) bytes (~80 on a 64-bit host). The default of 1024 is ~80 KiB
 * (embedded-sane); a high-throughput server may raise it (e.g. 8192), and a tiny
 * MCU may lower it (e.g. 256) via \c NEXENNE_LOG_QUEUE_SIZE.
 *
 * @code
 * using my_cfg = nexenne::logging::config<256, true>;
 * using mgr = nexenne::logging::basic_manager<my_cfg>;
 * using lgr = nexenne::logging::basic_logger<my_cfg>;
 * @endcode
 */

#include <concepts>
#include <cstddef>

namespace nexenne::logging {

/**
 * @brief Compile-time configuration policy for the logger and manager.
 *
 * @tparam QueueSize Capacity of the async backend queue.
 * @tparam Async Whether to dispatch asynchronously.
 *
 * @pre \p QueueSize is a power of two when \p Async is \c true.
 * @post None.
 */
template <std::size_t QueueSize, bool Async>
struct config {
  /// Async queue capacity; a power of two in async mode, ignored otherwise.
  static constexpr std::size_t queue_size = QueueSize;
  /// Whether the manager runs a backend thread and lock-free queue.
  static constexpr bool async = Async;
};

}  // namespace nexenne::logging

// Override either macro before including the umbrella to tune the default, e.g.
// for an ESP-IDF project:
//   target_compile_definitions(my_target PRIVATE
//       NEXENNE_LOG_QUEUE_SIZE=256 NEXENNE_LOG_ASYNC=true)
#ifndef NEXENNE_LOG_QUEUE_SIZE
#  define NEXENNE_LOG_QUEUE_SIZE 1024
#endif

#ifndef NEXENNE_LOG_ASYNC
#  define NEXENNE_LOG_ASYNC true
#endif

namespace nexenne::logging {

/**
 * @brief Default config, tunable via the build-time macros.
 *
 * Reads its queue size and async flag from \c NEXENNE_LOG_QUEUE_SIZE and
 * \c NEXENNE_LOG_ASYNC. The library ships only this default and leaves every
 * other deployment shape to the caller.
 *
 * @pre None.
 * @post None.
 */
using default_config = config<NEXENNE_LOG_QUEUE_SIZE, NEXENNE_LOG_ASYNC>;

/**
 * @brief Satisfied by any type usable as a logger configuration.
 *
 * Requires a \c queue_size convertible to \c std::size_t and an \c async
 * convertible to \c bool.
 *
 * @tparam C Type under test.
 *
 * @pre None.
 * @post None.
 */
template <typename C>
concept config_like = requires {
  { C::queue_size } -> std::convertible_to<std::size_t>;
  { C::async } -> std::convertible_to<bool>;
};

}  // namespace nexenne::logging
