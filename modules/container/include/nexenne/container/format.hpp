#pragma once

/**
 * @file
 * @brief Debug printing and formatting for nexenne containers.
 *
 * Three layers, like the rest of the library's format headers: \c to_string(x)
 * builds a readable string, \c operator<<(std::ostream&, x) streams it, and a
 * \c std::formatter specialization makes \c std::format("{}", x) work. The output
 * is for diagnostics, not serialisation, and is not stable across versions.
 * Sequence-like containers print as \c "[a, b, c]", set-like as \c "{a, b, c}",
 * and map-like as \c "{k: v, ...}". Each element type must itself be formattable.
 *
 * Containers whose contents cannot be enumerated safely or at all (the lock-free
 * queues, the memory tier, the bloom filter, and the iteration-free LRU cache)
 * print a single-line stats form instead, for example
 * \c "spsc_queue(size_approx=2, capacity=15)". For the concurrent queues the
 * counts are approximate, matching \c size_approx.
 */

#include <concepts>
#include <cstddef>
#include <format>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nexenne/container/bag.hpp>
#include <nexenne/container/bimap.hpp>
#include <nexenne/container/binary_tree.hpp>
#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/bloom_filter.hpp>
#include <nexenne/container/dense_map.hpp>
#include <nexenne/container/deque.hpp>
#include <nexenne/container/error.hpp>
#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/container/flat_hash_set.hpp>
#include <nexenne/container/flat_map.hpp>
#include <nexenne/container/flat_set.hpp>
#include <nexenne/container/gap_buffer.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/container/heap.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/container/intrusive_list.hpp>
#include <nexenne/container/linear_arena.hpp>
#include <nexenne/container/lru_cache.hpp>
#include <nexenne/container/mpmc_queue.hpp>
#include <nexenne/container/mpsc_queue.hpp>
#include <nexenne/container/object_pool.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/scratch_pad.hpp>
#include <nexenne/container/slot_map.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/container/sparse_set.hpp>
#include <nexenne/container/spsc_queue.hpp>
#include <nexenne/container/stable_vector.hpp>
#include <nexenne/container/static_flat_map.hpp>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/container/trie.hpp>
#include <nexenne/container/union_find.hpp>

namespace nexenne::container {

namespace detail {
/// @cond INTERNAL

/**
 * @brief Joins a range's elements into a comma-separated string.
 *
 * @tparam Range Forward range whose elements have a \c std::formatter.
 * @param range Source range.
 *
 * @return Comma-separated debug string; empty for an empty range.
 *
 * @pre Each element of \p range is formattable via \c std::format.
 * @post None. \p range is not modified.
 */
template <typename Range>
[[nodiscard]] auto join_csv(Range const& range) -> std::string {
  std::string out;
  bool first{true};
  for (auto const& e : range) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += std::format("{}", e);
  }
  return out;
}

/// @endcond
}  // namespace detail

/**
 * @brief Builds a one-line diagnostic string for a \c bitset_dynamic.
 *
 * @param b The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p b is not modified.
 */
[[nodiscard]] inline auto to_string(bitset_dynamic const& b) -> std::string {
  std::string bits;
  bits.reserve(b.size());
  // MSB-first, matching the conventional binary-literal direction.
  for (auto i{b.size()}; i > 0; --i) {
    bits.push_back(b[i - 1] ? '1' : '0');
  }
  return std::format("bitset_dynamic(size={}, bits=0b{})", b.size(), bits);
}

/**
 * @brief Streams the diagnostic string of \p b to \p os.
 *
 * @param os Output stream to write to.
 * @param b The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post The diagnostic string of \p b has been written to \p os.
 */
