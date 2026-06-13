#pragma once

/**
 * @file
 * @brief Sorted-vector ordered map: the std::map shape with contiguous storage.
 *
 * \c flat_map<Key, Value, Compare> keeps its entries in one \c std::vector of
 * key-value pairs, sorted by key. Lookups are an \c O(log N) binary search over
 * the keys, iteration is a flat in-order walk, and there is no per-entry node
 * allocation, only the vector's. Insert and erase are \c O(N) because the tail
 * shifts to keep the array sorted and packed. For read-mostly maps that fit in
 * cache, the locality usually beats a node-based \c std::map.
 *
 * The value of an entry may be changed in place (it does not affect ordering),
 * so the iterator is mutable, but the key must not be: rewriting a key through an
 * iterator breaks the sort. Use \c flat_hash_map or \c dense_map when ordering
 * does not matter and you want \c O(1) average lookup, and \c static_flat_map when
 * capacity is fixed at compile time. Every operation is \c noexcept; allocation
 * failure terminates. The map is a vector plus a comparator (rule of zero).
 */

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace nexenne::container {

/**
 * @brief Ordered map backed by a key-sorted std::vector of pairs.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped type.
 * @tparam Compare Strict weak ordering over \p Key; \c std::less<Key> by default.
 *
 * @pre None.
 * @post A default-constructed map is empty.
 */
template <typename Key, typename Value, typename Compare = std::less<Key>>
  requires std::strict_weak_order<Compare const&, Key const&, Key const&>
class flat_map {
public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<Key, Value>;
  using key_compare = Compare;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type&;
  using const_reference = value_type const&;
  using iterator = typename std::vector<value_type>::iterator;
  using const_iterator = typename std::vector<value_type>::const_iterator;

private:
  std::vector<value_type> m_data;
  [[no_unique_address]] Compare m_cmp{};

public:
  /**
   * @brief Constructs an empty map with a default-constructed comparator.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr flat_map() noexcept = default;

  /**
   * @brief Constructs an empty map ordered by \p cmp.
   *
   * @param cmp Comparator to store.
   *
   * @pre None.
   * @post \c empty() is \c true and the stored comparator is \p cmp.
   */
  explicit constexpr flat_map(Compare cmp) noexcept : m_cmp{std::move(cmp)} {}

  /**
   * @brief Constructs from an initializer list, keeping the first of equal keys.
   *
   * @param init Key-value pairs; later duplicate keys are ignored.
   *
   * @pre None.
   * @post Entries are stored sorted by key, each distinct key once.
   */
  constexpr flat_map(std::initializer_list<value_type> const init) noexcept {
    m_data.reserve(init.size());
    for (auto const& entry : init) {
      insert(entry);
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
    return m_data.empty();
  }

  /**
   * @brief Number of entries.
   *
   * @return The entry count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_data.size();
  }

  /**
   * @brief The largest number of entries the map can hold.
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
   * @brief Reserves storage for at least \p n entries.
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
   * @brief Removes every entry; capacity is retained.
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
   * @param other Map to exchange state with.
   *
   * @pre None.
   * @post This map and \p other have exchanged entries and comparators.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(flat_map& other) noexcept -> void {
    using std::swap;
    m_data.swap(other.m_data);
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
  friend constexpr auto swap(flat_map& a, flat_map& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Iterator to the first entry, ordered by key.
   *
   * @return An iterator to the start of the sorted range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return m_data.begin();
  }

  /**
   * @brief Iterator one past the last entry.
   *
   * @return An iterator to the end of the sorted range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return m_data.end();
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return m_data.begin();
  }

  /// @copydoc end()
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
    return std::lower_bound(
      m_data.begin(),
      m_data.end(),
      key,
      [this](value_type const& slot, Key const& probe) { return m_cmp(slot.first, probe); }
    );
  }

  /// @copydoc lower_bound(Key const&)
  [[nodiscard]] constexpr auto lower_bound(Key const& key) const noexcept -> const_iterator {
    return std::lower_bound(
      m_data.begin(),
      m_data.end(),
      key,
      [this](value_type const& slot, Key const& probe) { return m_cmp(slot.first, probe); }
    );
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
    return std::upper_bound(
      m_data.begin(),
      m_data.end(),
      key,
      [this](Key const& probe, value_type const& slot) { return m_cmp(probe, slot.first); }
    );
  }

  /// @copydoc upper_bound(Key const&)
  [[nodiscard]] constexpr auto upper_bound(Key const& key) const noexcept -> const_iterator {
    return std::upper_bound(
      m_data.begin(),
      m_data.end(),
      key,
      [this](Key const& probe, value_type const& slot) { return m_cmp(probe, slot.first); }
    );
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
    if (pos != m_data.end() && !m_cmp(key, pos->first)) {
      return pos;
    }
    return m_data.end();
  }

  /// @copydoc find(Key const&)
  [[nodiscard]] constexpr auto find(Key const& key) const noexcept -> const_iterator {
    auto const pos{lower_bound(key)};
    if (pos != m_data.end() && !m_cmp(key, pos->first)) {
      return pos;
    }
    return m_data.end();
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
    return find(key) != m_data.end();
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
   * Unlike \c std::map::at this never throws: it returns \c nullptr for a
   * missing key.
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
    return pos == m_data.end() ? nullptr : std::addressof(pos->second);
  }

  /// @copydoc at(Key const&)
  [[nodiscard]] constexpr auto at(Key const& key) const noexcept -> Value const* {
    auto const pos{find(key)};
    return pos == m_data.end() ? nullptr : std::addressof(pos->second);
  }

  /**
   * @brief Accesses the value for \p key, inserting a default-constructed value
   *        if absent.
   *
   * @param key Key whose value to access or create.
   *
   * @return A mutable reference to the value mapped to \p key.
   *
   * @pre None.
   * @post An entry for \p key exists; on insertion \c size() grew by one and
   *       iterators are invalidated.
   *
   * @complexity \c O(N) on insertion, \c O(log N) otherwise.
   */
  constexpr auto operator[](Key const& key) noexcept -> Value&
    requires std::default_initializable<Value>
  {
    auto const pos{lower_bound(key)};
    if (pos != m_data.end() && !m_cmp(key, pos->first)) {
      return pos->second;
    }
    return m_data.insert(pos, value_type{key, Value{}})->second;
  }

  /**
   * @brief Inserts \p entry unless its key is already present.
   *
   * @param entry Key-value pair to copy in.
   *
   * @return A pair of an iterator to the entry and \c true on insertion, or an
   *         iterator to the existing entry and \c false.
   *
   * @pre None.
   * @post On insertion \c size() grew by one and key order is preserved;
   *       iterators are invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto insert(value_type const& entry) noexcept -> std::pair<iterator, bool> {
    auto const pos{lower_bound(entry.first)};
    if (pos != m_data.end() && !m_cmp(entry.first, pos->first)) {
      return {pos, false};
    }
    return {m_data.insert(pos, entry), true};
  }

  /**
   * @brief Inserts \p entry by moving it unless its key is present.
   *
   * @param entry Key-value pair to move in.
   *
   * @return A pair of an iterator to the entry and \c true on insertion, or an
   *         iterator to the existing entry and \c false.
   *
   * @pre None.
   * @post On insertion \p entry has been moved from and \c size() grew by one;
   *       iterators are invalidated.
   *
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto insert(value_type&& entry) noexcept -> std::pair<iterator, bool> {
    auto const pos{lower_bound(entry.first)};
    if (pos != m_data.end() && !m_cmp(entry.first, pos->first)) {
      return {pos, false};
    }
    return {m_data.insert(pos, std::move(entry)), true};
  }

  /**
   * @brief Assigns \p value to \p key, inserting a new entry if absent.
   *
   * @param key Key to assign or insert.
   * @param value Mapped value to store, moved in.
   *
   * @return A pair of an iterator to the entry and \c true when a new entry was
   *         inserted, or \c false when an existing value was overwritten.
   *
   * @pre None.
   * @post The entry for \p key maps to \p value; on insertion \c size() grew by
   *       one and iterators are invalidated.
   *
   * @complexity \c O(N) on insertion, \c O(log N) on assignment.
   */
  constexpr auto
  insert_or_assign(Key const& key, Value value) noexcept -> std::pair<iterator, bool> {
    auto const pos{lower_bound(key)};
    if (pos != m_data.end() && !m_cmp(key, pos->first)) {
      pos->second = std::move(value);
      return {pos, false};
    }
    return {m_data.insert(pos, value_type{key, std::move(value)}), true};
  }

  /**
   * @brief Constructs an entry from \p args and inserts it unless its key is
   *        present.
   *
   * @tparam Args Constructor argument types for \c value_type.
   * @param args Arguments forwarded to the pair's constructor.
   *
   * @return A pair of an iterator to the entry and \c true on insertion, or an
   *         iterator to the existing entry and \c false.
   *
   * @pre None.
   * @post On insertion \c size() grew by one.
   *
   * @complexity \c O(N) for the element shift.
   */
  template <typename... Args>
    requires std::constructible_from<value_type, Args...>
  constexpr auto emplace(Args&&... args) noexcept -> std::pair<iterator, bool> {
    return insert(value_type(std::forward<Args>(args)...));
  }

  /**
   * @brief Inserts an entry for \p key with a value built from \p args, only if
   *        \p key is absent.
   *
   * The value is constructed only on insertion, so an existing entry is left
   * untouched and the arguments are not consumed.
   *
   * @tparam Args Constructor argument types for \p Value.
   * @param key Key to insert under.
   * @param args Arguments forwarded to \p Value's constructor on insertion.
   *
   * @return A pair of an iterator to the entry and \c true on insertion, or an
   *         iterator to the existing entry and \c false.
   *
   * @pre None.
   * @post An entry for \p key exists; on insertion \c size() grew by one.
   *
   * @complexity \c O(N) on insertion, \c O(log N) otherwise.
   */
  template <typename... Args>
    requires std::constructible_from<Value, Args...>
  constexpr auto try_emplace(Key const& key, Args&&... args) noexcept -> std::pair<iterator, bool> {
    auto const pos{lower_bound(key)};
    if (pos != m_data.end() && !m_cmp(key, pos->first)) {
      return {pos, false};
    }
    return {m_data.insert(pos, value_type{key, Value(std::forward<Args>(args)...)}), true};
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
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto erase(Key const& key) noexcept -> size_type {
    auto const pos{find(key)};
    if (pos == m_data.end()) {
      return 0;
    }
    m_data.erase(pos);
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
   * @complexity \c O(N) for the element shift.
   */
  constexpr auto erase(const_iterator const pos) noexcept -> iterator {
    return m_data.erase(pos);
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
  operator==(flat_map const& a, flat_map const& b) noexcept -> bool
    requires std::equality_comparable<value_type>
  {
    return a.m_data == b.m_data;
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
  [[nodiscard]] friend constexpr auto operator<=>(flat_map const& a, flat_map const& b) noexcept
    requires std::three_way_comparable<value_type>
  {
    return a.m_data <=> b.m_data;
  }
};

}  // namespace nexenne::container
