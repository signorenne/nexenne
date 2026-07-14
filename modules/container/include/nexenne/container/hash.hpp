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
 * \c stable_vector / \c ring_buffer / \c gap_buffer (sequence hash in iteration
 * order), \c flat_set (sorted, canonical by construction), \c flat_map /
 * \c static_flat_map (sorted key order, hashing key then value), \c binary_tree
 * (in-order, which is sorted and so canonical), and \c trie (a commutative fold
 * over entries, matching its structural equality).
 *
 * \c bag, \c heap, and \c indexed_priority_queue are deliberately not hashable:
 * their iteration order depends on operation history, so no deterministic hash
 * could match a logical equality they also do not provide. \c bimap,
 * \c dense_map, \c graph, \c union_find, \c slot_map, \c intrusive_list, and
 * \c bloom_filter also define \c operator== but are left unhashed for now: their
 * equality is either order-insensitive (a commutative fold like the trie's would
 * be needed) or keyed on stable handles rather than contents, so a matching hash
 * needs a per-type decision rather than the sequence recipe here.
 *
 * Each specialization is constrained on \c nexenne::utility::hashable so a query
 * against a container of a non-hashable element sees \c std::hash as disabled
 * (non-invocable) rather than hard-erroring in the body, and each \c operator()
 * is conditionally \c noexcept, propagating a throwing element hash instead of
 * calling \c std::terminate. The specializations live at global scope, as the
 * standard requires.
 */

#include <cstddef>
#include <functional>
#include <ranges>
#include <span>
#include <utility>

#include <nexenne/container/binary_tree.hpp>
#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/flat_map.hpp>
#include <nexenne/container/flat_set.hpp>
#include <nexenne/container/gap_buffer.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/container/stable_vector.hpp>
#include <nexenne/container/static_flat_map.hpp>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/container/trie.hpp>
#include <nexenne/utility/hash.hpp>

namespace nexenne::container::detail {
/// @cond INTERNAL

/// @brief Whether hashing a \c T \c const& through the shared combiner cannot throw.
template <typename T>
inline constexpr bool nothrow_hash_v{noexcept(
  nexenne::utility::hash_combine(std::declval<std::size_t&>(), std::declval<T const&>())
)};

/**
 * @brief Sequence hash: the element count then each element in iteration order.
 *
 * Folding the count first distinguishes sequences that share a prefix, and the
 * per-element fold is order-sensitive, so this matches an \c operator== over a
 * canonical iteration order.
 *
 * @tparam Range Forward range whose element type is hashable.
 * @param range Elements to hash, in iteration order.
 * @param count Element count folded ahead of the elements.
 *
 * @return The combined hash of \p count and every element.
 *
 * @pre None.
 * @post None. \p range is not modified.
 *
 * @complexity \c O(M) for a range of \c M elements.
 */
template <std::ranges::input_range Range>
  requires nexenne::utility::hashable<std::ranges::range_value_t<Range>>
[[nodiscard]] auto sequence_hash(Range const& range, std::size_t const count)
  noexcept(nothrow_hash_v<std::ranges::range_value_t<Range>>) -> std::size_t {
  auto seed{std::size_t{0}};
  nexenne::utility::hash_combine(seed, count);
  for (auto const& element : range) {
    nexenne::utility::hash_combine(seed, element);
  }
  return seed;
}

/**
 * @brief Map hash: the entry count then each key and value in iteration order.
 *
 * @tparam Key Hashable key type.
 * @tparam Value Hashable mapped type.
 * @tparam Range Forward range of \c (key, value) entries.
 * @param range Entries to hash, in iteration order.
 * @param count Entry count folded ahead of the entries.
 *
 * @return The combined hash of \p count and every key and value.
 *
 * @pre None.
 * @post None. \p range is not modified.
 *
 * @complexity \c O(M) for a range of \c M entries.
 */
template <typename Key, typename Value, std::ranges::input_range Range>
  requires nexenne::utility::hashable<Key> && nexenne::utility::hashable<Value>
[[nodiscard]] auto map_hash(Range const& range, std::size_t const count)
  noexcept(nothrow_hash_v<Key> && nothrow_hash_v<Value>) -> std::size_t {
  auto seed{std::size_t{0}};
  nexenne::utility::hash_combine(seed, count);
  for (auto const& [key, value] : range) {
    nexenne::utility::hash_combine(seed, key);
    nexenne::utility::hash_combine(seed, value);
  }
  return seed;
}

/// @endcond
}  // namespace nexenne::container::detail

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
  requires nexenne::utility::hashable<T>
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
  [[nodiscard]] auto operator()(nexenne::container::static_vector<T, N> const& v)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(v, v.size());
  }
};

/**
 * @brief Hashes a \c stable_vector as a sequence (size plus each element).
 */
template <typename T, std::size_t ChunkSize>
  requires nexenne::utility::hashable<T>
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
  [[nodiscard]] auto operator()(nexenne::container::stable_vector<T, ChunkSize> const& v)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(v, v.size());
  }
};

