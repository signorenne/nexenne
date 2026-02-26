#pragma once

/**
 * @file
 * @brief Lowest Common Ancestor queries for static rooted trees.
 *
 * Binary lifting over a sparse ancestor table.
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Lowest-common-ancestor index for a static rooted tree.
 *
 * Built once from a parent array via binary lifting, then answers ancestor
 * queries in logarithmic time. The tree is described by a parent array where
 * \c parent[i] is the parent of node \c i and the root's parent is itself. Suited
 * to any tree under a few million nodes; for an \c O(1)-query budget use
 * Euler-tour plus range-minimum query instead.
 *
 * @tparam Node Integer node-id type; \c std::int32_t covers any practical tree.
 */
template <std::integral Node = std::int32_t>
class lca {
public:
  using node_type = Node;         ///< Node identifier type.
  using size_type = std::size_t;  ///< Type for depths and table indices.

private:
  size_type m_n{0};
  size_type m_log{0};
  std::vector<size_type> m_depth{};
  std::vector<Node> m_up{};  // flat lift table: m_up[k * m_n + v] = 2^k-th ancestor of v

  // Flat index into the binary-lifting table: the 2^k-th ancestor of v. One
  // contiguous allocation and one indirection per lift (vs a vector-of-vectors).
  [[nodiscard]] constexpr auto up_at(size_type const k, Node const v) const noexcept -> Node {
    return m_up[k * m_n + static_cast<size_type>(v)];
  }

  auto compute_depth(std::span<Node const> parent, Node root) -> void {
    // Topological order by walking from root; child indices reachable via
    // inverted parent. For an N-sized tree this is one pass. We use BFS to fill
    // depth so we don't recurse (stack-safe).
    std::vector<std::vector<Node>> children(m_n);
    for (size_type i{0}; i < m_n; ++i) {
      auto const p{parent[i]};
      if (p != static_cast<Node>(i)) {
        children[static_cast<size_type>(p)].push_back(static_cast<Node>(i));
      }
    }
    std::vector<Node> queue;
    queue.reserve(m_n);
    queue.push_back(root);
    m_depth[static_cast<size_type>(root)] = 0;
    for (size_type head{0}; head < queue.size(); ++head) {
      auto const u{queue[head]};
      for (auto const c : children[static_cast<size_type>(u)]) {
        m_depth[static_cast<size_type>(c)] = m_depth[static_cast<size_type>(u)] + 1;
        queue.push_back(c);
      }
    }
  }

public:
  /**
   * @brief Builds the binary-lifting table for a tree.
   *
   * Computes node depths by a breadth-first pass from \p root, then fills the
   * sparse ancestor table so each query runs in logarithmic time. An empty
   * \p parent leaves the index empty. Calling \c build again replaces any
   * previously built tree.
   *
   * @param parent Parent array; \c parent[i] is the parent of node \c i, and the
   *        root satisfies \c parent[root] == root.
   * @param root Root node of the tree.
   *
   * @pre \p root is a valid index into \p parent, \c parent[root] == root, and
   *      following \p parent from any node reaches \p root.
   * @post The index answers \c query and \c depth_of for the supplied tree.
   *
   * @complexity \c O(N log N) time and space in the node count \c N.
   */
  auto build(std::span<Node const> parent, Node const root) -> void {
    m_n = parent.size();
    if (m_n == 0) {
      return;
    }

    m_log = static_cast<size_type>(std::bit_width(m_n));  // ceil(log2(n))+1
    if (m_log == 0) {
      m_log = 1;
    }

    m_depth.assign(m_n, 0);
    compute_depth(parent, root);

    m_up.assign(m_log * m_n, root);
    for (size_type v{0}; v < m_n; ++v) {
      m_up[v] = parent[v];  // row k == 0
    }
    for (size_type k{1}; k < m_log; ++k) {
      for (size_type v{0}; v < m_n; ++v) {
        auto const mid{m_up[(k - 1) * m_n + v]};  // 2^(k-1)-th ancestor of v
        m_up[k * m_n + v] = up_at(k - 1, mid);    // and 2^(k-1) above that
      }
    }
  }

  /**
   * @brief Depth of node \p v measured from the root.
   *
   * @param v Node whose depth is requested.
   *
   * @return The number of edges from the root to \p v; the root has depth 0.
   *
   * @pre \c build has been called and \p v is a valid node of that tree.
   * @post The index is unchanged.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto depth_of(Node const v) const noexcept -> size_type {
    return m_depth[static_cast<size_type>(v)];
  }

  /**
   * @brief Lowest common ancestor of nodes \p u and \p v.
   *
   * Lifts the deeper node to the depth of the shallower, then lifts both in
   * lock-step until their parents coincide.
   *
   * @param u First node.
   * @param v Second node.
   *
   * @return The deepest node that is an ancestor of both \p u and \p v.
   *
   * @pre \c build has been called and both \p u and \p v are valid nodes of that
   *      tree.
   * @post The index is unchanged.
   *
   * @complexity \c O(log N) in the node count \c N.
   */
  [[nodiscard]] auto query(Node u, Node v) const noexcept -> Node {
    if (m_depth[static_cast<size_type>(u)] < m_depth[static_cast<size_type>(v)]) {
      std::swap(u, v);
    }
    // Lift u up to v's depth.
    auto diff{m_depth[static_cast<size_type>(u)] - m_depth[static_cast<size_type>(v)]};
    for (size_type k{0}; diff != 0; ++k, diff >>= 1) {
      if (diff & 1) {
        u = up_at(k, u);
      }
    }
    if (u == v) {
      return u;
    }
    // Binary-lift both up until just below the LCA.
    for (size_type k{m_log}; k > 0; k -= 1) {
      auto const kk{k - 1};
      if (up_at(kk, u) != up_at(kk, v)) {
        u = up_at(kk, u);
        v = up_at(kk, v);
      }
    }
    return up_at(0, u);
  }
};

}  // namespace nexenne::algorithm