inline auto operator<<(std::ostream& os, bitset_dynamic const& b) -> std::ostream& {
  return os << to_string(b);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c static_vector.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param v The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p v is not modified.
 */
template <typename T, std::size_t N>
[[nodiscard]] auto to_string(static_vector<T, N> const& v) -> std::string {
  return std::format("static_vector[{}]", detail::join_csv(v));
}

/**
 * @brief Streams the diagnostic string of \p v to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param v The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p v has been written to \p os.
 */
template <typename T, std::size_t N>
auto operator<<(std::ostream& os, static_vector<T, N> const& v) -> std::ostream& {
  return os << to_string(v);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c stable_vector.
 *
 * @tparam T Element type stored in the container.
 * @tparam ChunkSize Number of elements per allocated chunk.
 * @param v The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p v is not modified.
 */
template <typename T, std::size_t ChunkSize>
[[nodiscard]] auto to_string(stable_vector<T, ChunkSize> const& v) -> std::string {
  return std::format("stable_vector[{}]", detail::join_csv(v));
}

/**
 * @brief Streams the diagnostic string of \p v to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam ChunkSize Number of elements per allocated chunk.
 * @param os Output stream to write to.
 * @param v The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p v has been written to \p os.
 */
template <typename T, std::size_t ChunkSize>
auto operator<<(std::ostream& os, stable_vector<T, ChunkSize> const& v) -> std::ostream& {
  return os << to_string(v);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c bag.
 *
 * @tparam T Element type stored in the container.
 * @param b The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p b is not modified.
 */
template <typename T>
[[nodiscard]] auto to_string(bag<T> const& b) -> std::string {
  return std::format("bag[{}]", detail::join_csv(b));
}

/**
 * @brief Streams the diagnostic string of \p b to \p os.
 *
 * @tparam T Element type stored in the container.
 * @param os Output stream to write to.
 * @param b The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p b has been written to \p os.
 */
template <typename T>
auto operator<<(std::ostream& os, bag<T> const& b) -> std::ostream& {
  return os << to_string(b);
}

// deque is indexed, not iterable (no begin/end), so we walk it by subscript
// rather than reusing detail::join_csv.
/**
 * @brief Builds a diagnostic string listing the elements of a \c deque.
 *
 * @tparam T Element type stored in the container.
 * @param d The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p d is not modified.
 */
template <std::move_constructible T>
[[nodiscard]] auto to_string(deque<T> const& d) -> std::string {
  std::string body;
  bool first{true};
  for (typename deque<T>::size_type i{0}; i < d.size(); ++i) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}", d[i]);
  }
  return std::format("deque[{}]", body);
}

/**
 * @brief Streams the diagnostic string of \p d to \p os.
 *
 * @tparam T Element type stored in the container.
 * @param os Output stream to write to.
 * @param d The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p d has been written to \p os.
 */
template <std::move_constructible T>
auto operator<<(std::ostream& os, deque<T> const& d) -> std::ostream& {
  return os << to_string(d);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c binary_tree.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param t The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p t is not modified.
 */
template <typename T, typename Compare>
[[nodiscard]] auto to_string(binary_tree<T, Compare> const& t) -> std::string {
  return std::format("binary_tree{{{}}}", detail::join_csv(t));
}

/**
 * @brief Streams the diagnostic string of \p t to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param os Output stream to write to.
 * @param t The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p t has been written to \p os.
 */
template <typename T, typename Compare>
auto operator<<(std::ostream& os, binary_tree<T, Compare> const& t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c heap.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param h The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p h is not modified.
 */
template <typename T, typename Compare>
[[nodiscard]] auto to_string(heap<T, Compare> const& h) -> std::string {
  return std::format("heap[{}]", detail::join_csv(h.span()));
}

/**
 * @brief Streams the diagnostic string of \p h to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param os Output stream to write to.
 * @param h The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p h has been written to \p os.
 */
template <typename T, typename Compare>
auto operator<<(std::ostream& os, heap<T, Compare> const& h) -> std::ostream& {
  return os << to_string(h);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c indexed_priority_queue.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param q The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p q is not modified.
 */
template <typename T, typename Compare>
[[nodiscard]] auto to_string(indexed_priority_queue<T, Compare> const& q) -> std::string {
  std::string body;
  bool first{true};
  for (auto const& e : q.entries()) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}->{}", e.handle, e.value);
  }
  return std::format("indexed_priority_queue{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p q to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param os Output stream to write to.
 * @param q The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p q has been written to \p os.
 */
template <typename T, typename Compare>
auto operator<<(std::ostream& os, indexed_priority_queue<T, Compare> const& q) -> std::ostream& {
  return os << to_string(q);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c union_find.
 *
 * @tparam Index Unsigned index type.
 * @param uf The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p uf is not modified.
 */
template <std::unsigned_integral Index>
[[nodiscard]] auto to_string(union_find<Index> const& uf) -> std::string {
  if (uf.empty()) {
    return std::string{"union_find{}"};
  }
  // Group members by their root.
  std::vector<std::vector<Index>> members(uf.size());
  for (auto const i : uf.nodes()) {
    members[uf.root_of(i)].push_back(i);
  }
  std::string body;
  bool first{true};
  for (auto const& group : members) {
    if (group.empty()) {
      continue;
    }
    if (!first) {
      body += ", ";
    }
    first = false;
    body += "{" + detail::join_csv(group) + "}";
  }
  return std::format("union_find[{}]", body);
}

/**
 * @brief Streams the diagnostic string of \p uf to \p os.
 *
 * @tparam Index Unsigned index type.
 * @param os Output stream to write to.
 * @param uf The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p uf has been written to \p os.
 */
template <std::unsigned_integral Index>
auto operator<<(std::ostream& os, union_find<Index> const& uf) -> std::ostream& {
  return os << to_string(uf);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c graph.
 *
 * @tparam E Edge data type (\c void when the graph is unweighted).
 * @tparam Vertex Unsigned vertex index type.
 * @param g The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p g is not modified.
 */
template <typename E, std::unsigned_integral Vertex>
[[nodiscard]] auto to_string(graph<E, Vertex> const& g) -> std::string {
  std::string body;
  bool first{true};
  for (auto const v : g.vertices()) {
    if (!first) {
      body += ", ";
    }
    first = false;
    std::string neighbours;
    bool first_n{true};
    for (auto const& e : g.edges_of(v)) {
      if (!first_n) {
        neighbours += ", ";
      }
      first_n = false;
      if constexpr (std::is_void_v<E>) {
        neighbours += std::format("{}", e.target);
      } else {
        neighbours += std::format("{}({})", e.target, e.data);
      }
    }
    body += std::format("{}:[{}]", v, neighbours);
  }
  return std::format("graph{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p g to \p os.
 *
 * @tparam E Deduced template parameter.
 * @tparam Vertex Unsigned vertex index type.
 * @param os Output stream to write to.
 * @param g The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p g has been written to \p os.
 */
template <typename E, std::unsigned_integral Vertex>
auto operator<<(std::ostream& os, graph<E, Vertex> const& g) -> std::ostream& {
  return os << to_string(g);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c intrusive_list.
 *
 * @tparam T Element type stored in the container.
 * @param l The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p l is not modified.
 */
template <typename T>
[[nodiscard]] auto to_string(intrusive_list<T> const& l) -> std::string {
  std::string body;
  bool first{true};
  for (auto const& n : l) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}", n);
  }
  return std::format("intrusive_list[{}]", body);
}

/**
 * @brief Streams the diagnostic string of \p l to \p os.
 *
 * @tparam T Element type stored in the container.
 * @param os Output stream to write to.
 * @param l The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p l has been written to \p os.
 */
template <typename T>
auto operator<<(std::ostream& os, intrusive_list<T> const& l) -> std::ostream& {
  return os << to_string(l);
}

/**
 * @brief Builds a diagnostic string for a \c slot_key handle.
 *
 * @param k The handle to describe.
 *
 * @return A readable string of the form \c "slot_key(index:generation)".
 *
 * @pre None.
 * @post None. \p k is not modified.
 */
[[nodiscard]] inline auto to_string(slot_key const k) -> std::string {
  return std::format("slot_key({}:{})", k.index(), k.generation());
}

/**
 * @brief Streams the diagnostic string of \p k to \p os.
 *
 * @param os Output stream to write to.
 * @param k The handle to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post The diagnostic string of \p k has been written to \p os.
 */
inline auto operator<<(std::ostream& os, slot_key const k) -> std::ostream& {
  return os << to_string(k);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c slot_map.
 *
 * @tparam T Element type stored in the container.
 * @param m The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p m is not modified.
 */
template <typename T>
[[nodiscard]] auto to_string(slot_map<T> const& m) -> std::string {
  return std::format("slot_map[{}]", detail::join_csv(m));
}

/**
 * @brief Streams the diagnostic string of \p m to \p os.
 *
 * @tparam T Element type stored in the container.
 * @param os Output stream to write to.
 * @param m The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p m has been written to \p os.
 */
template <typename T>
auto operator<<(std::ostream& os, slot_map<T> const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c sparse_set.
 *
 * @tparam Key Key type.
 * @param s The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p s is not modified.
 */
template <std::unsigned_integral Key>
[[nodiscard]] auto to_string(sparse_set<Key> const& s) -> std::string {
  return std::format("sparse_set{{{}}}", detail::join_csv(s));
}

/**
 * @brief Streams the diagnostic string of \p s to \p os.
 *
 * @tparam Key Key type.
 * @param os Output stream to write to.
 * @param s The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p s has been written to \p os.
 */
template <std::unsigned_integral Key>
auto operator<<(std::ostream& os, sparse_set<Key> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c flat_hash_map.
 *
 * @tparam K Key type.
 * @tparam V Mapped value type.
 * @tparam H Hash function type.
 * @tparam E Key-equality predicate type.
 * @param m The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p m is not modified.
 */
template <typename K, typename V, typename H, typename E>
[[nodiscard]] auto to_string(flat_hash_map<K, V, H, E> const& m) -> std::string {
  std::string body;
  bool first{true};
  for (auto const& [k, v] : m) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}: {}", k, v);
  }
  return std::format("flat_hash_map{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p m to \p os.
 *
 * @tparam K Key type.
 * @tparam V Mapped value type.
 * @tparam H Hash function type.
 * @tparam E Deduced template parameter.
 * @param os Output stream to write to.
 * @param m The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p m has been written to \p os.
 */
template <typename K, typename V, typename H, typename E>
auto operator<<(std::ostream& os, flat_hash_map<K, V, H, E> const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c flat_hash_set.
 *
 * @tparam T Element type stored in the container.
 * @tparam H Hash function type.
 * @tparam E Key-equality predicate type.
 * @param s The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p s is not modified.
 */
template <typename T, typename H, typename E>
[[nodiscard]] auto to_string(flat_hash_set<T, H, E> const& s) -> std::string {
  return std::format("flat_hash_set{{{}}}", detail::join_csv(s));
}

/**
 * @brief Streams the diagnostic string of \p s to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam H Hash function type.
 * @tparam E Deduced template parameter.
 * @param os Output stream to write to.
 * @param s The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p s has been written to \p os.
 */
template <typename T, typename H, typename E>
auto operator<<(std::ostream& os, flat_hash_set<T, H, E> const& s) -> std::ostream& {
  return os << to_string(s);
}

// The ordered flat containers print in the same brace style as their hashed
// cousins ("{k: v}" / "{elems}"), not the C++23 default range rendering of a
// sorted-pair sequence ("[(k, v), ...]"), so the whole module stays uniform.
/**
 * @brief Builds a diagnostic string listing the elements of a \c flat_map.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param m The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p m is not modified.
 */
template <typename Key, typename Value, typename Compare>
[[nodiscard]] auto to_string(flat_map<Key, Value, Compare> const& m) -> std::string {
  std::string body;
  bool first{true};
  for (auto const& [k, v] : m) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}: {}", k, v);
  }
  return std::format("flat_map{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p m to \p os.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param os Output stream to write to.
 * @param m The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p m has been written to \p os.
 */
template <typename Key, typename Value, typename Compare>
auto operator<<(std::ostream& os, flat_map<Key, Value, Compare> const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c flat_set.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param s The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p s is not modified.
 */
template <typename T, typename Compare>
[[nodiscard]] auto to_string(flat_set<T, Compare> const& s) -> std::string {
  return std::format("flat_set{{{}}}", detail::join_csv(s));
}

/**
 * @brief Streams the diagnostic string of \p s to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param os Output stream to write to.
 * @param s The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p s has been written to \p os.
 */
template <typename T, typename Compare>
auto operator<<(std::ostream& os, flat_set<T, Compare> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c static_flat_map.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Capacity Fixed maximum element count.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param m The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p m is not modified.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Compare>
[[nodiscard]] auto to_string(static_flat_map<Key, Value, Capacity, Compare> const& m)
  -> std::string {
  std::string body;
  bool first{true};
  for (auto const& [k, v] : m) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}: {}", k, v);
  }
  return std::format("static_flat_map{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p m to \p os.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Capacity Fixed maximum element count.
 * @tparam Compare Strict-weak-ordering comparator type.
 * @param os Output stream to write to.
 * @param m The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p m has been written to \p os.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Compare>
auto operator<<(std::ostream& os, static_flat_map<Key, Value, Capacity, Compare> const& m)
  -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c bimap.
 *
 * @tparam L Left key type.
 * @tparam R Right key type.
 * @tparam HL Left-key hash type.
 * @tparam HR Right-key hash type.
 * @param m The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p m is not modified.
 */
template <typename L, typename R, typename HL, typename HR>
[[nodiscard]] auto to_string(bimap<L, R, HL, HR> const& m) -> std::string {
  std::string body;
  bool first{true};
  for (auto const& [l, r] : m) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{} <-> {}", l, r);
  }
  return std::format("bimap{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p m to \p os.
 *
 * @tparam L Left key type.
 * @tparam R Right key type.
 * @tparam HL Left-key hash type.
 * @tparam HR Right-key hash type.
 * @param os Output stream to write to.
 * @param m The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p m has been written to \p os.
 */
template <typename L, typename R, typename HL, typename HR>
auto operator<<(std::ostream& os, bimap<L, R, HL, HR> const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c gap_buffer.
 *
 * @tparam T Element type stored in the container.
 * @param b The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p b is not modified.
 */
template <typename T>
[[nodiscard]] auto to_string(gap_buffer<T> const& b) -> std::string {
  return std::format("gap_buffer[{}]", detail::join_csv(b));
}

/**
 * @brief Streams the diagnostic string of \p b to \p os.
 *
 * @tparam T Element type stored in the container.
 * @param os Output stream to write to.
 * @param b The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p b has been written to \p os.
 */
template <typename T>
auto operator<<(std::ostream& os, gap_buffer<T> const& b) -> std::ostream& {
  return os << to_string(b);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c dense_map.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @param m The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p m is not modified.
 */
template <std::unsigned_integral Key, std::move_constructible Value>
[[nodiscard]] auto to_string(dense_map<Key, Value> const& m) -> std::string {
  std::string body;
  bool first{true};
  auto const keys{m.keys()};
  auto const values{m.values()};
  for (std::size_t i{0}; i < keys.size(); ++i) {
    if (!first) {
      body += ", ";
    }
    first = false;
    body += std::format("{}: {}", keys[i], values[i]);
  }
  return std::format("dense_map{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p m to \p os.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @param os Output stream to write to.
 * @param m The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p m has been written to \p os.
 */
template <std::unsigned_integral Key, std::move_constructible Value>
auto operator<<(std::ostream& os, dense_map<Key, Value> const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c ring_buffer.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param r The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p r is not modified.
 */
template <typename T, std::size_t N>
[[nodiscard]] auto to_string(ring_buffer<T, N> const& r) -> std::string {
  return std::format("ring_buffer[{}]", detail::join_csv(r));
}

/**
 * @brief Streams the diagnostic string of \p r to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param r The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p r has been written to \p os.
 */
template <typename T, std::size_t N>
auto operator<<(std::ostream& os, ring_buffer<T, N> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c small_vector.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param v The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p v is not modified.
 */
template <typename T, std::size_t N>
[[nodiscard]] auto to_string(small_vector<T, N> const& v) -> std::string {
  return std::format("small_vector[{}]", detail::join_csv(v));
}

/**
 * @brief Streams the diagnostic string of \p v to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param v The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p v has been written to \p os.
 */
template <typename T, std::size_t N>
auto operator<<(std::ostream& os, small_vector<T, N> const& v) -> std::ostream& {
  return os << to_string(v);
}

/**
 * @brief Builds a diagnostic string listing the elements of a \c trie.
 *
 * @tparam Char Key symbol type.
 * @tparam Value Mapped value type.
 * @param t The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post None. \p t is not modified.
 */
template <typename Char, typename Value>
[[nodiscard]] auto to_string(trie<Char, Value> const& t) -> std::string {
  std::string body;
  bool first{true};
  t.for_each([&](std::span<Char const> const key, Value const& value) {
    if (!first) {
      body += ", ";
    }
    first = false;
    std::string key_str;
    key_str.reserve(key.size());
    for (auto const c : key) {
      key_str.push_back(static_cast<char>(c));
    }
    body += std::format("\"{}\": {}", key_str, value);
  });
  return std::format("trie{{{}}}", body);
}

/**
 * @brief Streams the diagnostic string of \p t to \p os.
 *
 * @tparam Char Key symbol type.
 * @tparam Value Mapped value type.
 * @param os Output stream to write to.
 * @param t The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p t has been written to \p os.
 */
template <typename Char, typename Value>
auto operator<<(std::ostream& os, trie<Char, Value> const& t) -> std::ostream& {
  return os << to_string(t);
}

// The types below expose no safe element enumeration (the lock-free queues are
// mutated concurrently, the memory tier hands out raw storage, the bloom filter
// cannot list its members, the LRU cache offers no iteration), so each prints a
// single-line stats form rather than its elements.

/**
 * @brief Builds a one-line diagnostic string for a \c bloom_filter.
 *
 * @tparam T Element type stored in the container.
 * @tparam Hash Hash function type.
 * @param f The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p f is not modified.
 */
template <typename T, typename Hash>
[[nodiscard]] auto to_string(bloom_filter<T, Hash> const& f) -> std::string {
  return std::format(
    "bloom_filter(bit_count={}, hash_count={}, insertions={}, false_positive_rate={})",
    f.bit_count(),
    f.hash_count(),
    f.insertions(),
    f.false_positive_rate()
  );
}

/**
 * @brief Streams the diagnostic string of \p f to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam Hash Hash function type.
 * @param os Output stream to write to.
 * @param f The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p f has been written to \p os.
 */
template <typename T, typename Hash>
auto operator<<(std::ostream& os, bloom_filter<T, Hash> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Builds a one-line diagnostic string for a \c lru_cache.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Capacity Fixed maximum element count.
 * @tparam Hash Hash function type.
 * @tparam KeyEq Key-equality predicate type.
 * @param c The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p c is not modified.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Hash, typename KeyEq>
[[nodiscard]] auto to_string(lru_cache<Key, Value, Capacity, Hash, KeyEq> const& c) -> std::string {
  return std::format("lru_cache(size={}, capacity={})", c.size(), c.capacity());
}

/**
 * @brief Streams the diagnostic string of \p c to \p os.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Capacity Fixed maximum element count.
 * @tparam Hash Hash function type.
 * @tparam KeyEq Key-equality predicate type.
 * @param os Output stream to write to.
 * @param c The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p c has been written to \p os.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Hash, typename KeyEq>
auto operator<<(std::ostream& os, lru_cache<Key, Value, Capacity, Hash, KeyEq> const& c)
  -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Builds a one-line diagnostic string for a \c spsc_queue.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param q The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p q is not modified.
 */
template <std::move_constructible T, std::size_t N>
[[nodiscard]] auto to_string(spsc_queue<T, N> const& q) -> std::string {
  return std::format("spsc_queue(size_approx={}, capacity={})", q.size_approx(), q.capacity());
}

/**
 * @brief Streams the diagnostic string of \p q to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param q The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p q has been written to \p os.
 */
template <std::move_constructible T, std::size_t N>
auto operator<<(std::ostream& os, spsc_queue<T, N> const& q) -> std::ostream& {
  return os << to_string(q);
}

/**
 * @brief Builds a one-line diagnostic string for a \c mpsc_queue.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param q The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p q is not modified.
 */
template <std::move_constructible T, std::size_t N>
[[nodiscard]] auto to_string(mpsc_queue<T, N> const& q) -> std::string {
  return std::format("mpsc_queue(size_approx={}, capacity={})", q.size_approx(), q.capacity());
}

/**
 * @brief Streams the diagnostic string of \p q to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param q The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p q has been written to \p os.
 */
template <std::move_constructible T, std::size_t N>
auto operator<<(std::ostream& os, mpsc_queue<T, N> const& q) -> std::ostream& {
  return os << to_string(q);
}

/**
 * @brief Builds a one-line diagnostic string for a \c mpmc_queue.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param q The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p q is not modified.
 */
template <std::move_constructible T, std::size_t N>
[[nodiscard]] auto to_string(mpmc_queue<T, N> const& q) -> std::string {
  return std::format("mpmc_queue(size_approx={}, capacity={})", q.size_approx(), q.capacity());
}

/**
 * @brief Streams the diagnostic string of \p q to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param q The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p q has been written to \p os.
 */
template <std::move_constructible T, std::size_t N>
auto operator<<(std::ostream& os, mpmc_queue<T, N> const& q) -> std::ostream& {
  return os << to_string(q);
}

/**
 * @brief Builds a one-line diagnostic string for a \c object_pool.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param p The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p p is not modified.
 */
template <typename T, std::size_t N>
[[nodiscard]] auto to_string(object_pool<T, N> const& p) -> std::string {
  return std::format(
    "object_pool(size={}, capacity={}, high_water_mark={})",
    p.size(),
    p.capacity(),
    p.high_water_mark()
  );
}

/**
 * @brief Streams the diagnostic string of \p p to \p os.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param p The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p p has been written to \p os.
 */
template <typename T, std::size_t N>
auto operator<<(std::ostream& os, object_pool<T, N> const& p) -> std::ostream& {
  return os << to_string(p);
}

/**
 * @brief Builds a one-line diagnostic string for a \c linear_arena.
 *
 * @tparam N Fixed capacity or inline element count.
 * @param a The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p a is not modified.
 */
template <std::size_t N>
[[nodiscard]] auto to_string(linear_arena<N> const& a) -> std::string {
  return std::format(
    "linear_arena(bytes_used={}, capacity={}, high_water_mark={})",
    a.bytes_used(),
    a.capacity(),
    a.high_water_mark()
  );
}

/**
 * @brief Streams the diagnostic string of \p a to \p os.
 *
 * @tparam N Fixed capacity or inline element count.
 * @param os Output stream to write to.
 * @param a The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p a has been written to \p os.
 */
template <std::size_t N>
auto operator<<(std::ostream& os, linear_arena<N> const& a) -> std::ostream& {
  return os << to_string(a);
}

/**
 * @brief Builds a one-line diagnostic string for a \c scratch_pad.
 *
 * @tparam Arena Underlying checkpointable arena type.
 * @param s The container to describe.
 *
 * @return A readable diagnostic string.
 *
 * @pre None.
 * @post None. \p s is not modified.
 */
template <checkpointable_arena Arena>
[[nodiscard]] auto to_string(scratch_pad<Arena> const& s) -> std::string {
  return std::format(
    "scratch_pad(saved_offset={}, bytes_used={})", s.saved_offset(), s.arena().bytes_used()
  );
}

/**
 * @brief Streams the diagnostic string of \p s to \p os.
 *
 * @tparam Arena Underlying checkpointable arena type.
 * @param os Output stream to write to.
 * @param s The container to describe.
 *
 * @return \p os, to allow chaining.
 *
 * @pre Every contained element is formattable through \c std::format.
 * @post The diagnostic string of \p s has been written to \p os.
 */
template <checkpointable_arena Arena>
auto operator<<(std::ostream& os, scratch_pad<Arena> const& s) -> std::ostream& {
  return os << to_string(s);
}

}  // namespace nexenne::container

/**
 * @brief \c std::formatter that prints a \c bitset_dynamic via
 *        \c nexenne::container::to_string.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::container::bitset_dynamic> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p b to the output.
   *
   * @param b Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p b has been written through \p ctx.
   */
  static auto format(nexenne::container::bitset_dynamic const& b, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(b));
  }
};

/**
 * @brief \c std::formatter that prints a \c static_vector via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::static_vector<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p v to the output.
   *
   * @param v Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p v has been written through \p ctx.
   */
  static auto format(nexenne::container::static_vector<T, N> const& v, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(v));
  }
};

/**
 * @brief \c std::formatter that prints a \c stable_vector via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam ChunkSize Number of elements per allocated chunk.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::size_t ChunkSize>
struct std::formatter<nexenne::container::stable_vector<T, ChunkSize>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p v to the output.
   *
   * @param v Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p v has been written through \p ctx.
   */
  static auto format(nexenne::container::stable_vector<T, ChunkSize> const& v, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(v));
  }
};

/**
 * @brief \c std::formatter that prints a \c bag via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
struct std::formatter<nexenne::container::bag<T>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p b to the output.
   *
   * @param b Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p b has been written through \p ctx.
   */
  static auto format(nexenne::container::bag<T> const& b, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(b));
  }
};

/**
 * @brief \c std::formatter that prints a \c deque via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 *
 * @pre None.
 * @post None.
 */
template <std::move_constructible T>
struct std::formatter<nexenne::container::deque<T>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p d to the output.
   *
   * @param d Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p d has been written through \p ctx.
   */
  static auto format(nexenne::container::deque<T> const& d, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(d));
  }
};

/**
 * @brief \c std::formatter that prints a \c binary_tree via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 *
 * @pre None.
 * @post None.
 */
template <typename T, typename Compare>
struct std::formatter<nexenne::container::binary_tree<T, Compare>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p t to the output.
   *
   * @param t Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p t has been written through \p ctx.
   */
  static auto format(nexenne::container::binary_tree<T, Compare> const& t, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(t));
  }
};

/**
 * @brief \c std::formatter that prints a \c heap via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 *
 * @pre None.
 * @post None.
 */
template <typename T, typename Compare>
struct std::formatter<nexenne::container::heap<T, Compare>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p h to the output.
   *
   * @param h Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p h has been written through \p ctx.
   */
  static auto format(nexenne::container::heap<T, Compare> const& h, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(h));
  }
};

/**
 * @brief \c std::formatter that prints a \c indexed_priority_queue via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 *
 * @pre None.
 * @post None.
 */
template <typename T, typename Compare>
struct std::formatter<nexenne::container::indexed_priority_queue<T, Compare>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p q to the output.
   *
   * @param q Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p q has been written through \p ctx.
   */
  static auto format(nexenne::container::indexed_priority_queue<T, Compare> const& q, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(q));
  }
};

/**
 * @brief \c std::formatter that prints a \c union_find via
 *        \c nexenne::container::to_string.
 *
 * @tparam Index Unsigned index type.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral Index>
struct std::formatter<nexenne::container::union_find<Index>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p uf to the output.
   *
   * @param uf Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p uf has been written through \p ctx.
   */
  static auto format(nexenne::container::union_find<Index> const& uf, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(uf));
  }
};

/**
 * @brief \c std::formatter that prints a \c graph via
 *        \c nexenne::container::to_string.
 *
 * @tparam E Edge data type (\c void when the graph is unweighted).
 * @tparam Vertex Unsigned vertex index type.
 *
 * @pre None.
 * @post None.
 */
template <typename E, std::unsigned_integral Vertex>
struct std::formatter<nexenne::container::graph<E, Vertex>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p g to the output.
   *
   * @param g Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p g has been written through \p ctx.
   */
  static auto format(nexenne::container::graph<E, Vertex> const& g, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(g));
  }
};

