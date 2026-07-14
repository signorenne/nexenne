#pragma once

/**
 * @file
 * @brief Intrusive doubly-linked list: the links live inside the element.
 *
 * Unlike \c std::list, which owns each node and allocates one per push, an
 * intrusive list owns nothing: the \c prev / \c next links live inside \p T
 * itself, embedded by deriving from \c intrusive_list_hook<T> (CRTP). The list
 * only manipulates those existing hooks, so insert, erase, and splice allocate
 * nothing, and erasing a known element is \c O(1) with no search. The price is
 * that an element lives in at most one list at a time (its hook holds one link
 * pair), and the caller, not the list, owns each element's lifetime.
 *
 * \code
 *   struct body : nexenne::container::intrusive_list_hook<body> { ... };
 *   nexenne::container::intrusive_list<body> active;
 *   active.push_back(a_body);
 *   active.erase(a_body);   // O(1), no search
 * \endcode
 *
 * Reach for it for active-object sets where one object cycles between lists each
 * frame (physics islands, animations, pending commands), LRU chains where an
 * element moves itself to the head, and ECS bookkeeping without a parallel map.
 * Every operation is \c noexcept. The list does not own nodes, so it is
 * non-copyable but movable (the move detaches the source).
 */

#include <algorithm>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include <nexenne/utility/discard.hpp>

namespace nexenne::container {

/**
 * @brief CRTP base that embeds the doubly-linked list links into \p T.
 *
 * @tparam T The deriving element type (CRTP); derives publicly from
 *           \c intrusive_list_hook<T>.
 *
 * @pre None.
 * @post A freshly constructed hook is unlinked.
 */
template <typename T>
class intrusive_list_hook {
private:
  template <typename U>
    requires(std::derived_from<U, intrusive_list_hook<U>>)
  friend class intrusive_list;

  intrusive_list_hook* m_prev{nullptr};
  intrusive_list_hook* m_next{nullptr};

public:
  constexpr intrusive_list_hook() noexcept = default;

  /**
   * @brief Debug-guards against destroying an element still linked into a list.
   *
   * Destroying a linked element leaves its neighbours pointing into freed
   * storage and the owning list's size stale, so the next traversal or erase is
   * undefined. The hook cannot unlink itself (it cannot reach the owning list's
   * size counter), so it can only assert.
   *
   * @pre The element is unlinked (a checked precondition in debug builds).
   * @post None.
   */
  constexpr ~intrusive_list_hook() noexcept {
    assert(!is_linked() && "destroying an element still linked into a list");
  }

  intrusive_list_hook(intrusive_list_hook const&) = delete;
  auto operator=(intrusive_list_hook const&) -> intrusive_list_hook& = delete;

  /**
   * @brief Move-constructs a detached hook, ignoring the source's links.
   *
   * The moved-to hook is left detached: the list stores the source's address,
   * not the new one, so the moved-to object is in no list. Detach the source
   * from its list before moving the element, or the list dangles at the old
   * address.
   *
   * @pre None.
   * @post The newly constructed hook is unlinked.
   */
  constexpr intrusive_list_hook(intrusive_list_hook&&) noexcept {}

  /**
   * @brief Move-assigns a detached hook, ignoring the source's links.
   *
   * The target must not itself be linked. Nulling the links of a still-linked
   * target severs the ring one hop past it (its neighbours still route through
   * it into now-null links), which the hook cannot repair because it cannot
   * decrement the owning list's size. Unlink the target first.
   *
   * @return Reference to this hook.
   *
   * @pre This hook is unlinked (a checked precondition in debug builds).
   * @post This hook is unlinked.
   */
  constexpr auto operator=(intrusive_list_hook&&) noexcept -> intrusive_list_hook& {
    assert(!is_linked() && "move-assigning onto an element still linked into a list");
    m_prev = nullptr;
    m_next = nullptr;
    return *this;
  }

  /**
   * @brief Reports whether the element is linked into a list.
   *
   * @return \c true when either link is non-null.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_linked() const noexcept -> bool {
    return m_prev != nullptr || m_next != nullptr;
  }
};

/**
 * @brief Doubly-linked list over elements that carry their own links.
 *
 * @tparam T Element type; must derive publicly from \c intrusive_list_hook<T>.
 *
 * @pre None.
 * @post A default-constructed list is empty.
 */
template <typename T>
  requires(std::derived_from<T, intrusive_list_hook<T>>)
class intrusive_list {
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = T const&;
  using pointer = T*;
  using const_pointer = T const*;

private:
  using hook_type = intrusive_list_hook<T>;

