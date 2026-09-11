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
 * avoid false sharing. Each side also caches the last value it read of the
 * other's index, on its own cache line, and reloads the shared atomic only
 * when that cached value says the ring is full (producer) or empty
 * (consumer), so a steady stream does not bounce the peer's line on every
 * operation.
 *
 * The contract is strict: exactly one thread may call \c push / \c emplace (the
 * producer) and exactly one may call \c pop / \c try_pop (the consumer); the
 * approximate observers (\c size_approx, \c empty_approx, \c full_approx) are
 * best-effort from either side. Calling \c push or \c pop from more than one
 * thread breaks the contract; use \c mpsc_queue or \c mpmc_queue for that. One
 * slot is reserved as a sentinel so \c head == \c tail unambiguously means
 * empty, making the effective capacity \p N - 1. Every operation is \c noexcept
 * exactly when the element code it runs (a copy, move or construction) is;
 * there is no allocation.
 *
 * @tparam T Element type; must be move-constructible.
 * @tparam N Slot count; the effective capacity is \p N - 1.
 */

#include <array>
#include <atomic>
#include <bit>
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
  using value_type = T;           ///< Type of the queued elements.
  using size_type = std::size_t;  ///< Unsigned type for sizes and counts.

  /// @brief Maximum number of queued elements, N - 1 (one slot stays free).
  static constexpr size_type capacity_value{N - 1};

private:
  /**
   * @brief Cache-line size on x86-64 and common ARM cores.
   *
   * Hardcoded rather than \c std::hardware_destructive_interference_size, whose
   * value is an ABI-unstable constant that GCC warns against baking into a class
   * layout.
   */
  static constexpr std::size_t cache_line_size{64};

  alignas(T) std::array<std::byte, sizeof(T) * N> m_storage{};

  alignas(cache_line_size) std::atomic<size_type> m_head{0};  ///< Advanced by the consumer.
  size_type m_tail_cache{0};  ///< Consumer only: the last tail it acquired.
  alignas(cache_line_size) std::atomic<size_type> m_tail{0};  ///< Advanced by the producer.
  size_type m_head_cache{0};  ///< Producer only: the last head it acquired.

  /**
   * @brief Address of slot \p i, valid before any \c T exists there.
   *
   * Plain address arithmetic over the bytes; this is the pointer
   * \c std::construct_at needs to start an element.
   *
   * @param i Slot index.
   *
   * @return Pointer to the raw storage of slot \p i, not laundered.
   *
   * @pre \p i is less than \p N.
   * @post None.
   */
  [[nodiscard]] auto slot_address(size_type const i) noexcept -> T* {
    return reinterpret_cast<T*>(m_storage.data() + (i * sizeof(T)));
  }

  /**
   * @brief Pointer to the element living in slot \p i.
   *
   * A \c std::byte array element is not pointer-interconvertible with the \c T
   * constructed inside it, so access goes through \c std::launder.
   *
   * @param i Slot index.
   *
   * @return Laundered pointer to the element.
   *
   * @pre A \c T is alive in slot \p i.
   * @post None.
   */
  [[nodiscard]] auto element(size_type const i) noexcept -> T* {
    return std::launder(slot_address(i));
  }

  /**
   * @brief Slot index after \p i, wrapping at \p N.
   *
   * A power-of-two \p N wraps with a mask (a single AND); any other \p N uses a
   * modulo.
   *
   * @param i Current slot index.
   *
   * @return The following slot index.
   *
   * @pre \p i is less than \p N.
   * @post The result is less than \p N.
   */
  [[nodiscard]] static constexpr auto next(size_type const i) noexcept -> size_type {
    if constexpr (std::has_single_bit(N)) {
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

  /**
   * @brief Drains the queue so every remaining element is destroyed.
   *
   * @pre Neither the producer nor the consumer is running: teardown is
   *      single-threaded.
   * @post Every queued element has been destroyed.
   */
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
    // Tail is loaded relaxed: the producer that most cares about fullness already
    // owns the freshest tail, and either side only needs the best-effort answer
    // the approximate contract promises. Head is acquire to pair with the
    // consumer's release.
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
  auto push(T const& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
    -> std::expected<void, container_error> {
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
  auto push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
    -> std::expected<void, container_error> {
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
  auto emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
    -> std::expected<void, container_error> {
    auto const t{m_tail.load(std::memory_order_relaxed)};
    auto const next_t{next(t)};
    if (next_t == m_head_cache) {
      m_head_cache = m_head.load(std::memory_order_acquire);
      if (next_t == m_head_cache) {
        return std::unexpected{container_error::full};
      }
    }
    std::construct_at(slot_address(t), std::forward<Args>(args)...);
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
  [[nodiscard]] auto pop() noexcept(std::is_nothrow_move_constructible_v<T>)
    -> std::expected<T, container_error> {
    auto const h{m_head.load(std::memory_order_relaxed)};
    if (h == m_tail_cache) {
      m_tail_cache = m_tail.load(std::memory_order_acquire);
      if (h == m_tail_cache) {
        return std::unexpected{container_error::empty};
      }
    }
    auto value{std::move(*element(h))};
    std::destroy_at(element(h));
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
  [[nodiscard]] auto try_pop() noexcept(std::is_nothrow_move_constructible_v<T>)
    -> std::optional<T> {
    auto const h{m_head.load(std::memory_order_relaxed)};
    if (h == m_tail_cache) {
      m_tail_cache = m_tail.load(std::memory_order_acquire);
      if (h == m_tail_cache) {
        return std::nullopt;
      }
    }
    auto value{std::move(*element(h))};
    std::destroy_at(element(h));
    m_head.store(next(h), std::memory_order_release);
    return value;
  }
};

}  // namespace nexenne::container
