#pragma once

/**
 * @file
 * @brief \c std::hash specializations for value-typed nexenne containers.
 *
 * Including this header lets the value containers be used as keys in
 * \c std::unordered_map / \c std::unordered_set without each caller writing a
 * hash. The recipe reuses \c nexenne::utility::hash_combine (a Boost-style
 * mixer), so it lives in one place. The set of specializations matches the
 * containers that have \c operator== over a canonical iteration order:
 * \c bitset_dynamic (over its packed words), \c static_vector / \c small_vector /
 * \c stable_vector / \c ring_buffer (sequence hash in iteration order),
 * \c binary_tree (in-order, which is sorted and so canonical), and \c trie (a
 * commutative fold over entries, matching its structural equality).
 *
 * \c bag, \c heap, and \c indexed_priority_queue are deliberately not hashable:
 * their iteration order depends on operation history, so no deterministic hash
 * could match a logical equality they also do not provide. The specializations
 * live at global scope, as the standard requires.
 */

#include <cstddef>
#include <functional>
#include <span>

#include <nexenne/container/binary_tree.hpp>
#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/container/stable_vector.hpp>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/container/trie.hpp>
#include <nexenne/utility/hash.hpp>

/**
 * @brief Hashes a \c bitset_dynamic over its packed word storage and size.
 */
template <>
struct std::hash<nexenne::container::bitset_dynamic> {
  /**
   * @brief Computes the hash of \p b.
   *
   * @param b Bitset to hash.
   *
   * @return A hash combining the bit count and every packed word.
   *
   * @pre None.
   * @post None. \p b is not modified; equal bitsets hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::bitset_dynamic const& b
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, b.size());
    for (auto const w : b.words()) {
      nexenne::utility::hash_combine(seed, w);
    }
    return seed;
  }
};

/**
 * @brief Hashes a \c static_vector as a sequence (size plus each element).
 */
template <typename T, std::size_t N>
struct std::hash<nexenne::container::static_vector<T, N>> {
  /**
   * @brief Computes the sequence hash of \p v.
   *
   * @param v Vector to hash.
   *
   * @return A hash combining the size and each element in order.
   *
   * @pre None.
   * @post None. \p v is not modified; equal vectors hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::static_vector<T, N> const& v
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, v.size());
    for (auto const& e : v) {
      nexenne::utility::hash_combine(seed, e);
    }
    return seed;
  }
};

/**
 * @brief Hashes a \c stable_vector as a sequence (size plus each element).
 */
template <typename T, std::size_t ChunkSize>
struct std::hash<nexenne::container::stable_vector<T, ChunkSize>> {
  /**
   * @brief Computes the sequence hash of \p v.
   *
   * @param v Vector to hash.
   *
   * @return A hash combining the size and each element in order.
   *
   * @pre None.
   * @post None. \p v is not modified; equal vectors hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::stable_vector<T, ChunkSize> const& v
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, v.size());
    for (auto const& e : v) {
      nexenne::utility::hash_combine(seed, e);
    }
    return seed;
  }
};

/**
 * @brief Hashes a \c binary_tree by visiting elements in in-order.
 *
 * In-order is canonical (sorted under \p Compare), so the hash matches
 * \c operator==.
 */
template <typename T, typename Compare>
struct std::hash<nexenne::container::binary_tree<T, Compare>> {
  /**
   * @brief Computes the in-order sequence hash of \p t.
   *
   * @param t Tree to hash.
   *
   * @return A hash combining the size and each element in sorted order.
   *
   * @pre None.
   * @post None. \p t is not modified; equal trees hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::binary_tree<T, Compare> const& t
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, t.size());
    for (auto const& e : t) {
      nexenne::utility::hash_combine(seed, e);
    }
    return seed;
  }
};

/**
 * @brief Hashes a \c small_vector as a sequence (size plus each element).
 */
template <typename T, std::size_t N>
struct std::hash<nexenne::container::small_vector<T, N>> {
  /**
   * @brief Computes the sequence hash of \p v.
   *
   * @param v Vector to hash.
   *
   * @return A hash combining the size and each element in order.
   *
   * @pre None.
   * @post None. \p v is not modified; equal vectors hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::small_vector<T, N> const& v
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, v.size());
    for (auto const& e : v) {
      nexenne::utility::hash_combine(seed, e);
    }
    return seed;
  }
};

/**
 * @brief Hashes a \c ring_buffer in FIFO order (front to back).
 */
template <typename T, std::size_t N>
struct std::hash<nexenne::container::ring_buffer<T, N>> {
  /**
   * @brief Computes the FIFO-order sequence hash of \p r.
   *
   * @param r Ring buffer to hash.
   *
   * @return A hash combining the size and each element from front to back.
   *
   * @pre None.
   * @post None. \p r is not modified; equal buffers hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::ring_buffer<T, N> const& r
  ) const noexcept -> std::size_t {
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, r.size());
    for (auto const& e : r) {
      nexenne::utility::hash_combine(seed, e);
    }
    return seed;
  }
};

/**
 * @brief Hashes a \c trie by a commutative fold over its entries.
 *
 * XOR-ing per-entry hashes keeps the result insensitive to child-iteration
 * order, matching the trie's structural \c operator==.
 */
template <typename Char, typename Value>
struct std::hash<nexenne::container::trie<Char, Value>> {
  /**
   * @brief Computes the order-insensitive entry hash of \p t.
   *
   * @param t Trie to hash.
   *
   * @return A hash combining the size with a commutative fold over every
   *         \c (key, value) entry.
   *
   * @pre None.
   * @post None. \p t is not modified; equal tries hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::trie<Char, Value> const& t
  ) const noexcept -> std::size_t {
    auto entries_hash{std::size_t{0}};
    t.for_each([&entries_hash](std::span<Char const> const key, Value const& value) {
      auto seed{std::size_t{0}};
      nexenne::utility::hash_combine(seed, key.size());
      for (auto const c : key) {
        nexenne::utility::hash_combine(seed, c);
      }
      nexenne::utility::hash_combine(seed, value);
      entries_hash ^= seed;
    });
    auto seed{std::size_t{0}};
    nexenne::utility::hash_combine(seed, t.size());
    nexenne::utility::hash_combine(seed, entries_hash);
    return seed;
  }
};