  // A circular sentinel so begin()/end() never need a null check and a
  // boundary insert is the same code path as a middle insert.
  mutable hook_type m_sentinel{};
  size_type m_size{0};

  /**
   * @brief Address of the circular sentinel node.
   *
   * @return A non-owning pointer to the sentinel hook.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto sentinel() const noexcept -> hook_type* {
    return std::addressof(m_sentinel);
  }

  /**
   * @brief Links \p a and \p b as adjacent nodes (\p a before \p b).
   *
   * @param a Node to become the predecessor.
   * @param b Node to become the successor.
   *
   * @pre \p a and \p b are non-null hooks.
   * @post \p a's next is \p b and \p b's prev is \p a.
   */
  static constexpr auto link(hook_type* const a, hook_type* const b) noexcept -> void {
    a->m_next = b;
    b->m_prev = a;
  }

  /**
   * @brief Resets the list to empty with the sentinel linked to itself.
   *
   * @pre None.
   * @post The sentinel points at itself and \c size() is zero.
   */
  constexpr auto init() noexcept -> void {
    m_sentinel.m_prev = sentinel();
    m_sentinel.m_next = sentinel();
    m_size = 0;
  }

  /**
   * @brief Splices every element out of \p other into this (empty) list.
   *
   * @param other Source list, left empty.
   *
   * @pre This list is empty.
   * @post This list holds \p other's former elements and \p other is empty.
   */
  constexpr auto steal_from(intrusive_list& other) noexcept -> void {
    if (other.empty()) {
      return;
    }
    auto* const first{other.m_sentinel.m_next};
    auto* const last{other.m_sentinel.m_prev};
    link(sentinel(), first);
    link(last, sentinel());
    m_size = other.m_size;
    other.init();
  }

  /**
   * @brief Bidirectional iterator over the list's elements.
   *
   * @tparam IsConst Whether the iterator yields \c const access to the elements.
   *
   * @pre None.
   * @post A default-constructed iterator is singular.
   */
  template <bool IsConst>
  class basic_iterator {
  private:
    using hook_ptr = std::conditional_t<IsConst, hook_type const*, hook_type*>;
    using value_ref = std::conditional_t<IsConst, T const&, T&>;
    using value_ptr = std::conditional_t<IsConst, T const*, T*>;

    hook_ptr m_node{nullptr};

  public:
    using value_type = T;
    using reference = value_ref;
    using pointer = value_ptr;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;

    constexpr basic_iterator() noexcept = default;

    /**
     * @brief Constructs an iterator positioned at hook \p node.
     *
     * @param node Hook to point at (an element hook or the sentinel).
     *
     * @pre None.
     * @post The iterator refers to \p node.
     */
    explicit constexpr basic_iterator(hook_ptr const node) noexcept : m_node{node} {}

    /**
     * @brief Converts a mutable iterator to a const iterator.
     *
     * @tparam OtherConst Constness of the source iterator; the overload is
     *         enabled only when converting a mutable iterator into a const one.
     * @param other Source iterator whose position is copied.
     *
     * @pre None.
     * @post This iterator refers to the same hook as \p other.
     */
    template <bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr basic_iterator(basic_iterator<OtherConst> const& other) noexcept
        : m_node{other.m_node} {}

    /**
     * @brief Accesses the referenced element.
     *
     * @return Reference to the current element.
     *
     * @pre The iterator is dereferenceable (not \c end()).
     * @post None.
     */
    [[nodiscard]] constexpr auto operator*() const noexcept -> reference {
      return static_cast<reference>(*m_node);
    }

    /**
     * @brief Accesses a member of the referenced element.
     *
     * @return Pointer to the current element.
     *
     * @pre The iterator is dereferenceable (not \c end()).
     * @post None.
     */
    [[nodiscard]] constexpr auto operator->() const noexcept -> pointer {
      return static_cast<pointer>(m_node);
    }

