/**
 * @file
 * @brief countdown polling timer over a manual clock.
 *
 * A countdown runs against a target duration and ticks true exactly once on the
 * running-to-expired transition, then keeps tracking overrun. The manual clock
 * keeps the output deterministic.
 *
 * The program walks two steps:
 *
 * 1. A 100 ms countdown at 40 ms: 60 ms remain, progress reads 40%, tick() is
 *    still false, and the state prints by name.
 * 2. At 120 ms, past the target: tick() fires once and never again, and
 *    overrun() reports the 20 ms beyond the target.
 *
 * Expected output:
 *
 * \code
 * at 40ms: remaining 60 ms, progress 40%, expired tick: false
 * state: running
 * at 120ms: expired tick fires once: true
 * again (no second fire): false
 * overrun: 20 ms
 * state: expired
 * \endcode
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/format.hpp>
#include <nexenne/chrono/manual_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct cd_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::countdown<clk> cd{100ms};
  cd.start();

  clk::advance(40ms);
  std::println(
    "at 40ms: remaining {} ms, progress {:.0f}%, expired tick: {}",
    cd.remaining<std::chrono::milliseconds>().count(),
    cd.progress() * 100.0,
    cd.tick()
  );
  std::println("state: {}", cd.current_state());

  clk::advance(80ms);  // total 120ms, past the 100ms target
  std::println("at 120ms: expired tick fires once: {}", cd.tick());
  std::println("again (no second fire): {}", cd.tick());
  std::println("overrun: {} ms", cd.overrun<std::chrono::milliseconds>().count());
  std::println("state: {}", cd.current_state());
  // at 40ms: remaining 60 ms, progress 40%, expired tick: false
  // state: running
  // at 120ms: expired tick fires once: true
  // again (no second fire): false
  // overrun: 20 ms
  // state: expired
  return 0;
}
