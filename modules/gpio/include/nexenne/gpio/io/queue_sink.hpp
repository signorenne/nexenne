#pragma once

/**
 * @file
 * @brief The buffered edge transport: a lock-free single-producer ring.
 *
 * When the producer and the consumer are different contexts (an interrupt
 * handler feeding a main loop, a poll thread feeding a worker), events need
 * a buffer that neither blocks nor allocates. \c queue_sink wraps
 * \c nexenne::container::spsc_queue: the producer pushes, the single
 * consumer drains with \c try_pop, and a full ring rejects the push so the
 * producer can count the drop instead of stalling, the only correct
 * behaviour in an interrupt. It satisfies both \c edge_sink and
 * \c draining_edge_sink.
 *
 * The SPSC contract is strict: exactly one producer context calls \c push
 * and exactly one consumer context calls \c try_pop.
 */

#include <cstddef>
#include <optional>

#include <nexenne/container/spsc_queue.hpp>
#include <nexenne/gpio/line_event.hpp>

namespace nexenne::gpio {

/**
 * @brief A lock-free, allocation-free ring carrying edge events.
 *
 * Non-copyable and non-movable: the ring pins its storage. The effective
 * capacity is \p N - 1; one slot distinguishes empty from full.
 *
 * @tparam N Ring slot count, at least two.
 */
template <std::size_t N>
class queue_sink {
public:
  using value_type = line_event;

  /// Ring slot count; the effective capacity is one less.
  static constexpr std::size_t slot_count{N};

private:
  container::spsc_queue<line_event, N> m_queue{};

public:
  /**
   * @brief Constructs an empty ring.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr queue_sink() noexcept = default;

  /**
   * @brief Enqueues one event; producer side.
   *
   * @param event Event to buffer.
   *
   * @return \c true when queued; \c false when the ring was full and the
   *         event was dropped.
   *
   * @pre Called from the single producer context only.
   * @post On \c true the buffered count grew by one; on \c false the ring
   *       is unchanged.
   *
   * @complexity \c O(1).
   */
  auto push(line_event const& event) noexcept -> bool {
    return m_queue.push(event).has_value();
  }

  /**
   * @brief Dequeues the next buffered event; consumer side.
   *
   * @return The oldest buffered event, or \c std::nullopt when the ring is
   *         empty.
   *
   * @pre Called from the single consumer context only.
   * @post On a value result the buffered count shrank by one.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto try_pop() noexcept -> std::optional<line_event> {
    return m_queue.try_pop();
  }

  /**
   * @brief Best-effort emptiness test.
   *
   * @return \c true when the ring appeared empty at the observation.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_queue.empty_approx();
  }

  /**
   * @brief The number of events the ring can hold.
   *
   * @return The effective capacity, \p N - 1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t {
    return container::spsc_queue<line_event, N>::capacity();
  }
};

}  // namespace nexenne::gpio
