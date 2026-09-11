#pragma once

/**
 * @file
 * @brief Bidirectional map: look up by either side, O(1) both ways.
 *
 * \c bimap<Left, Right> stores \c (left, right) pairs in which every left value
 * is unique on the left and every right value is unique on the right. It keeps
 * two \c flat_hash_map instances, one indexed by \c Left and one by \c Right, in
 * sync through every mutation, so both \c find_by_left and \c find_by_right are
 * amortised \c O(1); a one-sided map would need a linear scan for the reverse
 * lookup. The cost is roughly twice the memory of a single hash map, since each
 * pair is stored on both sides.
 *
 * Insertion keeps the two-sided uniqueness invariant: \c insert fails (changing
 * nothing) if either side is already bound, because a partial insert would
 * break the invariant; \c replace instead evicts any existing entry on either
 * side and then binds the new pair, returning how many entries it displaced.
 * Reach for it for two-way registries: entity/name, asset id/path, enum/string.
 * Every mutation hashes each argument key once and reuses that hash for both
 * the uniqueness check and the store. Every operation is \c noexcept exactly
 * when the key, hasher and key-equality code it runs is; allocation failure
 * terminates.
 */

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/utility/ignore.hpp>

namespace nexenne::container {

/**
 * @brief Bidirectional, uniquely-keyed map with O(1) lookup on both sides.
 *
 * @tparam Left Hashable left-side key type.
 * @tparam Right Hashable right-side key type.
 * @tparam HashLeft Hash for \p Left; \c std::hash<Left> by default.
 * @tparam HashRight Hash for \p Right; \c std::hash<Right> by default.
 * @tparam EqualLeft Key equality for \p Left; \c std::equal_to<Left> by default.
 * @tparam EqualRight Key equality for \p Right; \c std::equal_to<Right> by default.
 *
 * @pre None.
 * @post A default-constructed bimap is empty with no allocated storage.
 */
template <
  typename Left,
  typename Right,
  typename HashLeft = std::hash<Left>,
  typename HashRight = std::hash<Right>,
  typename EqualLeft = std::equal_to<Left>,
  typename EqualRight = std::equal_to<Right>>
class bimap {
public:
  using value_type = std::pair<Left, Right>;
  using left_type = Left;
  using right_type = Right;
  using size_type = std::size_t;

private:
  using left_index = flat_hash_map<Left, Right, HashLeft, EqualLeft>;
  using right_index = flat_hash_map<Right, Left, HashRight, EqualRight>;

  left_index m_l_to_r;
  right_index m_r_to_l;

  /**
   * @brief Whether hashing and comparing a key on either side is nothrow.
   */
  static constexpr bool nothrow_probe_v{
    left_index::template nothrow_probe_v<Left> && right_index::template nothrow_probe_v<Right>
  };

  /**
   * @brief Whether copying a pair into both indexes and relocating it is nothrow.
   */
  static constexpr bool nothrow_store_v{
    left_index::nothrow_relocate_v && right_index::nothrow_relocate_v
    && std::is_nothrow_copy_constructible_v<Left> && std::is_nothrow_copy_constructible_v<Right>
  };

  /**
   * @brief Binds \p key to \p value in \p index, overwriting an existing value.
   *
   * @tparam Index One of the two indexes.
   * @tparam K Key type of \p index.
   * @tparam V Mapped type of \p index.
   * @param index Index to store into.
   * @param h The hash of \p key.
   * @param key Key to bind.
   * @param value Value to bind it to.
   *
   * @pre \p h is the hash of \p key under \p index's hasher.
   * @post \p key maps to \p value in \p index.
   */
  template <typename Index, typename K, typename V>
  static auto bind(Index& index, std::size_t const h, K key, V value) noexcept(
    Index::nothrow_relocate_v
    && std::is_nothrow_move_assignable_v<V> && noexcept(index.probe_slot(key, h))
  ) -> void {
    if (auto const* const kept{index.probe_slot(key, h)}) {
      index.mutable_slot(kept).value().second = std::move(value);
      return;
    }
    index.ensure_capacity_for(index.size() + 1);
    nexenne::utility::ignore(index.place_absent(h, std::move(key), std::move(value)));
  }

public:
  /**
   * @brief Constructs an empty bimap with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr bimap() noexcept(
    std::is_nothrow_default_constructible_v<decltype(m_l_to_r)>
    && std::is_nothrow_default_constructible_v<decltype(m_r_to_l)>
  ) = default;

  /**
   * @brief Constructs an empty bimap with storage for \p expected_entries.
   *
   * @param expected_entries Number of pairs to reserve storage for on each side.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is at least
   *       \p expected_entries.
   */
  explicit bimap(size_type const expected_entries) noexcept(
    std::is_nothrow_constructible_v<decltype(m_l_to_r), size_type>
    && std::is_nothrow_constructible_v<decltype(m_r_to_l), size_type>
  )
      : m_l_to_r{expected_entries}, m_r_to_l{expected_entries} {}

