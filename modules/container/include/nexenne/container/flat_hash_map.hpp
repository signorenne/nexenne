#pragma once

/**
 * @file
 * @brief Open-addressing hash map with linear probing.
 *
 * \c flat_hash_map<Key, Value, Hash, KeyEq> stores entries in one contiguous
 * vector of slots, resolving collisions by linear probing rather than a node per
 * entry. Compared with \c std::unordered_map that is one allocation instead of
 * many and roughly one cache miss per lookup instead of three, typically several
 * times faster on real workloads, at the cost of losing reference stability on a
 * rehash. Iteration walks the slot array in an unspecified order.
 *
 * Each slot caches its key's hash and carries an empty / occupied / tombstone
 * state so an erase can leave a tombstone (a probe must skip it without stopping)
 * while a fresh empty slot still terminates a lookup. The table is a power of two
 * in size (so the bucket index is a mask, not a modulo), starts at 16 slots,
 * doubles on growth, and rehashes when the occupied-plus-tombstone count reaches
 * 7/8 of the slots. Reach for it as a general hashable-key map in hot paths; use
 * \c dense_map when the keys are dense integers. Every operation is \c noexcept;
 * allocation failure terminates. \p Value must be move-constructible.
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace nexenne::container {

/**
 * @brief Open-addressing, linear-probing hash map.
 *
 * @tparam Key Hashable key type.
 * @tparam Value Mapped type; must be move-constructible.
 * @tparam Hash Hash functor; \c std::hash<Key> by default.
 * @tparam KeyEq Equality predicate; \c std::equal_to<Key> by default.
 *
 * @pre None.
 * @post A default-constructed map is empty with no allocated storage.
 */
template <
  typename Key,
  std::move_constructible Value,
  typename Hash = std::hash<Key>,
  typename KeyEq = std::equal_to<Key>>
class flat_hash_map {
public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<Key, Value>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEq;

  static constexpr size_type initial_capacity{16};

private:
  enum class slot_state : std::uint8_t {
    empty,
    occupied,
    tombstone
  };

  struct slot {
    slot_state state{slot_state::empty};
    std::size_t cached_hash{0};
    std::optional<value_type> entry;
  };

  std::vector<slot> m_slots;
  size_type m_size{0};
  size_type m_tombstones{0};
  [[no_unique_address]] Hash m_hash{};
  [[no_unique_address]] KeyEq m_eq{};

  [[nodiscard]] static constexpr auto next_pow2(size_type const n) noexcept -> size_type {
    return n < 2 ? 1 : std::bit_ceil(n);
  }

  // The bucket index for a hash; the slot count is a power of two when allocated,
  // so this is a mask, not a modulo.
  [[nodiscard]] constexpr auto bucket_of(std::size_t const h) const noexcept -> size_type {
    return m_slots.empty() ? 0 : h & (m_slots.size() - 1);
  }

  // The 7/8 load limit, computed without floating point.
  [[nodiscard]] constexpr auto load_threshold() const noexcept -> size_type {
    return (m_slots.size() * 7) / 8;
  }

  auto ensure_capacity_for(size_type const desired_entries) noexcept -> void {
    if (m_slots.empty()) {
      rehash(std::max<size_type>(initial_capacity, next_pow2(desired_entries * 8 / 7 + 1)));
      return;
    }
    // Tombstones count toward the probe window: a table full of tombstones with
    // no empty slot would probe forever, so keeping occupied + tombstones below
    // the threshold guarantees a terminating empty slot exists.
    if (desired_entries + m_tombstones > load_threshold()) {
      rehash(m_slots.size() * 2);
    }
  }

  auto rehash(size_type const new_bucket_count) noexcept -> void {
    auto old_slots{std::move(m_slots)};
    m_slots = std::vector<slot>(next_pow2(new_bucket_count));  // value-init, no slot copy
    m_size = 0;
    m_tombstones = 0;
    for (auto& old : old_slots) {
      if (old.state == slot_state::occupied) {
        place(old.cached_hash, std::move(old.entry->first), std::move(old.entry->second), false);
      }
    }
  }

