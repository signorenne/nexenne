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
 */

#include <concepts>
#include <cstddef>
#include <format>
#include <ostream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <nexenne/container/bag.hpp>
#include <nexenne/container/bimap.hpp>
#include <nexenne/container/binary_tree.hpp>
#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/dense_map.hpp>
#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/container/flat_hash_set.hpp>
#include <nexenne/container/gap_buffer.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/container/heap.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/container/intrusive_list.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/slot_map.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/container/sparse_set.hpp>
#include <nexenne/container/stable_vector.hpp>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/container/trie.hpp>
#include <nexenne/container/union_find.hpp>

namespace nexenne::container {

namespace detail {

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

}  // namespace detail

[[nodiscard]] inline auto to_string(bitset_dynamic const& b) -> std::string {
  std::string bits;
  bits.reserve(b.size());
  // MSB-first, matching the conventional binary-literal direction.
  for (auto i{b.size()}; i > 0; --i) {
    bits.push_back(b[i - 1] ? '1' : '0');
  }
  return std::format("bitset_dynamic(size={}, bits=0b{})", b.size(), bits);
}

inline auto operator<<(std::ostream& os, bitset_dynamic const& b) -> std::ostream& {
  return os << to_string(b);
}

template <typename T, std::size_t N>
[[nodiscard]] auto to_string(static_vector<T, N> const& v) -> std::string {
  return std::format("static_vector[{}]", detail::join_csv(v));
}

template <typename T, std::size_t N>
auto operator<<(std::ostream& os, static_vector<T, N> const& v) -> std::ostream& {
  return os << to_string(v);
}

template <typename T, std::size_t ChunkSize>
[[nodiscard]] auto to_string(stable_vector<T, ChunkSize> const& v) -> std::string {
  return std::format("stable_vector[{}]", detail::join_csv(v));
}

template <typename T, std::size_t ChunkSize>
auto operator<<(std::ostream& os, stable_vector<T, ChunkSize> const& v) -> std::ostream& {
  return os << to_string(v);
}

template <typename T>
[[nodiscard]] auto to_string(bag<T> const& b) -> std::string {
  return std::format("bag[{}]", detail::join_csv(b));
}

template <typename T>
auto operator<<(std::ostream& os, bag<T> const& b) -> std::ostream& {
  return os << to_string(b);
}

template <typename T, typename Compare>
[[nodiscard]] auto to_string(binary_tree<T, Compare> const& t) -> std::string {
  return std::format("binary_tree{{{}}}", detail::join_csv(t));
}

template <typename T, typename Compare>
auto operator<<(std::ostream& os, binary_tree<T, Compare> const& t) -> std::ostream& {
  return os << to_string(t);
}

template <typename T, typename Compare>
[[nodiscard]] auto to_string(heap<T, Compare> const& h) -> std::string {
  return std::format("heap[{}]", detail::join_csv(h.span()));
}

template <typename T, typename Compare>
auto operator<<(std::ostream& os, heap<T, Compare> const& h) -> std::ostream& {
  return os << to_string(h);
}

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

template <typename T, typename Compare>
auto operator<<(std::ostream& os, indexed_priority_queue<T, Compare> const& q) -> std::ostream& {
  return os << to_string(q);
}

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

template <std::unsigned_integral Index>
auto operator<<(std::ostream& os, union_find<Index> const& uf) -> std::ostream& {
  return os << to_string(uf);
}

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

template <typename E, std::unsigned_integral Vertex>
auto operator<<(std::ostream& os, graph<E, Vertex> const& g) -> std::ostream& {
  return os << to_string(g);
}

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

template <typename T>
auto operator<<(std::ostream& os, intrusive_list<T> const& l) -> std::ostream& {
  return os << to_string(l);
}

template <typename T>
[[nodiscard]] auto to_string(slot_map<T> const& m) -> std::string {
  return std::format("slot_map[{}]", detail::join_csv(m));
}

template <typename T>
auto operator<<(std::ostream& os, slot_map<T> const& m) -> std::ostream& {
  return os << to_string(m);
}

template <std::unsigned_integral Key>
[[nodiscard]] auto to_string(sparse_set<Key> const& s) -> std::string {
  return std::format("sparse_set{{{}}}", detail::join_csv(s));
}

template <std::unsigned_integral Key>
auto operator<<(std::ostream& os, sparse_set<Key> const& s) -> std::ostream& {
  return os << to_string(s);
}

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

template <typename K, typename V, typename H, typename E>
auto operator<<(std::ostream& os, flat_hash_map<K, V, H, E> const& m) -> std::ostream& {
  return os << to_string(m);
}

template <typename T, typename H, typename E>
[[nodiscard]] auto to_string(flat_hash_set<T, H, E> const& s) -> std::string {
  return std::format("flat_hash_set{{{}}}", detail::join_csv(s));
}

template <typename T, typename H, typename E>
auto operator<<(std::ostream& os, flat_hash_set<T, H, E> const& s) -> std::ostream& {
  return os << to_string(s);
}

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

template <typename L, typename R, typename HL, typename HR>
auto operator<<(std::ostream& os, bimap<L, R, HL, HR> const& m) -> std::ostream& {
  return os << to_string(m);
}

template <typename T>
[[nodiscard]] auto to_string(gap_buffer<T> const& b) -> std::string {
  return std::format("gap_buffer[{}]", detail::join_csv(b));
}

template <typename T>
auto operator<<(std::ostream& os, gap_buffer<T> const& b) -> std::ostream& {
  return os << to_string(b);
}

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

template <std::unsigned_integral Key, std::move_constructible Value>
auto operator<<(std::ostream& os, dense_map<Key, Value> const& m) -> std::ostream& {
  return os << to_string(m);
}

template <typename T, std::size_t N>
[[nodiscard]] auto to_string(ring_buffer<T, N> const& r) -> std::string {
  return std::format("ring_buffer[{}]", detail::join_csv(r));
}

template <typename T, std::size_t N>
auto operator<<(std::ostream& os, ring_buffer<T, N> const& r) -> std::ostream& {
  return os << to_string(r);
}

template <typename T, std::size_t N>
[[nodiscard]] auto to_string(small_vector<T, N> const& v) -> std::string {
  return std::format("small_vector[{}]", detail::join_csv(v));
}

template <typename T, std::size_t N>
auto operator<<(std::ostream& os, small_vector<T, N> const& v) -> std::ostream& {
  return os << to_string(v);
}

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

template <typename Char, typename Value>
auto operator<<(std::ostream& os, trie<Char, Value> const& t) -> std::ostream& {
  return os << to_string(t);
}

}  // namespace nexenne::container

