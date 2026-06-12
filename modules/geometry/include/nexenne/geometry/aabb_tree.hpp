#pragma once

/**
 * @file
 * @brief Dynamic AABB tree (a bounding-volume hierarchy) for broad-phase queries.
 *
 * An \c aabb_tree<T, N, Real> stores a set of (box, payload) leaves in a binary
 * tree of axis-aligned boxes, each internal node bounding its two children. It is
 * the standard broad-phase structure: feed it every object's world box, then ask
 * it which boxes overlap a region or are hit by a ray, in \c O(log n + k) instead
 * of the \c O(n^2) all-pairs scan. Typical uses are physics broad phase, mouse
 * picking and projectile raycasts against many objects, and frustum or region
 * culling.
 *
 * Design:
 *   - Contiguous node pool. Every node lives in one \c std::vector<node> and
 *     refers to its children by index, not pointer, so traversal is
 *     cache-friendly and a handle stays a small \c uint32_t.
 *   - Free list. \c remove threads freed slots onto a singly-linked free list
 *     (reusing a child index field), so the pool never grows past its
 *     high-water mark and handles stay dense.
 *   - Fat boxes. Each leaf's stored box is padded outward by a configurable
 *     amount, so a small movement that still fits the fat box needs no tree
 *     surgery: \c update returns \c false and does nothing.
 *   - Self-balancing. Insertion picks a sibling by a surface-area heuristic and
 *     then rebalances with single rotations on the way up, keeping the tree
 *     near-balanced so query depth stays logarithmic.
 *
 * This is the dynamic tree of Box2D's \c b2DynamicTree and Bullet's
 * \c btDbvt, the broad-phase form described in Ericson, Real-Time Collision
 * Detection, chapter 6.
 *
 * Exception policy: every operation is \c noexcept; the tree allocates its node
 * pool, so an allocation failure terminates rather than reporting an error (the
 * same contract as the EPA polytope). Aliases are not provided because the
 * payload type is caller-chosen.
 */

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/intersect.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>

namespace nexenne::geometry {

/**
 * @brief A dynamic bounding-volume hierarchy of axis-aligned boxes.
 *
 * @tparam T Payload stored at each leaf (an entity id, an index, any small
 *           handle).
 * @tparam N Dimension, 2 or 3.
 * @tparam Real Component type for the box coordinates.
 */
template <typename T, std::size_t N = 3, std::floating_point Real = float>
class aabb_tree {
public:
  using payload_type = T;
  using bounds_type = aabb<Real, N>;
  using ray_type = ray<Real, N>;
  using handle_type = std::uint32_t;
  using size_type = std::size_t;

  /// @brief Sentinel handle meaning "no such node".
  static constexpr handle_type null_handle = std::numeric_limits<handle_type>::max();

private:
  // One node of the pool. An internal node has two children and a positive
  // height; a leaf has height 0 and carries a payload; a free slot has height -1
  // and reuses child_a as the "next free slot" link.
  struct node {
    bounds_type bounds{};
    handle_type parent{null_handle};
    handle_type child_a{null_handle};  // doubles as the free-list link when free.
    handle_type child_b{null_handle};
    std::int32_t height{-1};  // -1 free, 0 leaf, >0 internal.
    payload_type payload{};

    /**
     * @brief Whether this node is a leaf (holds a payload, no children).
     *
     * @return \c true when the node's height is 0.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool {
      return height == 0;
    }

    /**
     * @brief Whether this slot is free (on the free list, not in the tree).
     *
     * @return \c true when the node's height is negative.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] constexpr auto is_free() const noexcept -> bool {
      return height < 0;
    }
  };

  std::vector<node> m_nodes{};
  handle_type m_root{null_handle};
  handle_type m_free_head{null_handle};
  size_type m_leaf_count{0};
  Real m_padding{static_cast<Real>(0.1)};

public:
  /// @brief Constructs an empty tree with the default fat-box padding (0.1).
  aabb_tree() noexcept = default;

  /**
   * @brief Constructs an empty tree with a chosen fat-box padding.
   *
   * Larger padding means fewer tree restructures for moving objects, at the cost
   * of slightly more conservative queries (a few extra candidate pairs). About
   * 0.1 suits objects roughly one unit in size.
   *
   * @param padding Outward padding applied to each stored leaf box.
   *
   * @pre \p padding is non-negative.
   * @post \c empty() is \c true and the tree uses \p padding for new leaves.
   */
  explicit aabb_tree(Real const padding) noexcept : m_padding{padding} {}

