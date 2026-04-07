#pragma once

/**
 * @file
 * @brief Prefix-keyed map (trie) over sequences of Char.
 *
 * \c trie<Char, Value> maps sequences of characters (any integral token type) to
 * a \p Value. Each node owns its children through \c std::unique_ptr and stores
 * them in a \c flat_map keyed by the next character, so per-node memory scales
 * with the number of live child edges, not the size of the character alphabet,
 * and the structure works for byte characters and wide tokens (Unicode code
 * points, command-token ids) alike. Lookup, insertion, and erasure are \c O(k)
 * in the key length (times a small \c O(log children) per level).
 *
 * Keys are arbitrary forward ranges of \p Char, so the same trie handles
 * \c std::string, \c std::string_view, and \c std::vector<std::uint32_t>. Reach
 * for it for command parsers and autocomplete (prefix queries), shared-prefix
 * name lookup, routing tables, and dictionaries. Child ownership through
 * \c unique_ptr lets the destructor and move be cheap (a move steals the graph);
 * copy is a custom deep clone. Every operation is \c noexcept; allocation failure
 * terminates.
 */

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/flat_map.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::container {

/**
 * @brief Prefix-keyed map over sequences of \p Char.
 *
 * @tparam Char Character or token type; any integral type.
 * @tparam Value Mapped value type; must be move-constructible.
 *
 * @pre None.
 * @post A default-constructed trie is empty.
 */
template <std::integral Char, std::move_constructible Value>
class trie {
private:
  using uchar_type = std::make_unsigned_t<Char>;

  struct node {
    flat_map<uchar_type, std::unique_ptr<node>> children;
    std::optional<Value> value;

    constexpr node() noexcept = default;

    // Tear descendants down iteratively. The default recursive unique_ptr
    // destruction would overflow the stack on a deep trie, whose depth can reach
    // the longest stored key length. Every teardown path (the trie destructor,
    // clear, and both assignments) reseats a node unique_ptr and so routes
    // through here.
    constexpr ~node() noexcept {
      std::vector<std::unique_ptr<node>> pending;
      auto detach{[&pending](node& n) noexcept {
        for (auto& entry : n.children) {
          if (entry.second != nullptr) {
            pending.push_back(std::move(entry.second));
          }
        }
        n.children.clear();
      }};
      detach(*this);
      while (!pending.empty()) {
        auto victim{std::move(pending.back())};
        pending.pop_back();
        detach(*victim);  // victim then destructs with no children, so O(1)
      }
    }
  };

  std::unique_ptr<node> m_root{std::make_unique<node>()};
  std::size_t m_size{};

public:
  using char_type = Char;
  using value_type = Value;
  using size_type = std::size_t;

  /**
   * @brief Constructs an empty trie.
   *
   * @pre None.
   * @post \c empty() is \c true.
   */
  constexpr trie() noexcept = default;

  ~trie() noexcept = default;

  /**
   * @brief Copy constructor; performs a structural deep clone of \p other.
   *
   * @param other Source trie to clone.
   *
   * @pre None.
   * @post This trie holds an independent copy of \p other's entries.
   *
   * @complexity \c O(total nodes) in the source.
   */
  constexpr trie(trie const& other) noexcept
      : m_root{clone_subtree(other.m_root.get())}, m_size{other.m_size} {}

  /**
   * @brief Copy-assigns a deep clone of \p other, replacing the contents.
   *
   * @param other Source trie to clone.
   *
   * @return Reference to this trie.
   *
   * @pre None.
   * @post This trie holds an independent copy of \p other's entries.
   *       Self-assignment leaves the trie unchanged.
   *
   * @complexity \c O(total nodes) in the source.
   */
  constexpr auto operator=(trie const& other) noexcept -> trie& {
    if (this != &other) {
      m_root = clone_subtree(other.m_root.get());
      m_size = other.m_size;
    }
    return *this;
  }

  /**
   * @brief Move-constructs from \p other, stealing its node graph in O(1).
   *
   * @param other Source trie, left empty after the move.
   *
   * @pre None.
   * @post This trie owns \p other's former nodes; \p other is empty.
   */
  constexpr trie(trie&& other) noexcept : m_root{std::move(other.m_root)}, m_size{other.m_size} {
    other.m_root = std::make_unique<node>();
    other.m_size = 0;
  }

  /**
   * @brief Move-assigns from \p other, replacing the contents.
   *
   * @param other Source trie, left empty after the move.
   *
   * @return Reference to this trie.
   *
   * @pre None.
   * @post This trie owns \p other's former nodes; \p other is empty.
   *       Self-assignment leaves the trie unchanged.
   */
  constexpr auto operator=(trie&& other) noexcept -> trie& {
    if (this != &other) {
      m_root = std::move(other.m_root);
      m_size = other.m_size;
      other.m_root = std::make_unique<node>();
      other.m_size = 0;
    }
    return *this;
  }

  /**
   * @brief Number of stored key-value entries.
   *
   * @return Entry count.
   *
   * @pre None.
   * @post None. The trie is not modified.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> size_type {
    return m_size;
  }

  /**
   * @brief Whether the trie holds no entries.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The trie is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_size == 0;
  }

  /**
   * @brief Largest number of entries the trie could ever hold.
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
   * @param other Trie to exchange state with.
   *
   * @pre None.
   * @post This trie holds \p other's former entries and vice versa.
   */
  constexpr auto swap(trie& other) noexcept -> void {
    using std::swap;
    m_root.swap(other.m_root);
    swap(m_size, other.m_size);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First trie.
   * @param b Second trie.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(trie& a, trie& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Removes every entry, resetting to an empty root.
   *
   * @pre None.
   * @post \c empty() is \c true.
   *
   * @complexity \c O(total nodes).
   */
  constexpr auto clear() noexcept -> void {
    m_root = std::make_unique<node>();
    m_size = 0;
  }

  /**
   * @brief Stores \p value at \p key, replacing any existing value there.
   *
   * @tparam KeyRange A forward range whose elements convert to \p Char.
   * @param key Sequence of characters forming the key.
   * @param value Value to store, moved in.
   *
   * @return \c true on a fresh insertion, \c false when an existing value was
   *         replaced.
   *
   * @pre None.
   * @post The trie holds \p value at \p key; on a fresh insertion \c size() grew
   *       by one, on a replacement it is unchanged.
   *
   * @complexity \c O(k log a), for key length \c k and per-node alphabet \c a.
   */
  template <std::ranges::forward_range KeyRange>
  constexpr auto insert(KeyRange&& key, Value value) noexcept -> bool {
    auto* cur{m_root.get()};
    for (auto const& c : key) {
      auto const uc{static_cast<uchar_type>(c)};
      if (auto* const slot{cur->children.at(uc)}) {
        cur = slot->get();
      } else {
        auto fresh{std::make_unique<node>()};
        auto* const raw{fresh.get()};
        nexenne::utility::discard(cur->children.try_emplace(uc, std::move(fresh)));
        cur = raw;
      }
    }
    auto const inserted{!cur->value.has_value()};
    cur->value.emplace(std::move(value));
    if (inserted) {
      ++m_size;
    }
    return inserted;
  }

  /**
   * @brief Erases the value at \p key, pruning now-empty internal nodes.
   *
   * @tparam KeyRange A forward range whose elements convert to \p Char.
   * @param key Sequence of characters forming the key.
   *
   * @return \c true on a removal, \c false when the key was absent.
   *
   * @pre None.
   * @post On \c true the value at \p key is gone, \c size() shrank by one, and
   *       now-empty internal nodes are pruned; otherwise the trie is unchanged.
   *
   * @complexity \c O(k log a) to find plus \c O(total nodes) to prune.
   */
  template <std::ranges::forward_range KeyRange>
  constexpr auto erase(KeyRange&& key) noexcept -> bool {
    // Record the (parent, edge) pairs along the descent so a removal can prune
    // back up its own path in O(k) instead of rescanning the whole trie.
    std::vector<std::pair<node*, uchar_type>> path;
    auto* cur{m_root.get()};
    for (auto const& c : key) {
      auto const uc{static_cast<uchar_type>(c)};
      auto* const slot{cur->children.at(uc)};
      if (slot == nullptr) {
        return false;
      }
      path.emplace_back(cur, uc);
      cur = slot->get();
    }
    if (!cur->value.has_value()) {
      return false;
    }
    cur->value.reset();
    --m_size;
    // Walk back up the path, dropping each node that is now a valueless leaf and
    // stopping at the first node still in use (it holds a value or has children).
    for (auto it{path.rbegin()}; it != path.rend(); ++it) {
      auto* const slot{it->first->children.at(it->second)};
      auto const* const child{slot != nullptr ? slot->get() : nullptr};
      if (child == nullptr || child->value.has_value() || !child->children.empty()) {
        break;
      }
      nexenne::utility::discard(it->first->children.erase(it->second));
    }
    return true;
  }

  /**
   * @brief Whether the trie holds a value at \p key.
   *
   * @tparam KeyRange A forward range whose elements convert to \p Char.
   * @param key Sequence of characters forming the key.
   *
   * @return \c true when a value is stored at \p key.
   *
   * @pre None.
   * @post None. The trie is not modified.
   *
   * @complexity \c O(k log a).
   */
  template <std::ranges::forward_range KeyRange>
  [[nodiscard]] constexpr auto contains(KeyRange&& key) const noexcept -> bool {
    auto const* const n{descend(key)};
    return n != nullptr && n->value.has_value();
  }

  /**
   * @brief Pointer to the value at \p key, or \c nullptr on a miss.
   *
   * @tparam KeyRange A forward range whose elements convert to \p Char.
   * @param key Sequence of characters forming the key.
   *
   * @return Pointer to the stored value, or \c nullptr when \p key is absent.
   *
   * @pre None.
   * @post None. The pointer stays valid until the next insertion or erasure on
   *       \p key's path.
   *
   * @complexity \c O(k log a).
   */
  template <std::ranges::forward_range KeyRange>
  [[nodiscard]] constexpr auto find(KeyRange&& key) noexcept -> Value* {
    auto* const n{descend(key)};
    if (n == nullptr || !n->value.has_value()) {
      return nullptr;
    }
    return std::addressof(*n->value);
  }

  /**
   * @brief Pointer to the const value at \p key, or \c nullptr on a miss.
   *
   * @tparam KeyRange A forward range whose elements convert to \p Char.
   * @param key Sequence of characters forming the key.
   *
   * @return Pointer to the stored const value, or \c nullptr when \p key is
   *         absent.
   *
   * @pre None.
   * @post None. The pointer stays valid until the next insertion or erasure on
   *       \p key's path.
   *
   * @complexity \c O(k log a).
   */
  template <std::ranges::forward_range KeyRange>
  [[nodiscard]] constexpr auto find(KeyRange&& key) const noexcept -> Value const* {
    auto const* const n{descend(key)};
    if (n == nullptr || !n->value.has_value()) {
      return nullptr;
    }
    return std::addressof(*n->value);
  }

  /**
   * @brief Whether at least one stored key begins with \p prefix.
   *
   * @tparam KeyRange A forward range whose elements convert to \p Char.
   * @param prefix Sequence of characters forming the prefix.
   *
   * @return \c true when some stored key starts with \p prefix.
   *
   * @pre None.
   * @post None. The trie is not modified.
   *
   * @complexity \c O(k log a) in the prefix length.
   */
  template <std::ranges::forward_range KeyRange>
  [[nodiscard]] constexpr auto starts_with(KeyRange&& prefix) const noexcept -> bool {
    return descend(prefix) != nullptr;
  }

  /**
   * @brief Visits every \c (key, value) entry in depth-first, sorted-sibling
   *        order.
   *
   * @tparam Visitor Invokable as \c f(std::span<Char const>, Value&).
   * @param visit Callback invoked once per stored entry.
   *
   * @pre \p visit must not insert into or erase from the trie during traversal.
   * @post Every stored entry was passed to \p visit once; structure unchanged.
   *
   * @complexity \c O(total nodes).
   */
  template <typename Visitor>
  constexpr auto for_each(Visitor&& visit) -> void {
    std::vector<Char> path;
    for_each_impl(m_root.get(), path, visit);
  }

  /**
   * @brief Visits every \c (key, value) entry in depth-first, sorted-sibling
   *        order (const overload).
   *
   * @tparam Visitor Invokable as \c f(std::span<Char const>, Value const&).
   * @param visit Callback invoked once per stored entry.
   *
   * @pre \p visit must not mutate the trie during traversal.
   * @post Every stored entry was passed to \p visit once; structure unchanged.
   *
   * @complexity \c O(total nodes).
   */
  template <typename Visitor>
  constexpr auto for_each(Visitor&& visit) const -> void {
    std::vector<Char> path;
    for_each_impl(m_root.get(), path, visit);
  }

  /**
   * @brief Whether \p a and \p b hold the same entries.
   *
   * @param a First trie.
   * @param b Second trie.
   *
   * @return \c true when both hold identical key-value entries.
   *
   * @pre None.
   * @post None. Neither trie is modified.
   *
   * @complexity \c O(total nodes).
   */
  [[nodiscard]] friend auto operator==(trie const& a, trie const& b) noexcept -> bool
    requires std::equality_comparable<Value>
  {
    return a.m_size == b.m_size && nodes_equal(a.m_root.get(), b.m_root.get());
  }

private:
  // Iterative pre-order DFS. Each frame tracks the next child to descend into and
  // whether it pushed a path character (the root pushes none), so the key path is
  // unwound correctly without recursing to the trie depth. Both for_each
  // overloads reach here with a node* (unique_ptr::get is non-const-propagating).
  template <typename Visitor>
  static auto for_each_impl(node* const root, std::vector<Char>& path, Visitor& visit) -> void {
    if (root == nullptr) {
      return;
    }

    struct frame {
      node* n;
      std::size_t idx;
      bool pushed;
    };

    if (root->value.has_value()) {
      visit(std::span<Char const>{path.data(), path.size()}, *root->value);
    }
    std::vector<frame> stack;
    stack.push_back(frame{root, 0, false});
    while (!stack.empty()) {
      auto& top{stack.back()};
      if (top.idx < top.n->children.size()) {
        auto& entry{*(top.n->children.begin() + static_cast<std::ptrdiff_t>(top.idx))};
        ++top.idx;
        path.push_back(static_cast<Char>(entry.first));
        auto* const child{entry.second.get()};
        if (child->value.has_value()) {
          visit(std::span<Char const>{path.data(), path.size()}, *child->value);
        }
        stack.push_back(frame{child, 0, true});
      } else {
        auto const pushed{top.pushed};
        stack.pop_back();
        if (pushed) {
          path.pop_back();
        }
      }
    }
  }

  static auto nodes_equal(node const* const root_a, node const* const root_b) noexcept -> bool {
    // Iterative structural comparison over a stack of node pairs to compare.
    std::vector<std::pair<node const*, node const*>> work;
    work.emplace_back(root_a, root_b);
    while (!work.empty()) {
      auto const [a, b]{work.back()};
      work.pop_back();
      if (a == nullptr && b == nullptr) {
        continue;
      }
      if (a == nullptr || b == nullptr) {
        return false;
      }
      if (a->value.has_value() != b->value.has_value()) {
        return false;
      }
      if (a->value.has_value() && !(*a->value == *b->value)) {
        return false;
      }
      if (a->children.size() != b->children.size()) {
        return false;
      }
      for (auto const& [uc, child] : a->children) {
        auto const* const other{b->children.at(uc)};
        if (other == nullptr) {
          return false;
        }
        work.emplace_back(child.get(), other->get());
      }
    }
    return true;
  }

  template <std::ranges::forward_range KeyRange>
  [[nodiscard]] constexpr auto descend(KeyRange&& key) noexcept -> node* {
    auto* cur{m_root.get()};
    for (auto const& c : key) {
      auto const uc{static_cast<uchar_type>(c)};
      auto* const slot{cur->children.at(uc)};
      if (slot == nullptr) {
        return nullptr;
      }
      cur = slot->get();
    }
    return cur;
  }

  template <std::ranges::forward_range KeyRange>
  [[nodiscard]] constexpr auto descend(KeyRange&& key) const noexcept -> node const* {
    auto const* cur{m_root.get()};
    for (auto const& c : key) {
      auto const uc{static_cast<uchar_type>(c)};
      auto const* const slot{cur->children.at(uc)};
      if (slot == nullptr) {
        return nullptr;
      }
      cur = slot->get();
    }
    return cur;
  }

  static auto clone_subtree(node const* const src) noexcept -> std::unique_ptr<node> {
    if (src == nullptr) {
      return nullptr;
    }
    auto root{std::make_unique<node>()};
    // Iterative pre-order copy over a stack of (source, freshly-made destination)
    // pairs, so a deep source cannot overflow the stack.
    std::vector<std::pair<node const*, node*>> work;
    work.emplace_back(src, root.get());
    while (!work.empty()) {
      auto const [s, d]{work.back()};
      work.pop_back();
      if (s->value.has_value()) {
        d->value.emplace(*s->value);
      }
      for (auto const& [uc, child] : s->children) {
        if (child != nullptr) {
          auto fresh{std::make_unique<node>()};
          auto* const raw{fresh.get()};
          nexenne::utility::discard(d->children.try_emplace(uc, std::move(fresh)));
          work.emplace_back(child.get(), raw);
        }
      }
    }
    return root;
  }
};

}  // namespace nexenne::container
