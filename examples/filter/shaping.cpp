/**
 * @file
 * @brief Control and discrete-input shaping filters.
 *
 * A slew limiter ramps a step instead of jumping, a debounce rejects a one
 * sample glitch on a digital input, a hysteresis Schmitt trigger holds its
 * state across a noisy threshold, and a glitch filter suppresses a short pulse.
 *
 * Each stateful push is bound to a named value before printing: argument
 * evaluation order is unspecified in C++, so feeding one filter several times
 * inside a single call would print the results in an undefined order.
 */

#include <print>

#include <nexenne/filter/debounce.hpp>
#include <nexenne/filter/glitch.hpp>
#include <nexenne/filter/hysteresis.hpp>
#include <nexenne/filter/slew.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  // Slew limited to 5 units per sample ramps from 0 toward a step of 100.
  auto sl{flt::slew{5.0}};
  static_cast<void>(sl.push(0.0));
  std::print("slew(5) toward 100 ->");
  for (auto n{0}; n < 4; ++n) {
    std::print(" {:.0f}", sl.push(100.0));
  }
  std::println("");

  // Debounce needs 3 consecutive equal reads. A lone true is ignored; three
  // steady trues switch the stable output on the third.
  auto db{flt::debounce<bool, 3>{}};
  for (auto const r : {false, false, false}) {
    static_cast<void>(db.push(r));
  }
  std::println("debounce(3): lone glitch true -> {}", db.push(true));
  db.reset();
  for (auto const r : {false, false, false}) {
    static_cast<void>(db.push(r));
  }
  auto const d1{db.push(true)};
  auto const d2{db.push(true)};
  auto const d3{db.push(true)};
  std::println("debounce(3): three steady trues -> {} {} {}", d1, d2, d3);

  // Hysteresis with a 20..25 deadband holds state between the thresholds.
  auto hy{flt::hysteresis{20.0, 25.0}};
  auto const h1{hy.push(26.0)};  // above high, latches true
  auto const h2{hy.push(22.0)};  // in the deadband, holds true
  auto const h3{hy.push(19.0)};  // below low, latches false
  std::println("hysteresis[20,25]: 26 -> {}, 22 -> {} (held), 19 -> {}", h1, h2, h3);

  // Glitch filter of width 3 suppresses a one sample pulse.
  auto gl{flt::glitch<bool, 3>{false}};
  static_cast<void>(gl.push(true));  // pending, not yet accepted
  std::println("glitch(3): 1-sample pulse accepted? {}", gl.push(false));
}