    /**
     * @brief Advances to the next element.
     *
     * @return Reference to this iterator after advancing.
     *
     * @pre The iterator is not \c end().
     * @post The iterator refers to the next element or to \c end().
     */
    constexpr auto operator++() noexcept -> basic_iterator& {
      m_node = m_node->m_next;
      return *this;
    }

    /**
     * @brief Advances to the next element, returning the prior position.
     *
     * @return A copy of the iterator as it was before advancing.
     *
     * @pre The iterator is not \c end().
     * @post The iterator refers to the next element or to \c end().
     */
    constexpr auto operator++(int) noexcept -> basic_iterator {
      auto const copy{*this};
      ++*this;
      return copy;
    }

    /**
     * @brief Retreats to the previous element.
     *
     * @return Reference to this iterator after retreating.
     *
     * @pre The iterator is not \c begin().
     * @post The iterator refers to the previous element.
     */
    constexpr auto operator--() noexcept -> basic_iterator& {
      m_node = m_node->m_prev;
      return *this;
    }

    /**
     * @brief Retreats to the previous element, returning the prior position.
     *
     * @return A copy of the iterator as it was before retreating.
     *
     * @pre The iterator is not \c begin().
     * @post The iterator refers to the previous element.
     */
    constexpr auto operator--(int) noexcept -> basic_iterator {
      auto const copy{*this};
      --*this;
      return copy;
    }

    /**
     * @brief Whether \p a and \p b refer to the same hook.
     *
     * @param a First iterator.
     * @param b Second iterator.
     *
     * @return \c true when both refer to the same hook.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] friend constexpr auto
    operator==(basic_iterator const& a, basic_iterator const& b) noexcept -> bool {
      return a.m_node == b.m_node;
    }

    template <bool>
    friend class basic_iterator;
    friend class intrusive_list;
  };

public:
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /**
   * @brief Constructs an empty list.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr intrusive_list() noexcept {
    init();
  }

  intrusive_list(intrusive_list const&) = delete;
  auto operator=(intrusive_list const&) -> intrusive_list& = delete;

  /**
   * @brief Move-constructs by stealing \p other's elements.
   *
   * @param other Source list, left empty.
   *
   * @pre None.
   * @post This list holds \p other's former elements; \p other is empty.
   */
  constexpr intrusive_list(intrusive_list&& other) noexcept {
    init();
    steal_from(other);
  }

  /**
   * @brief Move-assigns by detaching this list's elements and stealing
   *        \p other's.
   *
   * @param other Source list, left empty.
   *
   * @return Reference to this list.
   *
   * @pre None.
   * @post This list holds \p other's former elements; \p other is empty.
   */
  constexpr auto operator=(intrusive_list&& other) noexcept -> intrusive_list& {
    if (this != &other) {
      clear();
      steal_from(other);
    }
    return *this;
  }

  /**
   * @brief Detaches every element; elements are not destroyed.
   *
   * @pre None.
   * @post Every former element is unlinked; the caller still owns them.
   */
  constexpr ~intrusive_list() noexcept {
    clear();
    // The sentinel is deliberately self-linked while the list is alive; null its
    // links before it is destroyed so the hook's own "still linked" debug guard
    // (which fires for genuine elements) does not trip on the sentinel.
    m_sentinel.m_prev = nullptr;
    m_sentinel.m_next = nullptr;
  }

