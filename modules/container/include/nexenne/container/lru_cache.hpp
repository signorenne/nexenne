#pragma once

/**
 * @file
 * @brief Fixed-capacity cache with least-recently-used eviction.
 *
 * \c lru_cache<Key, Value, Capacity> bounds itself at a compile-time entry count.
 * Every \c get and \c put promotes the touched entry to the most-recently-used end
 * of an internal recency order; when the cache is full and a \c put introduces a
 * new key, the least-recently-used entry is evicted and its storage recycled.
 *
 * It layers two ported containers: an \c intrusive_list orders entries by recency
 * (front is MRU, back is LRU) and a \c flat_hash_map indexes them by key for
 * \c O(1) lookup. The list nodes live in a fixed pool allocated once at
 * construction and never grown, so steady-state \c get / \c put is allocation
 * free, the win over a \c std::unordered_map plus \c std::list. Reach for it for
 * asset caches (keep the hottest N textures resident), bounded memoisation
 * tables, and recently-used registries. Every operation is \c noexcept;
 * allocation failure terminates. Concurrent reads are not safe, because \c get
 * mutates the recency order.
 */

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/container/intrusive_list.hpp>

namespace nexenne::container {

/**
 * @brief Fixed-capacity cache with least-recently-used eviction.
 *
 * @tparam Key Key type; must be default-constructible and movable. The node
 *             pool is pre-sized at construction (so keys are value-initialised)
 *             and \c put places a key by move-assignment.
 * @tparam Value Mapped value type; must be default-constructible and movable,
 *               for the same reason (the pool is pre-sized and \c put
 *               move-assigns the value into a reused node).
 * @tparam Capacity Maximum number of entries; fixed at compile time and must be
 *                  at least one (a zero \c Capacity fails to compile).
 * @tparam Hash Hash functor; \c std::hash<Key> by default.
 * @tparam KeyEq Equality predicate; \c std::equal_to<Key> by default.
 *
 * @pre None.
 * @post A constructed cache is empty with its capacity reserved.
 */
template <
  typename Key,
  typename Value,
  std::size_t Capacity,
  typename Hash = std::hash<Key>,
  typename KeyEq = std::equal_to<Key>>
  requires(Capacity >= 1) && std::default_initializable<Key> && std::movable<Key>
          && std::default_initializable<Value> && std::movable<Value>
class lru_cache {
public:
  using key_type = Key;
  using mapped_type = Value;
  using size_type = std::size_t;

private:
  struct node_t : intrusive_list_hook<node_t> {
    Key key{};
    Value value{};
  };

  // Stable storage so the list and index pointers stay valid: the pool is sized
  // to capacity at construction and never grows, so node addresses never move.
  std::vector<node_t> m_pool;
  std::vector<node_t*> m_free;
  intrusive_list<node_t> m_lru;
  flat_hash_map<Key, node_t*, Hash, KeyEq> m_index;

  auto acquire_node() noexcept -> node_t* {
    if (!m_free.empty()) {
      auto* const n{m_free.back()};
      m_free.pop_back();
      return n;
    }
    // No free slot: evict the LRU entry and reuse its node. Safe because a full
    // cache (the only way the free list is empty) has a non-empty recency list,
    // which the Capacity >= 1 constraint guarantees.
    auto* const evicted{m_lru.back()};
    m_lru.erase(*evicted);
    static_cast<void>(m_index.erase(evicted->key));
    return evicted;
  }

public:
  /**
   * @brief Constructs an empty cache holding up to \c Capacity entries.
   *
   * Pre-allocates the node pool and the index, so steady-state \c get / \c put
   * performs no further allocation.
   *
   * @pre None. A zero \c Capacity fails to compile.
   * @post \c empty() is \c true and \c capacity() equals \c Capacity.
   */
  lru_cache() noexcept : m_pool(Capacity) {
    m_free.reserve(Capacity);
    m_index.reserve(Capacity);
    for (auto& n : m_pool) {
      m_free.push_back(std::addressof(n));
    }
  }

  lru_cache(lru_cache const&) = delete;
  auto operator=(lru_cache const&) -> lru_cache& = delete;

  // Moving would have to rewrite every node pointer in the list and index; the
  // cache is intentionally non-movable. Construct it in place.
  lru_cache(lru_cache&&) = delete;
  auto operator=(lru_cache&&) -> lru_cache& = delete;

