#pragma once

/**
 * @file
 * @brief Integer-key set with O(1) insert, erase, contains, and dense iteration.
 *
 * \c sparse_set<Key> is the data structure behind sparse-set entity-component
 * systems. It keeps two vectors: \c sparse, indexed by key, holds each present
 * key's position in \c dense (or \c invalid_index when absent); \c dense holds
 * the live keys contiguously. Every single-key operation is \c O(1): insert
 * appends to \c dense and records the position, erase swaps the key with the last
 * dense element and pops, and contains range-checks \c sparse[k] and verifies the
 * dense slot still holds \p k (so a stale sparse entry never reads as present).
 *
 * Iteration walks \c dense, so it is contiguous and cache-friendly; the order is
 * insertion order with swap-pop on erase (erasing an interior key moves the last
 * key into its slot). Memory is \c O(max_key + size): the sparse array grows to
 * the largest key ever inserted, so reuse keys densely from 0 to avoid waste.
 *
 * Reach for it for ECS entity sets and any membership test over dense integer
 * ids where you also want to iterate the members fast. Every operation is
 * \c noexcept; allocation failure terminates. It holds two vectors, so the rule
 * of zero applies.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace nexenne::container {

/**
 * @brief Integer-key set backed by a sparse-to-dense index pair.
 *
 * @tparam Key Unsigned integer key type; \c std::uint32_t by default (the
 *             typical ECS choice). \c std::uint16_t trims the sparse-array cost
 *             for a bounded key space; \c std::uint64_t suits huge worlds.
 *
 * @pre None.
 * @post A default-constructed set is empty.
 */
template <std::unsigned_integral Key = std::uint32_t>
class sparse_set {
public:
  using key_type = Key;
  using size_type = std::size_t;
  // The dense values are the keys, and mutating a key would break the sparse
  // mapping, so iteration is read-only and iterator == const_iterator.
  using iterator = typename std::vector<Key>::const_iterator;
  using const_iterator = iterator;

  /// Sentinel sparse value meaning "key not present"; public for custom storage.
  static constexpr size_type invalid_index{std::numeric_limits<size_type>::max()};

private:
  std::vector<size_type> m_sparse;
  std::vector<key_type> m_dense;

  constexpr auto ensure_sparse_capacity(key_type const k) noexcept -> void {
    auto const needed{static_cast<size_type>(k) + 1};
    if (needed > m_sparse.size()) {
      m_sparse.resize(needed, invalid_index);
    }
  }

public:
  /**
   * @brief Constructs an empty set with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr sparse_set() noexcept = default;

  /**
   * @brief Reserves sparse capacity for \p key_count keys and dense capacity for
   *        \p dense_count entries.
   *
   * @param key_count Largest key value to index without resizing the sparse
   *                  array.
   * @param dense_count Number of live keys to reserve dense storage for.
   *
   * @pre None.
   * @post \c key_capacity() is at least \p key_count and dense capacity is at
   *       least \p dense_count; \c size() is unchanged.
   */
  constexpr auto reserve(size_type const key_count, size_type const dense_count) noexcept -> void {
    if (key_count > m_sparse.size()) {
      m_sparse.resize(key_count, invalid_index);
    }
    m_dense.reserve(dense_count);
  }

