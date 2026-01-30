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
 * nothing) if either side is already bound, because a partial insert would break
 * the invariant; \c replace instead evicts any existing entry on either side and
 * then binds the new pair, returning how many entries it displaced. Reach for it
 * for two-way registries: entity/name, asset id/path, enum/string. Every
 * operation is \c noexcept; allocation failure terminates.
 */

#include <cstddef>
#include <functional>
#include <utility>

#include <nexenne/container/flat_hash_map.hpp>

namespace nexenne::container {

/**
 * @brief Bidirectional, uniquely-keyed map with O(1) lookup on both sides.
 *
 * @tparam Left Hashable left-side key type.
 * @tparam Right Hashable right-side key type.
 * @tparam HashLeft Hash for \p Left; \c std::hash<Left> by default.
 * @tparam HashRight Hash for \p Right; \c std::hash<Right> by default.
 *
 * @pre None.
 * @post A default-constructed bimap is empty with no allocated storage.
 */
template <
  typename Left,
  typename Right,
  typename HashLeft = std::hash<Left>,
  typename HashRight = std::hash<Right>>
class bimap {
public:
  using left_type = Left;
  using right_type = Right;
  using size_type = std::size_t;

private:
  // Both directions store full copies of the pair: l_to_r maps Left -> Right and
  // r_to_l maps Right -> Left. Every mutation updates both to keep them in sync.
  flat_hash_map<Left, Right, HashLeft> m_l_to_r;
  flat_hash_map<Right, Left, HashRight> m_r_to_l;

public:
  /**
   * @brief Constructs an empty bimap with no allocated storage.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr bimap() noexcept = default;

  /**
   * @brief Constructs an empty bimap with storage for \p expected_entries.
   *
   * @param expected_entries Number of pairs to reserve storage for on each side.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is at least
   *       \p expected_entries.
   */
  explicit bimap(size_type const expected_entries) noexcept
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
   * @brief Pairs that fit without rehashing.
   *
   * @return Current capacity of the left-side index.
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
  auto reserve(size_type const n) noexcept -> void {
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
  auto shrink_to_fit() noexcept -> void {
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
  auto swap(bimap& other) noexcept -> void {
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
  friend auto swap(bimap& a, bimap& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Inserts the pair \c (left, right) if neither side is bound.
   *
   * Fails when either side already has a binding, since a partial insert would
   * break the two-sided uniqueness invariant.
   *
   * @param left Left-side key, moved into the bimap on success.
   * @param right Right-side key, moved into the bimap on success.
   *
   * @return \c true on a fresh pair, \c false when either side was already bound
   *         (no state is changed).
   *
   * @pre None.
   * @post On \c true both sides are bound and \c size() grew by one; on \c false
   *       the bimap is unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  auto insert(Left left, Right right) noexcept -> bool {
    if (m_l_to_r.contains(left) || m_r_to_l.contains(right)) {
      return false;
    }
    static_cast<void>(m_l_to_r.insert(left, right));
    static_cast<void>(m_r_to_l.insert(std::move(right), std::move(left)));
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
  auto replace(Left left, Right right) noexcept -> size_type {
    size_type displaced{0};
    // Evict the left side's old binding (and its reverse), if any.
    if (auto const* const old_right{m_l_to_r.find(left)}) {
      static_cast<void>(m_r_to_l.erase(*old_right));
      ++displaced;
    }
    // Evict the right side's old binding (and its forward), if any.
    if (auto const* const old_left{m_r_to_l.find(right)}) {
      static_cast<void>(m_l_to_r.erase(*old_left));
      ++displaced;
    }
    // insert_or_assign, not insert: a surviving same-side key must be overwritten
    // so the two maps stay in sync.
    static_cast<void>(m_l_to_r.insert_or_assign(left, right));
    static_cast<void>(m_r_to_l.insert_or_assign(std::move(right), std::move(left)));
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
  auto erase_left(Left const& left) noexcept -> bool {
    auto const* const right{m_l_to_r.find(left)};
    if (right == nullptr) {
      return false;
    }
    static_cast<void>(m_r_to_l.erase(*right));
    static_cast<void>(m_l_to_r.erase(left));
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
  auto erase_right(Right const& right) noexcept -> bool {
    auto const* const left{m_r_to_l.find(right)};
    if (left == nullptr) {
      return false;
    }
    static_cast<void>(m_l_to_r.erase(*left));
    static_cast<void>(m_r_to_l.erase(right));
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
  [[nodiscard]] auto find_by_left(Left const& left) const noexcept -> Right const* {
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
  [[nodiscard]] auto find_by_right(Right const& right) const noexcept -> Left const* {
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
  [[nodiscard]] auto contains_left(Left const& left) const noexcept -> bool {
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
  [[nodiscard]] auto contains_right(Right const& right) const noexcept -> bool {
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
  [[nodiscard]] friend auto operator==(bimap const& a, bimap const& b) noexcept -> bool {
    return a.m_l_to_r == b.m_l_to_r;
  }
};

}  // namespace nexenne::container