  /**
   * @brief Number of linked elements.
   *
   * @return The element count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Reports whether the list holds no elements.
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
   * @brief The largest number of elements the list can hold.
   *
   * @return The maximum size.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return std::numeric_limits<size_type>::max();
  }

  /**
   * @brief Swaps contents with \p other in constant time.
   *
   * @param other List to exchange state with.
   *
   * @pre None.
   * @post This list and \p other have exchanged elements.
   *
   * @complexity \c O(1).
   */
  constexpr auto swap(intrusive_list& other) noexcept -> void {
    if (this == &other) {
      return;
    }
    auto temp{intrusive_list{}};
    temp.steal_from(other);
    other.steal_from(*this);
    steal_from(temp);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First list.
   * @param b Second list.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(intrusive_list& a, intrusive_list& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Detaches every element; the caller still owns them.
   *
   * @pre None.
   * @post \c size() is zero and each former element is unlinked.
   */
  constexpr auto clear() noexcept -> void {
    auto* node{m_sentinel.m_next};
    while (node != sentinel()) {
      auto* const next{node->m_next};
      node->m_prev = nullptr;
      node->m_next = nullptr;
      node = next;
    }
    init();
  }

  /**
   * @brief Links \p value into the list immediately before \p it.
   *
   * @param it Position to insert before; may be \c end().
   * @param value Element to link in; the list takes no ownership.
   *
   * @return An iterator to the inserted element.
   *
   * @pre \p value is not already linked into a list and \p it refers to a
   *      position in this list.
   * @post \c size() grew by one and \p value sits just before \p it.
   *
   * @complexity \c O(1).
   */
  constexpr auto insert(const_iterator const it, T& value) noexcept -> iterator {
    auto* const fresh{static_cast<hook_type*>(std::addressof(value))};
    assert(
      fresh->m_prev == nullptr && fresh->m_next == nullptr
      && "inserting an element already linked into a list"
    );
    auto* const next{const_cast<hook_type*>(it.m_node)};
    auto* const prev{next->m_prev};
    link(prev, fresh);
    link(fresh, next);
    ++m_size;
    return iterator{fresh};
  }

  /**
   * @brief Links \p value at the back of the list.
   *
   * @param value Element to link in; the list takes no ownership.
   *
   * @pre \p value is not already linked into a list.
   * @post \c size() grew by one and \p value is the new back.
   *
   * @complexity \c O(1).
   */
  constexpr auto push_back(T& value) noexcept -> void {
    nexenne::utility::discard(insert(end(), value));
  }

  /**
   * @brief Links \p value at the front of the list.
   *
   * @param value Element to link in; the list takes no ownership.
   *
   * @pre \p value is not already linked into a list.
   * @post \c size() grew by one and \p value is the new front.
   *
   * @complexity \c O(1).
   */
  constexpr auto push_front(T& value) noexcept -> void {
    nexenne::utility::discard(insert(begin(), value));
  }

  /**
   * @brief Unlinks \p value from this list.
   *
   * The caller is responsible for knowing the element is in this list; the list
   * does not verify it.
   *
   * @param value Element to unlink.
   *
   * @pre \p value is currently linked into this list.
   * @post \p value is unlinked and \c size() shrank by one; \p value is not
   *       destroyed.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase(T& value) noexcept -> void {
    auto* const node{static_cast<hook_type*>(std::addressof(value))};
    assert(
      node->m_prev != nullptr && node->m_next != nullptr
      && "erasing an element not linked into a list"
    );
    link(node->m_prev, node->m_next);
    node->m_prev = nullptr;
    node->m_next = nullptr;
    --m_size;
  }

  /**
   * @brief Unlinks the element \p it points at and returns the next iterator.
   *
   * @param it Iterator to the element to remove; must be dereferenceable.
   *
   * @return An iterator to the element following the removed one.
   *
   * @pre \p it refers to an element of this list, not \c end().
   * @post The element is unlinked and \c size() shrank by one; it is not
   *       destroyed.
   *
   * @complexity \c O(1).
   */
  constexpr auto erase(const_iterator const it) noexcept -> iterator {
    assert(it.m_node != sentinel() && "erasing end()");
    auto* const node{const_cast<hook_type*>(it.m_node)};
    auto* const next{node->m_next};
    erase(static_cast<T&>(*node));
    return iterator{next};
  }

  /**
   * @brief Unlinks the front element and returns a pointer to it.
   *
   * @return A pointer to the former front element, or \c nullptr when empty.
   *
   * @pre None.
   * @post On success the front element is unlinked and \c size() shrank by one;
   *       it is not destroyed.
   *
   * @complexity \c O(1).
   */
  constexpr auto pop_front() noexcept -> T* {
    if (empty()) {
      return nullptr;
    }
    auto* const node{m_sentinel.m_next};
    erase(static_cast<T&>(*node));
    return static_cast<T*>(node);
  }

  /**
   * @brief Unlinks the back element and returns a pointer to it.
   *
   * @return A pointer to the former back element, or \c nullptr when empty.
   *
   * @pre None.
   * @post On success the back element is unlinked and \c size() shrank by one;
   *       it is not destroyed.
   *
   * @complexity \c O(1).
   */
  constexpr auto pop_back() noexcept -> T* {
    if (empty()) {
      return nullptr;
    }
    auto* const node{m_sentinel.m_prev};
    erase(static_cast<T&>(*node));
    return static_cast<T*>(node);
  }

  /**
   * @brief Pointer to the front element, or \c nullptr when empty.
   *
   * @return A pointer to the front element, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto front() noexcept -> T* {
    return empty() ? nullptr : static_cast<T*>(m_sentinel.m_next);
  }

  /// @copydoc front()
  [[nodiscard]] constexpr auto front() const noexcept -> T const* {
    return empty() ? nullptr : static_cast<T const*>(m_sentinel.m_next);
  }

  /**
   * @brief Pointer to the back element, or \c nullptr when empty.
   *
   * @return A pointer to the back element, or \c nullptr.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto back() noexcept -> T* {
    return empty() ? nullptr : static_cast<T*>(m_sentinel.m_prev);
  }

  /// @copydoc back()
  [[nodiscard]] constexpr auto back() const noexcept -> T const* {
    return empty() ? nullptr : static_cast<T const*>(m_sentinel.m_prev);
  }

  /**
   * @brief Iterator to the first element.
   *
   * @return An iterator to the front element, or \c end() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() noexcept -> iterator {
    return iterator{m_sentinel.m_next};
  }

  /**
   * @brief Iterator one past the last element.
   *
   * @return A past-the-end iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() noexcept -> iterator {
    return iterator{sentinel()};
  }

  /**
   * @brief Const iterator to the first element.
   *
   * @return A const iterator to the front element, or \c end() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator {
    return const_iterator{m_sentinel.m_next};
  }

  /**
   * @brief Const iterator one past the last element.
   *
   * @return A past-the-end const iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
    return const_iterator{sentinel()};
  }

  /**
   * @brief Const iterator to the first element.
   *
   * @return A const iterator to the front element, or \c cend() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator {
    return begin();
  }

  /**
   * @brief Const iterator one past the last element.
   *
   * @return A past-the-end const iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator {
    return end();
  }

  /**
   * @brief Reverse iterator to the last element.
   *
   * @return A reverse iterator to the back element, or \c rend() when empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto rbegin() noexcept -> reverse_iterator {
    return reverse_iterator{end()};
  }

  /**
   * @brief Reverse iterator one before the first element.
   *
   * @return A past-the-end reverse iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto rend() noexcept -> reverse_iterator {
    return reverse_iterator{begin()};
  }

  /**
   * @brief Const reverse iterator to the last element.
   *
   * @return A const reverse iterator to the back element, or \c rend() when
   *         empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto rbegin() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{end()};
  }

  /**
   * @brief Const reverse iterator one before the first element.
   *
   * @return A past-the-end const reverse iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto rend() const noexcept -> const_reverse_iterator {
    return const_reverse_iterator{begin()};
  }

  /**
   * @brief Const reverse iterator to the last element.
   *
   * @return A const reverse iterator to the back element, or \c crend() when
   *         empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator {
    return rbegin();
  }

  /**
   * @brief Const reverse iterator one before the first element.
   *
   * @return A past-the-end const reverse iterator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator {
    return rend();
  }

  /**
   * @brief Equality over the element sequences.
   *
   * Two lists compare equal when they hold equal-valued elements in order, even
   * if the underlying element addresses differ; this is a value comparison, not
   * an identity comparison.
   *
   * @param a First list.
   * @param b Second list.
   *
   * @return \c true when both hold equal elements in the same order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(intrusive_list const& a, intrusive_list const& b) noexcept(
    noexcept(std::declval<T const&>() == std::declval<T const&>())
  ) -> bool
    requires std::equality_comparable<T>
  {
    return a.m_size == b.m_size && std::equal(a.begin(), a.end(), b.begin(), b.end());
  }

  /**
   * @brief Lexicographical ordering over the element sequences.
   *
   * @param a First list.
   * @param b Second list.
   *
   * @return The three-way comparison of the two sequences.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(intrusive_list const& a, intrusive_list const& b) noexcept(
    noexcept(std::declval<T const&>() <=> std::declval<T const&>())
  )
    requires std::three_way_comparable<T>
  {
    return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
  }
};

}  // namespace nexenne::container
