/**
 * @file
 * @brief indexed_priority_queue as an event scheduler: reschedule and cancel.
 *
 * Each scheduled event gets a handle; a min-heap keyed by time always yields the
 * soonest event. Rescheduling (update) and cancelling (erase) are O(log n) by
 * handle, with no scan to find the old entry.
 */

#include <functional>
#include <print>

#include <nexenne/container/indexed_priority_queue.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  // Min-heap on the firing time: the soonest event is on top.
  cn::indexed_priority_queue<int, std::greater<int>> schedule;
  schedule.push(100);               // event A at t=100
  auto const b{schedule.push(50)};  // event B at t=50
  schedule.push(75);                // event C at t=75

  std::println("next event time: {}", *schedule.top());  // 50

  static_cast<void>(schedule.update(b, 200));                       // reschedule B to t=200
  std::println("after rescheduling B, next: {}", *schedule.top());  // 75

  std::println("draining in time order:");
  while (!schedule.empty()) {
    std::println("  fire at {}", *schedule.pop());
  }
  // next event time: 50
  // after rescheduling B, next: 75
  // draining in time order:
  //   fire at 75
  //   fire at 100
  //   fire at 200
  return 0;
}
