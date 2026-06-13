#pragma once

/**
 * @file
 * @brief Bounded multi-producer multi-consumer lock-free queue.
 *
 * \c mpmc_queue<T, N> is the canonical Vyukov bounded MPMC ring: any number of
 * threads may \c push concurrently and any number may \c pop concurrently, with
 * no locks. Each slot carries an atomic sequence number that producers and
 * consumers handshake on. A producer CASes the shared enqueue counter from
 * \c pos to \c pos+1 when the slot's sequence equals \c pos, writes the value, and
 * publishes \c sequence = pos+1 (release). A consumer CASes the dequeue counter
 * the same way when the slot's sequence equals \c pos+1, takes the value, and
 * publishes \c sequence = pos+N (release), reopening the slot one lap later.
 *
 * It is lock-free and effectively wait-free per slot (one CAS unless a competing
 * thread wins the race), bounded at exactly \p N (a full push and an empty pop
 * fail fast; spin or yield in caller code for blocking), and cache friendly (the
 * two counters live on separate lines, each slot's sequence shares a line with
 * its data). Prefer \c spsc_queue for one-to-one and \c mpsc_queue for
 * many-to-one, which avoid CASes the general case needs. \p N must be a power of
 * two. Every operation is \c noexcept; there is no allocation after construction.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count; a power of two and at least two.
 */

#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Bounded multi-producer multi-consumer lock-free queue (Vyukov).
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count; a power of two and at least two.
 *
 * @pre None.
 * @post A default-constructed queue is empty.
 */
template <std::move_constructible T, std::size_t N>
  requires(N >= 2 && std::has_single_bit(N))
class mpmc_queue {
public:
  using value_type = T;
  using size_type = std::size_t;

  static constexpr size_type capacity_value = N;

private:
  struct slot {
    std::atomic<std::size_t> sequence{};
    alignas(T) std::byte storage[sizeof(T)]{};

    [[nodiscard]] auto ptr() noexcept -> T* {
      return reinterpret_cast<T*>(storage);
    }

    [[nodiscard]] auto ptr() const noexcept -> T const* {
      return reinterpret_cast<T const*>(storage);
    }
  };

