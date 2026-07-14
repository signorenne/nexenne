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
 * (front is MRU, back is LRU) and a \c flat_hash_map indexes them for \c O(1)
 * lookup. The index keys on a pointer into each node's own key storage rather
 * than a copy, so a key is stored exactly once and no key is allocated on a
 * steady-state put (a move-only key type is therefore supported). The list nodes
 * live in a fixed pool allocated once at construction and never grown, so
 * steady-state \c get / \c put is allocation free, the win over a
 * \c std::unordered_map plus \c std::list. Reach for it for
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
#include <nexenne/utility/discard.hpp>

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
  /**
   * @brief Pool node: a recency-list hook plus the cached key and value.
   *
   * @pre None.
   * @post A default-constructed node holds value-initialised key and value.
   */
  struct node : intrusive_list_hook<node> {
    Key key{};
    Value value{};
  };

  /**
   * @brief Hashes a key by pointer, forwarding to the user \p Hash on the pointee.
   *
   * The index keys on a pointer INTO each node's own key storage rather than a
   * copy of the key, so the key is stored exactly once (in the node), no key is
   * allocated on a steady-state put, and a move-only \p Key works. The pool never
   * moves, so these pointers stay stable; a slot's index entry is always erased
   * before its key is overwritten.
   *
   * @pre None.
   * @post None.
   */
  struct key_ptr_hash {
    [[no_unique_address]] Hash hash{};

    /**
     * @brief Hashes the key pointed at by \p key.
     *
     * @param key Non-null pointer to the key to hash.
     *
     * @return The hash of \c *key under \p Hash.
     *
     * @pre \p key is non-null.
     * @post None.
     */
    auto operator()(Key const* const key) const noexcept -> std::size_t {
      return hash(*key);
    }
  };

  /**
   * @brief Compares keys by pointer, forwarding to the user \p KeyEq on the
   *        pointees.
   *
   * @pre None.
   * @post None.
   */
  struct key_ptr_eq {
    [[no_unique_address]] KeyEq eq{};

    /**
     * @brief Whether the keys pointed at by \p a and \p b are equal.
     *
     * @param a Non-null pointer to the first key.
     * @param b Non-null pointer to the second key.
     *
     * @return \c true when \c *a equals \c *b under \p KeyEq.
     *
     * @pre \p a and \p b are non-null.
     * @post None.
     */
    auto operator()(Key const* const a, Key const* const b) const noexcept -> bool {
      return eq(*a, *b);
    }
  };

  // Stable storage so the list and index pointers stay valid: the pool is sized
  // to capacity at construction and never grows, so node addresses never move.
  std::vector<node> m_pool;
  std::vector<node*> m_free;
  intrusive_list<node> m_lru;
  flat_hash_map<Key const*, node*, key_ptr_hash, key_ptr_eq> m_index;

  /**
   * @brief Obtains a node slot, evicting the LRU entry when the pool is full.
   *
   * When no free slot remains, the least-recently-used entry is evicted (removed
   * from the recency list and the index) and its node is reused. This is safe
   * because a full cache (the only way the free list empties) has a non-empty
   * recency list, which the \c Capacity >= 1 constraint guarantees.
   *
   * @return A node ready to receive a fresh key and value.
   *
   * @pre None.
   * @post The returned node is detached from the recency list and index.
   */
  auto acquire_node() noexcept -> node* {
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
    nexenne::utility::discard(m_index.erase(std::addressof(evicted->key)));
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
   * @post \c empty() is \c true and \c capacity() is unchanged; every entry's key
   *       and value are released (reset to a value-initialised state).
   */
  auto clear() noexcept -> void {
    m_lru.clear();
    m_index.clear();
    m_free.clear();
    for (auto& n : m_pool) {
      // Release each slot's payload so a logically empty cache pins no resources.
      n.key = Key{};
      n.value = Value{};
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
    if (auto* const existing{m_index.find(std::addressof(key))}) {
      auto* const entry{*existing};
      entry->value = std::move(value);
      m_lru.erase(*entry);
      m_lru.push_front(*entry);
      return;
    }
    auto* const entry{acquire_node()};
    entry->key = std::move(key);
    entry->value = std::move(value);
    m_lru.push_front(*entry);
    nexenne::utility::discard(m_index.insert(std::addressof(entry->key), entry));
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
    auto* const slot{m_index.find(std::addressof(key))};
    if (slot == nullptr) {
      return nullptr;
    }
    auto* const entry{*slot};
    m_lru.erase(*entry);
    m_lru.push_front(*entry);
    return std::addressof(entry->value);
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
    auto const* const slot{m_index.find(std::addressof(key))};
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
    return m_index.contains(std::addressof(key));
  }

  /**
   * @brief Removes \p key's entry if present.
   *
   * @param key Key to remove.
   *
   * @return \c true on a removal, \c false when the key was absent.
   *
   * @pre None.
   * @post \p key is absent. On a removal \c size() shrank by one, the entry's key
   *       and value are released (reset to a value-initialised state), and the
   *       node returned to the free pool.
   *
   * @complexity Amortised \c O(1).
   */
  auto erase(Key const& key) noexcept -> bool {
    auto* const slot{m_index.find(std::addressof(key))};
    if (slot == nullptr) {
      return false;
    }
    auto* const entry{*slot};
    m_lru.erase(*entry);
    nexenne::utility::discard(m_index.erase(std::addressof(key)));
    // Release the payload so an erased entry does not pin its resources in the
    // pool until the slot is next reused (the default-initializable constraint
    // makes this reset well-formed).
    entry->key = Key{};
    entry->value = Value{};
    m_free.push_back(entry);
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