  /**
   * @brief Inserts a leaf with the given world box and payload.
   *
   * The stored box is the padded (fat) box; the returned handle identifies the
   * leaf for later \c update and \c remove.
   *
   * @param bounds World box of the object.
   * @param payload Payload to store, moved into the leaf.
   *
   * @return Handle of the new leaf; never \c null_handle.
   *
   * @pre \p bounds is well-formed (\c min <= max componentwise).
   * @post \c size() grows by one; the returned handle is valid until removed.
   */
  auto insert(bounds_type const& bounds, payload_type payload) noexcept -> handle_type {
    auto const leaf{allocate_node()};
    m_nodes[leaf].bounds = fatten(bounds);
    m_nodes[leaf].payload = std::move(payload);
    m_nodes[leaf].height = 0;
    insert_leaf(leaf);
    ++m_leaf_count;
    return leaf;
  }

  /**
   * @brief Removes the leaf identified by \p h.
   *
   * @param h Handle returned by a prior \c insert.
   *
   * @return \c true on success; \c false when \p h is not a live leaf.
   *
   * @pre None. \p h is validated.
   * @post On success \c size() shrinks by one and \p h is no longer valid; on
   *       failure the tree is unchanged.
   */
  auto remove(handle_type const h) noexcept -> bool {
    if (h >= m_nodes.size() || !m_nodes[h].is_leaf()) {
      return false;
    }
    remove_leaf(h);
    free_node(h);
    --m_leaf_count;
    return true;
  }

  /**
   * @brief Moves leaf \p h to a new world box.
   *
   * When \p new_bounds still fits inside the leaf's current fat box (the common
   * case for slow movers) nothing is done and \c false is returned, so callers
   * can update every frame cheaply.
   *
   * @param h Handle of the leaf to move.
   * @param new_bounds New world box.
   *
   * @return \c true when the tree was restructured; \c false when the move fit
   *         the fat box (the only false case, so it is unambiguous, unlike a
   *         stale handle, which is a precondition violation, not a return value).
   *
   * @pre \p h is a live leaf handle and \p new_bounds is well-formed.
   * @post After a restructure the leaf's fat box contains \p new_bounds;
   *       \c size() is unchanged.
   */
  auto update(handle_type const h, bounds_type const& new_bounds) noexcept -> bool {
    assert(h < m_nodes.size() && m_nodes[h].is_leaf() && "update() requires a live leaf handle");
    if (contains_aabb(m_nodes[h].bounds, new_bounds)) {
      return false;  // still inside the fat box: no work.
    }
    remove_leaf(h);
    m_nodes[h].bounds = fatten(new_bounds);
    insert_leaf(h);
    return true;
  }

  /**
   * @brief Looks up a leaf's stored box and payload by handle.
   *
   * @param h Leaf handle.
   *
   * @return \c {bounds, payload} when \p h is a live leaf; \c std::nullopt
   *         otherwise.
   *
   * @pre None. \p h is validated.
   * @post The tree is not modified.
   */
  [[nodiscard]] auto at(handle_type const h
  ) const noexcept -> std::optional<std::pair<bounds_type, payload_type>> {
    if (h >= m_nodes.size() || !m_nodes[h].is_leaf()) {
      return std::nullopt;
    }
    return std::pair{m_nodes[h].bounds, m_nodes[h].payload};
  }

