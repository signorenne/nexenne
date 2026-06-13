/**
 * @file
 * @brief stopwatch start/pause/resume/lap over a manual clock.
 *
 * Every nexenne::chrono type is templated on its clock; here a manual_clock is
 * advanced by hand so the output is deterministic (a real program would use the
 * default std::chrono::steady_clock and let wall time pass).
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/stopwatch.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(30ms);
  auto const lap1{sw.lap()};
  clk::advance(20ms);
  sw.pause();           // freeze the accumulator
  clk::advance(500ms);  // time passes while paused: not counted
  sw.resume();
  clk::advance(10ms);

  std::println(
    "lap 1: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(*lap1).count()
  );
  std::println("total elapsed: {} ms", sw.elapsed<std::chrono::milliseconds>().count());
  std::println("formatted: {}", sw);  // stopwatch has a std::formatter
  // lap 1: 30 ms
  // total elapsed: 60 ms
  // formatted: 00s:060ms
  return 0;
}
