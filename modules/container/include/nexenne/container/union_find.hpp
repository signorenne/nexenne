#pragma once

/**
 * @file
 * @brief Disjoint-set (union-find) with union by size and path compression.
 *
 * \c union_find<Index> maintains a partition of \c [0, n) into disjoint sets with
 * two near-constant-time operations: \c find(i) returns the representative root
 * of \p i's set, flattening the traversed chain (path compression) so later finds
 * are faster; \c unite(a, b) merges two sets, hanging the smaller tree under the
 * larger (union by size) to keep heights logarithmic. With both heuristics the
 * amortised cost per operation is \c O(alpha(n)), the inverse Ackermann function,
 * effectively constant for any practical \p n.
 *
 * Reach for it for connected-component and island detection (the "which bodies
 * form a solver island" pass of a physics engine), Kruskal-style minimum
 * spanning trees, and equivalence-class tracking while building a graph
 * incrementally. It holds two vectors (parent links and per-root sizes) plus a
 * set count, so the rule of zero applies. Every operation is \c noexcept;
 * allocation failure terminates. Note that \c find mutates the parent array via
 * path compression, so even a query is a mutating call.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

/**
 * @brief Disjoint-set forest over integer node indices.
 *
 * @tparam Index Unsigned integer node-index type; \c std::uint32_t by default. A
 *               smaller type trims memory at the cost of supporting fewer nodes.
 *
 * @pre None.
 * @post A default-constructed structure tracks zero nodes.
 */
template <std::unsigned_integral Index = std::uint32_t>
class union_find {
public:
  using index_type = Index;
  using size_type = std::size_t;

private:
  std::vector<index_type> m_parent;
  std::vector<size_type> m_set_size;
  size_type m_set_count{0};

public:
  /**
   * @brief Default-constructs an empty partition with no nodes.
   *
   * @pre None.
   * @post \c empty() is \c true and \c count() is zero.
   */
  constexpr union_find() noexcept = default;

  /**
   * @brief Constructs a partition of \p n singleton sets numbered \c 0..n-1.
   *
   * @param n Initial node count.
   *
   * @pre None.
   * @post \c size() and \c count() both equal \p n; each node is its own root.
   */
  explicit constexpr union_find(size_type const n) noexcept {
    reset(n);
  }

  /**
   * @brief Total number of nodes the structure tracks.
   *
   * Distinct from \c count(): this is the index-space size, not the number of
   * disjoint sets.
   *
   * @return The total node count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_parent.size();
  }

  /**
   * @brief Number of disjoint sets currently in the partition.
   *
   * Starts equal to \c size() and drops by one each time \c unite merges two
   * distinct sets.
   *
   * @return The count of disjoint sets.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto count() const noexcept -> size_type {
    return m_set_count;
  }

  /**
   * @brief Reports whether the structure tracks zero nodes.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_parent.empty();
  }

  /**
   * @brief The largest number of nodes the structure can track.
   *
   * @return The maximum size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_parent.max_size();
  }

  /**
   * @brief Reserves capacity for \p n nodes without changing the node count.
   *
   * @param n Minimum node capacity to reserve.
   *
   * @pre None.
   * @post Capacity is at least \p n; \c size() and \c count() are unchanged.
   */
  constexpr auto reserve(size_type const n) noexcept -> void {
    m_parent.reserve(n);
    m_set_size.reserve(n);
  }

  /**
   * @brief Releases unused capacity in the backing vectors.
   *
   * @pre None.
   * @post \c size() is unchanged.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_parent.shrink_to_fit();
    m_set_size.shrink_to_fit();
  }

  /**
   * @brief Resets the structure to \p n singleton sets.
   *
   * @param n New node count.
   *
   * @pre None.
   * @post \c size() and \c count() both equal \p n; each node is its own root.
   *
   * @complexity \c O(n).
   */
  constexpr auto reset(size_type const n) noexcept -> void {
    m_parent.assign(n, index_type{});
    m_set_size.assign(n, size_type{1});
    for (size_type i{0}; i < n; ++i) {
      m_parent[i] = static_cast<index_type>(i);
    }
    m_set_count = n;
  }

