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
 * Each slot caches its key's hash and an empty / occupied state; an empty slot
 * terminates a lookup. Erase uses backward-shift deletion: it empties the slot
 * and moves back the entries that followed it in the same probe run, so the
 * table never holds a tombstone and a probe run is always as short as the live
 * entries make it. The entry is stored in place, alive only while its slot is
 * occupied, so a slot costs the hash, the state byte, and the key-value pair
 * (24 bytes for an \c int to \c int map on a 64-bit target). The table is a
 * power of two in size (so the bucket index is a mask, not a modulo), starts at
 * 16 slots, doubles on growth, and rehashes when the live entry count reaches
 * 7/8 of the slots. Reach for it as a general hashable-key map in hot paths;
 * use \c dense_map when the keys are dense integers. Every operation is
 * \c noexcept exactly when the element, hasher and key-equality code it runs
 * is; allocation failure terminates. \p Value must be move-constructible.
 *
 * The value of an entry may be changed in place through an iterator, but the key
 * must not be: a slot caches its key's hash and probe position, so rewriting a
 * key through an iterator leaves it unfindable and breaks later probes and
 * erases. Treat the key reached through an iterator as read-only (the same
 * caller contract as \c flat_map's ordering key).
 *
 * Invalidation: a rehash (from a growing insert, \c reserve or
 * \c shrink_to_fit) invalidates every iterator, pointer and reference. An erase
 * invalidates those to the erased entry and to every entry after it in the same
 * probe run (up to the next empty slot), which may each move back; every other
 * entry keeps its address. Do not erase while iterating: collect the keys, then
 * erase them.
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

namespace detail {

/// @cond INTERNAL

/**
 * @brief Whether a hash and an equality functor both admit a heterogeneous probe.
 *
 * True only when both opt into transparency by exposing \c is_transparent.
 *
 * @tparam H Hash functor type.
 * @tparam E Equality functor type.
 */
template <typename H, typename E>
concept transparent_hash_pair = requires {
  typename H::is_transparent;
  typename E::is_transparent;
};

/// @endcond

}  // namespace detail

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
  using key_type = Key;                      ///< Type of the keys.
  using mapped_type = Value;                 ///< Type of the mapped values.
  using value_type = std::pair<Key, Value>;  ///< A stored key-value entry.
  using size_type = std::size_t;             ///< Unsigned type for sizes and counts.
  using difference_type = std::ptrdiff_t;    ///< Signed distance between two iterators.
  using hasher = Hash;                       ///< Hash functor over the keys.
  using key_equal = KeyEq;                   ///< Equality predicate over the keys.

  static constexpr size_type initial_capacity{16};  ///< Slot count of the first allocation.

private:
  enum class slot_state : std::uint8_t {
    empty,
    occupied
  };

  /**
   * @brief One table cell: a cached hash, an occupancy state and the entry.
   *
   * The entry lives in an anonymous union so it carries no engaged flag of its
   * own: the state already says whether it is alive (exactly while the state is
   * occupied). The slot runs the entry's constructor and destructor by hand and
   * is the only code that names the union member; the table goes through
   * \c value(), \c construct() and \c destroy().
   *
   * @pre None.
   * @post A default-constructed slot is empty.
   */
  struct slot {
    std::size_t cached_hash{0};
    slot_state state{slot_state::empty};

    union {
      value_type entry;  ///< The live key-value entry while the slot is occupied.
    };

    /**
     * @brief Constructs an empty slot that holds no entry.
     *
     * @pre None.
     * @post \c state is \c slot_state::empty and no entry is alive.
     */
    slot() noexcept {}

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

    /**
     * @brief Copies \p other, copying its entry when it holds one.
     *
     * @param other Slot to copy.
     *
     * @pre None.
     * @post This slot has \p other's state and hash, and a copy of its entry
     *       when it is occupied.
     */
    slot(slot const& other) noexcept(std::is_nothrow_copy_constructible_v<value_type>)
      requires std::copy_constructible<value_type>
        : cached_hash{other.cached_hash}, state{other.state} {
      if (state == slot_state::occupied) {
        std::construct_at(std::addressof(entry), other.entry);
      }
    }

    /**
     * @brief Moves \p other's entry, when it holds one, into this slot.
     *
     * @param other Slot to move from; its entry is left moved-from but alive.
     *
     * @pre None.
     * @post This slot has \p other's state and hash, and its entry when it is
     *       occupied.
     */
    slot(slot&& other) noexcept(std::is_nothrow_move_constructible_v<value_type>)
        : cached_hash{other.cached_hash}, state{other.state} {
      if (state == slot_state::occupied) {
        std::construct_at(std::addressof(entry), std::move(other.entry));
      }
    }

    /**
     * @brief Slots are never assigned; the table only builds and swaps them.
     *
     * @return Not applicable: deleted.
     *
     * @pre None.
     * @post None.
     */
    auto operator=(slot const&) -> slot& = delete;

    /**
     * @brief Slots are never assigned; the table only builds and swaps them.
     *
     * @return Not applicable: deleted.
     *
     * @pre None.
     * @post None.
     */
    auto operator=(slot&&) -> slot& = delete;

    /**
     * @brief Destroys the entry when the slot holds one.
     *
     * @pre None.
     * @post No entry is alive.
     */
    ~slot() {
      if (state == slot_state::occupied) {
        std::destroy_at(std::addressof(entry));
      }
    }

    /**
     * @brief The live entry.
     *
     * @return A reference to the entry.
     *
     * @pre \c state is \c slot_state::occupied.
     * @post None.
     */
    [[nodiscard]] auto value() noexcept -> value_type& {
      return entry;
    }

    /**
     * @brief The live entry, read-only.
     *
     * @return A const reference to the entry.
     *
     * @pre \c state is \c slot_state::occupied.
     * @post None.
     */
    [[nodiscard]] auto value() const noexcept -> value_type const& {
      return entry;
    }

    /**
     * @brief Builds the entry in this slot from \p args.
     *
     * @tparam Args Constructor argument types for the entry.
     * @param args Arguments forwarded to the entry's constructor.
     *
     * @pre No entry is alive in this slot.
     * @post The entry is alive; \c state is left for the caller to set.
     */
    template <typename... Args>
    auto construct(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>)
      -> void {
      std::construct_at(std::addressof(entry), std::forward<Args>(args)...);
    }

    /**
     * @brief Destroys the live entry.
     *
     * @pre The entry is alive.
     * @post No entry is alive; \c state is left for the caller to set.
     */
    auto destroy() noexcept -> void {
      std::destroy_at(std::addressof(entry));
    }

    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  };

  std::vector<slot> m_slots;
  size_type m_size{0};
  [[no_unique_address]] Hash m_hash{};
  [[no_unique_address]] KeyEq m_eq{};

  template <typename, typename, typename, typename, typename, typename>
  friend class bimap;

  /**
   * @brief Whether hashing a \p K probe and comparing it with a key is nothrow.
   *
   * @tparam K Probe type: the key type itself, or a transparent probe.
   */
  template <typename K>
  static constexpr bool nothrow_probe_v{
    detail::nothrow_invocable_v<Hash&, K const&>
    && detail::nothrow_invocable_v<Hash const&, K const&>
    && detail::nothrow_invocable_v<KeyEq const&, Key const&, K const&>
  };

  /// @brief Whether moving an entry into another slot is nothrow.
  static constexpr bool nothrow_relocate_v{
    std::is_nothrow_move_constructible_v<Key> && std::is_nothrow_move_constructible_v<Value>
  };

  /// @brief Whether exchanging the stored hasher and key equality is nothrow.
  static constexpr bool nothrow_swap_functors_v{
    std::is_nothrow_swappable_v<Hash> && std::is_nothrow_swappable_v<KeyEq>
  };

  /**
   * @brief The smallest power of two at least \p n, clamped to \c 1.
   *
   * @param n Lower bound the result must reach.
   *
   * @return The smallest power of two not less than \c max(n, 1).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto next_pow2(size_type const n) noexcept -> size_type {
    return n < 2 ? 1 : std::bit_ceil(n);
  }

  /**
   * @brief The bucket index for a hash value.
   *
   * Fibonacci hashing: \p h is multiplied by \c 2^bits divided by the golden
   * ratio and the top \c log2(capacity) bits are kept. Masking the low bits
   * instead would send every key that differs only in its high bits (an id in
   * the upper word, an aligned pointer) to one bucket, and the default
   * \c std::hash of an integer is the identity.
   *
   * @param h Hash value to reduce to a bucket.
   *
   * @return The starting bucket index for \p h, or \c 0 when the table has
   *         fewer than two slots.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bucket_of(std::size_t const h) const noexcept -> size_type {
    if (m_slots.size() < 2) {
      return 0;
    }
    constexpr auto golden{
      static_cast<std::size_t>(sizeof(std::size_t) >= 8 ? 0x9e3779b97f4a7c15ULL : 0x9e3779b9ULL)
    };
    auto const shift{std::numeric_limits<std::size_t>::digits - std::countr_zero(m_slots.size())};
    return (h * golden) >> shift;
  }

  /**
   * @brief The 7/8 load limit, computed without floating point.
   *
   * @return The live entry count that triggers a rehash.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto load_threshold() const noexcept -> size_type {
    return (m_slots.size() * 7) / 8;
  }

  /**
   * @brief Grows the table so \p desired_entries fit under the load limit.
   *
   * With no tombstones only live entries fill the table, so the trigger fires
   * only when the table genuinely needs to grow; churn at a constant live size
   * never rehashes. Keeping the live count at or below 7/8 of the slots
   * guarantees the empty slot that terminates every probe.
   *
   * @param desired_entries Live entry count the table must accommodate.
   *
   * @pre None.
   * @post A terminating empty slot is guaranteed for \p desired_entries entries; a
   *       rehash, if triggered, invalidates iterators, pointers, and references.
   */
  auto ensure_capacity_for(size_type const desired_entries) noexcept(nothrow_relocate_v) -> void {
    if (m_slots.empty()) {
      rehash(std::max<size_type>(initial_capacity, next_pow2(desired_entries * 8 / 7 + 1)));
      return;
    }
    if (desired_entries > load_threshold()) {
      rehash(m_slots.size() * 2);
    }
  }

  /**
   * @brief Rebuilds the slot array at (the next power of two of) a new size.
   *
   * Live entries are re-inserted into the fresh array (reusing their cached
   * hashes), so the count is restored from scratch.
   *
   * @param new_bucket_count Requested slot count, rounded up to a power of two.
   *
   * @pre \p new_bucket_count leaves room for every live entry below the load
   *      limit.
   * @post The table holds the same live entries; iterators, pointers, and
   *       references are invalidated.
   */
  auto rehash(size_type const new_bucket_count) noexcept(nothrow_relocate_v) -> void {
    auto old_slots{std::move(m_slots)};
    m_slots = std::vector<slot>(next_pow2(new_bucket_count));
    m_size = 0;
    for (auto& old : old_slots) {
      if (old.state == slot_state::occupied) {
        place_absent(old.cached_hash, std::move(old.value().first), std::move(old.value().second));
      }
    }
  }

  /**
   * @brief Inserts an absent \p key at the first free slot of its probe.
   *
   * The caller has already probed with the same hash and missed, so the first
   * empty slot from \c bucket_of(h) is where the key belongs; no equality walk
   * and no second hash are needed.
   *
   * @param h Precomputed hash of \p key.
   * @param key Key to insert, moved into the new entry.
   * @param value Value to store, moved into the new entry.
   *
   * @return A reference to the new entry's value.
   *
   * @pre \p key is absent, \p h is its hash, and a free slot exists (the caller
   *      ensured capacity).
   * @post \c size() grew by one and \p key maps to \p value.
   */
  auto place_absent(std::size_t const h, Key key, Value value) noexcept(nothrow_relocate_v)
    -> Value& {
    auto index{bucket_of(h)};
    while (m_slots[index].state == slot_state::occupied) {
      index = (index + 1) & (m_slots.size() - 1);
    }
    auto& target{m_slots[index]};
    target.construct(std::move(key), std::move(value));
    target.state = slot_state::occupied;
    target.cached_hash = h;
    ++m_size;
    return target.value().second;
  }

  /**
   * @brief Probes for \p key and returns its slot, or \c nullptr on a miss.
   *
   * Accepts the key itself or a heterogeneous probe comparable through \c m_hash
   * and \c m_eq (when both functors are transparent). Hashes \p key once.
   *
   * @tparam K Probe type hashable and comparable through the functors.
   * @param key Key or probe to search for.
   *
   * @return A pointer to the occupied slot holding \p key, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  template <typename K>
  [[nodiscard]] auto probe_slot(K const& key) const noexcept(nothrow_probe_v<K>) -> slot const* {
    if (m_slots.empty()) {
      return nullptr;
    }
    return probe_slot(key, m_hash(key));
  }

  /**
   * @brief Probes for \p key, whose hash is \p h, without hashing it again.
   *
   * @tparam K Probe type comparable through \c m_eq.
   * @param key Key or probe to search for.
   * @param h The hash of \p key.
   *
   * @return A pointer to the occupied slot holding \p key, or \c nullptr.
   *
   * @pre \p h equals \c m_hash(key).
   * @post None.
   */
  template <typename K>
  [[nodiscard]] auto probe_slot(
    K const& key, std::size_t const h
  ) const noexcept(detail::nothrow_invocable_v<KeyEq const&, Key const&, K const&>) -> slot const* {
    if (m_slots.empty()) {
      return nullptr;
    }
    auto index{bucket_of(h)};
    while (true) {
      auto const& current{m_slots[index]};
      if (current.state == slot_state::empty) {
        return nullptr;
      }
      if (current.cached_hash == h && m_eq(current.value().first, key)) {
        return std::addressof(current);
      }
      index = (index + 1) & (m_slots.size() - 1);
    }
  }

  /**
   * @brief Probes for the exact key \p key and returns its slot.
   *
   * @param key Key to search for.
   *
   * @return A pointer to the occupied slot holding \p key, or \c nullptr on a miss.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto find_slot(Key const& key) const noexcept(nothrow_probe_v<Key>) -> slot const* {
    return probe_slot(key);
  }

  /**
   * @brief The mutable slot a probe (which only has const access) located.
   *
   * @param found Slot returned by a probe.
   *
   * @return A mutable reference to the same slot.
   *
   * @pre \p found is non-null and points into this map's slot array.
   * @post None.
   */
  [[nodiscard]] auto mutable_slot(slot const* const found) noexcept -> slot& {
    return m_slots[static_cast<size_type>(found - m_slots.data())];
  }

  /**
   * @brief Erases the slot a probe located by backward shift, for every erase.
   *
   * Empties the slot, then walks the rest of its probe run (up to the next
   * empty slot). An entry whose home bucket lies cyclically at or before the
   * hole may fill it: it moves back into the hole, and its old slot becomes the
   * new hole. An entry whose home lies after the hole stays, since moving it
   * before its home would hide it from its own probe. Every probe run so stays
   * unbroken with no tombstone.
   *
   * @param found Slot returned by a probe, or \c nullptr for a miss.
   *
   * @return \c true when an entry was erased, \c false when \p found was
   *         \c nullptr.
   *
   * @pre \p found, when non-null, points into this map's slot array.
   * @post On \c true \c size() shrank by one; entries that followed the erased
   *       one in its probe run may have moved back.
   */
  auto erase_slot(slot const* const found) noexcept(nothrow_relocate_v) -> bool {
    if (found == nullptr) {
      return false;
    }
    auto const mask{m_slots.size() - 1};
    auto hole{static_cast<size_type>(found - m_slots.data())};
    m_slots[hole].destroy();
    m_slots[hole].state = slot_state::empty;
    for (auto next{(hole + 1) & mask}; m_slots[next].state == slot_state::occupied;
         next = (next + 1) & mask) {
      auto& candidate{m_slots[next]};
      // The hole lies in [home, next) exactly when the candidate is at least as
      // far from its home as from the hole (distances taken cyclically).
      auto const home{bucket_of(candidate.cached_hash)};
      if (((next - home) & mask) >= ((next - hole) & mask)) {
        auto& target{m_slots[hole]};
        target.construct(std::move(candidate.value()));
        target.cached_hash = candidate.cached_hash;
        target.state = slot_state::occupied;
        candidate.destroy();
        candidate.state = slot_state::empty;
        hole = next;
      }
    }
    --m_size;
    return true;
  }

  template <bool IsConst>
  class basic_iterator {
  private:
    using slot_ptr = std::conditional_t<IsConst, slot const*, slot*>;
    slot_ptr m_current{nullptr};
    slot_ptr m_end{nullptr};

    /**
     * @brief Advances the cursor to the next occupied slot, or to the end.
     *
     * @pre \c m_current and \c m_end bound a valid slot range.
     * @post \c m_current refers to an occupied slot or equals \c m_end.
     */
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

    /**
     * @brief Constructs a singular iterator not tied to any map.
     *
     * @pre None.
     * @post The iterator is singular and not dereferenceable.
     */
    constexpr basic_iterator() noexcept = default;

    /**
     * @brief Constructs an iterator over the slot range \c [current, end).
     *
     * @param current Slot the iterator starts at, advanced to the first occupied
     *                slot.
     * @param end One past the last slot to walk.
     *
     * @pre \p current and \p end bound a valid slot range.
     * @post The iterator refers to the first occupied slot at or after \p current,
     *       or to \p end when none remains.
     */
    constexpr basic_iterator(slot_ptr const current, slot_ptr const end) noexcept
        : m_current{current}, m_end{end} {
      advance_to_occupied();
    }

    /**
     * @brief Converts a mutable iterator into a const iterator.
     *
     * @tparam OtherConst Constness of the source iterator; enabled only when it is
     *                    non-const and this iterator is const.
     * @param other Mutable iterator to copy the position from.
     *
     * @pre None.
     * @post This iterator refers to the same slot as \p other.
     */
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_current{other.m_current}, m_end{other.m_end} {}

    /**
     * @brief The \c (key, value) entry the iterator refers to.
     *
     * @return A reference to the entry in the current slot.
     *
     * @pre The iterator is dereferenceable (not \c end()).
     * @post None.
     */
    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return m_current->value();
    }

    /**
     * @brief Member access to the \c (key, value) entry.
     *
     * @return A pointer to the entry in the current slot.
     *
     * @pre The iterator is dereferenceable (not \c end()).
     * @post None.
     */
    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(m_current->value());
    }

    /**
     * @brief Advances to the next occupied slot.
     *
     * @return A reference to this iterator after advancing.
     *
     * @pre The iterator is dereferenceable (not \c end()).
     * @post The iterator refers to the next occupied slot or to \c end().
     */
    constexpr auto operator++() noexcept -> basic_iterator& {
      ++m_current;
      advance_to_occupied();
      return *this;
    }

    /**
     * @brief Advances to the next occupied slot, returning the prior position.
     *
     * @return A copy of the iterator before it advanced.
     *
     * @pre The iterator is dereferenceable (not \c end()).
     * @post The iterator refers to the next occupied slot or to \c end().
     */
    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const copy{*this};
      ++*this;
      return copy;
    }

    /**
     * @brief Whether \p a and \p b refer to the same slot.
     *
     * @param a First iterator.
     * @param b Second iterator.
     *
     * @return \c true when both point at the same slot.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_current == b.m_current;
    }

    template <bool>
    friend class basic_iterator;
  };

public:
  using iterator = basic_iterator<false>;       ///< Forward iterator over mutable entries.
  using const_iterator = basic_iterator<true>;  ///< Forward iterator over read-only entries.

  /**
   * @brief Constructs an empty map with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is zero.
   */
  flat_hash_map() noexcept(
    std::is_nothrow_default_constructible_v<Hash> && std::is_nothrow_default_constructible_v<KeyEq>
  ) = default;

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
  explicit flat_hash_map(size_type const expected_entries) noexcept(
    std::is_nothrow_default_constructible_v<Hash> && std::is_nothrow_default_constructible_v<KeyEq>
  ) {
    if (expected_entries > 0) {
      rehash(next_pow2(expected_entries * 8 / 7 + 1));
    }
  }

  /**
   * @brief Copies \p other's entries, hasher, and predicate.
   *
   * @param other Map to copy.
   *
   * @pre None.
   * @post This map equals \p other and has the same capacity.
   */
  flat_hash_map(flat_hash_map const& other) = default;

  /**
   * @brief Takes \p other's table, leaving \p other empty.
   *
   * Written out rather than defaulted: a defaulted move would move the slot
   * vector but copy the counters, leaving \p other reporting its old size over
   * an empty table.
   *
   * @param other Map to move from.
   *
   * @pre None.
   * @post This map holds \p other's former entries; \p other is empty, with no
   *       allocated storage and a default-constructed hasher and predicate.
   */
  flat_hash_map(flat_hash_map&& other) noexcept(
    std::is_nothrow_default_constructible_v<Hash> && std::is_nothrow_default_constructible_v<KeyEq>
    && nothrow_swap_functors_v
  ) {
    swap(other);
  }

  /**
   * @brief Replaces the contents with those of \p other (copy-and-swap).
   *
   * @param other Map to take the state of, copied or moved in by the caller.
   *
   * @return \c *this.
   *
   * @pre None.
   * @post This map holds what \p other held; a moved-from source is empty.
   */
  auto operator=(flat_hash_map other) noexcept(nothrow_swap_functors_v) -> flat_hash_map& {
    swap(other);
    return *this;
  }

  /**
   * @brief Destroys every entry and releases the table.
   *
   * @pre None.
   * @post None.
   */
  ~flat_hash_map() = default;

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
   * Inserting until the map holds \p n entries then rehashes nothing.
   *
   * @param n Minimum entry capacity to ensure.
   *
   * @pre None.
   * @post The map holds up to \p n entries without a rehash; a rehash here, if
   *       triggered, invalidates iterators, pointers, and references.
   */
  auto reserve(size_type const n) noexcept(nothrow_relocate_v) -> void {
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
      if (current.state == slot_state::occupied) {
        current.destroy();
      }
      current.state = slot_state::empty;
    }
    m_size = 0;
  }

  /**
   * @brief Releases slot capacity not needed for the current entries.
   *
   * @pre None.
   * @post \c size() is unchanged; when the table shrank, iterators, pointers,
   *       and references are invalidated.
   */
  auto shrink_to_fit() noexcept(nothrow_relocate_v) -> void {
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
  auto swap(flat_hash_map& other) noexcept(nothrow_swap_functors_v) -> void {
    using std::swap;
    m_slots.swap(other.m_slots);
    swap(m_size, other.m_size);
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
  friend auto swap(flat_hash_map& a, flat_hash_map& b) noexcept(nothrow_swap_functors_v) -> void {
    a.swap(b);
  }

  /**
   * @brief Inserts \p key mapping to \p value, leaving an existing key
   *        unchanged.
   *
   * The key is probed before the table grows, so a \c false return never
   * rehashes or invalidates a reference to any element.
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
   * @note Hashes \p key once.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(Key key, Value value) noexcept(nothrow_probe_v<Key> && nothrow_relocate_v) -> bool {
    auto const h{m_hash(key)};
    if (probe_slot(key, h) != nullptr) {
      return false;
    }
    ensure_capacity_for(m_size + 1);
    place_absent(h, std::move(key), std::move(value));
    return true;
  }

  /**
   * @brief Inserts \p key mapping to \p value, overwriting an existing value.
   *
   * An existing value is overwritten in place without growing the table, so an
   * assignment never rehashes or invalidates a reference to another entry.
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
   * @note Hashes \p key once.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert_or_assign(Key key, Value value) noexcept(
    nothrow_probe_v<Key> && nothrow_relocate_v && std::is_nothrow_move_assignable_v<Value>
  ) -> bool {
    auto const h{m_hash(key)};
    if (auto const* const found{probe_slot(key, h)}) {
      mutable_slot(found).value().second = std::move(value);
      return false;
    }
    ensure_capacity_for(m_size + 1);
    place_absent(h, std::move(key), std::move(value));
    return true;
  }

  /**
   * @brief Constructs the value in place for \p key, leaving an existing key
   *        unchanged.
   *
   * The key is probed first, so an existing key neither constructs a discarded
   * value nor rehashes; the value is built only on a real insertion.
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
   * @note Hashes \p key once.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<Value, Args...>
  auto emplace(Key key, Args&&... args) noexcept(
    nothrow_probe_v<Key> && nothrow_relocate_v && std::is_nothrow_constructible_v<Value, Args...>
  ) -> bool {
    auto const h{m_hash(key)};
    if (probe_slot(key, h) != nullptr) {
      return false;
    }
    ensure_capacity_for(m_size + 1);
    place_absent(h, std::move(key), Value(std::forward<Args>(args)...));
    return true;
  }

  /**
   * @brief Inserts an entry for \p key with a value built from \p args, only when
   *        \p key is absent.
   *
   * The value is constructed only on a fresh insertion, so an existing entry is
   * left untouched, its \p args unused, and no rehash is triggered.
   *
   * @tparam Args Constructor argument types for \p Value.
   * @param key Key to insert under.
   * @param args Arguments forwarded to \p Value's constructor on insertion.
   *
   * @return \c true on a fresh insertion, \c false when \p key was already
   *         present.
   *
   * @pre None.
   * @post \p key is present; on a fresh insertion \c size() grew by one and a
   *       rehash may have invalidated iterators and references.
   *
   * @note Hashes \p key once.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename... Args>
    requires std::constructible_from<Value, Args...>
  auto try_emplace(Key key, Args&&... args) noexcept(
    nothrow_probe_v<Key> && nothrow_relocate_v && std::is_nothrow_constructible_v<Value, Args...>
  ) -> bool {
    auto const h{m_hash(key)};
    if (probe_slot(key, h) != nullptr) {
      return false;
    }
    ensure_capacity_for(m_size + 1);
    place_absent(h, std::move(key), Value(std::forward<Args>(args)...));
    return true;
  }

  /**
   * @brief Removes the entry for \p key.
   *
   * @param key Key to remove.
   *
   * @return \c true on a removal, \c false when \p key was absent.
   *
   * @pre None.
   * @post \p key is absent; on a removal \c size() shrank by one. Entries that
   *       followed the erased one in its probe run may have moved back one or
   *       more slots, so iterators, pointers, and references to the erased
   *       entry and to those entries are invalidated; every other entry keeps
   *       its address.
   *
   * @warning Do not erase while iterating: an entry can move back into a slot
   *          the iteration already passed (it is then skipped), or from the
   *          start of the table to its end (it is then visited twice). Collect
   *          the keys first, then erase them.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase(Key const& key) noexcept(nothrow_probe_v<Key> && nothrow_relocate_v) -> bool {
    return erase_slot(find_slot(key));
  }

  /**
   * @brief Heterogeneous erase of the entry for a probe \p key.
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
   * @param key Key to remove.
   *
   * @return \c true on a removal, \c false when \p key was absent.
   *
   * @pre None.
   * @post \p key is absent; on a removal \c size() shrank by one. Entries that
   *       followed the erased one in its probe run may have moved back one or
   *       more slots, so iterators, pointers, and references to the erased
   *       entry and to those entries are invalidated; every other entry keeps
   *       its address.
   *
   * @warning Do not erase while iterating: an entry can move back into a slot
   *          the iteration already passed (it is then skipped), or from the
   *          start of the table to its end (it is then visited twice). Collect
   *          the keys first, then erase them.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  auto erase(K const& key) noexcept(nothrow_probe_v<K> && nothrow_relocate_v) -> bool {
    return erase_slot(probe_slot(key));
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
  [[nodiscard]] auto find(Key const& key) noexcept(nothrow_probe_v<Key>) -> Value* {
    auto const* const found{find_slot(key)};
    if (found == nullptr) {
      return nullptr;
    }
    return std::addressof(mutable_slot(found).value().second);
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
  [[nodiscard]] auto find(Key const& key) const noexcept(nothrow_probe_v<Key>) -> Value const* {
    auto const* const found{find_slot(key)};
    return found == nullptr ? nullptr : std::addressof(found->value().second);
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
  [[nodiscard]] auto contains(Key const& key) const noexcept(nothrow_probe_v<Key>) -> bool {
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
  [[nodiscard]] auto count(Key const& key) const noexcept(nothrow_probe_v<Key>) -> size_type {
    return contains(key) ? size_type{1} : size_type{0};
  }

  /**
   * @brief Heterogeneous lookup: pointer to the value for a probe \p key.
   *
   * Enabled only when both \c Hash and \c KeyEq are transparent (each exposes
   * \c is_transparent), so a compatible probe type (for example a
   * \c std::string_view against \c std::string keys) is hashed and compared
   * without constructing a \c Key.
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
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
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  [[nodiscard]] auto find(K const& key) noexcept(nothrow_probe_v<K>) -> Value* {
    auto const* const found{probe_slot(key)};
    if (found == nullptr) {
      return nullptr;
    }
    return std::addressof(mutable_slot(found).value().second);
  }

  /**
   * @brief Heterogeneous lookup for a probe \p key, returning a const pointer.
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
   * @param key Key to look up.
   *
   * @return Pointer to the mapped value, or \c nullptr when \p key is absent.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  [[nodiscard]] auto find(K const& key) const noexcept(nothrow_probe_v<K>) -> Value const* {
    auto const* const found{probe_slot(key)};
    return found == nullptr ? nullptr : std::addressof(found->value().second);
  }

  /**
   * @brief Heterogeneous membership test for a probe \p key.
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
   * @param key Key to test.
   *
   * @return \c true when \p key is present.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  [[nodiscard]] auto contains(K const& key) const noexcept(nothrow_probe_v<K>) -> bool {
    return probe_slot(key) != nullptr;
  }

  /**
   * @brief Heterogeneous count for a probe \p key, always \c 0 or \c 1.
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
   * @param key Key to count.
   *
   * @return \c 1 when \p key is present, otherwise \c 0.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  [[nodiscard]] auto count(K const& key) const noexcept(nothrow_probe_v<K>) -> size_type {
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
  [[nodiscard]] auto at(Key const& key) noexcept(nothrow_probe_v<Key>) -> Value* {
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
  [[nodiscard]] auto at(Key const& key) const noexcept(nothrow_probe_v<Key>) -> Value const* {
    return find(key);
  }

  /**
   * @brief Heterogeneous checked access for a probe \p key (an alias for
   *        \c find).
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
   * @param key Key to look up.
   *
   * @return A pointer to the mapped value, or \c nullptr on a miss.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  [[nodiscard]] auto at(K const& key) noexcept(nothrow_probe_v<K>) -> Value* {
    return find(key);
  }

  /**
   * @brief Heterogeneous \c at for a probe \p key, returning a const pointer.
   *
   * @tparam K Probe type hashable and comparable through the transparent
   *           functors.
   * @param key Key to look up.
   *
   * @return Pointer to the mapped value, or \c nullptr when \p key is absent.
   *
   * @pre None.
   * @post None.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename K>
    requires detail::transparent_hash_pair<Hash, KeyEq>
  [[nodiscard]] auto at(K const& key) const noexcept(nothrow_probe_v<K>) -> Value const* {
    return find(key);
  }

  /**
   * @brief Accesses the value for \p key, inserting a default if absent.
   *
   * \p key is hashed once and, on a miss, moved into the new entry, so a
   * move-only \c Key works.
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
  auto operator[](Key key) noexcept(
    nothrow_probe_v<Key> && nothrow_relocate_v && std::is_nothrow_default_constructible_v<Value>
  ) -> Value&
    requires std::default_initializable<Value>
  {
    auto const h{m_hash(key)};
    if (auto const* const found{probe_slot(key, h)}) {
      return mutable_slot(found).value().second;
    }
    ensure_capacity_for(m_size + 1);
    return place_absent(h, std::move(key), Value{});
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
  [[nodiscard]] friend auto operator==(flat_hash_map const& a, flat_hash_map const& b) noexcept(
    nothrow_probe_v<Key> && noexcept(std::declval<Value const&>() == std::declval<Value const&>())
  ) -> bool
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
