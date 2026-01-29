#pragma once

/**
 * @file
 * @brief Sparse-set keyed map with dense, contiguous value storage.
 *
 * \c dense_map<Key, Value> pairs a \c sparse_set<Key> with a parallel
 * \c std::vector<Value>: the i-th key in \c keys() owns the i-th value in
 * \c values(), and the two stay synchronised through every insert and erase.
 * The "dense" part is load-bearing: iterating values is one contiguous walk, the
 * cache-friendly layout an ECS like EnTT relies on. Keys are unsigned integers,
 * so lookup is a sparse-set indirection, not a hash.
 *
 * Lookup comes two ways: \c find(k) returns a \c std::map -style iterator over
 * \c (key, value&) entries, while \c at(k) returns a \c Value* (or \c nullptr on
 * a miss) for the common "use the value, ignore the key" case. Like
 * \c sparse_set, \c erase is swap-pop, so dense order shifts on an interior
 * removal (the last entry fills the gap); hold the key, not the index, across an
 * erase. The \c dense_map<Key, void> specialisation degrades to a thin wrapper
 * over \c sparse_set for tag components with no payload. Every operation is
 * \c noexcept; allocation failure terminates.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/sparse_set.hpp>

namespace nexenne::container {

/**
 * @brief Sparse-set keyed map with dense, contiguous value storage.
 *
 * @tparam Key Unsigned integer key type.
 * @tparam Value Mapped type, or \c void for the tag-only specialisation.
 *
 * @pre None.
 * @post A default-constructed map is empty with no allocated storage.
 */
template <std::unsigned_integral Key, typename Value>
  requires std::is_void_v<Value> || std::move_constructible<Value>
class dense_map {
public:
  using key_type = Key;
  using value_type = Value;
  using size_type = std::size_t;

private:
  sparse_set<key_type> m_set;
  std::vector<value_type> m_values;

  template <bool IsConst>
  class basic_iterator;

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  /**
   * @brief Constructs an empty map with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr dense_map() noexcept = default;

  /**
   * @brief Reserves sparse capacity for keys and dense capacity for values.
   *
   * @param key_count Size of the key space to allocate sparse slots for; keys
   *                  \c 0 to \c key_count-1 are indexable without resizing.
   * @param dense_count Number of entries to reserve dense storage for.
   *
   * @pre None.
   * @post Capacity is at least the requested sizes; existing entries are kept.
   */
  constexpr auto reserve(size_type const key_count, size_type const dense_count) noexcept -> void {
    m_set.reserve(key_count, dense_count);
    m_values.reserve(dense_count);
  }

