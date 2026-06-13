#pragma once

/**
 * @file
 * @brief Generic adjacency-list graph with optional per-edge payload.
 *
 * \c graph<E, Vertex> stores vertices identified by dense integer indices and a
 * list of directed edges between them. Each vertex owns a contiguous list of
 * outgoing edges; an edge holds the target vertex and a user-defined payload \p E
 * (use \c void for none). Vertex IDs are handed out by \c add_vertex starting at
 * zero and stay stable across insertions; the graph does not support vertex
 * removal (mark a vertex logically dead outside the container instead).
 *
 * Over a raw \c vector<vector<pair>> it adds bounds-checked endpoints, an
 * \c edges_of view that hands back a contiguous \c std::span, a \c neighbors view
 * of target IDs, and documented complexities. It deliberately stores only
 * topology and payload: traversals and algorithms (BFS, DFS, shortest paths)
 * belong in a separate module. For an undirected edge, call \c add_edge once each
 * way. Every operation is \c noexcept; allocation failure terminates. Storage is
 * \c std::vector throughout, so the special members are the Rule of Zero defaults.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>

namespace nexenne::container {

namespace detail {

/// One outgoing edge: the target vertex and the per-edge payload.
template <typename E, typename Vertex>
struct graph_edge {
  Vertex target;
  E data;
};

/// Payload-free edge specialisation: just the target vertex.
template <typename Vertex>
struct graph_edge<void, Vertex> {
  Vertex target;
};

}  // namespace detail

/**
 * @brief Directed adjacency-list graph with an optional per-edge payload.
 *
 * @tparam E Edge payload type; \c void to omit per-edge data.
 * @tparam Vertex Unsigned integer type for vertex IDs; \c std::uint32_t by
 *         default.
 *
 * @pre None.
 * @post A default-constructed graph has no vertices and no edges.
 */
template <typename E = void, std::unsigned_integral Vertex = std::uint32_t>
class graph {
public:
  using vertex_type = Vertex;
  using edge_type = E;
  using size_type = std::size_t;
  using edge_record = detail::graph_edge<E, Vertex>;

private:
  std::vector<std::vector<edge_record>> m_adjacency;
  size_type m_edge_count{};

public:
  /**
   * @brief Constructs an empty graph with no vertices.
   *
   * @pre None.
   * @post \c vertex_count() and \c edge_count() are zero.
   */
  constexpr graph() noexcept = default;

  /**
   * @brief Constructs a graph pre-populated with \p n isolated vertices.
   *
   * @param n Number of isolated vertices to create.
   *
   * @pre None.
   * @post \c vertex_count() equals \p n, \c edge_count() is zero, and vertex IDs
   *       \c 0 through \c n-1 are valid.
   */
  explicit constexpr graph(size_type const n) noexcept : m_adjacency(n) {}

  /**
   * @brief Number of vertices in the graph.
   *
   * @return Count of vertices.
   *
   * @pre None.
   * @post None. The graph is not modified.
   */
  [[nodiscard]] constexpr auto vertex_count() const noexcept -> size_type {
    return m_adjacency.size();
  }

  /**
   * @brief Number of directed edges in the graph.
   *
   * @return Count of directed edges.
   *
   * @pre None.
   * @post None. The graph is not modified.
   */
  [[nodiscard]] constexpr auto edge_count() const noexcept -> size_type {
    return m_edge_count;
  }

  /**
   * @brief Whether the graph has no vertices.
   *
   * @return \c true when \c vertex_count() is zero.
   *
   * @pre None.
   * @post None. The graph is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_adjacency.empty();
  }

  /**
   * @brief Largest number of vertices the graph could ever hold.
   *
   * @return The maximum size of the backing adjacency vector.
   *
   * @pre None.
   * @post None. The graph is not modified.
   */
  [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
    return m_adjacency.max_size();
  }

  /**
   * @brief Reserves storage for at least \p n vertices.
   *
   * @param n Minimum vertex capacity to reserve.
   *
   * @pre None.
   * @post Capacity admits at least \p n vertices; \c vertex_count() is unchanged.
   */
  constexpr auto reserve_vertices(size_type const n) noexcept -> void {
    m_adjacency.reserve(n);
  }