  /**
   * @brief Visits every leaf whose stored box overlaps \p region.
   *
   * \p visitor is called as \c visitor(handle, payload). A visitor returning
   * \c bool may stop the walk early by returning \c false; a \c void visitor
   * always continues.
   *
   * @tparam Visitor Callable applied to each overlapping leaf.
   * @param region Query box.
   * @param visitor Per-leaf callback.
   *
   * @pre \p region is well-formed.
   * @post The tree is not modified.
   */
  template <typename Visitor>
  auto query(bounds_type const& region, Visitor&& visitor) const noexcept -> void {
    // The visitor's return type must be exactly void (always continue) or bool
    // (return false to stop early); anything else (int, a wider type) would be
    // silently ignored, so pruning would quietly not happen. Make that loud.
    using result_type =
      decltype(visitor(std::declval<handle_type>(), std::declval<payload_type const&>()));
    static_assert(
      std::is_void_v<result_type> || std::is_same_v<result_type, bool>,
      "aabb_tree::query visitor must return void or bool"
    );
    if (m_root == null_handle) {
      return;
    }
    // An inline stack avoids the heap for the logarithmic depth of a balanced
    // tree; 64 holds far more than any realistic node count's depth (an
    // AVL-balanced tree of depth 62 would need about 2^43 leaves, past the handle
    // space), and an assert guards the impossible overflow rather than silently
    // dropping a subtree.
    auto stack{std::array<handle_type, 64>{}};
    auto top{std::size_t{0}};
    stack[top++] = m_root;
    while (top != 0) {
      auto const idx{stack[--top]};
      auto const& n{m_nodes[idx]};
      if (!intersects(n.bounds, region)) {
        continue;
      }
      if (n.is_leaf()) {
        if constexpr (std::is_same_v<result_type, bool>) {
          if (!visitor(idx, n.payload)) {
            return;
          }
        } else {
          visitor(idx, n.payload);
        }
      } else {
        assert(top + 2 <= stack.size() && "aabb_tree::query traversal stack overflow");
        stack[top++] = n.child_a;
        stack[top++] = n.child_b;
      }
    }
  }

  /**
   * @brief Visits every leaf whose stored box is hit by \p r within \p max_t.
   *
   * \p visitor is called as \c visitor(handle, payload, t), where \c t is the
   * ray's entry distance into the leaf box. A visitor returning \c Real updates
   * the working \p max_t (return the current value to keep it, or a smaller one
   * to prune farther leaves), which makes closest-hit queries a one-liner; a
   * \c void visitor leaves \p max_t unchanged.
   *
   * @tparam Visitor Callable applied to each hit leaf.
   * @param r Query ray.
   * @param max_t Farthest parametric distance to consider.
   * @param visitor Per-leaf callback.
   *
   * @pre \c r.direction() has unit length and \p max_t is non-negative.
   * @post The tree is not modified.
   */
  template <typename Visitor>
  auto raycast(ray_type const& r, Real max_t, Visitor&& visitor) const noexcept -> void {
    // The visitor's return type must be exactly void (leave max_t) or Real (the
    // new working max_t); any other type (bool, a double on a float tree) would be
    // silently ignored, so max_t pruning would quietly not happen. Make that loud.
    using result_type = decltype(visitor(
      std::declval<handle_type>(), std::declval<payload_type const&>(), std::declval<Real>()
    ));
    static_assert(
      std::is_void_v<result_type> || std::is_same_v<result_type, Real>,
      "aabb_tree::raycast visitor must return void or Real"
    );
    if (m_root == null_handle) {
      return;
    }
    // See query() for the inline-stack bound; the assert guards the impossible
    // overflow instead of silently dropping a subtree.
    auto stack{std::array<handle_type, 64>{}};
    auto top{std::size_t{0}};
    stack[top++] = m_root;
    while (top != 0) {
      auto const idx{stack[--top]};
      auto const& n{m_nodes[idx]};
      auto const hit{intersects(r, n.bounds)};
      if (!hit || *hit > max_t) {
        continue;
      }
      if (n.is_leaf()) {
        if constexpr (std::is_same_v<result_type, Real>) {
          max_t = visitor(idx, n.payload, *hit);
        } else {
          visitor(idx, n.payload, *hit);
        }
      } else {
        assert(top + 2 <= stack.size() && "aabb_tree::raycast traversal stack overflow");
        stack[top++] = n.child_a;
        stack[top++] = n.child_b;
      }
    }
  }

