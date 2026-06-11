#pragma once

/**
 * @file
 * @brief Unordered multiset with O(1) insert and swap-pop erase.
 *
 * \c bag<T> is a contiguous, unordered collection where order is genuinely
 * irrelevant and the only operations that matter are add, remove, and iterate.
 * Erasure swaps the last element into the gap and pops, so it is \c O(1) at the
 * cost of not preserving order. Duplicates are allowed.
 *
 * Against \c std::vector it makes the swap-pop erase explicit (=erase_at=,
 * =erase_first=, =erase_all=) and drops the mid-range insert API that only makes
 * sense when order matters. Against \c std::multiset it is unordered (no element
 * comparison needed) and contiguous, so iteration is cache-friendly and
 * \c data() / \c span() plug straight into \c std::ranges algorithms. Reach for
 * it for active or dirty entity lists in an update loop, pending work where
 * order is not part of the contract, and membership bookkeeping that tolerates
 * duplicates. Every operation is \c noexcept and \c constexpr; allocation
 * failure terminates. It holds only a \c std::vector, so the rule of zero
 * applies.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Unordered, contiguous multiset with swap-pop erase.
 *
 * @tparam T Element type; must be move-constructible.
 *
 * @pre None.
 * @post A default-constructed bag is empty.
 */
template <std::move_constructible T>
class bag {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;
  using iterator = typename std::vector<T>::iterator;
  using const_iterator = typename std::vector<T>::const_iterator;
  using reverse_iterator = typename std::vector<T>::reverse_iterator;
  using const_reverse_iterator = typename std::vector<T>::const_reverse_iterator;

private:
  std::vector<T> m_data;

  // Removes the element at index (assumed in range) by moving the last element
  // into its slot and popping. O(1); does not preserve order.
  constexpr auto swap_pop(size_type const index) noexcept -> void {
    auto const last{m_data.size() - 1};
    if (index != last) {
      m_data[index] = std::move(m_data[last]);
    }
    m_data.pop_back();
  }

public:
  /**
   * @brief Constructs an empty bag.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr bag() noexcept = default;

  /**
   * @brief Constructs from an initializer list.
   *
   * @param init Elements to copy in.
   *
   * @pre None.
   * @post \c size() equals \c init.size().
   */
  constexpr bag(std::initializer_list<T> const init) noexcept : m_data{init} {}

  /**
   * @brief Constructs from the range \c [first, last).
   *
   * @tparam It Input iterator type.
   * @param first Iterator to the first source element.
   * @param last Iterator one past the last source element.
   *
   * @pre \c [first, last) is a valid range.
   * @post \c size() equals \c std::distance(first, last).
   */
  template <std::input_iterator It>
  constexpr bag(It first, It last) noexcept : m_data(first, last) {}

  // Rule of zero: copy, move, and destructor are defaulted.

  /**
   * @brief Number of elements.
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
   * @brief Reports whether the bag holds no elements.
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
   * @brief Current storage capacity.
   *
   * @return Elements that fit without reallocating.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_data.capacity();
  }

  /**
   * @brief The largest number of elements the bag can hold.
   *
   * @return The maximum element count.
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
   * @post \c capacity() is at least \p n.
   */
  constexpr auto reserve(size_type const n) noexcept -> void {
    m_data.reserve(n);
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
   * @brief Releases unused capacity.
   *
   * @pre None.
   * @post Element values are unchanged.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_data.shrink_to_fit();
  }

  /**
   * @brief Swaps contents with \p other in constant time.
   *
   * @param other Bag to exchange state with.
   *
   * @pre None.
   * @post This bag holds \p other's former elements and vice versa.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(bag& other) noexcept -> void {
    m_data.swap(other.m_data);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First bag.
   * @param b Second bag.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(bag& a, bag& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Replaces the contents with the elements of \p init.
   *
   * @param init Elements to copy in.
   *
   * @pre None.
   * @post \c size() equals \c init.size(); prior contents are discarded.
   */
  constexpr auto assign(std::initializer_list<T> const init) noexcept -> void {
    m_data.assign(init);
  }

  /**
   * @brief Replaces the contents with \p count copies of \p value.
   *
   * @param count Number of copies to store.
   * @param value Value to replicate.
   *
   * @pre None.
   * @post \c size() equals \p count and every element equals \p value.
   */
  constexpr auto assign(size_type const count, T const& value) noexcept -> void {
    m_data.assign(count, value);
  }

  /**
   * @brief Replaces the contents with the range \c [first, last).
   *
   * @tparam It Input iterator type.
   * @param first Iterator to the first source element.
   * @param last Iterator one past the last source element.
   *
   * @pre \c [first, last) is a valid range.
   * @post \c size() equals \c std::distance(first, last); prior contents are
   *       discarded.
   */
  template <std::input_iterator It>
  constexpr auto assign(It const first, It const last) noexcept -> void {
    m_data.assign(first, last);
  }

