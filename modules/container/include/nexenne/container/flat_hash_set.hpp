#pragma once

/**
 * @file
 * @brief Open-addressing hash set, companion to \c flat_hash_map.
 *
 * \c flat_hash_set<T, Hash, KeyEq> is a set built on the same single-vector,
 * linear-probing storage as \c flat_hash_map, sharing its implementation in full
 * by backing onto a \c flat_hash_map<T, detail::empty_value>. The same trade-offs
 * apply: one contiguous allocation and roughly one cache miss per lookup instead
 * of the node-per-element chaining of \c std::unordered_set, at the cost of losing
 * reference stability on a rehash. Iteration walks the slot array in an
 * unspecified order.
 *
 * The mapped type is a zero-state marker, so a set entry costs what a map entry
 * costs minus a meaningful value: per slot that is the empty / occupied /
 * tombstone state, the cached hash, and the stored key (the marker adds at most a
 * byte of padding). This is not a bit-per-element structure; for dense integer
 * membership prefer \c sparse_set or \c bitset_dynamic. Every operation is
 * \c noexcept; allocation failure terminates.
 */

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>

#include <nexenne/container/flat_hash_map.hpp>

namespace nexenne::container {

namespace detail {

/// Zero-state mapped marker that turns \c flat_hash_map into a set.
struct empty_value {
  [[nodiscard]] friend constexpr auto operator==(empty_value, empty_value) noexcept -> bool {
    return true;
  }
};

}  // namespace detail

/**
 * @brief Open-addressing, linear-probing hash set.
 *
 * @tparam T Hashable element type.
 * @tparam Hash Hash functor; \c std::hash<T> by default.
 * @tparam KeyEq Equality predicate; \c std::equal_to<T> by default.
 *
 * @pre None.
 * @post A default-constructed set is empty with no allocated storage.
 */
template <typename T, typename Hash = std::hash<T>, typename KeyEq = std::equal_to<T>>
class flat_hash_set {
public:
  using key_type = T;
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEq;

private:
  using backing = flat_hash_map<T, detail::empty_value, Hash, KeyEq>;

  backing m_map;

public:
  /**
   * @brief Constructs an empty set with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  flat_hash_set() noexcept = default;

  /**
   * @brief Constructs an empty set with storage reserved for \p expected_entries.
   *
   * @param expected_entries Number of elements to size the initial storage for.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() admits \p expected_entries.
   */
  explicit flat_hash_set(size_type const expected_entries) noexcept : m_map{expected_entries} {}

  /**
   * @brief Number of elements currently stored.
   *
   * @return Element count.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_map.size();
  }

  /**
   * @brief Whether the set holds no elements.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_map.empty();
  }

  /**
   * @brief Number of slots before the next rehash.
   *
   * @return Current bucket capacity.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return m_map.capacity();
  }

  /**
   * @brief Largest number of elements the set can ever hold.
   *
   * @return The maximum size of the backing map.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto max_size() const noexcept -> size_type {
    return m_map.max_size();
  }

  /**
   * @brief Current ratio of live elements to bucket capacity.
   *
   * @return The load factor in [0, 1).
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto load_factor() const noexcept -> double {
    return m_map.load_factor();
  }

  /**
   * @brief Reserves storage for at least \p n elements.
   *
   * @param n Minimum element capacity to reserve.
   *
   * @pre None.
   * @post Capacity admits at least \p n elements; a rehash may have invalidated
   *       iterators and references.
   */
  auto reserve(size_type const n) noexcept -> void {
    m_map.reserve(n);
  }

  /**
   * @brief Removes all elements, retaining capacity.
   *
   * @pre None.
   * @post \c empty() is \c true. Capacity is retained.
   */
  auto clear() noexcept -> void {
    m_map.clear();
  }

  /**
   * @brief Releases unused bucket capacity.
   *
   * @pre None.
   * @post \c size() is unchanged; iterators and references are invalidated.
   */
  auto shrink_to_fit() noexcept -> void {
    m_map.shrink_to_fit();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Set to exchange state with.
   *
   * @pre None.
   * @post This set holds \p other's former elements and vice versa.
   */
  auto swap(flat_hash_set& other) noexcept -> void {
    m_map.swap(other.m_map);
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
  friend auto swap(flat_hash_set& a, flat_hash_set& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Inserts \p value if it is not already present.
   *
   * @param value Value to insert, moved into the set.
   *
   * @return \c true on a fresh insertion, \c false when the value was already
   *         present.
   *
   * @pre None.
   * @post \p value is present; on a fresh insertion \c size() increased by one and
   *       a rehash may have invalidated iterators and references.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(T value) noexcept -> bool {
    return m_map.insert(std::move(value), detail::empty_value{});
  }

  /**
   * @brief Removes \p value if present.
   *
   * @param value Value to remove.
   *
   * @return \c true when an element was removed, \c false when it was absent.
   *
   * @pre None.
   * @post \p value is absent; on a removal \c size() decreased by one.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase(T const& value) noexcept -> bool {
    return m_map.erase(value);
  }

  /**
   * @brief Whether \p value is a member of the set.
   *
   * @param value Value to test for membership.
   *
   * @return \c true when \p value is present.
   *
   * @pre None.
   * @post None. The set is not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto contains(T const& value) const noexcept -> bool {
    return m_map.contains(value);
  }

  /**
   * @brief Number of elements equal to \p value, always \c 0 or \c 1.
   *
   * @param value Value to count.
   *
   * @return \c 1 when \p value is present, otherwise \c 0.
   *
   * @pre None.
   * @post None. The set is not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto count(T const& value) const noexcept -> size_type {
    return m_map.count(value);
  }

  /// Forward iterator that walks the live elements of the set in unspecified order.
  class const_iterator {
  public:
    using value_type = T;
    using reference = T const&;
    using pointer = T const*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    constexpr const_iterator() noexcept = default;

    explicit constexpr const_iterator(typename backing::const_iterator const inner) noexcept
        : m_inner{inner} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return m_inner->first;
    }

    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(m_inner->first);
    }

    constexpr auto operator++() noexcept -> const_iterator& {
      ++m_inner;
      return *this;
    }

    constexpr auto operator++(int) noexcept -> const_iterator {
      auto const tmp{*this};
      ++*this;
      return tmp;
    }

    [[nodiscard]] friend constexpr auto
    operator==(const_iterator const& a, const_iterator const& b) noexcept -> bool {
      return a.m_inner == b.m_inner;
    }

  private:
    typename backing::const_iterator m_inner{};
  };

  using iterator = const_iterator;

  /**
   * @brief Iterator to the first live element; iteration order is unspecified.
   *
   * @return Const iterator to a live element, or \c end() when empty.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return const_iterator{m_map.begin()};
  }

  /**
   * @brief Iterator one past the last live element.
   *
   * @return Past-the-end const iterator.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return const_iterator{m_map.end()};
  }

  /// @copydoc begin()
  [[nodiscard]] auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /// @copydoc end()
  [[nodiscard]] auto cend() const noexcept -> const_iterator {
    return end();
  }

  /**
   * @brief Whether \p a and \p b hold the same elements.
   *
   * @param a First set.
   * @param b Second set.
   *
   * @return \c true when both contain exactly the same values.
   *
   * @pre None.
   * @post None. Neither set is modified.
   *
   * @complexity \c O(n) average.
   */
  [[nodiscard]] friend auto
  operator==(flat_hash_set const& a, flat_hash_set const& b) noexcept -> bool {
    return a.m_map == b.m_map;
  }
};

}  // namespace nexenne::container