  /**
   * @brief Number of leaves in the tree.
   *
   * @return The leaf count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_leaf_count;
  }

  /**
   * @brief Reports whether the tree holds no leaves.
   *
   * @return \c true when the tree is empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_leaf_count == 0;
  }

  /**
   * @brief Removes every leaf and returns the tree to empty.
   *
   * @pre None.
   * @post \c empty() is \c true and every prior handle is invalid.
   */
  auto clear() noexcept -> void {
    m_nodes.clear();
    m_root = null_handle;
    m_free_head = null_handle;
    m_leaf_count = 0;
  }

  /**
   * @brief Height of the tree, the longest root-to-leaf path, for diagnostics.
   *
   * @return The height; 0 for an empty tree or a single leaf.
   *
   * @pre None.
   * @post The tree is not modified.
   */
  [[nodiscard]] auto height() const noexcept -> std::int32_t {
    return m_root == null_handle ? 0 : m_nodes[m_root].height;
  }

private:
  /**
   * @brief Returns a slot for a fresh node, reusing the free list when possible.
   *
   * Pops the free-list head when one exists, otherwise grows the pool by one
   * default-constructed node.
   *
   * @return Index of a usable node slot.
   *
   * @pre None.
   * @post The returned slot is owned by the caller and reset to a default node.
   */
  [[nodiscard]] auto allocate_node() noexcept -> handle_type {
    if (m_free_head != null_handle) {
      auto const idx{m_free_head};
      m_free_head = m_nodes[idx].child_a;  // pop the free-list head.
      m_nodes[idx] = node{};
      return idx;
    }
    auto const idx{static_cast<handle_type>(m_nodes.size())};
    m_nodes.emplace_back();
    return idx;
  }

  /**
   * @brief Returns a slot to the free list.
   *
   * Marks the slot free (height < 0) and threads it onto the free-list head
   * through the \c child_a field.
   *
   * @param idx Slot to free.
   *
   * @pre \p idx is a valid pool index not already free.
   * @post \p idx is the new free-list head, reads as free, and holds no payload.
   */
  auto free_node(handle_type const idx) noexcept -> void {
    m_nodes[idx].height = -1;
    // Release the payload now, not on the slot's eventual reuse: a caller-chosen
    // payload can own a resource (a shared_ptr, a handle), and a long-lived tree
    // must not pin the resources of removed leaves until the slot happens to be
    // reused.
    m_nodes[idx].payload = payload_type{};
    m_nodes[idx].child_a = m_free_head;
    m_free_head = idx;
  }

  /**
   * @brief Pads a box outward by the fat-box amount on every axis.
   *
   * @param b Tight world box.
   *
   * @return The box grown by the tree's padding on each side.
   *
   * @pre \p b is well-formed.
   * @post The result contains \p b.
   */
  [[nodiscard]] auto fatten(bounds_type const& b) const noexcept -> bounds_type {
    auto pad{nexenne::math::vector<Real, N>{}};
    for (auto i{std::size_t{0}}; i < N; ++i) {
      pad[i] = m_padding;
    }
    return bounds_type{b.min() - pad, b.max() + pad};
  }