  /**
   * @brief Adds a copy of \p value; order is not preserved.
   *
   * @param value Value to copy in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value is present.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(T const& value) noexcept -> void {
    m_data.push_back(value);
  }

  /**
   * @brief Adds \p value by moving it; order is not preserved.
   *
   * @param value Value to move in.
   *
   * @pre None.
   * @post \c size() grew by one and \p value is present.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(T&& value) noexcept -> void {
    m_data.push_back(std::move(value));
  }

  /**
   * @brief Constructs an element in place.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return Reference to the new element, valid until the next reallocation.
   *
   * @pre None.
   * @post \c size() grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> T& {
    return m_data.emplace_back(std::forward<Args>(args)...);
  }

  /**
   * @brief Removes the element at \p index via swap-pop.
   *
   * @param index Index of the element to remove.
   *
   * @return Nothing on success, or \c container_error::out_of_range when
   *         \p index is past the end.
   *
   * @pre None.
   * @post On success \c size() shrank by one and the former last element fills
   *       the gap; on failure the bag is unchanged.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase_at(size_type const index) noexcept -> result<void> {
    if (index >= m_data.size()) {
      return std::unexpected{container_error::out_of_range};
    }
    swap_pop(index);
    return {};
  }

  /**
   * @brief Removes the first element equal to \p value via swap-pop.
   *
   * @param value Value to find and remove.
   *
   * @return \c true on a removal, \c false when \p value was absent.
   *
   * @pre None.
   * @post On \c true the size shrank by one; otherwise the bag is unchanged.
   *
   * @complexity \c O(size).
   */
  constexpr auto erase_first(T const& value) noexcept -> bool
    requires std::equality_comparable<T>
  {
    auto const found{std::find(m_data.begin(), m_data.end(), value)};
    if (found == m_data.end()) {
      return false;
    }
    swap_pop(static_cast<size_type>(found - m_data.begin()));
    return true;
  }

  /**
   * @brief Removes every element equal to \p value.
   *
   * Walks from the back so each swap-pop is \c O(1).
   *
   * @param value Value to remove every occurrence of.
   *
   * @return The number of elements removed.
   *
   * @pre None.
   * @post No element equal to \p value remains; \c size() shrank by the return
   *       value.
   *
   * @complexity \c O(size).
   */
  constexpr auto erase_all(T const& value) noexcept -> size_type
    requires std::equality_comparable<T>
  {
    size_type removed{0};
    size_type i{m_data.size()};
    while (i > 0) {
      --i;
      if (m_data[i] == value) {
        swap_pop(i);
        ++removed;
      }
    }
    return removed;
  }

  /**
   * @brief Unchecked access by index.
   *
   * @param index Index of the element.
   *
   * @return Reference to the element at \p index.
   *
   * @pre \p index is less than \c size().
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const index) noexcept -> T& {
    return m_data[index];
  }

  /**
   * @brief Unchecked access by index (const overload).
   *
   * @param index Index of the element.
   *
   * @return Const reference to the element at \p index.
   *
   * @pre \p index is less than \c size().
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](size_type const index) const noexcept -> T const& {
    return m_data[index];
  }

  /**
   * @brief Pointer to the contiguous storage.
   *
   * @return Pointer to the first element.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto data() noexcept -> T* {
    return m_data.data();
  }

  /// @copydoc data()
  [[nodiscard]] constexpr auto data() const noexcept -> T const* {
    return m_data.data();
  }

  /**
   * @brief A \c std::span view over the elements.
   *
   * @return A span covering every element.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto span() noexcept -> std::span<T> {
    return std::span<T>{m_data.data(), m_data.size()};
  }

  /// @copydoc span()
  [[nodiscard]] constexpr auto span() const noexcept -> std::span<T const> {
    return std::span<T const>{m_data.data(), m_data.size()};
  }

  // No contains(value): it would just wrap std::ranges::contains, which works
  // on the bag directly through the iterators below.

  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return m_data.begin();
  }

  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return m_data.end();
  }

  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return m_data.begin();
  }

  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return m_data.end();
  }

  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return m_data.cbegin();
  }

  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return m_data.cend();
  }

  [[nodiscard]] constexpr auto rbegin() noexcept -> reverse_iterator {
    return m_data.rbegin();
  }

  [[nodiscard]] constexpr auto rend() noexcept -> reverse_iterator {
    return m_data.rend();
  }

  [[nodiscard]] constexpr auto rbegin() const noexcept -> const_reverse_iterator {
    return m_data.rbegin();
  }

  [[nodiscard]] constexpr auto rend() const noexcept -> const_reverse_iterator {
    return m_data.rend();
  }

  [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator {
    return m_data.crbegin();
  }

  [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator {
    return m_data.crend();
  }

  // No operator== or operator<=>: the bag promises nothing about element order
  // (erase_at uses swap-pop), so a sequence comparison would reflect operation
  // history rather than logical contents. Callers that need multiset equality
  // should compare sorted or hashed copies themselves.
};

/// @cond INTERNAL
template <std::input_iterator It>
bag(It, It) -> bag<typename std::iterator_traits<It>::value_type>;
/// @endcond

}  // namespace nexenne::container
