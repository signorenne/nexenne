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
        static_cast<void>(cur->children.try_emplace(uc, std::move(fresh)));
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
    auto* const target{descend(key)};
    if (target == nullptr || !target->value.has_value()) {
      return false;
    }
    target->value.reset();
    --m_size;
    prune_subtree(m_root.get());
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
  template <typename Visitor>
  static auto for_each_impl(node* const n, std::vector<Char>& path, Visitor& visit) -> void {
    if (n == nullptr) {
      return;
    }
    if (n->value.has_value()) {
      visit(std::span<Char const>{path.data(), path.size()}, *n->value);
    }
    for (auto& [uc, child] : n->children) {
      path.push_back(static_cast<Char>(uc));
      for_each_impl(child.get(), path, visit);
      path.pop_back();
    }
  }

  template <typename Visitor>
  static auto for_each_impl(node const* const n, std::vector<Char>& path, Visitor& visit) -> void {
    if (n == nullptr) {
      return;
    }
    if (n->value.has_value()) {
      visit(std::span<Char const>{path.data(), path.size()}, *n->value);
    }
    for (auto const& [uc, child] : n->children) {
      path.push_back(static_cast<Char>(uc));
      for_each_impl(child.get(), path, visit);
      path.pop_back();
    }
  }

  static auto nodes_equal(node const* const a, node const* const b) noexcept -> bool {
    if (a == nullptr && b == nullptr) {
      return true;
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
      if (!nodes_equal(child.get(), other->get())) {
        return false;
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

  // Walk post-order and drop nodes that hold no value and have no children.
  // Runs after a successful erase to keep the trie compact.
  static auto prune_subtree(node* const n) noexcept -> void {
    if (n == nullptr) {
      return;
    }
    // Recurse first, then collect the now-dead child keys and erase them after
    // the walk so the child map is not mutated mid-iteration.
    std::vector<uchar_type> victims;
    for (auto& [uc, child] : n->children) {
      prune_subtree(child.get());
      if (child != nullptr && !child->value.has_value() && child->children.empty()) {
        victims.push_back(uc);
      }
    }
    for (auto const uc : victims) {
      static_cast<void>(n->children.erase(uc));
    }
  }

  static auto clone_subtree(node const* const src) noexcept -> std::unique_ptr<node> {
    if (src == nullptr) {
      return nullptr;
    }
    auto fresh{std::make_unique<node>()};
    if (src->value.has_value()) {
      fresh->value.emplace(*src->value);
    }
    for (auto const& [uc, child] : src->children) {
      static_cast<void>(fresh->children.try_emplace(uc, clone_subtree(child.get())));
    }
    return fresh;
  }
};

}  // namespace nexenne::container
