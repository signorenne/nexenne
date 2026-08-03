#pragma once

/**
 * @file
 * @brief An in-memory CAN bus: frames sent are queued and read back.
 *
 * The loopback bus is the backend that needs no hardware. A sent frame is
 * enqueued in a fixed-capacity ring buffer, and receiving dequeues the oldest
 * frame, applying any filters on the way out. It satisfies the \c bus.hpp
 * \c can_bus concept, so tests, examples, and offline tools can exercise the send
 * and receive path deterministically and without allocation, on any platform. Its
 * controller state is always error-active; an in-memory bus has no error
 * confinement.
 */

#include <cstddef>
#include <optional>
#include <span>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/small_vector.hpp>

namespace nexenne::can {

/**
 * @brief A fixed-capacity, in-memory CAN bus backend.
 *
 * @tparam Capacity Number of frames the queue holds before \c send reports the
 *                  buffer is full.
 */
template <std::size_t Capacity = 64>
class loopback_bus {
public:
  using value_type = frame;

  /// @brief Inline capacity for the filter set before it spills to the heap.
  static constexpr std::size_t inline_filters{4};

private:
  container::ring_buffer<frame, Capacity> m_queue{};
  container::small_vector<filter, inline_filters> m_filters{};

  [[nodiscard]] auto accepts(can_id const id) const noexcept -> bool {
    if (m_filters.empty()) {
      return true;
    }
    for (filter const f : m_filters) {
      if (f.matches(id)) {
        return true;
      }
    }
    return false;
  }

public:
  /**
   * @brief Constructs an empty loopback bus with no filters.
   *
   * @pre None.
   * @post The queue is empty and every frame is accepted.
   */
  loopback_bus() noexcept = default;

  /**
   * @brief Enqueues a frame to be read back later.
   *
   * @param f Frame to send.
   *
   * @return Empty on success, or \c can_error::buffer_full when the queue is at
   *         capacity.
   *
   * @pre None.
   * @post On success the queue has grown by one frame.
   */
  auto send(frame const& f) -> result<void> {
    if (!m_queue.push(f)) {
      return std::unexpected{can_error::buffer_full};
    }
    return {};
  }

  /**
   * @brief Dequeues the next frame that passes the filters.
   *
   * Drops queued frames that the filters reject until an accepted frame is found
   * or the queue empties.
   *
   * @return The next accepted frame, \c std::nullopt when none is ready, never
   *         an error for this backend.
   *
   * @pre None.
   * @post Every frame dequeued, accepted or dropped, has left the queue.
   */
  auto receive() -> result<std::optional<frame>> {
    while (!m_queue.empty()) {
      auto const popped{m_queue.pop()};
      if (!popped) {
        break;
      }
      if (accepts(popped->id())) {
        return std::optional<frame>{*popped};
      }
    }
    return std::optional<frame>{};
  }

  /**
   * @brief Replaces the filter set applied on receive.
   *
   * @param filters Filters to install; an empty span accepts every frame.
   *
   * @return Empty; setting filters on this backend cannot fail.
   *
   * @pre None.
   * @post \c receive now drops frames that match none of \p filters.
   */
  auto set_filters(std::span<filter const> const filters) -> result<void> {
    m_filters.clear();
    for (filter const f : filters) {
      m_filters.push_back(f);
    }
    return {};
  }

  /**
   * @brief The controller state, always error-active for an in-memory bus.
   *
   * @return \c bus_state::error_active.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto state() const noexcept -> bus_state {
    return bus_state::error_active;
  }

  /**
   * @brief The number of frames waiting in the queue.
   *
   * @return The queue size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto pending() const noexcept -> std::size_t {
    return m_queue.size();
  }

  /**
   * @brief Reports whether the queue is empty.
   *
   * @return \c true when no frame is waiting.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_queue.empty();
  }

  /**
   * @brief Drops all queued frames.
   *
   * @pre None.
   * @post \c pending() is zero; the filter set is unchanged.
   */
  auto clear() noexcept -> void {
    m_queue.clear();
  }
};

static_assert(can_bus<loopback_bus<>>, "loopback_bus must satisfy the can_bus concept");

}  // namespace nexenne::can
