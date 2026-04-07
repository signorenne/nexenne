/**
 * @file
 * @brief ring_buffer: a fixed-capacity circular FIFO, two ways to handle "full".
 *
 * ring_buffer<T, N> is a queue laid out in one inline array of N slots with a
 * wrapping [head, tail) window; it never allocates. The choice that defines its
 * use is what happens at capacity: push refuses (returns an error) so a producer
 * can apply back-pressure, while push_overwrite drops the oldest element so the
 * buffer is always the most recent N. This tour shows both, plus pop, front/back
 * peeks, and the full/empty boundaries.
 */

#include <print>

#include <nexenne/container/error.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  // --- A bounded queue (push refuses when full: back-pressure) -----------------
  cn::ring_buffer<int, 3> queue;
  std::println("empty {}, full {}, cap {}", queue.empty(), queue.full(), queue.capacity());

  for (int const job : {1, 2, 3}) {
    nexenne::utility::discard(queue.push(job));  // succeeds while there is room
  }
  std::println("after 3 pushes: size {}, full {}", queue.size(), queue.full());

  // The 4th push fails instead of overwriting: the result carries the reason, so
  // the producer can wait and retry rather than lose data.
  if (auto const r{queue.push(4)}; !r) {
    std::println("push(4) rejected: {}", cn::to_string(r.error()));  // full
  }

  // front/back peek without removing; pop removes from the front (FIFO).
  std::println("front {}, back {}", *queue.front(), *queue.back());
  std::println("pop {}", *queue.pop());  // 1, the oldest
  std::println("now there is room, push(4): {}", queue.push(4).has_value());

  std::print("draining FIFO:");
  while (!queue.empty()) {
    std::print(" {}", *queue.pop());
  }
  std::println("");
  // pop on an empty buffer is a checked error, not undefined behaviour.
  std::println("pop when empty: {}", cn::to_string(queue.pop().error()));

  // --- A rolling window (push_overwrite drops the oldest, never fails) ----------
  cn::ring_buffer<int, 3> recent;  // keep only the last 3 readings
  for (int const reading : {20, 21, 23, 22, 25}) {
    recent.push_overwrite(reading);  // evicts the oldest once full
  }

  std::print("last {} readings (oldest first):", recent.size());
  int sum{0};
  for (int const reading : recent) {  // FIFO iteration over the live window
    std::print(" {}", reading);
    sum += reading;
  }
  std::println("");
  std::println("rolling average: {}", sum / static_cast<int>(recent.size()));
  // empty true, full false, cap 3
  // after 3 pushes: size 3, full true
  // push(4) rejected: full
  // front 1, back 3
  // pop 1
  // now there is room, push(4): true
  // draining FIFO: 2 3 4
  // pop when empty: empty
  // last 3 readings (oldest first): 23 22 25
  // rolling average: 23
  return 0;
}
