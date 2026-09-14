/**
 * @file
 * @brief stopwatch start/pause/resume/lap over a manual clock.
 *
 * Every nexenne::chrono type is templated on its clock; here a manual_clock is
 * advanced by hand so the output is deterministic (a real program would use the
 * default std::chrono::steady_clock and let wall time pass).
 *
 * The program walks two steps:
 *
 * 1. A 30 ms lap, then 20 ms more before pause() freezes the accumulator: the
 *    500 ms that pass while paused are not counted, and 10 ms more follow the
 *    resume().
 * 2. The stopwatch prints through its std::formatter and through an operator<<
 *    that gives the same text.
 *
 * Expected output:
 *
 * \code
 * lap 1: 30 ms
 * total elapsed: 60 ms
 * formatted: 00s:060ms
 * streamed: 00s:060ms
 * \endcode
 */

#include <chrono>
#include <iostream>
#include <print>

#include <nexenne/chrono/format.hpp>
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
  sw.pause();
  clk::advance(500ms);
  sw.resume();
  clk::advance(10ms);

  std::println(
    "lap 1: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(*lap1).count()
  );
  std::println("total elapsed: {} ms", sw.elapsed<std::chrono::milliseconds>().count());
  std::println("formatted: {}", sw);
  std::cout << "streamed: " << sw << '\n';
  return 0;
}