  // Each counter on its own cache line so producers and consumers do not
  // ping-pong each other's state.
  alignas(64) std::atomic<std::size_t> m_enqueue_pos{0};
  alignas(64) std::atomic<std::size_t> m_dequeue_pos{0};
  alignas(64) slot m_slots[N]{};

public:
  /**
   * @brief Constructs an empty queue with each slot sequence seeded to its index.
   *
   * @pre None.
   * @post \c empty_approx() is \c true.
   */
  mpmc_queue() noexcept {
    // Seed: sequence == index is "ready for a producer". After a push it becomes
    // pos+1 ("ready for a consumer"); after a pop, pos+N ("ready for the next
    // producer that wraps into this slot").
    for (std::size_t i{0}; i < N; ++i) {
      m_slots[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  // Destroys any elements still queued. The contract requires single-threaded
  // teardown, so no producer or consumer can be running here.
  ~mpmc_queue() noexcept {
    auto pos{m_dequeue_pos.load(std::memory_order_relaxed)};
    auto const end{m_enqueue_pos.load(std::memory_order_relaxed)};
    while (pos != end) {
      std::destroy_at(m_slots[pos & (N - 1)].ptr());
      ++pos;
    }
  }

  mpmc_queue(mpmc_queue const&) = delete;
  auto operator=(mpmc_queue const&) -> mpmc_queue& = delete;
  mpmc_queue(mpmc_queue&&) = delete;
  auto operator=(mpmc_queue&&) -> mpmc_queue& = delete;

  /**
   * @brief Slot count, the maximum number of queued elements.
   *
   * @return The template parameter \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> size_type {
    return N;
  }

  /**
   * @brief Largest number of elements the queue can ever hold.
   *
   * @return The template parameter \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return N;
  }

  /**
   * @brief Pushes a copy of \p value. Safe from any number of producers.
   *
   * @param value Element to copy in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None. Concurrent producers and consumers are supported.
   * @post On success a slot holds the element, published to consumers; on failure
   *       unchanged.
   *
   * @complexity \c O(1) plus CAS retries under contention.
   */
  auto push(T const& value) noexcept -> std::expected<void, container_error> {
    return emplace(value);
  }

  /**
   * @brief Pushes \p value by moving it. Safe from any number of producers.
   *
   * @param value Element to move in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None. Concurrent producers and consumers are supported.
   * @post On success a slot holds the element, published to consumers; on failure
   *       unchanged.
   *
   * @complexity \c O(1) plus CAS retries under contention.
   */
  auto push(T&& value) noexcept -> std::expected<void, container_error> {
    return emplace(std::move(value));
  }

  /**
   * @brief Constructs an element in place. Safe from any number of producers.
   *
   * @tparam Args Constructor argument types for \p T.
   * @param args Arguments forwarded to \p T's constructor (consumed only on a
   *             successful reservation).
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None. Concurrent producers and consumers are supported.
   * @post On success a slot holds the element, published to consumers; on failure
   *       unchanged.
   *
   * @complexity \c O(1) plus CAS retries under contention.
   */
  template <typename... Args>
  auto emplace(Args&&... args) noexcept -> std::expected<void, container_error> {
    auto pos{m_enqueue_pos.load(std::memory_order_relaxed)};
    while (true) {
      auto& s{m_slots[pos & (N - 1)]};
      auto const seq{s.sequence.load(std::memory_order_acquire)};
      auto const diff{static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos)};
      if (diff == 0) {
        if (m_enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          std::construct_at(s.ptr(), std::forward<Args>(args)...);
          s.sequence.store(pos + 1, std::memory_order_release);
          return {};
        }
        // CAS lost the race; compare_exchange refreshed pos, loop again.
      } else if (diff < 0) {
        return std::unexpected{container_error::full};
      } else {
        // Another producer advanced the counter; refresh and retry.
        pos = m_enqueue_pos.load(std::memory_order_relaxed);
      }
    }
  }

  /**
   * @brief Pops the front element. Safe from any number of consumers.
   *
   * @return The popped value, or \c container_error::empty when the queue has no
   *         items.
   *
   * @pre None. Concurrent producers and consumers are supported.
   * @post On success the front was removed and destroyed and the slot recycled;
   *       on failure unchanged.
   *
   * @complexity \c O(1) plus CAS retries under contention.
   */
  auto pop() noexcept -> std::expected<T, container_error> {
    auto pos{m_dequeue_pos.load(std::memory_order_relaxed)};
    while (true) {
      auto& s{m_slots[pos & (N - 1)]};
      auto const seq{s.sequence.load(std::memory_order_acquire)};
      auto const diff{static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1)};
      if (diff == 0) {
        if (m_dequeue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          auto value{std::move(*s.ptr())};
          std::destroy_at(s.ptr());
          s.sequence.store(pos + N, std::memory_order_release);
          return value;
        }
        // CAS lost the race; compare_exchange refreshed pos, loop again.
      } else if (diff < 0) {
        return std::unexpected{container_error::empty};
      } else {
        // Another consumer advanced the counter; refresh and retry.
        pos = m_dequeue_pos.load(std::memory_order_relaxed);
      }
    }
  }

  /**
   * @brief Non-blocking pop returning \c std::nullopt on empty.
   *
   * @return The popped value, or \c std::nullopt when the queue is empty.
   *
   * @pre None. Concurrent producers and consumers are supported.
   * @post On a value result the front was removed and destroyed; otherwise
   *       unchanged.
   *
   * @complexity \c O(1) plus CAS retries under contention.
   */
  [[nodiscard]] auto try_pop() noexcept -> std::optional<T> {
    auto r{pop()};
    if (r.has_value()) {
      return std::optional<T>{std::move(*r)};
    }
    return std::nullopt;
  }

  /**
   * @brief Approximate live element count.
   *
   * @return Best-effort count of queued elements; may be stale under concurrent
   *         mutation.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] auto size_approx() const noexcept -> size_type {
    auto const head{m_enqueue_pos.load(std::memory_order_relaxed)};
    auto const tail{m_dequeue_pos.load(std::memory_order_relaxed)};
    return head >= tail ? (head - tail) : 0;
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
    return size_approx() == 0;
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
    return size_approx() >= N;
  }
};

}  // namespace nexenne::container
