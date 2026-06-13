#pragma once

/**
 * @file
 * @brief Ordered binary search tree with std::set-like semantics.
 *
 * \c binary_tree<T, Compare> stores unique values in an unbalanced binary search
 * tree ordered by \p Compare. Each node owns its children through
 * \c std::unique_ptr and keeps a raw back-pointer to its parent, so the in-order
 * iterator walks to the successor without an auxiliary stack and destruction
 * cascades automatically. The tree is deliberately unbalanced: insert, lookup,
 * and erase are \c O(log n) on random input but degrade to \c O(n) on sorted
 * input, so reach for a balanced structure when worst-case bounds matter. Its
 * niche is a simple, transparent BST whose explicit node graph is a feature, for
 * teaching, ordered sets of moderate size, and tree-visitor building blocks.
 *
 * The in-order iterator models \c std::forward_iterator and visits every element
 * once, ascending under \p Compare, so the standard algorithms and range-for work
 * directly. Child ownership through \c unique_ptr lets the destructor and move
 * operations be cheap (a move steals the whole graph in \c O(1)); only copy is
 * custom (a deep clone). Every operation is \c noexcept; allocation failure
 * terminates. Do not mutate an element's ordering key in place through an
 * iterator, as that would break the search invariant.
 */

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace nexenne::container {

/**
 * @brief Ordered, unique-keyed binary search tree.
 *
 * @tparam T Value type; must be move-constructible.
 * @tparam Compare Strict weak ordering over \p T; \c std::less<T> by default.
 *
 * @pre None.
 * @post A default-constructed tree is empty.
 */
template <std::move_constructible T, typename Compare = std::less<T>>
  requires std::strict_weak_order<Compare const&, T const&, T const&>
class binary_tree {
public:
  using value_type = T;
  using size_type = std::size_t;
  using key_compare = Compare;

private:
  struct node {
    T value;
    std::unique_ptr<node> left;
    std::unique_ptr<node> right;
    node* parent{nullptr};

    template <typename... Args>
    explicit constexpr node(Args&&... args) noexcept : value{std::forward<Args>(args)...} {}
  };

  using node_ptr = std::unique_ptr<node>;

  node_ptr m_root;
  size_type m_size{};
  Compare m_cmp{};

  template <bool IsConst>
  class basic_iterator {
  private:
    using raw_node_ptr = std::conditional_t<IsConst, node const*, node*>;

  public:
    using value_type = T;
    using reference = std::conditional_t<IsConst, T const&, T&>;
    using pointer = std::conditional_t<IsConst, T const*, T*>;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    constexpr basic_iterator() noexcept = default;

    explicit constexpr basic_iterator(raw_node_ptr n) noexcept : m_node{n} {}

    // Convert a mutable iterator to a const_iterator.
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_node{other.m_node} {}

    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return m_node->value;
    }

    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return std::addressof(m_node->value);
    }

    constexpr auto operator++() noexcept -> basic_iterator& {
      if (m_node->right != nullptr) {
        m_node = leftmost(m_node->right.get());
      } else {
        auto* p{m_node->parent};
        while (p != nullptr && m_node == p->right.get()) {
          m_node = p;
          p = p->parent;
        }
        m_node = p;
      }
      return *this;
    }

    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const tmp{*this};
      ++*this;
      return tmp;
    }

    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_node == b.m_node;
    }

  private:
    raw_node_ptr m_node{nullptr};

    static constexpr auto leftmost(raw_node_ptr n) noexcept -> raw_node_ptr {
      while (n != nullptr && n->left != nullptr) {
        n = n->left.get();
      }
      return n;
    }

    template <bool>
    friend class basic_iterator;
    friend class binary_tree;
  };

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;

  /**
   * @brief Constructs an empty tree with a default-constructed comparator.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr binary_tree() noexcept = default;

  /**
   * @brief Constructs an empty tree using a user-supplied comparator.
   *
   * @param cmp Strict-weak ordering comparator to store.
   *
   * @pre None.
   * @post \c empty() is \c true and the stored comparator is \p cmp.
   */
  explicit constexpr binary_tree(Compare cmp) noexcept : m_cmp{std::move(cmp)} {}

  ~binary_tree() noexcept = default;

  /**
   * @brief Move-constructs from \p other, stealing its node graph in O(1).
   *
   * @param other Source tree, left empty after the move.
   *
   * @pre None.
   * @post This tree owns \p other's former nodes; \p other is empty.
   */
  constexpr binary_tree(binary_tree&& other) noexcept
      : m_root{std::move(other.m_root)}, m_size{other.m_size}, m_cmp{std::move(other.m_cmp)} {
    other.m_size = 0;
  }

  /**
   * @brief Move-assigns from \p other, replacing the current contents.
   *
   * @param other Source tree, left empty after the move.
   *
   * @return Reference to this tree.
   *
   * @pre None.
   * @post This tree owns \p other's former nodes; \p other is empty.
   *       Self-assignment leaves the tree unchanged.
   */
  constexpr auto operator=(binary_tree&& other) noexcept -> binary_tree& {
    if (this != &other) {
      m_root = std::move(other.m_root);
      m_size = other.m_size;
      m_cmp = std::move(other.m_cmp);
      other.m_size = 0;
    }
    return *this;
  }

  /**
   * @brief Copy constructor; performs a structural deep clone of \p other.
   *
   * @param other Source tree to clone.
   *
   * @pre None.
   * @post This tree holds an independent copy of \p other's elements with
   *       \c size() equal to \p other's.
   *
   * @complexity \c O(n) in the source size.
   */
  constexpr binary_tree(binary_tree const& other) noexcept : m_cmp{other.m_cmp} {
    m_root = clone_subtree(other.m_root.get(), nullptr);
    m_size = other.m_size;
  }

  /**
   * @brief Copy-assigns a deep clone of \p other, replacing the contents.
   *
   * @param other Source tree to clone.
   *
   * @return Reference to this tree.
   *
   * @pre None.
   * @post This tree holds an independent copy of \p other's elements.
   *       Self-assignment leaves the tree unchanged.
   *
   * @complexity \c O(n) in the source size.
   */
  constexpr auto operator=(binary_tree const& other) noexcept -> binary_tree& {
    if (this != &other) {
      m_cmp = other.m_cmp;
      m_root = clone_subtree(other.m_root.get(), nullptr);
      m_size = other.m_size;
    }
    return *this;
  }

  /**
   * @brief Number of elements in the tree.
   *
   * @return Element count.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Whether the tree holds no elements.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Largest number of elements the tree could ever hold.
   *
   * @return The maximum value of \c size_type.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return std::numeric_limits<size_type>::max();
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Tree to exchange state with.
   *
   * @pre None.
   * @post This tree holds \p other's former elements and vice versa.
   */
  constexpr auto swap(binary_tree& other) noexcept -> void {
    using std::swap;
    m_root.swap(other.m_root);
    swap(m_size, other.m_size);
    swap(m_cmp, other.m_cmp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First tree.
   * @param b Second tree.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(binary_tree& a, binary_tree& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Destroys every node and resets the tree to empty.
   *
   * @pre None.
   * @post \c empty() is \c true. All iterators are invalidated.
   *
   * @complexity \c O(n).
   */
  constexpr auto clear() noexcept -> void {
    m_root.reset();
    m_size = 0;
  }

  /**
   * @brief Inserts a copy of \p value if no equivalent element is present.
   *
   * Equivalence is by \p Compare: \c a and \c b are equivalent when neither
   * \c cmp(a,b) nor \c cmp(b,a) holds.
   *
   * @param value Value to copy in.
   *
   * @return \c true on a fresh insertion, \c false when an equivalent value was
   *         already present.
   *
   * @pre None.
   * @post \p value (or an equivalent) is present; on a fresh insertion \c size()
   *       grew by one.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  constexpr auto insert(T const& value) noexcept -> bool {
    return emplace_impl(value);
  }

  /**
   * @brief Inserts \p value by moving it if no equivalent element is present.
   *
   * @param value Value to move in.
   *
   * @return \c true on a fresh insertion, \c false when an equivalent value was
   *         already present (\p value is left in a valid moved-from state only on
   *         a fresh insertion).
   *
   * @pre None.
   * @post \p value (or an equivalent) is present; on a fresh insertion \c size()
   *       grew by one.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  constexpr auto insert(T&& value) noexcept -> bool {
    return emplace_impl(std::move(value));
  }

  /**
   * @brief Constructs a value in place and inserts it.
   *
   * @tparam Args Constructor argument types for \p T.
   * @param args Arguments forwarded to \p T's constructor.
   *
   * @return \c true on a fresh insertion, \c false on an equivalent hit (the
   *         constructed value is then discarded).
   *
   * @pre None.
   * @post On a fresh insertion the constructed value is present and \c size()
   *       grew by one.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  template <typename... Args>
  constexpr auto emplace(Args&&... args) noexcept -> bool {
    return emplace_impl(T(std::forward<Args>(args)...));
  }

  /**
   * @brief Removes the element equivalent to \p value, if any.
   *
   * @param value Value to remove.
   *
   * @return \c true when a node was removed, \c false when no equivalent value
   *         was found.
   *
   * @pre None.
   * @post No element equivalent to \p value remains; on a removal \c size()
   *       shrank by one and iterators to the removed node are invalidated.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  constexpr auto erase(T const& value) noexcept -> bool {
    auto* const target{locate(value)};
    if (target == nullptr) {
      return false;
    }
    erase_node(target);
    --m_size;
    return true;
  }

  /**
   * @brief Pointer to the live element equivalent to \p value, or \c nullptr.
   *
   * @param value Value to search for.
   *
   * @return Non-owning pointer when present, otherwise \c nullptr.
   *
   * @pre None.
   * @post None. The tree is not modified.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  [[nodiscard]] constexpr auto find(T const& value) noexcept -> T* {
    auto* const n{locate(value)};
    return n == nullptr ? nullptr : std::addressof(n->value);
  }

  /**
   * @brief Pointer to the live const element equivalent to \p value, or
   *        \c nullptr.
   *
   * @param value Value to search for.
   *
   * @return Non-owning const pointer when present, otherwise \c nullptr.
   *
   * @pre None.
   * @post None. The tree is not modified.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  [[nodiscard]] constexpr auto find(T const& value) const noexcept -> T const* {
    auto const* const n{locate(value)};
    return n == nullptr ? nullptr : std::addressof(n->value);
  }

  /**
   * @brief Whether an element equivalent to \p value is in the tree.
   *
   * @param value Value to test for membership.
   *
   * @return \c true when an equivalent element is present.
   *
   * @pre None.
   * @post None. The tree is not modified.
   *
   * @complexity \c O(h), where \c h is the tree height.
   */
  [[nodiscard]] constexpr auto contains(T const& value) const noexcept -> bool {
    return locate(value) != nullptr;
  }

  /**
   * @brief In-order iterator to the smallest element.
   *
   * @return Iterator to the first element under \c Compare.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return iterator{leftmost(m_root.get())};
  }

  /**
   * @brief Iterator one past the last element.
   *
   * @return Past-the-end iterator.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return iterator{nullptr};
  }

  /**
   * @brief In-order const iterator to the smallest element.
   *
   * @return Const iterator to the first element under \c Compare.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return const_iterator{leftmost(m_root.get())};
  }

  /**
   * @brief Const iterator one past the last element.
   *
   * @return Past-the-end const iterator.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return const_iterator{nullptr};
  }

  /**
   * @brief In-order const iterator to the smallest element.
   *
   * @return Const iterator to the first element under \c Compare.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /**
   * @brief Const iterator one past the last element.
   *
   * @return Past-the-end const iterator.
   *
   * @pre None.
   * @post None. The tree is not modified.
   */
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return end();
  }

  /**
   * @brief Whether \p a and \p b hold the same elements in the same order.
   *
   * @param a First tree.
   * @param b Second tree.
   *
   * @return \c true when both have equal size and equal in-order elements.
   *
   * @pre None.
   * @post None. Neither tree is modified.
   *
   * @complexity \c O(n).
   */
  [[nodiscard]] friend auto
  operator==(binary_tree const& a, binary_tree const& b) noexcept -> bool {
    return a.m_size == b.m_size && std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographic ordering over the in-order traversals.
   *
   * @param a First tree.
   * @param b Second tree.
   *
   * @return The three-way comparison of the two element sequences, in \p T's own
   *         comparison category.
   *
   * @pre None.
   * @post None. Neither tree is modified.
   *
   * @complexity \c O(n).
   */
  [[nodiscard]] friend auto operator<=>(binary_tree const& a, binary_tree const& b) noexcept
    requires std::three_way_comparable<T>
  {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }

private:
  static auto leftmost(node* n) noexcept -> node* {
    while (n != nullptr && n->left != nullptr) {
      n = n->left.get();
    }
    return n;
  }

  static auto leftmost(node const* n) noexcept -> node const* {
    while (n != nullptr && n->left != nullptr) {
      n = n->left.get();
    }
    return n;
  }

  [[nodiscard]] constexpr auto locate(T const& value) noexcept -> node* {
    auto* cur{m_root.get()};
    while (cur != nullptr) {
      if (m_cmp(value, cur->value)) {
        cur = cur->left.get();
      } else if (m_cmp(cur->value, value)) {
        cur = cur->right.get();
      } else {
        return cur;
      }
    }
    return nullptr;
  }

  [[nodiscard]] constexpr auto locate(T const& value) const noexcept -> node const* {
    auto const* cur{m_root.get()};
    while (cur != nullptr) {
      if (m_cmp(value, cur->value)) {
        cur = cur->left.get();
      } else if (m_cmp(cur->value, value)) {
        cur = cur->right.get();
      } else {
        return cur;
      }
    }
    return nullptr;
  }

  template <typename V>
  constexpr auto emplace_impl(V&& value) noexcept -> bool {
    node* parent{nullptr};
    node_ptr* link{&m_root};
    while (*link != nullptr) {
      parent = link->get();
      if (m_cmp(value, parent->value)) {
        link = &parent->left;
      } else if (m_cmp(parent->value, value)) {
        link = &parent->right;
      } else {
        return false;
      }
    }
    auto fresh{std::make_unique<node>(std::forward<V>(value))};
    fresh->parent = parent;
    *link = std::move(fresh);
    ++m_size;
    return true;
  }

  // Locate the unique_ptr slot that owns n: the root slot, or one of the
  // parent's child slots.
  [[nodiscard]] constexpr auto owning_slot(node* const n) noexcept -> node_ptr* {
    if (n->parent == nullptr) {
      return &m_root;
    }
    return n == n->parent->left.get() ? &n->parent->left : &n->parent->right;
  }

  // Move v into slot, repointing its parent back-pointer to new_parent. Passing
  // the parent explicitly lets callers transplant nodes whose previous owner has
  // already been emptied.
  constexpr auto transplant(node_ptr& slot, node* const new_parent, node_ptr v) noexcept -> void {
    if (v != nullptr) {
      v->parent = new_parent;
    }
    slot = std::move(v);
  }

  constexpr auto erase_node(node* const z) noexcept -> void {
    auto* const z_parent{z->parent};
    auto& z_slot{*owning_slot(z)};

    if (z->left == nullptr) {
      transplant(z_slot, z_parent, std::move(z->right));
      return;
    }
    if (z->right == nullptr) {
      transplant(z_slot, z_parent, std::move(z->left));
      return;
    }

    // Two children: splice in the in-order successor (leftmost of z's right
    // subtree), which has no left child by construction.
    auto* const y{leftmost(z->right.get())};
    auto* const y_parent{y->parent};

    node_ptr y_owned;
    if (y_parent != z) {
      // y is buried in z's right subtree: y's right subtree takes y's old slot,
      // then y inherits z's right subtree.
      auto& y_slot{*owning_slot(y)};
      auto y_right{std::move(y->right)};
      y_owned = std::move(y_slot);
      transplant(y_slot, y_parent, std::move(y_right));
      y_owned->right = std::move(z->right);
      if (y_owned->right != nullptr) {
        y_owned->right->parent = y_owned.get();
      }
    } else {
      // y is z's immediate right child; take it directly, its right subtree
      // carries over unchanged.
      y_owned = std::move(z->right);
    }

    y_owned->left = std::move(z->left);
    y_owned->left->parent = y_owned.get();
    transplant(z_slot, z_parent, std::move(y_owned));
  }

  static auto clone_subtree(node const* const src, node* const parent) noexcept -> node_ptr {
    if (src == nullptr) {
      return nullptr;
    }
    auto fresh{std::make_unique<node>(src->value)};
    fresh->parent = parent;
    fresh->left = clone_subtree(src->left.get(), fresh.get());
    fresh->right = clone_subtree(src->right.get(), fresh.get());
    return fresh;
  }
};

}  // namespace nexenne::container
