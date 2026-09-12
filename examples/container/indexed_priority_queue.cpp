/**
 * @file
 * @brief indexed_priority_queue as an event scheduler: reschedule and cancel.
 *
 * Each scheduled event gets a stable handle from push; a min-heap keyed by time
 * always yields the soonest. The handle is what sets this apart from
 * std::priority_queue: update() reschedules and erase() cancels in O(log n) by
 * identity, with no scan to find the entry, and value_at() reads a queued item
 * back without popping. The tour walks five steps:
 *
 * 1. Events A, B and C are pushed at t=100, 50 and 75; std::greater makes the
 *    soonest event the top, so B leads.
 * 2. value_at reads A's time by handle without disturbing the heap.
 * 3. update reschedules B to t=200, re-heapifying in place while B's handle
 *    stays valid; C becomes the top.
 * 4. erase cancels C by handle with no rescan to locate it, and a later value_at
 *    on that stale handle is a checked not_found error, never undefined.
 * 5. Draining pops in time order, and a pop on the drained queue reports empty.
 *
 * Expected output:
 *
 * \code
 * next event time: 50
 * top handle is B: true
 * A is scheduled for t=100
 * after rescheduling B, next: 75
 * C is queued: true
 * after cancelling C, queued: false
 * value_at(cancelled C): not_found
 * draining in time order:
 *   fire at 100
 *   fire at 200
 * pop when empty: empty
 * \endcode
 */

#include <functional>
#include <print>

#include <nexenne/container/error.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::indexed_priority_queue<int, std::greater<int>> schedule;
  auto const a{schedule.push(100)};
  auto const b{schedule.push(50)};
  auto const c{schedule.push(75)};

  std::println("next event time: {}", *schedule.top());
  std::println("top handle is B: {}", *schedule.top_handle() == b);

  std::println("A is scheduled for t={}", **schedule.value_at(a));

  nexenne::utility::ignore(schedule.update(b, 200));
  std::println("after rescheduling B, next: {}", *schedule.top());

  std::println("C is queued: {}", schedule.contains(c));
  nexenne::utility::ignore(schedule.erase(c));
  std::println("after cancelling C, queued: {}", schedule.contains(c));

  if (auto const r{schedule.value_at(c)}; !r) {
    std::println("value_at(cancelled C): {}", cn::to_string(r.error()));
  }

  std::println("draining in time order:");
  while (!schedule.empty()) {
    std::println("  fire at {}", *schedule.pop());
  }
  std::println("pop when empty: {}", cn::to_string(schedule.pop().error()));
  return 0;
}