  /**
   * @brief Inserts \c (k, value) if \p k is not already present.
   *
   * @param k Key to insert.
   * @param value Value to store, moved in on a fresh insertion.
   *
   * @return \c true on a fresh insertion, \c false when \p k was already present
   *         (its value is left untouched).
   *
   * @pre None.
   * @post \p k is present. On a fresh insertion \c size() grew by one and value
   *       references and spans may be invalidated by reallocation.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(key_type const k, value_type value) noexcept -> bool {
    if (m_set.contains(k)) {
      return false;
    }
    m_set.insert(k);
    m_values.push_back(std::move(value));
    return true;
  }

  /**
   * @brief Inserts \c (k, value), overwriting any existing value.
   *
   * @param k Key to insert or update.
   * @param value Value to store, moved into the map.
   *
   * @return \c true on a fresh insertion, \c false when an existing value was
   *         replaced.
   *
   * @pre None.
   * @post \p k maps to \p value. On a fresh insertion \c size() grew by one and
   *       value references and spans may be invalidated by reallocation.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert_or_assign(key_type const k, value_type value) noexcept -> bool {
    if (auto const pos{m_set.index_of(k)}) {
      m_values[*pos] = std::move(value);
      return false;
    }
    m_set.insert(k);
    m_values.push_back(std::move(value));
    return true;
  }

  /**
   * @brief Constructs a value in place for \p k if it is not already present.
   *
   * @tparam Args Constructor argument types.
   * @param k Key to insert.
   * @param args Arguments forwarded to \p Value's constructor.
   *
   * @return \c true on a fresh insertion, \c false when \p k was already present
   *         (its value is left untouched and \p args are not used).
   *
   * @pre None.
   * @post \p k is present. On a fresh insertion \c size() grew by one and value
   *       references and spans may be invalidated by reallocation.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<value_type, Args...>
  constexpr auto emplace(key_type const k, Args&&... args) noexcept -> bool {
    if (m_set.contains(k)) {
      return false;
    }
    m_set.insert(k);
    m_values.emplace_back(std::forward<Args>(args)...);
    return true;
  }

  /**
   * @brief Removes the entry for \p k via swap-pop.
   *
   * The last entry fills the vacancy, so dense order shifts on an interior
   * removal.
   *
   * @param k Key to remove.
   *
   * @return \c true when an entry was removed, \c false when \p k was absent.
   *
   * @pre None.
   * @post \p k is absent. On a removal \c size() shrank by one and the indices of
   *       the formerly-last entry change.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase(key_type const k) noexcept -> bool {
    auto const pos{m_set.index_of(k)};
    if (!pos) {
      return false;
    }
    auto const last_pos{m_values.size() - 1};
    if (*pos != last_pos) {
      m_values[*pos] = std::move(m_values[last_pos]);
    }
    m_values.pop_back();
    m_set.erase(k);
    return true;
  }

  /**
   * @brief Whether \p k has an entry.
   *
   * @param k Key to test for membership.
   *
   * @return \c true when \p k is present.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto contains(key_type const k) const noexcept -> bool {
    return m_set.contains(k);
  }

  /**
   * @brief Number of entries for \p k, always \c 0 or \c 1.
   *
   * @param k Key to count.
   *
   * @return \c 1 when \p k is present, otherwise \c 0.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto count(key_type const k) const noexcept -> size_type {
    return contains(k) ? size_type{1} : size_type{0};
  }

  /**
   * @brief Pointer to the value for \p k, or \c nullptr on a miss.
   *
   * The returned pointer is stable until the next \c erase or \c clear; another
   * insert may invalidate it by reallocating the dense value vector.
   *
   * @param k Key to look up.
   *
   * @return A pointer to the value when \p k is present, otherwise \c nullptr.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto at(key_type const k) noexcept -> value_type* {
    if (auto const pos{m_set.index_of(k)}) {
      return std::addressof(m_values[*pos]);
    }
    return nullptr;
  }

  /**
   * @brief Pointer to the const value for \p k, or \c nullptr on a miss.
   *
   * @param k Key to look up.
   *
   * @return A pointer to the const value when \p k is present, otherwise
   *         \c nullptr.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto at(key_type const k) const noexcept -> value_type const* {
    if (auto const pos{m_set.index_of(k)}) {
      return std::addressof(m_values[*pos]);
    }
    return nullptr;
  }

  /**
   * @brief \c std::map -style lookup by key.
   *
   * @param k Key to look up.
   *
   * @return Iterator to the matching entry, or \c end() when \p k is absent.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto find(key_type const k) noexcept -> iterator {
    if (auto const pos{m_set.index_of(k)}) {
      return iterator{*this, *pos};
    }
    return end();
  }

  /**
   * @brief \c std::map -style const lookup by key.
   *
   * @param k Key to look up.
   *
   * @return Const iterator to the matching entry, or \c end() when \p k is
   *         absent.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto find(key_type const k) const noexcept -> const_iterator {
    if (auto const pos{m_set.index_of(k)}) {
      return const_iterator{*this, *pos};
    }
    return end();
  }

  /**
   * @brief Dense index of \p k in \c keys() and \c values().
   *
   * @param k Key to locate.
   *
   * @return The dense index when \p k is present, otherwise an empty optional.
   *
   * @pre None.
   * @post None. The map is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto index_of(key_type const k
  ) const noexcept -> std::optional<size_type> {
    return m_set.index_of(k);
  }

  /**
   * @brief Removes all entries, retaining capacity.
   *
   * @pre None.
   * @post \c empty() is \c true. Capacity is retained.
   */
  constexpr auto clear() noexcept -> void {
    m_set.clear();
    m_values.clear();
  }

