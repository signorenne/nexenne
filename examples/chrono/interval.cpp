/**
 * @file
 * @brief interval: a polling-driven periodic timer with catch-up semantics.
 *
 * Where countdown fires once, interval fires repeatedly: one tick per period.
 * Call tick() in your loop; each call returns true at most once per crossed
 * boundary and advances the internal anchor by exactly one period. That single
 * step per call lets the caller choose the policy: drain it in a while loop to
 * process every missed period now, or call tick() once per iteration to spread
 * the backlog over later iterations (start() again re-anchors and drops it). The
 * manual clock makes the timing deterministic.
 *
 * The program walks five steps:
 *
 * 1. start() anchors at now and zeroes the count; at 40 ms tick() is false and
 *    remaining() reports the wait.
 * 2. At 110 ms one period has elapsed: a single tick fires and the anchor moves
 *    to the 200 ms boundary. tick() is sequenced before tick_count() is read,
 *    since argument evaluation order is unspecified.
 * 3. At 460 ms the boundaries at 200, 300 and 400 are due; a while loop drains
 *    all three, the right choice for a fixed-step physics update that must not
 *    skip steps.
 * 4. After a restart and a 450 ms overshoot, one tick() call consumes just one
 *    boundary and leaves the rest pending.
 * 5. next_tick_at() gives the absolute boundary time, a sort key for scheduling
 *    several intervals in a priority queue.
 *
 * Expected output:
 *
 * \code
 * at 40ms: tick false, remaining 60 ms
 * at 110ms: tick true, count 1
 * at 460ms: drained 3 missed ticks, count now 4
 * skip policy: one tick consumes just 1 boundary
 * next boundary is at 660 ms since epoch
 * \endcode
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/interval.hpp>
#include <nexenne/chrono/manual_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct interval_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::interval<clk> iv{100ms};
  iv.start();

  clk::advance(40ms);
  std::println(
    "at 40ms: tick {}, remaining {} ms",
    iv.tick(),
    iv.remaining<std::chrono::milliseconds>().count()
  );

  clk::advance(70ms);
  auto const fired{iv.tick()};
  std::println("at 110ms: tick {}, count {}", fired, iv.tick_count());

  clk::advance(350ms);
  int caught_up{0};
  while (iv.tick()) {
    ++caught_up;
  }
  std::println("at 460ms: drained {} missed ticks, count now {}", caught_up, iv.tick_count());

  iv.start();
  clk::advance(450ms);
  std::println("skip policy: one tick consumes just {} boundary", iv.tick() ? 1 : 0);

  auto const next{iv.next_tick_at()};
  std::println(
    "next boundary is at {} ms since epoch",
    std::chrono::duration_cast<std::chrono::milliseconds>(next.time_since_epoch()).count()
  );

  return 0;
}