  // Probes from bucket_of(h) and either inserts the entry (returning true) or
  // finds an equal key (returning false, overwriting the value when asked). The
  // caller guarantees a terminating empty slot exists.
  auto place(std::size_t const h, Key key, Value value, bool const overwrite) noexcept -> bool {
    auto index{bucket_of(h)};
    auto first_tombstone{m_slots.size()};
    while (true) {
      auto& current{m_slots[index]};
      if (current.state == slot_state::empty) {
        auto& target{first_tombstone < m_slots.size() ? m_slots[first_tombstone] : current};
        if (target.state == slot_state::tombstone) {
          --m_tombstones;
        }
        target.state = slot_state::occupied;
        target.cached_hash = h;
        target.entry.emplace(std::move(key), std::move(value));
        ++m_size;
        return true;
      }
      if (current.state == slot_state::tombstone) {
        if (first_tombstone == m_slots.size()) {
          first_tombstone = index;
        }
      } else if (current.cached_hash == h && m_eq(current.entry->first, key)) {
        if (overwrite) {
          current.entry->second = std::move(value);
        }
        return false;
      }
      index = (index + 1) & (m_slots.size() - 1);
    }
  }

  // Probes for key and returns its slot, or nullptr on a miss.
  [[nodiscard]] auto find_slot(Key const& key) const noexcept -> slot const* {
    if (m_slots.empty()) {
      return nullptr;
    }
    auto const h{m_hash(key)};
    auto index{bucket_of(h)};
    while (true) {
      auto const& current{m_slots[index]};
      if (current.state == slot_state::empty) {
        return nullptr;
      }
      if (current.state == slot_state::occupied && current.cached_hash == h
          && m_eq(current.entry->first, key)) {
        return std::addressof(current);
      }
      index = (index + 1) & (m_slots.size() - 1);
    }
  }

  template <bool IsConst>
  class basic_iterator {
  private:
    using slot_ptr = std::conditional_t<IsConst, slot const*, slot*>;
    slot_ptr m_current{nullptr};
    slot_ptr m_end{nullptr};

    constexpr auto advance_to_occupied() noexcept -> void {
      while (m_current != m_end && m_current->state != slot_state::occupied) {
        ++m_current;
      }
    }

  public:
    using value_type = std::pair<Key, Value>;
    using reference = std::conditional_t<IsConst, value_type const&, value_type&>;
    using pointer = std::conditional_t<IsConst, value_type const*, value_type*>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    constexpr basic_iterator() noexcept = default;