  /**
   * @brief Releases unused capacity in both the key set and the value vector.
   *
   * @pre None.
   * @post \c size() is unchanged; capacity may shrink toward \c size().
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_set.shrink_to_fit();
    m_values.shrink_to_fit();
  }

  /**
   * @brief Number of entries currently stored.
   *
   * @return Entry count.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_values.size();
  }

  /**
   * @brief Whether the map holds no entries.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_values.empty();
  }

  /**
   * @brief Largest number of entries the map can ever hold.
   *
   * @return The maximum size of the dense value vector.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_values.max_size();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Map to exchange state with.
   *
   * @pre None.
   * @post This map holds \p other's former entries and vice versa.
   */
  constexpr auto swap(dense_map& other) noexcept -> void {
    m_set.swap(other.m_set);
    m_values.swap(other.m_values);
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
  friend constexpr auto swap(dense_map& a, dense_map& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Contiguous view of the live keys.
   *
   * @return Span over the dense key array.
   *
   * @pre None.
   * @post None. The map is not modified; the span is invalidated by any mutation.
   */
  [[nodiscard]] constexpr auto keys() const noexcept -> std::span<key_type const> {
    return m_set.keys();
  }

  /**
   * @brief Contiguous view of the live values.
   *
   * The i-th value belongs to the i-th key in \c keys().
   *
   * @return Span over the dense value array.
   *
   * @pre None.
   * @post None. The map is not modified; the span is invalidated by any mutation.
   */
  [[nodiscard]] constexpr auto values() noexcept -> std::span<value_type> {
    return std::span<value_type>{m_values.data(), m_values.size()};
  }

  /**
   * @brief Contiguous const view of the live values.
   *
   * @return Span over the dense value array.
   *
   * @pre None.
   * @post None. The map is not modified; the span is invalidated by any mutation.
   */
  [[nodiscard]] constexpr auto values() const noexcept -> std::span<value_type const> {
    return std::span<value_type const>{m_values.data(), m_values.size()};
  }

  /**
   * @brief Iterator to the first \c (key, value) entry.
   *
   * @return Iterator to the start of the dense range.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return iterator{*this, 0};
  }

  /**
   * @brief Iterator one past the last entry.
   *
   * @return Iterator to the end of the dense range.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return iterator{*this, m_values.size()};
  }

  /**
   * @brief Const iterator to the first entry.
   *
   * @return Const iterator to the start of the dense range.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return const_iterator{*this, 0};
  }

  /**
   * @brief Const iterator one past the last entry.
   *
   * @return Const iterator to the end of the dense range.
   *
   * @pre None.
   * @post None. The map is not modified.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return const_iterator{*this, m_values.size()};
  }

  /// @copydoc begin() const
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /// @copydoc end() const
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return end();
  }

private:
  // Proxy forward-of-sorts iterator: dereferencing yields a fresh
  // std::pair<Key, Value&> by value, so it models only input_iterator.
  template <bool IsConst>
  class basic_iterator {
  public:
    using value_ref = std::conditional_t<IsConst, Value const&, Value&>;
    using value_type = std::pair<Key, value_ref>;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;
    using iterator_concept = std::input_iterator_tag;

    constexpr basic_iterator() noexcept = default;

    constexpr basic_iterator(
      std::conditional_t<IsConst, dense_map const&, dense_map&> map, size_type const pos
    ) noexcept
        : m_map{std::addressof(map)}, m_pos{pos} {}

    // Convert a mutable iterator to a const_iterator.
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_map{other.m_map}, m_pos{other.m_pos} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> value_type {
      return value_type{m_map->m_set.keys()[m_pos], m_map->m_values[m_pos]};
    }

    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_pos;
      return *this;
    }

    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const tmp{*this};
      ++*this;
      return tmp;
    }

    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_pos == b.m_pos && a.m_map == b.m_map;
    }

  private:
    using map_ptr_t = std::conditional_t<IsConst, dense_map const*, dense_map*>;

    map_ptr_t m_map{nullptr};
    size_type m_pos{0};

    template <bool>
    friend class basic_iterator;
  };
};

/**
 * @brief Tag-only specialisation: a thin \c sparse_set wrapper with no values.
 *
 * @tparam Key Unsigned integer key type.
 *
 * @pre None.
 * @post A default-constructed tag set is empty with no allocated storage.
 */
template <std::unsigned_integral Key>
class dense_map<Key, void> {
public:
  using key_type = Key;
  using value_type = void;
  using size_type = std::size_t;
  using iterator = typename sparse_set<key_type>::iterator;
  using const_iterator = typename sparse_set<key_type>::const_iterator;

  /**
   * @brief Constructs an empty tag set with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr dense_map() noexcept = default;

  /**
   * @brief Reserves sparse capacity for keys and dense capacity for the array.
   *
   * @param key_count Size of the key space to allocate sparse slots for; keys
   *                  \c 0 to \c key_count-1 are indexable without resizing.
   * @param dense_count Number of keys to reserve dense storage for.
   *
   * @pre None.
   * @post Capacity is at least the requested sizes; existing keys are kept.
   */
  constexpr auto reserve(size_type const key_count, size_type const dense_count) noexcept -> void {
    m_set.reserve(key_count, dense_count);
  }

  /**
   * @brief Inserts the tag key \p k.
   *
   * @param k Key to insert.
   *
   * @return \c true on a fresh key, \c false when \p k was already present.
   *
   * @pre None.
   * @post \p k is present. On a fresh key \c size() grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(key_type const k) noexcept -> bool {
    return m_set.insert(k);
  }

  /**
   * @brief Removes the tag key \p k.
   *
   * @param k Key to remove.
   *
   * @return \c true when \p k was removed, \c false when it was absent.
   *
   * @pre None.
   * @post \p k is absent. On a removal \c size() shrank by one.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase(key_type const k) noexcept -> bool {
    return m_set.erase(k);
  }

  /**
   * @brief Whether \p k is present.
   *
   * @param k Key to test.
   *
   * @return \c true when \p k is in the set.
   *
   * @pre None.
   * @post None. The set is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto contains(key_type const k) const noexcept -> bool {
    return m_set.contains(k);
  }

  /**
   * @brief Number of entries for \p k, always \c 0 or \c 1.
   *
   * @param k Key to count.
   *
   * @return \c 1 when \p k is present, otherwise \c 0.
   *
   * @pre None.
   * @post None. The set is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto count(key_type const k) const noexcept -> size_type {
    return m_set.count(k);
  }

  /**
   * @brief Locates the key \p k.
   *
   * @param k Key to look up.
   *
   * @return Iterator to \p k, or \c end() when absent.
   *
   * @pre None.
   * @post None. The set is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto find(key_type const k) const noexcept -> const_iterator {
    return m_set.find(k);
  }

  /**
   * @brief Dense index of \p k.
   *
   * @param k Key to locate.
   *
   * @return The dense index when present, otherwise an empty optional.
   *
   * @pre None.
   * @post None. The set is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto index_of(key_type const k
  ) const noexcept -> std::optional<size_type> {
    return m_set.index_of(k);
  }

  /**
   * @brief Removes all keys, retaining capacity.
   *
   * @pre None.
   * @post \c empty() is \c true. Capacity is retained.
   */
  constexpr auto clear() noexcept -> void {
    m_set.clear();
  }

  /**
   * @brief Releases unused capacity.
   *
   * @pre None.
   * @post \c size() is unchanged; capacity may shrink toward \c size().
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_set.shrink_to_fit();
  }

  /**
   * @brief Number of keys currently stored.
   *
   * @return Key count.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_set.size();
  }

  /**
   * @brief Whether the set holds no keys.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_set.empty();
  }

  /**
   * @brief Largest number of keys the set can ever hold.
   *
   * @return The maximum size of the underlying sparse set.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_set.max_size();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Tag set to exchange state with.
   *
   * @pre None.
   * @post This set holds \p other's former keys and vice versa.
   */
  constexpr auto swap(dense_map& other) noexcept -> void {
    m_set.swap(other.m_set);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First tag set.
   * @param b Second tag set.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(dense_map& a, dense_map& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Contiguous view of the live keys.
   *
   * @return Span over the dense key array.
   *
   * @pre None.
   * @post None. The set is not modified; the span is invalidated by any mutation.
   */
  [[nodiscard]] constexpr auto keys() const noexcept -> std::span<key_type const> {
    return m_set.keys();
  }

  /**
   * @brief Iterator to the first key.
   *
   * @return Iterator to the start of the dense key range.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return m_set.begin();
  }

  /**
   * @brief Iterator one past the last key.
   *
   * @return Iterator to the end of the dense key range.
   *
   * @pre None.
   * @post None. The set is not modified.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return m_set.end();
  }

  /// @copydoc begin()
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return m_set.begin();
  }

  /// @copydoc end()
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return m_set.end();
  }

private:
  sparse_set<key_type> m_set;
};

}  // namespace nexenne::container
