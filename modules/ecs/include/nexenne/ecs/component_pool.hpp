#pragma once

/**
 * @file
 * @brief Pointer-stable, tombstoning component pool for nexenne::ecs.
 *
 * \c detail::component_pool<T> is the per-component storage behind the registry:
 * a sparse array mapping an entity index to a slot, plus a
 * \c container::stable_vector of slots that hold the components. Because the
 * slab is a \c stable_vector, a \c T& obtained from the pool keeps its address
 * for the component's whole lifetime, even when other components are inserted or
 * removed (the slab never reallocates or moves an element). That is what lets a
 * caller add to or remove from a pool while iterating it without dangling a
 * reference, the property a flat \c vector based pool cannot offer.
 *
 * Removal is in place (tombstoning), not swap-pop: erasing a component clears
 * its slot (destroying the value and freeing its resources) but leaves the slot
 * where it is, so every other component keeps its position and address. The
 * freed slot goes on a free list and is reused by the next insert. Iteration
 * walks the slot range and skips the empty (tombstone) slots.
 *
 * The trade for that stability: slots are not compacted, so iteration visits up
 * to the high-water-mark number of slots (live plus tombstone) until reuse fills
 * the holes, and storage is chunked rather than one contiguous span.
 *
 * Every operation is \c noexcept; allocation failure terminates, per the module
 * policy. This is an implementation detail of \c component_storage, not a public
 * container.
 */

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/stable_vector.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::ecs::detail {

/**
 * @brief Pointer-stable sparse pool keyed by entity index.
 *
 * @tparam T Component value type stored per entity index.
 */
template <typename T>
class component_pool {
public:
  using key_type = std::uint32_t;  ///< Entity index used as the key.
  using value_type = T;            ///< Stored component type.
  using size_type = std::size_t;   ///< Count type.

private:
  /// @brief One stored component together with the entity index that owns it.
  struct entry {
    key_type key{};
    value_type value{};
  };

  using slot_type = std::optional<entry>;  ///< A live entry or a tombstone.
  using slab_type = container::stable_vector<slot_type>;

  static constexpr key_type absent{0};  ///< Sentinel for an unmapped sparse slot (stores slot+1).

  std::vector<key_type> m_sparse{};  ///< key -> (slot index + 1), or \c absent.
  slab_type m_slots{};               ///< Pointer-stable slab of slots.
  std::vector<size_type> m_free{};   ///< Tombstoned slot indices available for reuse.
  size_type m_size{0};               ///< Number of live components.

  /// @brief Returns the slot index for \p key, or \c m_slots.size() if absent.
  [[nodiscard]] auto slot_of(key_type const key) const noexcept -> size_type {
    if (key >= m_sparse.size() || m_sparse[key] == absent) {
      return m_slots.size();
    }
    return m_sparse[key] - 1;
  }

public:
  /// @brief Constructs an empty pool.
  component_pool() noexcept = default;

  /**
   * @brief Inserts \p value at \p key, overwriting any existing component.
   *
   * Reuses a tombstoned slot when one is free, otherwise appends a slot. An
   * existing component at \p key is assigned the new value in place, keeping its
   * address.
   *
   * @param key Entity index key.
   * @param value Component value, moved in.
   *
   * @return \c true when a new component was created, \c false when an existing
   *         one was overwritten.
   *
   * @pre None.
   * @post \c contains(key) is \c true and the live component count reflects the
   *       insertion.
   *
   * @complexity \c O(1) amortised.
   */
  auto insert_or_assign(key_type const key, value_type value) noexcept -> bool {
    if (auto const slot{slot_of(key)}; slot != m_slots.size()) {
      (**m_slots.at(slot)).value = std::move(value);
      return false;
    }
    if (key >= m_sparse.size()) {
      m_sparse.resize(static_cast<size_type>(key) + 1, absent);
    }
    if (!m_free.empty()) {
      auto const slot{m_free.back()};
      m_free.pop_back();
      m_slots.at(slot)->emplace(entry{key, std::move(value)});
      m_sparse[key] = static_cast<key_type>(slot) + 1;
    } else {
      auto const slot{m_slots.size()};
      nexenne::utility::discard(m_slots.push_back(slot_type{entry{key, std::move(value)}}));
      m_sparse[key] = static_cast<key_type>(slot) + 1;
    }
    ++m_size;
    return true;
  }

  /**
   * @brief Removes the component at \p key if present, in place.
   *
   * Clears the slot (destroying the value) and frees it for reuse; every other
   * component keeps its slot and address.
   *
   * @param key Entity index key.
   *
   * @return \c true when a component was removed, \c false when \p key was
   *         absent.
   *
   * @pre None.
   * @post \c contains(key) is \c false.
   *
   * @complexity \c O(1).
   */
  auto erase(key_type const key) noexcept -> bool {
    auto const slot{slot_of(key)};
    if (slot == m_slots.size()) {
      return false;
    }
    m_slots.at(slot)->reset();
    m_free.push_back(slot);
    m_sparse[key] = absent;
    --m_size;
    return true;
  }

  /**
   * @brief Pointer to the component for \p key, or \c nullptr on a miss.
   *
   * @param key Entity index key.
   *
   * @return Stable pointer to the component, valid until that component is
   *         removed; \c nullptr when \p key is absent.
   *
   * @pre None.
   * @post The pool is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto try_get(key_type const key) noexcept -> value_type* {
    auto const slot{slot_of(key)};
    return slot == m_slots.size() ? nullptr : &(**m_slots.at(slot)).value;
  }

  /// @copydoc try_get
  [[nodiscard]] auto try_get(key_type const key) const noexcept -> value_type const* {
    auto const slot{slot_of(key)};
    return slot == m_slots.size() ? nullptr : &(**m_slots.at(slot)).value;
  }

  /**
   * @brief Whether a component exists for \p key.
   *
   * @param key Entity index key.
   *
   * @return \c true iff a live component is mapped to \p key.
   *
   * @pre None.
   * @post The pool is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto contains(key_type const key) const noexcept -> bool {
    return key < m_sparse.size() && m_sparse[key] != absent;
  }

  /**
   * @brief Number of live components.
   *
   * @return The live component count, excluding tombstones.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Whether the pool holds no live components.
   *
   * @return \c true iff \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Removes every component and all tombstones.
   *
   * @pre None.
   * @post \c empty() is \c true and \c slot_count() is zero.
   *
   * @complexity \c O(slot_count()).
   */
  auto clear() noexcept -> void {
    m_slots.clear();
    m_free.clear();
    for (auto& s : m_sparse) {
      s = absent;
    }
    m_size = 0;
  }

  /**
   * @brief Total number of slots, live plus tombstone.
   *
   * Used to drive iteration: walk \c [0, slot_count()) and skip slots where
   * \c is_live is \c false. Capturing this once bounds iteration to the slots
   * present at the start, so a slot appended later is not visited.
   *
   * @return The slab size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto slot_count() const noexcept -> size_type {
    return m_slots.size();
  }

  /**
   * @brief Whether slot \p slot currently holds a live component.
   *
   * @param slot Slot index, less than \c slot_count().
   *
   * @return \c true when the slot is live, \c false when it is a tombstone.
   *
   * @pre \p slot is less than \c slot_count().
   * @post None.
   */
  [[nodiscard]] auto is_live(size_type const slot) const noexcept -> bool {
    return m_slots.at(slot)->has_value();
  }

  /**
   * @brief Entity index owning the live component at \p slot.
   *
   * @param slot Live slot index.
   *
   * @return The entity index key for that slot.
   *
   * @pre \c is_live(slot) is \c true.
   * @post None.
   */
  [[nodiscard]] auto key_at(size_type const slot) const noexcept -> key_type {
    return (**m_slots.at(slot)).key;
  }

  /**
   * @brief The component stored at live slot \p slot.
   *
   * @param slot Live slot index.
   *
   * @return Stable reference to the component.
   *
   * @pre \c is_live(slot) is \c true.
   * @post None.
   */
  [[nodiscard]] auto value_at(size_type const slot) noexcept -> value_type& {
    return (**m_slots.at(slot)).value;
  }

  /// @copydoc value_at
  [[nodiscard]] auto value_at(size_type const slot) const noexcept -> value_type const& {
    return (**m_slots.at(slot)).value;
  }
};

/**
 * @brief Forward range over the live components of a \c component_pool.
 *
 * Walks the pool's slots and skips tombstones, yielding each live component
 * by reference. \c Pool is \c component_pool<T> for a mutable range or
 * \c component_pool<T> const for a read-only one, and the element reference
 * follows. Because the pool is pointer-stable, a reference obtained from the
 * range stays valid even if other components are added or removed, though
 * such changes alter which slots are live; the range itself is invalidated
 * only by \c clear or the pool's destruction.
 *
 * @tparam Pool \c component_pool<T> (mutable) or \c component_pool<T> const.
 */
template <typename Pool>
class pool_value_range {
public:
  using pool_type = Pool;                      ///< Viewed pool, possibly \c const.
  using size_type = typename Pool::size_type;  ///< Slot-index type.
  /// @brief Element reference: \c T& for a mutable pool, \c T const& otherwise.
  using reference = decltype(std::declval<Pool&>().value_at(size_type{}));

  /**
   * @brief Input iterator over the pool's live components.
   *
   * Holds a slot cursor that advances past tombstones on construction and on
   * every increment, so dereferencing always yields a live component.
   */
  class iterator {
  public:
    using reference = pool_value_range::reference;      ///< Yielded reference type.
    using value_type = std::remove_cvref_t<reference>;  ///< Decayed component type.
    using difference_type = std::ptrdiff_t;             ///< Required by the iterator concept.
    using iterator_category = std::input_iterator_tag;  ///< Input-iterator category.
    using iterator_concept = std::input_iterator_tag;   ///< Input-iterator concept.

  private:
    pool_type* m_pool{nullptr};
    size_type m_slot{0};

    /// @brief Advances the cursor to the next live slot, or to the slot count.
    auto advance_to_live() noexcept -> void {
      while (m_slot < m_pool->slot_count() && !m_pool->is_live(m_slot)) {
        ++m_slot;
      }
    }

  public:
    /**
     * @brief Constructs a singular iterator bound to no pool.
     *
     * @pre None.
     * @post Must not be dereferenced or incremented.
     */
    constexpr iterator() noexcept = default;

    /**
     * @brief Constructs an iterator into \p pool at \p slot, then advances to
     *        the first live slot at or after it.
     *
     * @param pool Pool being iterated.
     * @param slot Starting slot index, in \c [0, slot_count()].
     *
     * @pre \p slot is no greater than \c pool.slot_count().
     * @post The cursor names the first live slot at or after \p slot, or the
     *       slot count when none remain.
     */
    explicit iterator(pool_type& pool, size_type const slot) noexcept
        : m_pool{&pool}, m_slot{slot} {
      advance_to_live();
    }

    /**
     * @brief The live component at the cursor.
     *
     * @return A reference to the component, mutable or \c const per \c Pool.
     *
     * @pre \c *this is dereferenceable (not the end iterator).
     * @post The iterator is unchanged.
     */
    [[nodiscard]] auto operator*() const noexcept -> reference {
      return m_pool->value_at(m_slot);
    }

    /**
     * @brief Pre-increment: advances to the next live component.
     *
     * @return Reference to \c *this after advancing.
     *
     * @pre \c *this is not the end iterator.
     * @post The cursor names the next live slot, or the slot count.
     */
    auto operator++() noexcept -> iterator& {
      ++m_slot;
      advance_to_live();
      return *this;
    }

    /**
     * @brief Post-increment: advances, returning the prior value.
     *
     * @return A copy of the iterator as it was before advancing.
     *
     * @pre \c *this is not the end iterator.
     * @post The cursor names the next live slot, or the slot count.
     */
    auto operator++(int) noexcept -> iterator {
      auto const tmp{*this};
      ++*this;
      return tmp;
    }

    /**
     * @brief Equality: same pool and same slot cursor.
     *
     * @param a Left operand.
     * @param b Right operand.
     *
     * @return \c true iff both iterators name the same slot in the same pool.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(iterator const& a, iterator const& b) noexcept -> bool {
      return a.m_slot == b.m_slot && a.m_pool == b.m_pool;
    }
  };

private:
  pool_type* m_pool{nullptr};

public:
  /**
   * @brief Binds a range to \p pool.
   *
   * @param pool Pool whose live components the range spans. Must outlive it.
   *
   * @pre None.
   * @post The range spans \p pool's live components.
   */
  explicit pool_value_range(pool_type& pool) noexcept : m_pool{&pool} {}

  /**
   * @brief Iterator to the first live component.
   *
   * @return An iterator advanced to the first live slot, equal to \c end()
   *         when the pool holds no live components.
   *
   * @pre None.
   * @post The pool is unchanged.
   */
  [[nodiscard]] auto begin() const noexcept -> iterator {
    return iterator{*m_pool, size_type{0}};
  }

  /**
   * @brief Past-the-end iterator.
   *
   * @return An iterator positioned at the slot count.
   *
   * @pre None.
   * @post The pool is unchanged.
   */
  [[nodiscard]] auto end() const noexcept -> iterator {
    return iterator{*m_pool, m_pool->slot_count()};
  }
};

}  // namespace nexenne::ecs::detail
