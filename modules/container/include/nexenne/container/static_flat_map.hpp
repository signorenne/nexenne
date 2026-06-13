#pragma once

/**
 * @file
 * @brief Fixed-capacity sorted-array ordered map: no heap, no growth.
 *
 * \c static_flat_map<Key, Value, Capacity, Compare> is \c flat_map 's inline
 * cousin: the same key-sorted semantics (\c O(log N) lookup, \c O(N) insert and
 * erase) but the vector is replaced by a \c std::array of \p Capacity slots, so
 * there is no heap allocation and no growth. The storage lives inline, which
 * suits embedded and hot-path code where the maximum size is known up front and
 * any allocation is unacceptable.
 *
 * Because the array is value-initialised, \p Key and \p Value must be default
 * constructible. Insertion into a full map fails: the insert family returns
 * \c container_error::full rather than overflowing. There is deliberately no
 * \c operator[], since on a full map it could not honour its insert-or-access
 * contract; use \c try_emplace / \c insert_or_assign (which report \c full) to
 * add entries and \c at (which returns a nullable pointer) to read them. Every
 * operation is \c noexcept and fully \c constexpr.
 */

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Fixed-capacity ordered map backed by a key-sorted std::array.
 *
 * @tparam Key Key type; must be default-constructible.
 * @tparam Value Mapped type; must be default-constructible.
 * @tparam Capacity Fixed slot count; must be greater than zero.
 * @tparam Compare Strict weak ordering over \p Key; \c std::less<Key> by default.
 *
 * @pre None.
 * @post A default-constructed map is empty.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Compare = std::less<Key>>
  requires(Capacity > 0 && std::strict_weak_order<Compare const&, Key const&, Key const&>)
class static_flat_map {
public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<Key, Value>;
  using key_compare = Compare;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using iterator = typename std::array<value_type, Capacity>::iterator;
  using const_iterator = typename std::array<value_type, Capacity>::const_iterator;

private:
  std::array<value_type, Capacity> m_data{};
  size_type m_size{0};
  [[no_unique_address]] Compare m_cmp{};

  // Opens a gap at pos by moving the active tail one slot right; the caller has
  // already checked there is room.
  constexpr auto shift_right(iterator const pos) noexcept -> void {
    for (auto slot{end()}; slot != pos; --slot) {
      *slot = std::move(*(slot - 1));
    }
  }

public:
  /**
   * @brief Constructs an empty map with a default-constructed comparator.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr static_flat_map() noexcept = default;

  /**
   * @brief Constructs an empty map ordered by \p cmp.
   *
   * @param cmp Comparator to store.
   *
   * @pre None.
   * @post \c empty() is \c true and the stored comparator is \p cmp.
   */
  explicit constexpr static_flat_map(Compare cmp) noexcept : m_cmp{std::move(cmp)} {}

  /**
   * @brief Constructs from an initializer list, dropping later duplicate keys and
   *        any entries past \p Capacity.
   *
   * Since every operation is \c constexpr, a \c constexpr instance built this way
   * is a compile-time constant lookup table.
   *
   * @param init Key-value pairs to insert.
   *
   * @pre None.
   * @post The first distinct keys of \p init, up to \p Capacity, are present in
   *       sorted order.
   */
  constexpr static_flat_map(std::initializer_list<value_type> const init) noexcept {
    for (auto const& entry : init) {
      static_cast<void>(insert(entry));
    }
  }