    constexpr basic_iterator(slot_ptr const current, slot_ptr const end) noexcept
        : m_current{current}, m_end{end} {
      advance_to_occupied();
    }

    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_current{other.m_current}, m_end{other.m_end} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return *m_current->entry;
    }

    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(*m_current->entry);
    }

    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_current;
      advance_to_occupied();
      return *this;
    }

    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const copy{*this};
      ++*this;
      return copy;
    }

    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_current == b.m_current;
    }

    template <bool>
    friend class basic_iterator;
  };

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  /**
   * @brief Constructs an empty map with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is zero.
   */
  flat_hash_map() noexcept = default;

  /**
   * @brief Constructs an empty map sized for \p expected_entries.
   *
   * @param expected_entries Entries to size the table for before the first
   *                         rehash.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() admits at least
   *       \p expected_entries entries without rehashing.
   */
  explicit flat_hash_map(size_type const expected_entries) noexcept {
    if (expected_entries > 0) {
      rehash(next_pow2(expected_entries * 8 / 7 + 1));
    }
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
    return m_size;
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
   * @brief Number of slots before the next rehash.
   *
   * @return The slot count, a power of two or zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_slots.size();
  }

  /**
   * @brief The largest number of slots the map can hold.
   *
   * @return The maximum slot count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_slots.max_size();
  }

  /**
   * @brief Ratio of live entries to slots.
   *
   * @return The load factor in \c [0, 1), or zero when unallocated.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto load_factor() const noexcept -> double {
    return m_slots.empty() ? 0.0
                           : static_cast<double>(m_size) / static_cast<double>(m_slots.size());
  }

  /**
   * @brief Reserves storage for at least \p n entries.
   *
   * @param n Minimum entry capacity to ensure.
   *
   * @pre None.
   * @post Capacity admits at least \p n entries; a rehash, if triggered,
   *       invalidates iterators, pointers, and references.
   */
  auto reserve(size_type const n) noexcept -> void {
    if (n == 0) {
      return;
    }
    auto const needed{next_pow2(n * 8 / 7 + 1)};
    if (needed > m_slots.size()) {
      rehash(needed);
    }
  }

  /**
   * @brief Removes every entry; slot capacity is retained.
   *
   * @pre None.
   * @post \c empty() is \c true.
   *
   * @complexity \c O(capacity).
   */
  auto clear() noexcept -> void {
    for (auto& current : m_slots) {
      current.state = slot_state::empty;
      current.entry.reset();
    }
    m_size = 0;
    m_tombstones = 0;
  }

  /**
   * @brief Releases slot capacity not needed for the current entries.
   *
   * @pre None.
   * @post \c size() is unchanged and tombstones are cleared; iterators,
   *       pointers, and references are invalidated.
   */
  auto shrink_to_fit() noexcept -> void {
    if (m_size == 0) {
      m_slots.clear();
      m_slots.shrink_to_fit();
      return;
    }
    auto const target{next_pow2(m_size * 8 / 7 + 1)};
    if (target < m_slots.size()) {
      rehash(target);
    }
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Map to exchange state with.
   *
   * @pre None.
   * @post This map and \p other have exchanged entries, hashers, and predicates.
   *
   * @complexity \c O(1).
   */
  auto swap(flat_hash_map& other) noexcept -> void {
    using std::swap;
    m_slots.swap(other.m_slots);
    swap(m_size, other.m_size);
    swap(m_tombstones, other.m_tombstones);
    swap(m_hash, other.m_hash);
    swap(m_eq, other.m_eq);
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
  friend auto swap(flat_hash_map& a, flat_hash_map& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Inserts \p key mapping to \p value, leaving an existing key
   *        unchanged.
   *
   * @param key Key to insert.
   * @param value Value to store on a fresh insertion.
   *
   * @return \c true on a fresh insertion, \c false when \p key was already
   *         present (its value is left as is).
   *
   * @pre None.
   * @post \p key is present; on a fresh insertion \c size() grew by one and a
   *       rehash may have invalidated iterators and references.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(Key key, Value value) noexcept -> bool {
    ensure_capacity_for(m_size + 1);
    auto const h{m_hash(key)};
    return place(h, std::move(key), std::move(value), false);
  }

  /**
   * @brief Inserts \p key mapping to \p value, overwriting an existing value.
   *
   * @param key Key to insert or update.
   * @param value Value to store.
   *
   * @return \c true on a fresh insertion, \c false when an existing value was
   *         overwritten.
   *
   * @pre None.
   * @post \p key maps to \p value; on a fresh insertion \c size() grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert_or_assign(Key key, Value value) noexcept -> bool {
    ensure_capacity_for(m_size + 1);
    auto const h{m_hash(key)};
    return place(h, std::move(key), std::move(value), true);
  }

  /**
   * @brief Constructs the value in place for \p key, leaving an existing key
   *        unchanged.
   *
   * @tparam Args Constructor argument types for \p Value.
   * @param key Key to insert.
   * @param args Arguments forwarded to \p Value's constructor.
   *
   * @return \c true on a fresh insertion, \c false when \p key was already
   *         present.
   *
   * @pre None.
   * @post \p key is present; on a fresh insertion \c size() grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<Value, Args...>
  auto emplace(Key key, Args&&... args) noexcept -> bool {
    return insert(std::move(key), Value(std::forward<Args>(args)...));
  }

  /**
   * @brief Removes the entry for \p key.
   *
   * @param key Key to remove.
   *
   * @return \c true on a removal, \c false when \p key was absent.
   *
   * @pre None.
   * @post \p key is absent; on a removal \c size() shrank by one and a tombstone
   *       is left in place.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase(Key const& key) noexcept -> bool {
    auto const* const found{find_slot(key)};
    if (found == nullptr) {
      return false;
    }
    auto& target{m_slots[static_cast<size_type>(found - m_slots.data())]};
    target.state = slot_state::tombstone;
    target.entry.reset();
    --m_size;
    ++m_tombstones;
    return true;
  }

  /**
   * @brief Pointer to the value for \p key, or \c nullptr on a miss.
   *
   * @param key Key to look up.
   *
   * @return A pointer to the mapped value, or \c nullptr; invalidated by a
   *         rehash.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto find(Key const& key) noexcept -> Value* {
    auto const* const found{find_slot(key)};
    if (found == nullptr) {
      return nullptr;
    }
    // find_slot is const; re-index the mutable slot vector for a mutable value.
    return std::addressof(m_slots[static_cast<size_type>(found - m_slots.data())].entry->second);
  }

  /**
   * @brief Const pointer to the value for \p key, or \c nullptr on a miss.
   *
   * @param key Key to look up.
   *
   * @return A const pointer to the mapped value, or \c nullptr; invalidated by a
   *         rehash.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto find(Key const& key) const noexcept -> Value const* {
    auto const* const found{find_slot(key)};
    return found == nullptr ? nullptr : std::addressof(found->entry->second);
  }

  /**
   * @brief Reports whether \p key is present.
   *
   * @param key Key to test.
   *
   * @return \c true when \p key is present.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto contains(Key const& key) const noexcept -> bool {
    return find_slot(key) != nullptr;
  }

  /**
   * @brief Number of entries for \p key, always \c 0 or \c 1.
   *
   * @param key Key to count.
   *
   * @return \c 1 when \p key is present, otherwise \c 0.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto count(Key const& key) const noexcept -> size_type {
    return contains(key) ? size_type{1} : size_type{0};
  }

  /**
   * @brief Checked access to the value for \p key (an alias for \c find).
   *
   * @param key Key to look up.
   *
   * @return A pointer to the mapped value, or \c nullptr on a miss.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto at(Key const& key) noexcept -> Value* {
    return find(key);
  }

  /**
   * @brief Const checked access to the value for \p key (an alias for \c find).
   *
   * @param key Key to look up.
   *
   * @return A const pointer to the mapped value, or \c nullptr on a miss.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto at(Key const& key) const noexcept -> Value const* {
    return find(key);
  }

  /**
   * @brief Accesses the value for \p key, inserting a default if absent.
   *
   * @param key Key whose value to access or create.
   *
   * @return A mutable reference to the value mapped to \p key.
   *
   * @pre None.
   * @post An entry for \p key exists; on insertion \c size() grew by one and a
   *       rehash may have invalidated other iterators and references.
   *
   * @complexity Amortised \c O(1).
   */
  auto operator[](Key key) noexcept -> Value&
    requires std::default_initializable<Value>
  {
    if (auto* const existing{find(key)}) {
      return *existing;
    }
    // Insert a copy so key stays valid for the lookup of the new slot below.
    static_cast<void>(insert(key, Value{}));
    return *find(key);
  }

  /**
   * @brief Iterator to the first occupied slot.
   *
   * @return An iterator to a live entry, or \c end(); the order is unspecified.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto begin() noexcept -> iterator {
    return iterator{m_slots.data(), m_slots.data() + m_slots.size()};
  }

  /**
   * @brief Iterator one past the last occupied slot.
   *
   * @return A past-the-end iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto end() noexcept -> iterator {
    return iterator{m_slots.data() + m_slots.size(), m_slots.data() + m_slots.size()};
  }

  /// @copydoc begin()
  [[nodiscard]] auto begin() const noexcept -> const_iterator {
    return const_iterator{m_slots.data(), m_slots.data() + m_slots.size()};
  }

  /// @copydoc end()
  [[nodiscard]] auto end() const noexcept -> const_iterator {
    return const_iterator{m_slots.data() + m_slots.size(), m_slots.data() + m_slots.size()};
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
   * @brief The stored hash functor.
   *
   * @return A const reference to the hasher.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto hash_function() const noexcept -> Hash const& {
    return m_hash;
  }

  /**
   * @brief The stored key-equality predicate.
   *
   * @return A const reference to the predicate.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto key_eq() const noexcept -> KeyEq const& {
    return m_eq;
  }

  /**
   * @brief Order-independent equality over the entry sets.
   *
   * @param a First map.
   * @param b Second map.
   *
   * @return \c true when both hold exactly the same key-value entries.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(n) average.
   */
  [[nodiscard]] friend auto
  operator==(flat_hash_map const& a, flat_hash_map const& b) noexcept -> bool
    requires std::equality_comparable<Value>
  {
    if (a.m_size != b.m_size) {
      return false;
    }
    for (auto const& [key, value] : a) {
      auto const* const other{b.find(key)};
      if (other == nullptr || !(*other == value)) {
        return false;
      }
    }
    return true;
  }
};

}  // namespace nexenne::container
