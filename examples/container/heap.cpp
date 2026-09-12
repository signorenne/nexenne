/**
 * @file
 * @brief heap as a min-priority event scheduler: process the earliest first.
 *
 * A min-heap (via std::greater) keeps the smallest event time on top, so popping
 * yields the events in chronological order regardless of insertion order.
 *
 * Expected output:
 *
 * \code
 * processing order: 10 20 30 40 50
 * \endcode
 */

#include <functional>
#include <print>

#include <nexenne/container/heap.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::heap<int, std::greater<int>> schedule;
  for (int const time : {50, 10, 30, 20, 40}) {
    schedule.push(time);
  }

  std::print("processing order:");
  while (!schedule.empty()) {
    auto const next{schedule.pop()};
    std::print(" {}", *next);
  }
  std::println("");
  return 0;
}
