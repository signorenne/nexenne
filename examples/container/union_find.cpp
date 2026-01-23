/**
 * @file
 * @brief union_find for connected components: unite the edges, count the islands.
 *
 * Six nodes and three edges form two multi-node components and one lone node;
 * uniting each edge's endpoints leaves the component count and membership ready
 * to query in near-constant time.
 */

#include <print>
#include <utility>

#include <nexenne/container/union_find.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::union_find_u32 graph{6};  // nodes 0..5, all singletons
  for (auto const& [a, b] : {std::pair{0u, 1u}, {1u, 2u}, {3u, 4u}}) {
    static_cast<void>(graph.unite(a, b));  // edge: connect the endpoints
  }

  std::println("nodes: {}, components: {}", graph.size(), graph.count());
  std::println("0 and 2 connected: {}", *graph.connected(0, 2));
  std::println("0 and 3 connected: {}", *graph.connected(0, 3));
  std::println("size of 0's component: {}", *graph.size_of(0));
  // nodes: 6, components: 3
  // 0 and 2 connected: true
  // 0 and 3 connected: false
  // size of 0's component: 3
  return 0;
}