/**
 * @brief \c std::formatter that prints a \c intrusive_list via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
struct std::formatter<nexenne::container::intrusive_list<T>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p l to the output.
   *
   * @param l Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p l has been written through \p ctx.
   */
  static auto format(nexenne::container::intrusive_list<T> const& l, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(l));
  }
};

/**
 * @brief \c std::formatter that prints a \c trie via
 *        \c nexenne::container::to_string.
 *
 * @tparam Char Key symbol type.
 * @tparam Value Mapped value type.
 *
 * @pre None.
 * @post None.
 */
template <typename Char, typename Value>
struct std::formatter<nexenne::container::trie<Char, Value>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p t to the output.
   *
   * @param t Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p t has been written through \p ctx.
   */
  static auto format(nexenne::container::trie<Char, Value> const& t, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(t));
  }
};

/**
 * @brief \c std::formatter that prints a \c slot_key via
 *        \c nexenne::container::to_string.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::container::slot_key> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p k to the output.
   *
   * @param k Handle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p k has been written through \p ctx.
   */
  static auto format(nexenne::container::slot_key const k, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(k));
  }
};

/**
 * @brief \c std::formatter that prints a \c slot_map via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
struct std::formatter<nexenne::container::slot_map<T>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p m to the output.
   *
   * @param m Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p m has been written through \p ctx.
   */
  static auto format(nexenne::container::slot_map<T> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/**
 * @brief \c std::formatter that prints a \c sparse_set via
 *        \c nexenne::container::to_string.
 *
 * @tparam Key Key type.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral Key>
struct std::formatter<nexenne::container::sparse_set<Key>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p s to the output.
   *
   * @param s Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p s has been written through \p ctx.
   */
  static auto format(nexenne::container::sparse_set<Key> const& s, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(s));
  }
};

