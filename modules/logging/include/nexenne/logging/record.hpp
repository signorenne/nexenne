#pragma once

/**
 * @file
 * @brief A single formatted-and-stamped log event awaiting dispatch.
 *
 * \c record is the unit of work that flows from a \c logger through the async
 * backend queue to one or more \c sink instances. By the time a record reaches
 * the queue its message has already been formatted on the producer thread, so
 * the backend only chooses where to write it.
 *
 * Trade-off: formatting on the producer keeps the queue payload a single
 * cheap-to-move type (one owned \c std::string message plus a borrowed name view
 * and scalar metadata), at the cost of one heap allocation per log call for the
 * message. A hot path that cannot tolerate that should gate the call out with
 * the compile-time \c level filter (\c NEXENNE_LOG_MIN_LEVEL), which elides the
 * whole \c LOG_* expansion.
 *
 * Records are move-only by convention: the queue pushes by move, sinks consume
 * by const reference.
 */

#include <chrono>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <nexenne/logging/level.hpp>

namespace nexenne::logging {

/**
 * @brief A single formatted-and-stamped log event awaiting dispatch.
 *
 * The unit of work that flows from a \c logger through the async backend queue
 * to one or more sinks. The message is already formatted on the producer
 * thread, so the backend only chooses where to write it. The logger name is a
 * borrowed view: a \c basic_logger passes an interned (process-lifetime) name,
 * so the view stays valid even after the originating logger is destroyed and
 * the record waits in the async queue. A caller constructing a record directly
 * must likewise ensure the name outlives the record.
 *
 * @pre None.
 * @post None.
 */
struct record {
  std::chrono::system_clock::time_point timestamp;  ///< Wall-clock time the record was created.
  level severity{level::info};                      ///< Severity of the event.
  std::source_location location{};                  ///< Call site that produced the record.
  std::thread::id thread_id;                        ///< Id of the producing thread.
  std::string_view logger_name;  ///< Borrowed (interned) originating logger name.
  std::string message;           ///< Pre-formatted message text.

  /**
   * @brief Constructs an empty record.
   *
   * @pre None.
   * @post The severity is \c level::info and all string fields are empty.
   */
  record() = default;

  /**
   * @brief Constructs a record, stamping the construction time and thread.
   *
   * The name is stored as a borrowed view (no copy); the message is moved in.
   *
   * @param sev Severity of the event.
   * @param loc Source location of the call site.
   * @param name Logger name; borrowed, must outlive the record (see the type
   *        documentation for the interning guarantee).
   * @param msg Pre-formatted message text, moved in.
   *
   * @pre \p name outlives this record.
   * @post \c timestamp is the construction time, \c thread_id is the calling
   *       thread, and the remaining fields hold the arguments.
   *
   * @complexity \c O(1).
   */
  record(
    level const sev, std::source_location const loc, std::string_view const name, std::string msg
  ) noexcept
      : timestamp{std::chrono::system_clock::now()}
      , severity{sev}
      , location{loc}
      , thread_id{std::this_thread::get_id()}
      , logger_name{name}
      , message{std::move(msg)} {}
};

}  // namespace nexenne::logging
