/**
 * @file
 * @brief union_find for connected components: unite the edges, count the islands.
 *
 * union_find partitions [0, n) into disjoint sets. unite() merges the sets of two
 * nodes; find() returns a set's representative root; connected() and size_of()
 * query membership and set size - all in near-constant amortised time, far
 * cheaper than re-running a flood fill per query. This tour builds a small graph
 * edge by edge, watches the component count fall, distinguishes a real merge from
 * a redundant edge, peeks at the roots, and shows the out_of_range error path.
 */

#include <print>
#include <utility>

#include <nexenne/container/error.hpp>
#include <nexenne/container/union_find.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::union_find_u32 graph{6};  // nodes 0..5, all singletons
  std::println("start: nodes {}, components {}", graph.size(), graph.count());

  // unite returns whether it actually merged two distinct sets. The (3,4)-(4,3)
  // pair shows the difference: the first joins, the second is redundant.
  for (auto const& [a, b] : {std::pair{0u, 1u}, {1u, 2u}, {3u, 4u}, {4u, 3u}}) {
    auto const merged{graph.unite(a, b)};
    std::println("  unite({}, {}): merged {}, components now {}", a, b, *merged, graph.count());
  }

  std::println("0 and 2 connected: {}", *graph.connected(0, 2));
  std::println("0 and 3 connected: {}", *graph.connected(0, 3));
  std::println("size of 0's component: {}", *graph.size_of(0));

  // find returns the set's representative. Two nodes are in the same set exactly
  // when their roots match; node 5 was never united, so it is its own root.
  std::println("root(2) == root(0): {}", *graph.find(2) == *graph.find(0));
  std::println("node 5 is its own root: {}", *graph.find(5) == 5);

  // The fallible API guards out-of-range indices instead of reading out of
  // bounds: node 99 does not exist, so the query reports an error.
  if (auto const r{graph.connected(0, 99)}; !r) {
    std::println("connected(0, 99): {}", cn::to_string(r.error()));  // out_of_range
  }

  // reset re-partitions into n fresh singletons, reusing the storage.
  graph.reset(3);
  std::println("after reset(3): nodes {}, components {}", graph.size(), graph.count());
  // start: nodes 6, components 6
  //   unite(0, 1): merged true, components now 5
  //   unite(1, 2): merged true, components now 4
  //   unite(3, 4): merged true, components now 3
  //   unite(4, 3): merged false, components now 3
  // 0 and 2 connected: true
  // 0 and 3 connected: false
  // size of 0's component: 3
  // root(2) == root(0): true
  // node 5 is its own root: true
  // connected(0, 99): out_of_range
  // after reset(3): nodes 3, components 3
  return 0;
}
