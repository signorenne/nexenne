#pragma once

/**
 * @file
 * @brief Priority queue with O(log n) update- and erase-by-handle.
 *
 * \c indexed_priority_queue<T, Compare> is the heap \c std::priority_queue should
 * have been: every \c push returns a stable opaque handle the caller can later
 * use to read the value back (\c value_at), change its priority and re-heapify in
 * \c O(log n) (\c update), or remove it in \c O(log n) (\c erase). The classic
 * "heap plus position index" pattern backs it: the heap stores \c (value, handle)
 * entries and a parallel vector maps each handle to its current heap position,
 * kept in sync by every sift, so the position lookup is \c O(1).
 *
 * As with \c heap, \p Compare is a strict weak ordering: the default
 * \c std::less<T> gives a max-heap (largest on top), and \c std::greater<T> gives
 * a min-heap (the usual choice for event schedulers and Dijkstra/A* frontiers).
 * Reach for it when you must update or cancel queued items by identity rather
 * than rescan to find them. Handles are recycled, so a handle is meaningful only
 * while its element is live; see the note on \c push. Every operation is
 * \c noexcept; allocation failure terminates.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Binary-heap priority queue with handle-based update and erase.
 *
 * @tparam T Value type; must be move-constructible.
 * @tparam Compare Strict weak ordering over \p T; \c std::less<T> (a max-heap) by
 *         default.
 *
 * @pre None.
 * @post A default-constructed queue is empty.
 */
template <std::move_constructible T, typename Compare = std::less<T>>
  requires std::strict_weak_order<Compare const&, T const&, T const&>
class indexed_priority_queue {
public:
  using value_type = T;
  using size_type = std::size_t;
  using key_compare = Compare;
  using handle_type = std::uint32_t;

  /// A heap slot: the value and the handle that currently owns it.
  struct entry {
    T value;
    handle_type handle{};
  };

  /**
   * @brief Sentinel a caller can use to mean "no handle".
   *
   * Never returned by \c push, so a handle variable initialised to this compares
   * unequal to every live handle. (Internally the queue marks free index slots
   * with a separate position tombstone, not this value.)
   */
  static constexpr handle_type invalid_handle = handle_type{} - 1;

private:
  std::vector<entry> m_heap;
  std::vector<size_type> m_position;     // handle -> index into m_heap
  std::vector<handle_type> m_free_list;  // recycled handles
  Compare m_cmp{};

  static constexpr size_type tombstone = size_type{} - 1;

  constexpr auto allocate_handle() noexcept -> handle_type {
    if (!m_free_list.empty()) {
      auto const h{m_free_list.back()};
      m_free_list.pop_back();
      return h;
    }
    auto const h{static_cast<handle_type>(m_position.size())};
    m_position.push_back(0);
    return h;
  }

  constexpr auto release_handle(handle_type const h) noexcept -> void {
    m_position[h] = tombstone;
    m_free_list.push_back(h);
  }

  [[nodiscard]] constexpr auto
  cmp_heap(size_type const i, size_type const j) const noexcept -> bool {
    return m_cmp(m_heap[i].value, m_heap[j].value);
  }

  constexpr auto swap_nodes(size_type const i, size_type const j) noexcept -> void {
    using std::swap;
    swap(m_heap[i], m_heap[j]);
    m_position[m_heap[i].handle] = i;
    m_position[m_heap[j].handle] = j;
  }

  constexpr auto sift_up(size_type i) noexcept -> void {
    while (i > 0) {
      auto const parent{(i - 1) / 2};
      if (cmp_heap(parent, i)) {
        swap_nodes(parent, i);
        i = parent;
      } else {
        return;
      }
    }
  }

  constexpr auto sift_down(size_type i) noexcept -> void {
    auto const n{m_heap.size()};
    while (true) {
      auto const left{2 * i + 1};
      auto const right{2 * i + 2};
      auto best{i};
      if (left < n && cmp_heap(best, left)) {
        best = left;
      }
      if (right < n && cmp_heap(best, right)) {
        best = right;
      }
      if (best == i) {
        return;
      }
      swap_nodes(i, best);
      i = best;
    }
  }