  /**
   * @brief Inserts \p k; a key already present leaves the set unchanged.
   *
   * @param k Key to insert.
   *
   * @return \c true on a new insertion, \c false when \p k was already present.
   *
   * @pre None.
   * @post \p k is present; on a new insertion \c size() grew by one.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto insert(key_type const k) noexcept -> bool {
    if (contains(k)) {
      return false;
    }
    ensure_sparse_capacity(k);
    m_sparse[k] = m_dense.size();
    m_dense.push_back(k);
    return true;
  }

  /**
   * @brief Removes \p k via swap-pop.
   *
   * The vacated dense slot is filled by the former last key, so dense order
   * changes when an interior key is erased.
   *
   * @param k Key to remove.
   *
   * @return \c true on a removal, \c false when \p k was absent.
   *
   * @pre None.
   * @post \p k is absent; on a removal \c size() shrank by one.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase(key_type const k) noexcept -> bool {
    if (!contains(k)) {
      return false;
    }
    auto const pos{m_sparse[k]};
    auto const last_pos{m_dense.size() - 1};
    if (pos != last_pos) {
      auto const moved_key{m_dense[last_pos]};
      m_dense[pos] = moved_key;
      m_sparse[moved_key] = pos;
    }
    m_dense.pop_back();
    m_sparse[k] = invalid_index;
    return true;
  }

  /**
   * @brief Reports whether \p k is in the set.
   *
   * @param k Key to test.
   *
   * @return \c true when \p k is present.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto contains(key_type const k) const noexcept -> bool {
    if (static_cast<size_type>(k) >= m_sparse.size()) {
      return false;
    }
    auto const pos{m_sparse[k]};
    return pos < m_dense.size() && m_dense[pos] == k;
  }

  /**
   * @brief Dense position of \p k, or \c std::nullopt when absent.
   *
   * The position is stable until the next \c erase or \c clear; other inserts
   * append and do not move existing entries. Pair it with a parallel value array
   * to build a key-to-value map.
   *
   * @param k Key to locate.
   *
   * @return The dense position when present, otherwise \c std::nullopt.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto index_of(key_type const k
  ) const noexcept -> std::optional<size_type> {
    if (!contains(k)) {
      return std::nullopt;
    }
    return m_sparse[k];
  }

  /**
   * @brief Iterator to \p k's dense slot, or \c end() when absent.
   *
   * @param k Key to look up.
   *
   * @return A dense iterator to \p k, or \c end().
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto find(key_type const k) const noexcept -> const_iterator {
    if (auto const pos{index_of(k)}) {
      return begin() + static_cast<std::ptrdiff_t>(*pos);
    }
    return end();
  }

  /**
   * @brief Number of entries for \p k, always \c 0 or \c 1.
   *
   * @param k Key to count.
   *
   * @return \c 1 when \p k is present, otherwise \c 0.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto count(key_type const k) const noexcept -> size_type {
    return contains(k) ? size_type{1} : size_type{0};
  }

  /**
   * @brief Removes every key; sparse capacity is retained.
   *
   * @pre None.
   * @post \c empty() is \c true and \c key_capacity() is unchanged.
   *
   * @complexity \c O(size).
   */
  constexpr auto clear() noexcept -> void {
    for (auto const k : m_dense) {
      m_sparse[k] = invalid_index;
    }
    m_dense.clear();
  }

  /**
   * @brief Releases unused sparse and dense capacity.
   *
   * @pre None.
   * @post \c size() is unchanged.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_sparse.shrink_to_fit();
    m_dense.shrink_to_fit();
  }

  /**
   * @brief Number of live keys.
   *
   * @return The key count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_dense.size();
  }

  /**
   * @brief Reports whether the set holds no keys.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_dense.empty();
  }

  /**
   * @brief The largest number of keys the dense array can hold.
   *
   * @return The maximum size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_dense.max_size();
  }

  /**
   * @brief The integer key space currently allocated for lookup.
   *
   * The largest key the sparse array tracks plus one; distinct from \c size().
   *
   * @return The sparse array length.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto key_capacity() const noexcept -> size_type {
    return m_sparse.size();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Set to exchange state with.
   *
   * @pre None.
   * @post This set and \p other have exchanged keys.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(sparse_set& other) noexcept -> void {
    m_sparse.swap(other.m_sparse);
    m_dense.swap(other.m_dense);
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
  friend constexpr auto swap(sparse_set& a, sparse_set& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief A contiguous view of the live keys, in dense order.
   *
   * @return A span over the dense key array; invalidated by any mutation.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto keys() const noexcept -> std::span<key_type const> {
    return std::span<key_type const>{m_dense.data(), m_dense.size()};
  }

  /**
   * @brief Iterator to the first live key, in dense order.
   *
   * @return A const iterator to the start of the dense range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return m_dense.begin();
  }

  /**
   * @brief Iterator one past the last live key.
   *
   * @return A const iterator to the end of the dense range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return m_dense.end();
  }
};

using sparse_set_u32 = sparse_set<std::uint32_t>;
using sparse_set_u16 = sparse_set<std::uint16_t>;
using sparse_set_u64 = sparse_set<std::uint64_t>;

}  // namespace nexenne::container