/// Formats a \c bitset_dynamic via \c nexenne::container::to_string.
template <>
struct std::formatter<nexenne::container::bitset_dynamic> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::bitset_dynamic const& b, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(b));
  }
};

/// Formats a \c static_vector via \c nexenne::container::to_string.
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::static_vector<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::static_vector<T, N> const& v, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(v));
  }
};

/// Formats a \c stable_vector via \c nexenne::container::to_string.
template <typename T, std::size_t ChunkSize>
struct std::formatter<nexenne::container::stable_vector<T, ChunkSize>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::stable_vector<T, ChunkSize> const& v, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(v));
  }
};

/// Formats a \c bag via \c nexenne::container::to_string.
template <typename T>
struct std::formatter<nexenne::container::bag<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::bag<T> const& b, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(b));
  }
};

/// Formats a \c binary_tree via \c nexenne::container::to_string.
template <typename T, typename Compare>
struct std::formatter<nexenne::container::binary_tree<T, Compare>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::binary_tree<T, Compare> const& t, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(t));
  }
};

/// Formats a \c heap via \c nexenne::container::to_string.
template <typename T, typename Compare>
struct std::formatter<nexenne::container::heap<T, Compare>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::heap<T, Compare> const& h, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(h));
  }
};

/// Formats an \c indexed_priority_queue via \c nexenne::container::to_string.
template <typename T, typename Compare>
struct std::formatter<nexenne::container::indexed_priority_queue<T, Compare>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::indexed_priority_queue<T, Compare> const& q, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(q));
  }
};

/// Formats a \c union_find via \c nexenne::container::to_string.
template <std::unsigned_integral Index>
struct std::formatter<nexenne::container::union_find<Index>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::union_find<Index> const& uf, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(uf));
  }
};

/// Formats a \c graph via \c nexenne::container::to_string.
template <typename E, std::unsigned_integral Vertex>
struct std::formatter<nexenne::container::graph<E, Vertex>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::graph<E, Vertex> const& g, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(g));
  }
};

/// Formats an \c intrusive_list via \c nexenne::container::to_string.
template <typename T>
struct std::formatter<nexenne::container::intrusive_list<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::intrusive_list<T> const& l, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(l));
  }
};

/// Formats a \c trie via \c nexenne::container::to_string.
template <typename Char, typename Value>
struct std::formatter<nexenne::container::trie<Char, Value>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::trie<Char, Value> const& t, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(t));
  }
};

/// Formats a \c slot_map via \c nexenne::container::to_string.
template <typename T>
struct std::formatter<nexenne::container::slot_map<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::slot_map<T> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/// Formats a \c sparse_set via \c nexenne::container::to_string.
template <std::unsigned_integral Key>
struct std::formatter<nexenne::container::sparse_set<Key>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::sparse_set<Key> const& s, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(s));
  }
};

/// Formats a \c dense_map via \c nexenne::container::to_string.
template <std::unsigned_integral Key, std::move_constructible Value>
struct std::formatter<nexenne::container::dense_map<Key, Value>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::dense_map<Key, Value> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/// Formats a \c ring_buffer via \c nexenne::container::to_string.
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::ring_buffer<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::ring_buffer<T, N> const& r, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(r));
  }
};

/// Formats a \c small_vector via \c nexenne::container::to_string.
template <typename T, std::size_t N>
struct std::formatter<nexenne::container::small_vector<T, N>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::small_vector<T, N> const& v, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(v));
  }
};

/// Formats a \c flat_hash_map via \c nexenne::container::to_string.
template <typename K, typename V, typename H, typename E>
struct std::formatter<nexenne::container::flat_hash_map<K, V, H, E>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::flat_hash_map<K, V, H, E> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/// Formats a \c flat_hash_set via \c nexenne::container::to_string.
template <typename T, typename H, typename E>
struct std::formatter<nexenne::container::flat_hash_set<T, H, E>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::flat_hash_set<T, H, E> const& s, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(s));
  }
};

/// Formats a \c bimap via \c nexenne::container::to_string.
template <typename L, typename R, typename HL, typename HR>
struct std::formatter<nexenne::container::bimap<L, R, HL, HR>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::bimap<L, R, HL, HR> const& m, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(m));
  }
};

/// Formats a \c gap_buffer via \c nexenne::container::to_string.
template <typename T>
struct std::formatter<nexenne::container::gap_buffer<T>> {
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  static auto format(nexenne::container::gap_buffer<T> const& b, auto& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::container::to_string(b));
  }
};
