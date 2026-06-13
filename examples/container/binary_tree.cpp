/**
 * @file
 * @brief binary_tree as an ordered unique set: insert out of order, read sorted.
 *
 * A binary search tree keeps its elements ordered by the comparator, so an
 * in-order walk yields them ascending regardless of insertion order, and
 * membership is O(h).
 */

#include <print>

#include <nexenne/container/binary_tree.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::binary_tree<int> scores;
  for (int s : {42, 17, 88, 17, 5, 63}) {  // 17 appears twice
    scores.insert(s);
  }

  std::print("sorted unique scores:");
  for (int const s : scores) {  // in-order: ascending
    std::print(" {}", s);
  }
  std::println("");
  std::println("count: {}", scores.size());
  std::println("has 63: {}", scores.contains(63));

  scores.erase(42);
  std::print("after erasing 42:");
  for (int const s : scores) {
    std::print(" {}", s);
  }
  std::println("");
  // sorted unique scores: 5 17 42 63 88
  // count: 5
  // has 63: true
  // after erasing 42: 5 17 63 88
  return 0;
}
