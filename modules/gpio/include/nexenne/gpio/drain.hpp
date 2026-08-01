#pragma once

/**
 * @file
 * @brief The pump between an edge source and an edge sink.
 *
 * Every readiness loop repeats the same four lines: poll the backend with a
 * zero timeout, stop on the clean miss, push into the transport, count what
 * the transport rejected. \c drain_events is that loop, written once. It
 * never blocks (the zero timeout is the whole point: the caller's event
 * loop already established readiness), so it drops into an epoll handler, a
 * Qt socket-notifier slot, or a bare-metal main loop unchanged.
 */

#include <chrono>
#include <cstdint>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/sink.hpp>

namespace nexenne::gpio {

/**
 * @brief What one \c drain_events call moved and lost.
 */
struct drain_report {
  using value_type = std::uint64_t;

  std::uint64_t delivered{0};  ///< Events the sink accepted.
  std::uint64_t rejected{0};   ///< Events the sink refused (full transport).

  /**
   * @brief Member-wise equality of two reports.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both counters are equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(drain_report const& lhs, drain_report const& rhs) noexcept -> bool = default;
};

/**
 * @brief Moves every ready event from \p source into \p sink, not blocking.
 *
 * Polls \p source with a zero timeout until it reports a clean miss,
 * pushing each event into \p sink. A rejected push is counted and draining
 * continues, because consuming the kernel's buffer is still right when the
 * userspace transport is full: leaving events in the kernel would only move
 * the overflow one level down. Call it whenever the source's native handle
 * signals readable.
 *
 * @tparam Source Backend type satisfying \c edge_source.
 * @tparam Sink Transport type satisfying \c edge_sink.
 * @param source Backend to drain.
 * @param sink Transport receiving the events.
 *
 * @return The delivered and rejected counts; on a wait failure, the error
 *         (events moved before the failure are already in \p sink).
 *
 * @pre \p source is open with edge detection requested.
 * @post \p source has no ready event buffered, or the error tells why the
 *       draining stopped.
 */
template <edge_source Source, edge_sink Sink>
auto drain_events(Source& source, Sink& sink) -> result<drain_report> {
  drain_report report{};
  while (true) {
    auto const event{source.wait_event(std::chrono::nanoseconds{0})};
    if (!event.has_value()) {
      return std::unexpected{event.error()};
    }
    if (!event->has_value()) {
      return report;
    }
    if (sink.push(**event)) {
      report.delivered += 1;
    } else {
      report.rejected += 1;
    }
  }
}

}  // namespace nexenne::gpio
