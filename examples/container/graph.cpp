/**
 * @file
 * @brief graph as a weighted dependency graph: vertices, edges, neighbour walk.
 *
 * Vertices are dense integer ids; each directed edge carries a payload (here a
 * weight). edges_of and neighbors hand back contiguous views for an algorithm to
 * sweep without copying.
 */

#include <print>

#include <nexenne/container/graph.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::graph<int> roads{4};  // 4 cities, edges weighted by distance
  nexenne::utility::discard(roads.add_edge(0, 1, 5));
  nexenne::utility::discard(roads.add_edge(0, 2, 3));
  nexenne::utility::discard(roads.add_edge(1, 3, 2));
  nexenne::utility::discard(roads.add_edge(2, 3, 7));

  std::println("cities: {}, roads: {}", roads.vertex_count(), roads.edge_count());

  int total_from_0{0};
  for (auto const& e : roads.edges_of(0)) {
    total_from_0 += e.data;
  }
  std::println("total distance leaving city 0: {}", total_from_0);
  std::println("city 0 out-degree: {}", *roads.out_degree(0));
  std::println("road 1 -> 3 exists: {}", roads.has_edge(1, 3));
  // cities: 4, roads: 4
  // total distance leaving city 0: 8
  // city 0 out-degree: 2
  // road 1 -> 3 exists: true
  return 0;
}
