/**
 * @file
 * @brief countdown polling timer over a manual clock.
 *
 * A countdown runs against a target duration and ticks true exactly once on the
 * running-to-expired transition, then keeps tracking overrun. The manual clock
 * keeps the output deterministic.
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/countdown.hpp>
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

  clk::advance(80ms);  // total 120ms, past the 100ms target
  std::println("at 120ms: expired tick fires once: {}", cd.tick());
  std::println("again (no second fire): {}", cd.tick());
  std::println("overrun: {} ms", cd.overrun<std::chrono::milliseconds>().count());
  // at 40ms: remaining 60 ms, progress 40%, expired tick: false
  // at 120ms: expired tick fires once: true
  // again (no second fire): false
  // overrun: 20 ms
  return 0;
}
