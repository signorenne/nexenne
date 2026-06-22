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

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  // 1. Slew limited to 5 units per sample ramps from 0 toward a step of 100. The
  // first push seeds directly; every later push moves by at most the rate, so a
  // step becomes a ramp - exactly what protects a motor or LED from a jolt.
  auto sl{flt::slew{5.0}};
  static_cast<void>(sl.push(0.0));
  std::print("1. slew(5) toward 100 ->");
  for (auto n{0}; n < 4; ++n) {
    std::print(" {:.0f}", sl.push(100.0));
  }
  std::println("");

  // 2. The rate knob trades response speed for gentleness. A larger max rate
  // reaches the target in fewer samples but with a harsher transition. Each
  // limiter starts primed at 0 and chases a step of 20; we print the samples
  // taken to arrive.
  std::println("2. slew rate vs. samples-to-target (step of 20):");
  for (auto const rate : {2.0, 5.0, 10.0}) {
    auto s{flt::slew{rate}};
    s.reset(0.0);  // prime at zero so the first push is already rate-limited
    auto steps{0};
    while (s.push(20.0) < 20.0 && steps < 100) {
      ++steps;
    }
    std::println("   rate {:4.1f}: {} samples", rate, steps + 1);
  }

  // 3. Debounce needs 3 consecutive equal reads. A lone true is ignored; three
  // steady trues switch the stable output on the third. This is the classic
  // mechanical-switch contact-bounce reject.
  auto db{flt::debounce<bool, 3>{}};
  for (auto const r : {false, false, false}) {
    static_cast<void>(db.push(r));
  }
  std::println("3. debounce(3): lone glitch true -> {}", db.push(true));
  db.reset();
  for (auto const r : {false, false, false}) {
    static_cast<void>(db.push(r));
  }
  auto const d1{db.push(true)};
  auto const d2{db.push(true)};
  auto const d3{db.push(true)};
  std::println("   debounce(3): three steady trues -> {} {} {}", d1, d2, d3);

  // 4. Glitch filter vs. debounce: the glitch filter is stricter. Debounce
  // counts consecutive matches, but the glitch filter also cancels a pending
  // candidate the moment the signal returns to the stable value, so a bouncing
  // line that flickers back never promotes. We feed both 0,1,0,1,1,1.
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

  // 5. Hysteresis with a 20..25 deadband holds state between the thresholds, so
  // a signal hovering near the boundary never chatters. The wider the deadband,
  // the more noise immunity (but the more lag before it flips).
  auto hy{flt::hysteresis{20.0, 25.0}};
  auto const h1{hy.push(26.0)};  // above high, latches true
  auto const h2{hy.push(22.0)};  // in the deadband, holds true
  auto const h3{hy.push(19.0)};  // below low, latches false
  std::println("5. hysteresis[20,25]: 26 -> {}, 22 -> {} (held), 19 -> {}", h1, h2, h3);

  // 6. The deadband width sets the chatter immunity. A signal wobbling 23 +/- 4
  // crosses a single 23 threshold repeatedly, but a [20,25] band swallows the
  // wobble. We count output flips for a narrow vs. a wide band on the same noisy
  // sweep.
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

  // 7. timed_debounce settles on elapsed time, not a sample count, so it stays
  // robust to a varying sample rate. It returns a settled level only once a
  // changed level has persisted for the period; until then update() yields
  // nullopt. We feed a 20 ms period with timestamps in milliseconds.
  std::println("7. timed_debounce(20ms): a changed level promotes after the period:");
  auto td{flt::timed_debounce<std::chrono::milliseconds>{std::chrono::milliseconds{20}}};
  using ms = std::chrono::milliseconds;
  static_cast<void>(td.update(ms{0}, false));  // first sample seeds stable=false
  auto const t1{td.update(ms{5}, true)};       // candidate starts, not yet settled
  auto const t2{td.update(ms{10}, true)};      // still within the period
  auto const t3{td.update(ms{30}, true)};      // 25 ms held, promotes to true
  std::println(
    "   t=5 settled? {}  t=10 settled? {}  t=30 settled? {}",
    t1.has_value(),
    t2.has_value(),
    t3.has_value() && *t3
  );
  return 0;
}