  [[nodiscard]] constexpr auto valid_handle(handle_type const h) const noexcept -> bool {
    return h < m_position.size() && m_position[h] != tombstone;
  }

public:
  /**
   * @brief Constructs an empty queue with a default-constructed comparator.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr indexed_priority_queue() noexcept = default;

  /**
   * @brief Constructs an empty queue using \p cmp for ordering.
   *
   * @param cmp Strict-weak ordering comparator to store.
   *
   * @pre None.
   * @post \c empty() is \c true and the stored comparator is \p cmp.
   */
  explicit constexpr indexed_priority_queue(Compare cmp) noexcept : m_cmp{std::move(cmp)} {}

  /**
   * @brief Number of live elements.
   *
   * @return Element count.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_heap.size();
  }

  /**
   * @brief Whether the queue holds no elements.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_heap.empty();
  }

  /**
   * @brief Number of elements that fit without reallocating.
   *
   * @return Allocated heap capacity.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_heap.capacity();
  }

  /**
   * @brief Largest number of elements the queue can ever hold.
   *
   * @return The maximum size of the backing heap vector.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_heap.max_size();
  }

  /**
   * @brief Releases unused capacity across all internal vectors.
   *
   * @pre None.
   * @post \c size() is unchanged; capacity may shrink. Handles remain valid.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_heap.shrink_to_fit();
    m_position.shrink_to_fit();
    m_free_list.shrink_to_fit();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Queue to exchange state with.
   *
   * @pre None.
   * @post This queue holds \p other's former elements and vice versa; handles
   *       stay valid against their original queue's new owner.
   */
  constexpr auto swap(indexed_priority_queue& other) noexcept -> void {
    using std::swap;
    m_heap.swap(other.m_heap);
    m_position.swap(other.m_position);
    m_free_list.swap(other.m_free_list);
    swap(m_cmp, other.m_cmp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First queue.
   * @param b Second queue.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto
  swap(indexed_priority_queue& a, indexed_priority_queue& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum capacity to reserve.
   *
   * @pre None.
   * @post Capacity is at least \p n; \c size() is unchanged.
   */
  constexpr auto reserve(size_type const n) noexcept -> void {
    m_heap.reserve(n);
    m_position.reserve(n);
  }

  /**
   * @brief Removes every element and invalidates all handles.
   *
   * @pre None.
   * @post \c empty() is \c true and every previously-issued handle is invalid.
   *       Capacity is retained.
   */
  constexpr auto clear() noexcept -> void {
    m_heap.clear();
    m_position.clear();
    m_free_list.clear();
  }

  /**
   * @brief Inserts \p value and returns a stable handle for it.
   *
   * The handle stays valid across other pushes, pops, updates, and erases, until
   * this element is itself popped or erased. After that the handle is invalid;
   * since handles are recycled, a later \c push may reissue it for a different
   * element, so do not query a handle past its element's removal.
   *
   * @param value Value to insert, moved into the queue.
   *
   * @return Handle that uniquely identifies the inserted element.
   *
   * @pre None.
   * @post \c size() grew by one, the heap invariant holds, and the handle refers
   *       to \p value. Existing handles stay valid.
   *
   * @complexity \c O(log n).
   */
  constexpr auto push(T value) noexcept -> handle_type {
    auto const h{allocate_handle()};
    auto const pos{m_heap.size()};
    m_heap.push_back(entry{std::move(value), h});
    m_position[h] = pos;
    sift_up(pos);
    return h;
  }

  /**
   * @brief Constructs an element in place and returns its handle.
   *
   * @tparam Args Constructor argument types for \p T.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Handle that uniquely identifies the new element.
   *
   * @pre None.
   * @post \c size() grew by one, the heap invariant holds, and the handle refers
   *       to the new element.
   *
   * @complexity \c O(log n).
   */
  template <typename... Args>
  constexpr auto emplace(Args&&... args) noexcept -> handle_type {
    auto const h{allocate_handle()};
    auto const pos{m_heap.size()};
    m_heap.push_back(entry{T(std::forward<Args>(args)...), h});
    m_position[h] = pos;
    sift_up(pos);
    return h;
  }

  /**
   * @brief Removes and returns the top element.
   *
   * @return The previous top on success, or \c container_error::empty when no
   *         elements remain.
   *
   * @pre None.
   * @post On success \c size() shrank by one, the popped element's handle is
   *       invalidated, and the heap invariant holds; on failure the queue is
   *       unchanged.
   *
   * @complexity \c O(log n).
   */
  constexpr auto pop() noexcept -> std::expected<T, container_error> {
    if (m_heap.empty()) {
      return std::unexpected{container_error::empty};
    }
    auto value{std::move(m_heap.front().value)};
    auto const old_handle{m_heap.front().handle};
    auto const last{m_heap.size() - 1};
    if (last != 0) {
      m_heap[0] = std::move(m_heap[last]);
      m_position[m_heap[0].handle] = 0;
    }
    m_heap.pop_back();
    release_handle(old_handle);
    if (!m_heap.empty()) {
      sift_down(0);
    }
    return value;
  }

  /**
   * @brief Replaces the value behind \p h and re-heapifies.
   *
   * @param h Handle of the element to update.
   * @param value New value to store, moved in.
   *
   * @return Nothing on success, or \c container_error::not_found when \p h is
   *         invalid (popped, erased, or never issued).
   *
   * @pre None.
   * @post On success the element behind \p h holds \p value and the heap
   *       invariant holds; on failure the queue is unchanged. \p h stays valid.
   *
   * @complexity \c O(log n).
   */
  constexpr auto
  update(handle_type const h, T value) noexcept -> std::expected<void, container_error> {
    if (!valid_handle(h)) {
      return std::unexpected{container_error::not_found};
    }
    auto const pos{m_position[h]};
    m_heap[pos].value = std::move(value);
    sift_up(pos);
    sift_down(pos);
    return {};
  }

  /**
   * @brief Erases the element behind \p h in O(log n).
   *
   * @param h Handle of the element to erase.
   *
   * @return Nothing on success, or \c container_error::not_found when \p h is
   *         invalid.
   *
   * @pre None.
   * @post On success \c size() shrank by one, \p h is invalidated, and the heap
   *       invariant holds; on failure the queue is unchanged.
   *
   * @complexity \c O(log n).
   */
  constexpr auto erase(handle_type const h) noexcept -> std::expected<void, container_error> {
    if (!valid_handle(h)) {
      return std::unexpected{container_error::not_found};
    }
    auto const pos{m_position[h]};
    auto const last{m_heap.size() - 1};
    if (pos != last) {
      m_heap[pos] = std::move(m_heap[last]);
      m_position[m_heap[pos].handle] = pos;
    }
    m_heap.pop_back();
    release_handle(h);
    if (pos < m_heap.size()) {
      sift_up(pos);
      sift_down(pos);
    }
    return {};
  }

  /**
   * @brief Pointer to the top element, or \c nullptr when empty.
   *
   * @return Pointer to the top value, or \c nullptr when empty.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] constexpr auto top() const noexcept -> T const* {
    return m_heap.empty() ? nullptr : std::addressof(m_heap.front().value);
  }

  /**
   * @brief Handle of the top element.
   *
   * @return The top handle on success, or \c container_error::empty when no
   *         elements remain.
   *
   * @pre None.
   * @post None. The queue is not modified.
   */
  [[nodiscard]] constexpr auto
  top_handle() const noexcept -> std::expected<handle_type, container_error> {
    if (m_heap.empty()) {
      return std::unexpected{container_error::empty};
    }
    return m_heap.front().handle;
  }

  /**
   * @brief Pointer to the value behind \p h.
   *
   * @param h Handle to look up.
   *
   * @return Pointer to the value on success, or \c container_error::not_found
   *         when \p h is invalid.
   *
   * @pre None.
   * @post None. The queue is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto value_at(handle_type const h
  ) const noexcept -> std::expected<T const*, container_error> {
    if (!valid_handle(h)) {
      return std::unexpected{container_error::not_found};
    }
    return std::addressof(m_heap[m_position[h]].value);
  }

  /**
   * @brief Whether \p h refers to a live element.
   *
   * @param h Handle to test.
   *
   * @return \c true when \p h is live.
   *
   * @pre None.
   * @post None. The queue is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto contains(handle_type const h) const noexcept -> bool {
    return valid_handle(h);
  }

  /**
   * @brief Read-only view of the backing heap layout, for diagnostics.
   *
   * Pairs values with handles in raw heap order, which is neither priority nor
   * sorted order; for normal use prefer \c pop / \c top / \c value_at.
   *
   * @return Span over the heap-ordered entries.
   *
   * @pre None.
   * @post None. The queue is not modified; the span is invalidated by any
   *       mutation.
   */
  [[nodiscard]] constexpr auto entries() const noexcept -> std::span<entry const> {
    return std::span<entry const>{m_heap.data(), m_heap.size()};
  }
};

}  // namespace nexenne::container