  /**
   * @brief The surface-area-heuristic cost of a box.
   *
   * Uses surface area in 3D and perimeter in 2D, the dimension-appropriate
   * measure the insertion heuristic minimises.
   *
   * @param b Box to score.
   *
   * @return The box's surface area (3D) or perimeter (2D).
   *
   * @pre None.
   * @post The result is non-negative.
   */
  [[nodiscard]] static auto sah_cost(bounds_type const& b) noexcept -> Real {
    if constexpr (N == 2) {
      return perimeter(b);
    } else {
      return surface_area(b);
    }
  }

  /**
   * @brief Inserts an already-prepared leaf node into the tree.
   *
   * Descends from the root to the best sibling by the surface-area heuristic,
   * splices a new internal parent above that sibling, then refits and rebalances
   * the ancestors up to the root.
   *
   * @param leaf Handle of a leaf node whose bounds and payload are already set.
   *
   * @pre \p leaf is a leaf node not yet linked into the tree.
   * @post \p leaf is reachable from the root and the tree stays balanced.
   */
  auto insert_leaf(handle_type const leaf) noexcept -> void {
    if (m_root == null_handle) {
      m_root = leaf;
      m_nodes[leaf].parent = null_handle;
      return;
    }

    // 1. Descend from the root to the best sibling: at each step compare the
    // cost of creating the new parent here against the cost of pushing the leaf
    // into either child, and follow the cheaper option.
    auto const leaf_bounds{m_nodes[leaf].bounds};
    auto sibling{m_root};
    while (!m_nodes[sibling].is_leaf()) {
      auto const& s{m_nodes[sibling]};
      auto const child_a{s.child_a};
      auto const child_b{s.child_b};
      auto const combined{union_of(s.bounds, leaf_bounds)};
      auto const combined_cost{sah_cost(combined)};
      auto const cost_here{Real{2} * combined_cost};
      auto const cost_inherit{Real{2} * (combined_cost - sah_cost(s.bounds))};
      auto const a_combined{union_of(m_nodes[child_a].bounds, leaf_bounds)};
      auto const b_combined{union_of(m_nodes[child_b].bounds, leaf_bounds)};
      auto const cost_a{
        m_nodes[child_a].is_leaf()
          ? sah_cost(a_combined) + cost_inherit
          : (sah_cost(a_combined) - sah_cost(m_nodes[child_a].bounds)) + cost_inherit
      };
      auto const cost_b{
        m_nodes[child_b].is_leaf()
          ? sah_cost(b_combined) + cost_inherit
          : (sah_cost(b_combined) - sah_cost(m_nodes[child_b].bounds)) + cost_inherit
      };
      if (cost_here < cost_a && cost_here < cost_b) {
        break;
      }
      sibling = cost_a < cost_b ? child_a : child_b;
    }

    // 2. Splice a new internal parent above the chosen sibling, with the sibling
    // and the new leaf as its two children.
    auto const old_parent{m_nodes[sibling].parent};
    auto const new_parent{allocate_node()};
    m_nodes[new_parent].parent = old_parent;
    m_nodes[new_parent].bounds = union_of(leaf_bounds, m_nodes[sibling].bounds);
    m_nodes[new_parent].height = m_nodes[sibling].height + 1;
    m_nodes[new_parent].child_a = sibling;
    m_nodes[new_parent].child_b = leaf;

    if (old_parent != null_handle) {
      if (m_nodes[old_parent].child_a == sibling) {
        m_nodes[old_parent].child_a = new_parent;
      } else {
        m_nodes[old_parent].child_b = new_parent;
      }
    } else {
      m_root = new_parent;
    }
    m_nodes[sibling].parent = new_parent;
    m_nodes[leaf].parent = new_parent;

    // 3. Refit boxes and rebalance from the new parent up to the root.
    refit_and_rebalance(m_nodes[leaf].parent);
  }

