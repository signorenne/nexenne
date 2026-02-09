#pragma once

/**
 * @file
 * @brief Binary heap (priority queue) over a contiguous std::vector.
 *
 * \c heap<T, Compare> is a priority structure built on \c std::vector and the
 * standard heap algorithms (\c std::push_heap, \c std::pop_heap,
 * \c std::make_heap). \p Compare is a strict weak ordering, exactly as
 * \c std::priority_queue expects: the default \c std::less<T> gives a max-heap
 * (the largest element on top), and \c std::greater<T> gives a min-heap.
 *
 * Reusing the standard algorithms keeps the storage a contiguous,
 * algorithm-friendly range and avoids a hand-written sift loop. Compared with
 * \c std::priority_queue it adds read-only inspection of the backing storage via
 * \c data() / \c span() (in heap order, not sorted order) and returns \c result
 * from fallible operations instead of risking undefined behaviour. It
 * deliberately offers no \c begin / \c end and no \c operator== / \c <=>:
 * heap-layout order is neither priority nor sorted order, so iterating or
 * comparing in it misleads; pop, or copy from \c span() and sort the copy.
 *
 * Reach for it for event schedulers (min-heap, smallest-time event next),
 * A* / Dijkstra frontiers, and top-K selection. It follows the rule of zero
 * (a vector plus a comparator). Every operation is \c noexcept; allocation
 * failure terminates. Concurrent reads are safe, concurrent mutation is not.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Binary heap over a std::vector with a configurable ordering.
 *
 * @tparam T Element type; must be movable (the heap algorithms move-assign
 *           elements while sifting).
 * @tparam Compare Strict weak ordering; \c std::less<T> (the default) yields a
 *                 max-heap, \c std::greater<T> a min-heap.
 *
 * @pre None.
 * @post A default-constructed heap is empty.
 */
template <std::movable T, typename Compare = std::less<T>>
  requires std::strict_weak_order<Compare const&, T const&, T const&>
class heap {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T const&;
  using const_reference = T const&;
  using pointer = T const*;
  using const_pointer = T const*;
  using key_compare = Compare;

private:
  std::vector<T> m_data;
  Compare m_cmp{};

public:
  /**
   * @brief Default-constructs an empty heap.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr heap() noexcept = default;

  /**
   * @brief Constructs an empty heap with a user-supplied comparator.
   *
   * @param cmp Comparator to store.
   *
   * @pre None.
   * @post \c empty() is \c true and the stored comparator is \p cmp.
   */
  explicit constexpr heap(Compare cmp) noexcept : m_cmp{std::move(cmp)} {}

  /**
   * @brief Constructs from an initializer list, heapifying in \c O(n).
   *
   * One \c std::make_heap is strictly cheaper than \c n successive pushes
   * (linear versus \c n log n).
   *
   * @param init Elements to copy in.
   *
   * @pre None.
   * @post \c size() equals \c init.size() and the heap invariant holds.
   *
   * @complexity \c O(n).
   */
  constexpr heap(std::initializer_list<T> const init) noexcept : m_data{init} {
    std::make_heap(m_data.begin(), m_data.end(), m_cmp);
  }

  /**
   * @brief Constructs from the range \c [first, last), heapifying in \c O(n).
   *
   * @tparam It Input iterator type.
   * @param first Iterator to the first source element.
   * @param last Iterator one past the last source element.
   *
   * @pre \c [first, last) is a valid range.
   * @post \c size() equals \c std::distance(first, last) and the heap invariant
   *       holds.
   *
   * @complexity \c O(n).
   */
  template <std::input_iterator It>
  constexpr heap(It const first, It const last) noexcept : m_data(first, last) {
    std::make_heap(m_data.begin(), m_data.end(), m_cmp);
  }

  /**
   * @brief Number of elements in the heap.
   *
   * @return The element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_data.size();
  }

  /**
   * @brief Reports whether the heap holds no elements.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_data.empty();
  }

  /**
   * @brief Elements that fit without reallocating.
   *
   * @return The capacity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_data.capacity();
  }

  /**
   * @brief The largest number of elements the heap can hold.
   *
   * @return The maximum size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_data.max_size();
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum capacity to ensure.
   *
   * @pre None.
   * @post \c capacity() is at least \p n; \c size() is unchanged.
   */
  constexpr auto reserve(size_type const n) noexcept -> void {
    m_data.reserve(n);
  }