/**
 * @brief \c std::formatter that prints a \c dense_map via
 *        \c nexenne::container::to_string.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral Key, std::move_constructible Value>
struct std::formatter<nexenne::container::dense_map<Key, Value>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p m to the output.
   *
   * @param m Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p m has been written through \p ctx.
   */
  static auto format(nexenne::container::dense_map<Key, Value> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/**
 * @brief \c std::formatter that prints a \c ring_buffer via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::ring_buffer<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p r to the output.
   *
   * @param r Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p r has been written through \p ctx.
   */
  static auto format(nexenne::container::ring_buffer<T, N> const& r, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(r));
  }
};

/**
 * @brief \c std::formatter that prints a \c small_vector via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::small_vector<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p v to the output.
   *
   * @param v Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p v has been written through \p ctx.
   */
  static auto format(nexenne::container::small_vector<T, N> const& v, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(v));
  }
};

/**
 * @brief \c std::formatter that prints a \c flat_hash_map via
 *        \c nexenne::container::to_string.
 *
 * @tparam K Key type.
 * @tparam V Mapped value type.
 * @tparam H Hash function type.
 * @tparam E Key-equality predicate type.
 *
 * @pre None.
 * @post None.
 */
template <typename K, typename V, typename H, typename E>
struct std::formatter<nexenne::container::flat_hash_map<K, V, H, E>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p m to the output.
   *
   * @param m Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p m has been written through \p ctx.
   */
  static auto format(nexenne::container::flat_hash_map<K, V, H, E> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/**
 * @brief \c std::formatter that prints a \c flat_hash_set via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam H Hash function type.
 * @tparam E Key-equality predicate type.
 *
 * @pre None.
 * @post None.
 */
template <typename T, typename H, typename E>
struct std::formatter<nexenne::container::flat_hash_set<T, H, E>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p s to the output.
   *
   * @param s Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p s has been written through \p ctx.
   */
  static auto format(nexenne::container::flat_hash_set<T, H, E> const& s, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(s));
  }
};

