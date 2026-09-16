/**
 * @file
 * @brief Control and discrete-input shaping filters.
 *
 * A slew limiter ramps a step instead of jumping, a debounce rejects a one
 * sample glitch on a digital input, a glitch filter suppresses a short pulse, a
 * hysteresis Schmitt trigger holds its state across a noisy threshold, and a
 * timed_debounce settles on elapsed time rather than a sample count. The tour
 * also probes the knobs (rate, threshold, deadband) and reset() behaviour.
 *
 * Each stateful push is bound to a named value before printing: argument
 * evaluation order is unspecified in C++, so feeding one filter several times
 * inside a single call would print the results in an undefined order.
 *
 * The program walks seven steps:
 *
 * 1. A slew limited to 5 units per sample turns a step of 100 into a ramp, the
 *    shape that protects a motor or an LED from a jolt; the first push seeds.
 * 2. The rate knob trades response speed for gentleness: each limiter starts
 *    primed at 0, so its first push is already rate-limited, and the samples to
 *    reach a step of 20 are printed.
 * 3. A debounce of 3 ignores a lone true and switches on the third steady true,
 *    the classic contact-bounce reject.
 * 4. glitch(3) beside debounce(3) on a bouncing line 0,1,0,1,1,1: the glitch
 *    filter cancels a pending candidate the moment the line returns to the
 *    stable value, so the flicker never promotes, and its output matches
 *    debounce with the same count.
 * 5. Hysteresis with a [20, 25] deadband latches high above 25, holds inside the
 *    band, and latches low below 20, so a hovering signal never chatters.
 * 6. The deadband width sets the chatter immunity: a wobble around 23 flips a
 *    single-threshold comparator repeatedly but barely moves a [20, 26] band.
 * 7. timed_debounce with a 20 ms period: the first update seeds the stable level,
 *    a changed level yields nothing while it is younger than the period, and
 *    promotes once it has held for 20 ms or more.
 */

#include <array>
#include <chrono>
#include <print>
#include <utility>

#include <nexenne/filter/debounce.hpp>
#include <nexenne/filter/glitch.hpp>
#include <nexenne/filter/hysteresis.hpp>
#include <nexenne/filter/slew.hpp>
#include <nexenne/filter/timed_debounce.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  auto sl{flt::slew{5.0}};
  nexenne::utility::ignore(sl.push(0.0));
  std::print("1. slew(5) toward 100 ->");
  for (auto n{0}; n < 4; ++n) {
    std::print(" {:.0f}", sl.push(100.0));
  }
  std::println("");

  std::println("2. slew rate vs. samples-to-target (step of 20):");
  for (auto const rate : {2.0, 5.0, 10.0}) {
    auto s{flt::slew{rate}};
    s.reset(0.0);
    auto steps{0};
    while (s.push(20.0) < 20.0 && steps < 100) {
      ++steps;
    }
    std::println("   rate {:4.1f}: {} samples", rate, steps + 1);
  }

  auto db{flt::debounce<bool, 3>{}};
  for (auto const r : {false, false, false}) {
    nexenne::utility::ignore(db.push(r));
  }
  std::println("3. debounce(3): lone glitch true -> {}", db.push(true));
  db.reset();
  for (auto const r : {false, false, false}) {
    nexenne::utility::ignore(db.push(r));
  }
  auto const d1{db.push(true)};
  auto const d2{db.push(true)};
  auto const d3{db.push(true)};
  std::println("   debounce(3): three steady trues -> {} {} {}", d1, d2, d3);

  std::println("4. glitch(3) vs debounce(3) on a bouncing line 0,1,0,1,1,1:");
  auto gl{flt::glitch<bool, 3>{false}};
  auto gd{flt::debounce<bool, 3>{false}};
  std::print("   glitch  :");
  for (auto const r : {false, true, false, true, true, true}) {
    std::print(" {:d}", gl.push(r));
  }
  std::println("");
  std::print("   debounce:");
  for (auto const r : {false, true, false, true, true, true}) {
    std::print(" {:d}", gd.push(r));
  }
  std::println("");

  auto hy{flt::hysteresis{20.0, 25.0}};
  auto const h1{hy.push(26.0)};
  auto const h2{hy.push(22.0)};
  auto const h3{hy.push(19.0)};
  std::println("5. hysteresis[20,25]: 26 -> {}, 22 -> {} (held), 19 -> {}", h1, h2, h3);

  std::println("6. hysteresis deadband vs. output flips on a wobbly signal:");
  constexpr double wobble[]{23, 27, 22, 26, 21, 27, 19, 24, 23, 26};
  constexpr std::array bands{std::pair{23.0, 23.0}, std::pair{20.0, 26.0}};
  for (auto const& [lo, hi] : bands) {
    auto h{flt::hysteresis{lo, hi}};
    auto flips{0};
    auto prev{h.value()};
    for (auto const x : wobble) {
      auto const now{h.push(x)};
      if (now != prev) {
        ++flips;
      }
      prev = now;
    }
    std::println("   band [{:.0f},{:.0f}]: {} flips", lo, hi, flips);
  }

  std::println("7. timed_debounce(20ms): a changed level promotes after the period:");
  auto td{flt::timed_debounce<std::chrono::milliseconds>{std::chrono::milliseconds{20}}};
  using ms = std::chrono::milliseconds;
  nexenne::utility::ignore(td.update(ms{0}, false));
  auto const t1{td.update(ms{5}, true)};
  auto const t2{td.update(ms{10}, true)};
  auto const t3{td.update(ms{30}, true)};
  std::println(
    "   t=5 settled? {}  t=10 settled? {}  t=30 settled? {}",
    t1.has_value(),
    t2.has_value(),
    t3.has_value() && *t3
  );
  return 0;
}