  /**
   * @brief Number of bound pairs.
   *
   * @return Count of \c (left, right) pairs in the bimap.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_l_to_r.size();
  }

  /**
   * @brief Whether the bimap holds no pairs.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_l_to_r.empty();
  }

  /**
   * @brief Slot count of the left-side index.
   *
   * This is the raw slot count, not a no-rehash budget: a rehash triggers once
   * the live entries exceed 7/8 of the slots, so fewer than \c capacity() fresh
   * pairs fit before one occurs.
   *
   * @return Current slot count of the left-side index.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto capacity() const noexcept -> size_type {
    return m_l_to_r.capacity();
  }

  /**
   * @brief Largest number of pairs the bimap can ever hold.
   *
   * @return The maximum size of the underlying maps.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto max_size() const noexcept -> size_type {
    return m_l_to_r.max_size();
  }

  /**
   * @brief Reserves storage for at least \p n pairs on both sides.
   *
   * @param n Minimum capacity to reserve.
   *
   * @pre None.
   * @post Capacity is at least \p n; existing bindings are preserved.
   */
  auto reserve(size_type const n) noexcept(
    noexcept(m_l_to_r.reserve(n)) && noexcept(m_r_to_l.reserve(n))
  ) -> void {
    m_l_to_r.reserve(n);
    m_r_to_l.reserve(n);
  }