  /**
   * @brief Detaches a leaf from the tree (without freeing its slot).
   *
   * Collapses the leaf's parent so the sibling takes the parent's place, frees
   * the parent slot, then refits and rebalances the remaining ancestors.
   *
   * @param leaf Handle of the leaf to unlink.
   *
   * @pre \p leaf is a leaf currently in the tree.
   * @post \p leaf is no longer reachable from the root; its own slot is not
   *       freed (the caller does that).
   */
  auto remove_leaf(handle_type const leaf) noexcept -> void {
    if (leaf == m_root) {
      m_root = null_handle;
      return;
    }
    auto const parent{m_nodes[leaf].parent};
    auto const grand{m_nodes[parent].parent};
    auto const sibling{
      m_nodes[parent].child_a == leaf ? m_nodes[parent].child_b : m_nodes[parent].child_a
    };
    if (grand != null_handle) {
      if (m_nodes[grand].child_a == parent) {
        m_nodes[grand].child_a = sibling;
      } else {
        m_nodes[grand].child_b = sibling;
      }
      m_nodes[sibling].parent = grand;
      free_node(parent);
      refit_and_rebalance(grand);
    } else {
      m_root = sibling;
      m_nodes[sibling].parent = null_handle;
      free_node(parent);
    }
  }

  /**
   * @brief Refits boxes and rebalances from a node up to the root.
   *
   * At each ancestor it balances the node (a single rotation if skewed), then
   * recomputes that node's height and bounds from its (possibly rotated)
   * children.
   *
   * @param idx Node to start from; the walk proceeds to the root.
   *
   * @pre \p idx is a valid node or \c null_handle.
   * @post Every node on the path to the root has a correct height and a box
   *       enclosing its children, and no subtree is skewed by more than one.
   */
  auto refit_and_rebalance(handle_type idx) noexcept -> void {
    while (idx != null_handle) {
      idx = balance(idx);
      auto& n{m_nodes[idx]};
      auto const& a{m_nodes[n.child_a]};
      auto const& b{m_nodes[n.child_b]};
      n.height = 1 + nexenne::math::max(a.height, b.height);
      n.bounds = union_of(a.bounds, b.bounds);
      idx = n.parent;
    }
  }

  /**
   * @brief Rebalances one subtree with a single rotation if it is skewed.
   *
   * If one child is more than one level taller than the other, rotates the taller
   * child up and returns the new subtree root; otherwise leaves the subtree
   * unchanged.
   *
   * @param a_idx Root of the subtree to check.
   *
   * @return The (possibly new) root of the subtree after balancing.
   *
   * @pre \p a_idx is a valid node.
   * @post The returned subtree root is skewed by at most one level.
   */
  [[nodiscard]] auto balance(handle_type const a_idx) noexcept -> handle_type {
    auto& a{m_nodes[a_idx]};
    if (a.is_leaf() || a.height < 2) {
      return a_idx;
    }
    auto const b_idx{a.child_a};
    auto const c_idx{a.child_b};
    auto const skew{m_nodes[c_idx].height - m_nodes[b_idx].height};
    if (skew > 1) {
      return rotate_left(a_idx, b_idx, c_idx);
    }
    if (skew < -1) {
      return rotate_right(a_idx, b_idx, c_idx);
    }
    return a_idx;
  }