  /**
   * @brief Reports whether the map holds no entries.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Number of entries, never exceeding \p Capacity.
   *
   * @return The entry count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Reports whether the map has reached its fixed capacity.
   *
   * @return \c true when \c size() equals \p Capacity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto full() const noexcept -> bool {
    return m_size == Capacity;
  }

  /**
   * @brief The fixed slot count.
   *
   * @return The template parameter \p Capacity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> size_type {
    return Capacity;
  }

  /// @copydoc capacity()
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return Capacity;
  }

  /**
   * @brief Logically removes every entry.
   *
   * The slots are not reset, only made unreachable; a resource-holding \p Value
   * is released when its slot is reused or the map is destroyed.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr auto clear() noexcept -> void {
    m_size = 0;
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Map to exchange state with.
   *
   * @pre None.
   * @post This map and \p other have exchanged entries and comparators.
   *
   * @complexity \c O(Capacity).
   */
  constexpr auto swap(static_flat_map& other) noexcept -> void {
    using std::swap;
    swap(m_data, other.m_data);
    swap(m_size, other.m_size);
    swap(m_cmp, other.m_cmp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First map.
   * @param b Second map.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(static_flat_map& a, static_flat_map& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Iterator to the first entry, ordered by key.
   *
   * @return An iterator to the start of the active range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return m_data.begin();
  }

  /**
   * @brief Iterator one past the last active entry.
   *
   * @return An iterator to the end of the active range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return m_data.begin() + m_size;
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return m_data.begin();
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return m_data.begin() + m_size;
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return m_data.begin();
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return m_data.begin() + m_size;
  }

  /**
   * @brief First entry whose key is not ordered before \p key.
   *
   * @param key Key to search for.
   *
   * @return An iterator to the first entry with key \c >= \p key, or \c end().
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto lower_bound(Key const& key) noexcept -> iterator {
    return std::lower_bound(begin(), end(), key, [this](value_type const& slot, Key const& probe) {
      return m_cmp(slot.first, probe);
    });
  }

  /// @copydoc lower_bound(Key const&)
  [[nodiscard]] constexpr auto lower_bound(Key const& key) const noexcept -> const_iterator {
    return std::lower_bound(begin(), end(), key, [this](value_type const& slot, Key const& probe) {
      return m_cmp(slot.first, probe);
    });
  }

  /**
   * @brief First entry whose key is ordered after \p key.
   *
   * @param key Key to search for.
   *
   * @return An iterator to the first entry with key \c > \p key, or \c end().
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto upper_bound(Key const& key) noexcept -> iterator {
    return std::upper_bound(begin(), end(), key, [this](Key const& probe, value_type const& slot) {
      return m_cmp(probe, slot.first);
    });
  }

  /// @copydoc upper_bound(Key const&)
  [[nodiscard]] constexpr auto upper_bound(Key const& key) const noexcept -> const_iterator {
    return std::upper_bound(begin(), end(), key, [this](Key const& probe, value_type const& slot) {
      return m_cmp(probe, slot.first);
    });
  }

  /**
   * @brief Locates the entry whose key equals \p key.
   *
   * @param key Key to search for.
   *
   * @return An iterator to the matching entry, or \c end() when absent.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto find(Key const& key) noexcept -> iterator {
    auto const pos{lower_bound(key)};
    if (pos != end() && !m_cmp(key, pos->first)) {
      return pos;
    }
    return end();
  }

  /// @copydoc find(Key const&)
  [[nodiscard]] constexpr auto find(Key const& key) const noexcept -> const_iterator {
    auto const pos{lower_bound(key)};
    if (pos != end() && !m_cmp(key, pos->first)) {
      return pos;
    }
    return end();
  }

  /**
   * @brief Reports whether an entry with key \p key exists.
   *
   * @param key Key to test.
   *
   * @return \c true when \p key is present.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto contains(Key const& key) const noexcept -> bool {
    return find(key) != end();
  }

  /**
   * @brief Number of entries with key \p key, always \c 0 or \c 1.
   *
   * @param key Key to count.
   *
   * @return \c 1 when \p key is present, otherwise \c 0.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto count(Key const& key) const noexcept -> size_type {
    return contains(key) ? size_type{1} : size_type{0};
  }

  /**
   * @brief Checked access to the value for \p key.
   *
   * @param key Key whose value to access.
   *
   * @return A pointer to the mapped value, or \c nullptr when \p key is absent.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(log N).
   */
  [[nodiscard]] constexpr auto at(Key const& key) noexcept -> Value* {
    auto const pos{find(key)};
    return pos == end() ? nullptr : std::addressof(pos->second);
  }

  /// @copydoc at(Key const&)
  [[nodiscard]] constexpr auto at(Key const& key) const noexcept -> Value const* {
    auto const pos{find(key)};
    return pos == end() ? nullptr : std::addressof(pos->second);
  }

  /**
   * @brief Inserts \p entry unless its key is present, failing if full.
   *
   * @param entry Key-value pair to insert.
   *
   * @return A pair of an iterator and \c true on a new insertion, an iterator to
   *         the existing entry and \c false for a duplicate key, or
   *         \c container_error::full when the map is full and the key is new.
   *
   * @pre None.
   * @post On a new insertion \c size() grew by one and key order is preserved;
   *       iterators are invalidated.
   *
   * @complexity \c O(N) for the slot shift.
   */
  constexpr auto insert(value_type const& entry) noexcept -> result<std::pair<iterator, bool>> {
    auto const pos{lower_bound(entry.first)};
    if (pos != end() && !m_cmp(entry.first, pos->first)) {
      return std::pair<iterator, bool>{pos, false};
    }
    if (m_size == Capacity) {
      return std::unexpected{container_error::full};
    }
    shift_right(pos);
    *pos = entry;
    ++m_size;
    return std::pair<iterator, bool>{pos, true};
  }

  /**
   * @brief Inserts \p entry by moving it unless its key is present, failing if
   *        full.
   *
   * @param entry Key-value pair to move in.
   *
   * @return As for \c insert(value_type const&).
   *
   * @pre None.
   * @post On a new insertion \p entry has been moved from and \c size() grew by
   *       one; iterators are invalidated.
   *
   * @complexity \c O(N) for the slot shift.
   */
  constexpr auto insert(value_type&& entry) noexcept -> result<std::pair<iterator, bool>> {
    auto const pos{lower_bound(entry.first)};
    if (pos != end() && !m_cmp(entry.first, pos->first)) {
      return std::pair<iterator, bool>{pos, false};
    }
    if (m_size == Capacity) {
      return std::unexpected{container_error::full};
    }
    shift_right(pos);
    *pos = std::move(entry);
    ++m_size;
    return std::pair<iterator, bool>{pos, true};
  }

  /**
   * @brief Assigns \p value to \p key, inserting a new entry if absent.
   *
   * @param key Key to assign or insert.
   * @param value Mapped value to store, moved in.
   *
   * @return A pair of an iterator and \c true on a new insertion, an iterator and
   *         \c false when an existing value was overwritten, or
   *         \c container_error::full when the map is full and the key is new.
   *
   * @pre None.
   * @post The entry for \p key maps to \p value on success; on a new insertion
   *       \c size() grew by one.
   *
   * @complexity \c O(N) on insertion, \c O(log N) on assignment.
   */
  constexpr auto
  insert_or_assign(Key const& key, Value value) noexcept -> result<std::pair<iterator, bool>> {
    auto const pos{lower_bound(key)};
    if (pos != end() && !m_cmp(key, pos->first)) {
      pos->second = std::move(value);
      return std::pair<iterator, bool>{pos, false};
    }
    if (m_size == Capacity) {
      return std::unexpected{container_error::full};
    }
    shift_right(pos);
    *pos = value_type{key, std::move(value)};
    ++m_size;
    return std::pair<iterator, bool>{pos, true};
  }

  /**
   * @brief Constructs an entry from \p args and inserts it unless its key is
   *        present, failing if full.
   *
   * @tparam Args Constructor argument types for \c value_type.
   * @param args Arguments forwarded to the pair's constructor.
   *
   * @return As for \c insert(value_type const&).
   *
   * @pre None.
   * @post On a new insertion \c size() grew by one.
   *
   * @complexity \c O(N) for the slot shift.
   */
  template <typename... Args>
    requires std::constructible_from<value_type, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> result<std::pair<iterator, bool>> {
    return insert(value_type(std::forward<Args>(args)...));
  }

  /**
   * @brief Inserts an entry for \p key with a value built from \p args, only if
   *        \p key is absent, failing if full.
   *
   * The value is constructed only on insertion.
   *
   * @tparam Args Constructor argument types for \p Value.
   * @param key Key to insert under.
   * @param args Arguments forwarded to \p Value's constructor on insertion.
   *
   * @return A pair of an iterator and \c true on insertion, an iterator to the
   *         existing entry and \c false, or \c container_error::full when the map
   *         is full and the key is new.
   *
   * @pre None.
   * @post On insertion \c size() grew by one.
   *
   * @complexity \c O(N) on insertion, \c O(log N) otherwise.
   */
  template <typename... Args>
    requires std::constructible_from<Value, Args...>
  constexpr auto
  try_emplace(Key const& key, Args&&... args) noexcept -> result<std::pair<iterator, bool>> {
    auto const pos{lower_bound(key)};
    if (pos != end() && !m_cmp(key, pos->first)) {
      return std::pair<iterator, bool>{pos, false};
    }
    if (m_size == Capacity) {
      return std::unexpected{container_error::full};
    }
    shift_right(pos);
    *pos = value_type{key, Value(std::forward<Args>(args)...)};
    ++m_size;
    return std::pair<iterator, bool>{pos, true};
  }

  /**
   * @brief Removes the entry for \p key, if present.
   *
   * @param key Key to remove.
   *
   * @return \c 1 when an entry was removed, otherwise \c 0.
   *
   * @pre None.
   * @post \p key is absent; on a removal \c size() shrank by one and iterators
   *       are invalidated.
   *
   * @complexity \c O(N) for the slot shift.
   */
  constexpr auto erase(Key const& key) noexcept -> size_type {
    auto const pos{find(key)};
    if (pos == end()) {
      return 0;
    }
    for (auto slot{pos}; slot + 1 != end(); ++slot) {
      *slot = std::move(*(slot + 1));
    }
    --m_size;
    return 1;
  }

  /**
   * @brief Removes the entry at \p pos and returns the next iterator.
   *
   * @param pos Iterator to the entry to remove; must be dereferenceable.
   *
   * @return An iterator to the entry after the removed one.
   *
   * @pre \p pos refers to an entry of this map, not \c end().
   * @post \c size() shrank by one; iterators are invalidated.
   *
   * @complexity \c O(N) for the slot shift.
   */
  constexpr auto erase(const_iterator const pos) noexcept -> iterator {
    auto const target{begin() + (pos - cbegin())};
    for (auto slot{target}; slot + 1 != end(); ++slot) {
      *slot = std::move(*(slot + 1));
    }
    --m_size;
    return target;
  }

  /**
   * @brief Equality over the entry sets.
   *
   * @param a First map.
   * @param b Second map.
   *
   * @return \c true when both hold the same key-value entries.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] friend constexpr auto
  operator==(static_flat_map const& a, static_flat_map const& b) noexcept -> bool
    requires std::equality_comparable<value_type>
  {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographical ordering over the key-sorted entry sequences.
   *
   * @param a First map.
   * @param b Second map.
   *
   * @return The three-way comparison of the two entry sequences.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(static_flat_map const& a, static_flat_map const& b) noexcept
    requires std::three_way_comparable<value_type>
  {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }
};

}  // namespace nexenne::container