/**
 * @brief \c std::formatter that prints a \c flat_map via
 *        \c nexenne::container::to_string.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Compare Strict-weak-ordering comparator type.
 *
 * @pre None.
 * @post None.
 */
template <typename Key, typename Value, typename Compare>
struct std::formatter<nexenne::container::flat_map<Key, Value, Compare>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p m to the output.
   *
   * @param m Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p m has been written through \p ctx.
   */
  static auto format(nexenne::container::flat_map<Key, Value, Compare> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/**
 * @brief \c std::formatter that prints a \c flat_set via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam Compare Strict-weak-ordering comparator type.
 *
 * @pre None.
 * @post None.
 */
template <typename T, typename Compare>
struct std::formatter<nexenne::container::flat_set<T, Compare>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p s to the output.
   *
   * @param s Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p s has been written through \p ctx.
   */
  static auto format(nexenne::container::flat_set<T, Compare> const& s, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(s));
  }
};

/**
 * @brief \c std::formatter that prints a \c static_flat_map via
 *        \c nexenne::container::to_string.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Capacity Fixed maximum element count.
 * @tparam Compare Strict-weak-ordering comparator type.
 *
 * @pre None.
 * @post None.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Compare>
struct std::formatter<nexenne::container::static_flat_map<Key, Value, Capacity, Compare>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p m to the output.
   *
   * @param m Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p m has been written through \p ctx.
   */
  static auto
  format(nexenne::container::static_flat_map<Key, Value, Capacity, Compare> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/**
 * @brief \c std::formatter that prints a \c bimap via
 *        \c nexenne::container::to_string.
 *
 * @tparam L Left key type.
 * @tparam R Right key type.
 * @tparam HL Left-key hash type.
 * @tparam HR Right-key hash type.
 *
 * @pre None.
 * @post None.
 */
template <typename L, typename R, typename HL, typename HR>
struct std::formatter<nexenne::container::bimap<L, R, HL, HR>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p m to the output.
   *
   * @param m Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p m has been written through \p ctx.
   */
  static auto format(nexenne::container::bimap<L, R, HL, HR> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/**
 * @brief \c std::formatter that prints a \c gap_buffer via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
struct std::formatter<nexenne::container::gap_buffer<T>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p b to the output.
   *
   * @param b Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p b has been written through \p ctx.
   */
  static auto format(nexenne::container::gap_buffer<T> const& b, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(b));
  }
};