  /**
   * @brief Rotates the right child above its parent (the right side is too tall).
   *
   * Promotes child \p c_idx above \p a_idx, keeps the taller of c's two children
   * with c, and demotes the shorter one to become a's new child, then fixes the
   * boxes and heights of both reshaped nodes.
   *
   * @param a_idx Subtree root being rotated down.
   * @param b_idx a's left child (stays under a).
   * @param c_idx a's right child, rotated up to become the new subtree root.
   *
   * @return The new subtree root, \p c_idx.
   *
   * @pre \p c_idx is internal and taller than \p b_idx by more than one.
   * @post \p c_idx is the new subtree root with consistent boxes and heights.
   */
  [[nodiscard]] auto rotate_left(
    handle_type const a_idx, handle_type const b_idx, handle_type const c_idx
  ) noexcept -> handle_type {
    auto& a{m_nodes[a_idx]};
    auto& c{m_nodes[c_idx]};
    auto const f_idx{c.child_a};
    auto const g_idx{c.child_b};
    auto& f{m_nodes[f_idx]};
    auto& g{m_nodes[g_idx]};

    c.child_a = a_idx;
    c.parent = a.parent;
    a.parent = c_idx;

    if (c.parent != null_handle) {
      if (m_nodes[c.parent].child_a == a_idx) {
        m_nodes[c.parent].child_a = c_idx;
      } else {
        m_nodes[c.parent].child_b = c_idx;
      }
    } else {
      m_root = c_idx;
    }

    if (f.height > g.height) {
      c.child_b = f_idx;
      a.child_b = g_idx;
      g.parent = a_idx;
      a.bounds = union_of(m_nodes[b_idx].bounds, g.bounds);
      c.bounds = union_of(a.bounds, f.bounds);
      a.height = 1 + nexenne::math::max(m_nodes[b_idx].height, g.height);
      c.height = 1 + nexenne::math::max(a.height, f.height);
    } else {
      c.child_b = g_idx;
      a.child_b = f_idx;
      f.parent = a_idx;
      a.bounds = union_of(m_nodes[b_idx].bounds, f.bounds);
      c.bounds = union_of(a.bounds, g.bounds);
      a.height = 1 + nexenne::math::max(m_nodes[b_idx].height, f.height);
      c.height = 1 + nexenne::math::max(a.height, g.height);
    }
    return c_idx;
  }

  /**
   * @brief Rotates the left child above its parent (mirror of \c rotate_left).
   *
   * Promotes child \p b_idx above \p a_idx, keeps the taller of b's two children
   * with b, and demotes the shorter one to become a's new child, then fixes the
   * boxes and heights of both reshaped nodes.
   *
   * @param a_idx Subtree root being rotated down.
   * @param b_idx a's left child, rotated up to become the new subtree root.
   * @param c_idx a's right child (stays under a).
   *
   * @return The new subtree root, \p b_idx.
   *
   * @pre \p b_idx is internal and taller than \p c_idx by more than one.
   * @post \p b_idx is the new subtree root with consistent boxes and heights.
   */
  [[nodiscard]] auto rotate_right(
    handle_type const a_idx, handle_type const b_idx, handle_type const c_idx
  ) noexcept -> handle_type {
    auto& a{m_nodes[a_idx]};
    auto& b{m_nodes[b_idx]};
    auto const d_idx{b.child_a};
    auto const e_idx{b.child_b};
    auto& d{m_nodes[d_idx]};
    auto& e{m_nodes[e_idx]};

    b.child_a = a_idx;
    b.parent = a.parent;
    a.parent = b_idx;

    if (b.parent != null_handle) {
      if (m_nodes[b.parent].child_a == a_idx) {
        m_nodes[b.parent].child_a = b_idx;
      } else {
        m_nodes[b.parent].child_b = b_idx;
      }
    } else {
      m_root = b_idx;
    }

    if (d.height > e.height) {
      b.child_b = d_idx;
      a.child_a = e_idx;
      e.parent = a_idx;
      a.bounds = union_of(m_nodes[c_idx].bounds, e.bounds);
      b.bounds = union_of(a.bounds, d.bounds);
      a.height = 1 + nexenne::math::max(m_nodes[c_idx].height, e.height);
      b.height = 1 + nexenne::math::max(a.height, d.height);
    } else {
      b.child_b = e_idx;
      a.child_a = d_idx;
      d.parent = a_idx;
      a.bounds = union_of(m_nodes[c_idx].bounds, d.bounds);
      b.bounds = union_of(a.bounds, e.bounds);
      a.height = 1 + nexenne::math::max(m_nodes[c_idx].height, d.height);
      b.height = 1 + nexenne::math::max(a.height, e.height);
    }
    return b_idx;
  }
};

}  // namespace nexenne::geometry