/**
 * @brief Hashes a \c binary_tree by visiting elements in in-order.
 *
 * In-order is canonical (sorted under \p Compare), so the hash matches
 * \c operator==.
 */
template <typename T, typename Compare>
  requires nexenne::utility::hashable<T>
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
  [[nodiscard]] auto operator()(nexenne::container::binary_tree<T, Compare> const& t)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(t, t.size());
  }
};

/**
 * @brief Hashes a \c small_vector as a sequence (size plus each element).
 */
template <typename T, std::size_t N>
  requires nexenne::utility::hashable<T>
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
  [[nodiscard]] auto operator()(nexenne::container::small_vector<T, N> const& v)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(v, v.size());
  }
};

/**
 * @brief Hashes a \c ring_buffer in FIFO order (front to back).
 */
template <typename T, std::size_t N>
  requires nexenne::utility::hashable<T>
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
  [[nodiscard]] auto operator()(nexenne::container::ring_buffer<T, N> const& r)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(r, r.size());
  }
};

/**
 * @brief Hashes a \c gap_buffer as a sequence in logical (gap-collapsed) order.
 */
template <typename T>
  requires nexenne::utility::hashable<T>
struct std::hash<nexenne::container::gap_buffer<T>> {
  /**
   * @brief Computes the logical-order sequence hash of \p b.
   *
   * @param b Gap buffer to hash.
   *
   * @return A hash combining the size and each element in logical order.
   *
   * @pre None.
   * @post None. \p b is not modified; equal buffers hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::gap_buffer<T> const& b)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(b, b.size());
  }
};

/**
 * @brief Hashes a \c flat_set as a sequence in sorted (canonical) order.
 */
template <typename T, typename Compare>
  requires nexenne::utility::hashable<T>
struct std::hash<nexenne::container::flat_set<T, Compare>> {
  /**
   * @brief Computes the sorted-order sequence hash of \p s.
   *
   * @param s Set to hash.
   *
   * @return A hash combining the size and each element in sorted order.
   *
   * @pre None.
   * @post None. \p s is not modified; equal sets hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::flat_set<T, Compare> const& s)
    const noexcept(nexenne::container::detail::nothrow_hash_v<T>) -> std::size_t {
    return nexenne::container::detail::sequence_hash(s, s.size());
  }
};

/**
 * @brief Hashes a \c flat_map by key then value in sorted key order.
 */
template <typename Key, typename Value, typename Compare>
  requires nexenne::utility::hashable<Key> && nexenne::utility::hashable<Value>
struct std::hash<nexenne::container::flat_map<Key, Value, Compare>> {
  /**
   * @brief Computes the sorted-key-order entry hash of \p m.
   *
   * @param m Map to hash.
   *
   * @return A hash combining the size and each key and value in sorted key
   *         order.
   *
   * @pre None.
   * @post None. \p m is not modified; equal maps hash equal.
   */
  [[nodiscard]] auto operator()(nexenne::container::flat_map<Key, Value, Compare> const& m)
    const noexcept(
      nexenne::container::detail::nothrow_hash_v<Key>
      && nexenne::container::detail::nothrow_hash_v<Value>
    ) -> std::size_t {
    return nexenne::container::detail::map_hash<Key, Value>(m, m.size());
  }
};

/**
 * @brief Hashes a \c static_flat_map by key then value in sorted key order.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Compare>
  requires nexenne::utility::hashable<Key> && nexenne::utility::hashable<Value>
struct std::hash<nexenne::container::static_flat_map<Key, Value, Capacity, Compare>> {
  /**
   * @brief Computes the sorted-key-order entry hash of \p m.
   *
   * @param m Map to hash.
   *
   * @return A hash combining the size and each key and value in sorted key
   *         order.
   *
   * @pre None.
   * @post None. \p m is not modified; equal maps hash equal.
   */
  [[nodiscard]] auto
  operator()(nexenne::container::static_flat_map<Key, Value, Capacity, Compare> const& m)
    const noexcept(
      nexenne::container::detail::nothrow_hash_v<Key>
      && nexenne::container::detail::nothrow_hash_v<Value>
    ) -> std::size_t {
    return nexenne::container::detail::map_hash<Key, Value>(m, m.size());
  }
};

/**
 * @brief Hashes a \c trie by a commutative fold over its entries.
 *
 * XOR-ing per-entry hashes keeps the result insensitive to child-iteration
 * order, matching the trie's structural \c operator==.
 */
template <typename Char, typename Value>
  requires nexenne::utility::hashable<Char> && nexenne::utility::hashable<Value>
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
  [[nodiscard]] auto operator()(nexenne::container::trie<Char, Value> const& t)
    const noexcept(
      nexenne::container::detail::nothrow_hash_v<Char>
      && nexenne::container::detail::nothrow_hash_v<Value>
    ) -> std::size_t {
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
