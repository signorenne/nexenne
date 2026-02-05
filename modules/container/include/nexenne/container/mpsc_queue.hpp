#pragma once

/**
 * @file
 * @brief Lock-free multi-producer / single-consumer bounded ring queue.
 *
 * \c mpsc_queue<T, N> is a fixed-capacity FIFO for many producer threads and
 * exactly one consumer. It is the Vyukov bounded queue trimmed to single
 * consumer: each slot carries an atomic sequence number, producers reserve a slot
 * by a relaxed CAS on the shared tail and publish the value with a release store
 * on the slot's sequence, and the lone consumer reads the sequence at the head
 * (acquire), takes the value, and frees the slot.
 *
 * Contract: any number of threads may call \c push / \c emplace concurrently (the
 * producers); exactly one thread may call \c pop / \c try_pop (the consumer).
 * Calling \c pop from more than one thread breaks the contract; use \c mpmc_queue
 * for many consumers. Producers never block each other (a CAS reservation) nor
 * the consumer, though heavy contention can spin the producer CAS. \p N must be a
 * power of two (the index wraps with a mask) and capacity is exactly \p N. Every
 * operation is \c noexcept; there is no allocation.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count; must be a power of two.
 */

#include <array>
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
 * @brief Lock-free multi-producer / single-consumer bounded ring queue.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count; at least two and a power of two.
 *
 * @pre None.
 * @post A default-constructed queue is empty.
 */
template <std::move_constructible T, std::size_t N>
  requires(N >= 2) && ((N & (N - 1)) == 0)
class mpsc_queue {
public:
  using value_type = T;
  using size_type = std::size_t;

  static constexpr size_type capacity_value = N;

private:
  struct slot {
    std::atomic<size_type> seq{0};
    alignas(T) std::byte storage[sizeof(T)]{};

    [[nodiscard]] auto value() noexcept -> T* {
      return reinterpret_cast<T*>(storage);
    }
  };

  static constexpr std::size_t cache_line = 64;

  alignas(cache_line) std::array<slot, N> m_slots{};
  alignas(cache_line) std::atomic<size_type> m_tail{0};  // shared by producers
  alignas(cache_line) std::atomic<size_type> m_head{0};  // consumer only

  static constexpr auto mask{N - 1};

public:
  /**
   * @brief Constructs an empty queue with each slot sequence seeded to its index.
   *
   * @pre None.
   * @post \c empty_approx() is \c true.
   */
  constexpr mpsc_queue() noexcept {
    for (size_type i{0}; i < N; ++i) {
      m_slots[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  mpsc_queue(mpsc_queue const&) = delete;
  auto operator=(mpsc_queue const&) -> mpsc_queue& = delete;
  mpsc_queue(mpsc_queue&&) = delete;
  auto operator=(mpsc_queue&&) -> mpsc_queue& = delete;

  ~mpsc_queue() noexcept {
    // Single-threaded at destruction: drain so element destructors run.
    while (try_pop().has_value()) {}
  }

  /**
   * @brief Slot count, the maximum number of queued elements.
   *
   * @return The template parameter \p N.
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
   * @return The template parameter \p N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return capacity_value;
  }

  /**
   * @brief Approximate live element count, safe from any thread.
   *
   * @return Best-effort count of queued elements; may already be stale.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] auto size_approx() const noexcept -> size_type {
    auto const h{m_head.load(std::memory_order_acquire)};
    auto const t{m_tail.load(std::memory_order_acquire)};
    return t >= h ? t - h : 0;
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
   * @brief Pushes a copy of \p value. Safe from any number of producer threads.
   *
   * @param value Element to copy in.
   *
   * @return Nothing on success, or \c container_error::full when at capacity.
   *
   * @pre None. Concurrent producers are supported.
   * @post On success a reserved slot holds the element, published to the
   *       consumer; on failure unchanged.
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
   * @pre None. Concurrent producers are supported.
   * @post On success a reserved slot holds the element, published to the
   *       consumer; on failure unchanged.
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
   * @pre None. Concurrent producers are supported.
   * @post On success a reserved slot holds the element, published to the
   *       consumer; on failure unchanged.
   *
   * @complexity \c O(1) plus CAS retries under contention.
   */
  template <typename... Args>
  auto emplace(Args&&... args) noexcept -> std::expected<void, container_error> {
    auto pos{m_tail.load(std::memory_order_relaxed)};
    while (true) {
      auto& s{m_slots[pos & mask]};
      auto const seq{s.seq.load(std::memory_order_acquire)};
      auto const diff{static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos)};
      if (diff == 0) {
        if (m_tail.compare_exchange_weak(
              pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed
            )) {
          std::construct_at(s.value(), std::forward<Args>(args)...);
          s.seq.store(pos + 1, std::memory_order_release);
          return {};
        }
        // CAS lost the race; retry with the refreshed pos.
      } else if (diff < 0) {
        return std::unexpected{container_error::full};
      } else {
        // Slot's sequence is ahead: another producer is mid-write here; refresh.
        pos = m_tail.load(std::memory_order_relaxed);
      }
    }
  }

  /**
   * @brief Pops the head element. Consumer thread only.
   *
   * @return The dequeued element, or \c container_error::empty when none is
   *         ready.
   *
   * @pre Called from the single consumer thread only.
   * @post On success the head was removed and destroyed and the slot recycled; on
   *       failure unchanged.
   *
   * @complexity \c O(1).
   */
  auto pop() noexcept -> std::expected<T, container_error> {
    auto const pos{m_head.load(std::memory_order_relaxed)};
    auto& s{m_slots[pos & mask]};
    auto const seq{s.seq.load(std::memory_order_acquire)};
    auto const diff{static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1)};
    if (diff < 0) {
      return std::unexpected{container_error::empty};
    }
    auto value{std::move(*s.value())};
    std::destroy_at(s.value());
    s.seq.store(pos + N, std::memory_order_release);
    m_head.store(pos + 1, std::memory_order_relaxed);
    return value;
  }

  /**
   * @brief Optional-returning pop. Consumer thread only.
   *
   * @return The dequeued element, or \c std::nullopt when empty.
   *
   * @pre Called from the single consumer thread only.
   * @post On a value result the head was removed and destroyed; otherwise
   *       unchanged.
   *
   * @complexity \c O(1).
   */
  auto try_pop() noexcept -> std::optional<T> {
    auto const pos{m_head.load(std::memory_order_relaxed)};
    auto& s{m_slots[pos & mask]};
    auto const seq{s.seq.load(std::memory_order_acquire)};
    auto const diff{static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1)};
    if (diff < 0) {
      return std::nullopt;
    }
    auto value{std::move(*s.value())};
    std::destroy_at(s.value());
    s.seq.store(pos + N, std::memory_order_release);
    m_head.store(pos + 1, std::memory_order_relaxed);
    return value;
  }
};

}  // namespace nexenne::container