  /**
   * @brief Removes all pairs, retaining capacity.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  auto clear() noexcept -> void {
    m_l_to_r.clear();
    m_r_to_l.clear();
  }

  /**
   * @brief Releases unused capacity on both sides.
   *
   * @pre None.
   * @post \c size() is unchanged; capacity may shrink toward \c size().
   */
  auto
  shrink_to_fit() noexcept(noexcept(m_l_to_r.shrink_to_fit()) && noexcept(m_r_to_l.shrink_to_fit()))
    -> void {
    m_l_to_r.shrink_to_fit();
    m_r_to_l.shrink_to_fit();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Bimap to exchange state with.
   *
   * @pre None.
   * @post This bimap holds \p other's former pairs and vice versa.
   */
  auto swap(bimap& other) noexcept(
    noexcept(m_l_to_r.swap(other.m_l_to_r)) && noexcept(m_r_to_l.swap(other.m_r_to_l))
  ) -> void {
    m_l_to_r.swap(other.m_l_to_r);
    m_r_to_l.swap(other.m_r_to_l);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First bimap.
   * @param b Second bimap.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend auto swap(bimap& a, bimap& b) noexcept(noexcept(a.swap(b))) -> void {
    a.swap(b);
  }

  /**
   * @brief Inserts the pair \c (left, right) if neither side is bound.
   *
   * Fails when either side already has a binding, since a partial insert would
   * break the two-sided uniqueness invariant.
   *
   * @param left Left-side key; both arguments are taken by value and are consumed
   *             even on failure, so a moved-in \p left is gone whatever the
   *             result.
   * @param right Right-side key; consumed on failure as well (see \p left).
   *
   * @return \c true on a fresh pair, \c false when either side was already bound
   *         (the bimap's contents are unchanged, though the arguments are
   *         consumed).
   *
   * @pre None.
   * @post On \c true both sides are bound and \c size() grew by one; on \c false
   *       the bimap's contents are unchanged. The by-value arguments are consumed
   *       in either case.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(Left left, Right right) noexcept(nothrow_probe_v && nothrow_store_v) -> bool {
    auto const left_hash{m_l_to_r.m_hash(left)};
    auto const right_hash{m_r_to_l.m_hash(right)};
    if (m_l_to_r.probe_slot(left, left_hash) != nullptr
        || m_r_to_l.probe_slot(right, right_hash) != nullptr) {
      return false;
    }
    m_l_to_r.ensure_capacity_for(m_l_to_r.size() + 1);
    m_r_to_l.ensure_capacity_for(m_r_to_l.size() + 1);
    nexenne::utility::ignore(m_l_to_r.place_absent(left_hash, left, right));
    nexenne::utility::ignore(m_r_to_l.place_absent(right_hash, std::move(right), std::move(left)));
    return true;
  }

  /**
   * @brief Binds \c (left, right), evicting any existing entry on either side.
   *
   * @param left Left-side key, moved into the bimap.
   * @param right Right-side key, moved into the bimap.
   *
   * @return Number of pre-existing entries displaced (0, 1, or 2).
   *
   * @pre None.
   * @post \p left is bound to \p right and any prior binding on either side is
   *       removed; \c contains_left(left) and \c contains_right(right) are both
   *       \c true.
   *
   * @complexity Amortised \c O(1).
   */
  auto replace(Left left, Right right) noexcept(
    nothrow_probe_v && nothrow_store_v
    && std::is_nothrow_move_assignable_v<Left> && std::is_nothrow_move_assignable_v<Right>
  ) -> size_type {
    auto const left_hash{m_l_to_r.m_hash(left)};
    auto const right_hash{m_r_to_l.m_hash(right)};
    size_type displaced{0};
    if (auto const* const old{m_l_to_r.probe_slot(left, left_hash)}) {
      nexenne::utility::ignore(m_r_to_l.erase(old->value().second));
      ++displaced;
    }
    if (auto const* const old{m_r_to_l.probe_slot(right, right_hash)}) {
      nexenne::utility::ignore(m_l_to_r.erase(old->value().second));
      ++displaced;
    }
    bind(m_l_to_r, left_hash, left, right);
    bind(m_r_to_l, right_hash, std::move(right), std::move(left));
    return displaced;
  }

  /**
   * @brief Removes the entry whose left side is \p left.
   *
   * @param left Left-side key to remove.
   *
   * @return \c true when a pair was removed, \c false when \p left was unbound.
   *
   * @pre None.
   * @post \p left is unbound. On a removal \c size() shrank by one.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase_left(Left const& left) noexcept(
    nothrow_probe_v && left_index::nothrow_relocate_v && right_index::nothrow_relocate_v
  ) -> bool {
    auto const* const found{m_l_to_r.probe_slot(left)};
    if (found == nullptr) {
      return false;
    }
    nexenne::utility::ignore(m_r_to_l.erase(found->value().second));
    nexenne::utility::ignore(m_l_to_r.erase_slot(found));
    return true;
  }

  /**
   * @brief Removes the entry whose right side is \p right.
   *
   * @param right Right-side key to remove.
   *
   * @return \c true when a pair was removed, \c false when \p right was unbound.
   *
   * @pre None.
   * @post \p right is unbound. On a removal \c size() shrank by one.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase_right(Right const& right) noexcept(
    nothrow_probe_v && left_index::nothrow_relocate_v && right_index::nothrow_relocate_v
  ) -> bool {
    auto const* const found{m_r_to_l.probe_slot(right)};
    if (found == nullptr) {
      return false;
    }
    nexenne::utility::ignore(m_l_to_r.erase(found->value().second));
    nexenne::utility::ignore(m_r_to_l.erase_slot(found));
    return true;
  }

  /**
   * @brief Pointer to the right value bound to \p left, or \c nullptr on a miss.
   *
   * @param left Left-side key to look up.
   *
   * @return Pointer to the bound right value, or \c nullptr when \p left is
   *         unbound.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto find_by_left(Left const& left) const noexcept(noexcept(m_l_to_r.find(left)))
    -> Right const* {
    return m_l_to_r.find(left);
  }

  /**
   * @brief Pointer to the left value bound to \p right, or \c nullptr on a miss.
   *
   * @param right Right-side key to look up.
   *
   * @return Pointer to the bound left value, or \c nullptr when \p right is
   *         unbound.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto
  find_by_right(Right const& right) const noexcept(noexcept(m_r_to_l.find(right))) -> Left const* {
    return m_r_to_l.find(right);
  }

  /**
   * @brief Whether \p left has a binding.
   *
   * @param left Left-side key to test.
   *
   * @return \c true when \p left is bound.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto
  contains_left(Left const& left) const noexcept(noexcept(m_l_to_r.contains(left))) -> bool {
    return m_l_to_r.contains(left);
  }

  /**
   * @brief Whether \p right has a binding.
   *
   * @param right Right-side key to test.
   *
   * @return \c true when \p right is bound.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto
  contains_right(Right const& right) const noexcept(noexcept(m_r_to_l.contains(right))) -> bool {
    return m_r_to_l.contains(right);
  }

  /**
   * @brief Iterator to the first \c (left, right) pair.
   *
   * @return Const iterator over the left-side index.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto begin() const noexcept {
    return m_l_to_r.begin();
  }

  /**
   * @brief Iterator one past the last pair.
   *
   * @return Const end iterator over the left-side index.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto end() const noexcept {
    return m_l_to_r.end();
  }

  /**
   * @brief Const iterator to the first \c (left, right) pair.
   *
   * @return Const iterator over the left-side index.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto cbegin() const noexcept {
    return m_l_to_r.cbegin();
  }

  /**
   * @brief Const iterator one past the last pair.
   *
   * @return Const end iterator over the left-side index.
   *
   * @pre None.
   * @post None. The bimap is not modified.
   */
  [[nodiscard]] auto cend() const noexcept {
    return m_l_to_r.cend();
  }

  /**
   * @brief Whether \p a and \p b hold the same set of pairs.
   *
   * @param a First bimap.
   * @param b Second bimap.
   *
   * @return \c true when both bind identical left-to-right pairs.
   *
   * @pre None.
   * @post None. Neither bimap is modified.
   *
   * @complexity \c O(n) average.
   */
  [[nodiscard]] friend auto
  operator==(bimap const& a, bimap const& b) noexcept(noexcept(a.m_l_to_r == b.m_l_to_r)) -> bool {
    return a.m_l_to_r == b.m_l_to_r;
  }
};

}  // namespace nexenne::container
