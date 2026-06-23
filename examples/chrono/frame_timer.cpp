/**
 * @file
 * @brief frame_timer per-frame delta and moving-average FPS over a manual clock.
 *
 * A frame_timer reports the delta since the previous tick and a moving-average
 * FPS over a fixed window. Advancing the manual clock by a fixed step makes the
 * averaged FPS deterministic.
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/frame_timer.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct ft_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::frame_timer<clk, 8> ft;  // 8-frame averaging window

  nexenne::utility::discard(ft.tick());  // first tick: establishes the baseline (dt 0)
  for (int i{0}; i < 8; ++i) {
    clk::advance(16ms);  // ~60 fps frames (16 ms each)
    nexenne::utility::discard(ft.tick());
  }

  std::println("frames: {}", ft.frame_count());
  std::println("fps (avg over window): {:.1f}", ft.fps());
  // frames: 9
  // fps (avg over window): 62.5
  return 0;
}
