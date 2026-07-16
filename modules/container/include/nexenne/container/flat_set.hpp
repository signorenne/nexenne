#pragma once

/**
 * @file
 * @brief Sorted-vector ordered set: the std::set shape with contiguous storage.
 *
 * \c flat_set<T, Compare> keeps its elements sorted in a single \c std::vector,
 * so lookups are an \c O(log N) binary search and iteration is a flat,
 * cache-friendly walk in sorted order. Insert and erase are \c O(N) because the
 * tail shifts to keep the array contiguous and sorted. There is no per-element
 * node allocation, only the vector's, so for sets that fit in cache the hit rate
 * usually beats a node-based \c std::set despite the \c O(N) mutation.
 *
 * Reach for it for small-to-medium ordered sets that are queried far more than
 * mutated, or built once and then read. Use \c flat_hash_set when ordering does
 * not matter and you want \c O(1) average lookup. Elements are immutable in place
 * (mutating one would break the sort), so the iterator is a const iterator. Every
 * operation is \c noexcept; allocation failure terminates. The container is a
 * vector plus a comparator, so the rule of zero applies.
 */

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace nexenne::container {

/**
 * @brief Ordered set backed by a sorted std::vector.
 *
 * @tparam T Element type.
 * @tparam Compare Strict weak ordering over \p T; \c std::less<T> by default.
 *
 * @pre None.
 * @post A default-constructed set is empty.
 */
template <typename T, typename Compare = std::less<T>>
  requires std::strict_weak_order<Compare const&, T const&, T const&>
class flat_set {
public:
  using value_type = T;
  using key_type = T;
  using key_compare = Compare;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  // Elements must stay sorted, so they are never mutable in place: the iterator
  // is a const iterator.
  using iterator = typename std::vector<T>::const_iterator;
  using const_iterator = iterator;

private:
  std::vector<T> m_data;
  [[no_unique_address]] Compare m_cmp{};

public:
  /**
   * @brief Constructs an empty set with a default-constructed comparator.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr flat_set() noexcept = default;

  /**
   * @brief Constructs an empty set ordered by \p cmp.
   *
   * @param cmp Comparator to store.
   *
   * @pre None.
   * @post \c empty() is \c true and the stored comparator is \p cmp.
   */
  explicit constexpr flat_set(Compare cmp) noexcept : m_cmp{std::move(cmp)} {}

  /**
   * @brief Constructs from an initializer list, dropping duplicate keys.
   *
   * @param init Elements to insert; later duplicates are ignored.
   *
   * @pre None.
   * @post Every distinct value of \p init is present, in sorted order.
   *
   * @complexity \c O(N^2) worst case: each element is inserted with an \c O(N)
   *             tail shift, so prefer building once from a mostly-sorted list.
   */
  constexpr flat_set(std::initializer_list<T> const init) noexcept {
    m_data.reserve(init.size());
    for (auto const& value : init) {
      insert(value);
    }
  }

  /**
   * @brief Reports whether the set holds no elements.
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
   * @brief The largest number of elements the set can hold.
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
   * @post Capacity is at least \p n; iterators may be invalidated.
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
   * @param other Set to exchange state with.
   *
   * @pre None.
   * @post This set and \p other have exchanged elements and comparators.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(flat_set& other) noexcept -> void {
    using std::swap;
    m_data.swap(other.m_data);
    swap(m_cmp, other.m_cmp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First set.
   * @param b Second set.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(flat_set& a, flat_set& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Iterator to the first (smallest) element.
   *
   * @return A const iterator to the start of the sorted range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return m_data.begin();
  }

  /**
   * @brief Iterator one past the last element.
   *
   * @return A const iterator to the end of the sorted range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return m_data.end();
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return m_data.begin();
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return m_data.end();
  }

  /**
   * @brief First element not ordered before \p key (the first \c >= key).
   *
   * @param key Value to search for.
   *
   * @return A const iterator to the first element not less than \p key, or
   *         \c end().
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto lower_bound(T const& key) const noexcept -> const_iterator {
    return std::lower_bound(m_data.begin(), m_data.end(), key, m_cmp);
  }

  /**
   * @brief First element ordered after \p key (the first \c > key).
   *
   * @param key Value to search for.
   *
   * @return A const iterator to the first element greater than \p key, or
   *         \c end().
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto upper_bound(T const& key) const noexcept -> const_iterator {
    return std::upper_bound(m_data.begin(), m_data.end(), key, m_cmp);
  }

  /**
   * @brief Locates the element equal to \p key.
   *
   * @param key Value to search for.
   *
   * @return A const iterator to the matching element, or \c end() when absent.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto find(T const& key) const noexcept -> const_iterator {
    auto const pos{lower_bound(key)};
    if (pos != m_data.end() && !m_cmp(key, *pos)) {  // pos >= key and key >= pos
      return pos;
    }
    return m_data.end();
  }

  /**
   * @brief Reports whether \p key is a member.
   *
   * @param key Value to test.
   *
   * @return \c true when \p key is present.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto contains(T const& key) const noexcept -> bool {
    return find(key) != m_data.end();
  }

  /**
   * @brief Number of elements equal to \p key, always \c 0 or \c 1.
   *
   * @param key Value to count.
   *
   * @return \c 1 when \p key is present, otherwise \c 0.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto count(T const& key) const noexcept -> size_type {
    return contains(key) ? size_type{1} : size_type{0};
  }

  /**
   * @brief The pair of bounds enclosing every element equal to \p key.
   *
   * For a set the range holds at most one element, so it is empty (both
   * iterators equal) on a miss and a single element wide on a hit.
   *
   * @param key Value to search for.
   *
   * @return A pair of \c lower_bound(key) and \c upper_bound(key).
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto equal_range(T const& key) const noexcept
    -> std::pair<const_iterator, const_iterator> {
    return {lower_bound(key), upper_bound(key)};
  }

  /**
   * @brief The stored comparator.
   *
   * @return A copy of the comparator ordering the set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto key_comp() const noexcept -> key_compare {
    return m_cmp;
  }

  /**
   * @brief Heterogeneous \c lower_bound for a key-comparable type \p K.
   *
   * Enabled only when \c Compare is transparent (exposes \c is_transparent, as
   * \c std::less<> does), so a compatible lookup type (for example a
   * \c std::string_view against \c std::string elements) is searched without
   * constructing a \p T.
   *
   * @tparam K Lookup type comparable with the elements through \c Compare.
   * @param key Value to search for.
   *
   * @return A const iterator to the first element not ordered before \p key.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  [[nodiscard]] constexpr auto lower_bound(K const& key) const noexcept -> const_iterator {
    return std::lower_bound(m_data.begin(), m_data.end(), key, m_cmp);
  }

  /**
   * @brief Heterogeneous \c upper_bound for a key-comparable type \p K.
   *
   * @tparam K Lookup type comparable with the elements through a transparent
   *           \c Compare.
   * @param key Value to search for.
   *
   * @return A const iterator to the first element ordered after \p key.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  [[nodiscard]] constexpr auto upper_bound(K const& key) const noexcept -> const_iterator {
    return std::upper_bound(m_data.begin(), m_data.end(), key, m_cmp);
  }

  /**
   * @brief Heterogeneous \c find for a key-comparable type \p K.
   *
   * @tparam K Lookup type comparable with the elements through a transparent
   *           \c Compare.
   * @param key Value to search for.
   *
   * @return A const iterator to the matching element, or \c end() when absent.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  [[nodiscard]] constexpr auto find(K const& key) const noexcept -> const_iterator {
    auto const pos{lower_bound(key)};
    if (pos != m_data.end() && !m_cmp(key, *pos)) {
      return pos;
    }
    return m_data.end();
  }

  /**
   * @brief Heterogeneous membership test for a key-comparable type \p K.
   *
   * @tparam K Lookup type comparable with the elements through a transparent
   *           \c Compare.
   * @param key Value to test.
   *
   * @return \c true when \p key is present.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  [[nodiscard]] constexpr auto contains(K const& key) const noexcept -> bool {
    return find(key) != m_data.end();
  }

  /**
   * @brief Heterogeneous \c count for a key-comparable type \p K.
   *
   * @tparam K Lookup type comparable with the elements through a transparent
   *           \c Compare.
   * @param key Value to count.
   *
   * @return \c 1 when \p key is present, otherwise \c 0.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  [[nodiscard]] constexpr auto count(K const& key) const noexcept -> size_type {
    return contains(key) ? size_type{1} : size_type{0};
  }

  /**
   * @brief Heterogeneous \c equal_range for a key-comparable type \p K.
   *
   * @tparam K Lookup type comparable with the elements through a transparent
   *           \c Compare.
   * @param key Value to search for.
   *
   * @return A pair of \c lower_bound(key) and \c upper_bound(key).
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  [[nodiscard]] constexpr auto equal_range(K const& key) const noexcept
    -> std::pair<const_iterator, const_iterator> {
    return {lower_bound(key), upper_bound(key)};
  }

  /**
   * @brief Inserts a copy of \p value if no equal element exists.
   *
   * @param value Value to copy in.
   *
   * @return A pair of an iterator to the element and \c true on insertion, or an
   *         iterator to the existing element and \c false.
   *
   * @pre None.
   * @post \p value is present and the order is preserved; on insertion \c size()
   *       grew by one and iterators are invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto insert(T const& value) noexcept -> std::pair<const_iterator, bool> {
    auto const pos{lower_bound(value)};
    if (pos != m_data.end() && !m_cmp(value, *pos)) {
      return {pos, false};
    }
    return {m_data.insert(pos, value), true};
  }

  /**
   * @brief Inserts \p value by moving it if no equal element exists.
   *
   * @param value Value to move in.
   *
   * @return A pair of an iterator to the element and \c true on insertion, or an
   *         iterator to the existing element and \c false.
   *
   * @pre None.
   * @post On insertion \p value has been moved from, is present, and \c size()
   *       grew by one; iterators are invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto insert(T&& value) noexcept -> std::pair<const_iterator, bool> {
    auto const pos{lower_bound(value)};
    if (pos != m_data.end() && !m_cmp(value, *pos)) {
      return {pos, false};
    }
    return {m_data.insert(pos, std::move(value)), true};
  }

  /**
   * @brief Constructs a \p T from \p args and inserts it if no equal element
   *        exists.
   *
   * The value is constructed first, since its position is found by comparison.
   *
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return A pair of an iterator to the element and \c true on insertion, or an
   *         iterator to the existing element and \c false.
   *
   * @pre None.
   * @post The value is present; on insertion \c size() grew by one.
   *
   * @complexity \c O(N) for the element shift.
   */
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> std::pair<const_iterator, bool> {
    return insert(T(std::forward<Args>(args)...));
  }

  /**
   * @brief Removes the element equal to \p key, if present.
   *
   * @param key Value to remove.
   *
   * @return \c 1 when an element was removed, otherwise \c 0.
   *
   * @pre None.
   * @post \p key is absent; on a removal \c size() shrank by one and iterators
   *       are invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto erase(T const& key) noexcept -> size_type {
    auto const pos{find(key)};
    if (pos == m_data.end()) {
      return 0;
    }
    m_data.erase(pos);
    return 1;
  }

  /**
   * @brief Heterogeneous erase of the element equal to \p key.
   *
   * Enabled only when \c Compare is transparent, so an equal element is located
   * from a compatible probe type without constructing a \p T.
   *
   * @tparam K Lookup type comparable with the elements through \c Compare.
   * @param key Value to remove.
   *
   * @return \c 1 when an element was removed, otherwise \c 0.
   *
   * @pre None.
   * @post \p key is absent; on a removal \c size() shrank by one and iterators
   *       are invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  template <typename K>
    requires requires { typename Compare::is_transparent; }
  constexpr auto erase(K const& key) noexcept -> size_type {
    auto const pos{find(key)};
    if (pos == m_data.end()) {
      return 0;
    }
    m_data.erase(pos);
    return 1;
  }

  /**
   * @brief Removes the element at \p pos and returns the next iterator.
   *
   * @param pos Iterator to the element to remove; must be dereferenceable.
   *
   * @return A const iterator to the element after the removed one.
   *
   * @pre \p pos refers to an element of this set, not \c end().
   * @post The element is removed and \c size() shrank by one; iterators are
   *       invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto erase(const_iterator const pos) noexcept -> const_iterator {
    return m_data.erase(pos);
  }

  /**
   * @brief Equality over the element sets.
   *
   * @param a First set.
   * @param b Second set.
   *
   * @return \c true when both hold the same elements.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] friend constexpr auto operator==(flat_set const& a, flat_set const& b) noexcept
    -> bool
    requires std::equality_comparable<T>
  {
    return a.m_data == b.m_data;
  }

  /**
   * @brief Lexicographical ordering over the sorted sequences.
   *
   * @param a First set.
   * @param b Second set.
   *
   * @return The three-way comparison of the two sorted sequences.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] friend constexpr auto operator<=>(flat_set const& a, flat_set const& b) noexcept
    requires std::three_way_comparable<T>
  {
    return a.m_data <=> b.m_data;
  }
};

}  // namespace nexenne::container