/**
 * @brief \c std::formatter that prints a \c bloom_filter via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam Hash Hash function type.
 *
 * @pre None.
 * @post None.
 */
template <typename T, typename Hash>
struct std::formatter<nexenne::container::bloom_filter<T, Hash>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p f to the output.
   *
   * @param f Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p f has been written through \p ctx.
   */
  static auto format(nexenne::container::bloom_filter<T, Hash> const& f, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(f));
  }
};

/**
 * @brief \c std::formatter that prints a \c lru_cache via
 *        \c nexenne::container::to_string.
 *
 * @tparam Key Key type.
 * @tparam Value Mapped value type.
 * @tparam Capacity Fixed maximum element count.
 * @tparam Hash Hash function type.
 * @tparam KeyEq Key-equality predicate type.
 *
 * @pre None.
 * @post None.
 */
template <typename Key, typename Value, std::size_t Capacity, typename Hash, typename KeyEq>
struct std::formatter<nexenne::container::lru_cache<Key, Value, Capacity, Hash, KeyEq>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p c to the output.
   *
   * @param c Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p c has been written through \p ctx.
   */
  static auto
  format(nexenne::container::lru_cache<Key, Value, Capacity, Hash, KeyEq> const& c, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(c));
  }
};

/**
 * @brief \c std::formatter that prints a \c spsc_queue via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <std::move_constructible T, std::size_t N>
struct std::formatter<nexenne::container::spsc_queue<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p q to the output.
   *
   * @param q Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p q has been written through \p ctx.
   */
  static auto format(nexenne::container::spsc_queue<T, N> const& q, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(q));
  }
};