  /**
   * @brief Number of entries currently cached.
   *
   * @return Live entry count.
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_lru.size();
  }

  /**
   * @brief Whether the cache holds no entries.
   *
   * @return \c true when \c size() is zero.
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_lru.empty();
  }

  /**
   * @brief Whether the cache is at capacity.
   *
   * @return \c true when \c size() equals \c capacity().
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] auto full() const noexcept -> bool {
    return m_lru.size() == Capacity;
  }

  /**
   * @brief Maximum number of entries the cache may hold.
   *
   * @return \c Capacity.
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] static constexpr auto capacity() noexcept -> size_type {
    return Capacity;
  }

  /**
   * @brief Largest number of entries the cache can ever hold.
   *
   * @return \c Capacity.
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] static constexpr auto max_size() noexcept -> size_type {
    return Capacity;
  }

  /**
   * @brief Drops every entry, keeping capacity.
   *
   * @pre None.
   * @post \c empty() is \c true and \c capacity() is unchanged.
   */
  auto clear() noexcept -> void {
    m_lru.clear();
    m_index.clear();
    m_free.clear();
    for (auto& n : m_pool) {
      m_free.push_back(std::addressof(n));
    }
  }

  /**
   * @brief Inserts or updates \p key's entry and marks it most recently used.
   *
   * May evict the least-recently-used entry when the cache is full and \p key is
   * new.
   *
   * @param key Key to insert or update, moved into the cache.
   * @param value Value to store, moved into the cache.
   *
   * @pre None.
   * @post \p key maps to \p value and is the most recently used entry; when the
   *       cache was full and \p key was new, the prior LRU entry was evicted.
   *
   * @complexity Amortised \c O(1).
   */
  auto put(Key key, Value value) noexcept -> void {
    if (auto* const existing{m_index.find(key)}) {
      auto* const node{*existing};
      node->value = std::move(value);
      m_lru.erase(*node);
      m_lru.push_front(*node);
      return;
    }
    auto* const node{acquire_node()};
    node->key = std::move(key);
    node->value = std::move(value);
    m_lru.push_front(*node);
    static_cast<void>(m_index.insert(node->key, node));
  }

  /**
   * @brief Looks up \p key, promoting the entry to most recently used on a hit.
   *
   * @param key Key to look up.
   *
   * @return Pointer to the entry's value on a hit, or \c nullptr on a miss.
   *
   * @pre None.
   * @post On a hit \p key becomes the most recently used entry; on a miss the
   *       cache is unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto get(Key const& key) noexcept -> Value* {
    auto* const slot{m_index.find(key)};
    if (slot == nullptr) {
      return nullptr;
    }
    auto* const node{*slot};
    m_lru.erase(*node);
    m_lru.push_front(*node);
    return std::addressof(node->value);
  }

  /**
   * @brief Looks up \p key without promoting it.
   *
   * @param key Key to look up.
   *
   * @return Pointer to the entry's value on a hit, or \c nullptr on a miss.
   *
   * @pre None.
   * @post None. The cache and its eviction order are not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto peek(Key const& key) const noexcept -> Value const* {
    auto const* const slot{m_index.find(key)};
    if (slot == nullptr) {
      return nullptr;
    }
    return std::addressof((*slot)->value);
  }

  /**
   * @brief Whether \p key has an entry.
   *
   * @param key Key to test for membership.
   *
   * @return \c true when \p key is cached.
   *
   * @pre None.
   * @post None. The cache and its eviction order are not modified.
   *
   * @complexity Amortised \c O(1).
   */
  [[nodiscard]] auto contains(Key const& key) const noexcept -> bool {
    return m_index.contains(key);
  }

  /**
   * @brief Removes \p key's entry if present.
   *
   * @param key Key to remove.
   *
   * @return \c true on a removal, \c false when the key was absent.
   *
   * @pre None.
   * @post \p key is absent. On a removal \c size() shrank by one and the node
   *       returned to the free pool.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase(Key const& key) noexcept -> bool {
    auto* const slot{m_index.find(key)};
    if (slot == nullptr) {
      return false;
    }
    auto* const node{*slot};
    m_lru.erase(*node);
    static_cast<void>(m_index.erase(key));
    m_free.push_back(node);
    return true;
  }

  /**
   * @brief The most recently used key.
   *
   * @return Pointer to the MRU key, or \c nullptr when empty.
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] auto mru_key() const noexcept -> Key const* {
    return m_lru.empty() ? nullptr : std::addressof(m_lru.front()->key);
  }

  /**
   * @brief The least recently used key, the next to be evicted under pressure.
   *
   * @return Pointer to the LRU key, or \c nullptr when empty.
   *
   * @pre None.
   * @post None. The cache is not modified.
   */
  [[nodiscard]] auto lru_key() const noexcept -> Key const* {
    return m_lru.empty() ? nullptr : std::addressof(m_lru.back()->key);
  }
};

}  // namespace nexenne::container
