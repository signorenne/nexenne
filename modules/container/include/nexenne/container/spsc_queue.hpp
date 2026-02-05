#pragma once

/**
 * @file
 * @brief Lock-free single-producer / single-consumer bounded ring queue.
 *
 * \c spsc_queue<T, N> is a fixed-capacity FIFO for exactly one producer thread
 * and one consumer thread. Both indices are \c std::atomic and the protocol uses
 * acquire/release ordering to publish writes across cores: no locks, no
 * compare-exchange loops, no allocation. The producer owns the tail and the
 * consumer owns the head, and the two atomics sit on separate cache lines to
 * avoid false sharing.
 *
 * The contract is strict: exactly one thread may call \c push / \c emplace (the
 * producer) and exactly one may call \c pop / \c try_pop (the consumer); the
 * approximate observers (\c size_approx, \c empty_approx, \c full_approx) are
 * best-effort from either side. Calling \c push or \c pop from more than one
 * thread breaks the contract; use \c mpsc_queue or \c mpmc_queue for that. One
 * slot is reserved as a sentinel so \c head == \c tail unambiguously means empty,
 * making the effective capacity \p N - 1. Every operation is \c noexcept; there
 * is no allocation.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count; the effective capacity is \p N - 1.
 */

#include <atomic>
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Lock-free single-producer / single-consumer bounded ring queue.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count (at least two); effective capacity is \p N - 1.
 *
 * @pre None.
 * @post A default-constructed queue is empty.
 */
template <std::move_constructible T, std::size_t N>
  requires(N >= 2)
class spsc_queue {
public:
  using value_type = T;
  using size_type = std::size_t;

  static constexpr size_type capacity_value = N - 1;

private:
  alignas(T) std::byte m_storage[sizeof(T) * N]{};

  // Head and tail on separate cache lines: the producer writing tail must not
  // invalidate the consumer's cache line holding head, and vice versa.
  static constexpr std::size_t cache_line = 64;
  alignas(cache_line) std::atomic<size_type> m_head{0};  // consumer advances this
  alignas(cache_line) std::atomic<size_type> m_tail{0};  // producer advances this

  [[nodiscard]] auto buffer() noexcept -> T* {
    return reinterpret_cast<T*>(m_storage);
  }

  [[nodiscard]] auto buffer() const noexcept -> T const* {
    return reinterpret_cast<T const*>(m_storage);
  }

  // Power-of-two N wraps with a mask (a single AND); any other N uses a modulo.
  [[nodiscard]] static constexpr auto next(size_type const i) noexcept -> size_type {
    if constexpr ((N & (N - 1)) == 0) {
      return (i + 1) & (N - 1);
    } else {
      return (i + 1) % N;
    }
  }

public:
  /**
   * @brief Constructs an empty queue.
   *
   * @pre None.
   * @post \c empty_approx() is \c true.
   */
  constexpr spsc_queue() noexcept = default;

  spsc_queue(spsc_queue const&) = delete;
  auto operator=(spsc_queue const&) -> spsc_queue& = delete;
  spsc_queue(spsc_queue&&) = delete;
  auto operator=(spsc_queue&&) -> spsc_queue& = delete;

  ~spsc_queue() noexcept {
    // Single-threaded at destruction: drain so element destructors run.
    while (pop().has_value()) {}
  }

  /**
   * @brief Effective capacity, one slot fewer than \p N.
   *
   * @return The number of elements the queue can hold.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> size_type {
    return capacity_value;
  }

  /**
   * @brief Largest number of elements the queue can ever hold.
   *
   * @return The effective capacity, \p N - 1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return capacity_value;
  }

  /**
   * @brief Approximate live element count, safe from either thread.
   *
   * @return Best-effort count of queued elements; may already be stale.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] auto size_approx() const noexcept -> size_type {
    auto const h{m_head.load(std::memory_order_acquire)};
    auto const t{m_tail.load(std::memory_order_acquire)};
    return (t + N - h) % N;
  }

  /**
   * @brief Best-effort test for an empty queue.
   *
   * @return \c true when the queue appeared empty at the observation.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] auto empty_approx() const noexcept -> bool {
    return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
  }

  /**
   * @brief Best-effort test for a full queue.
   *
   * @return \c true when the queue appeared full at the observation.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] auto full_approx() const noexcept -> bool {
    auto const t{m_tail.load(std::memory_order_relaxed)};
    return next(t) == m_head.load(std::memory_order_acquire);
  }

  /**
   * @brief Pushes a copy of \p value at the tail. Producer thread only.
   *
   * @param value Element to copy in.
   *
   * @return Nothing on success, or \c container_error::full when the queue has no
   *         room.
   *
   * @pre Called from the single producer thread only.
   * @post On success the queued count grew by one; on failure unchanged.
   *
   * @complexity \c O(1).
   */
  auto push(T const& value) noexcept -> std::expected<void, container_error> {
    return emplace(value);
  }

  /**
   * @brief Pushes \p value at the tail by moving it. Producer thread only.
   *
   * @param value Element to move in.
   *
   * @return Nothing on success, or \c container_error::full when the queue has no
   *         room.
   *
   * @pre Called from the single producer thread only.
   * @post On success the queued count grew by one; on failure unchanged.
   *
   * @complexity \c O(1).
   */
  auto push(T&& value) noexcept -> std::expected<void, container_error> {
    return emplace(std::move(value));
  }

  /**
   * @brief Constructs an element in place at the tail. Producer thread only.
   *
   * @tparam Args Constructor argument types for \p T.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Nothing on success, or \c container_error::full when the queue has no
   *         room.
   *
   * @pre Called from the single producer thread only.
   * @post On success the queued count grew by one; on failure unchanged.
   *
   * @complexity \c O(1).
   */
  template <typename... Args>
  auto emplace(Args&&... args) noexcept -> std::expected<void, container_error> {
    auto const t{m_tail.load(std::memory_order_relaxed)};
    auto const next_t{next(t)};
    if (next_t == m_head.load(std::memory_order_acquire)) {
      return std::unexpected{container_error::full};
    }
    std::construct_at(buffer() + t, std::forward<Args>(args)...);
    m_tail.store(next_t, std::memory_order_release);
    return {};
  }

  /**
   * @brief Pops and returns the head element. Consumer thread only.
   *
   * @return The dequeued element, or \c container_error::empty when none is
   *         ready.
   *
   * @pre Called from the single consumer thread only.
   * @post On success the queued count shrank by one and the former head was
   *       destroyed; on failure unchanged.
   *
   * @complexity \c O(1).
   */
  auto pop() noexcept -> std::expected<T, container_error> {
    auto const h{m_head.load(std::memory_order_relaxed)};
    if (h == m_tail.load(std::memory_order_acquire)) {
      return std::unexpected{container_error::empty};
    }
    auto value{std::move(buffer()[h])};
    std::destroy_at(buffer() + h);
    m_head.store(next(h), std::memory_order_release);
    return value;
  }

  /**
   * @brief Optional-returning pop. Consumer thread only.
   *
   * @return The dequeued element, or \c std::nullopt when empty.
   *
   * @pre Called from the single consumer thread only.
   * @post On a value result the queued count shrank by one and the former head
   *       was destroyed; otherwise unchanged.
   *
   * @complexity \c O(1).
   */
  auto try_pop() noexcept -> std::optional<T> {
    auto const h{m_head.load(std::memory_order_relaxed)};
    if (h == m_tail.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    auto value{std::move(buffer()[h])};
    std::destroy_at(buffer() + h);
    m_head.store(next(h), std::memory_order_release);
    return value;
  }
};

}  // namespace nexenne::container