  /**
   * @brief Releases unused capacity.
   *
   * @pre None.
   * @post \c size() is unchanged.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_data.shrink_to_fit();
  }

  /**
   * @brief Removes every element; capacity is retained.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr auto clear() noexcept -> void {
    m_data.clear();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Heap to exchange state with.
   *
   * @pre None.
   * @post This heap and \p other have exchanged elements and comparators.
   */
  constexpr auto swap(heap& other) noexcept -> void {
    using std::swap;
    m_data.swap(other.m_data);
    swap(m_cmp, other.m_cmp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First heap.
   * @param b Second heap.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(heap& a, heap& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Constructs an element in place and sifts it into position.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @pre None.
   * @post \c size() grew by one and the heap invariant holds.
   *
   * @complexity \c O(log n).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> void {
    m_data.emplace_back(std::forward<Args>(args)...);
    std::push_heap(m_data.begin(), m_data.end(), m_cmp);
  }

  /**
   * @brief Pushes a copy of \p value onto the heap.
   *
   * @param value Value to copy in.
   *
   * @pre None.
   * @post \c size() grew by one and the heap invariant holds.
   *
   * @complexity \c O(log n).
   */
  constexpr auto push(T const& value) noexcept -> void {
    emplace(value);
  }

  /**
   * @brief Pushes \p value onto the heap by moving it.
   *
   * @param value Value to move in.
   *
   * @pre None.
   * @post \c size() grew by one and the heap invariant holds.
   *
   * @complexity \c O(log n).
   */
  constexpr auto push(T&& value) noexcept -> void {
    emplace(std::move(value));
  }

  /**
   * @brief Removes and returns the top element.
   *
   * @return The former top, or \c container_error::empty when the heap is
   *         empty.
   *
   * @pre None.
   * @post On success \c size() shrank by one and the heap invariant holds; on
   *       failure the heap is unchanged.
   *
   * @complexity \c O(log n).
   */
  [[nodiscard]] constexpr auto pop() noexcept -> result<T> {
    if (m_data.empty()) {
      return std::unexpected{container_error::empty};
    }
    std::pop_heap(m_data.begin(), m_data.end(), m_cmp);
    result<T> removed{std::move(m_data.back())};
    m_data.pop_back();
    return removed;
  }

  /**
   * @brief Re-establishes the heap invariant over the whole range.
   *
   * Use after bulk-modifying the storage so order is restored in one \c O(n)
   * pass rather than by popping and pushing every element.
   *
   * @pre None.
   * @post The heap invariant holds.
   *
   * @complexity \c O(n).
   */
  constexpr auto rebuild() noexcept -> void {
    std::make_heap(m_data.begin(), m_data.end(), m_cmp);
  }

  /**
   * @brief The top element (highest priority under \p Compare).
   *
   * @return A pointer to the top element, or \c nullptr when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto top() const noexcept -> T const* {
    return m_data.empty() ? nullptr : std::addressof(m_data.front());
  }

  /**
   * @brief Pointer to the backing storage, in heap (not sorted) order.
   *
   * @return A pointer to the first element.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto data() const noexcept -> T const* {
    return m_data.data();
  }

  /**
   * @brief A \c std::span over the elements, in heap (not sorted) order.
   *
   * @return A span covering every element; invalidated by any mutation.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto span() const noexcept -> std::span<T const> {
    return std::span<T const>{m_data.data(), m_data.size()};
  }

  /**
   * @brief The stored comparator.
   *
   * @return A const reference to the comparator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto key_comp() const noexcept -> Compare const& {
    return m_cmp;
  }

  // No begin/end and no operator== / operator<=>: heap-layout order is neither
  // priority nor sorted order and depends on insertion history, so iterating or
  // comparing in it misleads. Pop the elements, or copy from span() and sort
  // the copy, and compare whatever projection you actually mean.
};

/// @cond INTERNAL
template <std::input_iterator It>
heap(It, It) -> heap<typename std::iterator_traits<It>::value_type>;
/// @endcond

}  // namespace nexenne::container
