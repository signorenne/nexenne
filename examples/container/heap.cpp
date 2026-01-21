/**
 * @file
 * @brief heap as a min-priority event scheduler: process the earliest first.
 *
 * A min-heap (via std::greater) keeps the smallest event time on top, so popping
 * yields the events in chronological order regardless of insertion order.
 */

#include <functional>
#include <print>

#include <nexenne/container/heap.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::heap<int, std::greater<int>> schedule;  // min-heap on event time
  for (int const time : {50, 10, 30, 20, 40}) {
    schedule.push(time);
  }

  std::print("processing order:");
  while (!schedule.empty()) {
    auto const next{schedule.pop()};  // earliest event each time
    std::print(" {}", *next);
  }
  std::println("");
  // processing order: 10 20 30 40 50
  return 0;
}