  /**
   * @brief Removes every node; backing storage is retained.
   *
   * @pre None.
   * @post \c empty() is \c true and \c count() is zero.
   */
  constexpr auto clear() noexcept -> void {
    m_parent.clear();
    m_set_size.clear();
    m_set_count = 0;
  }

  /**
   * @brief Appends a fresh singleton set and returns its node index.
   *
   * @return The index of the new node.
   *
   * @pre None.
   * @post \c size() and \c count() each grew by one; the new node is its own
   *       root.
   *
   * @complexity Amortised \c O(1).
   */
  constexpr auto make_set() noexcept -> index_type {
    auto const node{static_cast<index_type>(m_parent.size())};
    m_parent.push_back(node);
    m_set_size.push_back(1);
    ++m_set_count;
    return node;
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Partition to exchange state with.
   *
   * @pre None.
   * @post This structure and \p other have exchanged partitions.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(union_find& other) noexcept -> void {
    using std::swap;
    m_parent.swap(other.m_parent);
    m_set_size.swap(other.m_set_size);
    swap(m_set_count, other.m_set_count);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First partition.
   * @param b Second partition.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(union_find& a, union_find& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Representative root of \p i's set, with path compression.
   *
   * Walks the parent chain to the root and halves it on the way (each node is
   * pointed at its grandparent), so subsequent finds are faster.
   *
   * @param i Node whose set to look up.
   *
   * @return The set's root, or \c container_error::out_of_range when \p i is not
   *         a valid node index.
   *
   * @pre None.
   * @post On success the chain from \p i is flattened; the partition is
   *       unchanged.
   *
   * @complexity Amortised \c O(alpha(n)).
   */
  constexpr auto find(index_type const i) noexcept -> result<index_type> {
    if (static_cast<size_type>(i) >= m_parent.size()) {
      return std::unexpected{container_error::out_of_range};
    }
    auto current{i};
    while (m_parent[current] != current) {
      m_parent[current] = m_parent[m_parent[current]];  // path halving
      current = m_parent[current];
    }
    return current;
  }

  /**
   * @brief Root of \p i without path compression.
   *
   * Slower than \c find on later queries (no flattening) but \c const, so usable
   * from a constant context or a comparison that must not mutate.
   *
   * @param i Node whose root to compute.
   *
   * @return The representative root of \p i's set.
   *
   * @pre \p i is a valid node index in \c [0, size()).
   * @post None.
   *
   * @complexity \c O(h), for chain length \c h.
   */
  [[nodiscard]] constexpr auto root_of(index_type i) const noexcept -> index_type {
    while (m_parent[i] != i) {
      i = m_parent[i];
    }
    return i;
  }

  /**
   * @brief Reports whether \p a and \p b share a set.
   *
   * @param a First node.
   * @param b Second node.
   *
   * @return \c true when \p a and \p b are in the same set, \c false otherwise,
   *         or \c container_error::out_of_range when either index is invalid.
   *
   * @pre None.
   * @post The chains from \p a and \p b may be flattened; the partition is
   *       unchanged.
   *
   * @complexity Amortised \c O(alpha(n)).
   */
  constexpr auto connected(index_type const a, index_type const b) noexcept -> result<bool> {
    auto const root_a{find(a)};
    if (!root_a.has_value()) {
      return std::unexpected{root_a.error()};
    }
    auto const root_b{find(b)};
    if (!root_b.has_value()) {
      return std::unexpected{root_b.error()};
    }
    return *root_a == *root_b;
  }

  /**
   * @brief Size of the set containing \p i.
   *
   * @param i Node whose set size to report.
   *
   * @return The number of nodes in \p i's set, or \c container_error::out_of_range
   *         when \p i is invalid.
   *
   * @pre None.
   * @post The chain from \p i may be flattened; the partition is unchanged.
   *
   * @complexity Amortised \c O(alpha(n)).
   */
  constexpr auto size_of(index_type const i) noexcept -> result<size_type> {
    auto const root{find(i)};
    if (!root.has_value()) {
      return std::unexpected{root.error()};
    }
    return m_set_size[*root];
  }

  /**
   * @brief Merges the sets containing \p a and \p b.
   *
   * The smaller set is hung under the larger (union by size); on a tie the
   * second argument's root wins.
   *
   * @param a First node.
   * @param b Second node.
   *
   * @return \c true when two distinct sets were merged, \c false when \p a and
   *         \p b were already in the same set, or \c container_error::out_of_range
   *         when either index is invalid.
   *
   * @pre None.
   * @post \p a and \p b share a set; on a merge \c count() shrank by one.
   *
   * @complexity Amortised \c O(alpha(n)).
   */
  constexpr auto unite(index_type const a, index_type const b) noexcept -> result<bool> {
    auto const root_a{find(a)};
    if (!root_a.has_value()) {
      return std::unexpected{root_a.error()};
    }
    auto const root_b{find(b)};
    if (!root_b.has_value()) {
      return std::unexpected{root_b.error()};
    }
    if (*root_a == *root_b) {
      return false;
    }
    auto small_root{*root_a};
    auto big_root{*root_b};
    if (m_set_size[small_root] > m_set_size[big_root]) {
      std::swap(small_root, big_root);
    }
    m_parent[small_root] = big_root;
    m_set_size[big_root] += m_set_size[small_root];
    m_set_size[small_root] = 0;  // a non-root no longer carries a size
    --m_set_count;
    return true;
  }

  /**
   * @brief Read-only view of the parent-link array.
   *
   * \c parents()[i] is the immediate parent of node \c i (or \c i itself for a
   * current root), reflecting whatever path compression has produced so far.
   *
   * @return A span over the parent-link array; invalidated by any mutating call.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto parents() const noexcept -> std::span<index_type const> {
    return std::span<index_type const>{m_parent.data(), m_parent.size()};
  }

  /**
   * @brief Read-only view of the per-root set sizes.
   *
   * A root's entry holds its set's size; a non-root's entry is zero.
   *
   * @return A span over the per-node set-size array; invalidated by any mutating
   *         call.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto set_sizes() const noexcept -> std::span<size_type const> {
    return std::span<size_type const>{m_set_size.data(), m_set_size.size()};
  }

  /**
   * @brief A lazy range over every node index in \c [0, size()).
   *
   * Returned as \p index_type so it plugs straight into algorithms wanting the
   * natural index type; combine with \c find to enumerate roots.
   *
   * @return A lazy range of every node index.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto nodes() const noexcept {
    return std::views::iota(index_type{0}, static_cast<index_type>(m_parent.size()));
  }

  /**
   * @brief Reports whether this and \p other classify every node into the same
   *        sets, regardless of operation history.
   *
   * Uses \c root_of (no mutation), so two structures that differ only in
   * path-compression state still compare equal. This is given a visible name
   * rather than \c operator== so its \c O(n) cost is never hidden behind a
   * generic comparison in a hot loop or in generic code.
   *
   * @param other Partition to compare against.
   *
   * @return \c true when both classify every node into the same sets.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(n * alpha(n)).
   */
  [[nodiscard]] constexpr auto same_partition(union_find const& other) const noexcept -> bool {
    if (m_parent.size() != other.m_parent.size()) {
      return false;
    }
    constexpr auto sentinel{std::numeric_limits<index_type>::max()};
    auto map_a_to_b{std::vector<index_type>(m_parent.size(), sentinel)};
    auto map_b_to_a{std::vector<index_type>(other.m_parent.size(), sentinel)};
    for (size_type i{0}; i < m_parent.size(); ++i) {
      auto const root_a{root_of(static_cast<index_type>(i))};
      auto const root_b{other.root_of(static_cast<index_type>(i))};
      // The root mapping must be a bijection; checking both directions is what
      // catches "this set has too few or too many elements" differences.
      if (map_a_to_b[root_a] == sentinel) {
        map_a_to_b[root_a] = root_b;
      } else if (map_a_to_b[root_a] != root_b) {
        return false;
      }
      if (map_b_to_a[root_b] == sentinel) {
        map_b_to_a[root_b] = root_a;
      } else if (map_b_to_a[root_b] != root_a) {
        return false;
      }
    }
    return true;
  }
};

using union_find_u32 = union_find<std::uint32_t>;
using union_find_u16 = union_find<std::uint16_t>;
using union_find_u64 = union_find<std::uint64_t>;

}  // namespace nexenne::container