/**
 * @brief \c std::formatter that prints a \c mpsc_queue via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <std::move_constructible T, std::size_t N>
struct std::formatter<nexenne::container::mpsc_queue<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p q to the output.
   *
   * @param q Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p q has been written through \p ctx.
   */
  static auto format(nexenne::container::mpsc_queue<T, N> const& q, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(q));
  }
};

/**
 * @brief \c std::formatter that prints a \c mpmc_queue via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <std::move_constructible T, std::size_t N>
struct std::formatter<nexenne::container::mpmc_queue<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p q to the output.
   *
   * @param q Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p q has been written through \p ctx.
   */
  static auto format(nexenne::container::mpmc_queue<T, N> const& q, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(q));
  }
};

/**
 * @brief \c std::formatter that prints a \c object_pool via
 *        \c nexenne::container::to_string.
 *
 * @tparam T Element type stored in the container.
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::object_pool<T, N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p p to the output.
   *
   * @param p Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p p has been written through \p ctx.
   */
  static auto format(nexenne::container::object_pool<T, N> const& p, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(p));
  }
};

/**
 * @brief \c std::formatter that prints a \c linear_arena via
 *        \c nexenne::container::to_string.
 *
 * @tparam N Fixed capacity or inline element count.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::formatter<nexenne::container::linear_arena<N>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p a to the output.
   *
   * @param a Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p a has been written through \p ctx.
   */
  static auto format(nexenne::container::linear_arena<N> const& a, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(a));
  }
};

/**
 * @brief \c std::formatter that prints a \c scratch_pad via
 *        \c nexenne::container::to_string.
 *
 * @tparam Arena Underlying checkpointable arena type.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::container::checkpointable_arena Arena>
struct std::formatter<nexenne::container::scratch_pad<Arena>> {
  /**
   * @brief Accepts the empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec is empty; only \c "{}" is supported.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p s to the output.
   *
   * @param s Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The diagnostic string of \p s has been written through \p ctx.
   */
  static auto format(nexenne::container::scratch_pad<Arena> const& s, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(s));
  }
};

/**
 * @brief \c std::format support for \c container_error: prints its \c to_string
 *        name, so \c std::format("{}", err) works on a value from a \c result<T>.
 *
 * Inherits the string formatter, so a spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::container::container_error> : std::formatter<std::string_view> {
  /**
   * @brief Formats the error's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param err Error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The error name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::container::container_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::container::to_string(err), ctx);
  }
};
