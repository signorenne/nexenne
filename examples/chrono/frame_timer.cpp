/**
 * @file
 * @brief frame_timer per-frame delta and moving-average FPS over a manual clock.
 *
 * A frame_timer reports the delta since the previous tick and a moving-average
 * FPS over a fixed window. Advancing the manual clock by a fixed step makes the
 * averaged FPS deterministic.
 *
 * The program walks two steps:
 *
 * 1. The first tick() only establishes the baseline and returns a zero delta.
 * 2. Eight 16 ms frames (about 60 fps) fill the 8-frame window, so fps() is the
 *    window average, 1 / 16 ms.
 *
 * Expected output:
 *
 * \code
 * frames: 9
 * fps (avg over window): 62.5
 * \endcode
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/frame_timer.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct ft_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::frame_timer<8, clk> ft;

  nexenne::utility::ignore(ft.tick());
  for (int i{0}; i < 8; ++i) {
    clk::advance(16ms);
    nexenne::utility::ignore(ft.tick());
  }

  std::println("frames: {}", ft.frame_count());
  std::println("fps (avg over window): {:.1f}", ft.fps());
  return 0;
}