  /**
   * @brief Releases unused capacity in the adjacency lists.
   *
   * @pre None.
   * @post \c vertex_count() and \c edge_count() are unchanged; capacity may
   *       shrink.
   */
  constexpr auto shrink_to_fit() noexcept -> void {
    m_adjacency.shrink_to_fit();
    for (auto& list : m_adjacency) {
      list.shrink_to_fit();
    }
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Graph to exchange state with.
   *
   * @pre None.
   * @post This graph holds \p other's former vertices and edges and vice versa.
   */
  constexpr auto swap(graph& other) noexcept -> void {
    using std::swap;
    m_adjacency.swap(other.m_adjacency);
    swap(m_edge_count, other.m_edge_count);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First graph.
   * @param b Second graph.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend constexpr auto swap(graph& a, graph& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Removes all vertices and edges.
   *
   * @pre None.
   * @post \c vertex_count() and \c edge_count() are zero.
   */
  constexpr auto clear() noexcept -> void {
    m_adjacency.clear();
    m_edge_count = 0;
  }

  /**
   * @brief Adds an isolated vertex and returns its ID.
   *
   * @return The ID of the newly created vertex.
   *
   * @pre None.
   * @post \c vertex_count() grew by one; the returned ID is valid with no
   *       outgoing edges, and previously issued IDs stay valid.
   */
  constexpr auto add_vertex() noexcept -> vertex_type {
    auto const id{static_cast<vertex_type>(m_adjacency.size())};
    m_adjacency.emplace_back();
    return id;
  }

  /**
   * @brief Whether \p v is a valid vertex ID.
   *
   * @param v Vertex ID to test.
   *
   * @return \c true when \p v is less than \c vertex_count().
   *
   * @pre None.
   * @post None. The graph is not modified.
   */
  [[nodiscard]] constexpr auto contains(vertex_type const v) const noexcept -> bool {
    return static_cast<size_type>(v) < m_adjacency.size();
  }

  /**
   * @brief Adds a directed edge \p from to \p to carrying \p data.
   *
   * Available when \p E is not \c void. For an undirected edge, call it once each
   * way. Parallel edges and self-loops are allowed.
   *
   * @tparam Edge Edge payload type, defaulted to \p E.
   * @param from Source vertex ID.
   * @param to Target vertex ID.
   * @param data Payload stored on the edge, moved in.
   *
   * @return Nothing on success, or \c container_error::out_of_range when either
   *         endpoint is invalid.
   *
   * @pre None. Both endpoints are bounds-checked.
   * @post On success \c edge_count() grew by one and \p from gained an edge to
   *       \p to; on failure the graph is unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename Edge = E>
    requires(!std::is_void_v<Edge>)
  constexpr auto add_edge(vertex_type const from, vertex_type const to, Edge data) noexcept
    -> std::expected<void, container_error> {
    if (!contains(from) || !contains(to)) {
      return std::unexpected{container_error::out_of_range};
    }
    m_adjacency[from].push_back(edge_record{to, std::move(data)});
    ++m_edge_count;
    return {};
  }

  /**
   * @brief Adds a directed edge \p from to \p to (payload-free variant).
   *
   * Available when \p E is \c void. For an undirected edge, call it once each
   * way. Parallel edges and self-loops are allowed.
   *
   * @tparam Edge Edge payload type, defaulted to \p E.
   * @param from Source vertex ID.
   * @param to Target vertex ID.
   *
   * @return Nothing on success, or \c container_error::out_of_range when either
   *         endpoint is invalid.
   *
   * @pre None. Both endpoints are bounds-checked.
   * @post On success \c edge_count() grew by one and \p from gained an edge to
   *       \p to; on failure the graph is unchanged.
   *
   * @complexity Amortised \c O(1).
   */
  template <typename Edge = E>
    requires std::is_void_v<Edge>
  constexpr auto add_edge(vertex_type const from, vertex_type const to) noexcept
    -> std::expected<void, container_error> {
    if (!contains(from) || !contains(to)) {
      return std::unexpected{container_error::out_of_range};
    }
    m_adjacency[from].push_back(edge_record{to});
    ++m_edge_count;
    return {};
  }

  /**
   * @brief Removes the first edge from \p from to \p to, if present.
   *
   * @param from Source vertex ID.
   * @param to Target vertex ID.
   *
   * @return \c true on a removal, \c false when no such edge existed, or
   *         \c container_error::out_of_range when \p from is invalid.
   *
   * @pre None. \p from is bounds-checked.
   * @post On \c true \c edge_count() shrank by one and one \p from to \p to edge
   *       is gone; otherwise the graph is unchanged.
   *
   * @complexity \c O(out_degree(from)).
   */
  constexpr auto remove_edge(vertex_type const from, vertex_type const to) noexcept
    -> std::expected<bool, container_error> {
    if (!contains(from)) {
      return std::unexpected{container_error::out_of_range};
    }
    auto& list{m_adjacency[from]};
    auto const it{std::find_if(list.begin(), list.end(), [&](edge_record const& e) {
      return e.target == to;
    })};
    if (it == list.end()) {
      return false;
    }
    list.erase(it);
    --m_edge_count;
    return true;
  }

  /**
   * @brief Whether an edge from \p from to \p to exists.
   *
   * @param from Source vertex ID.
   * @param to Target vertex ID.
   *
   * @return \c true when at least one \p from to \p to edge exists; \c false when
   *         \p from is invalid or no such edge exists.
   *
   * @pre None. \p from is bounds-checked.
   * @post None. The graph is not modified.
   *
   * @complexity \c O(out_degree(from)).
   */
  [[nodiscard]] constexpr auto
  has_edge(vertex_type const from, vertex_type const to) const noexcept -> bool {
    if (!contains(from)) {
      return false;
    }
    auto const& list{m_adjacency[from]};
    return std::any_of(list.begin(), list.end(), [&](edge_record const& e) {
      return e.target == to;
    });
  }

  /**
   * @brief Outgoing edges of \p v as a contiguous span.
   *
   * @param v Vertex whose outgoing edges are requested.
   *
   * @return Span over \p v's edge records, empty when \p v is invalid.
   *
   * @pre None. \p v is bounds-checked.
   * @post None. The span stays valid until the next mutation of \p v's edge list.
   */
  [[nodiscard]] constexpr auto edges_of(vertex_type const v
  ) const noexcept -> std::span<edge_record const> {
    if (!contains(v)) {
      return {};
    }
    return std::span<edge_record const>{m_adjacency[v].data(), m_adjacency[v].size()};
  }

  /**
   * @brief Lazy range over the target vertex IDs of \p v's outgoing edges.
   *
   * A convenience view over \c edges_of for traversals that only need the
   * neighbour IDs, not the payloads.
   *
   * @param v Vertex whose neighbours are requested.
   *
   * @return Lazy range yielding each outgoing edge's target, empty when \p v is
   *         invalid.
   *
   * @pre None. \p v is bounds-checked.
   * @post None. The view stays valid until the next mutation of \p v's edge list.
   */
  [[nodiscard]] constexpr auto neighbors(vertex_type const v) const noexcept {
    return edges_of(v)
           | std::views::transform([](edge_record const& e) noexcept { return e.target; });
  }

  /**
   * @brief Out-degree of \p v.
   *
   * @param v Vertex whose out-degree is requested.
   *
   * @return Number of outgoing edges of \p v, or \c container_error::out_of_range
   *         when \p v is invalid.
   *
   * @pre None. \p v is bounds-checked.
   * @post None. The graph is not modified.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto out_degree(vertex_type const v
  ) const noexcept -> std::expected<size_type, container_error> {
    if (!contains(v)) {
      return std::unexpected{container_error::out_of_range};
    }
    return m_adjacency[v].size();
  }

  /**
   * @brief Lazy range over every vertex ID in ascending order.
   *
   * Typed as \p Vertex so it plugs into algorithms expecting the natural index
   * type. The graph deliberately does not itself model \c std::ranges::range:
   * "iterate a graph" is ambiguous (vertices? edges? a traversal?), so callers
   * pick \c vertices() or \c edges_of explicitly.
   *
   * @return Lazy range yielding every valid vertex ID, ascending.
   *
   * @pre None.
   * @post None. The graph is not modified.
   */
  [[nodiscard]] constexpr auto vertices() const noexcept {
    return std::views::iota(vertex_type{0}, static_cast<vertex_type>(m_adjacency.size()));
  }

  /**
   * @brief Whether \p a and \p b have the same vertices and edges.
   *
   * Compares adjacency lists positionally, so edges are equal only when they
   * appear in the same per-vertex insertion order.
   *
   * @param a First graph.
   * @param b Second graph.
   *
   * @return \c true when both have equal vertex and edge counts and identical
   *         adjacency lists.
   *
   * @pre None.
   * @post None. Neither graph is modified.
   *
   * @complexity \c O(vertices + edges).
   */
  [[nodiscard]] friend auto operator==(graph const& a, graph const& b) noexcept -> bool
    requires(std::is_void_v<E> || std::equality_comparable<E>)
  {
    if (a.m_adjacency.size() != b.m_adjacency.size()) {
      return false;
    }
    if (a.m_edge_count != b.m_edge_count) {
      return false;
    }
    for (size_type i{0}; i < a.m_adjacency.size(); ++i) {
      auto const& la{a.m_adjacency[i]};
      auto const& lb{b.m_adjacency[i]};
      if (la.size() != lb.size()) {
        return false;
      }
      for (size_type j{0}; j < la.size(); ++j) {
        if (la[j].target != lb[j].target) {
          return false;
        }
        if constexpr (!std::is_void_v<E>) {
          if (!(la[j].data == lb[j].data)) {
            return false;
          }
        }
      }
    }
    return true;
  }
};

}  // namespace nexenne::container
