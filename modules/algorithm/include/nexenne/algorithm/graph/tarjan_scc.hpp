#pragma once

/**
 * @file
 * @brief Strongly connected components of a directed graph (Tarjan).
 *
 * An iterative Tarjan pass assigns each vertex an SCC label, emitting component
 * IDs in reverse topological order of the condensation DAG. The explicit work
 * stack keeps deep graphs off the call stack.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <nexenne/container/graph.hpp>

namespace nexenne::algorithm {

/**
 * @brief Result of tarjan_scc: per-vertex labels and a component count.
 *
 * @tparam V Unsigned-integer vertex ID type.
 */
template <std::unsigned_integral V>
struct scc_result {
  std::vector<V> labels;          ///< Component identifier of each vertex.
  std::size_t num_components{0};  ///< Number of distinct components.
};

/**
 * @brief Labels every vertex of \p g with its strongly connected component.
 *
 * Runs Tarjan's algorithm: each vertex gets a DFS index and a low-link, and an
 * SCC is peeled off the path stack whenever a root vertex's low-link equals its
 * index. The output mirrors \c connected_components for ergonomics.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @param g Directed graph to analyse.
 *
 * @return An \c scc_result whose \c labels[v] is the component identifier of
 *         vertex \c v and whose \c num_components is the count of components.
 *
 * @pre None.
 * @post \c labels has size \c g.vertex_count(); two vertices share a label if and
 *       only if they are mutually reachable, every label lies in
 *       \c [0, num_components), and labels are assigned in reverse topological
 *       order of the condensation DAG. \p g is not modified.
 *
 * @complexity \c O(V + E) time and \c O(V) auxiliary space.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto tarjan_scc(nexenne::container::graph<E, V> const& g) -> scc_result<V> {
  auto const n{g.vertex_count()};

  auto constexpr undef{std::numeric_limits<V>::max()};

  auto index{std::vector<V>(n, undef)};
  auto lowlink{std::vector<V>(n, V{0})};
  auto on_stack{std::vector<std::uint8_t>(n, 0)};  // bool-as-byte
  auto labels{std::vector<V>(n, V{0})};

  auto path{std::vector<V>{}};
  path.reserve(n);

  // DFS work frame: vertex + the edge index we left off at.
  struct frame {
    V u;
    std::size_t edge_i;
  };

  auto work{std::vector<frame>{}};
  work.reserve(n);

  auto next_index{V{0}};
  auto next_comp{V{0}};

  for (V root{0}; root < n; ++root) {
    if (index[root] != undef) {
      continue;
    }

    // Initialise the DFS tree rooted at root.
    index[root] = next_index;
    lowlink[root] = next_index;
    ++next_index;
    path.push_back(root);
    on_stack[root] = 1;
    work.push_back(frame{.u = root, .edge_i = 0});

    while (!work.empty()) {
      auto& [u, ei]{work.back()};
      auto const edges{g.edges_of(u)};

      // Either finish exploring children we've already started (the previous
      // call returned), or step into the next one.
      auto descended{false};
      while (ei < edges.size()) {
        auto const v{edges[ei].target};
        ++ei;
        if (index[v] == undef) {
          // Tree edge: descend.
          index[v] = next_index;
          lowlink[v] = next_index;
          ++next_index;
          path.push_back(v);
          on_stack[v] = 1;
          work.push_back(frame{.u = v, .edge_i = 0});
          descended = true;
          break;
        }
        if (on_stack[v]) {
          // Back edge to an ancestor still on the path: pull the ancestor's
          // index into our lowlink.
          if (index[v] < lowlink[u]) {
            lowlink[u] = index[v];
          }
        }
        // Cross/forward edge to a settled SCC: ignore.
      }

      if (descended) {
        continue;
      }

      // Done with u. If it's an SCC root, peel.
      if (lowlink[u] == index[u]) {
        while (true) {
          auto const w{path.back()};
          path.pop_back();
          on_stack[w] = 0;
          labels[w] = next_comp;
          if (w == u) {
            break;
          }
        }
        ++next_comp;
      }

      auto const finished_u{u};
      work.pop_back();
      if (!work.empty()) {
        // Propagate lowlink up to the parent.
        auto& parent{work.back()};
        if (lowlink[finished_u] < lowlink[parent.u]) {
          lowlink[parent.u] = lowlink[finished_u];
        }
      }
    }
  }

  return scc_result<V>{
    .labels = std::move(labels),
    .num_components = static_cast<std::size_t>(next_comp),
  };
}

}  // namespace nexenne::algorithm
